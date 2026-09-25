#ifndef RAIN_H
#define RAIN_H

// Public API for running Rain from another program. Unlike rain_platform.h this defines no macros,
// so it is safe to include anywhere.

#define RAIN_RUN_OK 0
#define RAIN_RUN_COMPILE_ERROR 1
#define RAIN_RUN_RUNTIME_ERROR 2

// Receives everything Rain prints, in pieces (not whole lines). is_error is set for error messages
typedef void (*rain_write_fn)(const char *text, int length, int is_error);

// Runs one Rain program in a fresh VM and returns a RAIN_RUN_* code
int rain_run(const char *source, rain_write_fn writer);

#endif
