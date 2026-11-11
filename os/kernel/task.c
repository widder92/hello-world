#include "task.h"
#include "cpu.h"
#include "memory.h"
#include "string.h"
#include "time.h"

#define STACK_SIZE (64 * 1024)
#define TIME_SLICE 10                   /* Ticks (ms), bis der nächste Task drankommt */

typedef struct task {
    uint64_t rsp;                       /* gesicherter Stackpointer */
    uint32_t pid;
    task_state_t state;
    char name[TASK_NAME_MAX];
    void (*entry)(void *arg);
    void *arg;
    void *stack;
    console_t *console;
    uint64_t wake_ms;                   /* Weckzeit; 0 = keine Frist */
    uint64_t cpu_tsc;                   /* Rechenzeit in TSC-Einheiten */
    bool boosted;                       /* gerade aufgewacht: bevorzugen */
} task_t;

void context_switch(uint64_t *old_rsp, uint64_t new_rsp);   /* lowlevel.asm */

static task_t tasks[MAX_TASKS];         /* tasks[0] ist der Leerlauf-Task */
static task_t *current;
static uint32_t next_pid = 1;
static uint64_t switch_tsc;             /* Zeitpunkt des letzten Wechsels */
static int slice_left = TIME_SLICE;
static bool need_resched;

/*
 * Wählt den nächsten lauffähigen Task und wechselt zu ihm. Tasks, die gerade
 * aus dem Warten aufgewacht sind (Eingaben, Oberfläche), haben Vorrang vor
 * solchen, die einfach nur rechnen; ansonsten geht es reihum.
 * Muss mit gesperrten Interrupts aufgerufen werden.
 */
static void schedule(void)
{
    task_t *prev = current;
    task_t *next = NULL;

    uint64_t now = rdtsc();
    prev->cpu_tsc += now - switch_tsc;
    switch_tsc = now;

    if (prev->state == TASK_RUNNING)
        prev->state = TASK_READY;

    int start = (int)(prev - tasks);
    for (int pass = 0; pass < 2 && !next; pass++) {
        for (int i = 1; i <= MAX_TASKS; i++) {
            task_t *t = &tasks[(start + i) % MAX_TASKS];
            if (t == &tasks[0] || t->state != TASK_READY)
                continue;
            if (pass == 0 && !t->boosted)
                continue;
            next = t;
            break;
        }
    }
    if (!next)
        next = &tasks[0];               /* Leerlauf, wenn sonst niemand will */

    next->boosted = false;
    next->state = TASK_RUNNING;
    slice_left = TIME_SLICE;
    need_resched = false;
    if (next != prev) {
        current = next;
        context_switch(&prev->rsp, next->rsp);
    }
}

/* Erster Code jedes neuen Tasks. */
static void task_trampoline(void)
{
    irq_enable();
    current->entry(current->arg);
    task_exit();
}

void task_init(void)
{
    current = &tasks[0];
    current->pid = 0;
    current->state = TASK_RUNNING;
    strlcpy(current->name, "leerlauf", TASK_NAME_MAX);
    switch_tsc = rdtsc();
}

int task_create(const char *name, void (*entry)(void *arg), void *arg,
                console_t *console)
{
    task_reap();

    void *stack = kmalloc(STACK_SIZE);
    if (!stack)
        return -1;

    uint64_t flags = irq_save();
    task_t *t = NULL;
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_UNUSED) {
            t = &tasks[i];
            break;
        }
    }
    if (!t) {
        irq_restore(flags);
        kfree(stack);
        return -1;
    }

    /*
     * Stack so vorbereiten, als hätte der Task context_switch() aufgerufen:
     * acht gesicherte Register und die Rücksprungadresse zum Trampolin.
     * Darüber bleibt Platz für den "Shadow Space" der Aufrufkonvention.
     */
    uint64_t *sp = (uint64_t *)((uint8_t *)stack + STACK_SIZE - 48);
    *--sp = 0;                          /* Rücksprungadresse des Trampolins */
    *--sp = (uint64_t)task_trampoline;
    for (int i = 0; i < 8; i++)
        *--sp = 0;                      /* rbp, rbx, rdi, rsi, r12-r15 */

    if (!console && current)
        console = current->console;
    console_ref(console);

    t->rsp = (uint64_t)sp;
    t->pid = next_pid++;
    t->entry = entry;
    t->arg = arg;
    t->stack = stack;
    t->console = console;
    t->cpu_tsc = 0;
    t->wake_ms = 0;
    t->boosted = true;
    strlcpy(t->name, name, TASK_NAME_MAX);
    t->state = TASK_READY;

    int pid = (int)t->pid;
    irq_restore(flags);
    return pid;
}

void task_sleep(uint32_t ms)
{
    uint64_t flags = irq_save();
    current->wake_ms = time_ms() + (ms ? ms : 1);
    current->state = TASK_SLEEPING;
    schedule();
    irq_restore(flags);
}

void task_block(uint32_t timeout_ms)
{
    current->wake_ms = timeout_ms ? time_ms() + timeout_ms : 0;
    current->state = TASK_BLOCKED;
    schedule();
}

void task_wake(uint32_t pid)
{
    uint64_t flags = irq_save();
    for (int i = 1; i < MAX_TASKS; i++) {
        task_t *t = &tasks[i];
        if (t->pid == pid && t->state == TASK_BLOCKED) {
            t->state = TASK_READY;
            t->boosted = true;
            need_resched = true;
        }
    }
    irq_restore(flags);
}

void task_exit(void)
{
    irq_disable();
    current->state = TASK_ZOMBIE;
    schedule();
    for (;;)                            /* wird nie erreicht */
        cpu_halt();
}

bool task_kill(uint32_t pid)
{
    bool found = false;
    uint64_t flags = irq_save();
    for (int i = 1; i < MAX_TASKS; i++) {
        task_t *t = &tasks[i];
        if (t->pid != pid || t == current)
            continue;
        if (t->state != TASK_UNUSED && t->state != TASK_ZOMBIE) {
            t->state = TASK_ZOMBIE;
            found = true;
        }
    }
    irq_restore(flags);
    return found;
}

bool task_alive(uint32_t pid)
{
    bool alive = false;
    uint64_t flags = irq_save();
    for (int i = 0; i < MAX_TASKS; i++) {
        const task_t *t = &tasks[i];
        if (t->pid == pid && t->state != TASK_UNUSED && t->state != TASK_ZOMBIE)
            alive = true;
    }
    irq_restore(flags);
    return alive;
}

void task_tick(void)
{
    if (!current)
        return;                         /* Multitasking läuft noch nicht */

    uint64_t now = time_ms();
    for (int i = 1; i < MAX_TASKS; i++) {
        task_t *t = &tasks[i];
        bool waiting = t->state == TASK_SLEEPING ||
                       (t->state == TASK_BLOCKED && t->wake_ms != 0);
        if (waiting && now >= t->wake_ms) {
            t->state = TASK_READY;
            t->boosted = true;
            need_resched = true;
        }
    }

    if (--slice_left <= 0 || need_resched || current == &tasks[0])
        schedule();
}

void task_reap(void)
{
    for (int i = 1; i < MAX_TASKS; i++) {
        uint64_t flags = irq_save();
        task_t *t = &tasks[i];
        void *stack = NULL;
        console_t *console = NULL;
        if (t->state == TASK_ZOMBIE && t != current) {
            stack = t->stack;
            console = t->console;
            t->stack = NULL;
            t->console = NULL;
            t->state = TASK_UNUSED;
        }
        irq_restore(flags);
        kfree(stack);
        console_unref(console);
    }
}

uint32_t task_current_pid(void)
{
    return current ? current->pid : 0;
}

const char *task_current_name(void)
{
    return current ? current->name : "kernel";
}

console_t *task_console(void)
{
    return current ? current->console : NULL;
}

void task_set_console(console_t *console)
{
    uint64_t flags = irq_save();
    console_t *old = current->console;
    current->console = console;
    irq_restore(flags);

    console_ref(console);
    console_unref(old);
}

int task_snapshot(task_info_t *out, int max)
{
    int count = 0;
    uint64_t flags = irq_save();
    uint64_t now = rdtsc();
    for (int i = 0; i < MAX_TASKS && count < max; i++) {
        const task_t *t = &tasks[i];
        if (t->state == TASK_UNUSED)
            continue;
        uint64_t tsc = t->cpu_tsc;
        if (t == current)
            tsc += now - switch_tsc;    /* angebrochene Zeitscheibe mitzählen */
        out[count].pid = t->pid;
        out[count].state = t->state;
        out[count].cpu_ms = time_tsc_to_ms(tsc);
        strlcpy(out[count].name, t->name, TASK_NAME_MAX);
        count++;
    }
    irq_restore(flags);
    return count;
}
