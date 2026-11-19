/* JavaScript: JSON, Date, Zeitgeber - und der in JavaScript selbst geschriebene Teil */
#include "js_impl.h"
#include "string.h"

#define ARGI(i) (J->bot + 2 + (i))
#define THISI (J->bot + 1)
#define MAGIC (js_callee(J)->u.n.magic)

/* --- JSON lesen ------------------------------------------------------------------------------------ */

typedef struct {
    js_state *J;
    const char *p, *end;
    int depth;
    /*
     * Für JSON.parse mit Nachbearbeitung (reviver): Neben jedem Wert entsteht
     * sein Abbild - bei einfachen Werten ihr Quelltext, bei Feldern und
     * Objekten die Abbilder ihrer Glieder. Dann legt json_value() zwei Werte
     * auf den Stapel: erst den Wert, dann das Abbild.
     */
    bool sources;
} json_in;

static bool json_fail(json_in *in)
{
    return js_throw(in->J, JS_E_SYNTAX, "Unexpected token in JSON");
}

static void json_space(json_in *in)
{
    while (in->p < in->end && (*in->p == ' ' || *in->p == '\t' || *in->p == '\n' || *in->p == '\r'))
        in->p++;
}

static int json_hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if ((c | 32) >= 'a' && (c | 32) <= 'f') return (c | 32) - 'a' + 10;
    return -1;
}

/* Liest eine Zeichenkette ab dem öffnenden Anführungszeichen und legt sie auf den Stapel. */
static bool json_string(json_in *in)
{
    js_state *J = in->J;
    const char *start = ++in->p;
    bool plain = true;
    while (in->p < in->end && *in->p != '"') {
        if (*in->p == '\\') {
            plain = false;
            in->p++;
        }
        if ((unsigned char)*in->p < 0x20)
            return json_fail(in);
        in->p++;
    }
    if (in->p >= in->end)
        return json_fail(in);
    const char *stop = in->p++;
    if (plain)
        return js_push_lstr(J, start, (size_t)(stop - start));
    size_t cap = (size_t)(stop - start) + 4, n = 0;
    char *out = js_alloc(J, cap);
    if (!out)
        return false;
    for (const char *q = start; q < stop; q++) {
        if (*q != '\\') {
            out[n++] = *q;
            continue;
        }
        char c = *++q;
        uint32_t code = 0;
        switch (c) {
        case 'n': out[n++] = '\n'; break;
        case 't': out[n++] = '\t'; break;
        case 'r': out[n++] = '\r'; break;
        case 'b': out[n++] = '\b'; break;
        case 'f': out[n++] = '\f'; break;
        case 'u':
            for (int i = 1; i <= 4; i++) {
                int h = q + i < stop ? json_hex(q[i]) : -1;
                if (h < 0) {
                    js_release(J, out, cap);
                    return json_fail(in);
                }
                code = code * 16 + (uint32_t)h;
            }
            q += 4;
            if (code >= 0xD800 && code <= 0xDBFF && stop - q > 6 && q[1] == '\\' && q[2] == 'u') {
                uint32_t low = 0;
                bool ok = true;
                for (int i = 3; i <= 6; i++) {
                    int h = json_hex(q[i]);
                    ok &= h >= 0;
                    low = low * 16 + (uint32_t)(h < 0 ? 0 : h);
                }
                if (ok && low >= 0xDC00 && low <= 0xDFFF) {
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    q += 6;
                }
            }
            n += (size_t)js_utf8_encode(code, out + n);
            break;
        default:
            out[n++] = c;               /* \" \\ \/ */
        }
    }
    js_string *s = js_string_new(J, out, n);
    js_release(J, out, cap);
    return js_push(J, js_str(s));
}

static bool json_value(json_in *in)
{
    js_state *J = in->J;
    json_space(in);
    if (in->p >= in->end || ++in->depth > 400 || JS_STACK_HERE() < J->stack_floor ||
        J->top + 100 > JS_STACK_SIZE)
        return json_fail(in);
    char c = *in->p;
    const char *start = in->p;
    bool ok = true;
    int extra = in->sources ? 1 : 0;    /* so viele Werte mehr liegen je Wert auf dem Stapel */
    if (c == '{') {
        js_object *o = js_object_new(J, JS_C_OBJECT, J->object_proto);
        js_object *shadow = in->sources ? js_object_new(J, JS_C_OBJECT, NULL) : NULL;
        js_push(J, js_obj(o));
        if (shadow)
            js_push(J, js_obj(shadow));
        in->p++;
        json_space(in);
        if (in->p < in->end && *in->p == '}') {
            in->p++;
        } else {
            for (;;) {
                json_space(in);
                if (in->p >= in->end || *in->p != '"' || !json_string(in))
                    return json_fail(in);
                js_string *key = js_intern_string(J, js_top(J, 0)->u.s);
                *js_top(J, 0) = js_str(key);
                json_space(in);
                if (in->p >= in->end || *in->p++ != ':')
                    return json_fail(in);
                if (!json_value(in))
                    return false;
                js_define(J, o, key, *js_top(J, extra), JS_P_ALL);
                if (shadow)
                    js_define(J, shadow, key, *js_top(J, 0), JS_P_ALL);
                js_pop(J, 2 + extra);
                json_space(in);
                if (in->p < in->end && *in->p == ',') {
                    in->p++;
                    continue;
                }
                if (in->p < in->end && *in->p == '}') {
                    in->p++;
                    break;
                }
                return json_fail(in);
            }
        }
    } else if (c == '[') {
        js_object *a = js_array_new(J, 0);
        js_object *shadow = in->sources ? js_array_new(J, 0) : NULL;
        js_push(J, js_obj(a));
        if (shadow)
            js_push(J, js_obj(shadow));
        in->p++;
        json_space(in);
        if (in->p < in->end && *in->p == ']') {
            in->p++;
        } else {
            for (;;) {
                if (!json_value(in))
                    return false;
                if (!js_array_push(J, a, *js_top(J, extra)) ||
                    (shadow && !js_array_push(J, shadow, *js_top(J, 0))))
                    return false;
                js_pop(J, 1 + extra);
                json_space(in);
                if (in->p < in->end && *in->p == ',') {
                    in->p++;
                    continue;
                }
                if (in->p < in->end && *in->p == ']') {
                    in->p++;
                    break;
                }
                return json_fail(in);
            }
        }
    } else if (c == '"') {
        ok = json_string(in);
    } else if (c == '-' || (c >= '0' && c <= '9')) {
        size_t used = 0;
        double n = js_text_to_number(in->p, (size_t)(in->end - in->p), &used);
        if (used == 0 || (c == '-' && used == 1))
            return json_fail(in);
        in->p += used;
        js_push(J, js_num(n));
    } else if (in->end - in->p >= 4 && memcmp(in->p, "true", 4) == 0) {
        in->p += 4;
        js_push(J, js_bool(true));
    } else if (in->end - in->p >= 5 && memcmp(in->p, "false", 5) == 0) {
        in->p += 5;
        js_push(J, js_bool(false));
    } else if (in->end - in->p >= 4 && memcmp(in->p, "null", 4) == 0) {
        in->p += 4;
        js_push(J, js_null());
    } else {
        return json_fail(in);
    }
    /* einfache Werte: ihr Abbild ist ihr Quelltext */
    if (ok && in->sources && c != '{' && c != '[')
        ok = js_push_lstr(J, start, (size_t)(in->p - start));
    in->depth--;
    return ok;
}

/* Steht in source (Quelltext eines einfachen Werts) genau der Wert v? */
static bool json_same(js_state *J, const js_string *source, const js_value *v)
{
    json_in in = { J, source->data, source->data + source->len, 0, false };
    js_value kept = *v;
    if (!json_value(&in))
        return false;
    bool same = js_strict_equal(js_top(J, 0), &kept);
    js_pop(J, 1);
    return same;
}

/*
 * Nachbearbeitung: ruft reviver für jedes Glied (die inneren zuerst) und
 * setzt ein, was er liefert; undefined entfernt das Glied. Der dritte
 * Parameter des reviver nennt bei einfachen Werten ihren Quelltext - solange
 * der Wert noch der gelesene ist. holder und reviver sind Plätze auf dem
 * Stapel; shadow ist das Abbild des Werts oder NULL. Legt das Ergebnis auf
 * den Stapel.
 */
static bool json_revive(js_state *J, int holder, js_string *key, int reviver, const js_value *shadow,
                        int depth)
{
    if (depth > 400 || JS_STACK_HERE() < J->stack_floor || J->top + 100 > JS_STACK_SIZE)
        return js_throw(J, JS_E_RANGE, "Maximum call stack size exceeded");
    if (!js_get(J, holder, key))
        return false;
    int at = J->top - 1;                /* hier liegt der Wert */
    js_value snap = shadow ? *shadow : js_undef();
    js_push(J, snap);                   /* festhalten: reviver dürfen alles umbauen */
    if (J->stack[at].t == JS_T_OBJ) {
        js_object *keys = js_array_new(J, 0);
        js_push(J, js_obj(keys));
        if (!js_own_keys(J, J->stack[at].u.o, true, keys))
            return false;
        bool list = js_is_array(&J->stack[at]);
        bool fits = snap.t == JS_T_OBJ && list == js_is_array(&snap);
        for (uint32_t i = 0; i < keys->u.arr.len; i++) {
            js_string *k = keys->u.arr.v[i].u.s;
            js_value child = js_undef();
            if (J->stack[at].t != JS_T_OBJ)
                break;
            if (fits && list && k->index < snap.u.o->u.arr.len) {
                child = snap.u.o->u.arr.v[k->index];
            } else if (fits && !list) {
                js_prop *p = js_own(snap.u.o, k);
                if (p)
                    child = p->v;
            }
            js_push(J, child);
            if (!json_revive(J, at, k, reviver, child.t == JS_T_UNDEF ? NULL : js_top(J, 0), depth + 1))
                return false;
            if (js_top(J, 0)->t == JS_T_UNDEF) {
                js_pop(J, 1);
                if (J->stack[at].t == JS_T_OBJ && js_is_array(&J->stack[at])) {
                    if (k->index < J->stack[at].u.o->u.arr.len)
                        J->stack[at].u.o->u.arr.v[k->index] = js_undef();
                } else if (J->stack[at].t == JS_T_OBJ) {
                    js_delete_own(J, J->stack[at].u.o, k);
                }
            } else if (!js_put(J, at, k)) {
                return false;
            }
            js_pop(J, 1);               /* das Abbild des Glieds */
        }
        js_pop(J, 1);                   /* die Namen */
    }
    js_object *context = js_object_new(J, JS_C_OBJECT, J->object_proto);
    js_push(J, js_obj(context));
    if (J->stack[at].t != JS_T_OBJ && snap.t == JS_T_STR && json_same(J, snap.u.s, &J->stack[at]))
        js_define(J, context, js_intern_cstr(J, "source"), snap, JS_P_ALL);
    int ctx = J->top - 1;
    js_push(J, J->stack[reviver]);
    js_push(J, J->stack[holder]);
    js_push(J, js_str(key));
    js_push(J, J->stack[at]);
    js_push(J, J->stack[ctx]);
    if (!js_call(J, 3))
        return false;
    J->stack[at] = *js_top(J, 0);       /* Ergebnis an die Stelle des Werts */
    J->top = at + 1;
    return true;
}

static bool json_parse(js_state *J, int argc)
{
    if (!js_to_string(J, ARGI(0)))
        return false;
    js_string *s = js_arg(J, 0)->u.s;
    bool revive = argc > 1 && js_is_callable(js_arg(J, 1));
    json_in in = { J, s->data, s->data + s->len, 0, revive };
    if (!json_value(&in))
        return false;
    json_space(&in);
    if (in.p != in.end)
        return json_fail(&in);
    if (!revive)
        return true;
    /* oben liegen der Wert und sein Abbild: in ein Objekt mit dem Namen "" hängen und durchgehen */
    js_object *root = js_object_new(J, JS_C_OBJECT, J->object_proto);
    js_define(J, root, J->k[K_empty], *js_top(J, 1), JS_P_ALL);
    js_push(J, js_obj(root));
    int holder = J->top - 1;
    return json_revive(J, holder, J->k[K_empty], ARGI(1), &J->stack[holder - 1], 0);
}

/* --- JSON schreiben -------------------------------------------------------------------------------- */

typedef struct {
    js_state *J;
    char *text;
    size_t len, cap;
    const char *indent;
    size_t indent_len;
    int depth;
    int allow;                          /* Platz des Felds erlaubter Namen oder -1 */
} json_out;

static bool json_put(json_out *out, const char *text, size_t n)
{
    js_state *J = out->J;
    if (out->len + n > out->cap) {
        size_t cap = out->cap ? out->cap * 2 : 256;
        while (cap < out->len + n)
            cap *= 2;
        if (cap > JS_STRING_MAX)
            return js_throw(J, JS_E_RANGE, "JSON text too long");
        char *bigger = js_alloc(J, cap);
        if (!bigger)
            return false;
        if (out->len)
            memcpy(bigger, out->text, out->len);
        js_release(J, out->text, out->cap);
        out->text = bigger;
        out->cap = cap;
    }
    memcpy(out->text + out->len, text, n);
    out->len += n;
    return true;
}

static bool json_quote(json_out *out, const js_string *s)
{
    if (!json_put(out, "\"", 1))
        return false;
    for (uint32_t i = 0; i < s->len; i++) {
        unsigned char c = (unsigned char)s->data[i];
        char esc[8];
        if (c == '"' || c == '\\') {
            esc[0] = '\\';
            esc[1] = (char)c;
            if (!json_put(out, esc, 2))
                return false;
        } else if (c < 0x20) {
            static const char hex[] = "0123456789abcdef";
            size_t n = 2;
            esc[0] = '\\';
            if (c == '\n') esc[1] = 'n';
            else if (c == '\r') esc[1] = 'r';
            else if (c == '\t') esc[1] = 't';
            else {
                esc[1] = 'u'; esc[2] = '0'; esc[3] = '0'; esc[4] = hex[c >> 4]; esc[5] = hex[c & 15];
                n = 6;
            }
            if (!json_put(out, esc, n))
                return false;
        } else if (!json_put(out, (const char *)&s->data[i], 1)) {
            return false;
        }
    }
    return json_put(out, "\"", 1);
}

static bool json_newline(json_out *out, int depth)
{
    if (!out->indent_len)
        return true;
    if (!json_put(out, "\n", 1))
        return false;
    for (int i = 0; i < depth; i++) {
        if (!json_put(out, out->indent, out->indent_len))
            return false;
    }
    return true;
}

/* Schreibt den Wert oben auf dem Stapel (bleibt liegen); *skipped: war nicht darstellbar */
static bool json_write(json_out *out, bool *skipped)
{
    js_state *J = out->J;
    int slot = J->top - 1;
    *skipped = false;
    if (out->depth > 200 || JS_STACK_HERE() < J->stack_floor || J->top + 100 > JS_STACK_SIZE)
        return js_throw(J, JS_E_TYPE, "Converting circular structure to JSON");
    /* toJSON, falls vorhanden (etwa bei Date) */
    if (J->stack[slot].t == JS_T_OBJ && (J->stack[slot].u.o->cls == JS_C_PROXY ||
                                         js_lookup(J->stack[slot].u.o, J->k[K_toJSON], NULL))) {
        if (!js_get(J, slot, J->k[K_toJSON]))
            return false;
        if (js_is_callable(js_top(J, 0))) {
            js_push(J, J->stack[slot]);
            if (!js_call(J, 0))
                return false;
            J->stack[slot] = *js_top(J, 0);
        }
        js_pop(J, 1);
    }
    js_value *v = &J->stack[slot];
    if (v->t == JS_T_OBJ && (v->u.o->cls == JS_C_NUMBER || v->u.o->cls == JS_C_STRING ||
                             v->u.o->cls == JS_C_BOOLEAN))
        *v = v->u.o->u.prim;
    char number[48];
    switch (v->t) {
    case JS_T_NULL: return json_put(out, "null", 4);
    case JS_T_BOOL: return json_put(out, v->u.b ? "true" : "false", v->u.b ? 4 : 5);
    case JS_T_NUM:
        if (!js_isfinite(v->u.n))
            return json_put(out, "null", 4);
        return json_put(out, number, (size_t)js_number_to_text(v->u.n, number));
    case JS_T_STR: return json_quote(out, v->u.s);
    case JS_T_UNDEF: case JS_T_SYM: *skipped = true; return true;
    default: break;
    }
    js_object *o = v->u.o;
    if (js_is_callable(v)) {
        *skipped = true;
        return true;
    }
    out->depth++;
    if (o->cls == JS_C_PROXY) {
        js_object *real = js_proxy_unwrap(o);
        if (!real)
            return js_throw(J, JS_E_TYPE, "Cannot perform 'get' on a proxy that has been revoked");
        if (real->cls == JS_C_ARRAY) {
            /* ein Proxy vor einem Feld: Länge und Glieder über den Proxy erfragen */
            double length;
            if (!js_get(J, slot, J->k[K_length]) || !js_to_number(J, J->top - 1, &length))
                return false;
            js_pop(J, 1);
            if (!json_put(out, "[", 1))
                return false;
            uint32_t n = length > 0 && length < JS_ARRAY_MAX ? (uint32_t)length : 0;
            for (uint32_t i = 0; i < n; i++) {
                bool none;
                if ((i && !json_put(out, ",", 1)) || !json_newline(out, out->depth))
                    return false;
                if (!js_get_index(J, slot, i) || !json_write(out, &none))
                    return false;
                js_pop(J, 1);
                if (none && !json_put(out, "null", 4))
                    return false;
            }
            out->depth--;
            if (n && !json_newline(out, out->depth))
                return false;
            return json_put(out, "]", 1);
        }
    }
    if (o->cls == JS_C_ARRAY) {
        if (!json_put(out, "[", 1))
            return false;
        for (uint32_t i = 0; i < o->u.arr.len; i++) {
            bool none;
            if ((i && !json_put(out, ",", 1)) || !json_newline(out, out->depth))
                return false;
            js_push(J, o->u.arr.v[i]);
            if (!json_write(out, &none))
                return false;
            js_pop(J, 1);
            if (none && !json_put(out, "null", 4))
                return false;
        }
        out->depth--;
        if (o->u.arr.len && !json_newline(out, out->depth))
            return false;
        return json_put(out, "]", 1);
    }
    js_object *keys = js_array_new(J, 0);
    js_push(J, js_obj(keys));
    if (!js_own_keys(J, o, true, keys) || !json_put(out, "{", 1))
        return false;
    bool first = true;
    for (uint32_t i = 0; i < keys->u.arr.len; i++) {
        js_string *key = keys->u.arr.v[i].u.s;
        if (out->allow >= 0) {
            js_object *allow = J->stack[out->allow].u.o;
            bool listed = false;
            for (uint32_t k = 0; k < allow->u.arr.len && !listed; k++)
                listed = allow->u.arr.v[k].t == JS_T_STR &&
                         js_string_equal(allow->u.arr.v[k].u.s, key);
            if (!listed)
                continue;
        }
        size_t mark = out->len;
        bool none;
        if ((!first && !json_put(out, ",", 1)) || !json_newline(out, out->depth) ||
            !json_quote(out, key) || !json_put(out, out->indent_len ? ": " : ":", out->indent_len ? 2 : 1))
            return false;
        if (!js_get(J, slot, key) || !json_write(out, &none))
            return false;
        js_pop(J, 1);
        if (none)
            out->len = mark;            /* undefined und Funktionen: Eigenschaft weglassen */
        else
            first = false;
    }
    js_pop(J, 1);
    out->depth--;
    if (!first && !json_newline(out, out->depth))
        return false;
    return json_put(out, "}", 1);
}

static bool json_stringify(js_state *J, int argc)
{
    json_out out = { J, NULL, 0, 0, "", 0, 0, -1 };
    char spaces[11] = "          ";
    if (argc > 1 && js_is_array(js_arg(J, 1)))
        out.allow = ARGI(1);
    if (argc > 2) {
        js_value *gap = js_arg(J, 2);
        if (gap->t == JS_T_NUM && gap->u.n >= 1) {
            out.indent = spaces;
            out.indent_len = gap->u.n > 10 ? 10 : (size_t)gap->u.n;
        } else if (gap->t == JS_T_STR && gap->u.s->len) {
            out.indent = gap->u.s->data;
            out.indent_len = gap->u.s->len > 10 ? 10 : gap->u.s->len;
        }
    }
    bool skipped;
    js_push(J, argc > 0 ? *js_arg(J, 0) : js_undef());
    bool ok = json_write(&out, &skipped);
    if (ok)
        ok = skipped ? js_push(J, js_undef()) : js_push_lstr(J, out.text, out.len);
    js_release(J, out.text, out.cap);
    return ok;
}

/* --- Date ------------------------------------------------------------------------------------------ */

static int64_t days_from_civil(int64_t y, int64_t m, int64_t d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

typedef struct {
    int64_t year;
    int month, day, hour, minute, second, ms, weekday;
} date_parts;

static void split_time(double t, date_parts *p)
{
    int64_t ms = (int64_t)t;
    int64_t days = ms >= 0 ? ms / 86400000 : -((-ms + 86399999) / 86400000);
    int64_t rest = ms - days * 86400000;
    p->hour = (int)(rest / 3600000);
    p->minute = (int)(rest / 60000 % 60);
    p->second = (int)(rest / 1000 % 60);
    p->ms = (int)(rest % 1000);
    p->weekday = (int)(((days % 7) + 11) % 7);       /* 1.1.1970 war ein Donnerstag */
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    p->day = (int)(doy - (153 * mp + 2) / 5 + 1);
    p->month = (int)(mp < 10 ? mp + 3 : mp - 9);
    p->year = yoe + era * 400 + (p->month <= 2);
}

static double make_time(double year, double month, double day, double hour, double minute,
                        double second, double ms)
{
    if (js_isnan(year) || js_isnan(month) || js_isnan(day) || js_isnan(hour) || js_isnan(minute) ||
        js_isnan(second) || js_isnan(ms) || year < -200000 || year > 200000 || month < -1e6 ||
        month > 1e6 || day < -1e8 || day > 1e8)
        return js_nan();
    /* Monate über 11 und unter 0 laufen ins nächste oder vorige Jahr */
    double y = year + js_floor(month / 12), m = month - js_floor(month / 12) * 12;
    double days = (double)days_from_civil((int64_t)y, (int64_t)m + 1, 1) + day - 1;
    double t = days * 86400000.0 + hour * 3600000.0 + minute * 60000.0 + second * 1000.0 + ms;
    return t > 8.64e15 || t < -8.64e15 ? js_nan() : js_trunc(t);
}

static int two_digits(const char *p)
{
    return (p[0] - '0') * 10 + (p[1] - '0');
}

static bool all_digits(const char *p, int n, const char *end)
{
    if (end - p < n)
        return false;
    for (int i = 0; i < n; i++) {
        if (p[i] < '0' || p[i] > '9')
            return false;
    }
    return true;
}

static double parse_date(const js_string *s)
{
    static const char *const months[] = { "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug",
                                          "sep", "oct", "nov", "dec" };
    const char *p = s->data, *end = s->data + s->len;
    while (p < end && *p == ' ')
        p++;
    /* 2026-11-15T15:30:00.000Z und Kürzeres */
    if (all_digits(p, 4, end) && (end - p == 4 || p[4] == '-')) {
        double year = two_digits(p) * 100 + two_digits(p + 2), month = 1, day = 1;
        double hour = 0, minute = 0, second = 0, ms = 0, offset = 0;
        p += 4;
        if (p < end && *p == '-' && all_digits(p + 1, 2, end)) {
            month = two_digits(p + 1);
            p += 3;
            if (p < end && *p == '-' && all_digits(p + 1, 2, end)) {
                day = two_digits(p + 1);
                p += 3;
            }
        }
        if (p < end && (*p == 'T' || *p == ' ') && all_digits(p + 1, 2, end) && end - p >= 6 &&
            p[3] == ':' && all_digits(p + 4, 2, end)) {
            hour = two_digits(p + 1);
            minute = two_digits(p + 4);
            p += 6;
            if (p < end && *p == ':' && all_digits(p + 1, 2, end)) {
                second = two_digits(p + 1);
                p += 3;
                if (p < end && (*p == '.' || *p == ',')) {
                    double scale = 100;
                    for (p++; p < end && *p >= '0' && *p <= '9'; p++, scale /= 10)
                        ms += (*p - '0') * scale;
                }
            }
            if (p < end && (*p == '+' || *p == '-') && all_digits(p + 1, 2, end)) {
                double sign = *p == '-' ? -1 : 1;
                offset = two_digits(p + 1) * 60;
                p += 3;
                if (p < end && *p == ':')
                    p++;
                if (all_digits(p, 2, end)) {
                    offset += two_digits(p);
                    p += 2;
                }
                offset *= sign;
            } else if (p < end && *p == 'Z') {
                p++;
            }
        }
        while (p < end && *p == ' ')
            p++;
        if (p != end)
            return js_nan();
        return make_time(year, month - 1, day, hour, minute - offset, second, js_trunc(ms));
    }
    /* freiere Schreibweisen: "Nov 15, 2026 15:30:00", "15 Nov 2026", "11/15/2026" */
    double year = js_nan(), month = js_nan(), day = js_nan(), time[3] = { 0, 0, 0 };
    double numbers[3];
    int count = 0, times = 0;
    bool pm = false, am = false;
    while (p < end) {
        if (*p >= '0' && *p <= '9') {
            double n = 0;
            int digits = 0;
            for (; p < end && *p >= '0' && *p <= '9' && digits < 9; p++, digits++)
                n = n * 10 + (*p - '0');
            if (p < end && *p == ':' && times == 0) {
                time[times++] = n;
                while (p < end && *p == ':' && times < 3) {
                    n = 0;
                    for (p++; p < end && *p >= '0' && *p <= '9'; p++)
                        n = n * 10 + (*p - '0');
                    time[times++] = n;
                }
                while (p < end && (*p == '.' || (*p >= '0' && *p <= '9')))
                    p++;
            } else if (digits >= 3 || n > 31) {
                year = n;
            } else if (count < 3) {
                numbers[count++] = n;
            }
        } else if ((*p | 32) >= 'a' && (*p | 32) <= 'z') {
            char word[4] = { 0, 0, 0, 0 };
            int n = 0;
            for (; p < end && (*p | 32) >= 'a' && (*p | 32) <= 'z'; p++) {
                if (n < 3)
                    word[n++] = (char)(*p | 32);
            }
            for (int i = 0; i < 12 && n == 3; i++) {
                if (memcmp(word, months[i], 3) == 0)
                    month = i;
            }
            if (n == 2 && word[0] == 'p' && word[1] == 'm')
                pm = true;
            if (n == 2 && word[0] == 'a' && word[1] == 'm')
                am = true;
        } else if ((*p == '+' || *p == '-') && times > 0) {
            break;                      /* Zeitzonenangabe: hier gilt die Ortszeit */
        } else {
            p++;
        }
    }
    if (js_isnan(month)) {
        if (count < 2)
            return js_nan();
        month = numbers[0] - 1;         /* Monat/Tag/Jahr */
        day = numbers[1];
        if (js_isnan(year) && count > 2)
            year = numbers[2] < 50 ? numbers[2] + 2000 : numbers[2] + 1900;
    } else if (count > 0) {
        day = numbers[0];
        if (js_isnan(year) && count > 1)
            year = numbers[1] < 50 ? numbers[1] + 2000 : numbers[1] + 1900;
    }
    if (js_isnan(year) || js_isnan(day))
        return js_nan();
    if (pm && time[0] < 12)
        time[0] += 12;
    if (am && time[0] == 12)
        time[0] = 0;
    return make_time(year, month, day, time[0], time[1], time[2], 0);
}

static double now_ms(js_state *J)
{
    return J->host.clock_ms ? (double)J->host.clock_ms(J->host.ctx) : 0;
}

static int put_number(char *out, int64_t value, int width)
{
    char tmp[24];
    int k = 0, n = 0;
    bool negative = value < 0;
    uint64_t u = negative ? (uint64_t)-value : (uint64_t)value;
    do {
        tmp[k++] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    if (negative)
        out[n++] = '-';
    for (int i = k; i < width; i++)
        out[n++] = '0';
    while (k > 0)
        out[n++] = tmp[--k];
    return n;
}

static bool push_date_text(js_state *J, double t, int kind)
{
    static const char *const days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *const months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug",
                                          "Sep", "Oct", "Nov", "Dec" };
    if (js_isnan(t)) {
        if (kind == 0)
            return js_throw(J, JS_E_RANGE, "Invalid time value");
        return js_push_cstr(J, "Invalid Date");
    }
    date_parts p;
    split_time(t, &p);
    char text[80];
    int n = 0;
    switch (kind) {
    case 0:                             /* ISO */
        n += put_number(text + n, p.year, 4);
        text[n++] = '-';
        n += put_number(text + n, p.month, 2);
        text[n++] = '-';
        n += put_number(text + n, p.day, 2);
        text[n++] = 'T';
        n += put_number(text + n, p.hour, 2);
        text[n++] = ':';
        n += put_number(text + n, p.minute, 2);
        text[n++] = ':';
        n += put_number(text + n, p.second, 2);
        text[n++] = '.';
        n += put_number(text + n, p.ms, 3);
        text[n++] = 'Z';
        break;
    case 1: case 2: case 3:             /* toString, toDateString, toTimeString */
        if (kind != 3) {
            memcpy(text + n, days[p.weekday], 3);
            n += 3;
            text[n++] = ' ';
            memcpy(text + n, months[p.month - 1], 3);
            n += 3;
            text[n++] = ' ';
            n += put_number(text + n, p.day, 2);
            text[n++] = ' ';
            n += put_number(text + n, p.year, 4);
        }
        if (kind == 1)
            text[n++] = ' ';
        if (kind != 2) {
            n += put_number(text + n, p.hour, 2);
            text[n++] = ':';
            n += put_number(text + n, p.minute, 2);
            text[n++] = ':';
            n += put_number(text + n, p.second, 2);
            memcpy(text + n, " GMT+0000", 9);
            n += 9;
        }
        break;
    default:                            /* deutsch: 15.11.2026, 15:30:00 */
        if (kind != 6) {
            n += put_number(text + n, p.day, 1);
            text[n++] = '.';
            n += put_number(text + n, p.month, 1);
            text[n++] = '.';
            n += put_number(text + n, p.year, 4);
        }
        if (kind == 4) {
            text[n++] = ',';
            text[n++] = ' ';
        }
        if (kind != 5) {
            n += put_number(text + n, p.hour, 2);
            text[n++] = ':';
            n += put_number(text + n, p.minute, 2);
            text[n++] = ':';
            n += put_number(text + n, p.second, 2);
        }
        break;
    }
    return js_push_lstr(J, text, (size_t)n);
}

static bool date_ctor(js_state *J, int argc)
{
    double t;
    if (!J->constructing)
        return push_date_text(J, now_ms(J), 1);
    if (argc == 0) {
        t = now_ms(J);
    } else if (argc == 1) {
        js_value *v = js_arg(J, 0);
        if (v->t == JS_T_OBJ && v->u.o->cls == JS_C_DATE) {
            t = v->u.o->u.prim.u.n;
        } else {
            if (!js_to_primitive(J, ARGI(0), 0))
                return false;
            if (js_arg(J, 0)->t == JS_T_STR) {
                t = parse_date(js_arg(J, 0)->u.s);
            } else if (!js_to_number(J, ARGI(0), &t)) {
                return false;
            } else {
                t = js_isfinite(t) ? js_trunc(t) : js_nan();
            }
        }
    } else {
        double part[7] = { 0, 0, 1, 0, 0, 0, 0 };
        for (int i = 0; i < argc && i < 7; i++) {
            if (!js_to_number(J, ARGI(i), &part[i]))
                return false;
            part[i] = js_trunc(part[i]);
        }
        if (part[0] >= 0 && part[0] <= 99)
            part[0] += 1900;
        t = make_time(part[0], part[1], part[2], part[3], part[4], part[5], part[6]);
    }
    js_object *d = js_object_new(J, JS_C_DATE, J->date_proto);
    d->u.prim = js_num(t);
    return js_push(J, js_obj(d));
}

static bool this_date(js_state *J, js_object **out)
{
    js_value *self = js_this(J);
    if (self->t != JS_T_OBJ || self->u.o->cls != JS_C_DATE)
        return js_throw(J, JS_E_TYPE, "Date method called on a value that is not a Date");
    *out = self->u.o;
    return true;
}

enum { D_TIME, D_YEAR, D_MONTH, D_DATE, D_DAY, D_HOURS, D_MINUTES, D_SECONDS, D_MS, D_OFFSET,
       D_YEAR2 };

static bool date_get(js_state *J, int argc)
{
    (void)argc;
    js_object *d;
    if (!this_date(J, &d))
        return false;
    double t = d->u.prim.u.n;
    if (MAGIC == D_OFFSET)
        return js_push(J, js_num(0));
    if (js_isnan(t) || MAGIC == D_TIME)
        return js_push(J, js_num(t));
    date_parts p;
    split_time(t, &p);
    double r = MAGIC == D_YEAR ? (double)p.year : MAGIC == D_MONTH ? p.month - 1
             : MAGIC == D_DATE ? p.day : MAGIC == D_DAY ? p.weekday : MAGIC == D_HOURS ? p.hour
             : MAGIC == D_MINUTES ? p.minute : MAGIC == D_SECONDS ? p.second
             : MAGIC == D_MS ? p.ms : (double)p.year - 1900;
    return js_push(J, js_num(r));
}

static bool date_set(js_state *J, int argc)
{
    js_object *d;
    if (!this_date(J, &d))
        return false;
    double a[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < argc && i < 4; i++) {
        if (!js_to_number(J, ARGI(i), &a[i]))
            return false;
        a[i] = js_trunc(a[i]);
    }
    double t = d->u.prim.u.n;
    if (MAGIC == D_TIME) {
        d->u.prim = js_num(argc > 0 && js_isfinite(a[0]) ? a[0] : js_nan());
        return js_push(J, d->u.prim);
    }
    date_parts p;
    split_time(js_isnan(t) ? 0 : t, &p);
    double year = (double)p.year, month = p.month - 1, day = p.day, hour = p.hour,
           minute = p.minute, second = p.second, ms = p.ms;
    switch (MAGIC) {
    case D_YEAR:
        year = a[0];
        if (argc > 1) month = a[1];
        if (argc > 2) day = a[2];
        break;
    case D_MONTH:
        month = a[0];
        if (argc > 1) day = a[1];
        break;
    case D_DATE: day = a[0]; break;
    case D_HOURS:
        hour = a[0];
        if (argc > 1) minute = a[1];
        if (argc > 2) second = a[2];
        if (argc > 3) ms = a[3];
        break;
    case D_MINUTES:
        minute = a[0];
        if (argc > 1) second = a[1];
        if (argc > 2) ms = a[2];
        break;
    case D_SECONDS:
        second = a[0];
        if (argc > 1) ms = a[1];
        break;
    default: ms = a[0]; break;
    }
    d->u.prim = js_num(argc == 0 ? js_nan() : make_time(year, month, day, hour, minute, second, ms));
    return js_push(J, d->u.prim);
}

static bool date_text(js_state *J, int argc)
{
    (void)argc;
    js_object *d;
    if (!this_date(J, &d))
        return false;
    if (MAGIC == 7) {                   /* toJSON */
        if (js_isnan(d->u.prim.u.n))
            return js_push(J, js_null());
        return push_date_text(J, d->u.prim.u.n, 0);
    }
    return push_date_text(J, d->u.prim.u.n, MAGIC);
}

static bool date_static(js_state *J, int argc)
{
    switch (MAGIC) {
    case 0:
        return js_push(J, js_num(now_ms(J)));
    case 1:
        if (!js_to_string(J, ARGI(0)))
            return false;
        return js_push(J, js_num(parse_date(js_arg(J, 0)->u.s)));
    default: {
        double part[7] = { 0, 0, 1, 0, 0, 0, 0 };
        for (int i = 0; i < argc && i < 7; i++) {
            if (!js_to_number(J, ARGI(i), &part[i]))
                return false;
            part[i] = js_trunc(part[i]);
        }
        if (part[0] >= 0 && part[0] <= 99)
            part[0] += 1900;
        return js_push(J, js_num(make_time(part[0], part[1], part[2], part[3], part[4], part[5],
                                           part[6])));
    }
    }
}

/* --- Zeitgeber und Nacharbeiten -------------------------------------------------------------------- */

static js_object *registry_array(js_state *J, const char *name)
{
    js_string *key = js_intern_cstr(J, name);
    js_prop *p = js_own(J->registry, key);
    if (p && js_is_array(&p->v))
        return p->v.u.o;
    js_object *a = js_array_new(J, 0);
    js_define(J, J->registry, key, js_obj(a), JS_P_ALL);
    return a;
}

static uint64_t ticks(js_state *J)
{
    return J->host.ticks_ms ? J->host.ticks_ms(J->host.ctx) : 0;
}

/* setTimeout (0), setInterval (1), requestAnimationFrame (2), setImmediate (3) */
static bool timer_set(js_state *J, int argc)
{
    js_value *fn = js_arg(J, 0);
    if (!js_is_callable(fn) && fn->t != JS_T_STR)
        return js_push(J, js_num(0));
    double delay = 0;
    if (MAGIC < 2 && argc > 1 && !js_to_number(J, ARGI(1), &delay))
        return false;
    if (MAGIC == 2)
        delay = 16;
    if (js_isnan(delay) || delay < 0)
        delay = 0;
    if (delay > 2147483647.0)
        delay = 2147483647.0;
    if (MAGIC == 1 && delay < 4)
        delay = 4;                      /* Dauerläufer bremsen */
    js_object *timers = registry_array(J, "timers");
    if (timers->cls == JS_C_ARRAY && timers->u.arr.len >= 2000)
        return js_throw(J, JS_E_RANGE, "Too many timers");
    js_prop *counter = js_own(J->registry, js_intern_cstr(J, "timer_id"));
    double id = counter && counter->v.t == JS_T_NUM ? counter->v.u.n + 1 : 1;
    js_define(J, J->registry, js_intern_cstr(J, "timer_id"), js_num(id), JS_P_ALL);
    /* Eintrag: [nummer, funktion, fällig, abstand, argumente...] */
    js_object *t = js_array_new(J, 4 + (argc > 2 ? (uint32_t)argc - 2 : 0));
    js_push(J, js_obj(t));
    js_array_push(J, t, js_num(id));
    js_array_push(J, t, *fn);
    js_array_push(J, t, js_num((double)ticks(J) + delay));
    js_array_push(J, t, js_num(MAGIC == 1 ? delay : 0));
    for (int i = 2; i < argc && MAGIC < 2; i++)
        js_array_push(J, t, *js_arg(J, i));
    if (!js_array_push(J, timers, js_obj(t)))
        return false;
    return js_push(J, js_num(id));
}

static bool timer_clear(js_state *J, int argc)
{
    (void)argc;
    js_value *id = js_arg(J, 0);
    js_object *timers = registry_array(J, "timers");
    for (uint32_t i = 0; id->t == JS_T_NUM && timers->cls == JS_C_ARRAY && i < timers->u.arr.len; i++) {
        js_object *t = timers->u.arr.v[i].u.o;
        if (t->u.arr.len >= 4 && t->u.arr.v[0].u.n == id->u.n) {
            memmove(timers->u.arr.v + i, timers->u.arr.v + i + 1,
                    (size_t)(timers->u.arr.len - i - 1) * sizeof(js_value));
            timers->u.arr.len--;
            break;
        }
    }
    return js_push(J, js_undef());
}

static bool queue_microtask(js_state *J, int argc)
{
    (void)argc;
    if (!js_is_callable(js_arg(J, 0)))
        return js_throw(J, JS_E_TYPE, "queueMicrotask needs a function");
    js_object *jobs = registry_array(J, "jobs");
    if (jobs->cls == JS_C_ARRAY && jobs->u.arr.len >= 100000)
        return js_throw(J, JS_E_RANGE, "Too many pending jobs");
    if (!js_array_push(J, jobs, *js_arg(J, 0)))
        return false;
    return js_push(J, js_undef());
}

/* Erledigt, was sich Promises für "gleich danach" vorgemerkt haben. */
void js_jobs_run(js_state *J)
{
    for (int round = 0; round < 10000 && !J->fatal; round++) {
        js_object *jobs = registry_array(J, "jobs");
        if (jobs->cls != JS_C_ARRAY || jobs->u.arr.len == 0)
            return;
        /* die Liste austauschen: Was jetzt dazukommt, ist in der nächsten Runde dran */
        int base = J->top;
        js_push(J, js_obj(jobs));
        js_define(J, J->registry, js_intern_cstr(J, "jobs"), js_obj(js_array_new(J, 0)), JS_P_ALL);
        for (uint32_t i = 0; i < jobs->u.arr.len && !J->fatal; i++) {
            js_push(J, jobs->u.arr.v[i]);
            js_push(J, js_undef());
            bool ok = js_call(J, 0);
            if (!ok) {
                js_leave(J, false);     /* melden und weitermachen */
                J->top = base + 1;
            } else {
                js_pop(J, 1);
            }
        }
        J->top = base;
    }
}

int64_t js_timers_due(js_state *J)
{
    js_object *timers = registry_array(J, "timers");
    if (J->fatal || timers->cls != JS_C_ARRAY || timers->u.arr.len == 0)
        return -1;
    double first = 1e300, now = (double)ticks(J);
    for (uint32_t i = 0; i < timers->u.arr.len; i++) {
        js_object *t = timers->u.arr.v[i].u.o;
        if (t->u.arr.len >= 4 && t->u.arr.v[2].u.n < first)
            first = t->u.arr.v[2].u.n;
    }
    return first <= now ? 0 : (int64_t)(first - now);
}

bool js_timers_run(js_state *J, uint32_t budget_ms)
{
    bool ran = false;
    for (int guard = 0; guard < 200 && !J->fatal; guard++) {
        js_object *timers = registry_array(J, "timers");
        if (timers->cls != JS_C_ARRAY)
            break;
        double now = (double)ticks(J), first = 1e300;
        uint32_t pick = 0;
        bool found = false;
        for (uint32_t i = 0; i < timers->u.arr.len; i++) {
            js_object *t = timers->u.arr.v[i].u.o;
            if (t->u.arr.len >= 4 && t->u.arr.v[2].u.n <= now && t->u.arr.v[2].u.n < first) {
                first = t->u.arr.v[2].u.n;
                pick = i;
                found = true;
            }
        }
        if (!found)
            break;
        js_object *t = timers->u.arr.v[pick].u.o;
        int base = J->top;
        js_push(J, js_obj(t));
        if (t->u.arr.v[3].u.n > 0) {
            t->u.arr.v[2] = js_num(now + t->u.arr.v[3].u.n);    /* Dauerläufer: neu anstellen */
        } else {
            memmove(timers->u.arr.v + pick, timers->u.arr.v + pick + 1,
                    (size_t)(timers->u.arr.len - pick - 1) * sizeof(js_value));
            timers->u.arr.len--;
        }
        js_enter(J, budget_ms);
        bool ok;
        if (t->u.arr.v[1].t == JS_T_STR) {
            js_string *code = t->u.arr.v[1].u.s;
            js_fninfo *program = js_parse(J, code->data, code->len, "timer");
            ok = program && js_exec_program(J, program);
        } else {
            js_push(J, t->u.arr.v[1]);
            js_push(J, js_undef());
            uint32_t argc = t->u.arr.len - 4;
            for (uint32_t i = 0; i < argc && i < 32; i++)
                js_push(J, t->u.arr.v[4 + i]);
            if (argc == 0)
                js_push(J, js_num(now));
            ok = js_call(J, argc == 0 ? 1 : argc > 32 ? 32 : (int)argc);
        }
        J->top = base;
        if (ok)
            js_jobs_run(J);
        js_leave(J, ok);
        J->top = base;
        ran = true;
    }
    return ran;
}

/* --- Der in JavaScript geschriebene Teil ------------------------------------------------------------ */

#include "jslib.inc"                    /* jslib.js als Zeichenfeld jslib_js (vom Makefile erzeugt) */

/* --- Einrichten ------------------------------------------------------------------------------------ */

void js_lib2_init(js_state *J)
{
    js_object *G = J->global;

    js_object *json = js_object_new(J, JS_C_OBJECT, J->object_proto);
    js_def_value(J, G, "JSON", js_obj(json), JS_P_WRITE | JS_P_CONF);
    js_def_native(J, json, "parse", json_parse, 2, 0);
    js_def_native(J, json, "stringify", json_stringify, 3, 0);

    J->date_proto = js_object_new(J, JS_C_OBJECT, J->object_proto);
    js_object *date = js_native_new(J, date_ctor, "Date", 7, 0);
    js_define(J, date, J->k[K_prototype], js_obj(J->date_proto), 0);
    js_define(J, J->date_proto, J->k[K_constructor], js_obj(date), JS_P_WRITE | JS_P_CONF);
    js_def_value(J, G, "Date", js_obj(date), JS_P_WRITE | JS_P_CONF);
    date->u.n.nargs = 0;                /* fehlende Argumente nicht auffüllen: Die Anzahl zählt */
    js_def_native(J, date, "now", date_static, 0, 0);
    js_def_native(J, date, "parse", date_static, 1, 1);
    js_def_native(J, date, "UTC", date_static, 0, 2);
    static const struct {
        const char *get, *set;
        int magic;
    } parts[] = {
        { "getTime", "setTime", D_TIME }, { "getFullYear", "setFullYear", D_YEAR },
        { "getMonth", "setMonth", D_MONTH }, { "getDate", "setDate", D_DATE },
        { "getDay", NULL, D_DAY }, { "getHours", "setHours", D_HOURS },
        { "getMinutes", "setMinutes", D_MINUTES }, { "getSeconds", "setSeconds", D_SECONDS },
        { "getMilliseconds", "setMilliseconds", D_MS }, { "getTimezoneOffset", NULL, D_OFFSET },
        { "getYear", NULL, D_YEAR2 }, { "valueOf", NULL, D_TIME },
        { "getUTCFullYear", "setUTCFullYear", D_YEAR }, { "getUTCMonth", "setUTCMonth", D_MONTH },
        { "getUTCDate", "setUTCDate", D_DATE }, { "getUTCDay", NULL, D_DAY },
        { "getUTCHours", "setUTCHours", D_HOURS }, { "getUTCMinutes", "setUTCMinutes", D_MINUTES },
        { "getUTCSeconds", "setUTCSeconds", D_SECONDS },
        { "getUTCMilliseconds", "setUTCMilliseconds", D_MS },
    };
    for (unsigned i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
        js_def_native(J, J->date_proto, parts[i].get, date_get, 0, parts[i].magic);
        if (parts[i].set)
            js_def_native(J, J->date_proto, parts[i].set, date_set, 0, parts[i].magic);
    }
    static const struct {
        const char *name;
        int magic;
    } texts[] = {
        { "toISOString", 0 }, { "toString", 1 }, { "toDateString", 2 }, { "toTimeString", 3 },
        { "toLocaleString", 4 }, { "toLocaleDateString", 5 }, { "toLocaleTimeString", 6 },
        { "toJSON", 7 }, { "toUTCString", 1 }, { "toGMTString", 1 },
    };
    for (unsigned i = 0; i < sizeof(texts) / sizeof(texts[0]); i++)
        js_def_native(J, J->date_proto, texts[i].name, date_text, 0, texts[i].magic);

    js_def_native(J, G, "setTimeout", timer_set, 2, 0);
    js_def_native(J, G, "setInterval", timer_set, 2, 1);
    js_def_native(J, G, "requestAnimationFrame", timer_set, 2, 2);
    js_def_native(J, G, "setImmediate", timer_set, 2, 3);
    js_def_native(J, G, "clearTimeout", timer_clear, 1, 0);
    js_def_native(J, G, "clearInterval", timer_clear, 1, 0);
    js_def_native(J, G, "cancelAnimationFrame", timer_clear, 1, 0);
    js_def_native(J, G, "clearImmediate", timer_clear, 1, 0);
    js_def_native(J, G, "queueMicrotask", queue_microtask, 1, 0);

    js_coll_init(J);
    js_co_init(J);
    if (!J->fatal && !js_run(J, jslib_js, sizeof(jslib_js) - 1, "jslib", 0))
        js_fatal(J, "Die eingebauten Skripte ließen sich nicht einrichten");
}
