#ifndef rain_builtins_h
#define rain_builtins_h

/* Global builtin names registered in natives.c */
#define RAIN_BUILTIN_CLOCK "clock"
#define RAIN_BUILTIN_VAKYA "vakya"
#define RAIN_BUILTIN_TO_STRING "toString"
#define RAIN_BUILTIN_TYPE_OF "typeOf"
#define RAIN_BUILTIN_IS_TYPE "isType"
#define RAIN_BUILTIN_ASSERT_TYPE "assertType"
#define RAIN_BUILTIN_IMPORT "__import__"

/* Preloaded module loader names */
#define RAIN_MOD_MATH "math"
#define RAIN_MOD_IO "io"
#define RAIN_MOD_STR "str"
#define RAIN_MOD_OS "os"
#define RAIN_MOD_JSON "json"
#define RAIN_MOD_HTTP "http"
#define RAIN_MOD_TIME "time"
#define RAIN_MOD_DB "db"
#define RAIN_MOD_CSV "csv"
#define RAIN_MOD_CRYPTO "crypto"

/* Interned method / special names */
#define RAIN_NAME_INIT "init"

#endif
