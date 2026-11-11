/* Präemptives Multitasking mit Kernel-Threads */
#pragma once

#include "console.h"

#include <stdbool.h>
#include <stdint.h>

#define MAX_TASKS      32
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
    uint64_t cpu_ms;                    /* verbrauchte Rechenzeit */
} task_info_t;

/* Macht den laufenden Code zum Leerlauf-Task (PID 0). */
void task_init(void);

/*
 * Startet einen neuen Kernel-Thread; liefert die PID oder -1. Der Task
 * schreibt auf die angegebene Konsole (NULL: die des Erzeugers erben).
 */
int task_create(const char *name, void (*entry)(void *arg), void *arg,
                console_t *console);

void task_sleep(uint32_t ms);
void task_exit(void) __attribute__((noreturn));
bool task_kill(uint32_t pid);
bool task_alive(uint32_t pid);

/*
 * Aufrufenden Task blockieren, bis task_wake() ihn weckt oder die Frist
 * abläuft (0 = keine Frist). Nur mit gesperrten Interrupts aufrufen.
 */
void task_block(uint32_t timeout_ms);
void task_wake(uint32_t pid);

/* Vom Systemtakt aufgerufen. */
void task_tick(void);

/* Gibt die Stacks beendeter Tasks frei. */
void task_reap(void);

uint32_t task_current_pid(void);
const char *task_current_name(void);
console_t *task_console(void);
void task_set_console(console_t *console);   /* für den laufenden Task */
int task_snapshot(task_info_t *out, int max);
