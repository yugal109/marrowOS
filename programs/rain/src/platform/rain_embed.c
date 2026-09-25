#include "rain.h"
#include "vm.h"

int rain_run(const char *source, rain_write_fn writer)
{
    rain_set_writer(writer);

    // A fresh VM per run, so nothing leaks from one Run click into the next
    initVM();
    InterpretResult result = interpret(source);
    freeVM();

    if (result == INTERPRET_COMPILE_ERROR)
    {
        return RAIN_RUN_COMPILE_ERROR;
    }
    if (result == INTERPRET_RUNTIME_ERROR)
    {
        return RAIN_RUN_RUNTIME_ERROR;
    }
    return RAIN_RUN_OK;
}
