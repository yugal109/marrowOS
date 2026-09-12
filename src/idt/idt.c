#include "idt.h"
#include "config.h"
#include "memory/memory.h"
#include "kernel.h"
#include "memory/heap/kheap.h"
#include "task/process.h"
#include "task/task.h"
#include "io/io.h"
#include "status.h"
#include "string/string.h"
#include "memory/heap/heap.h"

extern struct heap kernel_minimal_heap;

struct idt_desc idtr_descriptors[MARROWOS_TOTAL_INTERRUPTS];
struct idtr_desc idtr_descriptor;

extern void *interrupt_pointer_table[MARROWOS_TOTAL_INTERRUPTS];

static INTERRUPT_CALLBACK_FUNCTION interrupt_callbacks[MARROWOS_TOTAL_INTERRUPTS];
static ISR80H_COMMAND isr80h_commands[MARROWOS_MAX_ISR80H_COMMANDS];

extern void idt_load(struct idtr_desc *ptr);
extern void idt_zero();
extern void int21h();
extern void no_interrupt();
extern void isr80h_wrapper();

static int current_interrupt = -1;

void interrupt_handler(int interrupt, struct interrupt_frame *frame)
{
    kernel_page();
    if (interrupt_callbacks[interrupt] != 0)
    {
        // task_current_save_state(frame);
        interrupt_callbacks[interrupt](frame);
    }

    // task_page();
    outb(0x20, 0x20);
    outb(0xA0, 0x20);
}

void idt_zero_handler()
{
    print("Divide by zero error.\n");
    while (1)
    {
    }
}

void no_interrupt_handler()
{
    outb(0x20, 0x20);
    outb(0xA0, 0x20);
}

void idt_set(int interrupt_no, void *address)
{
    struct idt_desc *desc = &idtr_descriptors[interrupt_no];
    uintptr_t _address = (uintptr_t)address;
    desc->offset_1 = _address & 0x000000000000ffff;
    desc->selector = KERNEL_LONG_MODE_CODE_SELECTOR;
    desc->ist = 0;
    desc->type_attr = 0xEE;
    if (interrupt_no <= 0x31)
    {
        desc->type_attr = 0x8E;
    }
    desc->offset_2 = (_address >> 16) & 0x000000000000ffff;
    desc->offset_3 = (_address >> 32) & 0x00000000ffffffff;
}

static char *idt_append_str(char *out, const char *s)
{
    while (*s)
    {
        *out++ = *s++;
    }
    *out = 0;
    return out;
}

static char *idt_append_hex(char *out, uint64_t value)
{
    const char *digits = "0123456789ABCDEF";
    out = idt_append_str(out, "0x");
    for (int shift = 60; shift >= 0; shift -= 4)
    {
        *out++ = digits[(value >> shift) & 0xF];
    }
    *out = 0;
    return out;
}

static int idt_exception_has_error_code(int exception)
{
    return exception == 8 || (exception >= 10 && exception <= 14) || exception == 17 ||
           exception == 21 || exception == 29 || exception == 30;
}

void idt_handle_exception(struct interrupt_frame *frame)
{
    // Some exceptions push an error code, shifting the frame by one word
    uint64_t words[14];
    memcpy(words, (void *)frame, sizeof(words));
    int has_error_code = idt_exception_has_error_code(current_interrupt);
    uint64_t error_code = has_error_code ? words[8] : 0;
    uint64_t rip = has_error_code ? words[9] : words[8];
    uint64_t fault_rsp = has_error_code ? words[12] : words[11];
    uint64_t cr2 = 0;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));

    static char msg[512];
    char *p = msg;
    p = idt_append_str(p, "EXCEPTION ");
    p = idt_append_str(p, itoa(current_interrupt));
    p = idt_append_str(p, " RIP=");
    p = idt_append_hex(p, rip);
    p = idt_append_str(p, " ERR=");
    p = idt_append_hex(p, error_code);
    p = idt_append_str(p, " CR2=");
    p = idt_append_hex(p, cr2);
    p = idt_append_str(p, "\nRAX=");
    p = idt_append_hex(p, words[7]);
    p = idt_append_str(p, " RDX=");
    p = idt_append_hex(p, words[5]);
    p = idt_append_str(p, " RSP=");
    p = idt_append_hex(p, fault_rsp);
    p = idt_append_str(p, "\nHEAP USED ");
    p = idt_append_str(p, itoa((int)kernel_minimal_heap.used_blocks));
    p = idt_append_str(p, " OF ");
    p = idt_append_str(p, itoa((int)kernel_minimal_heap.total_blocks));
    p = idt_append_str(p, " BLOCKS\nSTACK");

    // No frame pointers, so just scan stack for kernel-looking addresses
    uint64_t *stack = (uint64_t *)fault_rsp;
    int shown = 0;
    for (int i = 0; i < 64 && shown < 8; i++)
    {
        if (stack[i] >= 0x100000 && stack[i] < 0x140000)
        {
            p = idt_append_str(p, " ");
            p = idt_append_hex(p, stack[i]);
            shown++;
        }
    }
    idt_append_str(p, "\n");
    panic(msg);
}

void idt_clock()
{
    outb(0x20, 0x20);

    print("test\n");
    task_next();
}

void idt_init()
{
    memset(idtr_descriptors, 0, sizeof(idtr_descriptors));
    idtr_descriptor.limit = sizeof(idtr_descriptors) - 1;
    idtr_descriptor.base = (uint64_t)idtr_descriptors;

    for (int i = 0; i < MARROWOS_TOTAL_INTERRUPTS; i++)
    {
        idt_set(i, interrupt_pointer_table[i]);
    }
    idt_set(0, idt_zero);
    idt_set(0x80, isr80h_wrapper);

    for (int i = 0; i < 0x20; i++)
    {
        idt_register_interrupt_callback(i, idt_handle_exception);
    }

    idt_register_interrupt_callback(0x20, idt_clock);
    // Load the interrupt descriptor table
    idt_load(&idtr_descriptor);
}

int idt_register_interrupt_callback(int interrupt, INTERRUPT_CALLBACK_FUNCTION interrupt_callback)
{
    int res = MARROWOS_ALL_OK;
    if (interrupt < 0 || interrupt >= MARROWOS_TOTAL_INTERRUPTS)
    {
        return -EINVARG;
    }
    interrupt_callbacks[interrupt] = interrupt_callback;
    return res;
}

void isr80h_register_command(int command_id, ISR80H_COMMAND command)
{
    if (command_id < 0 || command_id >= MARROWOS_MAX_ISR80H_COMMANDS)
    {
        panic("The command is out of bounds\n");
    }

    if (isr80h_commands[command_id])
    {
        panic("You are attempting to overwrite an existing command\n");
    }

    isr80h_commands[command_id] = command;
}

void *isr80h_handle_command(int command, struct interrupt_frame *frame)
{
    void *result = 0;
    if (command < 0 || command >= MARROWOS_MAX_ISR80H_COMMANDS)
    {
        // Invalid command
        return 0;
    }

    ISR80H_COMMAND command_func = isr80h_commands[command];
    if (!command_func)
    {
        return 0;
    }
    result = command_func(frame);
    return result;
}

void *isr80h_handler(int command, struct interrupt_frame *frame)
{
    void *res = 0;
    kernel_page();

    task_current_save_state(frame);
    res = isr80h_handle_command(command, frame);

    task_page();
    return res;
}
