#include "console.h"
#include "cpu.h"
#include "memory.h"
#include "serial.h"
#include "string.h"
#include "task.h"

/* --- Konsole des laufenden Tasks -------------------------------------------- */

static console_t *current_console(void)
{
    console_t *con = task_console();
    return con ? con : serial_console();
}

void console_ref(console_t *con)
{
    if (!con)
        return;
    uint64_t flags = irq_save();
    con->refs++;
    irq_restore(flags);
}

void console_unref(console_t *con)
{
    if (!con)
        return;
    uint64_t flags = irq_save();
    bool last = --con->refs == 0;
    irq_restore(flags);
    if (last && con->release)
        con->release(con);
}

void console_putc(char c)
{
    console_t *con = current_console();
    con->putc(con, c);
}

void console_write(const char *data, size_t len)
{
    console_t *con = current_console();
    while (len--)
        con->putc(con, *data++);
}

void console_set_color(console_color_t color)
{
    console_t *con = current_console();
    if (con->set_color)
        con->set_color(con, color);
}

void console_clear(void)
{
    console_t *con = current_console();
    if (con->clear)
        con->clear(con);
}

int console_getchar(void)
{
    console_t *con = current_console();
    return con->getchar(con);
}

/* --- Formatierte Ausgabe ----------------------------------------------------- */

typedef void (*out_fn)(char c, void *ctx);

static void pad(out_fn out, void *ctx, int count, char ch)
{
    while (count-- > 0)
        out(ch, ctx);
}

static void format_number(out_fn out, void *ctx, uint64_t value, unsigned base,
                          bool negative, bool upper, int width, bool zero,
                          bool left)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char buf[24];
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

        bool wide = false;              /* 64-Bit-Argument */
        if (*fmt == 'z') {
            wide = true;
            fmt++;
        } else if (fmt[0] == 'l' && fmt[1] == 'l') {
            wide = true;
            fmt += 2;
        } else if (*fmt == 'l') {
            fmt++;                      /* long ist hier 32 Bit */
        }

        switch (*fmt) {
        case 'd':
        case 'i': {
            int64_t v = wide ? va_arg(ap, int64_t) : va_arg(ap, int32_t);
            uint64_t mag = v < 0 ? 0 - (uint64_t)v : (uint64_t)v;
            format_number(out, ctx, mag, 10, v < 0, false, width, zero, left);
            break;
        }
        case 'u': {
            uint64_t v = wide ? va_arg(ap, uint64_t) : va_arg(ap, uint32_t);
            format_number(out, ctx, v, 10, false, false, width, zero, left);
            break;
        }
        case 'x':
        case 'X': {
            uint64_t v = wide ? va_arg(ap, uint64_t) : va_arg(ap, uint32_t);
            format_number(out, ctx, v, 16, false, *fmt == 'X', width, zero,
                          left);
            break;
        }
        case 'p':
            out('0', ctx);
            out('x', ctx);
            format_number(out, ctx, (uint64_t)va_arg(ap, void *), 16, false,
                          false, 16, true, false);
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
    console_t *con = ctx;
    con->putc(con, c);
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    format(out_console, current_console(), fmt, ap);
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

int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    struct buffer b = { buf, size, 0 };
    format(out_buffer, &b, fmt, ap);
    if (size > 0)
        buf[b.len < size ? b.len : size - 1] = '\0';
    return (int)b.len;
}

int ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int len = kvsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return len;
}

/* --- Kernel-Meldungen -------------------------------------------------------- */

static void (*screen_write)(const char *text);

void klog_set_screen(void (*write)(const char *text))
{
    screen_write = write;
}

void klog(const char *fmt, ...)
{
    char line[160];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    serial_write(line);
    if (screen_write)
        screen_write(line);
}
