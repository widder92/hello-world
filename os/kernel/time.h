/* Zeit: Laufzeit seit dem Start und Datum/Uhrzeit */
#pragma once

#include <stdint.h>

typedef struct {
    uint16_t year;
    uint8_t month, day;
    uint8_t hour, minute, second;
} datetime_t;

void time_init(void);

/* Millisekunden bzw. Mikrosekunden seit dem Start des Kernels. */
uint64_t time_ms(void);
uint64_t time_us(void);

/* Aktives Warten - nur für sehr kurze Pausen bei der Hardware-Einrichtung. */
void time_delay_ms(uint32_t ms);
void time_delay_us(uint32_t us);

/* Rechnet TSC-Einheiten (für die CPU-Zeit der Tasks) in Millisekunden um. */
uint64_t time_tsc_to_ms(uint64_t tsc);

void time_now(datetime_t *out);
