/* Präemptives Multitasking mit Kernel-Threads (Round Robin) */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MAX_TASKS      16
#define TASK_NAME_MAX  16

typedef enum {
    TASK_UNUSED,
    TASK_READY,                         /* wartet auf die CPU */
    TASK_RUNNING,
    TASK_SLEEPING,                      /* wartet auf einen Zeitpunkt */
    TASK_BLOCKED,                       /* wartet auf ein Ereignis */
    TASK_ZOMBIE,                        /* beendet, Stack noch nicht freigegeben */
} task_state_t;

typedef struct {
    uint32_t pid;
    task_state_t state;
    char name[TASK_NAME_MAX];
    uint32_t cpu_ticks;
} task_info_t;

/* Macht den laufenden Code zum Leerlauf-Task (PID 0). */
void task_init(void);

/* Startet einen neuen Kernel-Thread; liefert die PID oder -1. */
int task_create(const char *name, void (*entry)(void *arg), void *arg);

void task_yield(void);
void task_sleep(uint32_t ms);
void task_exit(void) __attribute__((noreturn));
bool task_kill(uint32_t pid);

/* Aufrufenden Task blockieren - nur mit gesperrten Interrupts aufrufen. */
void task_block(void);
void task_wake(uint32_t pid);

/* Vom Timer-Interrupt aufgerufen. */
void task_tick(uint32_t now);

/* Gibt die Stacks beendeter Tasks frei. */
void task_reap(void);

uint32_t task_current_pid(void);
const char *task_current_name(void);
int task_snapshot(task_info_t *out, int max);
