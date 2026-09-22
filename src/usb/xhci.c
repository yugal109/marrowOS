#include "xhci.h"
#include "io/pci.h"
#include "memory/paging/paging.h"
#include "memory/heap/kheap.h"
#include "memory/memory.h"
#include "kernel.h"
#include "string/string.h"
#include <stdint.h>
#include <stdbool.h>

#define XHCI_PCI_BASE_CLASS 0x0C
#define XHCI_PCI_SUBCLASS 0x03
#define XHCI_PCI_PROG_IF 0x30

// Operational register offsets (from op base = mmio + CAPLENGTH)
#define XHCI_OP_USBCMD 0x00
#define XHCI_OP_USBSTS 0x04
#define XHCI_OP_PAGESIZE 0x08
#define XHCI_OP_DNCTRL 0x14
#define XHCI_OP_CRCR 0x18
#define XHCI_OP_DCBAAP 0x30
#define XHCI_OP_CONFIG 0x38
#define XHCI_OP_PORTSC_BASE 0x400
#define XHCI_OP_PORT_STRIDE 0x10

#define XHCI_USBCMD_RUN (1u << 0)
#define XHCI_USBCMD_HCRESET (1u << 1)

#define XHCI_USBSTS_HCHALTED (1u << 0)
#define XHCI_USBSTS_HSE (1u << 2)
#define XHCI_USBSTS_CNR (1u << 11)

#define XHCI_PORTSC_CCS (1u << 0)

// Runtime register offsets (from rt base = mmio + RTSOFF)
#define XHCI_RT_MFINDEX 0x00
#define XHCI_RT_IR0 0x20 // Interrupter Register Set 0
#define XHCI_IR_IMAN 0x00
#define XHCI_IR_IMOD 0x04
#define XHCI_IR_ERSTSZ 0x08
#define XHCI_IR_ERSTBA 0x10
#define XHCI_IR_ERDP 0x18

#define XHCI_TRB_CYCLE (1u << 0)
#define XHCI_TRB_TOGGLE_CYCLE (1u << 1)
#define XHCI_TRB_TYPE_SHIFT 10
#define XHCI_TRB_TYPE(t) ((uint32_t)(t) << XHCI_TRB_TYPE_SHIFT)
#define XHCI_TRB_TYPE_OF(control) (((control) >> XHCI_TRB_TYPE_SHIFT) & 0x3F)

#define XHCI_TRB_TYPE_LINK 6
#define XHCI_TRB_TYPE_NOOP_CMD 23
#define XHCI_TRB_TYPE_CMD_COMPLETION 33
#define XHCI_TRB_TYPE_PORT_STATUS_CHANGE 34

#define XHCI_CMD_RING_TRBS 32
#define XHCI_EVT_RING_TRBS 32

struct xhci_trb
{
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
};

struct xhci_erst_entry
{
    uint64_t ring_segment_base;
    uint32_t ring_segment_size;
    uint32_t reserved;
};

struct xhci_hc
{
    struct pci_device *pci;
    volatile uint8_t *mmio;
    volatile uint8_t *op;
    volatile uint8_t *rt;
    volatile uint32_t *db;

    uint8_t max_slots;
    uint8_t max_ports;

    uint64_t *dcbaa;

    struct xhci_trb *cmd_ring;
    uint32_t cmd_enqueue;
    uint8_t cmd_cycle;

    struct xhci_trb *evt_ring;
    uint32_t evt_dequeue;
    uint8_t evt_cycle;
};

static struct xhci_hc g_xhci;

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

static uint32_t xhci_op_read32(uint32_t off)
{
    return *(volatile uint32_t *)(g_xhci.op + off);
}

static void xhci_op_write32(uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)(g_xhci.op + off) = val;
}

static uint64_t xhci_op_read64(uint32_t off)
{
    return *(volatile uint64_t *)(g_xhci.op + off);
}

static void xhci_op_write64(uint32_t off, uint64_t val)
{
    *(volatile uint64_t *)(g_xhci.op + off) = val;
}

static uint32_t xhci_ir_read32(uint32_t off)
{
    return *(volatile uint32_t *)(g_xhci.rt + XHCI_RT_IR0 + off);
}

static void xhci_ir_write32(uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)(g_xhci.rt + XHCI_RT_IR0 + off) = val;
}

static uint64_t xhci_ir_read64(uint32_t off)
{
    return *(volatile uint64_t *)(g_xhci.rt + XHCI_RT_IR0 + off);
}

static void xhci_ir_write64(uint32_t off, uint64_t val)
{
    *(volatile uint64_t *)(g_xhci.rt + XHCI_RT_IR0 + off) = val;
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

// Waits for a condition on USBSTS, ~1 second worth of spins.
static bool xhci_wait_usbsts(uint32_t mask, uint32_t want)
{
    for (int i = 0; i < 5000000; i++)
    {
        if ((xhci_op_read32(XHCI_OP_USBSTS) & mask) == want)
        {
            return true;
        }
        __asm__ __volatile__("pause");
    }
    return false;
}

static bool xhci_reset()
{
    // Stop it first, in case firmware/BIOS left it running.
    uint32_t cmd = xhci_op_read32(XHCI_OP_USBCMD);
    xhci_op_write32(XHCI_OP_USBCMD, cmd & ~XHCI_USBCMD_RUN);

    if (!xhci_wait_usbsts(XHCI_USBSTS_HCHALTED, XHCI_USBSTS_HCHALTED))
    {
        print("xHCI: timed out waiting for halt before reset\n");
        return false;
    }

    cmd = xhci_op_read32(XHCI_OP_USBCMD);
    xhci_op_write32(XHCI_OP_USBCMD, cmd | XHCI_USBCMD_HCRESET);

    for (int i = 0; i < 5000000; i++)
    {
        uint32_t c = xhci_op_read32(XHCI_OP_USBCMD);
        uint32_t s = xhci_op_read32(XHCI_OP_USBSTS);
        if (!(c & XHCI_USBCMD_HCRESET) && !(s & XHCI_USBSTS_CNR))
        {
            return true;
        }
        __asm__ __volatile__("pause");
    }

    print("xHCI: timed out waiting for reset to finish\n");
    return false;
}

static void xhci_setup_dcbaa()
{
    size_t entries = (size_t)g_xhci.max_slots + 1;
    g_xhci.dcbaa = kzalloc(entries * sizeof(uint64_t));
    xhci_op_write64(XHCI_OP_DCBAAP, (uint64_t)(uintptr_t)g_xhci.dcbaa);
}

static void xhci_setup_scratchpad(uint32_t hcsparams2)
{
    uint32_t hi = (hcsparams2 >> 21) & 0x1F;
    uint32_t lo = (hcsparams2 >> 27) & 0x1F;
    uint32_t max_scratchpad = (hi << 5) | lo;

    if (max_scratchpad == 0)
    {
        return;
    }

    uint64_t *array = kzalloc((size_t)max_scratchpad * sizeof(uint64_t));
    for (uint32_t i = 0; i < max_scratchpad; i++)
    {
        void *buf = kzalloc(0x1000);
        array[i] = (uint64_t)(uintptr_t)buf;
    }

    // DCBAA[0] is reserved for the scratchpad buffer array pointer.
    g_xhci.dcbaa[0] = (uint64_t)(uintptr_t)array;

    print("xHCI: scratchpad buffers=");
    print(itoa(max_scratchpad));
    print("\n");
}

static void xhci_setup_command_ring()
{
    g_xhci.cmd_ring = kzalloc(XHCI_CMD_RING_TRBS * sizeof(struct xhci_trb));
    g_xhci.cmd_enqueue = 0;
    g_xhci.cmd_cycle = 1;

    struct xhci_trb *link = &g_xhci.cmd_ring[XHCI_CMD_RING_TRBS - 1];
    link->parameter = (uint64_t)(uintptr_t)g_xhci.cmd_ring;
    link->status = 0;
    link->control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_LINK) | XHCI_TRB_TOGGLE_CYCLE | XHCI_TRB_CYCLE;

    // Bit 0 here is RCS (ring cycle state), matching the ring's starting cycle of 1.
    uint64_t crcr = ((uint64_t)(uintptr_t)g_xhci.cmd_ring) | 1;
    xhci_op_write64(XHCI_OP_CRCR, crcr);
}

static void xhci_setup_event_ring()
{
    g_xhci.evt_ring = kzalloc(XHCI_EVT_RING_TRBS * sizeof(struct xhci_trb));
    g_xhci.evt_dequeue = 0;
    g_xhci.evt_cycle = 1;

    struct xhci_erst_entry *erst = kzalloc(sizeof(struct xhci_erst_entry));
    erst->ring_segment_base = (uint64_t)(uintptr_t)g_xhci.evt_ring;
    erst->ring_segment_size = XHCI_EVT_RING_TRBS;
    erst->reserved = 0;

    xhci_ir_write32(XHCI_IR_ERSTSZ, 1);
    xhci_ir_write64(XHCI_IR_ERDP, (uint64_t)(uintptr_t)g_xhci.evt_ring);
    xhci_ir_write64(XHCI_IR_ERSTBA, (uint64_t)(uintptr_t)erst);
}

static bool xhci_run()
{
    uint32_t cmd = xhci_op_read32(XHCI_OP_USBCMD);
    xhci_op_write32(XHCI_OP_USBCMD, cmd | XHCI_USBCMD_RUN);

    if (!xhci_wait_usbsts(XHCI_USBSTS_HCHALTED, 0))
    {
        print("xHCI: timed out waiting for controller to start\n");
        return false;
    }

    return true;
}

static void xhci_report_ports()
{
    for (int port = 0; port < g_xhci.max_ports; port++)
    {
        uint32_t portsc = *(volatile uint32_t *)(g_xhci.op + XHCI_OP_PORTSC_BASE + port * XHCI_OP_PORT_STRIDE);
        if (portsc & XHCI_PORTSC_CCS)
        {
            print("xHCI: port ");
            print(itoa(port + 1));
            print(" connected, PORTSC=");
            print(xhci_hex32(portsc));
            print("\n");
        }
    }
}

// Advances the command ring's software enqueue index/cycle, following the
// link TRB at the end exactly like the controller will when consuming it.
static void xhci_cmd_ring_advance()
{
    g_xhci.cmd_enqueue++;
    if (g_xhci.cmd_enqueue == (uint32_t)(XHCI_CMD_RING_TRBS - 1))
    {
        g_xhci.cmd_enqueue = 0;
        g_xhci.cmd_cycle ^= 1;
    }
}

// No-Op command round trip: proves the command ring, event ring, ERST and
// doorbell are all wired correctly, independent of any real device existing.
static void xhci_test_noop_command()
{
    struct xhci_trb *trb = &g_xhci.cmd_ring[g_xhci.cmd_enqueue];
    trb->parameter = 0;
    trb->status = 0;
    trb->control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_NOOP_CMD) | (g_xhci.cmd_cycle ? XHCI_TRB_CYCLE : 0);

    xhci_cmd_ring_advance();

    // Doorbell 0, target field 0: rings the command ring.
    g_xhci.db[0] = 0;

    for (int i = 0; i < 5000000; i++)
    {
        struct xhci_trb *evt = &g_xhci.evt_ring[g_xhci.evt_dequeue];
        uint32_t cycle_bit = evt->control & XHCI_TRB_CYCLE;
        if ((cycle_bit != 0) == (g_xhci.evt_cycle != 0))
        {
            uint32_t type = XHCI_TRB_TYPE_OF(evt->control);
            if (type == XHCI_TRB_TYPE_CMD_COMPLETION)
            {
                uint32_t completion_code = (evt->status >> 24) & 0xFF;
                print("xHCI: No-Op command completed, code=");
                print(itoa(completion_code));
                print(" (1 = success)\n");
            }
            else
            {
                print("xHCI: unexpected event type=");
                print(itoa(type));
                print("\n");
            }

            g_xhci.evt_dequeue++;
            if (g_xhci.evt_dequeue == XHCI_EVT_RING_TRBS)
            {
                g_xhci.evt_dequeue = 0;
                g_xhci.evt_cycle ^= 1;
            }

            uint64_t erdp = ((uint64_t)(uintptr_t)&g_xhci.evt_ring[g_xhci.evt_dequeue]) | (1u << 3);
            xhci_ir_write64(XHCI_IR_ERDP, erdp);
            return;
        }
        __asm__ __volatile__("pause");
    }

    print("xHCI: No-Op command timed out, no completion event seen\n");
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

    memset(&g_xhci, 0, sizeof(g_xhci));
    g_xhci.pci = dev;
    g_xhci.mmio = (volatile uint8_t *)(uintptr_t)dev->bars[0].addr;

    uint8_t cap_length = g_xhci.mmio[0];
    uint32_t hcsparams1 = *(volatile uint32_t *)(g_xhci.mmio + 0x4);
    uint32_t hcsparams2 = *(volatile uint32_t *)(g_xhci.mmio + 0x8);
    uint32_t hccparams1 = *(volatile uint32_t *)(g_xhci.mmio + 0x10);
    uint32_t dboff = *(volatile uint32_t *)(g_xhci.mmio + 0x14);
    uint32_t rtsoff = *(volatile uint32_t *)(g_xhci.mmio + 0x18);

    g_xhci.op = g_xhci.mmio + cap_length;
    g_xhci.rt = g_xhci.mmio + (rtsoff & ~0x1Fu);
    g_xhci.db = (volatile uint32_t *)(g_xhci.mmio + (dboff & ~0x3u));

    g_xhci.max_slots = hcsparams1 & 0xFF;
    g_xhci.max_ports = (hcsparams1 >> 24) & 0xFF;

    print("xHCI: CAPLENGTH=");
    print(xhci_hex32(cap_length));
    print(" HCCPARAMS1=");
    print(xhci_hex32(hccparams1));
    print("\n");
    print("xHCI: MaxSlots=");
    print(itoa(g_xhci.max_slots));
    print(" MaxPorts=");
    print(itoa(g_xhci.max_ports));
    print("\n");

    if (!xhci_reset())
    {
        return -1;
    }
    print("xHCI: reset ok\n");

    uint32_t config = xhci_op_read32(XHCI_OP_CONFIG);
    config = (config & ~0xFFu) | g_xhci.max_slots;
    xhci_op_write32(XHCI_OP_CONFIG, config);

    xhci_setup_dcbaa();
    xhci_setup_scratchpad(hcsparams2);
    xhci_setup_command_ring();
    xhci_setup_event_ring();

    if (!xhci_run())
    {
        return -1;
    }
    print("xHCI: running\n");

    xhci_test_noop_command();
    xhci_report_ports();

    return 0;
}
