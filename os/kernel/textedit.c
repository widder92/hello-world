#include "textedit.h"
#include "console.h"
#include "input.h"
#include "memory.h"
#include "string.h"

#define TAB          4
#define GUTTER       (6 * FONT_W)               /* Zeilennummern */
#define PAD          6

#define C_BACKGROUND RGB(252, 252, 250)
#define C_GUTTER     RGB(236, 236, 232)
#define C_NUMBER     RGB(150, 152, 158)
#define C_TEXT       RGB(34, 38, 48)
#define C_LINE       RGB(240, 244, 252)         /* Zeile mit der Schreibmarke */

void textedit_init(textedit_t *t, int columns, int rows)
{
    memset(t, 0, sizeof(*t));
    t->columns = columns;
    t->rows = rows;
}

void textedit_free(textedit_t *t)
{
    kfree(t->text);
    t->text = NULL;
    t->len = t->capacity = 0;
}

void textedit_adopt(textedit_t *t, char *text, size_t len, size_t capacity)
{
    kfree(t->text);
    t->text = text;
    t->len = len;
    t->capacity = capacity;
    t->cursor = 0;
    t->top = 0;
    t->left = 0;
    t->wanted_column = 0;
    t->modified = false;
}

bool textedit_set(textedit_t *t, const char *text, size_t len)
{
    char *copy = kmalloc(len + 1);
    if (!copy)
        return false;
    memcpy(copy, text, len);
    textedit_adopt(t, copy, len, len + 1);
    return true;
}

/* --- Bewegen im Text ------------------------------------------------------------------ */

static bool continuation(char c)
{
    return ((uint8_t)c & 0xC0) == 0x80;
}

static size_t line_start(const textedit_t *t, size_t pos)
{
    while (pos > 0 && t->text[pos - 1] != '\n')
        pos--;
    return pos;
}

static size_t line_end(const textedit_t *t, size_t pos)
{
    while (pos < t->len && t->text[pos] != '\n')
        pos++;
    return pos;
}

static size_t next_char(const textedit_t *t, size_t pos)
{
    if (pos < t->len)
        pos++;
    while (pos < t->len && continuation(t->text[pos]))
        pos++;
    return pos;
}

static size_t previous_char(const textedit_t *t, size_t pos)
{
    if (pos > 0)
        pos--;
    while (pos > 0 && continuation(t->text[pos]))
        pos--;
    return pos;
}

/* Bildschirmspalte einer Position (Tabulatoren springen zur nächsten Marke). */
static int column_of(const textedit_t *t, size_t pos)
{
    int column = 0;
    for (size_t p = line_start(t, pos); p < pos; p++) {
        if (t->text[p] == '\t')
            column += TAB - column % TAB;
        else if (!continuation(t->text[p]))
            column++;
    }
    return column;
}

/* Position in der Zeile ab start, die der Spalte am nächsten kommt. */
static size_t position_at(const textedit_t *t, size_t start, int wanted)
{
    int column = 0;
    size_t p = start;
    while (p < t->len && t->text[p] != '\n' && column < wanted) {
        column += t->text[p] == '\t' ? TAB - column % TAB : 1;
        p = next_char(t, p);
    }
    return p;
}

static int line_number(const textedit_t *t, size_t pos)
{
    int line = 0;
    for (size_t p = 0; p < pos; p++) {
        if (t->text[p] == '\n')
            line++;
    }
    return line;
}

void textedit_position(const textedit_t *t, int *line, int *column)
{
    *line = line_number(t, t->cursor) + 1;
    *column = column_of(t, t->cursor) + 1;
}

/* Verschiebt den Ausschnitt so, dass die Schreibmarke zu sehen ist. */
static void scroll_to_cursor(textedit_t *t)
{
    size_t start = line_start(t, t->cursor);
    if (start < t->top) {
        t->top = start;
    } else {
        int below = 0;
        for (size_t p = t->top; p < start; p++) {
            if (t->text[p] == '\n')
                below++;
        }
        while (below >= t->rows) {
            t->top = line_end(t, t->top) + 1;
            below--;
        }
    }
    int column = column_of(t, t->cursor);
    if (column < t->left)
        t->left = column > 8 ? column - 8 : 0;
    else if (column >= t->left + t->columns)
        t->left = column - t->columns + 8;
}

void textedit_resize(textedit_t *t, int columns, int rows)
{
    t->columns = columns < 8 ? 8 : columns;
    t->rows = rows < 2 ? 2 : rows;
    if (t->text)
        scroll_to_cursor(t);
}

static void move_vertical(textedit_t *t, int lines)
{
    size_t start = line_start(t, t->cursor);
    while (lines < 0 && start > 0) {
        start = line_start(t, start - 1);
        lines++;
    }
    while (lines > 0 && line_end(t, start) < t->len) {
        start = line_end(t, start) + 1;
        lines--;
    }
    t->cursor = position_at(t, start, t->wanted_column);
}

/* --- Ändern ---------------------------------------------------------------------------- */

static bool insert(textedit_t *t, const char *bytes, size_t n)
{
    if (t->len + n > TEXTEDIT_MAX)
        return false;
    if (t->len + n + 1 > t->capacity) {
        size_t capacity = (t->len + n + 1) * 2 + 256;
        char *bigger = kmalloc(capacity);
        if (!bigger)
            return false;
        if (t->len)
            memcpy(bigger, t->text, t->len);
        kfree(t->text);
        t->text = bigger;
        t->capacity = capacity;
    }
    memmove(t->text + t->cursor + n, t->text + t->cursor, t->len - t->cursor);
    memcpy(t->text + t->cursor, bytes, n);
    t->len += n;
    t->cursor += n;
    t->modified = true;
    return true;
}

static void erase(textedit_t *t, size_t from, size_t to)
{
    if (from >= to)
        return;
    memmove(t->text + from, t->text + to, t->len - to);
    t->len -= to - from;
    t->cursor = from;
    t->modified = true;
}

bool textedit_key(textedit_t *t, int key, int mods)
{
    bool keep_column = false;

    if (mods & MOD_CTRL) {
        if (key == KEY_HOME)
            t->cursor = 0;
        else if (key == KEY_END)
            t->cursor = t->len;
        else
            return false;
    } else if (key == KEY_LEFT) {
        t->cursor = previous_char(t, t->cursor);
    } else if (key == KEY_RIGHT) {
        t->cursor = next_char(t, t->cursor);
    } else if (key == KEY_UP || key == KEY_DOWN) {
        move_vertical(t, key == KEY_UP ? -1 : 1);
        keep_column = true;
    } else if (key == KEY_PAGE_UP || key == KEY_PAGE_DOWN) {
        move_vertical(t, key == KEY_PAGE_UP ? -(t->rows - 2) : t->rows - 2);
        keep_column = true;
    } else if (key == KEY_HOME) {
        t->cursor = line_start(t, t->cursor);
    } else if (key == KEY_END) {
        t->cursor = line_end(t, t->cursor);
    } else if (key == '\b') {
        erase(t, previous_char(t, t->cursor), t->cursor);
    } else if (key == KEY_DELETE) {
        erase(t, t->cursor, next_char(t, t->cursor));
    } else if (key == '\n') {
        /* Die neue Zeile übernimmt die Einrückung der alten. */
        char indent[64];
        size_t n = 0;
        indent[n++] = '\n';
        for (size_t p = line_start(t, t->cursor);
             p < t->cursor && (t->text[p] == ' ' || t->text[p] == '\t') &&
             n < sizeof(indent); p++)
            indent[n++] = t->text[p];
        insert(t, indent, n);
    } else if (key == '\t') {
        insert(t, "    ", (size_t)(TAB - column_of(t, t->cursor) % TAB));
    } else if (key >= 32 && key < 0x800 && key != 127) {
        char bytes[2];
        if (key < 0x80) {
            bytes[0] = (char)key;
            insert(t, bytes, 1);
        } else {
            bytes[0] = (char)(0xC0 | (key >> 6));
            bytes[1] = (char)(0x80 | (key & 0x3F));
            insert(t, bytes, 2);
        }
    } else {
        return false;
    }

    if (!keep_column)
        t->wanted_column = column_of(t, t->cursor);
    scroll_to_cursor(t);
    return true;
}

void textedit_click(textedit_t *t, int x, int y)
{
    int row = (y - 4) / TEXTEDIT_LINE_H;
    size_t start = t->top;
    for (int i = 0; i < row && line_end(t, start) < t->len; i++)
        start = line_end(t, start) + 1;
    int column = t->left + (x - PAD + FONT_W / 2) / FONT_W;
    t->cursor = position_at(t, start, column > 0 ? column : 0);
    t->wanted_column = column_of(t, t->cursor);
}

/* --- Zeichnen -------------------------------------------------------------------------- */

int textedit_width(const textedit_t *t, bool numbers)
{
    return (numbers ? GUTTER : 0) + PAD + t->columns * FONT_W + PAD;
}

int textedit_height(const textedit_t *t)
{
    return t->rows * TEXTEDIT_LINE_H + 8;
}

void textedit_draw(const textedit_t *t, surface_t *s, int x, int y, bool numbers)
{
    int gutter = numbers ? GUTTER : 0;
    int width = textedit_width(t, numbers), height = textedit_height(t);
    int text_x = x + gutter + PAD;
    size_t cursor_line = line_start(t, t->cursor);
    int number = line_number(t, t->top) + 1;
    size_t p = t->top;

    gfx_fill(s, x, y, width, height, C_BACKGROUND);
    if (numbers)
        gfx_fill(s, x, y, gutter, height, C_GUTTER);

    for (int row = 0; row < t->rows; row++, number++) {
        int line_y = y + 4 + row * TEXTEDIT_LINE_H;
        char label[12];

        if (p == cursor_line)
            gfx_fill(s, x + gutter, line_y, width - gutter, TEXTEDIT_LINE_H, C_LINE);
        if (numbers) {
            ksnprintf(label, sizeof(label), "%5d", number);
            gfx_text(s, x, line_y, label, C_NUMBER, false);
        }

        int column = 0;
        const char *text = t->text + p;
        const char *end = t->text + line_end(t, p);
        while (text < end) {
            if (text == t->text + t->cursor && column >= t->left)
                gfx_fill(s, text_x + (column - t->left) * FONT_W, line_y, 2, FONT_H,
                         C_TEXT);
            uint32_t cp = utf8_next(&text);
            if (cp == '\t') {
                column += TAB - column % TAB;
                continue;
            }
            if (column >= t->left && column < t->left + t->columns)
                gfx_glyph(s, text_x + (column - t->left) * FONT_W, line_y, cp, C_TEXT,
                          false);
            column++;
        }
        if (end == t->text + t->cursor && column >= t->left &&
            column <= t->left + t->columns)
            gfx_fill(s, text_x + (column - t->left) * FONT_W, line_y, 2, FONT_H, C_TEXT);

        p = (size_t)(end - t->text);
        if (p >= t->len)
            break;                      /* hinter der letzten Zeile ist nichts mehr */
        p++;
    }
}
