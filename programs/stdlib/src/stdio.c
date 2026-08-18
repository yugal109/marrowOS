#include "stdio.h"
#include "marrowos.h"
#include "stdlib.h"
#include <stdarg.h>

int putchar(int c)
{
    marrowos_putchar(c);
    return 0;
}

static void print_uint(unsigned int val, unsigned int base, int upper)
{
    char buf[16];
    int loc = 15;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    buf[15] = 0;
    if (val == 0)
    {
        buf[--loc] = '0';
        print(&buf[loc]);
        return;
    }
    while (val)
    {
        buf[--loc] = digits[val % base];
        val /= base;
    }
    print(&buf[loc]);
}

static unsigned long long udivmod64(unsigned long long n, unsigned int d, unsigned int *r_out)
{
    unsigned long long q = 0;
    unsigned long long r = 0;
    int i;
    for (i = 63; i >= 0; i--)
    {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d)
        {
            r -= d;
            q |= (1ULL << i);
        }
    }
    *r_out = (unsigned int)r;
    return q;
}

static void print_ull(unsigned long long val, unsigned int base, int upper)
{
    char buf[24];
    int loc = 23;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    unsigned int rem;
    buf[23] = 0;
    if (val == 0)
    {
        buf[--loc] = '0';
        print(&buf[loc]);
        return;
    }
    while (val)
    {
        val = udivmod64(val, base, &rem);
        buf[--loc] = digits[rem];
    }
    print(&buf[loc]);
}

static void print_ll(long long val)
{
    unsigned long long uval;
    if (val < 0)
    {
        putchar('-');
        uval = (unsigned long long)(-(val + 1)) + 1;
    }
    else
    {
        uval = (unsigned long long)val;
    }
    print_ull(uval, 10, 0);
}

int printf(const char *fmt, ...)
{
    va_list ap;
    const char *p;
    char *sval;
    int ival;
    va_start(ap, fmt);
    for (p = fmt; *p; p++)
    {
        if (*p != '%')
        {
            putchar(*p);
            continue;
        }
        switch (*++p)
        {
        case 'i': // %i
            ival = va_arg(ap, int);
            print(itoa(ival));
            break;
        case 'd':
            ival = va_arg(ap, int);
            print(itoa(ival));
            break;
        case 's':
            sval = va_arg(ap, char *);
            print(sval);
            break;
        case 'c':
            putchar(va_arg(ap, int));
            break;
        case 'u':
            print_uint(va_arg(ap, unsigned int), 10, 0);
            break;
        case 'x':
            print_uint(va_arg(ap, unsigned int), 16, 0);
            break;
        case 'X':
            print_uint(va_arg(ap, unsigned int), 16, 1);
            break;
        case 'o':
            print_uint(va_arg(ap, unsigned int), 8, 0);
            break;
        case 'p':
            print("0x");
            print_uint((unsigned int)va_arg(ap, void *), 16, 0);
            break;
        case '%':
            putchar('%');
            break;
        case 'l':
            if (*(p + 1) == 'l')
            {
                p++;
                switch (*++p)
                {
                case 'd':
                case 'i':
                    print_ll(va_arg(ap, long long));
                    break;
                case 'u':
                    print_ull(va_arg(ap, unsigned long long), 10, 0);
                    break;
                case 'x':
                    print_ull(va_arg(ap, unsigned long long), 16, 0);
                    break;
                case 'X':
                    print_ull(va_arg(ap, unsigned long long), 16, 1);
                    break;
                case 'o':
                    print_ull(va_arg(ap, unsigned long long), 8, 0);
                    break;
                default:
                    putchar(*p);
                    break;
                }
            }
            else
            {
                switch (*++p)
                {
                case 'd':
                case 'i':
                    ival = va_arg(ap, long);
                    print(itoa(ival));
                    break;
                case 'u':
                    print_uint(va_arg(ap, unsigned long), 10, 0);
                    break;
                case 'x':
                    print_uint(va_arg(ap, unsigned long), 16, 0);
                    break;
                case 'X':
                    print_uint(va_arg(ap, unsigned long), 16, 1);
                    break;
                case 'o':
                    print_uint(va_arg(ap, unsigned long), 8, 0);
                    break;
                default:
                    putchar(*p);
                    break;
                }
            }
            break;
        default:
            putchar(*p);
            break;
        }
    }
    va_end(ap);
    return 0;
}
