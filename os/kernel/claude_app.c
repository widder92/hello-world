/*
 * Das Claude-Fenster: ein Gesprächsverlauf, darunter eine Eingabezeile.
 *
 * Zwei Tasks teilen sich die Arbeit: Der Fenster-Task zeichnet und nimmt
 * Eingaben entgegen; für jede Frage startet er einen Helfer-Task, der mit
 * dem Server spricht (das kann eine Weile dauern) und die Antwort in den
 * Verlauf hängt. So bleibt das Fenster währenddessen bedienbar.
 */
#include "apps.h"
#include "claude.h"
#include "console.h"
#include "cpu.h"
#include "gui.h"
#include "input.h"
#include "memory.h"
#include "sound.h"
#include "string.h"
#include "task.h"
#include "time.h"

#define WIDTH        (app->width)       /* Größe des Fensters - veränderbar */
#define HEIGHT       (app->height)
#define START_WIDTH  664
#define START_HEIGHT 484
#define MARGIN       12
#define LINE_H       (FONT_H + 2)
#define INPUT_Y      (HEIGHT - 60)
#define INPUT_H      30
#define STATUS_Y     (HEIGHT - 22)
#define COLUMNS      ((WIDTH - 2 * MARGIN) / FONT_W)
#define VISIBLE      ((INPUT_Y - 16) / LINE_H)
#define INPUT_MAX    1000

#define C_BACKGROUND RGB(246, 244, 240)
#define C_TEXT       RGB(34, 38, 48)
#define C_MUTED      RGB(120, 124, 134)
#define C_USER       RGB(48, 104, 196)
#define C_CLAUDE     RGB(190, 92, 58)
#define C_ERROR      RGB(196, 56, 56)
#define C_FIELD      RGB(255, 255, 255)
#define C_BORDER     RGB(190, 192, 198)

typedef enum { ENTRY_USER, ENTRY_CLAUDE, ENTRY_NOTE, ENTRY_ERROR } entry_kind_t;

typedef struct entry {
    entry_kind_t kind;
    struct entry *next;
    char text[];
} entry_t;

typedef struct {
    window_t *window;
    int width, height;
    claude_chat_t *chat;
    entry_t *first, *last;              /* Verlauf; wächst nur am Ende */

    char input[INPUT_MAX];
    size_t input_len;
    int scroll;                         /* Zeilen über dem Ende; 0 = mitlaufen */

    /* Zusammenarbeit mit dem Helfer-Task */
    bool busy;
    uint64_t busy_since;
    char *question;
    bool confirm_pending;
    int confirm_answer;                 /* 0 offen, 1 erlaubt, 2 abgelehnt */
    bool closing;
    bool dirty;
} app_t;

static void add_entry(app_t *app, entry_kind_t kind, const char *text)
{
    size_t len = strlen(text);
    entry_t *e = kmalloc(sizeof(*e) + len + 1);
    if (!e)
        return;
    e->kind = kind;
    e->next = NULL;
    memcpy(e->text, text, len + 1);

    uint64_t flags = irq_save();
    if (app->last)
        app->last->next = e;
    else
        app->first = e;
    app->last = e;
    app->scroll = 0;                    /* Neues ist sofort zu sehen */
    app->dirty = true;
    irq_restore(flags);
}

/* --- Helfer-Task ------------------------------------------------------------------- */

static void ui_text(void *ctx, const char *text)
{
    add_entry(ctx, ENTRY_CLAUDE, text);
}

static void ui_note(void *ctx, const char *text)
{
    add_entry(ctx, ENTRY_NOTE, text);
}

static bool ui_confirm(void *ctx, const char *command)
{
    app_t *app = ctx;
    char note[300];

    /* Der vollständige Befehl steht im Verlauf, bevor gefragt wird. */
    ksnprintf(note, sizeof(note), "Claude möchte ausführen: %s", command);
    add_entry(app, ENTRY_NOTE, note);
    sound_play(SOUND_NOTIFY);            /* Claude wartet auf eine Erlaubnis */
    app->confirm_answer = 0;
    app->confirm_pending = true;
    app->dirty = true;
    while (app->confirm_answer == 0 && !app->closing)
        task_sleep(50);
    app->confirm_pending = false;
    app->dirty = true;
    return app->confirm_answer == 1;
}

static void worker(void *arg)
{
    app_t *app = arg;
    claude_ui_t ui = { ui_text, ui_note, ui_confirm, app };
    char error[200];

    if (!claude_send(app->chat, app->question, &ui, error, sizeof(error))) {
        add_entry(app, ENTRY_ERROR, error);
        sound_play(SOUND_ERROR);
    }
    kfree(app->question);
    app->question = NULL;
    app->busy = false;
    app->dirty = true;
}

/* --- Zeichnen ------------------------------------------------------------------------ */

/*
 * Geht alle Zeilen des Verlaufs durch (umbrochen auf die Fensterbreite) und
 * zeichnet diejenigen im Bereich [first, first + VISIBLE). Liefert die
 * Gesamtzahl der Zeilen; mit s == NULL wird nur gezählt.
 */
static int layout(app_t *app, surface_t *s, int first)
{
    int line = 0;
    for (const entry_t *e = app->first; e; e = e->next) {
        const char *label = e->kind == ENTRY_USER ? "Du"
                          : e->kind == ENTRY_CLAUDE ? "Claude" : NULL;
        color_t color = e->kind == ENTRY_NOTE ? C_MUTED
                      : e->kind == ENTRY_ERROR ? C_ERROR : C_TEXT;
        if (label) {
            if (s && line >= first && line < first + VISIBLE)
                gfx_text(s, MARGIN, 8 + (line - first) * LINE_H, label,
                         e->kind == ENTRY_USER ? C_USER : C_CLAUDE, true);
            line++;
        }
        const char *text = e->text;
        char buffer[640];
        while (gfx_wrap_line(&text, COLUMNS, buffer, sizeof(buffer))) {
            if (s && line >= first && line < first + VISIBLE)
                gfx_text(s, MARGIN, 8 + (line - first) * LINE_H, buffer, color,
                         false);
            line++;
        }
        line++;                         /* Leerzeile zwischen den Beiträgen */
    }
    return line;
}

static rect_t allow_rect(const app_t *app)
{
    return rect_make(WIDTH - MARGIN - 220, INPUT_Y, 104, INPUT_H);
}

static rect_t deny_rect(const app_t *app)
{
    return rect_make(WIDTH - MARGIN - 108, INPUT_Y, 108, INPUT_H);
}

static void draw(app_t *app)
{
    surface_t *s = window_surface(app->window);
    char line[160];

    gfx_fill(s, 0, 0, WIDTH, HEIGHT, C_BACKGROUND);

    int total = layout(app, NULL, 0);
    int max_scroll = total > VISIBLE ? total - VISIBLE : 0;
    if (app->scroll > max_scroll)
        app->scroll = max_scroll;
    layout(app, s, max_scroll - app->scroll);

    if (app->confirm_pending) {
        /* Rückfrage: Claude möchte etwas ausführen, das den Rechner verändert */
        gfx_text(s, MARGIN, INPUT_Y + (INPUT_H - FONT_H) / 2,
                 "Diesen Befehl ausführen lassen?  (J / N)", C_TEXT, true);
        ui_button(s, allow_rect(app), "Erlauben", true);
        ui_button(s, deny_rect(app), "Ablehnen", false);
    } else {
        gfx_fill(s, MARGIN, INPUT_Y, WIDTH - 2 * MARGIN, INPUT_H, C_FIELD);
        gfx_frame(s, MARGIN, INPUT_Y, WIDTH - 2 * MARGIN, INPUT_H, C_BORDER);

        /* Passt die Eingabe nicht ins Feld, ist ihr Ende zu sehen. */
        const char *shown = app->input;
        int width = gfx_text_width(shown) / FONT_W;
        while (width > COLUMNS - 3) {
            shown++;
            while (((uint8_t)*shown & 0xC0) == 0x80)
                shown++;
            width--;
        }
        int x = gfx_text(s, MARGIN + 8, INPUT_Y + (INPUT_H - FONT_H) / 2, shown,
                         app->busy ? C_MUTED : C_TEXT, false);
        if (!app->busy)
            gfx_fill(s, x + 1, INPUT_Y + 6, 2, INPUT_H - 12, C_TEXT);
    }

    if (app->busy)
        ksnprintf(line, sizeof(line), "Claude arbeitet ... %llu s",
                  (time_ms() - app->busy_since) / 1000);
    else if (!claude_has_key())
        ksnprintf(line, sizeof(line), "Kein API-Schlüssel hinterlegt");
    else
        ksnprintf(line, sizeof(line),
                  "Enter: senden   Bild auf/ab: blättern   /neu: neues Gespräch   %s",
                  claude_model());
    gfx_text(s, MARGIN, STATUS_Y, line, C_MUTED, false);

    window_invalidate(app->window, 0, 0, WIDTH, HEIGHT);
    app->dirty = false;
}

/* --- Eingaben ---------------------------------------------------------------------------- */

static void new_chat(app_t *app)
{
    uint64_t flags = irq_save();
    entry_t *e = app->first;
    app->first = app->last = NULL;
    irq_restore(flags);
    while (e) {
        entry_t *next = e->next;
        kfree(e);
        e = next;
    }
    claude_chat_free(app->chat);
    app->chat = claude_chat_new();
    add_entry(app, ENTRY_NOTE, "Neues Gespräch. Womit kann ich helfen?");
}

static void submit(app_t *app)
{
    if (app->input_len == 0 || app->busy)
        return;
    app->input[app->input_len] = '\0';

    if (strcmp(app->input, "/neu") == 0) {
        new_chat(app);
    } else if (memcmp(app->input, "/key ", 5) == 0) {
        claude_set_key(app->input + 5);
        add_entry(app, ENTRY_NOTE,
                  claude_has_key() ? "Der Schlüssel ist für diese Sitzung hinterlegt."
                                   : "Das sieht nicht nach einem gültigen Schlüssel aus.");
    } else if (app->input[0] == '/') {
        add_entry(app, ENTRY_NOTE, "Bekannt sind: /neu und /key <schlüssel>");
    } else {
        size_t len = app->input_len + 1;
        app->question = kmalloc(len);
        if (app->question && app->chat) {
            memcpy(app->question, app->input, len);
            add_entry(app, ENTRY_USER, app->input);
            app->busy = true;
            app->busy_since = time_ms();
            if (task_create("claude-anfrage", worker, app, NULL) < 0) {
                app->busy = false;
                kfree(app->question);
                app->question = NULL;
                add_entry(app, ENTRY_ERROR, "Zu viele Tasks - bitte etwas schließen.");
            }
        }
    }
    /* Die Eingabe (vielleicht ein Schlüssel) nicht im Speicher stehen lassen */
    memset(app->input, 0, sizeof(app->input));
    app->input_len = 0;
    app->dirty = true;
}

static void key_pressed(app_t *app, int key)
{
    if (key == KEY_PAGE_UP) {
        app->scroll += VISIBLE / 2;
    } else if (key == KEY_PAGE_DOWN) {
        app->scroll = app->scroll > VISIBLE / 2 ? app->scroll - VISIBLE / 2 : 0;
    } else if (app->confirm_pending) {
        if (key == 'j' || key == 'J')
            app->confirm_answer = 1;
        else if (key == 'n' || key == 'N' || key == 27)
            app->confirm_answer = 2;
    } else if (app->busy) {
        return;                         /* während Claude arbeitet, ruht die Eingabe */
    } else if (key == '\n') {
        submit(app);
    } else if (key == '\b') {
        while (app->input_len > 0 &&
               ((uint8_t)app->input[app->input_len - 1] & 0xC0) == 0x80)
            app->input_len--;
        if (app->input_len > 0)
            app->input_len--;
        app->input[app->input_len] = '\0';
    } else if (key >= 32 && key < 0x800 && key != 127 &&
               app->input_len + 3 < INPUT_MAX) {
        if (key < 0x80) {
            app->input[app->input_len++] = (char)key;
        } else {
            app->input[app->input_len++] = (char)(0xC0 | (key >> 6));
            app->input[app->input_len++] = (char)(0x80 | (key & 0x3F));
        }
        app->input[app->input_len] = '\0';
    }
    app->dirty = true;
}

void claude_app(void *arg)
{
    (void)arg;
    app_t *app = kzalloc(sizeof(*app));
    if (!app)
        return;
    app->width = START_WIDTH;
    app->height = START_HEIGHT;
    app->window = window_create("Claude", WIDTH, HEIGHT);
    if (app->window)
        window_set_resizable(app->window, 420, 260);
    app->chat = claude_chat_new();
    if (!app->window || !app->chat) {
        if (app->window)
            window_destroy(app->window);
        claude_chat_free(app->chat);
        kfree(app);
        return;
    }

    if (claude_has_key() || claude_load_key()) {
        add_entry(app, ENTRY_NOTE,
                  "Hallo! Stell mir eine Frage oder bitte mich, etwas in HelloOS "
                  "nachzusehen oder zu erledigen.");
    } else {
        add_entry(app, ENTRY_NOTE,
                  "Um mit Claude zu sprechen, braucht HelloOS einen API-Schlüssel von "
                  "console.anthropic.com (die Nutzung wird dort abgerechnet).\n\n"
                  "Am einfachsten: Lege auf dem Datenträger, von dem HelloOS startet, im "
                  "Hauptverzeichnis eine Textdatei claude.key an, die nur den Schlüssel "
                  "enthält, und öffne dieses Fenster neu.\n\n"
                  "Oder tippe hier: /key <dein Schlüssel>");
    }

    for (;;) {
        if (app->dirty)
            draw(app);

        event_t ev;
        if (!window_get_event(app->window, &ev, 250)) {
            if (app->busy)
                app->dirty = true;      /* Sekundenanzeige weiterzählen */
            continue;
        }
        if (ev.type == EV_CLOSE)
            break;
        if (ev.type == EV_RESIZE && window_resize(app->window, ev.x, ev.y)) {
            app->width = ev.x;
            app->height = ev.y;
            app->dirty = true;
        }
        if (ev.type == EV_KEY) {
            key_pressed(app, ev.key);
        } else if (ev.type == EV_MOUSE_UP && app->confirm_pending) {
            if (rect_contains(allow_rect(app), ev.x, ev.y))
                app->confirm_answer = 1;
            else if (rect_contains(deny_rect(app), ev.x, ev.y))
                app->confirm_answer = 2;
        }
    }

    /* Fenster sofort schließen; auf einen noch laufenden Helfer aber warten. */
    app->closing = true;
    window_destroy(app->window);
    while (app->busy)
        task_sleep(100);

    for (entry_t *e = app->first; e; ) {
        entry_t *next = e->next;
        kfree(e);
        e = next;
    }
    claude_chat_free(app->chat);
    kfree(app);
}
