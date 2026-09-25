#ifndef rain_common_h
#define rain_common_h

#include <stdbool.h> // bool, true,false -C has no native boolean type
#include <stddef.h>  // NULL,size_t -null pointer constant and memory size type
#include <stdint.h>  // unit8_t ,uint16_t,uint32_t - fixed width integers, no platform surprises

// #define NAN_BOXING
// #define DEBUG_PRINT_CODE
// #define DEBUG_TRACE_EXECUTION

// #define DEBUG_STRESS_GC
// #define DEBUG_LOG_GC

#define UINT8_COUNT (UINT8_MAX + 1)

#ifndef RAIN_VERSION
#define RAIN_VERSION "dev"
#endif

#endif
