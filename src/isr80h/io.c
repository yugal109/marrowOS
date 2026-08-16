#include "io.h"
#include "task/task.h"
#include "kernel.h"
#include "memory/memory.h"

void *isr80h_command1_print(struct interrupt_frame *frame)
{
    void *user_space_msg_buffer = task_get_stack_item(task_current(), 0);
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    copy_string_from_task(task_current(), user_space_msg_buffer, buf, sizeof(buf));

    print(buf);
    return 0;
}
