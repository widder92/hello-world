#include "rtc.h"
#include "io.h"
#include "string.h"

static uint8_t cmos_read(uint8_t reg)
{
    outb(0x70, reg | 0x80);             /* Bit 7: NMI gesperrt lassen */
    return inb(0x71);
}

static uint8_t from_bcd(uint8_t v)
{
    return (uint8_t)((v >> 4) * 10 + (v & 0x0F));
}

static void read_raw(uint8_t raw[6])
{
    for (int i = 0; i < 10000 && (cmos_read(0x0A) & 0x80); i++)
        ;                               /* laufende Aktualisierung abwarten */
    raw[0] = cmos_read(0x00);           /* Sekunde */
    raw[1] = cmos_read(0x02);           /* Minute */
    raw[2] = cmos_read(0x04);           /* Stunde */
    raw[3] = cmos_read(0x07);           /* Tag */
    raw[4] = cmos_read(0x08);           /* Monat */
    raw[5] = cmos_read(0x09);           /* Jahr (zweistellig) */
}

void rtc_read(datetime_t *out)
{
    uint8_t a[6], b[6];

    uint32_t flags = irq_save();
    /* Zweimal lesen, bis beide Durchgänge gleich sind (keine halben Werte) */
    read_raw(a);
    for (int i = 0; i < 5; i++) {
        read_raw(b);
        if (memcmp(a, b, sizeof(a)) == 0)
            break;
        memcpy(a, b, sizeof(a));
    }
    uint8_t status_b = cmos_read(0x0B);
    irq_restore(flags);

    bool pm = a[2] & 0x80;
    a[2] &= 0x7F;
    if (!(status_b & 0x04)) {           /* Werte liegen als BCD vor */
        for (int i = 0; i < 6; i++)
            a[i] = from_bcd(a[i]);
    }
    if (!(status_b & 0x02)) {           /* 12-Stunden-Format */
        a[2] %= 12;
        if (pm)
            a[2] += 12;
    }

    out->second = a[0];
    out->minute = a[1];
    out->hour = a[2];
    out->day = a[3];
    out->month = a[4];
    out->year = (uint16_t)(2000 + a[5]);
}
