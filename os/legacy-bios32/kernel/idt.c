#include "idt.h"
#include "console.h"
#include "io.h"
#include "task.h"
#include "vga.h"

#include <stddef.h>

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t flags;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_pointer {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

extern const uint32_t isr_table[48];

static struct idt_entry idt[256];
static struct idt_pointer idtr;
static irq_handler_t irq_handlers[16];

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

/* Eine CPU-Ausnahme im Kernel ist nicht behebbar: melden und anhalten. */
static void panic(regs_t *r)
{
    irq_disable();
    vga_set_color(VGA_WHITE, VGA_RED);
    kprintf("\n*** KERNEL PANIC: %s (Ausnahme %u, Fehlercode 0x%x) ***\n",
            exception_names[r->int_no], r->int_no, r->err_code);
    kprintf("Task: %s (PID %u)\n", task_current_name(), task_current_pid());
    kprintf("EIP=%p CS=0x%x EFLAGS=%p\n", (void *)r->eip, r->cs,
            (void *)r->eflags);
    kprintf("EAX=%p EBX=%p ECX=%p EDX=%p\n", (void *)r->eax, (void *)r->ebx,
            (void *)r->ecx, (void *)r->edx);
    kprintf("ESI=%p EDI=%p EBP=%p ESP=%p\n", (void *)r->esi, (void *)r->edi,
            (void *)r->ebp, (void *)r->esp);
    kprintf("System angehalten.\n");
    for (;;)
        cpu_halt();
}

static uint8_t pic_in_service(uint16_t cmd_port)
{
    outb(cmd_port, 0x0B);               /* In-Service-Register lesen */
    return inb(cmd_port);
}

void isr_handler(regs_t *r)
{
    if (r->int_no < 32)
        panic(r);
    if (r->int_no >= 48)
        return;

    int irq = (int)r->int_no - 32;

    /* "Spurious" IRQs (Störimpulse) erkennen und ignorieren */
    if (irq == 7 && !(pic_in_service(PIC1_CMD) & 0x80))
        return;
    if (irq == 15 && !(pic_in_service(PIC2_CMD) & 0x80)) {
        outb(PIC1_CMD, PIC_EOI);
        return;
    }

    /*
     * Quittung (EOI) vor dem Handler senden: Der Timer-Handler kann den Task
     * wechseln und kehrt dann erst viel später hierher zurück.
     */
    if (irq >= 8)
        outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);

    if (irq_handlers[irq])
        irq_handlers[irq](r);
}

void irq_register(int irq, irq_handler_t handler)
{
    if (irq < 0 || irq >= 16)
        return;
    uint32_t flags = irq_save();
    irq_handlers[irq] = handler;
    if (irq < 8) {
        outb(PIC1_DATA, inb(PIC1_DATA) & ~(1 << irq));
    } else {
        outb(PIC2_DATA, inb(PIC2_DATA) & ~(1 << (irq - 8)));
        outb(PIC1_DATA, inb(PIC1_DATA) & ~(1 << 2));    /* Kaskade */
    }
    irq_restore(flags);
}

/* Die IRQs liegen ab Werk auf den Vektoren der CPU-Ausnahmen: nach 32-47. */
static void pic_remap(void)
{
    outb(PIC1_CMD, 0x11);
    io_wait();
    outb(PIC2_CMD, 0x11);
    io_wait();
    outb(PIC1_DATA, 0x20);              /* Master: Vektoren 32-39 */
    io_wait();
    outb(PIC2_DATA, 0x28);              /* Slave: Vektoren 40-47 */
    io_wait();
    outb(PIC1_DATA, 0x04);              /* Slave hängt an IRQ 2 */
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();
    outb(PIC1_DATA, 0x01);              /* 8086-Modus */
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();
    outb(PIC1_DATA, 0xFF);              /* zunächst alles maskieren */
    outb(PIC2_DATA, 0xFF);
}

void idt_init(void)
{
    for (int i = 0; i < 48; i++) {
        idt[i].offset_low = isr_table[i] & 0xFFFF;
        idt[i].selector = 0x08;
        idt[i].zero = 0;
        idt[i].flags = 0x8E;            /* vorhanden, Ring 0, Interrupt-Gate */
        idt[i].offset_high = isr_table[i] >> 16;
    }

    pic_remap();

    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint32_t)idt;
    __asm__ volatile("lidt %0" : : "m"(idtr));
}
