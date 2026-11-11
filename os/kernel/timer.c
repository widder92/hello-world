#include "timer.h"
#include "idt.h"
#include "io.h"
#include "task.h"

#define PIT_FREQUENCY 1193182

static volatile uint32_t ticks;

static void timer_irq(regs_t *regs)
{
    (void)regs;
    ticks++;
    task_tick(ticks);                   /* Schläfer wecken, Task wechseln */
}

uint32_t timer_ticks(void)
{
    return ticks;
}

void timer_init(void)
{
    uint16_t divisor = PIT_FREQUENCY / TIMER_HZ;
    outb(0x43, 0x36);                   /* Kanal 0, Rechtecksignal */
    outb(0x40, divisor & 0xFF);
    outb(0x40, divisor >> 8);
    irq_register(0, timer_irq);
}
