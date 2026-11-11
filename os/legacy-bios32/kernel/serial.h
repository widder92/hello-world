/* Serielle Schnittstelle COM1: Spiegel der Konsole und zweiter Eingabeweg */
#pragma once

void serial_init(void);
void serial_putc(char c);

/* Empfangs-Interrupt einschalten (erst nach idt_init() aufrufen). */
void serial_enable_input(void);
