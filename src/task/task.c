#include "task.h"
#include "kernel.h"
#include "status.h"
#include "process.h"
#include "memory/heap/kheap.h"
#include "memory/memory.h"
#include "string/string.h"
#include "memory/paging/paging.h"
#include "loader/formats/elfloader.h"
#include "idt/idt.h"

// The current task that is running
struct task *current_task = 0;

// Task linked list
struct task *task_tail = 0;
struct task *task_head = 0;

int task_init(struct task *task, struct process *process);

struct task *task_current()
{
    return current_task;
}

struct task *task_new(struct process *process)
{
    int res = 0;
    struct task *task = kzalloc(sizeof(struct task));
    if (!task)
    {
        res = -ENOMEM;
        goto out;
    }

    res = task_init(task, process);
    if (res != MARROWOS_ALL_OK)
    {
        goto out;
    }

    if (task_head == 0)
    {
        task_head = task;
        task_tail = task;
        current_task = task;
        goto out;
    }

    task_tail->next = task;
    task->prev = task_tail;
    task_tail = task;

out:
    if (ISERR(res))
    {
        task_free(task);
        return ERROR(res);
    }

    return task;
}

struct task *task_get_next()
{
    if (!current_task->next)
    {
        return task_head;
    }

    return current_task->next;
}

static void task_list_remove(struct task *task)
{
    if (task->prev)
    {
        task->prev->next = task->next;
    }

    // Or the next task keeps a prev pointer into this freed task
    if (task->next)
    {
        task->next->prev = task->prev;
    }

    if (task == task_head)
    {
        task_head = task->next;
    }

    if (task == task_tail)
    {
        task_tail = task->prev;
    }

    // Reads task->next, so keep it above the clearing below
    if (task == current_task)
    {
        current_task = task_get_next();
    }

    task->next = NULL;
    task->prev = NULL;
}

int task_free(struct task *task)
{
    task_list_remove(task);

    // Finally free the task data
    kfree(task);
    return 0;
}

void task_next()
{
    struct task *next_task = NULL;
    do
    {
        int res = task_get_next_non_sleeping_task(&next_task);
        if (res < 0)
        {
            panic("No more tasks or task error\n");
        }
        if (!next_task)
        {
            udelay(100);
        }
    } while (!next_task);

    task_switch(next_task);
    task_return(&next_task->registers);
}

bool task_asleep(struct task *task)
{
    return task->sleeping.sleep_until_microseconds > tsc_microseconds();
}

void task_sleep(struct task *task, TIME_MICROSECONDS microseconds)
{
    task->sleeping.sleep_until_microseconds = tsc_microseconds() + microseconds;
}

// Walks the task list starting after current_task, skipping sleeping tasks,
// wrapping around once back to task_head.
int task_get_next_non_sleeping_task(struct task **task_out)
{
    int res = 0;
    struct task *_current_task = current_task ? current_task->next : task_head;
    do
    {
        if (!_current_task)
        {
            _current_task = task_head;
            if (!_current_task)
            {
                res = -EIO;
                break;
            }
        }

        if (task_asleep(_current_task))
        {
            _current_task = _current_task->next;
            continue;
        }

        res = 0;
        break;
    } while (_current_task);

    *task_out = _current_task;
    return res;
}

int task_switch(struct task *task)
{
    current_task = task;
    paging_switch(task->process->paging_desc);
    return 0;
}

struct paging_desc *task_paging_desc(struct task *task)
{
    return task->process->paging_desc;
}

struct paging_desc *task_current_paging_desc()
{
    if (!current_task)
    {
        panic("NO task yet\n");
    }

    return task_paging_desc(current_task);
}

// idt.asm saves the interrupted code's x87/SSE registers here as the first thing after an interrupt
extern uint8_t isr_fpu_scratch[TASK_FPU_STATE_SIZE];

void task_save_state(struct task *task, struct interrupt_frame *frame)
{
    memcpy(task->fpu_state, isr_fpu_scratch, TASK_FPU_STATE_SIZE);
    task->registers.ip = frame->ip;
    task->registers.cs = frame->cs;
    task->registers.flags = frame->flags;
    task->registers.rsp = frame->rsp;
    task->registers.ss = frame->ss;
    task->registers.rax = frame->rax;
    task->registers.rbp = frame->rbp;
    task->registers.rbx = frame->rbx;
    task->registers.rcx = frame->rcx;
    task->registers.rdi = frame->rdi;
    task->registers.rdx = frame->rdx;
    task->registers.rsi = frame->rsi;
}

int copy_string_from_task(struct task *task, void *virtual, void *phys, int max)
{
    if (max >= PAGING_PAGE_SIZE)
    {
        return -EINVARG;
    }

    int res = 0;
    char *tmp = kzalloc(max);
    if (!tmp)
    {
        res = -ENOMEM;
        goto out;
    }

    // L44 adds kernel_desc(); until then current CR3 is kernel while we are in kernel_page.
    void *phys_tmp = paging_get_physical_address(kernel_desc(), tmp);
    struct paging_desc *task_desc = task_paging_desc(task);
    struct paging_desc_entry old_entry;
    memcpy(&old_entry, paging_get(task_desc, phys_tmp), sizeof(struct paging_desc_entry));

    int old_entry_flags = 0;
    if (old_entry.present)
        old_entry_flags |= PAGING_IS_PRESENT;
    if (old_entry.read_write)
        old_entry_flags |= PAGING_IS_WRITEABLE;
    if (old_entry.user_supervisor)
        old_entry_flags |= PAGING_ACCESS_FROM_ALL;

    paging_map(task_desc, phys_tmp, phys_tmp, PAGING_IS_WRITEABLE | PAGING_IS_PRESENT | PAGING_ACCESS_FROM_ALL);

    // Switch to the user pages
    task_page_task(task);
    // we are now on the page tables of the task
    strncpy(tmp, virtual, max);

    // switch back to kernel page tables
    kernel_page();

    strncpy(phys, tmp, max);

    // Remap back to what it was before.
    paging_map(task_desc, phys_tmp, (void *)((uint64_t)(old_entry.address << 12)), old_entry_flags);
out:
    if (tmp)
    {
        kfree(tmp);
    }
    return res;
}

void task_current_save_state(struct interrupt_frame *frame)
{
    if (!task_current())
    {
        panic("No current task to save\n");
    }

    struct task *task = task_current();
    task_save_state(task, frame);
}

int task_page()
{
    user_registers();
    task_switch(current_task);
    return 0;
}

int task_page_task(struct task *task)
{
    user_registers();
    paging_switch(task_paging_desc(task));
    return 0;
}

void task_run_first_ever_task()
{
    if (!current_task)
    {
        panic("task_run_first_ever_task(): No current task exists!\n");
    }

    task_switch(task_head);
    task_return(&task_head->registers);
}

int task_init(struct task *task, struct process *process)
{
    memset(task, 0, sizeof(struct task));

    task->registers.ip = MARROWOS_PROGRAM_VIRTUAL_ADDRESS;
    if (process->filetype == PROCESS_FILE_TYPE_ELF)
    {
        // panic("Elf files not supported\n");
        task->registers.ip = elf_header(process->elf_file)->e_entry;
    }

    // Power-on defaults: x87 control word 0x037F and MXCSR 0x1F80 (all exceptions masked)
    task->fpu_state[0] = 0x7F;
    task->fpu_state[1] = 0x03;
    task->fpu_state[24] = 0x80;
    task->fpu_state[25] = 0x1F;

    task->registers.ss = USER_DATA_SEGMENT;
    task->registers.cs = USER_CODE_SEGMENT;
    task->registers.rsp = MARROWOS_PROGRAM_VIRTUAL_STACK_ADDRESS_START;
    task->process = process;

    return 0;
}

void *task_get_stack_item(struct task *task, int index)
{
    void *result = 0;

    uint64_t *sp_ptr = (uint64_t *)task->registers.rsp;

    // Switch to the given tasks page
    task_page_task(task);

    result = (void *)sp_ptr[index];

    // Switch back to the kernel page
    kernel_page();

    return result;
}

void *task_virtual_address_to_physical(struct task *task, void *virtual_address)
{
    return paging_get_physical_address(task->process->paging_desc, virtual_address);
}
