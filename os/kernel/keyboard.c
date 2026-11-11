#include "input.h"
#include "idt.h"
#include "io.h"
#include "string.h"
#include "task.h"

#include <stddef.h>
#include <stdint.h>

/* --- Tastaturbelegungen (Scancode-Set 1 -> Latin-1/Unicode) ---------------- */

struct altgr_key {
    uint8_t scancode;
    uint8_t ch;
};

struct layout {
    const char *name;
    /* Tastenreihen, jeweils normal / mit Umschalt */
    const char *digits[2];              /* ab Scancode 0x02 */
    const char *top[2];                 /* ab 0x10 */
    const char *home[2];                /* ab 0x1E */
    const char *bottom[2];              /* ab 0x2B */
    const char *extra[2];               /* 0x56: Taste neben der linken Umschalt */
    const struct altgr_key *altgr;      /* mit {0, 0} abgeschlossen */
};

static const struct altgr_key altgr_none[] = { { 0, 0 } };

static const struct altgr_key altgr_de[] = {
    { 0x03, 0xB2 },                     /* ² */
    { 0x08, '{' }, { 0x09, '[' }, { 0x0A, ']' }, { 0x0B, '}' },
    { 0x0C, '\\' }, { 0x10, '@' }, { 0x1B, '~' }, { 0x56, '|' },
    { 0x32, 0xB5 },                     /* µ */
    { 0, 0 },
};

static const struct layout layouts[] = {
    {
        .name = "de",
        .digits = { "1234567890\xDF'", "!\"\xA7$%&/()=?`" },
        .top = { "qwertzuiop\xFC+", "QWERTZUIOP\xDC*" },
        .home = { "asdfghjkl\xF6\xE4^", "ASDFGHJKL\xD6\xC4\xB0" },
        .bottom = { "#yxcvbnm,.-", "'YXCVBNM;:_" },
        .extra = { "<", ">" },
        .altgr = altgr_de,
    },
    {
        .name = "us",
        .digits = { "1234567890-=", "!@#$%^&*()_+" },
        .top = { "qwertyuiop[]", "QWERTYUIOP{}" },
        .home = { "asdfghjkl;'`", "ASDFGHJKL:\"~" },
        .bottom = { "\\zxcvbnm,./", "|ZXCVBNM<>?" },
        .extra = { "\\", "|" },
        .altgr = altgr_none,
    },
};

static const struct layout *layout = &layouts[0];
static uint8_t keymap[3][128];          /* normal, Umschalt, AltGr */

static void fill(uint8_t *map, uint8_t start, const char *chars)
{
    while (*chars)
        map[start++] = (uint8_t)*chars++;
}

static void build_keymap(void)
{
    memset(keymap, 0, sizeof(keymap));
    for (int level = 0; level < 2; level++) {
        uint8_t *map = keymap[level];
        fill(map, 0x02, layout->digits[level]);
        fill(map, 0x10, layout->top[level]);
        fill(map, 0x1E, layout->home[level]);
        fill(map, 0x2B, layout->bottom[level]);
        fill(map, 0x56, layout->extra[level]);
        map[0x01] = 27;                 /* Esc */
        map[0x0E] = '\b';
        map[0x0F] = '\t';
        map[0x1C] = '\n';
        map[0x39] = ' ';
        map[0x37] = '*';                /* Ziffernblock */
        fill(map, 0x47, "789-456+1230.");
    }
    for (const struct altgr_key *k = layout->altgr; k->scancode; k++)
        keymap[2][k->scancode] = k->ch;
}

bool keyboard_set_layout(const char *name)
{
    for (size_t i = 0; i < sizeof(layouts) / sizeof(layouts[0]); i++) {
        if (strcmp(layouts[i].name, name) == 0) {
            uint32_t flags = irq_save();
            layout = &layouts[i];
            build_keymap();
            irq_restore(flags);
            return true;
        }
    }
    return false;
}

const char *keyboard_layout_name(void)
{
    return layout->name;
}

/* --- Eingabepuffer ---------------------------------------------------------- */

#define BUFFER_SIZE 128

static int buffer[BUFFER_SIZE];
static unsigned head, tail;             /* head: schreiben, tail: lesen */
static int waiting_pid;                 /* Task, der auf Eingabe wartet */

void input_push(int c)
{
    uint32_t flags = irq_save();
    unsigned next = (head + 1) % BUFFER_SIZE;
    if (next != tail) {                 /* voller Puffer: Zeichen verwerfen */
        buffer[head] = c;
        head = next;
    }
    if (waiting_pid) {
        task_wake(waiting_pid);
        waiting_pid = 0;
    }
    irq_restore(flags);
}

int input_getchar(void)
{
    for (;;) {
        uint32_t flags = irq_save();
        if (head != tail) {
            int c = buffer[tail];
            tail = (tail + 1) % BUFFER_SIZE;
            irq_restore(flags);
            return c;
        }
        /* Nichts da: schlafen legen, bis input_push() uns weckt. */
        waiting_pid = (int)task_current_pid();
        task_block();
        irq_restore(flags);
    }
}

/* --- PS/2-Tastaturtreiber --------------------------------------------------- */

static bool shift, ctrl, altgr, caps_lock;
static bool extended;                   /* Präfix 0xE0 gesehen */

static bool is_letter(uint8_t c)
{
    return (c >= 'a' && c <= 'z') || (c >= 0xE0 && c != 0xF7);
}

static void keyboard_irq(regs_t *regs)
{
    (void)regs;
    uint8_t scancode = inb(0x60);

    if (scancode == 0xE0) {
        extended = true;
        return;
    }
    bool released = scancode & 0x80;
    bool ext = extended;
    scancode &= 0x7F;
    extended = false;

    if (ext) {
        switch (scancode) {
        case 0x38: altgr = !released; break;            /* rechte Alt-Taste */
        case 0x1D: ctrl = !released; break;             /* rechte Strg-Taste */
        case 0x48: if (!released) input_push(KEY_UP); break;
        case 0x50: if (!released) input_push(KEY_DOWN); break;
        case 0x4B: if (!released) input_push(KEY_LEFT); break;
        case 0x4D: if (!released) input_push(KEY_RIGHT); break;
        case 0x1C: if (!released) input_push('\n'); break;
        case 0x35: if (!released) input_push('/'); break;
        default: break;
        }
        return;
    }

    switch (scancode) {
    case 0x2A:
    case 0x36:
        shift = !released;
        return;
    case 0x1D:
        ctrl = !released;
        return;
    case 0x3A:
        if (!released)
            caps_lock = !caps_lock;
        return;
    default:
        break;
    }
    if (released)
        return;

    uint8_t c;
    if (altgr) {
        c = keymap[2][scancode];
    } else {
        bool upper = shift;
        if (caps_lock && is_letter(keymap[0][scancode]))
            upper = !upper;
        c = keymap[upper ? 1 : 0][scancode];
    }
    if (!c)
        return;
    if (ctrl && c >= 'a' && c <= 'z')
        c &= 0x1F;                      /* Strg+C -> 3, Strg+L -> 12, ... */
    input_push(c);
}

void keyboard_init(void)
{
    build_keymap();
    while (inb(0x64) & 0x01)            /* alte Daten im Controller verwerfen */
        inb(0x60);
    irq_register(1, keyboard_irq);
}
