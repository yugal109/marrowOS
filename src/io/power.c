#include "power.h"
#include "io/io.h"
#include "io/pci.h"
#include "io/cpuid.h"
#include <stdbool.h>
#include <stdint.h>

// Reset Control register: latch the type (full), then start the reset
#define RESET_CONTROL_PORT 0xCF9
#define RESET_CONTROL_FULL 0x02
#define RESET_CONTROL_START 0x04
// 8042 keyboard controller: pulses the CPU reset line
#define KBC_COMMAND_PORT 0x64
#define KBC_STATUS_INPUT_FULL 0x02
#define KBC_PULSE_RESET 0xFE

// Intel chipsets: the LPC bridge (00:1f.0) holds the ACPI I/O base, and the
// PM1 control register there starts the sleep transition
#define PCI_VENDOR_INTEL 0x8086
#define LPC_BUS 0
#define LPC_SLOT 31
#define LPC_FUNC 0
#define LPC_ACPI_BASE_REG 0x40
#define LPC_ACPI_BASE_IO_SPACE 0x1
#define LPC_ACPI_BASE_MASK 0xFF80
#define PM1_CNT_OFFSET 0x04
#define PM1_CNT_SLP_TYP_SHIFT 10
#define PM1_CNT_SLP_EN (1u << 13)
// Soft-off (S5) sleep type: 7 on Intel chipsets, 0 on QEMU
#define SLP_TYP_S5_INTEL 7
#define SLP_TYP_S5_QEMU 0
// PM1 control register in QEMU's default power management block
#define QEMU_PM1_CNT_PORT 0x604

static void power_outw(uint16_t port, uint16_t value)
{
    __asm__ volatile("outw %0, %1" ::"a"(value), "Nd"(port));
}

static void power_delay()
{
    for (volatile int i = 0; i < 1000000; i++)
    {
        // delay
    }
}

static bool power_running_in_vm()
{
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    return (ecx & (1u << 31)) != 0;
}

void system_reboot()
{
    __asm__ volatile("cli");

    outb(RESET_CONTROL_PORT, RESET_CONTROL_FULL);
    outb(RESET_CONTROL_PORT, RESET_CONTROL_FULL | RESET_CONTROL_START);
    power_delay();

    // Chipset reset ignored: try the keyboard controller
    for (int i = 0; i < 100000 && (insb(KBC_COMMAND_PORT) & KBC_STATUS_INPUT_FULL); i++)
    {
        // wait for the controller's input buffer to drain
    }
    outb(KBC_COMMAND_PORT, KBC_PULSE_RESET);
    power_delay();

    // Last resort: an interrupt with an empty IDT triple-faults the CPU
    struct
    {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) empty_idt = {0, 0};
    __asm__ volatile("lidt %0; int3" ::"m"(empty_idt));

    while (1)
    {
        __asm__ volatile("hlt");
    }
}

int system_poweroff()
{
    bool in_vm = power_running_in_vm();
    if (in_vm)
    {
        power_outw(QEMU_PM1_CNT_PORT, PM1_CNT_SLP_EN | (SLP_TYP_S5_QEMU << PM1_CNT_SLP_TYP_SHIFT));
    }

    if (pci_cfg_read_word(LPC_BUS, LPC_SLOT, LPC_FUNC, 0x00) == PCI_VENDOR_INTEL)
    {
        uint32_t acpi_base = pci_cfg_read_dword(LPC_BUS, LPC_SLOT, LPC_FUNC, LPC_ACPI_BASE_REG);
        if (acpi_base & LPC_ACPI_BASE_IO_SPACE)
        {
            uint16_t slp_typ = in_vm ? SLP_TYP_S5_QEMU : SLP_TYP_S5_INTEL;
            power_outw((acpi_base & LPC_ACPI_BASE_MASK) + PM1_CNT_OFFSET, PM1_CNT_SLP_EN | (slp_typ << PM1_CNT_SLP_TYP_SHIFT));
        }
    }

    // The machine is gone within milliseconds if this worked
    power_delay();
    return -1;
}
