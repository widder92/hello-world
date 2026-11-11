/* Programmable Interval Timer: Systemtakt und Zeitscheiben */
#pragma once

#include <stdint.h>

#define TIMER_HZ 100                    /* ein Tick = 10 ms */

void timer_init(void);
uint32_t timer_ticks(void);
