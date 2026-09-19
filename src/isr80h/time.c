#include "time.h"
#include "task/task.h"
#include "task/process.h"
#include "io/tsc.h"

void *isr80h_command25_udelay(struct interrupt_frame *frame)
{
    uint64_t microseconds = (uint64_t)task_get_stack_item(task_current(), 0);
    task_sleep(task_current(), microseconds);

    task_next();

    // this line never runs, task_next() switches away
    return NULL;
}
