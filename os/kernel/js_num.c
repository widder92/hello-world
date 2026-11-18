/*
 * JavaScript: Zahlen. Umwandlung zwischen Kommazahl und Text - exakt, wie die
 * Sprache es verlangt (die kürzeste Ziffernfolge, die wieder dieselbe Zahl
 * ergibt) - und die mathematischen Funktionen, die es im Kernel sonst nicht
 * gibt.
 */
#include "js_impl.h"
#include "string.h"

#ifdef _WIN32
/* Der Übersetzer verlangt dieses Symbol, sobald irgendwo mit Kommazahlen gerechnet wird. */
int _fltused = 1;
#endif

typedef union {
    double d;
    uint64_t u;
} bits_t;

bool js_isnan(double x) { return x != x; }
double js_nan(void) { bits_t b; b.u = 0x7FF8000000000000ull; return b.d; }
double js_infinity(void) { bits_t b; b.u = 0x7FF0000000000000ull; return b.d; }
bool js_isfinite(double x) { bits_t b; b.d = x; return ((b.u >> 52) & 0x7FF) != 0x7FF; }

/* --- Exakte Dezimalziffern einer Zahl mant * 2^exp ------------------------------------------ */

#define LIMBS 40

typedef struct {
    char whole[330];                    /* Ziffern vor dem Komma */
    int whole_count, whole_at;
    uint32_t frac[LIMBS];               /* Rest hinter dem Komma: frac / 2^shift */
    int limbs, shift;
    int pos;                            /* Wert = 0,z1z2z3... * 10^pos (z1 ist nicht 0) */
    int pending;                        /* schon erzeugte erste Ziffer hinter führenden Nullen */
} decimal_t;

static bool frac_zero(const decimal_t *g)
{
    for (int i = 0; i < g->limbs; i++) {
        if (g->frac[i])
            return false;
    }
    return true;
}

static int frac_digit(decimal_t *g)
{
    if (g->limbs == 0)
        return 0;
    uint64_t carry = 0;
    for (int i = 0; i < g->limbs; i++) {
        uint64_t v = (uint64_t)g->frac[i] * 10 + carry;
        g->frac[i] = (uint32_t)v;
        carry = v >> 32;
    }
    int r = g->shift % 32;
    if (r == 0)
        return (int)carry;
    uint32_t top = g->frac[g->limbs - 1];
    int digit = (int)((carry << (32 - r)) | (top >> r));
    g->frac[g->limbs - 1] = top & ((1u << r) - 1);
    return digit;
}

static void decimal_init(decimal_t *g, uint64_t mant, int exp)
{
    memset(g, 0, sizeof(*g));
    g->pending = -1;
    if (exp >= 0) {
        /* ganze Zahl: mant << exp, dann durch wiederholtes Teilen in Ziffern */
        uint32_t big[LIMBS] = { 0 };
        int word = exp / 32, bit = exp % 32, limbs = word + 3;
        uint64_t lo = mant << bit;
        uint64_t hi = bit ? mant >> (64 - bit) : 0;
        if (limbs > LIMBS)
            limbs = LIMBS;
        if (word + 2 < LIMBS) {
            big[word] = (uint32_t)lo;
            big[word + 1] = (uint32_t)(lo >> 32);
            big[word + 2] = (uint32_t)hi;
        }
        char reversed[330];
        int n = 0;
        for (;;) {
            while (limbs > 0 && big[limbs - 1] == 0)
                limbs--;
            if (limbs == 0)
                break;
            uint64_t rem = 0;
            for (int i = limbs - 1; i >= 0; i--) {
                uint64_t v = (rem << 32) | big[i];
                big[i] = (uint32_t)(v / 1000000000u);
                rem = v % 1000000000u;
            }
            for (int k = 0; k < 9 && n < 329; k++) {
                reversed[n++] = (char)(rem % 10);
                rem /= 10;
            }
        }
        while (n > 0 && reversed[n - 1] == 0)
            n--;
        for (int i = 0; i < n; i++)
            g->whole[i] = reversed[n - 1 - i];
        g->whole_count = n;
    } else {
        int s = -exp;
        uint64_t whole = s < 64 ? mant >> s : 0;
        uint64_t rest = s < 64 ? mant & ((1ull << s) - 1) : mant;
        g->shift = s;
        g->limbs = (s + 31) / 32;
        if (g->limbs > LIMBS)
            g->limbs = LIMBS;
        g->frac[0] = (uint32_t)rest;
        if (g->limbs > 1)
            g->frac[1] = (uint32_t)(rest >> 32);
        char reversed[24];
        int n = 0;
        while (whole) {
            reversed[n++] = (char)(whole % 10);
            whole /= 10;
        }
        for (int i = 0; i < n; i++)
            g->whole[i] = reversed[n - 1 - i];
        g->whole_count = n;
    }
    g->pos = g->whole_count;
    if (g->whole_count == 0) {
        /* führende Nullen hinter dem Komma überspringen */
        int digit = 0;
        while (!frac_zero(g) && (digit = frac_digit(g)) == 0)
            g->pos--;
        g->pending = digit;
    }
}

static int decimal_next(decimal_t *g)
{
    if (g->whole_at < g->whole_count)
        return g->whole[g->whole_at++];
    if (g->pending >= 0) {
        int digit = g->pending;
        g->pending = -1;
        return digit;
    }
    return frac_digit(g);
}

static bool decimal_done(const decimal_t *g)
{
    if (g->pending > 0)
        return false;
    for (int i = g->whole_at; i < g->whole_count; i++) {
        if (g->whole[i])
            return false;
    }
    return frac_zero(g);
}

/* Vergleicht 0,d1d2...dn * 10^pos (plus "sticky": dahinter kommt noch etwas) mit mant * 2^exp */
static int decimal_compare(const char *digits, int count, int pos, bool sticky, uint64_t mant,
                           int exp)
{
    decimal_t g;
    decimal_init(&g, mant, exp);
    if (pos != g.pos)
        return pos > g.pos ? 1 : -1;
    for (int i = 0; i < 1200; i++) {
        if (i >= count && decimal_done(&g))
            return sticky ? 1 : 0;
        int theirs = decimal_next(&g);
        int ours = i < count ? digits[i] : 0;
        if (ours != theirs)
            return ours > theirs ? 1 : -1;
    }
    return sticky ? 1 : 0;
}

static void split(double value, uint64_t *mant, int *exp)
{
    bits_t b;
    b.d = value;
    uint64_t frac = b.u & 0xFFFFFFFFFFFFFull;
    int e = (int)((b.u >> 52) & 0x7FF);
    if (e == 0) {
        *mant = frac;
        *exp = -1074;
    } else {
        *mant = frac | (1ull << 52);
        *exp = e - 1075;
    }
}

/* Die Grenzen, zwischen denen alles zu value gerundet wird */
static void neighbours(double value, uint64_t *low_mant, int *low_exp, uint64_t *high_mant,
                       int *high_exp)
{
    uint64_t mant;
    int exp;
    split(value, &mant, &exp);
    *high_mant = 2 * mant + 1;
    *high_exp = exp - 1;
    if (mant == (1ull << 52) && exp > -1074) {
        *low_mant = 4 * mant - 1;
        *low_exp = exp - 2;
    } else {
        *low_mant = 2 * mant - 1;
        *low_exp = exp - 1;
    }
}

/*
 * Rundet die exakten Ziffern von value auf "want" geltende Ziffern
 * (kaufmännisch). Liefert die Anzahl und in *pos die Stelle des Kommas.
 */
static int round_digits(double value, int want, char *digits, int *pos)
{
    uint64_t mant;
    int exp;
    decimal_t g;
    split(value, &mant, &exp);
    decimal_init(&g, mant, exp);
    *pos = g.pos;
    for (int i = 0; i < want; i++)
        digits[i] = (char)decimal_next(&g);
    if (decimal_next(&g) >= 5) {
        int i = want - 1;
        while (i >= 0 && digits[i] == 9)
            digits[i--] = 0;
        if (i >= 0) {
            digits[i]++;
        } else {
            digits[0] = 1;              /* 9,99 -> 10,0 */
            (*pos)++;
        }
    }
    return want;
}

/* Die kürzesten Ziffern, die beim Zurücklesen wieder genau value ergeben (value > 0, endlich) */
static int shortest_digits(double value, char *digits, int *pos)
{
    uint64_t mant, low_mant, high_mant;
    int exp, low_exp, high_exp;
    split(value, &mant, &exp);
    neighbours(value, &low_mant, &low_exp, &high_mant, &high_exp);
    bool even = (mant & 1) == 0;
    for (int want = 1; want < 17; want++) {
        round_digits(value, want, digits, pos);
        int low = decimal_compare(digits, want, *pos, false, low_mant, low_exp);
        int high = decimal_compare(digits, want, *pos, false, high_mant, high_exp);
        if ((low > 0 || (low == 0 && even)) && (high < 0 || (high == 0 && even))) {
            int n = want;
            while (n > 1 && digits[n - 1] == 0)
                n--;
            return n;
        }
    }
    int n = round_digits(value, 17, digits, pos);
    while (n > 1 && digits[n - 1] == 0)
        n--;
    return n;
}

static int put_exponent(char *out, int exponent)
{
    int n = 0;
    out[n++] = 'e';
    out[n++] = exponent < 0 ? '-' : '+';
    if (exponent < 0)
        exponent = -exponent;
    char tmp[6];
    int k = 0;
    do {
        tmp[k++] = (char)('0' + exponent % 10);
        exponent /= 10;
    } while (exponent);
    while (k > 0)
        out[n++] = tmp[--k];
    return n;
}

int js_number_to_text(double value, char *out)
{
    int n = 0;
    if (js_isnan(value)) {
        memcpy(out, "NaN", 4);
        return 3;
    }
    if (value == 0) {
        out[0] = '0';
        out[1] = '\0';
        return 1;
    }
    if (value < 0) {
        out[n++] = '-';
        value = -value;
    }
    if (!js_isfinite(value)) {
        memcpy(out + n, "Infinity", 9);
        return n + 8;
    }
    /* ganze Zahlen, wie sie meistens vorkommen */
    if (value < 9007199254740992.0 && value == (double)(uint64_t)value) {
        uint64_t whole = (uint64_t)value;
        char tmp[20];
        int k = 0;
        do {
            tmp[k++] = (char)('0' + whole % 10);
            whole /= 10;
        } while (whole);
        while (k > 0)
            out[n++] = tmp[--k];
        out[n] = '\0';
        return n;
    }
    char digits[20];
    int pos;
    int count = shortest_digits(value, digits, &pos);
    if (count <= pos && pos <= 21) {
        for (int i = 0; i < pos; i++)
            out[n++] = (char)('0' + (i < count ? digits[i] : 0));
    } else if (pos > 0 && pos <= 21) {
        for (int i = 0; i < count; i++) {
            if (i == pos)
                out[n++] = '.';
            out[n++] = (char)('0' + digits[i]);
        }
    } else if (pos > -6 && pos <= 0) {
        out[n++] = '0';
        out[n++] = '.';
        for (int i = 0; i < -pos; i++)
            out[n++] = '0';
        for (int i = 0; i < count; i++)
            out[n++] = (char)('0' + digits[i]);
    } else {
        out[n++] = (char)('0' + digits[0]);
        if (count > 1) {
            out[n++] = '.';
            for (int i = 1; i < count; i++)
                out[n++] = (char)('0' + digits[i]);
        }
        n += put_exponent(out + n, pos - 1);
    }
    out[n] = '\0';
    return n;
}

int js_number_fixed(double value, int places, char *out, int size)
{
    if (js_isnan(value) || !js_isfinite(value) || value >= 1e21 || value <= -1e21 || size < 130)
        return js_number_to_text(value, out);
    int n = 0;
    bool negative = value < 0;
    if (negative)
        value = -value;
    char digits[130];
    int count = 0, whole = 1;
    if (value == 0) {
        memset(digits, 0, sizeof(digits));
        count = 1 + places;
    } else {
        uint64_t mant;
        int exp;
        decimal_t g;
        split(value, &mant, &exp);
        decimal_init(&g, mant, exp);
        /* Stellen vor dem Komma: g.pos (bei Werten unter 1 null oder weniger) */
        whole = g.pos > 0 ? g.pos : 1;
        int lead = g.pos > 0 ? 0 : 1 - g.pos;      /* Nullen vor der ersten Ziffer */
        count = whole + places;
        if (count > 128)
            count = 128;
        for (int i = 0; i < count; i++)
            digits[i] = i < lead ? 0 : (char)decimal_next(&g);
        int next = count < lead ? 0 : decimal_next(&g);
        if (next >= 5) {
            int i = count - 1;
            while (i >= 0 && digits[i] == 9)
                digits[i--] = 0;
            if (i >= 0) {
                digits[i]++;
            } else {
                memmove(digits + 1, digits, (size_t)count);
                digits[0] = 1;
                count++;
                whole++;
            }
        }
    }
    bool nonzero = false;
    for (int i = 0; i < count; i++)
        nonzero |= digits[i] != 0;
    if (negative && nonzero)
        out[n++] = '-';
    for (int i = 0; i < count && n < size - 2; i++) {
        if (i == whole)
            out[n++] = '.';
        out[n++] = (char)('0' + digits[i]);
    }
    out[n] = '\0';
    return n;
}

int js_number_precision(double value, int want, bool exponential, char *out, int size)
{
    if (js_isnan(value) || !js_isfinite(value) || size < 130 || want < 1 || want > 100)
        return js_number_to_text(value, out);
    int n = 0;
    if (value < 0) {
        out[n++] = '-';
        value = -value;
    }
    char digits[104];
    int pos = 1;
    if (value == 0)
        memset(digits, 0, sizeof(digits));
    else
        round_digits(value, want, digits, &pos);
    int e = pos - 1;
    if (exponential || e < -6 || e >= want) {
        out[n++] = (char)('0' + digits[0]);
        if (want > 1) {
            out[n++] = '.';
            for (int i = 1; i < want; i++)
                out[n++] = (char)('0' + digits[i]);
        }
        n += put_exponent(out + n, e);
    } else if (e >= 0) {
        for (int i = 0; i < want; i++) {
            if (i == e + 1)
                out[n++] = '.';
            out[n++] = (char)('0' + digits[i]);
        }
    } else {
        out[n++] = '0';
        out[n++] = '.';
        for (int i = 0; i < -e - 1; i++)
            out[n++] = '0';
        for (int i = 0; i < want; i++)
            out[n++] = (char)('0' + digits[i]);
    }
    out[n] = '\0';
    return n;
}

int js_number_to_radix(double value, int radix, char *out, int size)
{
    static const char symbols[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    if (radix == 10 || js_isnan(value) || !js_isfinite(value) || size < 1200)
        return js_number_to_text(value, out);
    int n = 0;
    if (value < 0) {
        out[n++] = '-';
        value = -value;
    }
    double whole = js_floor(value), frac = value - whole;
    char tmp[1100];
    int k = 0;
    do {
        double q = js_floor(whole / radix);
        int digit = (int)(whole - q * radix);
        tmp[k++] = symbols[digit < 0 ? 0 : digit >= radix ? radix - 1 : digit];
        whole = q;
    } while (whole > 0 && k < 1090);
    while (k > 0)
        out[n++] = tmp[--k];
    if (frac > 0) {
        out[n++] = '.';
        for (int i = 0; i < 52 && frac > 0; i++) {
            frac *= radix;
            int digit = (int)frac;
            out[n++] = symbols[digit];
            frac -= digit;
        }
    }
    out[n] = '\0';
    return n;
}

/* --- Text -> Zahl ---------------------------------------------------------------------------- */

static double next_up(double value)
{
    bits_t b;
    b.d = value;
    b.u++;
    return b.d;
}

static double next_down(double value)
{
    bits_t b;
    b.d = value;
    b.u--;
    return b.d;
}

/* 0,d1...dn * 10^pos als nächstgelegene Kommazahl (d1 ist nicht 0) */
static double digits_to_double(const char *digits, int count, int pos, bool sticky)
{
    static const double exact[] = { 1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
                                    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21,
                                    1e22 };
    if (pos > 310)
        return js_infinity();
    if (pos < -330)
        return 0;
    int used = count < 19 ? count : 19;
    uint64_t whole = 0;
    for (int i = 0; i < used; i++)
        whole = whole * 10 + (uint64_t)digits[i];
    int scale = pos - used;
    double value = (double)whole;
    if (count <= 15 && !sticky && scale >= -22 && scale <= 22)
        return scale >= 0 ? value * exact[scale] : value / exact[-scale];   /* genau gerundet */

    /* Näherung, danach mit exaktem Vergleich zurechtrücken */
    for (int s = scale; s > 0; s -= 22)
        value *= exact[s > 22 ? 22 : s];
    for (int s = -scale; s > 0; s -= 22)
        value /= exact[s > 22 ? 22 : s];
    if (!js_isfinite(value))
        value = 1.7976931348623157e308;
    for (int round = 0; round < 200; round++) {
        uint64_t mant, low_mant, high_mant;
        int exp, low_exp, high_exp;
        if (value == 0) {
            /* unter der kleinsten Zahl: liegt es über deren Hälfte? */
            int c = decimal_compare(digits, count, pos, sticky, 1, -1075);
            if (c <= 0)
                return 0;
            value = 4.9406564584124654e-324;
            continue;
        }
        split(value, &mant, &exp);
        neighbours(value, &low_mant, &low_exp, &high_mant, &high_exp);
        int high = decimal_compare(digits, count, pos, sticky, high_mant, high_exp);
        if (high > 0 || (high == 0 && (mant & 1))) {
            if (value >= 1.7976931348623157e308)
                return js_infinity();
            value = next_up(value);
            continue;
        }
        int low = decimal_compare(digits, count, pos, sticky, low_mant, low_exp);
        if (low < 0 || (low == 0 && (mant & 1))) {
            value = next_down(value);
            continue;
        }
        break;
    }
    return value;
}

double js_text_to_number(const char *text, size_t len, size_t *used)
{
    char digits[800];
    int count = 0, pos = 0;
    bool sticky = false, negative = false, any = false, seen_point = false;
    size_t i = 0;
    *used = 0;
    if (i < len && (text[i] == '+' || text[i] == '-'))
        negative = text[i++] == '-';
    for (; i < len; i++) {
        char c = text[i];
        if (c == '.' && !seen_point) {
            seen_point = true;
            continue;
        }
        if (c < '0' || c > '9')
            break;
        any = true;
        if (count == 0 && c == '0') {
            if (seen_point)
                pos--;                  /* führende Null hinter dem Komma */
            continue;
        }
        if (count < (int)sizeof(digits))
            digits[count++] = (char)(c - '0');
        else
            sticky |= c != '0';
        if (!seen_point)
            pos++;
    }
    if (!any)
        return js_nan();
    /* ein Punkt ohne folgende Ziffer gehört noch zur Zahl ("5.") */
    if (i < len && (text[i] == 'e' || text[i] == 'E')) {
        size_t k = i + 1;
        bool minus = false;
        if (k < len && (text[k] == '+' || text[k] == '-'))
            minus = text[k++] == '-';
        if (k < len && text[k] >= '0' && text[k] <= '9') {
            int e = 0;
            for (; k < len && text[k] >= '0' && text[k] <= '9'; k++) {
                if (e < 100000)
                    e = e * 10 + (text[k] - '0');
            }
            pos += minus ? -e : e;
            i = k;
        }
    }
    *used = i;
    while (count > 0 && digits[count - 1] == 0)
        count--;
    double value = count == 0 ? 0 : digits_to_double(digits, count, pos, sticky);
    return negative ? -value : value;
}

/* --- Mathematik ------------------------------------------------------------------------------ */

double js_trunc(double x)
{
    if (!(x > -4503599627370496.0 && x < 4503599627370496.0))
        return x;                       /* schon ganz - oder keine Zahl */
    double t = (double)(int64_t)x;
    if (t == 0 && x < 0)
        return -0.0;
    return t;
}

double js_floor(double x)
{
    double t = js_trunc(x);
    return t > x ? t - 1 : t;
}

double js_ceil(double x)
{
    double t = js_trunc(x);
    return t < x ? t + 1 : t;
}

double js_fmod(double x, double y)
{
    if (js_isnan(x) || js_isnan(y) || !js_isfinite(x) || y == 0)
        return js_nan();
    if (!js_isfinite(y) || x == 0)
        return x;
    double ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    for (int guard = 0; ax >= ay && guard < 3000; guard++) {
        double t = ay;
        while (t + t <= ax)
            t += t;
        ax -= t;
    }
    return x < 0 ? -ax : ax;
}

int32_t js_double_to_int32(double x)
{
    if (x >= -2147483648.0 && x <= 2147483647.0)
        return (int32_t)x;
    if (js_isnan(x) || !js_isfinite(x))
        return 0;
    double m = js_fmod(js_trunc(x), 4294967296.0);
    if (m < 0)
        m += 4294967296.0;
    return (int32_t)(uint32_t)(uint64_t)m;
}

static double scale2(double x, int k)
{
    /* x * 2^k, auch für sehr große und sehr kleine k */
    while (k > 1000) {
        x *= 8.98846567431158e307;      /* 2^1023 */
        k -= 1023;
    }
    while (k < -1000) {
        x *= 1.1125369292536007e-308;   /* 2^-1023 */
        k += 1023;
    }
    bits_t b;
    b.u = (uint64_t)(k + 1023) << 52;
    return x * b.d;
}

double js_exp(double x)
{
    if (js_isnan(x))
        return x;
    if (x > 709.8)
        return js_infinity();
    if (x < -745.2)
        return 0;
    const double ln2_hi = 6.93147180369123816490e-01, ln2_lo = 1.90821492927058770002e-10;
    int k = (int)(x * 1.4426950408889634 + (x < 0 ? -0.5 : 0.5));
    double r = (x - k * ln2_hi) - k * ln2_lo;
    /* e^r für |r| <= ln2/2 als Reihe */
    double term = 1, sum = 1;
    for (int i = 1; i < 18; i++) {
        term *= r / i;
        sum += term;
    }
    return scale2(sum, k);
}

double js_log(double x)
{
    if (js_isnan(x) || x < 0)
        return js_nan();
    if (x == 0)
        return -js_infinity();
    if (!js_isfinite(x))
        return x;
    bits_t b;
    int k = 0;
    b.d = x;
    if (((b.u >> 52) & 0x7FF) == 0) {   /* sehr kleine Zahl: erst hochschieben */
        b.d = x * 18014398509481984.0;  /* 2^54 */
        k = -54;
    }
    k += (int)((b.u >> 52) & 0x7FF) - 1023;
    b.u = (b.u & 0xFFFFFFFFFFFFFull) | (1023ull << 52);
    double m = b.d;                     /* in [1, 2) */
    if (m > 1.4142135623730951) {
        m *= 0.5;
        k++;
    }
    double s = (m - 1) / (m + 1), s2 = s * s, term = s, sum = 0;
    for (int i = 1; i < 30; i += 2) {
        sum += term / i;
        term *= s2;
    }
    return 2 * sum + k * 6.93147180369123816490e-01 + k * 1.90821492927058770002e-10;
}

double js_pow(double x, double y)
{
    if (js_isnan(y))
        return y;
    if (y == 0)
        return 1;
    if (js_isnan(x))
        return x;
    double ay = y < 0 ? -y : y, ax = x < 0 ? -x : x;
    bool y_whole = js_isfinite(y) && y == js_trunc(y);
    bool y_odd = y_whole && ay < 9007199254740992.0 && js_fmod(ay, 2) == 1;
    if (!js_isfinite(y)) {
        if (ax == 1)
            return js_nan();
        return (ax > 1) == (y > 0) ? js_infinity() : 0;
    }
    if (x == 0 || !js_isfinite(x)) {
        bool big = (x == 0) == (y < 0);
        double r = big ? js_infinity() : 0;
        return (x < 0 || (x == 0 && 1 / x < 0)) && y_odd ? -r : r;
    }
    if (y_whole && ay <= 1048576) {
        /* ganzzahliger Exponent: fortgesetztes Quadrieren, exakt wo es geht */
        double result = 1, base = x;
        uint32_t n = (uint32_t)ay;
        while (n) {
            if (n & 1)
                result *= base;
            base *= base;
            n >>= 1;
        }
        return y < 0 ? 1 / result : result;
    }
    if (x < 0) {
        if (!y_whole)
            return js_nan();
        double r = js_exp(y * js_log(-x));
        return y_odd ? -r : r;
    }
    if (y == 0.5)
        return __builtin_sqrt(x);
    return js_exp(y * js_log(x));
}

/* Bringt x in den Bereich [-pi/4, pi/4]; liefert das Viertel (0 bis 3) */
static int reduce(double x, double *r)
{
    const double pio2_1 = 1.57079632673412561417e+00, pio2_2 = 6.07710050650619224932e-11,
                 pio2_3 = 2.02226624879595063154e-21;
    double n = js_floor(x * 0.63661977236758134308 + 0.5);
    *r = ((x - n * pio2_1) - n * pio2_2) - n * pio2_3;
    double q = js_fmod(n, 4);
    if (q < 0)
        q += 4;
    return (int)q;
}

static double sin_poly(double r)
{
    double r2 = r * r, term = r, sum = r;
    for (int i = 3; i < 20; i += 2) {
        term *= -r2 / ((i - 1) * i);
        sum += term;
    }
    return sum;
}

static double cos_poly(double r)
{
    double r2 = r * r, term = 1, sum = 1;
    for (int i = 2; i < 20; i += 2) {
        term *= -r2 / ((i - 1) * i);
        sum += term;
    }
    return sum;
}

double js_sin(double x)
{
    if (js_isnan(x) || !js_isfinite(x))
        return js_nan();
    if (x > -1e-8 && x < 1e-8)
        return x;
    double r;
    switch (reduce(x, &r)) {
    case 0: return sin_poly(r);
    case 1: return cos_poly(r);
    case 2: return -sin_poly(r);
    default: return -cos_poly(r);
    }
}

double js_cos(double x)
{
    if (js_isnan(x) || !js_isfinite(x))
        return js_nan();
    double r;
    switch (reduce(x, &r)) {
    case 0: return cos_poly(r);
    case 1: return -sin_poly(r);
    case 2: return -cos_poly(r);
    default: return sin_poly(r);
    }
}

double js_tan(double x)
{
    double c = js_cos(x);
    return js_sin(x) / c;
}

double js_atan(double x)
{
    if (js_isnan(x))
        return x;
    bool negative = x < 0, inverse = false;
    double extra = 0;
    if (negative)
        x = -x;
    if (x > 1) {
        x = 1 / x;
        inverse = true;
    }
    if (x > 0.2679491924311227) {       /* tan(pi/12): um pi/6 verschieben */
        x = (x * 1.7320508075688772 - 1) / (x + 1.7320508075688772);
        extra = 0.5235987755982989;
    }
    double x2 = x * x, term = x, sum = 0;
    for (int i = 1; i < 30; i += 2) {
        sum += term / i;
        term *= -x2;
    }
    double r = extra + sum;
    if (inverse)
        r = 1.5707963267948966 - r;
    return negative ? -r : r;
}

double js_atan2(double y, double x)
{
    const double pi = 3.141592653589793;
    if (js_isnan(x) || js_isnan(y))
        return js_nan();
    if (x == 0 && y == 0)
        return (1 / x < 0) ? (1 / y < 0 ? -pi : pi) : y;
    if (!js_isfinite(x) && !js_isfinite(y)) {
        double r = x > 0 ? pi / 4 : 3 * pi / 4;
        return y < 0 ? -r : r;
    }
    if (x == 0 || !js_isfinite(y))
        return y < 0 ? -pi / 2 : pi / 2;
    double r = js_atan(y / x);
    if (x < 0)
        r += y < 0 || (y == 0 && 1 / y < 0) ? -pi : pi;
    return r;
}

double js_asin(double x)
{
    if (js_isnan(x) || x > 1 || x < -1)
        return js_nan();
    return js_atan2(x, __builtin_sqrt((1 - x) * (1 + x)));
}
