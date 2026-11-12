/* HID-Berichte verstehen - siehe hid.h */
#include "hid.h"

/* Die Beschreibung besteht aus kleinen Einträgen dreier Arten. */
enum { ITEM_MAIN = 0, ITEM_GLOBAL = 1, ITEM_LOCAL = 2 };

/* Haupteinträge */
enum { MAIN_INPUT = 8, MAIN_OUTPUT = 9, MAIN_COLLECTION = 10, MAIN_FEATURE = 11,
       MAIN_END_COLLECTION = 12 };
/* Globale Einträge: gelten, bis sie überschrieben werden */
enum { GLOBAL_USAGE_PAGE = 0, GLOBAL_LOGICAL_MIN = 1, GLOBAL_LOGICAL_MAX = 2,
       GLOBAL_REPORT_SIZE = 7, GLOBAL_REPORT_ID = 8, GLOBAL_REPORT_COUNT = 9,
       GLOBAL_PUSH = 10, GLOBAL_POP = 11 };
/* Lokale Einträge: gelten nur für den nächsten Haupteintrag */
enum { LOCAL_USAGE = 0, LOCAL_USAGE_MIN = 1, LOCAL_USAGE_MAX = 2 };

#define INPUT_CONSTANT  (1u << 0)
#define INPUT_VARIABLE  (1u << 1)
#define INPUT_RELATIVE  (1u << 2)

/* Bedeutungen ("Usages"): Seite in den oberen 16 Bits */
#define PAGE_DESKTOP    0x01u
#define PAGE_KEYBOARD   0x07u
#define PAGE_BUTTON     0x09u
#define USAGE(page, id) (((uint32_t)(page) << 16) | (id))
#define DESKTOP_POINTER USAGE(PAGE_DESKTOP, 0x01)
#define DESKTOP_MOUSE   USAGE(PAGE_DESKTOP, 0x02)
#define DESKTOP_KEYBOARD USAGE(PAGE_DESKTOP, 0x06)
#define DESKTOP_KEYPAD  USAGE(PAGE_DESKTOP, 0x07)
#define DESKTOP_X       USAGE(PAGE_DESKTOP, 0x30)
#define DESKTOP_Y       USAGE(PAGE_DESKTOP, 0x31)
#define DESKTOP_WHEEL   USAGE(PAGE_DESKTOP, 0x38)

#define MAX_USAGES      16
#define MAX_DEPTH       8
#define MAX_BITS        (4096 * 8)      /* längster Bericht, den wir annehmen */

typedef struct {
    uint32_t usage_page;
    int32_t logical_min, logical_max;
    uint32_t report_size, report_count;
    uint8_t report_id;
} globals_t;

typedef struct {
    uint32_t usages[MAX_USAGES];
    int usage_count;
    uint32_t usage_min, usage_max;
    bool has_range;
} locals_t;

static hid_report_t *report_for(hid_layout_t *layout, uint8_t id)
{
    for (int i = 0; i < layout->count; i++) {
        if (layout->reports[i].id == id)
            return &layout->reports[i];
    }
    if (layout->count >= HID_MAX_REPORTS)
        return NULL;
    hid_report_t *report = &layout->reports[layout->count++];
    *report = (hid_report_t){ .id = id };
    return report;
}

/* Die Bedeutung des i-ten Eintrags eines Feldes */
static uint32_t usage_at(const locals_t *locals, uint32_t index)
{
    if (index < (uint32_t)locals->usage_count)
        return locals->usages[index];
    if (locals->has_range) {
        uint32_t extra = index - (uint32_t)locals->usage_count;
        if (locals->usage_min + extra <= locals->usage_max)
            return locals->usage_min + extra;
        return locals->usage_max;
    }
    if (locals->usage_count > 0)
        return locals->usages[locals->usage_count - 1];
    return 0;
}

static void set_field(hid_field_t *field, uint32_t offset, const globals_t *g,
                      uint32_t count)
{
    if (field->bits)
        return;                         /* das erste Vorkommen zählt */
    field->offset = (uint16_t)offset;
    field->bits = (uint8_t)g->report_size;
    field->count = (uint8_t)(count > 255 ? 255 : count);
    field->minimum = g->logical_min;
    field->maximum = g->logical_max;
}

/* Ein Eingabefeld einordnen: Gehört es zu einer Maus oder Tastatur? */
static void input_item(hid_report_t *report, const globals_t *g,
                       const locals_t *locals, uint32_t flags, uint32_t application)
{
    uint32_t offset = report->bits;
    uint32_t size = g->report_size, count = g->report_count;
    if (flags & INPUT_CONSTANT)
        return;                         /* Füllbits */

    bool mouse = application == DESKTOP_MOUSE || application == DESKTOP_POINTER;
    bool keyboard = application == DESKTOP_KEYBOARD || application == DESKTOP_KEYPAD;

    if (mouse && (flags & INPUT_VARIABLE)) {
        for (uint32_t i = 0; i < count; i++) {
            uint32_t usage = usage_at(locals, i);
            uint32_t at = offset + i * size;
            if (usage == DESKTOP_X && !report->x.bits) {
                set_field(&report->x, at, g, 1);
                report->absolute = !(flags & INPUT_RELATIVE);
            } else if (usage == DESKTOP_Y) {
                set_field(&report->y, at, g, 1);
            } else if (usage == DESKTOP_WHEEL) {
                set_field(&report->wheel, at, g, 1);
            } else if ((usage >> 16) == PAGE_BUTTON && size == 1 &&
                       (usage & 0xFFFF) == 1) {
                /* ab Taste 1 folgen die weiteren Tasten Bit für Bit */
                set_field(&report->buttons, at, g, count - i);
            }
        }
        if (report->x.bits && report->y.bits)
            report->kind = HID_REPORT_MOUSE;
    } else if (keyboard && g->usage_page == PAGE_KEYBOARD) {
        uint32_t first = usage_at(locals, 0) & 0xFFFF;
        if (!(flags & INPUT_VARIABLE)) {
            set_field(&report->keys, offset, g, count);         /* Liste */
            report->kind = HID_REPORT_KEYBOARD;
        } else if (size == 1 && first == 0xE0 && count >= 8) {
            set_field(&report->modifiers, offset, g, 8);
            report->kind = HID_REPORT_KEYBOARD;
        } else if (size == 1 && first < 0xE0 && count > 8 && !report->bitmap.bits) {
            set_field(&report->bitmap, offset, g, count);       /* ein Bit je Taste */
            report->bitmap_first = (uint16_t)first;
            report->kind = HID_REPORT_KEYBOARD;
        }
    }
}

bool hid_parse(const uint8_t *descriptor, size_t length, hid_layout_t *out)
{
    globals_t g = { 0 }, saved[4];
    int saved_count = 0;
    locals_t locals = { 0 };
    uint32_t collections[MAX_DEPTH];    /* Bedeutung je offener Gruppe */
    int depth = 0;
    uint32_t application = 0;           /* äußerste Gruppe: wofür das Gerät da ist */

    *out = (hid_layout_t){ 0 };

    size_t pos = 0;
    while (pos < length) {
        uint8_t prefix = descriptor[pos++];
        if (prefix == 0xFE) {           /* langer Eintrag: überspringen */
            if (pos + 2 > length)
                return false;
            pos += 2 + (size_t)descriptor[pos];
            continue;
        }
        unsigned size = prefix & 0x03;
        if (size == 3)
            size = 4;
        unsigned type = (prefix >> 2) & 0x03;
        unsigned tag = prefix >> 4;
        if (pos + size > length)
            return false;

        uint32_t value = 0;
        for (unsigned i = 0; i < size; i++)
            value |= (uint32_t)descriptor[pos + i] << (8 * i);
        int32_t signed_value = (int32_t)value;
        if (size == 1)
            signed_value = (int8_t)value;
        else if (size == 2)
            signed_value = (int16_t)value;
        pos += size;

        if (type == ITEM_GLOBAL) {
            switch (tag) {
            case GLOBAL_USAGE_PAGE:   g.usage_page = value & 0xFFFF; break;
            case GLOBAL_LOGICAL_MIN:  g.logical_min = signed_value; break;
            case GLOBAL_LOGICAL_MAX:  g.logical_max = signed_value; break;
            case GLOBAL_REPORT_SIZE:  g.report_size = value; break;
            case GLOBAL_REPORT_COUNT: g.report_count = value; break;
            case GLOBAL_REPORT_ID:
                g.report_id = (uint8_t)value;
                out->uses_ids = true;
                break;
            case GLOBAL_PUSH:
                if (saved_count < 4)
                    saved[saved_count++] = g;
                break;
            case GLOBAL_POP:
                if (saved_count > 0)
                    g = saved[--saved_count];
                break;
            default:
                break;
            }
        } else if (type == ITEM_LOCAL) {
            /* Angaben mit 4 Bytes bringen ihre Seite selbst mit */
            uint32_t usage = size == 4 ? value : USAGE(g.usage_page, value & 0xFFFF);
            if (tag == LOCAL_USAGE && locals.usage_count < MAX_USAGES) {
                locals.usages[locals.usage_count++] = usage;
            } else if (tag == LOCAL_USAGE_MIN) {
                locals.usage_min = usage;
                locals.has_range = true;
            } else if (tag == LOCAL_USAGE_MAX) {
                locals.usage_max = usage;
                locals.has_range = true;
            }
        } else if (type == ITEM_MAIN) {
            if (tag == MAIN_COLLECTION) {
                if (depth >= MAX_DEPTH)
                    return false;
                collections[depth] = usage_at(&locals, 0);
                if (depth == 0)
                    application = collections[0];
                depth++;
            } else if (tag == MAIN_END_COLLECTION) {
                if (depth > 0)
                    depth--;
                if (depth == 0)
                    application = 0;
            } else if (tag == MAIN_INPUT) {
                if (g.report_size > 32 || g.report_count > MAX_BITS)
                    return false;
                uint32_t bits = g.report_size * g.report_count;
                hid_report_t *report = report_for(out, g.report_id);
                if (!report || report->bits + bits > MAX_BITS)
                    return false;
                input_item(report, &g, &locals, value, application);
                report->bits = (uint16_t)(report->bits + bits);
            }
            /* Ausgaben (z. B. Leuchten der Tastatur) brauchen wir nicht. */
            locals = (locals_t){ 0 };
        }
    }
    return true;
}

bool hid_has(const hid_layout_t *layout, hid_report_kind_t kind)
{
    for (int i = 0; i < layout->count; i++) {
        if (layout->reports[i].kind == kind)
            return true;
    }
    return false;
}

const hid_report_t *hid_match(const hid_layout_t *layout, const uint8_t *data,
                              size_t length, const uint8_t **payload,
                              size_t *payload_length)
{
    uint8_t id = 0;
    if (layout->uses_ids) {
        if (length < 1)
            return NULL;
        id = data[0];
        data++;
        length--;
    }
    for (int i = 0; i < layout->count; i++) {
        if (layout->reports[i].id == id) {
            *payload = data;
            *payload_length = length;
            return &layout->reports[i];
        }
    }
    return NULL;
}

/* Liest den Eintrag index eines Feldes; false, wenn er nicht mehr im Bericht liegt. */
static bool read_bits(const uint8_t *data, size_t length, const hid_field_t *field,
                      unsigned index, uint32_t *out)
{
    if (!field->bits || index >= field->count)
        return false;
    uint32_t start = field->offset + index * field->bits;
    if (start + field->bits > length * 8)
        return false;

    uint32_t value = 0;
    for (unsigned bit = 0; bit < field->bits; bit++) {
        uint32_t at = start + bit;
        if (data[at / 8] & (1u << (at % 8)))
            value |= 1u << bit;
    }
    *out = value;
    return true;
}

/* Wie read_bits, deutet den Wert aber mit Vorzeichen, wenn das Feld negativ werden kann. */
static bool read_value(const uint8_t *data, size_t length, const hid_field_t *field,
                       int32_t *out)
{
    uint32_t raw;
    if (!read_bits(data, length, field, 0, &raw))
        return false;
    if (field->minimum < 0 && field->bits < 32 && (raw & (1u << (field->bits - 1))))
        raw |= ~0u << field->bits;
    *out = (int32_t)raw;
    return true;
}

bool hid_decode_mouse(const hid_report_t *report, const uint8_t *payload,
                      size_t length, hid_mouse_t *out)
{
    if (report->kind != HID_REPORT_MOUSE)
        return false;
    int32_t x, y, wheel = 0;
    if (!read_value(payload, length, &report->x, &x) ||
        !read_value(payload, length, &report->y, &y))
        return false;
    read_value(payload, length, &report->wheel, &wheel);

    *out = (hid_mouse_t){ 0 };
    out->absolute = report->absolute;
    if (report->absolute) {
        /* auf 0..65535 umrechnen, unabhängig vom Wertebereich des Geräts */
        int64_t range_x = (int64_t)report->x.maximum - report->x.minimum;
        int64_t range_y = (int64_t)report->y.maximum - report->y.minimum;
        if (range_x <= 0 || range_y <= 0)
            return false;
        int64_t scaled_x = ((int64_t)x - report->x.minimum) * 65535 / range_x;
        int64_t scaled_y = ((int64_t)y - report->y.minimum) * 65535 / range_y;
        out->dx = (int)(scaled_x < 0 ? 0 : scaled_x > 65535 ? 65535 : scaled_x);
        out->dy = (int)(scaled_y < 0 ? 0 : scaled_y > 65535 ? 65535 : scaled_y);
    } else {
        out->dx = x;
        out->dy = y;
    }
    out->wheel = wheel;
    for (unsigned i = 0; i < 3; i++) {
        uint32_t pressed;
        if (read_bits(payload, length, &report->buttons, i, &pressed) && pressed)
            out->buttons |= 1 << i;
    }
    return true;
}

bool hid_decode_keyboard(const hid_report_t *report, const uint8_t *payload,
                         size_t length, uint8_t boot[8])
{
    if (report->kind != HID_REPORT_KEYBOARD)
        return false;
    for (int i = 0; i < 8; i++)
        boot[i] = 0;

    for (unsigned i = 0; i < 8; i++) {
        uint32_t pressed;
        if (read_bits(payload, length, &report->modifiers, i, &pressed) && pressed)
            boot[0] |= (uint8_t)(1u << i);
    }

    int used = 0;
    for (unsigned i = 0; i < report->keys.count && used < 6; i++) {
        uint32_t key;
        if (!read_bits(payload, length, &report->keys, i, &key))
            break;
        if (key == 1) {                 /* "zu viele Tasten": wie im Standardformat */
            for (int k = 2; k < 8; k++)
                boot[k] = 1;
            return true;
        }
        if (key >= 4 && key <= 0xFF)
            boot[2 + used++] = (uint8_t)key;
    }
    for (unsigned i = 0; i < report->bitmap.count && used < 6; i++) {
        uint32_t pressed;
        if (!read_bits(payload, length, &report->bitmap, i, &pressed))
            break;
        uint32_t key = report->bitmap_first + i;
        if (pressed && key >= 4 && key <= 0xFF)
            boot[2 + used++] = (uint8_t)key;
    }
    return true;
}
