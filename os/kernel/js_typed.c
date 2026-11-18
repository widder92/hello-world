/*
 * JavaScript: typisierte Felder - ArrayBuffer, Uint8Array und Verwandte, DataView.
 *
 * Ein ArrayBuffer ist ein Stück Speicher fester Größe; die Felder sind
 * Sichten darauf, die Zahlen einer festen Art lesen und schreiben. Jede Sicht
 * hält ihren Puffer über die Eigenschaft "buffer" am Leben; jeder Zugriff
 * prüft die Grenzen.
 */
#include "js_impl.h"
#include "string.h"

#define ARGI(i) (J->bot + 2 + (i))
#define THISI (J->bot + 1)
#define MAGIC (js_callee(J)->u.n.magic)

#define BUFFER_MAX (32u * 1024 * 1024)

enum { T_I8, T_U8, T_U8C, T_I16, T_U16, T_I32, T_U32, T_F32, T_F64, T_VIEW, T_KINDS };

static const uint8_t sizes[T_KINDS] = { 1, 1, 1, 2, 2, 4, 4, 4, 8, 1 };
static const char *const names[T_KINDS] = {
    "Int8Array", "Uint8Array", "Uint8ClampedArray", "Int16Array", "Uint16Array", "Int32Array",
    "Uint32Array", "Float32Array", "Float64Array", "DataView",
};

/* Eine Sicht auf einen Puffer; length zählt Glieder (bei DataView: Bytes) */
typedef struct {
    js_object *buffer;
    uint32_t offset, length;
    uint8_t kind;
} view_t;

static bool view_get(js_state *J, js_object *o, js_string *key, bool *found);
static bool view_put(js_state *J, js_object *o, js_string *key, const js_value *v, bool *done);
static bool buffer_get(js_state *J, js_object *o, js_string *key, bool *found);
static void view_free(js_state *J, js_object *o);
static void buffer_free(js_state *J, js_object *o);

static const js_host_class buffer_class = { "ArrayBuffer", buffer_get, NULL, buffer_free };
static const js_host_class view_classes[T_KINDS] = {
    { "Int8Array", view_get, view_put, view_free }, { "Uint8Array", view_get, view_put, view_free },
    { "Uint8ClampedArray", view_get, view_put, view_free },
    { "Int16Array", view_get, view_put, view_free }, { "Uint16Array", view_get, view_put, view_free },
    { "Int32Array", view_get, view_put, view_free }, { "Uint32Array", view_get, view_put, view_free },
    { "Float32Array", view_get, view_put, view_free },
    { "Float64Array", view_get, view_put, view_free }, { "DataView", view_get, view_put, view_free },
};

static js_object *buffer_of(const js_value *v)
{
    return v->t == JS_T_OBJ && v->u.o->cls == JS_C_HOST && v->u.o->u.host.cls == &buffer_class
               ? v->u.o : NULL;
}

static view_t *view_of(const js_value *v)
{
    if (v->t != JS_T_OBJ || v->u.o->cls != JS_C_HOST)
        return NULL;
    const js_host_class *c = v->u.o->u.host.cls;
    return c >= &view_classes[0] && c < &view_classes[T_KINDS] ? v->u.o->u.host.ptr : NULL;
}

static uint8_t *view_bytes(const view_t *v)
{
    return (uint8_t *)v->buffer->u.host.ptr + v->offset;
}

static void buffer_free(js_state *J, js_object *o)
{
    js_release(J, o->u.host.ptr, o->u.host.id ? o->u.host.id : 1);
    o->u.host.ptr = NULL;
}

static void view_free(js_state *J, js_object *o)
{
    js_release(J, o->u.host.ptr, sizeof(view_t));
    o->u.host.ptr = NULL;
}

static js_object *proto_for(js_state *J, const char *name)
{
    char key[40] = "proto:";
    strlcpy(key + 6, name, sizeof(key) - 6);
    js_prop *p = js_lookup(J->registry, js_intern_cstr(J, key), NULL);
    return p && p->v.t == JS_T_OBJ ? p->v.u.o : J->object_proto;
}

/* Legt einen Puffer an und auf den Stapel; NULL (mit Ausnahme), wenn er zu groß ist */
static js_object *buffer_new(js_state *J, double bytes)
{
    if (!(bytes >= 0 && bytes <= BUFFER_MAX)) {
        js_throw(J, JS_E_RANGE, "Array buffer allocation failed");
        return NULL;
    }
    uint32_t n = (uint32_t)bytes;
    js_object *o = js_object_new(J, JS_C_HOST, proto_for(J, "ArrayBuffer"));
    if (!js_push(J, js_obj(o)) || o == J->dummy)
        return NULL;
    uint8_t *data = js_alloc(J, n ? n : 1);
    if (!data)
        return NULL;
    memset(data, 0, n ? n : 1);
    o->u.host.cls = &buffer_class;
    o->u.host.ptr = data;
    o->u.host.id = n;
    return o;
}

/* Legt eine Sicht an und auf den Stapel */
static view_t *view_new(js_state *J, int kind, js_object *buffer, uint32_t offset, uint32_t length)
{
    js_object *o = js_object_new(J, JS_C_HOST, proto_for(J, names[kind]));
    if (!js_push(J, js_obj(o)) || o == J->dummy)
        return NULL;
    view_t *v = js_alloc(J, sizeof(view_t));
    if (!v)
        return NULL;
    v->buffer = buffer;
    v->offset = offset;
    v->length = length;
    v->kind = (uint8_t)kind;
    o->u.host.cls = &view_classes[kind];
    o->u.host.ptr = v;
    /* hält den Puffer am Leben; nicht zu ändern und nicht zu löschen */
    js_define(J, o, js_intern_cstr(J, "buffer"), js_obj(buffer), 0);
    return v;
}

/* --- Zahlen lesen und schreiben -------------------------------------------------------------------- */

static double load(const uint8_t *p, int kind, bool swap)
{
    uint8_t b[8];
    int size = sizes[kind];
    for (int i = 0; i < size; i++)
        b[i] = p[swap ? size - 1 - i : i];
    switch (kind) {
    case T_I8: return (int8_t)b[0];
    case T_U8: case T_U8C: return b[0];
    case T_I16: return (int16_t)(b[0] | b[1] << 8);
    case T_U16: return (uint16_t)(b[0] | b[1] << 8);
    case T_I32: return (int32_t)((uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24);
    case T_U32: return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    case T_F32: {
        float f;
        memcpy(&f, b, 4);
        return f;
    }
    default: {
        double d;
        memcpy(&d, b, 8);
        return d;
    }
    }
}

static void store(uint8_t *p, int kind, double x, bool swap)
{
    uint8_t b[8];
    int size = sizes[kind];
    if (kind == T_F32) {
        float f = (float)x;
        memcpy(b, &f, 4);
    } else if (kind == T_F64) {
        memcpy(b, &x, 8);
    } else {
        uint32_t n;
        if (kind == T_U8C) {            /* begrenzen statt umlaufen; halbe Werte zur geraden Zahl */
            double r = js_floor(x + 0.5);
            if (r - x == 0.5 && js_fmod(r, 2) != 0)
                r -= 1;
            n = js_isnan(x) || x <= 0 ? 0 : x >= 255 ? 255 : (uint32_t)r;
        } else {
            n = (uint32_t)js_double_to_int32(x);
        }
        b[0] = (uint8_t)n;
        b[1] = (uint8_t)(n >> 8);
        b[2] = (uint8_t)(n >> 16);
        b[3] = (uint8_t)(n >> 24);
    }
    for (int i = 0; i < size; i++)
        p[i] = b[swap ? size - 1 - i : i];
}

static bool key_is(const js_string *key, const char *name)
{
    return strcmp(key->data, name) == 0;
}

static bool view_get(js_state *J, js_object *o, js_string *key, bool *found)
{
    const view_t *v = o->u.host.ptr;
    if (!v)
        return true;
    if (key->index != JS_NO_INDEX && v->kind != T_VIEW) {
        *found = true;
        if (key->index >= v->length)
            return js_push(J, js_undef());
        return js_push(J, js_num(load(view_bytes(v) + (size_t)key->index * sizes[v->kind], v->kind, false)));
    }
    double value;
    if (key == J->k[K_length] && v->kind != T_VIEW)
        value = v->length;
    else if (key_is(key, "byteLength"))
        value = (double)v->length * sizes[v->kind];
    else if (key_is(key, "byteOffset"))
        value = v->offset;
    else
        return true;                    /* Methoden und "buffer": über die üblichen Wege */
    *found = true;
    return js_push(J, js_num(value));
}

static bool view_put(js_state *J, js_object *o, js_string *key, const js_value *value, bool *done)
{
    view_t *v = o->u.host.ptr;
    if (!v)
        return true;
    if (key->index != JS_NO_INDEX && v->kind != T_VIEW) {
        double x;
        *done = true;
        js_push(J, *value);
        bool ok = js_to_number(J, J->top - 1, &x);
        js_pop(J, 1);
        v = o->u.host.ptr;              /* die Umwandlung kann Skripte ausgeführt haben */
        if (ok && v && key->index < v->length)
            store(view_bytes(v) + (size_t)key->index * sizes[v->kind], v->kind, x, false);
        return ok;
    }
    if (key == J->k[K_length] || key_is(key, "byteLength") || key_is(key, "byteOffset") ||
        key_is(key, "buffer"))
        *done = true;                   /* nur zum Lesen */
    return true;
}

static bool buffer_get(js_state *J, js_object *o, js_string *key, bool *found)
{
    if (!key_is(key, "byteLength"))
        return true;
    *found = true;
    return js_push(J, js_num(o->u.host.id));
}

/* --- Anlegen ---------------------------------------------------------------------------------------- */

/* Liest ein ganzzahliges Argument; fehlt es, gilt fallback */
static bool arg_whole(js_state *J, int i, int argc, double fallback, double *out)
{
    *out = fallback;
    if (i >= argc || js_arg(J, i)->t == JS_T_UNDEF)
        return true;
    if (!js_to_number(J, ARGI(i), out))
        return false;
    *out = js_isnan(*out) ? 0 : js_trunc(*out);
    return true;
}

static bool buffer_ctor(js_state *J, int argc)
{
    double n;
    if (!J->constructing)
        return js_throw(J, JS_E_TYPE, "Constructor ArrayBuffer requires 'new'");
    if (!arg_whole(J, 0, argc, 0, &n))
        return false;
    return buffer_new(J, n) != NULL;
}

/* Füllt eine neue Sicht aus den Gliedern eines Felds (liegt auf dem Stapel bei items) */
static bool view_from_items(js_state *J, int kind, int items)
{
    uint32_t count = J->stack[items].u.o->u.arr.len;
    js_object *buffer = buffer_new(J, (double)count * sizes[kind]);
    if (!buffer)
        return false;
    view_t *v = view_new(J, kind, buffer, 0, count);
    if (!v)
        return false;
    for (uint32_t i = 0; i < count; i++) {
        double x;
        js_object *a = J->stack[items].u.o;
        if (i >= a->u.arr.len)
            break;
        js_push(J, a->u.arr.v[i]);
        bool ok = js_to_number(J, J->top - 1, &x);
        js_pop(J, 1);
        if (!ok)
            return false;
        store(view_bytes(v) + (size_t)i * sizes[kind], kind, x, false);
    }
    return true;                        /* oben liegt die Sicht */
}

static bool view_ctor(js_state *J, int argc)
{
    int kind = MAGIC;
    int size = sizes[kind];
    if (!J->constructing)
        return js_throw(J, JS_E_TYPE, "Constructor %s requires 'new'", names[kind]);
    js_value *first = js_arg(J, 0);
    js_object *buffer = argc > 0 ? buffer_of(first) : NULL;
    if (buffer) {
        double offset, length;
        uint32_t total = buffer->u.host.id;
        if (!arg_whole(J, 1, argc, 0, &offset))
            return false;
        if (offset < 0 || offset > total || js_fmod(offset, size) != 0)
            return js_throw(J, JS_E_RANGE, "Start offset of %s is out of bounds", names[kind]);
        if (!arg_whole(J, 2, argc, (total - offset) / size, &length))
            return false;
        if (argc < 3 || js_arg(J, 2)->t == JS_T_UNDEF) {
            if (js_fmod(total - offset, size) != 0)
                return js_throw(J, JS_E_RANGE, "Byte length of %s should be a multiple of %d",
                                names[kind], size);
        }
        buffer = buffer_of(js_arg(J, 0));       /* die Umwandlungen können Skripte ausgeführt haben */
        if (!buffer || length < 0 || offset + length * size > buffer->u.host.id)
            return js_throw(J, JS_E_RANGE, "Invalid typed array length");
        return view_new(J, kind, buffer, (uint32_t)offset, (uint32_t)length) != NULL;
    }
    if (kind == T_VIEW)
        return js_throw(J, JS_E_TYPE, "First argument to DataView constructor must be an ArrayBuffer");
    if (argc > 0 && first->t == JS_T_OBJ) {     /* ein Feld oder etwas Feldähnliches: kopieren */
        js_object *items = js_array_new(J, 0);
        int at = J->top;
        js_push(J, js_obj(items));
        if (!js_iterate(J, ARGI(0), items))
            return false;
        return view_from_items(J, kind, at);
    }
    double length;
    if (!arg_whole(J, 0, argc, 0, &length))
        return false;
    if (length < 0 || length * size > BUFFER_MAX)
        return js_throw(J, JS_E_RANGE, "Invalid typed array length");
    buffer = buffer_new(J, length * size);
    if (!buffer)
        return false;
    return view_new(J, kind, buffer, 0, (uint32_t)length) != NULL;
}

/* Uint8Array.from(feld[, fn]) und .of(...) */
static bool view_static(js_state *J, int argc)
{
    int kind = MAGIC & 15;
    js_object *items = js_array_new(J, 0);
    int at = J->top;
    js_push(J, js_obj(items));
    if (MAGIC & 16) {                   /* of */
        for (int i = 0; i < argc; i++) {
            if (!js_array_push(J, items, *js_arg(J, i)))
                return false;
        }
    } else {
        if (argc > 0 && js_arg(J, 0)->t > JS_T_NULL && !js_iterate(J, ARGI(0), items))
            return false;
        if (argc > 1 && js_is_callable(js_arg(J, 1))) {
            for (uint32_t i = 0; i < items->u.arr.len; i++) {
                js_push(J, *js_arg(J, 1));
                js_push(J, js_undef());
                js_push(J, items->u.arr.v[i]);
                js_push(J, js_num(i));
                if (!js_call(J, 2))
                    return false;
                if (i < items->u.arr.len)
                    items->u.arr.v[i] = *js_top(J, 0);
                js_pop(J, 1);
            }
        }
    }
    return view_from_items(J, kind, at);
}

/* --- Methoden --------------------------------------------------------------------------------------- */

static view_t *this_view(js_state *J, bool data_view)
{
    view_t *v = view_of(js_this(J));
    if (!v || (v->kind == T_VIEW) != data_view) {
        js_throw(J, JS_E_TYPE, data_view ? "this is not a DataView" : "this is not a typed array");
        return NULL;
    }
    return v;
}

static uint32_t clamp_index(double i, uint32_t len)
{
    if (i < 0)
        i += len;
    if (i < 0)
        return 0;
    return i > len ? len : (uint32_t)i;
}

enum { M_SET, M_SUBARRAY, M_SLICE, M_FILL };

static bool view_method(js_state *J, int argc)
{
    view_t *v = this_view(J, false);
    if (!v)
        return false;
    int size = sizes[v->kind];
    switch (MAGIC) {
    case M_SET: {
        double offset;
        js_object *items = js_array_new(J, 0);
        js_push(J, js_obj(items));
        if (argc < 1 || js_arg(J, 0)->t <= JS_T_NULL)
            return js_throw(J, JS_E_TYPE, "invalid_argument");
        if (!js_iterate(J, ARGI(0), items) || !arg_whole(J, 1, argc, 0, &offset))
            return false;
        v = view_of(js_this(J));
        if (!v || offset < 0 || offset + items->u.arr.len > v->length)
            return js_throw(J, JS_E_RANGE, "offset is out of bounds");
        for (uint32_t i = 0; i < items->u.arr.len; i++) {
            double x;
            js_push(J, items->u.arr.v[i]);
            bool ok = js_to_number(J, J->top - 1, &x);
            js_pop(J, 1);
            if (!ok)
                return false;
            v = view_of(js_this(J));
            if (!v || (uint32_t)offset + i >= v->length)
                break;
            store(view_bytes(v) + ((size_t)offset + i) * size, v->kind, x, false);
        }
        return js_push(J, js_undef());
    }
    case M_SUBARRAY: case M_SLICE: {
        double from, to;
        if (!arg_whole(J, 0, argc, 0, &from) || !arg_whole(J, 1, argc, v->length, &to))
            return false;
        v = view_of(js_this(J));
        if (!v)
            return false;
        uint32_t start = clamp_index(from, v->length), end = clamp_index(to, v->length);
        uint32_t count = end > start ? end - start : 0;
        if (MAGIC == M_SUBARRAY)
            return view_new(J, v->kind, v->buffer, v->offset + start * (uint32_t)size, count) != NULL;
        js_object *buffer = buffer_new(J, (double)count * size);
        if (!buffer)
            return false;
        memcpy(buffer->u.host.ptr, view_bytes(v) + (size_t)start * size, (size_t)count * size);
        return view_new(J, v->kind, buffer, 0, count) != NULL;
    }
    default: {                          /* fill */
        double x, from, to;
        if (!js_to_number(J, ARGI(0), &x) || !arg_whole(J, 1, argc, 0, &from) ||
            !arg_whole(J, 2, argc, v->length, &to))
            return false;
        v = view_of(js_this(J));
        if (!v)
            return false;
        uint32_t end = clamp_index(to, v->length);
        for (uint32_t i = clamp_index(from, v->length); i < end; i++)
            store(view_bytes(v) + (size_t)i * size, v->kind, x, false);
        return js_push(J, *js_this(J));
    }
    }
}

static bool buffer_method(js_state *J, int argc)
{
    if (MAGIC == 1)                     /* ArrayBuffer.isView */
        return js_push(J, js_bool(view_of(js_arg(J, 0)) != NULL));
    js_object *b = buffer_of(js_this(J));
    double from, to;
    if (!b)
        return js_throw(J, JS_E_TYPE, "this is not an ArrayBuffer");
    if (!arg_whole(J, 0, argc, 0, &from) || !arg_whole(J, 1, argc, b->u.host.id, &to))
        return false;
    b = buffer_of(js_this(J));
    uint32_t start = clamp_index(from, b->u.host.id), end = clamp_index(to, b->u.host.id);
    uint32_t count = end > start ? end - start : 0;
    js_object *copy = buffer_new(J, count);
    if (!copy)
        return false;
    memcpy(copy->u.host.ptr, (uint8_t *)b->u.host.ptr + start, count);
    return true;
}

/* DataView: getInt8 ... setFloat64; magic = Art, +16 zum Schreiben */
static bool data_view_method(js_state *J, int argc)
{
    view_t *v = this_view(J, true);
    int kind = MAGIC & 15, size = sizes[kind];
    bool write = MAGIC & 16;
    double at, x = 0;
    if (!v || !arg_whole(J, 0, argc, 0, &at))
        return false;
    if (write && !js_to_number(J, ARGI(1), &x))
        return false;
    bool little = js_truthy(js_arg(J, write ? 2 : 1));
    v = view_of(js_this(J));
    if (!v || at < 0 || at + size > v->length)
        return js_throw(J, JS_E_RANGE, "Offset is outside the bounds of the DataView");
    uint8_t *p = view_bytes(v) + (uint32_t)at;
    if (write) {
        store(p, kind, x, !little);
        return js_push(J, js_undef());
    }
    return js_push(J, js_num(load(p, kind, !little)));
}

void js_typed_init(js_state *J)
{
    js_object *G = J->global;

    js_object *buffer_proto = js_object_new(J, JS_C_OBJECT, J->object_proto);
    js_object *buffer = js_native_new(J, buffer_ctor, "ArrayBuffer", 1, 0);
    js_define(J, buffer, J->k[K_prototype], js_obj(buffer_proto), 0);
    js_define(J, buffer_proto, J->k[K_constructor], js_obj(buffer), JS_P_WRITE | JS_P_CONF);
    js_def_value(J, G, "ArrayBuffer", js_obj(buffer), JS_P_WRITE | JS_P_CONF);
    js_def_value(J, J->registry, "proto:ArrayBuffer", js_obj(buffer_proto), 0);
    js_def_native(J, buffer_proto, "slice", buffer_method, 2, 0);
    js_def_native(J, buffer, "isView", buffer_method, 1, 1);

    /* gemeinsame Methoden; alles Übrige (join, indexOf, forEach ...) kommt von den Feldern */
    js_object *shared = js_object_new(J, JS_C_OBJECT, J->array_proto);
    js_def_value(J, J->registry, "proto:TypedArray", js_obj(shared), 0);
    js_def_native(J, shared, "set", view_method, 2, M_SET);
    js_def_native(J, shared, "subarray", view_method, 2, M_SUBARRAY);
    js_def_native(J, shared, "slice", view_method, 2, M_SLICE);
    js_def_native(J, shared, "fill", view_method, 3, M_FILL);

    for (int kind = 0; kind < T_VIEW; kind++) {
        char key[40] = "proto:";
        strlcpy(key + 6, names[kind], sizeof(key) - 6);
        js_object *proto = js_object_new(J, JS_C_OBJECT, shared);
        js_object *ctor = js_native_new(J, view_ctor, names[kind], 3, kind);
        ctor->u.n.nargs = 3;
        js_define(J, ctor, J->k[K_prototype], js_obj(proto), 0);
        js_define(J, proto, J->k[K_constructor], js_obj(ctor), JS_P_WRITE | JS_P_CONF);
        js_def_value(J, proto, "BYTES_PER_ELEMENT", js_num(sizes[kind]), 0);
        js_def_value(J, ctor, "BYTES_PER_ELEMENT", js_num(sizes[kind]), 0);
        js_def_native(J, ctor, "from", view_static, 0, kind);
        js_def_native(J, ctor, "of", view_static, 0, kind | 16);
        js_def_value(J, G, names[kind], js_obj(ctor), JS_P_WRITE | JS_P_CONF);
        js_def_value(J, J->registry, key, js_obj(proto), 0);
    }

    static const struct {
        const char *name;
        int kind;
    } parts[] = {
        { "Int8", T_I8 }, { "Uint8", T_U8 }, { "Int16", T_I16 }, { "Uint16", T_U16 },
        { "Int32", T_I32 }, { "Uint32", T_U32 }, { "Float32", T_F32 }, { "Float64", T_F64 },
    };
    js_object *view_proto = js_object_new(J, JS_C_OBJECT, J->object_proto);
    js_object *view = js_native_new(J, view_ctor, "DataView", 3, T_VIEW);
    js_define(J, view, J->k[K_prototype], js_obj(view_proto), 0);
    js_define(J, view_proto, J->k[K_constructor], js_obj(view), JS_P_WRITE | JS_P_CONF);
    js_def_value(J, G, "DataView", js_obj(view), JS_P_WRITE | JS_P_CONF);
    js_def_value(J, J->registry, "proto:DataView", js_obj(view_proto), 0);
    for (unsigned i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
        char name[16] = "get";
        strlcpy(name + 3, parts[i].name, sizeof(name) - 3);
        js_def_native(J, view_proto, name, data_view_method, 2, parts[i].kind);
        name[0] = 's';
        js_def_native(J, view_proto, name, data_view_method, 3, parts[i].kind | 16);
    }
}
