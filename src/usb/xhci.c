#include "xhci.h"
#include "io/pci.h"
#include "memory/paging/paging.h"
#include "kernel.h"
#include "string/string.h"
#include <stdint.h>

#define XHCI_PCI_BASE_CLASS 0x0C
#define XHCI_PCI_SUBCLASS 0x03
#define XHCI_PCI_PROG_IF 0x30

static char *xhci_hex32(uint32_t value)
{
    static char buf[11];
    const char *digits = "0123456789ABCDEF";
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 8; i++)
    {
        buf[2 + i] = digits[(value >> ((7 - i) * 4)) & 0xF];
    }
    buf[10] = 0;
    return buf;
}

static struct pci_device *xhci_pci_find()
{
    size_t total = pci_device_count();
    for (size_t i = 0; i < total; i++)
    {
        struct pci_device *dev = NULL;
        if (pci_device_get(i, &dev) < 0 || !dev)
        {
            continue;
        }

        if (pci_device_base_class(dev) != XHCI_PCI_BASE_CLASS || pci_device_subclass(dev) != XHCI_PCI_SUBCLASS)
        {
            continue;
        }

        uint8_t prog_if = pci_cfg_read_byte(dev->addr.bus, dev->addr.slot, dev->addr.func, PCI_HEADER_PROG_IF_OFFSET);
        if (prog_if != XHCI_PCI_PROG_IF)
        {
            continue;
        }

        return dev;
    }

    return NULL;
}

static void xhci_map_mmio(struct pci_device *dev)
{
    uintptr_t base = (uintptr_t)dev->bars[0].addr;
    uint64_t size = dev->bars[0].size ? dev->bars[0].size : 0x4000;
    const int flags = PAGING_IS_PRESENT | PAGING_IS_WRITEABLE | PAGING_CACHE_DISABLED;

    for (uint64_t off = 0; off < size; off += 0x1000)
    {
        paging_map(kernel_desc(), (void *)(base + off), (void *)(base + off), flags);
    }
}

int xhci_init()
{
    struct pci_device *dev = xhci_pci_find();
    if (!dev)
    {
        print("xHCI: no controller found\n");
        return -1;
    }

    print("xHCI: controller found bus=");
    print(itoa(dev->addr.bus));
    print(" slot=");
    print(itoa(dev->addr.slot));
    print(" func=");
    print(itoa(dev->addr.func));
    print("\n");

    if (dev->bars[0].addr == 0 || dev->bars[0].type != PCI_DEVICE_IO_MEMORY)
    {
        print("xHCI: BAR0 is not a valid MMIO region\n");
        return -1;
    }

    pci_enable_bus_master(dev);
    xhci_map_mmio(dev);

    volatile uint8_t *mmio = (volatile uint8_t *)(uintptr_t)dev->bars[0].addr;

    uint8_t cap_length = mmio[0];
    uint16_t hci_version = *(volatile uint16_t *)(mmio + 2);
    uint32_t hcsparams1 = *(volatile uint32_t *)(mmio + 4);
    uint32_t hcsparams2 = *(volatile uint32_t *)(mmio + 8);
    uint32_t hcsparams3 = *(volatile uint32_t *)(mmio + 0xC);
    uint32_t hccparams1 = *(volatile uint32_t *)(mmio + 0x10);
    uint32_t dboff = *(volatile uint32_t *)(mmio + 0x14);
    uint32_t rtsoff = *(volatile uint32_t *)(mmio + 0x18);

    print("xHCI: CAPLENGTH=");
    print(xhci_hex32(cap_length));
    print(" HCIVERSION=");
    print(xhci_hex32(hci_version));
    print("\n");

    print("xHCI: HCSPARAMS1=");
    print(xhci_hex32(hcsparams1));
    print(" HCSPARAMS2=");
    print(xhci_hex32(hcsparams2));
    print(" HCSPARAMS3=");
    print(xhci_hex32(hcsparams3));
    print("\n");

    print("xHCI: HCCPARAMS1=");
    print(xhci_hex32(hccparams1));
    print(" DBOFF=");
    print(xhci_hex32(dboff));
    print(" RTSOFF=");
    print(xhci_hex32(rtsoff));
    print("\n");

    uint8_t max_slots = hcsparams1 & 0xFF;
    uint16_t max_intrs = (hcsparams1 >> 8) & 0x7FF;
    uint8_t max_ports = (hcsparams1 >> 24) & 0xFF;

    print("xHCI: MaxSlots=");
    print(itoa(max_slots));
    print(" MaxIntrs=");
    print(itoa(max_intrs));
    print(" MaxPorts=");
    print(itoa(max_ports));
    print("\n");

    return 0;
}
