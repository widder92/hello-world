#include "console.h"
#include "io.h"
#include "serial.h"
#include "string.h"
#include "vga.h"

#include <stdbool.h>
#include <stdint.h>

void console_putc(char c)
{
    /* Mehrere Tasks schreiben auf die Konsole - kurz Interrupts sperren. */
    uint32_t flags = irq_save();
    vga_putc(c);
    if (c == '\b') {
        serial_putc('\b');
        serial_putc(' ');
    }
    serial_putc(c);
    irq_restore(flags);
}

void console_write(const char *data, size_t len)
{
    while (len--)
        console_putc(*data++);
}

/* --- Formatierte Ausgabe --------------------------------------------------- */

typedef void (*out_fn)(char c, void *ctx);

static void pad(out_fn out, void *ctx, int count, char ch)
{
    while (count-- > 0)
        out(ch, ctx);
}

static void format_number(out_fn out, void *ctx, uint32_t value, unsigned base,
                          bool negative, bool upper, int width, bool zero,
                          bool left)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char buf[12];
    int len = 0;

    do {
        buf[len++] = digits[value % base];
        value /= base;
    } while (value);

    int fill = width - len - (negative ? 1 : 0);
    if (!left && !zero)
        pad(out, ctx, fill, ' ');
    if (negative)
        out('-', ctx);
    if (!left && zero)
        pad(out, ctx, fill, '0');
    while (len--)
        out(buf[len], ctx);
    if (left)
        pad(out, ctx, fill, ' ');
}

static void format(out_fn out, void *ctx, const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            out(*fmt, ctx);
            continue;
        }
        fmt++;

        bool left = false, zero = false;
        for (;; fmt++) {
            if (*fmt == '-')
                left = true;
            else if (*fmt == '0')
                zero = true;
            else
                break;
        }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l')             /* long ist hier ebenfalls 32 Bit */
            fmt++;

        switch (*fmt) {
        case 'd':
        case 'i': {
            int32_t v = va_arg(ap, int32_t);
            uint32_t mag = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
            format_number(out, ctx, mag, 10, v < 0, false, width, zero, left);
            break;
        }
        case 'u':
            format_number(out, ctx, va_arg(ap, uint32_t), 10, false, false,
                          width, zero, left);
            break;
        case 'x':
        case 'X':
            format_number(out, ctx, va_arg(ap, uint32_t), 16, false,
                          *fmt == 'X', width, zero, left);
            break;
        case 'p':
            out('0', ctx);
            out('x', ctx);
            format_number(out, ctx, (uint32_t)va_arg(ap, void *), 16, false,
                          false, 8, true, false);
            break;
        case 'c':
            out((char)va_arg(ap, int), ctx);
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            int fill = width - (int)strlen(s);
            if (!left)
                pad(out, ctx, fill, ' ');
            while (*s)
                out(*s++, ctx);
            if (left)
                pad(out, ctx, fill, ' ');
            break;
        }
        case '%':
            out('%', ctx);
            break;
        case '\0':
            return;
        default:
            out('%', ctx);
            out(*fmt, ctx);
            break;
        }
    }
}

static void out_console(char c, void *ctx)
{
    (void)ctx;
    console_putc(c);
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    format(out_console, NULL, fmt, ap);
    va_end(ap);
}

struct buffer {
    char *data;
    size_t size;
    size_t len;
};

static void out_buffer(char c, void *ctx)
{
    struct buffer *b = ctx;
    if (b->len + 1 < b->size)
        b->data[b->len] = c;
    b->len++;
}

int ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
    struct buffer b = { buf, size, 0 };
    va_list ap;
    va_start(ap, fmt);
    format(out_buffer, &b, fmt, ap);
    va_end(ap);
    if (size > 0)
        buf[b.len < size ? b.len : size - 1] = '\0';
    return (int)b.len;
}
