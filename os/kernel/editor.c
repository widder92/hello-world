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
#include "string.h"
#include "time.h"

#define COLS         90
#define ROWS         27
#define GUTTER       (6 * FONT_W)               /* Zeilennummern */
#define LINE_H       (FONT_H + 1)
#define TEXT_X       (GUTTER + 6)
#define TEXT_H       (ROWS * LINE_H + 8)
#define STATUS_H     26
#define WIDTH        (TEXT_X + COLS * FONT_W + 6)
#define HEIGHT       (TEXT_H + STATUS_H)
#define TAB          4
#define TEXT_MAX     (4u * 1024 * 1024)

#define C_BACKGROUND RGB(252, 252, 250)
#define C_GUTTER     RGB(236, 236, 232)
#define C_NUMBER     RGB(150, 152, 158)
#define C_TEXT       RGB(34, 38, 48)
#define C_LINE       RGB(240, 244, 252)         /* Zeile mit der Schreibmarke */
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
    char name[FS_NAME_MAX];
    char *text;
    size_t len, capacity;
    size_t cursor;
    int wanted_column;                  /* Spalte, die Pfeil hoch/runter anpeilen */
    size_t top;                         /* Anfang der ersten sichtbaren Zeile */
    int left;                           /* erste sichtbare Spalte */
    bool modified;
    bool crlf;                          /* Datei hatte Windows-Zeilenenden */
    edit_mode_t mode;
    char input[FS_NAME_MAX];
    size_t input_len;
    char message[96];
    bool warning;
} editor_t;

static void set_message(editor_t *e, bool warning, const char *text)
{
    strlcpy(e->message, text, sizeof(e->message));
    e->warning = warning;
}

/* --- Bewegen im Text ------------------------------------------------------------------ */

static bool continuation(char c)
{
    return ((uint8_t)c & 0xC0) == 0x80;
}

static size_t line_start(const editor_t *e, size_t pos)
{
    while (pos > 0 && e->text[pos - 1] != '\n')
        pos--;
    return pos;
}

static size_t line_end(const editor_t *e, size_t pos)
{
    while (pos < e->len && e->text[pos] != '\n')
        pos++;
    return pos;
}

static size_t next_char(const editor_t *e, size_t pos)
{
    if (pos < e->len)
        pos++;
    while (pos < e->len && continuation(e->text[pos]))
        pos++;
    return pos;
}

static size_t previous_char(const editor_t *e, size_t pos)
{
    if (pos > 0)
        pos--;
    while (pos > 0 && continuation(e->text[pos]))
        pos--;
    return pos;
}

/* Bildschirmspalte einer Position (Tabulatoren springen zur nächsten Marke). */
static int column_of(const editor_t *e, size_t pos)
{
    int column = 0;
    for (size_t p = line_start(e, pos); p < pos; p++) {
        if (e->text[p] == '\t')
            column += TAB - column % TAB;
        else if (!continuation(e->text[p]))
            column++;
    }
    return column;
}

/* Position in der Zeile ab start, die der Spalte am nächsten kommt. */
static size_t position_at(const editor_t *e, size_t start, int wanted)
{
    int column = 0;
    size_t p = start;
    while (p < e->len && e->text[p] != '\n' && column < wanted) {
        column += e->text[p] == '\t' ? TAB - column % TAB : 1;
        p = next_char(e, p);
    }
    return p;
}

static int line_number(const editor_t *e, size_t pos)
{
    int line = 0;
    for (size_t p = 0; p < pos; p++) {
        if (e->text[p] == '\n')
            line++;
    }
    return line;
}

/* Verschiebt den Ausschnitt so, dass die Schreibmarke zu sehen ist. */
static void scroll_to_cursor(editor_t *e)
{
    size_t start = line_start(e, e->cursor);
    if (start < e->top) {
        e->top = start;
    } else {
        int below = 0;
        for (size_t p = e->top; p < start; p++) {
            if (e->text[p] == '\n')
                below++;
        }
        while (below >= ROWS) {
            e->top = line_end(e, e->top) + 1;
            below--;
        }
    }
    int column = column_of(e, e->cursor);
    if (column < e->left)
        e->left = column > 8 ? column - 8 : 0;
    else if (column >= e->left + COLS)
        e->left = column - COLS + 8;
}

static void move_vertical(editor_t *e, int lines)
{
    size_t start = line_start(e, e->cursor);
    while (lines < 0 && start > 0) {
        start = line_start(e, start - 1);
        lines++;
    }
    while (lines > 0 && line_end(e, start) < e->len) {
        start = line_end(e, start) + 1;
        lines--;
    }
    e->cursor = position_at(e, start, e->wanted_column);
}

/* --- Ändern ---------------------------------------------------------------------------- */

static bool insert(editor_t *e, const char *bytes, size_t n)
{
    if (e->len + n > TEXT_MAX)
        return false;
    if (e->len + n + 1 > e->capacity) {
        size_t capacity = (e->len + n + 1) * 2 + 256;
        char *bigger = kmalloc(capacity);
        if (!bigger)
            return false;
        memcpy(bigger, e->text, e->len);
        kfree(e->text);
        e->text = bigger;
        e->capacity = capacity;
    }
    memmove(e->text + e->cursor + n, e->text + e->cursor, e->len - e->cursor);
    memcpy(e->text + e->cursor, bytes, n);
    e->len += n;
    e->cursor += n;
    e->modified = true;
    return true;
}

static void erase(editor_t *e, size_t from, size_t to)
{
    if (from >= to)
        return;
    memmove(e->text + from, e->text + to, e->len - to);
    e->len -= to - from;
    e->cursor = from;
    e->modified = true;
}

/* --- Laden und Speichern ---------------------------------------------------------------- */

static void reset_view(editor_t *e)
{
    e->cursor = 0;
    e->top = 0;
    e->left = 0;
    e->wanted_column = 0;
    e->modified = false;
    e->mode = MODE_EDIT;
}

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
        if (size > TEXT_MAX || binary) {
            kfree(data);
            set_message(e, true, size > TEXT_MAX ? "Die Datei ist zu groß für den Editor"
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

    kfree(e->text);
    e->text = data;
    e->len = out;
    e->capacity = size + 1;
    /* Der Name bleibt so geschrieben, wie er schon gespeichert ist. */
    const file_t *existing = fs_find(name);
    strlcpy(e->name, existing ? existing->name : name, sizeof(e->name));
    reset_view(e);
}

static bool save_file(editor_t *e)
{
    int err;
    if (e->crlf) {
        size_t lines = 0;
        for (size_t i = 0; i < e->len; i++)
            lines += e->text[i] == '\n';
        char *out = kmalloc(e->len + lines + 1);
        if (!out) {
            set_message(e, true, "Nicht genug Speicher zum Speichern");
            return false;
        }
        size_t n = 0;
        for (size_t i = 0; i < e->len; i++) {
            if (e->text[i] == '\n')
                out[n++] = '\r';
            out[n++] = e->text[i];
        }
        err = fs_write(e->name, out, n, false);
        kfree(out);
    } else {
        err = fs_write(e->name, e->text, e->len, false);
    }
    if (err != FS_OK) {
        char text[96];
        ksnprintf(text, sizeof(text), "Speichern fehlgeschlagen: %s", fs_error_text(err));
        set_message(e, true, text);
        return false;
    }
    e->modified = false;
    const file_t *f = fs_find(e->name);
    if (f && f->saved)
        set_message(e, false, "Gespeichert (auf dem USB-Stick gesichert)");
    else
        set_message(e, true, "Gespeichert - aber nur im Arbeitsspeicher, kein USB-Stick");
    return true;
}

/* --- Zeichnen -------------------------------------------------------------------------- */

static void draw_text(editor_t *e, surface_t *s)
{
    size_t cursor_line = line_start(e, e->cursor);
    int number = line_number(e, e->top) + 1;
    size_t p = e->top;

    gfx_fill(s, 0, 0, WIDTH, TEXT_H, C_BACKGROUND);
    gfx_fill(s, 0, 0, GUTTER, TEXT_H, C_GUTTER);

    for (int row = 0; row < ROWS; row++, number++) {
        int y = 4 + row * LINE_H;
        char label[12];

        if (p == cursor_line)
            gfx_fill(s, GUTTER, y, WIDTH - GUTTER, LINE_H, C_LINE);
        ksnprintf(label, sizeof(label), "%5d", number);
        gfx_text(s, 0, y, label, C_NUMBER, false);

        int column = 0;
        const char *text = e->text + p;
        const char *end = e->text + line_end(e, p);
        while (text < end) {
            if (text == e->text + e->cursor && column >= e->left)
                gfx_fill(s, TEXT_X + (column - e->left) * FONT_W, y, 2, FONT_H,
                         C_TEXT);
            uint32_t cp = utf8_next(&text);
            if (cp == '\t') {
                column += TAB - column % TAB;
                continue;
            }
            if (column >= e->left && column < e->left + COLS)
                gfx_glyph(s, TEXT_X + (column - e->left) * FONT_W, y, cp, C_TEXT,
                          false);
            column++;
        }
        if (end == e->text + e->cursor && column >= e->left)
            gfx_fill(s, TEXT_X + (column - e->left) * FONT_W, y, 2, FONT_H, C_TEXT);

        p = (size_t)(end - e->text);
        if (p >= e->len)
            break;                      /* hinter der letzten Zeile ist nichts mehr */
        p++;
    }
}

/* Die Liste zum Öffnen: alle Dateien, in Spalten. */
#define LIST_Y       64
#define LIST_COLUMN  (WIDTH / 2 - 16)
#define LIST_ROWS    ((TEXT_H - LIST_Y - 8) / LINE_H)

static rect_t list_rect(int shown)
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
        rect_t r = list_rect(shown++);
        gfx_text(s, r.x, r.y, f->name, C_LINK, false);
    }
}

static void draw(editor_t *e)
{
    surface_t *s = window_surface(e->window);
    char status[200];

    if (e->mode == MODE_OPEN)
        draw_open(e, s);
    else
        draw_text(e, s);

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
        ksnprintf(status, sizeof(status), "%s%s   Zeile %d, Spalte %d", e->name,
                  e->modified ? " *" : "", line_number(e, e->cursor) + 1,
                  column_of(e, e->cursor) + 1);
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
        if (!e->text)
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
    bool keep_column = false;

    set_message(e, false, "");
    if (mods & MOD_CTRL) {
        if (key == 19) {                /* Strg+S */
            save_file(e);
        } else if (key == 15) {         /* Strg+O */
            if (e->modified)
                set_message(e, true, "Erst speichern (Strg+S) - es gibt Änderungen");
            else
                start_open(e);
        } else if (key == KEY_HOME) {
            e->cursor = 0;
        } else if (key == KEY_END) {
            e->cursor = e->len;
        }
    } else if (key == KEY_LEFT) {
        e->cursor = previous_char(e, e->cursor);
    } else if (key == KEY_RIGHT) {
        e->cursor = next_char(e, e->cursor);
    } else if (key == KEY_UP || key == KEY_DOWN) {
        move_vertical(e, key == KEY_UP ? -1 : 1);
        keep_column = true;
    } else if (key == KEY_PAGE_UP || key == KEY_PAGE_DOWN) {
        move_vertical(e, key == KEY_PAGE_UP ? -(ROWS - 2) : ROWS - 2);
        keep_column = true;
    } else if (key == KEY_HOME) {
        e->cursor = line_start(e, e->cursor);
    } else if (key == KEY_END) {
        e->cursor = line_end(e, e->cursor);
    } else if (key == '\b') {
        erase(e, previous_char(e, e->cursor), e->cursor);
    } else if (key == KEY_DELETE) {
        erase(e, e->cursor, next_char(e, e->cursor));
    } else if (key == '\n') {
        /* Die neue Zeile übernimmt die Einrückung der alten. */
        char indent[64];
        size_t n = 0;
        indent[n++] = '\n';
        for (size_t p = line_start(e, e->cursor);
             p < e->cursor && (e->text[p] == ' ' || e->text[p] == '\t') &&
             n < sizeof(indent); p++)
            indent[n++] = e->text[p];
        insert(e, indent, n);
    } else if (key == '\t') {
        insert(e, "    ", (size_t)(TAB - column_of(e, e->cursor) % TAB));
    } else if (key >= 32 && key < 0x800 && key != 127) {
        char bytes[2];
        if (key < 0x80) {
            bytes[0] = (char)key;
            insert(e, bytes, 1);
        } else {
            bytes[0] = (char)(0xC0 | (key >> 6));
            bytes[1] = (char)(0x80 | (key & 0x3F));
            insert(e, bytes, 2);
        }
    } else {
        return;
    }

    if (!keep_column)
        e->wanted_column = column_of(e, e->cursor);
    scroll_to_cursor(e);
}

static void click_edit(editor_t *e, int x, int y)
{
    if (y >= TEXT_H)
        return;
    int row = (y - 4) / LINE_H;
    size_t start = e->top;
    for (int i = 0; i < row && line_end(e, start) < e->len; i++)
        start = line_end(e, start) + 1;
    int column = e->left + (x - TEXT_X + FONT_W / 2) / FONT_W;
    e->cursor = position_at(e, start, column > 0 ? column : 0);
    e->wanted_column = column_of(e, e->cursor);
    set_message(e, false, "");
}

static void click_open(editor_t *e, int x, int y)
{
    int shown = 0;
    for (int i = 0; i < FS_MAX_FILES && shown < 2 * LIST_ROWS; i++) {
        const file_t *f = fs_entry(i);
        if (!f)
            continue;
        if (rect_contains(list_rect(shown++), x, y)) {
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
    if (e)
        e->window = window_create("Editor", WIDTH, HEIGHT);
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
        if (ev.type == EV_CLOSE) {
            if (e->modified && e->mode != MODE_UNSAVED) {
                e->mode = MODE_UNSAVED;
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
        } else if (ev.type == EV_MOUSE_DOWN && e->mode == MODE_EDIT) {
            click_edit(e, ev.x, ev.y);
        } else if (ev.type == EV_MOUSE_DOWN && e->mode == MODE_OPEN) {
            click_open(e, ev.x, ev.y);
        }
    }

    window_destroy(e->window);
    kfree(e->text);
    kfree(e);
}
