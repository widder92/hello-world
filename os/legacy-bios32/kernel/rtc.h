/* Echtzeituhr (CMOS) */
#pragma once

#include <stdint.h>

typedef struct {
    uint16_t year;
    uint8_t month, day;
    uint8_t hour, minute, second;
} datetime_t;

void rtc_read(datetime_t *out);
