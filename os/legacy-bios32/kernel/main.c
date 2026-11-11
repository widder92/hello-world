/*
 * HelloOS - Kernel-Hauptprogramm
 *
 * Der Bootloader springt nach entry.asm, von dort geht es hierher. kmain()
 * initialisiert der Reihe nach alle Teile des Kernels, startet die ersten
 * Tasks und wird danach selbst zum Leerlauf-Task.
 */
#include "console.h"
#include "fs.h"
#include "gdt.h"
#include "idt.h"
#include "input.h"
#include "io.h"
#include "memory.h"
#include "rtc.h"
#include "serial.h"
#include "shell.h"
#include "task.h"
#include "timer.h"
#include "vga.h"

static void ok(const char *what)
{
    kprintf("[ ");
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    kprintf("OK");
    vga_reset_color();
    kprintf(" ] %s\n", what);
}

static void banner(void)
{
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    kprintf("  _   _      _ _        ___  ____\n"
            " | | | | ___| | | ___  / _ \\/ ___|\n"
            " | |_| |/ _ \\ | |/ _ \\| | | \\___ \\\n"
            " |  _  |  __/ | | (_) | |_| |___) |\n"
            " |_| |_|\\___|_|_|\\___/ \\___/|____/   Version %s\n\n",
            OS_VERSION);
    vga_reset_color();
}

/* Hält die oberste Bildschirmzeile aktuell - läuft als eigener Task. */
static void statusbar_task(void *arg)
{
    (void)arg;
    for (;;) {
        char left[64], line[VGA_WIDTH + 1];
        task_info_t tasks[MAX_TASKS];
        datetime_t now;

        int count = task_snapshot(tasks, MAX_TASKS);
        uint32_t s = timer_ticks() / TIMER_HZ;
        rtc_read(&now);

        ksnprintf(left, sizeof(left), " %s %s | Tasks: %d | Laufzeit %02u:%02u:%02u",
                  OS_NAME, OS_VERSION, count, s / 3600, s / 60 % 60, s % 60);
        ksnprintf(line, sizeof(line), "%-60s%02u.%02u.%u %02u:%02u:%02u ", left,
                  now.day, now.month, now.year, now.hour, now.minute,
                  now.second);
        vga_write_status(line, VGA_WHITE, VGA_BLUE);

        task_sleep(250);
    }
}

void kmain(void)
{
    serial_init();
    vga_init();
    banner();

    gdt_init();
    ok("Segmentierung eingerichtet (GDT)");

    idt_init();
    ok("Interrupts eingerichtet (IDT, PIC)");

    memory_init();
    memory_stats_t mem;
    memory_stats(&mem);
    kprintf("[ ");
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    kprintf("OK");
    vga_reset_color();
    kprintf(" ] Speicher: %u MiB RAM, %u MiB Kernel-Heap\n",
            (mem.total_kb + 512) / 1024, mem.heap_size / (1024 * 1024));

    timer_init();
    ok("Systemtakt läuft (PIT, 100 Hz)");

    keyboard_init();
    serial_enable_input();
    ok("Tastatur und serielle Konsole bereit");

    fs_init();
    ok("RAM-Dateisystem angelegt");

    task_init();
    task_create("shell", shell_task, NULL);
    task_create("statusleiste", statusbar_task, NULL);
    ok("Multitasking gestartet");

    irq_enable();

    /* Ab hier ist kmain der Leerlauf-Task: aufräumen und die CPU schlafen legen. */
    for (;;) {
        task_reap();
        cpu_halt();
    }
}
