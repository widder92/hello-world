/*
 * JavaScript: Koroutinen - das, was hinter Generatoren (function*, yield) und
 * hinter async/await steckt. Siehe js.h
 *
 * Eine Funktion, die mitten in der Ausführung anhalten und später an derselben
 * Stelle weitermachen soll, braucht einen eigenen Stack: Die Maschine führt
 * den Programmbaum mit gewöhnlichen C-Aufrufen aus, der Stand der Ausführung
 * steckt also im C-Stack (und im Wertestapel).
 *
 * Statt jeder Koroutine einen eigenen, großen Stack zu geben, teilen sich alle
 * EINEN: Jede läuft stets ab derselben Adresse. Hält sie an, wird der benutzte
 * Teil des Stacks (und ihres Wertestapels) in einen Puffer kopiert und beim
 * Fortsetzen zurückkopiert - an dieselbe Stelle, sodass alle Zeiger wieder
 * stimmen. Eine ruhende Koroutine kostet damit nur die wenigen Kilobyte, die
 * sie gerade belegt, und darf trotzdem so tief rechnen wie das Hauptprogramm.
 *
 * Kopiert wird von einem kleinen Umschalter aus, der auf einem dritten Stack
 * läuft: So kann auch eine Koroutine eine andere fortsetzen (dabei wird die
 * rufende ausgelagert).
 *
 * async-Funktionen sind dieselben Koroutinen; ein Stück JavaScript im
 * Vorspann (js_lib2.c) setzt sie fort, sobald das erwartete Promise fertig ist.
 */
#include "js_impl.h"
#include "memory.h"
#include "string.h"

#if !defined(__x86_64__)
#error "js_co.c: Der Wechsel zwischen Koroutinen ist nur für x86-64 geschrieben"
#endif

#define ARGI(i) (J->bot + 2 + (i))
#define MAGIC (js_callee(J)->u.n.magic)

/* Prüfwerkzeug der Tests: Es merkt sich Sperrzonen im Stack, die beim Umkopieren nicht mehr gelten */
#if defined(__SANITIZE_ADDRESS__)
#define CO_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define CO_ASAN 1
#endif
#endif
#ifdef CO_ASAN
void __asan_unpoison_memory_region(void const volatile *addr, size_t size);
#define UNPOISON(addr, size) __asan_unpoison_memory_region((addr), (size))
#else
#define UNPOISON(addr, size) ((void)0)
#endif

#define STACK_GUARD   256               /* Luft vor und hinter dem Wertestapel, wie in js_core.c */
#define CO_MARGIN     (96 * 1024)       /* unter der Grenze: Platz für eingebaute Funktionen und Interrupts */
#define SWITCH_BYTES  (48 * 1024)       /* Stack des Umschalters */
#define NESTING_MAX   1500              /* Koroutinen, die einander fortsetzen */

enum { CO_NEW, CO_SUSPENDED, CO_RUNNING, CO_DONE };
enum { KIND_GENERATOR, KIND_ASYNC, KIND_ASYNC_GENERATOR };
enum { IN_NEXT, IN_THROW, IN_RETURN };                  /* womit eine Koroutine fortgesetzt wird */
enum { OUT_AWAIT, OUT_YIELD, OUT_RETURN, OUT_THROW };   /* womit sie sich meldet */

/* Was die Maschine über den laufenden Ablauf weiß - je Ablauf einmal */
typedef struct {
    js_value *stack;
    int top, bot, fbase, depth, line;
    bool constructing;
    uintptr_t floor;
    void *sp;                           /* Stackzeiger, solange der Ablauf ruht */
} context;

typedef struct js_co {
    uint8_t state, kind, in_mode, out_kind;
    js_object *self;                    /* das Objekt, an dem die Koroutine hängt */
    js_object *fn, *env;
    js_value this_value;
    js_value value;                     /* was beim Wechsel hinein- oder herausgereicht wird */
    context ctx;
    char *c_image;                      /* Abbild des C-Stacks, solange sie ruht */
    size_t c_size, c_cap;
    js_value *v_image;                  /* ... und ihres Wertestapels */
    int v_count, v_cap;
    struct js_co *resumer;              /* wer sie gerade fortgesetzt hat (NULL: das Hauptprogramm) */
} js_co;

struct js_costate {
    char *stack, *stack_top;            /* der gemeinsame C-Stack aller Koroutinen */
    js_value *values_base, *values;     /* ... und ihr gemeinsamer Wertestapel */
    char *switch_stack;
    void *switch_sp;
    uintptr_t floor;
    context main;                       /* das Hauptprogramm, solange eine Koroutine läuft */
    js_co *current;                     /* die laufende Koroutine oder NULL */
    js_co *from, *to;                   /* Auftrag an den Umschalter */
    bool failed;                        /* ... den er nicht ausführen konnte (kein Speicher) */
    int nesting;
};

/* --- Der Wechsel des Stacks -------------------------------------------------------------------------- */

/*
 * co_jump(save, to): sichert die Register, die eine Funktion erhalten muss,
 * auf dem laufenden Stack, merkt sich dessen Zeiger in *save und macht auf
 * dem Stack "to" weiter - dort, wo zuletzt selbst co_jump() gerufen wurde.
 *
 * co_boot: Hier beginnt ein frischer Stack (siehe boot_frame): ruft die
 * Funktion in r12 mit dem Argument in rbx.
 */
#if defined(_WIN32)
/* Aufrufkonvention von Microsoft/UEFI: Argumente in rcx, rdx; auch xmm6-15 bleiben erhalten */
__attribute__((naked, noinline)) static void co_jump(void **save __attribute__((unused)),
                                                     void *to __attribute__((unused)))
{
    __asm__("pushq %rbp\n pushq %rbx\n pushq %rdi\n pushq %rsi\n"
            "pushq %r12\n pushq %r13\n pushq %r14\n pushq %r15\n"
            "subq $168, %rsp\n"
            "movups %xmm6, 0(%rsp)\n movups %xmm7, 16(%rsp)\n movups %xmm8, 32(%rsp)\n"
            "movups %xmm9, 48(%rsp)\n movups %xmm10, 64(%rsp)\n movups %xmm11, 80(%rsp)\n"
            "movups %xmm12, 96(%rsp)\n movups %xmm13, 112(%rsp)\n movups %xmm14, 128(%rsp)\n"
            "movups %xmm15, 144(%rsp)\n"
            "movq %rsp, (%rcx)\n"
            "movq %rdx, %rsp\n"
            "movups 0(%rsp), %xmm6\n movups 16(%rsp), %xmm7\n movups 32(%rsp), %xmm8\n"
            "movups 48(%rsp), %xmm9\n movups 64(%rsp), %xmm10\n movups 80(%rsp), %xmm11\n"
            "movups 96(%rsp), %xmm12\n movups 112(%rsp), %xmm13\n movups 128(%rsp), %xmm14\n"
            "movups 144(%rsp), %xmm15\n"
            "addq $168, %rsp\n"
            "popq %r15\n popq %r14\n popq %r13\n popq %r12\n"
            "popq %rsi\n popq %rdi\n popq %rbx\n popq %rbp\n"
            "ret\n");
}

__attribute__((naked, noinline)) static void co_boot(void)
{
    __asm__("subq $40, %rsp\n movq %rbx, %rcx\n callq *%r12\n ud2\n");
}

#define SAVED_WORDS 8
#define SAVED_EXTRA 168
#define SLOT_ENTRY  3                   /* r12 */
#define SLOT_ARG    6                   /* rbx */
#else
/* System V (Entwicklungsrechner): Argumente in rdi, rsi */
__attribute__((naked, noinline)) static void co_jump(void **save __attribute__((unused)),
                                                     void *to __attribute__((unused)))
{
    __asm__("pushq %rbp\n pushq %rbx\n pushq %r12\n pushq %r13\n pushq %r14\n pushq %r15\n"
            "movq %rsp, (%rdi)\n"
            "movq %rsi, %rsp\n"
            "popq %r15\n popq %r14\n popq %r13\n popq %r12\n popq %rbx\n popq %rbp\n"
            "ret\n");
}

__attribute__((naked, noinline)) static void co_boot(void)
{
    __asm__("subq $8, %rsp\n movq %rbx, %rdi\n callq *%r12\n ud2\n");
}

#define SAVED_WORDS 6
#define SAVED_EXTRA 0
#define SLOT_ENTRY  3                   /* r12 */
#define SLOT_ARG    4                   /* rbx */
#endif

/*
 * Richtet einen frischen Stack so her, als hätte dort jemand co_jump()
 * gerufen: Der erste Wechsel dorthin "kehrt" nach co_boot zurück, das
 * entry(J) aufruft. Liefert den Stackzeiger für co_jump().
 */
static void *boot_frame(js_state *J, char *top, void (*entry)(js_state *))
{
    uintptr_t *sp = (uintptr_t *)(void *)(top - 64);
    UNPOISON(top - 512, 512);
    memset(top - 64, 0, 64);
    sp[0] = (uintptr_t)co_boot;         /* dorthin führt das "ret" von co_jump */
    sp -= SAVED_WORDS;
    memset(sp, 0, SAVED_WORDS * sizeof(uintptr_t));
    sp[SLOT_ENTRY] = (uintptr_t)entry;
    sp[SLOT_ARG] = (uintptr_t)J;
    char *low = (char *)sp - SAVED_EXTRA;
    if (SAVED_EXTRA)
        memset(low, 0, SAVED_EXTRA);
    return low;
}

static void co_main(js_state *J);

/*
 * Der Umschalter: läuft auf seinem eigenen Stack und kopiert, während weder
 * der alte noch der neue Ablauf auf dem gemeinsamen Stack steht.
 */
static void switcher(js_state *J)
{
    for (;;) {
        js_costate *S = J->co;
        js_co *from = S->from, *to = S->to;
        S->failed = false;
        if (from) {
            size_t n = (size_t)(S->stack_top - (char *)from->ctx.sp);
            UNPOISON(from->ctx.sp, n);
            if (from->state != CO_DONE) {
                if (n > from->c_cap) {
                    /* genau so viel, wie sie gerade belegt */
                    char *image = js_alloc(J, n);
                    if (!image) {
                        /* kein Platz für das Abbild: zurück, als wäre nichts gewesen */
                        S->failed = true;
                        co_jump(&S->switch_sp, from->ctx.sp);
                        continue;
                    }
                    js_release(J, from->c_image, from->c_cap);
                    from->c_image = image;
                    from->c_cap = n;
                }
                memcpy(from->c_image, from->ctx.sp, n);
                from->c_size = n;
            }
        }
        void *target;
        if (!to) {
            target = S->main.sp;
        } else if (to->state == CO_NEW) {
            target = boot_frame(J, S->stack_top, co_main);
        } else {
            target = S->stack_top - to->c_size;
            memcpy(target, to->c_image, to->c_size);
        }
        co_jump(&S->switch_sp, target);
    }
}

static void costate_free(js_costate *S)
{
    if (!S)
        return;
    kfree(S->stack);
    kfree(S->values_base);
    kfree(S->switch_stack);
    kfree(S);
}

/* Legt die gemeinsamen Stacks an, sobald die erste Koroutine läuft. */
static js_costate *costate(js_state *J)
{
    if (J->co)
        return J->co;
    size_t room = J->host.stack_bytes ? J->host.stack_bytes : 256 * 1024;
    size_t size = (room + CO_MARGIN + 15) & ~(size_t)15;
    js_costate *S = kzalloc(sizeof(js_costate));
    if (S) {
        S->stack = kmalloc(size + 16);
        S->values_base = kzalloc((size_t)(JS_STACK_SIZE + 2 * STACK_GUARD) * sizeof(js_value));
        S->switch_stack = kmalloc(SWITCH_BYTES);
    }
    if (!S || !S->stack || !S->values_base || !S->switch_stack) {
        costate_free(S);
        js_fatal(J, "Nicht genug Speicher für das Skript");
        return NULL;
    }
    S->stack_top = (char *)(((uintptr_t)S->stack + size) & ~(uintptr_t)15);
    S->floor = (uintptr_t)S->stack + CO_MARGIN;
    S->values = S->values_base + STACK_GUARD;
    char *switch_top = (char *)(((uintptr_t)S->switch_stack + SWITCH_BYTES) & ~(uintptr_t)15);
    S->switch_sp = boot_frame(J, switch_top, switcher);
    J->co = S;
    return S;
}

/*
 * Wechselt vom laufenden Ablauf "from" zum Ablauf "to" (NULL: das
 * Hauptprogramm) und kehrt zurück, sobald jemand wieder zu "from" wechselt.
 * false: Es fehlte Speicher für das Abbild - dann ist nichts geschehen.
 */
static bool switch_to(js_state *J, js_co *from, js_co *to)
{
    js_costate *S = J->co;
    context *self = from ? &from->ctx : &S->main;
    if (from && from->state != CO_DONE) {
        if (J->top > from->v_cap) {
            js_value *image = js_alloc(J, (size_t)J->top * sizeof(js_value));
            if (!image)
                return false;
            js_release(J, from->v_image, (size_t)from->v_cap * sizeof(js_value));
            from->v_image = image;
            from->v_cap = J->top;
        }
        if (J->top > 0)
            memcpy(from->v_image, S->values, (size_t)J->top * sizeof(js_value));
        from->v_count = J->top;
    }
    self->stack = J->stack;
    self->top = J->top;
    self->bot = J->bot;
    self->fbase = J->fbase;
    self->depth = J->depth;
    self->line = J->line;
    self->constructing = J->constructing;
    self->floor = J->stack_floor;
    S->from = from;
    S->to = to;
    co_jump(&self->sp, S->switch_sp);

    /* ... wieder da: jemand hat zu "from" zurückgewechselt - oder der Wechsel kam nicht zustande */
    S = J->co;
    if (S->failed) {
        S->failed = false;
        if (from)
            from->v_count = 0;
        return false;
    }
    J->stack = self->stack;
    J->top = self->top;
    J->bot = self->bot;
    J->fbase = self->fbase;
    J->depth = self->depth;
    J->line = self->line;
    J->constructing = self->constructing;
    J->stack_floor = self->floor;
    if (from) {
        if (from->v_count > 0)
            memcpy(S->values, from->v_image, (size_t)from->v_count * sizeof(js_value));
        from->v_count = 0;              /* das Abbild ist veraltet, sobald sie wieder rechnet */
    }
    S->current = from;
    return true;
}

static void co_release(js_co *co)
{
    co->fn = NULL;
    co->env = NULL;
    co->this_value = js_undef();
}

/* Erzwungene Rückkehr (generator.return()): wie eine Ausnahme unterwegs, die niemand fängt */
static bool throw_return(js_state *J, js_value v)
{
    js_push(J, v);
    js_object *signal = js_object_new(J, JS_C_SIGNAL, NULL);
    if (signal == J->dummy)
        return false;
    signal->u.prim = v;
    js_pop(J, 1);
    return js_throw_value(J, js_obj(signal));
}

/* Hier beginnt jede Koroutine - auf dem gemeinsamen Stack, ganz oben. */
static void co_main(js_state *J)
{
    js_costate *S = J->co;
    js_co *co = S->to;
    S->current = co;
    co->state = CO_RUNNING;
    J->stack = S->values;
    J->top = 0;
    J->bot = 0;
    J->fbase = 0;
    J->depth = 0;
    J->constructing = false;
    J->stack_floor = S->floor;
    js_push(J, js_obj(co->fn));
    js_push(J, co->this_value);
    js_push(J, js_obj(co->env));

    js_value result = js_undef();
    if (js_exec_body(J, co->fn, co->env, &result)) {
        co->out_kind = OUT_RETURN;
        co->value = result;
    } else if (J->fatal) {
        co->out_kind = OUT_THROW;
        co->value = js_undef();
    } else {
        js_value e = J->exception;
        J->throwing = false;
        J->exception = js_undef();
        if (e.t == JS_T_OBJ && e.u.o->cls == JS_C_SIGNAL) {
            co->out_kind = OUT_RETURN;
            co->value = e.u.o->u.prim;
        } else {
            co->out_kind = OUT_THROW;
            co->value = e;
        }
    }
    co->state = CO_DONE;
    co_release(co);
    J->top = 0;
    switch_to(J, co, co->resumer);
    for (;;) {
        /* hierher kehrt nie jemand zurück */
    }
}

/*
 * Setzt co fort, bis sie sich wieder meldet. Danach liegt ihr Wert auf dem
 * Stapel und *kind sagt, was er bedeutet. false: eine Ausnahme - auch die,
 * mit der die Koroutine geendet hat.
 */
static bool co_resume(js_state *J, js_co *co, int mode, js_value value, int *kind)
{
    *kind = OUT_RETURN;
    if (co->state == CO_RUNNING)
        return js_throw(J, JS_E_TYPE, "Generator is already running");
    if (co->state == CO_DONE || (co->state == CO_NEW && mode != IN_NEXT)) {
        if (co->state == CO_NEW) {
            co->state = CO_DONE;
            co_release(co);
        }
        if (mode == IN_THROW)
            return js_throw_value(J, value);
        return js_push(J, mode == IN_RETURN ? value : js_undef());
    }
    js_costate *S = costate(J);
    if (!S)
        return false;
    if (S->nesting >= NESTING_MAX || J->top + 16 > JS_STACK_SIZE)
        return js_throw(J, JS_E_RANGE, "Maximum call stack size exceeded");
    js_co *caller = S->current;
    co->resumer = caller;
    co->in_mode = (uint8_t)mode;
    co->value = value;
    S->nesting++;
    bool switched = switch_to(J, caller, co);
    S = J->co;
    S->nesting--;
    co->resumer = NULL;
    if (!switched)
        return false;
    js_value v = co->value;
    co->value = js_undef();
    if (co->state == CO_DONE) {
        /* fertig: die Abbilder werden nicht mehr gebraucht */
        js_release(J, co->c_image, co->c_cap);
        js_release(J, co->v_image, (size_t)co->v_cap * sizeof(js_value));
        co->c_image = NULL;
        co->v_image = NULL;
        co->c_cap = co->c_size = 0;
        co->v_cap = co->v_count = 0;
    }
    if (J->fatal)
        return false;
    *kind = co->out_kind;
    if (co->out_kind == OUT_THROW)
        return js_throw_value(J, v);
    return js_push(J, v);
}

/*
 * In einer Koroutine: meldet den Wert oben auf dem Stapel nach draußen und
 * ruht, bis sie fortgesetzt wird. Danach liegt der hereingereichte Wert auf
 * dem Stapel, und *mode sagt, was mit ihm geschehen soll (IN_*).
 */
static bool co_suspend_raw(js_state *J, int kind, int *mode)
{
    js_costate *S = J->co;
    js_co *co = S ? S->current : NULL;
    *mode = IN_NEXT;
    if (!co)
        return js_throw(J, JS_E_SYNTAX, "yield or await outside of a generator or async function");
    co->value = *js_top(J, 0);
    co->out_kind = (uint8_t)kind;
    co->state = CO_SUSPENDED;
    js_pop(J, 1);
    bool switched = switch_to(J, co, co->resumer);
    co->state = CO_RUNNING;
    if (!switched)
        return false;
    js_value v = co->value;
    co->value = js_undef();
    if (J->fatal)
        return false;
    *mode = co->in_mode;
    return js_push(J, v);
}

/* Wie co_suspend_raw, aber der hereingereichte Wert wirkt gleich: als Wert, Ausnahme oder Rückkehr */
static bool co_suspend(js_state *J, int kind)
{
    int mode;
    if (!co_suspend_raw(J, kind, &mode))
        return false;
    if (mode == IN_NEXT)
        return true;
    js_value v = *js_top(J, 0);
    js_pop(J, 1);
    return mode == IN_THROW ? js_throw_value(J, v) : throw_return(J, v);
}

/* --- Die Objekte ------------------------------------------------------------------------------------- */

static void mark_co(js_state *J, js_co *co)
{
    js_value f = co->fn ? js_obj(co->fn) : js_undef(), e = co->env ? js_obj(co->env) : js_undef();
    js_gc_mark(J, &f);
    js_gc_mark(J, &e);
    js_gc_mark(J, &co->this_value);
    js_gc_mark(J, &co->value);
    for (int i = 0; i < co->v_count; i++)
        js_gc_mark(J, &co->v_image[i]);
}

static void co_mark(js_state *J, js_object *o)
{
    if (o->u.host.ptr)
        mark_co(J, o->u.host.ptr);
}

static void co_finalize(js_state *J, js_object *o)
{
    js_co *co = o->u.host.ptr;
    if (!co)
        return;
    js_release(J, co->c_image, co->c_cap);
    js_release(J, co->v_image, (size_t)co->v_cap * sizeof(js_value));
    js_release(J, co, sizeof(js_co));
    o->u.host.ptr = NULL;
}

static const js_host_class co_class = { "Generator", NULL, NULL, co_finalize, co_mark };

static js_co *co_of(const js_value *v)
{
    if (v->t != JS_T_OBJ || v->u.o->cls != JS_C_HOST || v->u.o->u.host.cls != &co_class)
        return NULL;
    return v->u.o->u.host.ptr;
}

/* Für die Speicherbereinigung: was gerade läuft oder auf einen inneren Ablauf wartet */
void js_co_mark_roots(js_state *J)
{
    js_costate *S = J->co;
    if (!S || !S->current)
        return;
    for (int i = 0; i < S->main.top; i++)
        js_gc_mark(J, &S->main.stack[i]);
    for (js_co *co = S->current; co; co = co->resumer) {
        js_value self = js_obj(co->self);
        js_gc_mark(J, &self);
        mark_co(J, co);
    }
}

bool js_co_active(const js_state *J)
{
    return J->co && J->co->current;
}

void js_co_free(js_state *J)
{
    costate_free(J->co);
    J->co = NULL;
}

/* --- Generatoren ------------------------------------------------------------------------------------- */

/* next (0), throw (1), return (2) eines Generators */
static bool generator_method(js_state *J, int argc)
{
    (void)argc;
    js_co *co = co_of(js_this(J));
    int kind;
    if (!co || co->kind != KIND_GENERATOR)
        return js_throw(J, JS_E_TYPE, "Method called on a value that is not a generator");
    if (!co_resume(J, co, MAGIC, *js_arg(J, 0), &kind))
        return false;
    return js_push_iter_result(J, *js_top(J, 0), kind == OUT_RETURN);
}

/*
 * Für den Vorspann: resume(koroutine, art, wert) setzt eine async-Funktion
 * fort und liefert { k, v } - k: 0 sie wartet auf v (await), 1 sie liefert v
 * (yield), 2 sie ist fertig mit v.
 */
static bool raw_resume(js_state *J, int argc)
{
    (void)argc;
    js_co *co = co_of(js_arg(J, 0));
    double mode = 0;
    int kind;
    if (!co || !js_to_number(J, ARGI(1), &mode))
        return js_throw(J, JS_E_TYPE, "Value is not a coroutine");
    if (!co_resume(J, co, mode == 1 ? IN_THROW : mode == 2 ? IN_RETURN : IN_NEXT, *js_arg(J, 2), &kind))
        return false;
    js_object *r = js_object_new(J, JS_C_OBJECT, J->object_proto);
    js_push(J, js_obj(r));
    js_define(J, r, js_intern_cstr(J, "k"), js_num(kind), JS_P_ALL);
    js_define(J, r, js_intern_cstr(J, "v"), *js_top(J, 1), JS_P_ALL);
    return !J->fatal;
}

/* Für den Vorspann: register(name, wert) hinterlegt etwas, das nur die Maschine selbst braucht */
static bool raw_register(js_state *J, int argc)
{
    (void)argc;
    if (!js_to_string(J, ARGI(0)))
        return false;
    js_define(J, J->registry, js_intern_string(J, js_arg(J, 0)->u.s), *js_arg(J, 1), JS_P_ALL);
    return js_push(J, js_undef());
}

static js_value registered(js_state *J, const char *name)
{
    js_prop *p = js_own(J->registry, js_intern_cstr(J, name));
    return p ? p->v : js_undef();
}

/*
 * Aufruf einer Generator- oder async-Funktion: Die Argumente sind schon
 * gebunden (env). Der Rumpf läuft später als Koroutine; hier entsteht das,
 * was der Aufrufer bekommt: ein Generator oder ein Promise.
 * Stapel bei base: [funktion, this, ...].
 */
bool js_co_call(js_state *J, int base, js_object *env, js_value *result)
{
    js_object *f = J->stack[base].u.o;
    bool generator = f->flags & JS_F_GENERATOR, is_async = f->flags & JS_F_ASYNC;
    js_object *proto = NULL;
    if (generator) {
        /* die Eigenschaft "prototype" der Funktion: von ihr erbt, was der Aufruf liefert */
        if (!js_get(J, base, J->k[K_prototype]))
            return false;
        if (js_top(J, 0)->t == JS_T_OBJ)
            proto = js_top(J, 0)->u.o;
        else if (!is_async)
            proto = J->generator_proto;
    }
    js_co *co = js_alloc(J, sizeof(js_co));
    if (!co)
        return false;
    memset(co, 0, sizeof(*co));
    js_object *o = js_object_new(J, JS_C_HOST, is_async ? NULL : proto);
    if (o == J->dummy) {
        js_release(J, co, sizeof(js_co));
        return false;
    }
    o->u.host.cls = &co_class;
    o->u.host.ptr = co;
    co->self = o;
    co->fn = f;
    co->env = env;
    co->this_value = J->stack[base + 1];
    co->value = js_undef();
    co->kind = !is_async ? KIND_GENERATOR : generator ? KIND_ASYNC_GENERATOR : KIND_ASYNC;
    js_push(J, js_obj(o));
    if (!is_async) {
        *result = js_obj(o);
        return true;
    }
    /* async: Ein Helfer aus dem Vorspann treibt die Koroutine an und liefert das Promise */
    js_value helper = registered(J, generator ? "asyncGenerator" : "asyncRun");
    if (!js_is_callable(&helper))
        return js_throw(J, JS_E_TYPE, "async functions are not available");
    js_push(J, helper);
    js_push(J, js_undef());
    js_push(J, js_obj(o));
    js_push(J, proto ? js_obj(proto) : js_undef());
    if (!js_call(J, 2))
        return false;
    *result = *js_top(J, 0);
    return true;
}

/* await: [wert] -> [ergebnis des Promise] - oder dessen Fehler als Ausnahme */
bool js_co_await(js_state *J)
{
    return co_suspend(J, OUT_AWAIT);
}

/* Ruft die Methode fn des Iterators (beide liegen bei slot, slot + 1) mit einem Argument */
static bool call_step(js_state *J, int slot, js_value fn, js_value arg, bool awaiting)
{
    js_push(J, fn);
    js_push(J, J->stack[slot]);
    js_push(J, arg);
    if (!js_call(J, 1))
        return false;
    if (awaiting && !js_co_await(J))
        return false;
    if (js_top(J, 0)->t != JS_T_OBJ)
        return js_throw(J, JS_E_TYPE, "Iterator result is not an object");
    return true;
}

/*
 * yield* quelle: reicht alles, was die Quelle liefert, nach draußen weiter -
 * und alles, was von draußen kommt (next, throw, return), an die Quelle.
 * [quelle] -> [rückgabewert der Quelle]
 */
static bool delegate(js_state *J)
{
    int base = J->top - 1;
    js_co *co = J->co ? J->co->current : NULL;
    bool awaiting = co && co->kind == KIND_ASYNC_GENERATOR;
    js_value source = J->stack[base];
    if (source.t <= JS_T_NULL)
        return js_throw(J, JS_E_TYPE, "Value is not iterable");
    /* [quelle] -> [iterator, next, empfangen] */
    bool found = false;
    if (awaiting) {
        if (!js_get(J, base, J->sym[SYM_asyncIterator]))
            return false;
        found = js_is_callable(js_top(J, 0));
        if (!found)
            js_pop(J, 1);
    }
    if (!found && !js_get(J, base, J->sym[SYM_iterator]))
        return false;
    if (!js_is_callable(js_top(J, 0)))
        return js_throw(J, JS_E_TYPE, "Value is not iterable");
    js_push(J, source);
    if (!js_call(J, 0))
        return false;
    if (js_top(J, 0)->t != JS_T_OBJ)
        return js_throw(J, JS_E_TYPE, "Result of the Symbol.iterator method is not an object");
    J->stack[base] = *js_top(J, 0);
    js_pop(J, 1);
    if (!js_get(J, base, J->k[K_next]))
        return false;
    js_push(J, js_undef());
    int mode = IN_NEXT;
    for (;;) {
        J->top = base + 3;
        js_value received = J->stack[base + 2];
        if (mode == IN_NEXT) {
            if (!call_step(J, base, J->stack[base + 1], received, awaiting))
                return false;
        } else {
            if (!js_get(J, base, J->k[mode == IN_THROW ? K_throw : K_return]))
                return false;
            js_value fn = *js_top(J, 0);
            js_pop(J, 1);
            if (fn.t <= JS_T_NULL) {
                if (mode == IN_RETURN)
                    return throw_return(J, received);
                /* die Quelle kennt throw() nicht: sie schließen und selbst scheitern */
                if (js_get(J, base, J->k[K_return]) && js_is_callable(js_top(J, 0))) {
                    js_push(J, J->stack[base]);
                    if (!js_call(J, 0))
                        return false;
                }
                return js_throw(J, JS_E_TYPE, "The iterator does not provide a 'throw' method");
            }
            if (!call_step(J, base, fn, received, awaiting))
                return false;
        }
        /* [.., ergebnis] */
        if (!js_get(J, J->top - 1, J->k[K_done]))
            return false;
        bool done = js_truthy(js_top(J, 0));
        js_pop(J, 1);
        if (!js_get(J, J->top - 1, J->k[K_value]))
            return false;
        if (done) {
            js_value v = *js_top(J, 0);
            if (mode == IN_RETURN)
                return throw_return(J, v);
            J->stack[base] = v;
            J->top = base + 1;
            return true;
        }
        if (!co_suspend_raw(J, OUT_YIELD, &mode))
            return false;
        J->stack[base + 2] = *js_top(J, 0);
    }
}

/* yield wert (oder yield* quelle): [wert] -> [hereingereichter wert] */
bool js_co_yield(js_state *J, bool delegating)
{
    return delegating ? delegate(J) : co_suspend(J, OUT_YIELD);
}

/* --- Einrichten ------------------------------------------------------------------------------------ */

void js_co_init(js_state *J)
{
    /* %GeneratorPrototype%: davon erbt das "prototype" jeder Generatorfunktion */
    J->generator_proto = js_object_new(J, JS_C_OBJECT, J->iterator_proto);
    js_def_native(J, J->generator_proto, "next", generator_method, 1, IN_NEXT);
    js_def_native(J, J->generator_proto, "throw", generator_method, 1, IN_THROW);
    js_def_native(J, J->generator_proto, "return", generator_method, 1, IN_RETURN);
    js_define(J, J->generator_proto, J->sym[SYM_toStringTag], js_str(js_intern_cstr(J, "Generator")),
              JS_P_CONF);

    /* nur für den Vorspann sichtbar; er räumt den Namen selbst wieder weg */
    js_object *tools = js_object_new(J, JS_C_OBJECT, NULL);
    js_def_value(J, J->global, "__co", js_obj(tools), JS_P_ALL);
    js_def_native(J, tools, "resume", raw_resume, 3, 0);
    js_def_native(J, tools, "register", raw_register, 2, 0);
}
