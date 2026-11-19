/*
 * Test für die JavaScript-Maschine (kernel/js*.c) auf dem Entwicklungsrechner.
 *
 *   js_test                 führt alle Skripte in tests/js/ aus; jedes prüft sich mit assert()
 *   js_test datei.js        führt ein Skript aus und zeigt, was es mit console.log ausgibt
 *   js_test --fuzz N        verändert die Testskripte N-mal zufällig: nichts darf abstürzen
 *
 * Gebaut mit -fsanitize=address,undefined; die Speicherbereinigung läuft dabei
 * bei jeder Gelegenheit, damit vergessene Verweise sofort auffallen.
 */
#include "../kernel/js_impl.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#define strlcpy libc_strlcpy            /* der Kernel hat ein eigenes, etwas anderes */
#include <string.h>
#undef strlcpy

/*
 * Das Prüfwerkzeug soll lokale Variablen auf dem echten Stack lassen: Die
 * Koroutinen der Maschine kopieren Stücke davon um (kernel/js_co.c).
 */
const char *__asan_default_options(void) { return "detect_stack_use_after_return=0"; }

void *kmalloc(size_t n) { return malloc(n ? n : 1); }
void *kzalloc(size_t n) { return calloc(1, n ? n : 1); }
void kfree(void *p) { free(p); }
void strlcpy(char *d, const char *s, size_t n)
{
    if (!n) return;
    size_t i = 0;
    for (; i + 1 < n && s[i]; i++) d[i] = s[i];
    d[i] = 0;
}

static int checks, failures;
static bool quiet, echo;
static uint64_t fake_now = 1000;
static const char *current;

/*
 * Die Uhr der Tests steht still, solange ein Skript rechnet - außer im
 * Zufallstest: Dort vergeht mit jeder Nachfrage eine Millisekunde, damit auch
 * ein verstümmeltes Skript mit einer Endlosschleife an seine Zeitgrenze stößt.
 */
static bool clock_runs;
static uint64_t ticks(void *ctx) { (void)ctx; return clock_runs ? ++fake_now : fake_now; }
static int64_t wall(void *ctx) { (void)ctx; return 1794670200000LL + (int64_t)fake_now; }
static void print(void *ctx, const char *text)
{
    (void)ctx;
    if (echo)
        printf("%s\n", text);
}

static bool native_assert(js_state *J, int argc)
{
    checks++;
    if (!js_truthy(js_arg(J, 0))) {
        failures++;
        if (!quiet) {
            const char *what = "(ohne Namen)";
            if (argc > 1 && js_to_string(J, J->bot + 3))
                what = js_arg(J, 1)->u.s->data;
            printf("FEHLER in %s (Zeile %d): %s\n", current, J->line, what);
        }
    }
    return js_push(J, js_undef());
}

/* assertEq(ist, soll, name): gleich als Werte - oder gleich, wenn beide zu Text gemacht werden */
static bool native_assert_eq(js_state *J, int argc)
{
    checks++;
    js_value a = *js_arg(J, 0), b = *js_arg(J, 1);
    bool same = js_strict_equal(&a, &b);
    if (!same && a.t == JS_T_NUM && b.t == JS_T_NUM && js_isnan(a.u.n) && js_isnan(b.u.n))
        same = true;
    if (!same) {
        failures++;
        if (!quiet) {
            char got[200] = "?", want[200] = "?";
            js_push(J, a);
            if (js_to_string(J, J->top - 1))
                snprintf(got, sizeof(got), "%s", js_top(J, 0)->u.s->data);
            js_push(J, b);
            if (js_to_string(J, J->top - 1))
                snprintf(want, sizeof(want), "%s", js_top(J, 0)->u.s->data);
            const char *what = "";
            if (argc > 2 && js_to_string(J, J->bot + 4))
                what = js_arg(J, 2)->u.s->data;
            printf("FEHLER in %s (Zeile %d): %s - ist \"%s\", soll \"%s\"\n", current, J->line, what,
                   got, want);
            J->throwing = false;
        }
    }
    return js_push(J, js_undef());
}

static bool native_gc(js_state *J, int argc)
{
    (void)argc;
    js_gc_run(J);
    return js_push(J, js_num((double)J->heap));
}

static js_state *fresh(size_t heap, bool stress)
{
    js_host_t host = { .heap_max = heap, .stack_bytes = 400 * 1024, .clock_ms = wall,
                       .ticks_ms = ticks, .print = print };
    js_state *J = js_create(&host);
    if (!J)
        return NULL;
    J->gc_stress = stress;
    js_def_native(J, J->global, "assert", native_assert, 2, 0);
    js_def_native(J, J->global, "assertEq", native_assert_eq, 3, 0);
    js_def_native(J, J->global, "gc", native_gc, 0, 0);
    js_def_value(J, J->global, "HELLOOS", js_bool(true), 0);
    return J;
}

/* Lässt die Zeitgeber ablaufen, als verginge die Zeit sofort */
static void run_timers(js_state *J, int rounds)
{
    for (int i = 0; i < rounds && !js_dead(J); i++) {
        int64_t wait = js_timers_due(J);
        if (wait < 0)
            break;
        fake_now += (uint64_t)wait;
        js_timers_run(J, 2000);
    }
}

static char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *text = malloc((size_t)n + 1);
    if (fread(text, 1, (size_t)n, f) != (size_t)n)
        n = 0;
    text[n] = 0;
    fclose(f);
    *len = (size_t)n;
    return text;
}

static int compare_names(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

int main(int argc, char **argv)
{
    const char *dir = "tests/js";
    char *names[200];
    int count = 0;
    DIR *d = opendir(dir);
    for (struct dirent *e; d && (e = readdir(d)) && count < 200;) {
        size_t n = strlen(e->d_name);
        if (n > 3 && strcmp(e->d_name + n - 3, ".js") == 0)
            names[count++] = strdup(e->d_name);
    }
    if (d)
        closedir(d);
    qsort(names, (size_t)count, sizeof(char *), compare_names);

    if (argc > 1 && strcmp(argv[1], "--fuzz") != 0) {
        size_t len;
        char *text = slurp(argv[1], &len);
        if (!text) {
            perror(argv[1]);
            return 2;
        }
        echo = true;
        current = argv[1];
        js_state *J = fresh(64u * 1024 * 1024, argc > 2);
        bool ok = js_run(J, text, len, argv[1], 20000);
        if (!ok)
            printf("ABBRUCH: %s\n", js_last_error(J));
        run_timers(J, 10000);
        printf("(%d Prüfungen, %d Fehler, %u Aufräumläufe, %zu Bytes belegt)\n", checks, failures,
               J->gc_runs, J->heap);
        js_destroy(J);
        free(text);
        for (int i = 0; i < count; i++)
            free(names[i]);
        return ok && !failures ? 0 : 1;
    }

    if (argc > 2) {
        /* Zufallstest: Skripte verstümmeln; es darf nur sauber scheitern */
        int rounds = atoi(argv[2]);
        unsigned seed = argc > 3 ? (unsigned)atoi(argv[3]) : 4711;
        quiet = true;
        clock_runs = true;
        for (int r = 0; r < rounds && count > 0; r++) {
            size_t len;
            char path[400];
            seed = seed * 1103515245u + 12345u;
            snprintf(path, sizeof(path), "%s/%s", dir, names[(seed >> 8) % (unsigned)count]);
            char *text = slurp(path, &len);
            if (!text || len == 0) {
                free(text);
                continue;
            }
            /* ein Stück herausgreifen und beschädigen */
            seed = seed * 1103515245u + 12345u;
            size_t from = (seed >> 8) % len, n = len - from;
            seed = seed * 1103515245u + 12345u;
            if (n > 3000)
                n = 200 + (seed >> 8) % 2800;
            char *piece = malloc(n + 1);
            memcpy(piece, text + from, n);
            int edits = r % 4 == 0 ? 0 : 1 + (int)((seed >> 4) % 8);
            for (int k = 0; k < edits; k++) {
                seed = seed * 1103515245u + 12345u;
                size_t at = (seed >> 8) % n;
                static const char junk[] = "(){}[];,.+-*/=<>!&|?:'\"`\\ \n$0aZ_";
                seed = seed * 1103515245u + 12345u;
                switch ((seed >> 12) % 4) {
                case 0: piece[at] = junk[(seed >> 16) % (sizeof(junk) - 1)]; break;
                case 1: piece[at] = (char)(seed >> 16); break;
                case 2: if (at + 1 < n) { char t = piece[at]; piece[at] = piece[at + 1]; piece[at + 1] = t; } break;
                default: memmove(piece + at, piece + at + 1, n - at - 1); n--; if (n == 0) n = 1; break;
                }
            }
            current = path;
            js_state *J = fresh(r % 7 == 0 ? 300 * 1024 : 8u * 1024 * 1024, r % 3 == 0);
            if (J) {
                js_run(J, piece, n, "fuzz", 300);
                run_timers(J, 20);
                js_destroy(J);
            }
            free(piece);
            free(text);
        }
        printf("%d verstümmelte Skripte verarbeitet\n", rounds);
        for (int i = 0; i < count; i++)
            free(names[i]);
        return 0;
    }

    for (int i = 0; i < count; i++) {
        size_t len;
        char path[400];
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        char *text = slurp(path, &len);
        if (!text)
            continue;
        current = names[i];
        /* jedes Skript zweimal: gewöhnlich und mit Aufräumen bei jeder Gelegenheit */
        for (int stress = 0; stress < 2; stress++) {
            int before = failures;
            js_state *J = fresh(64u * 1024 * 1024, stress);
            if (!J) {
                failures++;
                continue;
            }
            if (!js_run(J, text, len, names[i], 60000)) {
                failures++;
                printf("FEHLER in %s: %s\n", names[i], js_last_error(J));
            }
            run_timers(J, 10000);
            if (js_dead(J) && failures == before) {
                failures++;
                printf("FEHLER in %s: abgebrochen\n", names[i]);
            }
            js_destroy(J);
        }
        free(text);
    }
    printf("%d Prüfungen an der JavaScript-Maschine in %d Skripten, %d Fehler\n", checks, count,
           failures);
    for (int i = 0; i < count; i++)
        free(names[i]);
    return failures != 0;
}
