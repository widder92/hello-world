/*
 * Tastaturlogik, unabhängig vom Anschluss: Die Treiber für PS/2 und USB
 * melden gedrückte Tasten als Scancodes; hier werden daraus Zeichen in der
 * gewählten Tastaturbelegung.
 */
#include "input.h"
#include "cpu.h"
#include "crypto.h"
#include "gui.h"
#include "string.h"

#include <stddef.h>

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
        .digits = { "1234567890\xDF\xB4", "!\"\xA7$%&/()=?`" },
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
            uint64_t flags = irq_save();
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

void keyboard_init(void)
{
    build_keymap();
}

/* --- Vom Scancode zum Zeichen ------------------------------------------------- */

static bool shift, ctrl, alt, altgr, caps_lock;

static bool is_letter(uint8_t c)
{
    return (c >= 'a' && c <= 'z') || (c >= 0xE0 && c != 0xF7);
}

static int modifiers(void)
{
    return (shift ? MOD_SHIFT : 0) | (ctrl ? MOD_CTRL : 0) | (alt ? MOD_ALT : 0);
}

void keyboard_handle_scancode(uint8_t scancode, bool extended, bool released)
{
    /* Wann genau jemand eine Taste drückt, ist schwer vorherzusagen. */
    random_stir(&scancode, 1);

    if (extended) {
        int key = 0;
        switch (scancode) {
        case 0x38: altgr = !released; return;           /* rechte Alt-Taste */
        case 0x1D: ctrl = !released; return;            /* rechte Strg-Taste */
        case 0x48: key = KEY_UP; break;
        case 0x50: key = KEY_DOWN; break;
        case 0x4B: key = KEY_LEFT; break;
        case 0x4D: key = KEY_RIGHT; break;
        case 0x47: key = KEY_HOME; break;
        case 0x4F: key = KEY_END; break;
        case 0x49: key = KEY_PAGE_UP; break;
        case 0x51: key = KEY_PAGE_DOWN; break;
        case 0x53: key = KEY_DELETE; break;
        case 0x1C: key = '\n'; break;                   /* Enter am Ziffernblock */
        case 0x35: key = '/'; break;
        default: return;
        }
        if (!released)
            gui_post_key(key, modifiers());
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
    case 0x38:
        alt = !released;
        return;
    case 0x3A:
        if (!released)
            caps_lock = !caps_lock;
        return;
    default:
        break;
    }
    if (released || scancode >= 128)
        return;

    /* Funktionstasten: F1-F10 ab 0x3B, F11 und F12 bei 0x57 und 0x58 */
    if (scancode >= 0x3B && scancode <= 0x44) {
        gui_post_key(KEY_F1 + (scancode - 0x3B), modifiers());
        return;
    }
    if (scancode == 0x57 || scancode == 0x58) {
        gui_post_key(KEY_F1 + 10 + (scancode - 0x57), modifiers());
        return;
    }

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
    gui_post_key(c, modifiers());
}
