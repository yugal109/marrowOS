// Compares the platform layer against the host's libc on random input
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int rain_snprintf(char *buffer, size_t size, const char *format, ...);
double rain_strtod(const char *text, char **end);
double rain_floor(double value);
void *rain_malloc(size_t size);
void *rain_realloc(void *pointer, size_t size);
void rain_free(void *pointer);
void rain_qsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *));

static double random_double(void)
{
    int kind = rand() % 4;
    double mantissa = (double)rand() / RAND_MAX;
    switch (kind)
    {
    case 0: return (double)(rand() % 2000000 - 1000000) / (double)(1 + rand() % 1000);
    case 1: return mantissa * pow(10, rand() % 40 - 20) * (rand() % 2 ? 1 : -1);
    case 2: return (double)(rand() % 100000);
    default: return mantissa;
    }
}

static int compare_ints(const void *a, const void *b)
{
    return *(const int *)a - *(const int *)b;
}

int main(void)
{
    srand(12345);
    int bad = 0;

    for (int i = 0; i < 300000; i++)
    {
        double value = random_double();
        char mine[64], real[64];
        rain_snprintf(mine, sizeof(mine), "%g", value);
        snprintf(real, sizeof(real), "%g", value);
        if (strcmp(mine, real) != 0 && bad++ < 10)
        {
            printf("%%g mismatch for %.17g: mine=%s real=%s\n", value, mine, real);
        }
    }
    printf("%%g checked, mismatches: %d\n", bad);

    int strtod_bad = 0;
    for (int i = 0; i < 300000; i++)
    {
        char text[64];
        int whole = rand() % 100000;
        int frac = rand() % 100000;
        int frac_digits = 1 + rand() % 5;
        if (rand() % 3 == 0)
        {
            snprintf(text, sizeof(text), "%d", whole);
        }
        else
        {
            snprintf(text, sizeof(text), "%d.%0*d", whole, frac_digits, frac % (int)pow(10, frac_digits));
        }
        char *end_mine, *end_real;
        double mine = rain_strtod(text, &end_mine);
        double real = strtod(text, &end_real);
        if ((mine != real || end_mine != text + (end_real - text)) && strtod_bad++ < 10)
        {
            printf("strtod mismatch for %s: mine=%.17g real=%.17g\n", text, mine, real);
        }
    }
    printf("strtod checked, mismatches: %d\n", strtod_bad);

    int floor_bad = 0;
    for (int i = 0; i < 300000; i++)
    {
        double value = random_double() * 1000;
        if (rain_floor(value) != floor(value) && floor_bad++ < 10)
        {
            printf("floor mismatch for %.17g\n", value);
        }
    }
    printf("floor checked, mismatches: %d\n", floor_bad);

    // Allocator: random sizes, contents checked after growing and after other blocks are freed
    enum { SLOTS = 400 };
    unsigned char *slot[SLOTS] = {0};
    size_t size[SLOTS] = {0};
    int alloc_bad = 0;
    for (int i = 0; i < 200000; i++)
    {
        int s = rand() % SLOTS;
        int action = rand() % 3;
        size_t new_size = rand() % 3 == 0 ? (size_t)(rand() % 6000) : (size_t)(rand() % 300);
        if (action == 0 && slot[s])
        {
            for (size_t k = 0; k < size[s]; k++)
            {
                if (slot[s][k] != (unsigned char)(s + k) && alloc_bad++ < 5)
                {
                    printf("allocator: slot %d corrupted at %zu\n", s, k);
                    break;
                }
            }
            rain_free(slot[s]);
            slot[s] = NULL;
            size[s] = 0;
        }
        else
        {
            unsigned char *grown = rain_realloc(slot[s], new_size);
            if (new_size == 0)
            {
                slot[s] = NULL;
                size[s] = 0;
                continue;
            }
            size_t keep = size[s] < new_size ? size[s] : new_size;
            for (size_t k = 0; k < keep; k++)
            {
                if (grown[k] != (unsigned char)(s + k) && alloc_bad++ < 5)
                {
                    printf("allocator: slot %d lost data on realloc at %zu\n", s, k);
                    break;
                }
            }
            for (size_t k = keep; k < new_size; k++)
            {
                grown[k] = (unsigned char)(s + k);
            }
            slot[s] = grown;
            size[s] = new_size;
        }
    }
    printf("allocator checked, problems: %d\n", alloc_bad);

    int values[5000];
    for (int i = 0; i < 5000; i++)
    {
        values[i] = rand() % 1000;
    }
    rain_qsort(values, 5000, sizeof(int), compare_ints);
    int sort_bad = 0;
    for (int i = 1; i < 5000; i++)
    {
        sort_bad += values[i - 1] > values[i];
    }
    printf("qsort checked, out of order: %d\n", sort_bad);

    for (int s = 0; s < SLOTS; s++)
    {
        rain_free(slot[s]);
    }

    return bad + strtod_bad + floor_bad + alloc_bad + sort_bad != 0;
}
