/* TrueType-Schriften lesen und rastern - siehe truetype.h */
#include "truetype.h"
#include "memory.h"
#include "string.h"

#define SUB         256                 /* Unterteilungen je Bildpunkt (8 Bit) */
#define PIXELS_MIN  4
#define PIXELS_MAX  512
#define DEPTH_MAX   5                   /* zusammengesetzte Zeichen in zusammengesetzten */
#define POINTS_MAX  4096
#define EDGES_MAX   65536

/* --- Datei lesen ------------------------------------------------------------------- */

static bool in_file(const ttf_t *f, uint32_t offset, uint32_t length)
{
    return offset <= f->size && length <= f->size - offset;
}

static unsigned u8(const ttf_t *f, uint32_t at)
{
    return in_file(f, at, 1) ? f->data[at] : 0;
}

static unsigned u16(const ttf_t *f, uint32_t at)
{
    return in_file(f, at, 2) ? (unsigned)((f->data[at] << 8) | f->data[at + 1]) : 0;
}

static int s16(const ttf_t *f, uint32_t at)
{
    return (int16_t)u16(f, at);
}

static uint32_t u32(const ttf_t *f, uint32_t at)
{
    return ((uint32_t)u16(f, at) << 16) | u16(f, at + 2);
}

/* Sucht eine Tabelle im Verzeichnis der Datei. */
static bool table(const ttf_t *f, const char *tag, uint32_t *offset, uint32_t *length)
{
    unsigned count = u16(f, 4);
    for (unsigned i = 0; i < count; i++) {
        uint32_t entry = 12 + 16 * i;
        if (!in_file(f, entry, 16))
            return false;
        if (memcmp(f->data + entry, tag, 4) == 0) {
            *offset = u32(f, entry + 8);
            *length = u32(f, entry + 12);
            return in_file(f, *offset, *length);
        }
    }
    return false;
}

bool ttf_open(ttf_t *font, const uint8_t *data, size_t size)
{
    uint32_t head, hhea, maxp, cmap, length;

    memset(font, 0, sizeof(*font));
    font->data = data;
    font->size = size;
    if (size < 12 || size > 0x7FFFFFFF)
        return false;
    uint32_t version = u32(font, 0);
    if (version != 0x00010000 && version != 0x74727565)     /* nur TrueType-Umrisse */
        return false;

    if (!table(font, "head", &head, &length) || length < 54 ||
        !table(font, "hhea", &hhea, &length) || length < 36 ||
        !table(font, "maxp", &maxp, &length) || length < 6 ||
        !table(font, "cmap", &cmap, &length) ||
        !table(font, "glyf", &font->glyf, &font->glyf_size) ||
        !table(font, "loca", &font->loca, &font->loca_size) ||
        !table(font, "hmtx", &font->hmtx, &font->hmtx_size))
        return false;

    font->units_per_em = (int)u16(font, head + 18);
    font->long_loca = s16(font, head + 50) != 0;
    font->ascent = s16(font, hhea + 4);
    font->descent = s16(font, hhea + 6);
    font->line_gap = s16(font, hhea + 8);
    font->metric_count = (int)u16(font, hhea + 34);
    font->glyph_count = (int)u16(font, maxp + 4);
    if (font->units_per_em < 16 || font->units_per_em > 16384 || font->glyph_count < 1 ||
        font->metric_count < 1)
        return false;

    /* Zuordnungstabellen: die mit vollem Unicode-Umfang (12) und die übliche (4) */
    unsigned tables = u16(font, cmap + 2);
    for (unsigned i = 0; i < tables; i++) {
        uint32_t record = cmap + 4 + 8 * i;
        unsigned platform = u16(font, record), encoding = u16(font, record + 2);
        uint32_t sub = cmap + u32(font, record + 4);
        bool unicode = platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10));
        if (!unicode || !in_file(font, sub, 16))
            continue;
        unsigned format = u16(font, sub);
        if (format == 12)
            font->cmap12 = sub;
        else if (format == 4)
            font->cmap4 = sub;
    }
    return font->cmap4 || font->cmap12;
}

int ttf_glyph(const ttf_t *f, uint32_t codepoint)
{
    if (f->cmap12) {                    /* Gruppen: [erstes, letztes Zeichen, erster Umriss] */
        uint32_t groups = u32(f, f->cmap12 + 12);
        uint32_t low = 0, high = groups;
        while (low < high) {
            uint32_t mid = low + (high - low) / 2;
            uint32_t group = f->cmap12 + 16 + 12 * mid;
            if (!in_file(f, group, 12))
                return 0;
            uint32_t first = u32(f, group), last = u32(f, group + 4);
            if (codepoint < first) {
                high = mid;
            } else if (codepoint > last) {
                low = mid + 1;
            } else {
                uint32_t glyph = u32(f, group + 8) + (codepoint - first);
                return glyph < (uint32_t)f->glyph_count ? (int)glyph : 0;
            }
        }
        return 0;
    }
    if (!f->cmap4 || codepoint > 0xFFFF)
        return 0;

    /* Format 4: Abschnitte mit Ende, Anfang, Versatz und optionalem Verweis */
    unsigned segments = u16(f, f->cmap4 + 6) / 2;
    uint32_t ends = f->cmap4 + 14;
    uint32_t starts = ends + 2 * segments + 2;
    uint32_t deltas = starts + 2 * segments;
    uint32_t ranges = deltas + 2 * segments;
    unsigned low = 0, high = segments;
    while (low < high) {
        unsigned mid = low + (high - low) / 2;
        if (u16(f, ends + 2 * mid) < codepoint)
            low = mid + 1;
        else
            high = mid;
    }
    if (low >= segments)
        return 0;
    unsigned start = u16(f, starts + 2 * low);
    if (codepoint < start)
        return 0;
    unsigned range = u16(f, ranges + 2 * low);
    unsigned glyph;
    if (range == 0) {
        glyph = (codepoint + u16(f, deltas + 2 * low)) & 0xFFFF;
    } else {
        glyph = u16(f, ranges + 2 * low + range + 2 * (codepoint - start));
        if (glyph)
            glyph = (glyph + u16(f, deltas + 2 * low)) & 0xFFFF;
    }
    return glyph < (unsigned)f->glyph_count ? (int)glyph : 0;
}

/* Rundet value * size / em kaufmännisch (auch für negative Werte). */
static int scale_round(int64_t value, int size, int em)
{
    int64_t scaled = value * size * 2 + (value >= 0 ? em : -em);
    return (int)(scaled / (2 * (int64_t)em));
}

int ttf_advance(const ttf_t *f, int glyph, int size)
{
    if (glyph < 0 || glyph >= f->glyph_count)
        glyph = 0;
    /* Die letzten Zeichen teilen sich oft den Vorschub des letzten Eintrags */
    int index = glyph < f->metric_count ? glyph : f->metric_count - 1;
    return scale_round(u16(f, f->hmtx + 4 * (uint32_t)index), size, f->units_per_em);
}

void ttf_line_metrics(const ttf_t *f, int size, int *ascent, int *descent, int *line_height)
{
    int up = scale_round(f->ascent, size, f->units_per_em);
    int down = scale_round(-f->descent, size, f->units_per_em);
    int gap = scale_round(f->line_gap, size, f->units_per_em);
    if (ascent)
        *ascent = up;
    if (descent)
        *descent = down;
    if (line_height)
        *line_height = up + down + gap;
}

/* --- Umrisse sammeln ----------------------------------------------------------------- */

typedef struct {
    int32_t x0, y0, x1, y1;             /* in 1/256 Bildpunkt, y nach unten */
} edge_t;

typedef struct {
    edge_t *edges;
    int count, capacity;
    bool failed;
    int32_t min_x, min_y, max_x, max_y;
} outline_t;

/* Abbildung von Schrifteinheiten auf Bildpunkte: Faktoren mit 16 Nachkommabits */
typedef struct {
    int64_t xx, xy, yx, yy, dx, dy;
} matrix_t;

typedef struct {
    int32_t x, y;
} point_t;

static point_t apply(const matrix_t *m, int64_t x, int64_t y)
{
    point_t p;
    p.x = (int32_t)((m->xx * x + m->xy * y + m->dx) >> 16);
    p.y = (int32_t)((m->yx * x + m->yy * y + m->dy) >> 16);
    return p;
}

static void add_line(outline_t *o, point_t a, point_t b)
{
    if (o->failed || a.y == b.y)
        return;                         /* Waagerechte tragen nichts zur Fläche bei */
    if (o->count == o->capacity) {
        int capacity = o->capacity ? o->capacity * 2 : 256;
        edge_t *grown = capacity <= EDGES_MAX ? kmalloc((size_t)capacity * sizeof(edge_t)) : NULL;
        if (!grown) {
            o->failed = true;
            return;
        }
        if (o->edges)
            memcpy(grown, o->edges, (size_t)o->count * sizeof(edge_t));
        kfree(o->edges);
        o->edges = grown;
        o->capacity = capacity;
    }
    o->edges[o->count++] = (edge_t){ a.x, a.y, b.x, b.y };
}

static void track(outline_t *o, point_t p)
{
    if (p.x < o->min_x) o->min_x = p.x;
    if (p.x > o->max_x) o->max_x = p.x;
    if (p.y < o->min_y) o->min_y = p.y;
    if (p.y > o->max_y) o->max_y = p.y;
}

/* Ein Bogen (Parabel) von a über den Leitpunkt c nach b: in kurze Geraden zerlegen. */
static void add_curve(outline_t *o, point_t a, point_t c, point_t b)
{
    /* Je stärker gekrümmt, desto mehr Stücke: Abweichung unter 1/4 Bildpunkt */
    int64_t bend_x = (int64_t)a.x - 2 * (int64_t)c.x + b.x;
    int64_t bend_y = (int64_t)a.y - 2 * (int64_t)c.y + b.y;
    int64_t bend = (bend_x < 0 ? -bend_x : bend_x) + (bend_y < 0 ? -bend_y : bend_y);
    int steps = 1;
    while (bend > SUB / 4 && steps < 64) {
        bend /= 4;
        steps *= 2;
    }

    point_t previous = a;
    for (int i = 1; i <= steps; i++) {
        point_t p = b;
        if (i < steps) {
            int64_t u = steps - i, t = i, n2 = (int64_t)steps * steps;
            p.x = (int32_t)((u * u * a.x + 2 * u * t * c.x + t * t * b.x) / n2);
            p.y = (int32_t)((u * u * a.y + 2 * u * t * c.y + t * t * b.y) / n2);
        }
        add_line(o, previous, p);
        previous = p;
    }
}

static bool glyph_location(const ttf_t *f, int glyph, uint32_t *offset, uint32_t *length)
{
    uint32_t start, end;
    if (glyph < 0 || glyph >= f->glyph_count)
        return false;
    if (f->long_loca) {
        start = u32(f, f->loca + 4 * (uint32_t)glyph);
        end = u32(f, f->loca + 4 * (uint32_t)glyph + 4);
    } else {
        start = 2 * u16(f, f->loca + 2 * (uint32_t)glyph);
        end = 2 * u16(f, f->loca + 2 * (uint32_t)glyph + 2);
    }
    if (end < start || end > f->glyf_size)
        return false;
    *offset = f->glyf + start;
    *length = end - start;
    return true;
}

static void load_glyph(const ttf_t *f, int glyph, const matrix_t *m, outline_t *o, int depth);

/* Ein Zeichen, das aus anderen zusammengesetzt ist (etwa "ä" aus "a" und Punkten) */
static void load_composite(const ttf_t *f, uint32_t at, uint32_t end, const matrix_t *m,
                           outline_t *o, int depth)
{
    for (int part = 0; part < 64; part++) {
        if (at + 4 > end) {
            o->failed = true;
            return;
        }
        unsigned flags = u16(f, at), child = u16(f, at + 2);
        at += 4;

        int64_t ox = 0, oy = 0;
        if (flags & 0x0001) {           /* Versatz als 16-Bit-Werte */
            ox = s16(f, at);
            oy = s16(f, at + 2);
            at += 4;
        } else {
            ox = (int8_t)u8(f, at);
            oy = (int8_t)u8(f, at + 1);
            at += 2;
        }
        if (!(flags & 0x0002))
            ox = oy = 0;                /* Bezug über Punktnummern: selten, hier ohne Versatz */

        /* optionale Streckung oder Drehung, Werte mit 14 Nachkommabits */
        int64_t a = 1 << 14, b = 0, c = 0, d = 1 << 14;
        if (flags & 0x0008) {
            a = d = s16(f, at);
            at += 2;
        } else if (flags & 0x0040) {
            a = s16(f, at);
            d = s16(f, at + 2);
            at += 4;
        } else if (flags & 0x0080) {
            a = s16(f, at);
            b = s16(f, at + 2);
            c = s16(f, at + 4);
            d = s16(f, at + 6);
            at += 8;
        }
        if (at > end) {
            o->failed = true;
            return;
        }

        /* erst die Abbildung des Teils, dann die des Ganzen */
        matrix_t inner;
        inner.xx = (m->xx * a + m->xy * b) >> 14;
        inner.yx = (m->yx * a + m->yy * b) >> 14;
        inner.xy = (m->xx * c + m->xy * d) >> 14;
        inner.yy = (m->yx * c + m->yy * d) >> 14;
        inner.dx = m->xx * ox + m->xy * oy + m->dx;
        inner.dy = m->yx * ox + m->yy * oy + m->dy;
        load_glyph(f, (int)child, &inner, o, depth + 1);

        if (!(flags & 0x0020) || o->failed)
            return;                     /* keine weiteren Teile */
    }
}

static void load_glyph(const ttf_t *f, int glyph, const matrix_t *m, outline_t *o, int depth)
{
    uint32_t at, length;
    if (depth > DEPTH_MAX || !glyph_location(f, glyph, &at, &length)) {
        o->failed = true;
        return;
    }
    if (length < 10)
        return;                         /* leer, z. B. das Leerzeichen */
    uint32_t end = at + length;
    int contours = s16(f, at);
    at += 10;
    if (contours < 0) {
        load_composite(f, at, end, m, o, depth);
        return;
    }
    if (contours == 0)
        return;

    /* Kopf: letzter Punkt jedes Umrisses, dann (übersprungene) Anweisungen */
    uint32_t ends = at;
    if (ends + 2 * (uint32_t)contours + 2 > end) {
        o->failed = true;
        return;
    }
    int points = (int)u16(f, ends + 2 * (uint32_t)(contours - 1)) + 1;
    at = ends + 2 * (uint32_t)contours;
    at += 2 + u16(f, at);
    if (points < 1 || points > POINTS_MAX || at > end) {
        o->failed = true;
        return;
    }

    uint8_t *flags = kmalloc((size_t)points);
    point_t *pts = kmalloc((size_t)points * sizeof(point_t));
    if (!flags || !pts) {
        o->failed = true;
        goto done;
    }

    /* Merkmale je Punkt; ein Wert kann für mehrere Punkte in Folge gelten */
    for (int i = 0; i < points;) {
        if (at >= end) {
            o->failed = true;
            goto done;
        }
        uint8_t flag = (uint8_t)u8(f, at++);
        int repeat = 1;
        if (flag & 0x08) {
            if (at >= end) {
                o->failed = true;
                goto done;
            }
            repeat += (int)u8(f, at++);
        }
        while (repeat-- > 0 && i < points)
            flags[i++] = flag;
    }

    /* Koordinaten: jeweils als Abstand zum vorigen Punkt, kurz oder lang gespeichert */
    for (int axis = 0; axis < 2; axis++) {
        int short_bit = axis == 0 ? 0x02 : 0x04, same_bit = axis == 0 ? 0x10 : 0x20;
        int value = 0;
        for (int i = 0; i < points; i++) {
            if (flags[i] & short_bit) {
                if (at + 1 > end) {
                    o->failed = true;
                    goto done;
                }
                int delta = (int)u8(f, at++);
                value += (flags[i] & same_bit) ? delta : -delta;
            } else if (!(flags[i] & same_bit)) {
                if (at + 2 > end) {
                    o->failed = true;
                    goto done;
                }
                value += s16(f, at);
                at += 2;
            }
            if (axis == 0)
                pts[i].x = value;
            else
                pts[i].y = value;
        }
    }

    /* In Bildpunkte umrechnen */
    for (int i = 0; i < points; i++) {
        pts[i] = apply(m, pts[i].x, pts[i].y);
        track(o, pts[i]);
    }

    /*
     * Jeden Umriss ablaufen. Punkte "auf der Kurve" sind Ecken oder
     * Bogenenden, die anderen Leitpunkte; zwischen zwei Leitpunkten liegt
     * stillschweigend ein Bogenende genau in der Mitte.
     */
    int first = 0;
    for (int c = 0; c < contours; c++) {
        int last = (int)u16(f, ends + 2 * (uint32_t)c);
        if (last >= points || last < first) {
            o->failed = true;
            goto done;
        }
        int count = last - first + 1, begin = first;
        point_t start;
        if (flags[first] & 1) {
            start = pts[first];
            begin = first + 1;
            count--;
        } else if (flags[last] & 1) {
            start = pts[last];
            count--;
        } else {
            start.x = (pts[first].x + pts[last].x) / 2;
            start.y = (pts[first].y + pts[last].y) / 2;
        }

        point_t current = start, control = start;
        bool have_control = false;
        for (int k = 0; k < count; k++) {
            point_t p = pts[begin + k];
            if (flags[begin + k] & 1) {
                if (have_control)
                    add_curve(o, current, control, p);
                else
                    add_line(o, current, p);
                current = p;
                have_control = false;
            } else if (have_control) {
                point_t middle = { (control.x + p.x) / 2, (control.y + p.y) / 2 };
                add_curve(o, current, control, middle);
                current = middle;
                control = p;
            } else {
                control = p;
                have_control = true;
            }
        }
        if (have_control)
            add_curve(o, current, control, start);
        else
            add_line(o, current, start);
        first = last + 1;
    }

done:
    kfree(flags);
    kfree(pts);
}

/* --- Füllen ------------------------------------------------------------------------- */

/*
 * Jede Kante trägt in jeder Zeile, die sie kreuzt, zu zwei Summen bei:
 * "cover" (wie viel Höhe der Zeile rechts von ihr gefüllt wird) und "area"
 * (die Fläche links von ihr innerhalb ihres Bildpunkts). Am Ende ergibt das
 * Aufsummieren von links nach rechts für jeden Bildpunkt die bedeckte Fläche.
 */
static void row_part(int32_t *cover, int32_t *area, int32_t xa, int32_t xb, int32_t height)
{
    if (xa > xb) {
        int32_t swap = xa;
        xa = xb;
        xb = swap;
    }
    int first = xa >> 8, last = xb > xa ? (xb - 1) >> 8 : first;
    if (first == last) {
        cover[first] += height;
        area[first] += ((xa - first * SUB) + (xb - first * SUB)) * height;
        return;
    }
    int32_t width = xb - xa, remaining = height, x = xa;
    for (int cell = first; cell <= last; cell++) {
        int32_t next = cell == last ? xb : (cell + 1) * SUB;
        int32_t part = cell == last ? remaining
                                    : (int32_t)((int64_t)height * (next - x) / width);
        remaining -= part;
        cover[cell] += part;
        area[cell] += ((x - cell * SUB) + (next - cell * SUB)) * part;
        x = next;
    }
}

static void fill_edge(int32_t *cover, int32_t *area, int stride, const edge_t *e,
                      int32_t left, int32_t top)
{
    int32_t x0 = e->x0 - left, y0 = e->y0 - top, x1 = e->x1 - left, y1 = e->y1 - top;
    int direction = 1;
    if (y0 > y1) {
        int32_t swap = x0;
        x0 = x1;
        x1 = swap;
        swap = y0;
        y0 = y1;
        y1 = swap;
        direction = -1;
    }
    int64_t dx = (int64_t)x1 - x0, dy = (int64_t)y1 - y0;
    int32_t xa = x0, ya = y0;
    for (int row = y0 >> 8; row <= (y1 - 1) >> 8; row++) {
        int32_t yb = (row + 1) * SUB < y1 ? (row + 1) * SUB : y1;
        int32_t xb = yb == y1 ? x1 : (int32_t)(x0 + dx * (yb - y0) / dy);
        row_part(cover + (size_t)row * stride, area + (size_t)row * stride, xa, xb,
                 (yb - ya) * direction);
        xa = xb;
        ya = yb;
    }
}

bool ttf_render(const ttf_t *f, int glyph, int size, bool oblique, ttf_bitmap_t *out)
{
    memset(out, 0, sizeof(*out));
    if (size < PIXELS_MIN || size > PIXELS_MAX || glyph < 0 || glyph >= f->glyph_count)
        return false;

    /* Schrifteinheiten -> 1/256 Bildpunkt; y zeigt in der Schrift nach oben */
    matrix_t m = { 0 };
    int64_t scale = ((int64_t)size * SUB << 16) / f->units_per_em;
    m.xx = scale;
    m.yy = -scale;
    if (oblique)
        m.xy = scale * 13 / 64;         /* etwa 11 Grad Neigung */

    outline_t o = { .min_x = INT32_MAX, .min_y = INT32_MAX, .max_x = INT32_MIN,
                    .max_y = INT32_MIN };
    load_glyph(f, glyph, &m, &o, 0);
    bool ok = !o.failed;
    if (!ok || o.count == 0)
        goto done;

    int left = o.min_x >> 8, top = o.min_y >> 8;
    int width = (o.max_x >> 8) - left + 1, height = (o.max_y >> 8) - top + 1;
    if (width < 1 || height < 1 || width > 2048 || height > 2048) {
        ok = false;
        goto done;
    }

    int stride = width + 2;
    int32_t *cover = kzalloc((size_t)stride * (size_t)height * sizeof(int32_t));
    int32_t *area = kzalloc((size_t)stride * (size_t)height * sizeof(int32_t));
    uint8_t *coverage = kmalloc((size_t)width * (size_t)height);
    if (!cover || !area || !coverage) {
        kfree(cover);
        kfree(area);
        kfree(coverage);
        ok = false;
        goto done;
    }
    for (int i = 0; i < o.count; i++)
        fill_edge(cover, area, stride, &o.edges[i], left * SUB, top * SUB);

    for (int y = 0; y < height; y++) {
        int32_t sum = 0;
        for (int x = 0; x < width; x++) {
            sum += cover[(size_t)y * stride + x];
            int32_t value = sum * (2 * SUB) - area[(size_t)y * stride + x];
            if (value < 0)
                value = -value;
            value >>= 9;
            coverage[(size_t)y * width + x] = (uint8_t)(value > 255 ? 255 : value);
        }
    }
    kfree(cover);
    kfree(area);
    out->width = width;
    out->height = height;
    out->left = left;
    out->top = top;
    out->coverage = coverage;

done:
    kfree(o.edges);
    return ok;
}
