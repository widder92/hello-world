#include "interrupts.h"
#include "bootinfo.h"
#include "console.h"
#include "cpu.h"
#include "gui.h"
#include "input.h"
#include "memory.h"
#include "serial.h"
#include "task.h"
#include "time.h"

#define VECTOR_TIMER    32
#define VECTOR_SPURIOUS 47

/* --- Segmenttabelle (GDT) ---------------------------------------------------- */

struct table_pointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

/* Im 64-Bit-Modus gibt es keine echte Segmentierung mehr - Pflicht bleibt sie. */
static uint64_t gdt[3] = {
    0,
    0x00AF9A000000FFFFull,              /* 0x08: Kernel-Code, 64 Bit */
    0x00CF92000000FFFFull,              /* 0x10: Kernel-Daten */
};

void gdt_load(void *pointer);           /* lowlevel.asm */

/* --- Interrupt-Tabelle (IDT) -------------------------------------------------- */

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t flags;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

extern const uint64_t isr_table[256];

static struct idt_entry idt[256];

static const char *const exception_names[32] = {
    "Division durch Null",
    "Debug",
    "Nicht maskierbarer Interrupt",
    "Breakpoint",
    "Überlauf",
    "Bereichsüberschreitung",
    "Ungültiger Opcode",
    "Gerät nicht verfügbar",
    "Double Fault",
    "Koprozessor-Segmentüberlauf",
    "Ungültiges TSS",
    "Segment nicht vorhanden",
    "Stack-Segment-Fehler",
    "Allgemeine Schutzverletzung",
    "Seitenfehler",
    "Reserviert",
    "x87-Gleitkommafehler",
    "Ausrichtungsfehler",
    "Machine Check",
    "SIMD-Gleitkommafehler",
    "Virtualisierungsfehler",
    "Control-Protection-Fehler",
    "Reserviert", "Reserviert", "Reserviert", "Reserviert", "Reserviert",
    "Reserviert", "Reserviert", "Reserviert", "Reserviert", "Reserviert",
};

void panic(const char *fmt, ...)
{
    static char text[1024];

    irq_disable();
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);

    serial_write("\n*** KERNEL PANIC ***\n");
    serial_write(text);
    serial_write("\nSystem angehalten.\n");
    gui_panic_screen(text);
    for (;;)
        cpu_halt();
}

static void exception(regs_t *r)
{
    uint64_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));

    panic("%s (Ausnahme %llu, Fehlercode 0x%llx)\n"
          "Task: %s (PID %u)\n"
          "RIP=%p  RSP=%p\n"
          "RFLAGS=%p  CR2=%p\n"
          "RAX=%p  RBX=%p\n"
          "RCX=%p  RDX=%p\n"
          "RSI=%p  RDI=%p\n"
          "RBP=%p  R8 =%p",
          exception_names[r->int_no], r->int_no, r->err_code,
          task_current_name(), task_current_pid(),
          (void *)r->rip, (void *)r->rsp, (void *)r->rflags, (void *)cr2,
          (void *)r->rax, (void *)r->rbx, (void *)r->rcx, (void *)r->rdx,
          (void *)r->rsi, (void *)r->rdi, (void *)r->rbp, (void *)r->r8);
}

/* --- Local APIC: der Interrupt-Controller im Prozessor ----------------------- */

#define MSR_APIC_BASE    0x1B
#define APIC_ENABLE      (1u << 11)
#define APIC_X2APIC      (1u << 10)

#define LAPIC_TPR        0x080
#define LAPIC_EOI        0x0B0
#define LAPIC_SPURIOUS   0x0F0
#define LAPIC_IN_SERVICE 0x100          /* acht Register, je 32 Vektoren */
#define LAPIC_LVT_THERMAL 0x330
#define LAPIC_LVT_PERF   0x340
#define LAPIC_LVT_LINT0  0x350
#define LAPIC_LVT_ERROR  0x370
#define LAPIC_LVT_TIMER  0x320
#define LAPIC_TIMER_INIT 0x380
#define LAPIC_TIMER_CUR  0x390
#define LAPIC_TIMER_DIV  0x3E0

#define TIMER_MASKED     (1u << 16)
#define TIMER_PERIODIC   (1u << 17)

static bool x2apic;                     /* Zugriff über MSRs statt Speicher */
static bool lapic_ready;
static volatile uint32_t *lapic;

static uint32_t lapic_read(uint32_t reg)
{
    if (x2apic)
        return (uint32_t)rdmsr(0x800 + (reg >> 4));
    return lapic[reg / 4];
}

static void lapic_write(uint32_t reg, uint32_t value)
{
    if (x2apic)
        wrmsr(0x800 + (reg >> 4), value);
    else
        lapic[reg / 4] = value;
}

void timer_start(void)
{
    uint64_t base = rdmsr(MSR_APIC_BASE);
    if (!(base & APIC_ENABLE)) {
        base |= APIC_ENABLE;
        wrmsr(MSR_APIC_BASE, base);
    }
    x2apic = base & APIC_X2APIC;
    if (!x2apic) {
        uint64_t address = base & 0x000FFFFFFFFFF000ull;
        vmm_map(address, 4096);
        lapic = (volatile uint32_t *)address;
    }

    lapic_write(LAPIC_TPR, 0);
    lapic_write(LAPIC_SPURIOUS, 0x100 | VECTOR_SPURIOUS);  /* APIC einschalten */

    /* Interrupt-Quellen der Firmware abklemmen - wir brauchen nur den Timer. */
    lapic_write(LAPIC_LVT_LINT0, TIMER_MASKED);
    lapic_write(LAPIC_LVT_ERROR, TIMER_MASKED);
    lapic_write(LAPIC_LVT_THERMAL, TIMER_MASKED);
    lapic_write(LAPIC_LVT_PERF, TIMER_MASKED);
    lapic_ready = true;

    /*
     * Der Timer zählt mit einer unbekannten Frequenz herunter. Wir lassen ihn
     * 10 ms laufen (gemessen am Zeitstempelzähler) und wissen dann, wie viele
     * Schritte auf eine Millisekunde kommen.
     */
    lapic_write(LAPIC_TIMER_DIV, 0x3);  /* Teiler 16 */
    lapic_write(LAPIC_LVT_TIMER, TIMER_MASKED | VECTOR_TIMER);
    lapic_write(LAPIC_TIMER_INIT, 0xFFFFFFFF);
    time_delay_ms(10);
    uint32_t per_ms = (0xFFFFFFFF - lapic_read(LAPIC_TIMER_CUR)) / 10;
    if (per_ms == 0)
        per_ms = 1;

    lapic_write(LAPIC_LVT_TIMER, TIMER_PERIODIC | VECTOR_TIMER);
    lapic_write(LAPIC_TIMER_INIT, per_ms * (1000 / TICK_HZ));
}

/* --- Einstieg für alle Interrupts --------------------------------------------- */

void interrupt_handler(regs_t *r)
{
    if (r->int_no < 32)
        exception(r);

    if (r->int_no != VECTOR_TIMER) {
        /*
         * Ein Interrupt, den niemand bestellt hat (z. B. noch von der
         * Firmware eingerichtet): quittieren, falls nötig, und ignorieren.
         */
        uint32_t vector = (uint32_t)r->int_no;
        if (lapic_ready &&
            (lapic_read(LAPIC_IN_SERVICE + 0x10 * (vector / 32)) &
             (1u << (vector % 32))))
            lapic_write(LAPIC_EOI, 0);
        return;
    }

    /*
     * Quittung vor der Arbeit senden: task_tick() kann den Task wechseln und
     * kehrt dann erst viel später hierher zurück.
     */
    lapic_write(LAPIC_EOI, 0);

    /* Tastatur, Maus und serielle Schnittstelle werden im Takt abgefragt. */
    ps2_poll();
    serial_poll();
    task_tick();
}

/* --- Initialisierung ------------------------------------------------------------ */

/*
 * Der alte Interrupt-Controller (8259 PIC) wird nicht benutzt. Damit seine
 * Störimpulse nicht wie CPU-Ausnahmen aussehen, legen wir ihn auf die
 * Vektoren 32-47 und sperren alle Leitungen.
 */
static void pic_disable(void)
{
    outb(0x20, 0x11);
    io_wait();
    outb(0xA0, 0x11);
    io_wait();
    outb(0x21, 0x20);
    io_wait();
    outb(0xA1, 0x28);
    io_wait();
    outb(0x21, 0x04);
    io_wait();
    outb(0xA1, 0x02);
    io_wait();
    outb(0x21, 0x01);
    io_wait();
    outb(0xA1, 0x01);
    io_wait();
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}

void interrupts_init(void)
{
    struct table_pointer gdtr = { sizeof(gdt) - 1, (uint64_t)gdt };
    gdt_load(&gdtr);

    for (int i = 0; i < 256; i++) {
        idt[i].offset_low = isr_table[i] & 0xFFFF;
        idt[i].selector = 0x08;
        idt[i].ist = 0;
        idt[i].flags = 0x8E;            /* vorhanden, Ring 0, Interrupt-Gate */
        idt[i].offset_mid = (isr_table[i] >> 16) & 0xFFFF;
        idt[i].offset_high = (uint32_t)(isr_table[i] >> 32);
        idt[i].reserved = 0;
    }
    struct table_pointer idtr = { sizeof(idt) - 1, (uint64_t)idt };
    __asm__ volatile("lidt %0" : : "m"(idtr));

    pic_disable();
}
