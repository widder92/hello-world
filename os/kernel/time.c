/*
 * Zeitmessung über den Zeitstempelzähler (TSC) des Prozessors. Dessen
 * Frequenz hat der Startcode mit Hilfe der Firmware gemessen. Die Uhrzeit
 * stammt ebenfalls von der Firmware (einmalig beim Start) und wird von da an
 * selbst weitergezählt.
 */
#include "time.h"
#include "bootinfo.h"
#include "cpu.h"

static uint64_t tsc_start;
static uint64_t tsc_per_ms = 1;
static uint64_t boot_seconds;           /* Uhrzeit beim Start, ab 1.1.1970 */

/* Tage seit dem 1.1.1970 für ein Datum (gregorianischer Kalender). */
static int64_t days_from_date(int64_t y, unsigned m, unsigned d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static void date_from_days(int64_t z, datetime_t *out)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    out->day = (uint8_t)(doy - (153 * mp + 2) / 5 + 1);
    out->month = (uint8_t)(mp < 10 ? mp + 3 : mp - 9);
    out->year = (uint16_t)(yoe + era * 400 + (out->month <= 2));
}

void time_init(void)
{
    tsc_start = rdtsc();
    tsc_per_ms = boot_info.tsc_hz / 1000;
    if (tsc_per_ms < 1000)              /* unplausibel: 1 GHz annehmen */
        tsc_per_ms = 1000000;

    const efi_time_t *t = &boot_info.boot_time;
    if (t->year >= 1970 && t->month >= 1 && t->month <= 12 && t->day >= 1) {
        boot_seconds = (uint64_t)days_from_date(t->year, t->month, t->day) *
                       86400 + t->hour * 3600 + t->minute * 60 + t->second;
    }
}

uint64_t time_ms(void)
{
    return (rdtsc() - tsc_start) / tsc_per_ms;
}

uint64_t time_us(void)
{
    return (rdtsc() - tsc_start) * 1000 / tsc_per_ms;
}

uint64_t time_tsc_to_ms(uint64_t tsc)
{
    return tsc / tsc_per_ms;
}

void time_delay_us(uint32_t us)
{
    uint64_t end = rdtsc() + (uint64_t)us * tsc_per_ms / 1000;
    while (rdtsc() < end)
        cpu_pause();
}

void time_delay_ms(uint32_t ms)
{
    time_delay_us(ms * 1000);
}

void time_now(datetime_t *out)
{
    uint64_t seconds = boot_seconds + time_ms() / 1000;
    date_from_days((int64_t)(seconds / 86400), out);
    uint32_t rest = (uint32_t)(seconds % 86400);
    out->hour = (uint8_t)(rest / 3600);
    out->minute = (uint8_t)(rest / 60 % 60);
    out->second = (uint8_t)(rest % 60);
}
