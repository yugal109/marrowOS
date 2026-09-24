#include "serial.h"
#include "task/task.h"
#include "task/process.h"
#include "usb/ehci.h"
#include "status.h"
#include <stdint.h>

// One writer at a time: the Print button is the only user
static uint8_t serial_chunk[EHCI_SERIAL_CHUNK];

void *isr80h_command29_serial_write(struct interrupt_frame *frame)
{
    struct process *process = task_current()->process;
    uint8_t *virt_data = task_get_stack_item(task_current(), 0);
    size_t len = (size_t)task_get_stack_item(task_current(), 1);
    if (len > sizeof(serial_chunk))
    {
        len = sizeof(serial_chunk);
    }

    if (process_validate_memory_or_terminate(process, virt_data, len) < 0)
    {
        return (void *)(long)-EINVARG;
    }

    // The buffer's pages need not be contiguous in memory
    for (size_t i = 0; i < len; i++)
    {
        uint8_t *src = process_virtual_address_to_physical(process, virt_data + i);
        if (!src)
        {
            return (void *)(long)-EINVARG;
        }
        serial_chunk[i] = *src;
    }

    return (void *)(long)ehci_serial_write(serial_chunk, len);
}
