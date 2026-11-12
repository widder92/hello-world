/*
 * Texteditor: bearbeitet eine der eigenen Dateien (siehe fs.h).
 *
 * Der Text liegt als ein zusammenhängender UTF-8-Puffer im Speicher; die
 * Schreibmarke ist eine Byte-Position darin. Zeilen werden nicht umbrochen -
 * bei langen Zeilen wandert der sichtbare Ausschnitt seitlich mit.
 */
#include "apps.h"
#include "console.h"
#include "fs.h"
#include "gui.h"
#include "input.h"
#include "memory.h"
#include "sound.h"
#include "string.h"
#include "textedit.h"

#define COLS         90
#define ROWS         27
#define STATUS_H     26
#define C_BACKGROUND RGB(252, 252, 250)
#define C_TEXT       RGB(34, 38, 48)
#define C_STATUS     RGB(44, 52, 70)
#define C_STATUS_TXT RGB(226, 230, 238)
#define C_WARN       RGB(255, 214, 102)
#define C_LINK       RGB(48, 104, 196)

typedef enum {
    MODE_EDIT,
    MODE_OPEN,                          /* Dateiname wird abgefragt */
    MODE_UNSAVED,                       /* Schließen trotz ungesicherter Änderungen? */
} edit_mode_t;

typedef struct {
    window_t *window;
    int width, text_height;             /* Fläche des Textfelds in Pixeln */
    char name[FS_NAME_MAX];
    textedit_t edit;
    bool crlf;                          /* Datei hatte Windows-Zeilenenden */
    edit_mode_t mode;
    char input[FS_NAME_MAX];
    size_t input_len;
    char message[96];
    bool warning;
} editor_t;

#define WIDTH   (e->width)
#define TEXT_H  (e->text_height)
#define HEIGHT  (TEXT_H + STATUS_H)
#define LINE_H  TEXTEDIT_LINE_H

static void set_message(editor_t *e, bool warning, const char *text)
{
    strlcpy(e->message, text, sizeof(e->message));
    e->warning = warning;
}

static bool continuation(char c)
{
    return ((uint8_t)c & 0xC0) == 0x80;
}

/* --- Laden und Speichern ---------------------------------------------------------------- */

/* Öffnet die Datei; gibt es sie noch nicht, beginnt eine leere mit diesem Namen. */
static void open_file(editor_t *e, const char *name)
{
    char *data;
    size_t size;
    int err = fs_read(name, &data, &size);

    if (err == FS_ERR_NOT_FOUND) {
        data = kmalloc(1);
        size = 0;
        err = data ? FS_OK : FS_ERR_MEMORY;
        set_message(e, false, "Neue Datei");
    } else if (err == FS_OK) {
        set_message(e, false, "");
        bool binary = false;
        for (size_t i = 0; i < size && !binary; i++)
            binary = data[i] == '\0';
        if (size > TEXTEDIT_MAX || binary) {
            kfree(data);
            set_message(e, true, size > TEXTEDIT_MAX ? "Die Datei ist zu groß für den Editor"
                                                     : "Das ist keine Textdatei");
            return;
        }
    }
    if (err != FS_OK) {
        set_message(e, true, fs_error_text(err));
        return;
    }

    /* Windows-Zeilenenden (CR LF) werden intern zu LF und beim Speichern zurück. */
    size_t out = 0;
    e->crlf = false;
    for (size_t i = 0; i < size; i++) {
        if (data[i] == '\r' && i + 1 < size && data[i + 1] == '\n') {
            e->crlf = true;
            continue;
        }
        data[out++] = data[i];
    }

    textedit_adopt(&e->edit, data, out, size + 1);
    /* Der Name bleibt so geschrieben, wie er schon gespeichert ist. */
    const file_t *existing = fs_find(name);
    strlcpy(e->name, existing ? existing->name : name, sizeof(e->name));
    e->mode = MODE_EDIT;
}

static bool save_file(editor_t *e)
{
    const textedit_t *t = &e->edit;
    int err;
    if (e->crlf) {
        size_t lines = 0;
        for (size_t i = 0; i < t->len; i++)
            lines += t->text[i] == '\n';
        char *out = kmalloc(t->len + lines + 1);
        if (!out) {
            set_message(e, true, "Nicht genug Speicher zum Speichern");
            return false;
        }
        size_t n = 0;
        for (size_t i = 0; i < t->len; i++) {
            if (t->text[i] == '\n')
                out[n++] = '\r';
            out[n++] = t->text[i];
        }
        err = fs_write(e->name, out, n, false);
        kfree(out);
    } else {
        err = fs_write(e->name, t->text, t->len, false);
    }
    if (err != FS_OK) {
        char text[96];
        ksnprintf(text, sizeof(text), "Speichern fehlgeschlagen: %s", fs_error_text(err));
        set_message(e, true, text);
        sound_play(SOUND_ERROR);
        return false;
    }
    e->edit.modified = false;
    const file_t *f = fs_find(e->name);
    if (f && f->saved)
        set_message(e, false, "Gespeichert und auf dem Datenträger gesichert");
    else
        set_message(e, true, "Gespeichert - aber nur im Arbeitsspeicher, kein Datenträger");
    return true;
}

/* --- Zeichnen -------------------------------------------------------------------------- */

/* Die Liste zum Öffnen: alle Dateien, in Spalten. */
#define LIST_Y       64
#define LIST_COLUMN  (WIDTH / 2 - 16)
#define LIST_ROWS    ((TEXT_H - LIST_Y - 8) / LINE_H)

static rect_t list_rect(const editor_t *e, int shown)
{
    return rect_make(20 + (shown / LIST_ROWS) * LIST_COLUMN,
                     LIST_Y + (shown % LIST_ROWS) * LINE_H, LIST_COLUMN - 12, LINE_H);
}

static void draw_open(editor_t *e, surface_t *s)
{
    char line[160];

    gfx_fill(s, 0, 0, WIDTH, TEXT_H, C_BACKGROUND);
    gfx_text(s, 20, 14, "Datei öffnen oder neu anlegen", C_TEXT, true);
    ksnprintf(line, sizeof(line), "Name: %s", e->input);
    int x = gfx_text(s, 20, 36, line, C_TEXT, false);
    gfx_fill(s, x + 1, 36, 2, FONT_H, C_TEXT);

    int shown = 0;
    for (int i = 0; i < FS_MAX_FILES && shown < 2 * LIST_ROWS; i++) {
        const file_t *f = fs_entry(i);
        if (!f)
            continue;
        rect_t r = list_rect(e, shown++);
        gfx_text(s, r.x, r.y, f->name, C_LINK, false);
    }
}

static void draw(editor_t *e)
{
    surface_t *s = window_surface(e->window);
    char status[200];

    if (e->mode == MODE_OPEN) {
        draw_open(e, s);
    } else {
        /* Das Textfeld füllt das Fenster nur auf ganze Zeichen genau. */
        gfx_fill(s, 0, 0, WIDTH, TEXT_H, C_BACKGROUND);
        textedit_draw(&e->edit, s, 0, 0, true);
    }

    gfx_fill(s, 0, TEXT_H, WIDTH, STATUS_H, C_STATUS);
    if (e->mode == MODE_UNSAVED) {
        ksnprintf(status, sizeof(status),
                  "Ungesicherte Änderungen:  S speichern   V verwerfen   Esc zurück");
        gfx_text(s, 8, TEXT_H + 5, status, C_WARN, true);
    } else if (e->mode == MODE_OPEN) {
        gfx_text(s, 8, TEXT_H + 5,
                 e->message[0] ? e->message
                               : "Namen tippen oder Datei anklicken   Enter öffnen   Esc zurück",
                 e->message[0] && e->warning ? C_WARN : C_STATUS_TXT, false);
    } else {
        int line, column;
        textedit_position(&e->edit, &line, &column);
        ksnprintf(status, sizeof(status), "%s%s   Zeile %d, Spalte %d", e->name,
                  e->edit.modified ? " *" : "", line, column);
        int x = gfx_text(s, 8, TEXT_H + 5, status, C_STATUS_TXT, true);
        const char *hint = e->message[0] ? e->message
                                         : "Strg+S speichern   Strg+O öffnen";
        int hint_x = WIDTH - 8 - gfx_text_width(hint);
        if (hint_x > x + 16)
            gfx_text(s, hint_x, TEXT_H + 5, hint,
                     e->message[0] && e->warning ? C_WARN : C_STATUS_TXT, false);
    }
    window_invalidate(e->window, 0, 0, WIDTH, HEIGHT);
}

/* --- Eingaben --------------------------------------------------------------------------- */

static void start_open(editor_t *e)
{
    e->mode = MODE_OPEN;
    e->input[0] = '\0';
    e->input_len = 0;
    set_message(e, false, "");
}

/* true: Das Fenster soll geschlossen werden. */
static bool key_open(editor_t *e, int key)
{
    if (key == 27) {
        if (!e->edit.text)
            return true;                /* es war noch gar nichts geöffnet */
        e->mode = MODE_EDIT;
        set_message(e, false, "");
    } else if (key == '\n') {
        if (e->input_len > 0) {
            open_file(e, e->input);
            if (e->mode == MODE_OPEN && !e->message[0])
                set_message(e, true, "Dieser Name geht nicht");
        }
    } else if (key == '\b') {
        while (e->input_len > 0 && continuation(e->input[e->input_len - 1]))
            e->input_len--;
        if (e->input_len > 0)
            e->input_len--;
        e->input[e->input_len] = '\0';
    } else if (key > 32 && key < 0x800 && key != 127 &&
               e->input_len + 3 < sizeof(e->input)) {
        if (key < 0x80) {
            e->input[e->input_len++] = (char)key;
        } else {
            e->input[e->input_len++] = (char)(0xC0 | (key >> 6));
            e->input[e->input_len++] = (char)(0x80 | (key & 0x3F));
        }
        e->input[e->input_len] = '\0';
    }
    return false;
}

static void key_edit(editor_t *e, int key, int mods)
{
    set_message(e, false, "");
    if ((mods & MOD_CTRL) && key == 19) {               /* Strg+S */
        save_file(e);
    } else if ((mods & MOD_CTRL) && key == 15) {        /* Strg+O */
        if (e->edit.modified)
            set_message(e, true, "Erst speichern (Strg+S) - es gibt Änderungen");
        else
            start_open(e);
    } else {
        textedit_key(&e->edit, key, mods);
    }
}

static void click_open(editor_t *e, int x, int y)
{
    int shown = 0;
    for (int i = 0; i < FS_MAX_FILES && shown < 2 * LIST_ROWS; i++) {
        const file_t *f = fs_entry(i);
        if (!f)
            continue;
        if (rect_contains(list_rect(e, shown++), x, y)) {
            char name[FS_NAME_MAX];
            strlcpy(name, f->name, sizeof(name));
            open_file(e, name);
            return;
        }
    }
}

void editor_app(void *arg)
{
    char *name = arg;                   /* Dateiname (kmalloc) oder NULL */
    editor_t *e = kzalloc(sizeof(*e));
    if (e) {
        textedit_init(&e->edit, COLS, ROWS);
        e->width = textedit_width(&e->edit, true);
        e->text_height = textedit_height(&e->edit);
        e->window = window_create("Editor", WIDTH, HEIGHT);
    }
    if (e && e->window)
        window_set_resizable(e->window, 46 * FONT_W, 10 * LINE_H + STATUS_H);
    if (!e || !e->window) {
        kfree(e);
        kfree(name);
        return;
    }

    start_open(e);
    if (name && *name) {
        open_file(e, name);
        if (e->mode == MODE_OPEN)       /* ließ sich nicht öffnen: Name vorbelegen */
            strlcpy(e->input, name, sizeof(e->input));
        e->input_len = strlen(e->input);
    }
    kfree(name);

    for (bool running = true; running; ) {
        draw(e);

        event_t ev;
        window_get_event(e->window, &ev, 0);
        if (ev.type == EV_RESIZE) {
            /* So viele Spalten und Zeilen, wie in die neue Fläche passen */
            int fixed = textedit_width(&e->edit, true) - e->edit.columns * FONT_W;
            if (window_resize(e->window, ev.x, ev.y)) {
                e->width = ev.x;
                e->text_height = ev.y - STATUS_H;
                textedit_resize(&e->edit, (ev.x - fixed) / FONT_W,
                                (e->text_height - 8) / LINE_H);
            }
        } else if (ev.type == EV_CLOSE) {
            if (e->edit.modified && e->mode != MODE_UNSAVED) {
                e->mode = MODE_UNSAVED;
                sound_play(SOUND_NOTIFY);
            } else {
                running = false;
            }
        } else if (ev.type == EV_KEY && e->mode == MODE_UNSAVED) {
            if (ev.key == 's' || ev.key == 'S') {
                e->mode = MODE_EDIT;
                running = !save_file(e);
            } else if (ev.key == 'v' || ev.key == 'V') {
                running = false;
            } else if (ev.key == 27) {
                e->mode = MODE_EDIT;
            }
        } else if (ev.type == EV_KEY && e->mode == MODE_OPEN) {
            running = !key_open(e, ev.key);
        } else if (ev.type == EV_KEY) {
            key_edit(e, ev.key, ev.mods);
        } else if (ev.type == EV_SCROLL && e->mode == MODE_EDIT) {
            /* Mausrad: die Schreibmarke wandert, die Ansicht folgt ihr */
            int steps = ev.y > 0 ? ev.y : -ev.y;
            for (int i = 0; i < 3 * steps && i < 60; i++)
                key_edit(e, ev.y > 0 ? KEY_UP : KEY_DOWN, 0);
        } else if (ev.type == EV_MOUSE_DOWN && e->mode == MODE_EDIT) {
            if (ev.y < TEXT_H)
                textedit_click(&e->edit, ev.x - (WIDTH - textedit_width(&e->edit, false)),
                               ev.y);
            set_message(e, false, "");
        } else if (ev.type == EV_MOUSE_DOWN && e->mode == MODE_OPEN) {
            click_open(e, ev.x, ev.y);
        }
    }

    window_destroy(e->window);
    textedit_free(&e->edit);
    kfree(e);
}
