#include "task.h"
#include "io.h"
#include "memory.h"
#include "string.h"
#include "timer.h"

#include <stddef.h>

#define STACK_SIZE 16384

typedef struct task {
    uint32_t esp;                       /* gesicherter Stackpointer */
    uint32_t pid;
    task_state_t state;
    char name[TASK_NAME_MAX];
    void (*entry)(void *arg);
    void *arg;
    void *stack;
    uint32_t wake_tick;
    uint32_t cpu_ticks;
} task_t;

void context_switch(uint32_t *old_esp, uint32_t new_esp);   /* switch.asm */

static task_t tasks[MAX_TASKS];         /* tasks[0] ist der Leerlauf-Task */
static task_t *current;
static uint32_t next_pid = 1;

/*
 * Wählt den nächsten lauffähigen Task (Round Robin) und wechselt zu ihm.
 * Muss mit gesperrten Interrupts aufgerufen werden.
 */
static void schedule(void)
{
    task_t *prev = current;
    task_t *next = &tasks[0];           /* Leerlauf, falls sonst niemand will */

    if (prev->state == TASK_RUNNING)
        prev->state = TASK_READY;

    int start = (int)(prev - tasks);
    for (int i = 1; i <= MAX_TASKS; i++) {
        task_t *t = &tasks[(start + i) % MAX_TASKS];
        if (t != &tasks[0] && t->state == TASK_READY) {
            next = t;
            break;
        }
    }

    next->state = TASK_RUNNING;
    if (next != prev) {
        current = next;
        context_switch(&prev->esp, next->esp);
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
}

int task_create(const char *name, void (*entry)(void *arg), void *arg)
{
    task_reap();

    void *stack = kmalloc(STACK_SIZE);
    if (!stack)
        return -1;

    uint32_t flags = irq_save();
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
     * vier gesicherte Register und die Rücksprungadresse zum Trampolin.
     */
    uint32_t *sp = (uint32_t *)((uint8_t *)stack + STACK_SIZE);
    *--sp = 0;                          /* Rücksprungadresse des Trampolins */
    *--sp = (uint32_t)task_trampoline;
    *--sp = 0;                          /* ebp */
    *--sp = 0;                          /* ebx */
    *--sp = 0;                          /* esi */
    *--sp = 0;                          /* edi */

    t->esp = (uint32_t)sp;
    t->pid = next_pid++;
    t->entry = entry;
    t->arg = arg;
    t->stack = stack;
    t->cpu_ticks = 0;
    strlcpy(t->name, name, TASK_NAME_MAX);
    t->state = TASK_READY;

    int pid = (int)t->pid;
    irq_restore(flags);
    return pid;
}

void task_yield(void)
{
    uint32_t flags = irq_save();
    schedule();
    irq_restore(flags);
}

void task_sleep(uint32_t ms)
{
    uint32_t ticks = (ms * TIMER_HZ + 999) / 1000;
    if (ticks == 0)
        ticks = 1;

    uint32_t flags = irq_save();
    current->wake_tick = timer_ticks() + ticks;
    current->state = TASK_SLEEPING;
    schedule();
    irq_restore(flags);
}

void task_block(void)
{
    current->state = TASK_BLOCKED;
    schedule();
}

void task_wake(uint32_t pid)
{
    uint32_t flags = irq_save();
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].pid == pid && tasks[i].state == TASK_BLOCKED)
            tasks[i].state = TASK_READY;
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
    uint32_t flags = irq_save();
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

void task_tick(uint32_t now)
{
    if (!current)
        return;                         /* Multitasking läuft noch nicht */
    current->cpu_ticks++;

    for (int i = 1; i < MAX_TASKS; i++) {
        task_t *t = &tasks[i];
        if (t->state == TASK_SLEEPING && (int32_t)(now - t->wake_tick) >= 0)
            t->state = TASK_READY;
    }
    schedule();
}

void task_reap(void)
{
    for (int i = 1; i < MAX_TASKS; i++) {
        uint32_t flags = irq_save();
        task_t *t = &tasks[i];
        void *stack = NULL;
        if (t->state == TASK_ZOMBIE && t != current) {
            stack = t->stack;
            t->stack = NULL;
            t->state = TASK_UNUSED;
        }
        irq_restore(flags);
        kfree(stack);
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

int task_snapshot(task_info_t *out, int max)
{
    int count = 0;
    uint32_t flags = irq_save();
    for (int i = 0; i < MAX_TASKS && count < max; i++) {
        const task_t *t = &tasks[i];
        if (t->state == TASK_UNUSED)
            continue;
        out[count].pid = t->pid;
        out[count].state = t->state;
        out[count].cpu_ticks = t->cpu_ticks;
        strlcpy(out[count].name, t->name, TASK_NAME_MAX);
        count++;
    }
    irq_restore(flags);
    return count;
}
