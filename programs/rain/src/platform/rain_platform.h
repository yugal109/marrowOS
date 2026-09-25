#ifndef RAIN_PLATFORM_H
#define RAIN_PLATFORM_H

// Force-included into every Rain source file (-include). MarrowOS has no C library, so this
// points the calls Rain relies on at small replacements in rain_platform.c.

#include <stddef.h>
#include <stdarg.h>

typedef void FILE;

// Everything Rain prints (print statements and error messages) goes to this function
typedef void (*rain_write_fn)(const char *text, int length, int is_error);
void rain_set_writer(rain_write_fn writer);

int rain_vsnprintf(char *buffer, size_t size, const char *format, va_list args);
int rain_snprintf(char *buffer, size_t size, const char *format, ...);
int rain_printf(const char *format, ...);
int rain_fprintf(void *stream, const char *format, ...);
int rain_fputs(const char *text, void *stream);
int rain_vfprintf(void *stream, const char *format, va_list args);
size_t rain_strlen(const char *text);
int rain_strcmp(const char *a, const char *b);
void *rain_memcpy(void *destination, const void *source, size_t count);
void *rain_memmove(void *destination, const void *source, size_t count);
void *rain_memset(void *destination, int value, size_t count);
int rain_memcmp(const void *a, const void *b, size_t count);
double rain_strtod(const char *text, char **end);
double rain_floor(double value);
void rain_qsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *));
void *rain_malloc(size_t size);
void *rain_realloc(void *pointer, size_t size);
void rain_free(void *pointer);
void rain_exit(int code);

// Returns the contents of a file as a rain_malloc'd, NUL-terminated string, or NULL if it cannot
// be read. The default reads nothing, so importing .rn files reports "not found" until it is provided
char *rain_read_file(const char *path);
double rain_clock(void);

#define printf rain_printf
#define fprintf rain_fprintf
#define fputs rain_fputs
#define vfprintf rain_vfprintf
#define snprintf rain_snprintf
#define vsnprintf rain_vsnprintf
#define strlen rain_strlen
#define strcmp rain_strcmp
#define memcpy rain_memcpy
#define memmove rain_memmove
#define memset rain_memset
#define memcmp rain_memcmp
#define strtod rain_strtod
#define floor rain_floor
#define qsort rain_qsort
#define malloc rain_malloc
#define realloc rain_realloc
#define free rain_free
#define exit rain_exit


#define stdout ((void *)1)
#define stderr ((void *)2)

#endif
