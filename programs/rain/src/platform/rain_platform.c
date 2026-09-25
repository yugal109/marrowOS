// The few C library functions Rain needs, written for MarrowOS. rain_platform.h points Rain's
// calls at these, and the macros are removed here so the MarrowOS versions can be used.

#undef printf
#undef fprintf
#undef fputs
#undef vfprintf
#undef snprintf
#undef vsnprintf
#undef strlen
#undef strcmp
#undef memcpy
#undef memmove
#undef memset
#undef memcmp
#undef strtod
#undef floor
#undef qsort
#undef malloc
#undef realloc
#undef free
#undef exit

#include "rain_platform.h"
#include "stdlib.h"
#include "marrowos.h"
#include <stdbool.h>
#include <stdint.h>

// ---- output ----

static rain_write_fn rain_writer = NULL;

void rain_set_writer(rain_write_fn writer)
{
    rain_writer = writer;
}

static void write_out(const char *text, int length, int is_error)
{
    if (rain_writer && length > 0)
    {
        rain_writer(text, length, is_error);
    }
}

// ---- formatting ----

typedef void (*emit_fn)(void *context, const char *text, int length);

struct buffer_sink
{
    char *buffer;
    size_t size;
    size_t length;
};

static void buffer_emit(void *context, const char *text, int length)
{
    struct buffer_sink *sink = context;
    for (int i = 0; i < length; i++)
    {
        if (sink->size > 0 && sink->length + 1 < sink->size)
        {
            sink->buffer[sink->length] = text[i];
        }
        sink->length++;
    }
}

struct writer_sink
{
    int is_error;
};

static void writer_emit(void *context, const char *text, int length)
{
    struct writer_sink *sink = context;
    write_out(text, length, sink->is_error);
}

// a * b as a rounded product plus the sign of what rounding lost (Dekker's exact product)
static double two_product(double a, double b, int *lost_sign)
{
    double product = a * b;
    double split_a = a * 134217729.0;
    double a_high = split_a - (split_a - a);
    double a_low = a - a_high;
    double split_b = b * 134217729.0;
    double b_high = split_b - (split_b - b);
    double b_low = b - b_high;
    double error = ((a_high * b_high - product) + a_high * b_low + a_low * b_high) + a_low * b_low;
    *lost_sign = error > 0 ? 1 : (error < 0 ? -1 : 0);
    return product;
}

// value * 10^shift or value / 10^-shift, rounded once, with the sign of the rounding error
static double scale_by_power(double value, int shift, const double *powers, int *lost_sign)
{
    if (shift >= 0)
    {
        return two_product(value, powers[shift], lost_sign);
    }

    double power = powers[-shift];
    double quotient = value / power;
    // back plus what its rounding lost is quotient * power exactly, so value minus that is the remainder
    int product_lost = 0;
    double back = two_product(quotient, power, &product_lost);
    double remainder = value - back;
    if (remainder == 0)
    {
        *lost_sign = -product_lost;
    }
    else
    {
        *lost_sign = remainder > 0 ? 1 : -1;
    }
    return quotient;
}

// Same output as printf's %g: 6 significant digits, no trailing zeros, exponent form when
// the exponent is below -4 or at least 6.
static int format_double(double value, char *out)
{
    int n = 0;
    if (value != value)
    {
        out[n++] = 'n';
        out[n++] = 'a';
        out[n++] = 'n';
        return n;
    }

    bool negative = value < 0 || (value == 0 && 1.0 / value < 0);
    if (negative)
    {
        out[n++] = '-';
        value = -value;
    }

    if (value > 1.7976931348623157e308)
    {
        out[n++] = 'i';
        out[n++] = 'n';
        out[n++] = 'f';
        return n;
    }

    if (value == 0)
    {
        out[n++] = '0';
        return n;
    }

    // Scale the value so it has six digits before the point. Multiplying or dividing by an exact
    // power of ten rounds only once, so a value that is exactly halfway (5386.125) is seen as
    // halfway and rounded to even like printf does
    static const double powers[23] = {1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
                                      1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
    int exponent = 0;
    long long scaled;
    if (value >= 1e22 || value < 1e-17)
    {
        // Out of the exact range: less exact, but only for extreme values
        double mantissa = value;
        while (mantissa >= 10.0)
        {
            mantissa /= 10.0;
            exponent++;
        }
        while (mantissa < 1.0)
        {
            mantissa *= 10.0;
            exponent--;
        }
        scaled = (long long)(mantissa * 100000.0 + 0.5);
    }
    else
    {
        if (value >= 1.0)
        {
            while (exponent < 22 && powers[exponent + 1] <= value)
            {
                exponent++;
            }
        }
        else
        {
            int k = 1;
            while (k < 22 && value * powers[k] < 1.0)
            {
                k++;
            }
            exponent = -k;
        }

        double scaled_value = 0;
        int lost_sign = 0;
        for (int attempt = 0; attempt < 2; attempt++)
        {
            scaled_value = scale_by_power(value, 5 - exponent, powers, &lost_sign);
            if (scaled_value >= 100000.0)
            {
                break;
            }
            // The exponent was one too high
            exponent--;
        }

        // Round like printf: past halfway goes up, exactly halfway goes to the even digit, and
        // "exactly" is decided by the exact value, not the rounded product
        scaled = (long long)scaled_value;
        double fraction = scaled_value - (double)scaled;
        if (fraction > 0.5 || (fraction == 0.5 && (lost_sign > 0 || (lost_sign == 0 && (scaled & 1)))))
        {
            scaled++;
        }
    }
    if (scaled >= 1000000)
    {
        scaled /= 10;
        exponent++;
    }

    char digits[6];
    for (int i = 5; i >= 0; i--)
    {
        digits[i] = (char)('0' + scaled % 10);
        scaled /= 10;
    }
    int count = 6;
    while (count > 1 && digits[count - 1] == '0')
    {
        count--;
    }

    if (exponent < -4 || exponent >= 6)
    {
        out[n++] = digits[0];
        if (count > 1)
        {
            out[n++] = '.';
            for (int i = 1; i < count; i++)
            {
                out[n++] = digits[i];
            }
        }
        out[n++] = 'e';
        int abs_exponent = exponent;
        if (exponent < 0)
        {
            out[n++] = '-';
            abs_exponent = -exponent;
        }
        else
        {
            out[n++] = '+';
        }
        if (abs_exponent >= 100)
        {
            out[n++] = (char)('0' + abs_exponent / 100);
        }
        out[n++] = (char)('0' + (abs_exponent / 10) % 10);
        out[n++] = (char)('0' + abs_exponent % 10);
    }
    else if (exponent >= 0)
    {
        for (int i = 0; i <= exponent; i++)
        {
            out[n++] = i < count ? digits[i] : '0';
        }
        if (count > exponent + 1)
        {
            out[n++] = '.';
            for (int i = exponent + 1; i < count; i++)
            {
                out[n++] = digits[i];
            }
        }
    }
    else
    {
        out[n++] = '0';
        out[n++] = '.';
        for (int i = 0; i < -exponent - 1; i++)
        {
            out[n++] = '0';
        }
        for (int i = 0; i < count; i++)
        {
            out[n++] = digits[i];
        }
    }
    return n;
}

static int format_unsigned(unsigned long long value, int base, char *out)
{
    char reversed[24];
    int count = 0;
    do
    {
        int digit = (int)(value % (unsigned long long)base);
        reversed[count++] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
        value /= (unsigned long long)base;
    } while (value > 0);

    for (int i = 0; i < count; i++)
    {
        out[i] = reversed[count - 1 - i];
    }
    return count;
}

// Handles the conversions Rain uses: %s %d %i %u %c %g %p %x, the l and z length modifiers, and %.*s
static void format_to(emit_fn emit, void *context, const char *format, va_list args)
{
    while (*format)
    {
        if (*format != '%')
        {
            const char *start = format;
            while (*format && *format != '%')
            {
                format++;
            }
            emit(context, start, (int)(format - start));
            continue;
        }

        format++;
        int precision = -1;
        if (*format == '.')
        {
            format++;
            if (*format == '*')
            {
                precision = va_arg(args, int);
                format++;
            }
            else
            {
                precision = 0;
                while (*format >= '0' && *format <= '9')
                {
                    precision = precision * 10 + (*format - '0');
                    format++;
                }
            }
        }

        bool is_long = false;
        while (*format == 'l' || *format == 'z')
        {
            is_long = true;
            format++;
        }

        char scratch[64];
        switch (*format++)
        {
        case 's':
        {
            const char *text = va_arg(args, const char *);
            if (!text)
            {
                text = "(null)";
            }
            int length = 0;
            while (text[length] && (precision < 0 || length < precision))
            {
                length++;
            }
            emit(context, text, length);
            break;
        }

        case 'd':
        case 'i':
        {
            long long value = is_long ? va_arg(args, long) : va_arg(args, int);
            int n = 0;
            unsigned long long magnitude = (unsigned long long)value;
            if (value < 0)
            {
                scratch[n++] = '-';
                magnitude = (unsigned long long)(-(value + 1)) + 1;
            }
            n += format_unsigned(magnitude, 10, scratch + n);
            emit(context, scratch, n);
            break;
        }

        case 'u':
        {
            unsigned long long value = is_long ? va_arg(args, unsigned long) : va_arg(args, unsigned int);
            emit(context, scratch, format_unsigned(value, 10, scratch));
            break;
        }

        case 'x':
        {
            unsigned long long value = is_long ? va_arg(args, unsigned long) : va_arg(args, unsigned int);
            emit(context, scratch, format_unsigned(value, 16, scratch));
            break;
        }

        case 'p':
        {
            unsigned long long value = (unsigned long long)(uintptr_t)va_arg(args, void *);
            scratch[0] = '0';
            scratch[1] = 'x';
            emit(context, scratch, 2 + format_unsigned(value, 16, scratch + 2));
            break;
        }

        case 'c':
        {
            scratch[0] = (char)va_arg(args, int);
            emit(context, scratch, 1);
            break;
        }

        case 'g':
        {
            emit(context, scratch, format_double(va_arg(args, double), scratch));
            break;
        }

        case '%':
            emit(context, "%", 1);
            break;

        default:
            // An unsupported conversion: show it as written, so the mistake is visible
            emit(context, "%?", 2);
            break;
        }
    }
}

int rain_vsnprintf(char *buffer, size_t size, const char *format, va_list args)
{
    struct buffer_sink sink = {buffer, size, 0};
    format_to(buffer_emit, &sink, format, args);
    if (size > 0)
    {
        buffer[sink.length < size ? sink.length : size - 1] = 0;
    }
    return (int)sink.length;
}

int rain_snprintf(char *buffer, size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int length = rain_vsnprintf(buffer, size, format, args);
    va_end(args);
    return length;
}

int rain_printf(const char *format, ...)
{
    struct writer_sink sink = {0};
    va_list args;
    va_start(args, format);
    format_to(writer_emit, &sink, format, args);
    va_end(args);
    return 0;
}

int rain_fprintf(void *stream, const char *format, ...)
{
    struct writer_sink sink = {stream == (void *)2};
    va_list args;
    va_start(args, format);
    format_to(writer_emit, &sink, format, args);
    va_end(args);
    return 0;
}

int rain_vfprintf(void *stream, const char *format, va_list args)
{
    struct writer_sink sink = {stream == (void *)2};
    format_to(writer_emit, &sink, format, args);
    return 0;
}

size_t rain_strlen(const char *text)
{
    size_t length = 0;
    while (text[length])
    {
        length++;
    }
    return length;
}

int rain_fputs(const char *text, void *stream)
{
    int length = 0;
    while (text[length])
    {
        length++;
    }
    write_out(text, length, stream == (void *)2);
    return 0;
}

// ---- strings and memory helpers ----

int rain_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

void *rain_memcpy(void *destination, const void *source, size_t count)
{
    unsigned char *to = destination;
    const unsigned char *from = source;
    for (size_t i = 0; i < count; i++)
    {
        to[i] = from[i];
    }
    return destination;
}

void *rain_memmove(void *destination, const void *source, size_t count)
{
    unsigned char *to = destination;
    const unsigned char *from = source;
    if (to < from)
    {
        for (size_t i = 0; i < count; i++)
        {
            to[i] = from[i];
        }
    }
    else
    {
        for (size_t i = count; i > 0; i--)
        {
            to[i - 1] = from[i - 1];
        }
    }
    return destination;
}

void *rain_memset(void *destination, int value, size_t count)
{
    unsigned char *to = destination;
    for (size_t i = 0; i < count; i++)
    {
        to[i] = (unsigned char)value;
    }
    return destination;
}

int rain_memcmp(const void *a, const void *b, size_t count)
{
    const unsigned char *left = a;
    const unsigned char *right = b;
    for (size_t i = 0; i < count; i++)
    {
        if (left[i] != right[i])
        {
            return left[i] - right[i];
        }
    }
    return 0;
}

// ---- numbers ----

double rain_strtod(const char *text, char **end)
{
    const char *p = text;
    bool negative = false;
    if (*p == '-' || *p == '+')
    {
        negative = *p == '-';
        p++;
    }

    // All digits go into one integer, and the decimal point is applied afterwards, so
    // 3.14 is 314 / 100 and comes out correctly rounded
    unsigned long long mantissa = 0;
    int decimal_exponent = 0;
    bool digits_seen = false;
    while (*p >= '0' && *p <= '9')
    {
        digits_seen = true;
        if (mantissa < 1000000000000000000ULL)
        {
            mantissa = mantissa * 10 + (unsigned long long)(*p - '0');
        }
        else
        {
            decimal_exponent++;
        }
        p++;
    }
    if (*p == '.')
    {
        p++;
        while (*p >= '0' && *p <= '9')
        {
            digits_seen = true;
            if (mantissa < 1000000000000000000ULL)
            {
                mantissa = mantissa * 10 + (unsigned long long)(*p - '0');
                decimal_exponent--;
            }
            p++;
        }
    }

    if (!digits_seen)
    {
        if (end)
        {
            *end = (char *)text;
        }
        return 0;
    }

    if (*p == 'e' || *p == 'E')
    {
        const char *q = p + 1;
        bool exponent_negative = false;
        if (*q == '-' || *q == '+')
        {
            exponent_negative = *q == '-';
            q++;
        }
        if (*q >= '0' && *q <= '9')
        {
            int exponent = 0;
            while (*q >= '0' && *q <= '9')
            {
                if (exponent < 10000)
                {
                    exponent = exponent * 10 + (*q - '0');
                }
                q++;
            }
            decimal_exponent += exponent_negative ? -exponent : exponent;
            p = q;
        }
    }

    double value = (double)mantissa;
    if (decimal_exponent < 0)
    {
        double divisor = 1.0;
        for (int i = 0; i < -decimal_exponent && i < 400; i++)
        {
            divisor *= 10.0;
        }
        value /= divisor;
    }
    else
    {
        for (int i = 0; i < decimal_exponent && i < 400; i++)
        {
            value *= 10.0;
        }
    }

    if (end)
    {
        *end = (char *)p;
    }
    return negative ? -value : value;
}

double rain_floor(double value)
{
    // Beyond 2^53 every double is already a whole number
    if (value >= 9007199254740992.0 || value <= -9007199254740992.0 || value != value)
    {
        return value;
    }
    double truncated = (double)(long long)value;
    return truncated > value ? truncated - 1.0 : truncated;
}

// Shell sort: no recursion and no extra memory, and fast enough for script arrays
void rain_qsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *))
{
    unsigned char *items = base;
    unsigned char temporary[64];
    if (size > sizeof(temporary))
    {
        return;
    }

    for (size_t gap = count / 2; gap > 0; gap /= 2)
    {
        for (size_t i = gap; i < count; i++)
        {
            rain_memcpy(temporary, items + i * size, size);
            size_t j = i;
            while (j >= gap && compare(items + (j - gap) * size, temporary) > 0)
            {
                rain_memcpy(items + j * size, items + (j - gap) * size, size);
                j -= gap;
            }
            rain_memcpy(items + j * size, temporary, size);
        }
    }
}

// ---- memory ----
//
// Every malloc in MarrowOS is a system call that takes at least a whole 4 KB page, and Rain makes
// thousands of small objects. So small blocks are cut from 1 MB chunks and recycled through free
// lists, and only big blocks go to the system. Each block has a 16 byte header in front of it.

#define ARENA_CHUNK_SIZE (1024 * 1024)
#define ARENA_HEADER_SIZE 16
#define ARENA_CLASS_COUNT 8
#define ARENA_LARGE 0xFF

struct arena_header
{
    size_t size;
    size_t class_index;
};

struct free_block
{
    struct free_block *next;
};

static const size_t arena_class_sizes[ARENA_CLASS_COUNT] = {16, 32, 64, 128, 256, 512, 1024, 2048};
static struct free_block *arena_free_lists[ARENA_CLASS_COUNT];
static char *arena_next = NULL;
static char *arena_end = NULL;

static int arena_class_for(size_t size)
{
    for (int i = 0; i < ARENA_CLASS_COUNT; i++)
    {
        if (size <= arena_class_sizes[i])
        {
            return i;
        }
    }
    return -1;
}

void *rain_malloc(size_t size)
{
    if (size == 0)
    {
        size = 1;
    }

    int class_index = arena_class_for(size);
    if (class_index < 0)
    {
        struct arena_header *header = yreserve(size + ARENA_HEADER_SIZE);
        if (!header)
        {
            return NULL;
        }
        header->size = size;
        header->class_index = ARENA_LARGE;
        return (char *)header + ARENA_HEADER_SIZE;
    }

    struct free_block *recycled = arena_free_lists[class_index];
    if (recycled)
    {
        arena_free_lists[class_index] = recycled->next;
        return recycled;
    }

    size_t block_size = arena_class_sizes[class_index] + ARENA_HEADER_SIZE;
    if (arena_next == NULL || arena_next + block_size > arena_end)
    {
        char *chunk = yreserve(ARENA_CHUNK_SIZE);
        if (!chunk)
        {
            return NULL;
        }
        arena_next = chunk;
        arena_end = chunk + ARENA_CHUNK_SIZE;
    }

    struct arena_header *header = (struct arena_header *)arena_next;
    arena_next += block_size;
    header->size = arena_class_sizes[class_index];
    header->class_index = (size_t)class_index;
    return (char *)header + ARENA_HEADER_SIZE;
}

void rain_free(void *pointer)
{
    if (!pointer)
    {
        return;
    }

    struct arena_header *header = (struct arena_header *)((char *)pointer - ARENA_HEADER_SIZE);
    if (header->class_index == ARENA_LARGE)
    {
        yfree(header);
        return;
    }

    struct free_block *block = pointer;
    block->next = arena_free_lists[header->class_index];
    arena_free_lists[header->class_index] = block;
}

void *rain_realloc(void *pointer, size_t size)
{
    if (!pointer)
    {
        return rain_malloc(size);
    }
    if (size == 0)
    {
        rain_free(pointer);
        return NULL;
    }

    struct arena_header *header = (struct arena_header *)((char *)pointer - ARENA_HEADER_SIZE);
    if (size <= header->size && (header->class_index != ARENA_LARGE || size * 2 > header->size))
    {
        return pointer;
    }

    void *grown = rain_malloc(size);
    if (!grown)
    {
        return NULL;
    }
    rain_memcpy(grown, pointer, header->size < size ? header->size : size);
    rain_free(pointer);
    return grown;
}

// ---- files ----

// Weak, so a platform can supply the real thing
__attribute__((weak)) char *rain_read_file(const char *path)
{
    (void)path;
    return NULL;
}

// ---- process ----

void rain_exit(int code)
{
    // Only reached when Rain runs out of memory
    marrowos_exit();
    for (;;)
    {
    }
}

static struct marrowos_system_info *clock_info = NULL;

// Seconds since boot
double rain_clock(void)
{
    // Syscall buffers must be on the heap, so this is allocated once
    if (!clock_info)
    {
        clock_info = yreserve(sizeof(struct marrowos_system_info));
        if (!clock_info)
        {
            return 0;
        }
    }
    marrowos_system_info(clock_info);
    return (double)clock_info->uptime_ms / 1000.0;
}
