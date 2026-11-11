/*
 * HelloOS - Kernel-Hauptprogramm
 *
 * Der Startcode (boot.c) hat die Firmware verabschiedet und ruft kmain() auf.
 * Hier werden der Reihe nach alle Teile des Kernels eingerichtet, die ersten
 * Tasks gestartet - danach ist kmain() selbst der Leerlauf-Task.
 */
#include "bootinfo.h"
#include "console.h"
#include "cpu.h"
#include "crypto.h"
#include "fs.h"
#include "gui.h"
#include "input.h"
#include "interrupts.h"
#include "memory.h"
#include "net.h"
#include "serial.h"
#include "shell.h"
#include "task.h"
#include "time.h"
#include "update.h"
#include "usb.h"

/* Shell auf der seriellen Schnittstelle - praktisch zum Testen ohne Bildschirm. */
static void serial_shell(void *arg)
{
    (void)arg;
    shell_run();
}

void kmain(void *unused)
{
    (void)unused;

    interrupts_init();
    time_init();
    memory_init();
    gui_init();

    memory_stats_t mem;
    memory_stats(&mem);
    klog("Prozessor im 64-Bit-Modus, eigene Seitentabellen aktiv\n");
    klog("Speicher: %llu MiB nutzbar, Kernel-Heap %llu MiB\n",
         mem.total_bytes / (1024 * 1024), mem.heap_size / (1024 * 1024));
    if (gui_available())
        klog("Bildschirm: %u x %u Pixel\n", boot_info.fb_width,
             boot_info.fb_height);
    else
        klog("Kein Bildschirm gefunden - nur serielle Konsole\n");

    timer_start();
    klog("Systemtakt: %u Hz (Zeitstempelzähler: %llu MHz)\n", TICK_HZ,
         boot_info.tsc_hz / 1000000);

    random_stir(&boot_info.boot_time, sizeof(boot_info.boot_time));
    random_stir(&boot_info.tsc_hz, sizeof(boot_info.tsc_hz));

    keyboard_init();
    ps2_init();
    fs_init();
    task_init();

    usb_init();
    net_init();
    update_init();
    if (serial_present())
        task_create("shell-seriell", serial_shell, NULL, serial_console());
    gui_start();
    klog("Multitasking gestartet\n");

    irq_enable();

    /* Leerlauf: beendete Tasks aufräumen und die CPU schlafen legen. */
    for (;;) {
        task_reap();
        cpu_halt();
    }
}
