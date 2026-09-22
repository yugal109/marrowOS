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
#define XHCI_TRB_TYPE_SETUP_STAGE 2
#define XHCI_TRB_TYPE_DATA_STAGE 3
#define XHCI_TRB_TYPE_STATUS_STAGE 4
#define XHCI_TRB_TYPE_ENABLE_SLOT_CMD 9
#define XHCI_TRB_TYPE_ADDRESS_DEVICE_CMD 11
#define XHCI_TRB_TYPE_EVALUATE_CONTEXT_CMD 13
#define XHCI_TRB_TYPE_NOOP_CMD 23
#define XHCI_TRB_TYPE_TRANSFER_EVENT 32
#define XHCI_TRB_TYPE_CMD_COMPLETION 33
#define XHCI_TRB_TYPE_PORT_STATUS_CHANGE 34

#define XHCI_TRB_CTRL_CH (1u << 4)
#define XHCI_TRB_CTRL_IOC (1u << 5)
#define XHCI_TRB_CTRL_IDT (1u << 6)
#define XHCI_TRB_CTRL_DIR_IN (1u << 16)

#define XHCI_PORTSC_PED (1u << 1)
#define XHCI_PORTSC_PR (1u << 4)
#define XHCI_PORTSC_CSC (1u << 17)
#define XHCI_PORTSC_PRC (1u << 21)
#define XHCI_PORTSC_SPEED_SHIFT 10
#define XHCI_PORTSC_SPEED_MASK (0xFu << XHCI_PORTSC_SPEED_SHIFT)

// RW1CS bits (CSC, PEC, WRC, OCC, PRC, PLC, CEC) plus PED (RW1C to disable)
// must be masked out of a read-modify-write, or writing them back as 1
// clears change flags or disables the port by accident.
#define XHCI_PORTSC_WRITE_MASK (~(uint32_t)(XHCI_PORTSC_PED | XHCI_PORTSC_CSC | (1u << 18) | (1u << 19) | (1u << 20) | XHCI_PORTSC_PRC | (1u << 22) | (1u << 23)))

#define USB_REQ_GET_DESCRIPTOR 6
#define USB_REQ_SET_CONFIGURATION 9
#define USB_DESC_DEVICE 1
#define USB_DESC_CONFIGURATION 2
#define USB_DESC_TYPE_INTERFACE 4

#define XHCI_CMD_RING_TRBS 32
#define XHCI_EVT_RING_TRBS 32
#define XHCI_EP0_RING_TRBS 16
#define XHCI_MAX_TRACKED_SLOTS 16

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

union xhci_setup_packet
{
    struct
    {
        uint8_t bm_request_type;
        uint8_t b_request;
        uint16_t w_value;
        uint16_t w_index;
        uint16_t w_length;
    } fields;
    uint64_t raw;
};

struct usb_device_descriptor
{
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
} __attribute__((packed));

// Per-device-slot state, indexed by the Slot ID the controller assigned.
struct xhci_slot_state
{
    bool in_use;
    uint8_t port; // 0-based
    uint8_t context_size;

    struct xhci_trb *ep0_ring;
    uint32_t ep0_enqueue;
    uint8_t ep0_cycle;

    uint8_t *input_ctx;
    uint8_t *output_ctx;
};

static struct xhci_hc g_xhci;
static struct xhci_slot_state g_xhci_slots[XHCI_MAX_TRACKED_SLOTS];
static uint8_t g_xhci_context_size = 32;

static void ctx_set_dword(uint8_t *ctx_base, size_t dword_index, uint32_t value)
{
    ((uint32_t *)ctx_base)[dword_index] = value;
}

static uint32_t ctx_get_dword(uint8_t *ctx_base, size_t dword_index)
{
    return ((uint32_t *)ctx_base)[dword_index];
}

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

// Consumes one event TRB of the given type from the event ring, updating
// the dequeue pointer either way so unrelated events don't jam the ring.
static bool xhci_poll_event(uint32_t want_type, uint32_t *completion_code_out, uint32_t *slot_id_out)
{
    for (int i = 0; i < 5000000; i++)
    {
        struct xhci_trb *evt = &g_xhci.evt_ring[g_xhci.evt_dequeue];
        uint32_t cycle_bit = evt->control & XHCI_TRB_CYCLE;
        if ((cycle_bit != 0) != (g_xhci.evt_cycle != 0))
        {
            __asm__ __volatile__("pause");
            continue;
        }

        uint32_t type = XHCI_TRB_TYPE_OF(evt->control);
        uint32_t completion_code = (evt->status >> 24) & 0xFF;
        uint32_t slot_id = (evt->control >> 24) & 0xFF;

        g_xhci.evt_dequeue++;
        if (g_xhci.evt_dequeue == XHCI_EVT_RING_TRBS)
        {
            g_xhci.evt_dequeue = 0;
            g_xhci.evt_cycle ^= 1;
        }

        uint64_t erdp = ((uint64_t)(uintptr_t)&g_xhci.evt_ring[g_xhci.evt_dequeue]) | (1u << 3);
        xhci_ir_write64(XHCI_IR_ERDP, erdp);

        if (type == want_type)
        {
            if (completion_code_out)
            {
                *completion_code_out = completion_code;
            }
            if (slot_id_out)
            {
                *slot_id_out = slot_id;
            }
            return true;
        }
        // Not the event we're waiting for (e.g. a leftover Port Status
        // Change event from the reset) — keep polling.
    }

    return false;
}

static void xhci_enqueue_command(uint64_t parameter, uint32_t status, uint32_t control_type)
{
    struct xhci_trb *trb = &g_xhci.cmd_ring[g_xhci.cmd_enqueue];
    trb->parameter = parameter;
    trb->status = status;
    trb->control = control_type | (g_xhci.cmd_cycle ? XHCI_TRB_CYCLE : 0);

    xhci_cmd_ring_advance();

    g_xhci.db[0] = 0;
}

static void xhci_portsc_set_bit(int port_index, uint32_t bit_to_set)
{
    volatile uint32_t *portsc = (volatile uint32_t *)(g_xhci.op + XHCI_OP_PORTSC_BASE + port_index * XHCI_OP_PORT_STRIDE);
    uint32_t val = *portsc;
    val &= XHCI_PORTSC_WRITE_MASK;
    val |= bit_to_set;
    *portsc = val;
}

// Resets the port, which is required before it leaves the Polling link
// state and becomes usable. Reports the negotiated speed on success.
static bool xhci_port_reset(int port_index, uint8_t *speed_out)
{
    xhci_portsc_set_bit(port_index, XHCI_PORTSC_PR);

    volatile uint32_t *portsc = (volatile uint32_t *)(g_xhci.op + XHCI_OP_PORTSC_BASE + port_index * XHCI_OP_PORT_STRIDE);
    bool saw_prc = false;
    for (int i = 0; i < 5000000; i++)
    {
        if (*portsc & XHCI_PORTSC_PRC)
        {
            saw_prc = true;
            xhci_portsc_set_bit(port_index, XHCI_PORTSC_PRC);
            break;
        }
        __asm__ __volatile__("pause");
    }

    if (!saw_prc)
    {
        print("xHCI: port reset timed out\n");
        return false;
    }

    uint32_t val = *portsc;
    if (!(val & XHCI_PORTSC_PED))
    {
        print("xHCI: port did not enable after reset\n");
        return false;
    }

    if (speed_out)
    {
        *speed_out = (uint8_t)((val & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT);
    }

    return true;
}

static int xhci_cmd_enable_slot()
{
    xhci_enqueue_command(0, 0, XHCI_TRB_TYPE(XHCI_TRB_TYPE_ENABLE_SLOT_CMD));

    uint32_t code = 0;
    uint32_t slot_id = 0;
    if (!xhci_poll_event(XHCI_TRB_TYPE_CMD_COMPLETION, &code, &slot_id))
    {
        print("xHCI: Enable Slot timed out\n");
        return -1;
    }
    if (code != 1)
    {
        print("xHCI: Enable Slot failed, code=");
        print(itoa(code));
        print("\n");
        return -1;
    }

    print("xHCI: Enable Slot ok, slot=");
    print(itoa(slot_id));
    print("\n");

    return (int)slot_id;
}

static void xhci_ep0_ring_init(struct xhci_slot_state *slot)
{
    slot->ep0_ring = kzalloc(XHCI_EP0_RING_TRBS * sizeof(struct xhci_trb));
    slot->ep0_enqueue = 0;
    slot->ep0_cycle = 1;

    struct xhci_trb *link = &slot->ep0_ring[XHCI_EP0_RING_TRBS - 1];
    link->parameter = (uint64_t)(uintptr_t)slot->ep0_ring;
    link->status = 0;
    link->control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_LINK) | XHCI_TRB_TOGGLE_CYCLE | XHCI_TRB_CYCLE;
}

// Builds the Input Context (Slot + EP0), points DCBAA at a fresh Output
// Context for this slot, and issues Address Device — this is what makes
// the controller actually send the device a real USB address.
static bool xhci_cmd_address_device(struct xhci_slot_state *slot, int slot_id, uint8_t root_port_number, uint8_t speed, uint16_t ep0_max_packet)
{
    size_t cs = g_xhci_context_size;
    slot->context_size = (uint8_t)cs;

    slot->input_ctx = kzalloc(3 * cs); // input control + slot + ep0
    slot->output_ctx = kzalloc(2 * cs); // slot + ep0

    xhci_ep0_ring_init(slot);

    uint8_t *input_ctrl = slot->input_ctx;
    uint8_t *slot_ctx = slot->input_ctx + cs;
    uint8_t *ep0_ctx = slot->input_ctx + 2 * cs;

    // Add Context Flags: A0 (Slot Context) and A1 (EP0 Context).
    ctx_set_dword(input_ctrl, 1, (1u << 0) | (1u << 1));

    // Slot Context DWORD0: Route String=0 (direct attach), Speed, Context Entries=1.
    ctx_set_dword(slot_ctx, 0, ((uint32_t)speed << 20) | (1u << 27));
    // DWORD1: Root Hub Port Number.
    ctx_set_dword(slot_ctx, 1, (uint32_t)root_port_number << 16);

    // EP0 Context DWORD1: CErr=3, EP Type=4 (Control), Max Packet Size.
    ctx_set_dword(ep0_ctx, 1, (3u << 1) | (4u << 3) | ((uint32_t)ep0_max_packet << 16));
    // DWORD2/3: TR Dequeue Pointer | DCS (matches ep0_cycle=1 above).
    uint64_t tr_dq = (uint64_t)(uintptr_t)slot->ep0_ring | 1;
    ctx_set_dword(ep0_ctx, 2, (uint32_t)(tr_dq & 0xFFFFFFFFu));
    ctx_set_dword(ep0_ctx, 3, (uint32_t)(tr_dq >> 32));
    // DWORD4: Average TRB Length — must be non-zero; 8 matches our setup packets.
    ctx_set_dword(ep0_ctx, 4, 8);

    g_xhci.dcbaa[slot_id] = (uint64_t)(uintptr_t)slot->output_ctx;

    uint32_t control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_ADDRESS_DEVICE_CMD) | ((uint32_t)slot_id << 24);
    xhci_enqueue_command((uint64_t)(uintptr_t)slot->input_ctx, 0, control);

    uint32_t code = 0;
    uint32_t got_slot = 0;
    if (!xhci_poll_event(XHCI_TRB_TYPE_CMD_COMPLETION, &code, &got_slot))
    {
        print("xHCI: Address Device timed out\n");
        return false;
    }
    if (code != 1)
    {
        print("xHCI: Address Device failed, code=");
        print(itoa(code));
        print("\n");
        return false;
    }

    print("xHCI: Address Device ok\n");
    return true;
}

// Updates EP0's Max Packet Size after learning the real value from the
// device's first 8 bytes of descriptor, via Evaluate Context.
static bool xhci_cmd_evaluate_context_ep0_packet_size(struct xhci_slot_state *slot, int slot_id, uint16_t real_max_packet)
{
    size_t cs = slot->context_size;
    uint8_t *input_ctrl = slot->input_ctx;
    uint8_t *ep0_ctx = slot->input_ctx + 2 * cs;

    memset(input_ctrl, 0, cs);
    ctx_set_dword(input_ctrl, 1, (1u << 1)); // A1 = EP0 context only

    uint32_t ep1_dword = ctx_get_dword(ep0_ctx, 1);
    ep1_dword = (ep1_dword & 0x0000FFFFu) | ((uint32_t)real_max_packet << 16);
    ctx_set_dword(ep0_ctx, 1, ep1_dword);

    uint32_t control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_EVALUATE_CONTEXT_CMD) | ((uint32_t)slot_id << 24);
    xhci_enqueue_command((uint64_t)(uintptr_t)slot->input_ctx, 0, control);

    uint32_t code = 0;
    uint32_t got_slot = 0;
    if (!xhci_poll_event(XHCI_TRB_TYPE_CMD_COMPLETION, &code, &got_slot) || code != 1)
    {
        print("xHCI: Evaluate Context failed\n");
        return false;
    }

    return true;
}

// Setup + optional Data + Status stage TRBs on this slot's EP0 ring, then
// waits for the Transfer Event the Status stage's IOC bit requests.
static bool xhci_control_transfer(struct xhci_slot_state *slot, int slot_id, uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, uint16_t w_length, void *buf, bool data_in)
{
    struct xhci_trb *ring = slot->ep0_ring;

    union xhci_setup_packet setup_packet;
    setup_packet.fields.bm_request_type = bm_request_type;
    setup_packet.fields.b_request = b_request;
    setup_packet.fields.w_value = w_value;
    setup_packet.fields.w_index = w_index;
    setup_packet.fields.w_length = w_length;

    struct xhci_trb *setup = &ring[slot->ep0_enqueue];
    setup->parameter = setup_packet.raw;
    setup->status = 8;
    uint32_t trt = w_length == 0 ? 0 : (data_in ? 3u : 2u);
    setup->control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_SETUP_STAGE) | XHCI_TRB_CTRL_IDT | (trt << 16) | (slot->ep0_cycle ? XHCI_TRB_CYCLE : 0);

    slot->ep0_enqueue++;
    if (slot->ep0_enqueue == (uint32_t)(XHCI_EP0_RING_TRBS - 1))
    {
        slot->ep0_enqueue = 0;
        slot->ep0_cycle ^= 1;
    }

    if (w_length > 0)
    {
        struct xhci_trb *data = &ring[slot->ep0_enqueue];
        data->parameter = (uint64_t)(uintptr_t)buf;
        data->status = w_length;
        data->control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_DATA_STAGE) | (data_in ? XHCI_TRB_CTRL_DIR_IN : 0) | (slot->ep0_cycle ? XHCI_TRB_CYCLE : 0);

        slot->ep0_enqueue++;
        if (slot->ep0_enqueue == (uint32_t)(XHCI_EP0_RING_TRBS - 1))
        {
            slot->ep0_enqueue = 0;
            slot->ep0_cycle ^= 1;
        }
    }

    // Status stage direction is the opposite of the data stage (IN if there
    // was no data stage, or the data stage was OUT).
    bool status_dir_in = w_length == 0 || !data_in;
    struct xhci_trb *status = &ring[slot->ep0_enqueue];
    status->parameter = 0;
    status->status = 0;
    status->control = XHCI_TRB_TYPE(XHCI_TRB_TYPE_STATUS_STAGE) | XHCI_TRB_CTRL_IOC | (status_dir_in ? XHCI_TRB_CTRL_DIR_IN : 0) | (slot->ep0_cycle ? XHCI_TRB_CYCLE : 0);

    slot->ep0_enqueue++;
    if (slot->ep0_enqueue == (uint32_t)(XHCI_EP0_RING_TRBS - 1))
    {
        slot->ep0_enqueue = 0;
        slot->ep0_cycle ^= 1;
    }

    // Doorbell target 1 is always the default control endpoint (EP0).
    g_xhci.db[slot_id] = 1;

    uint32_t code = 0;
    if (!xhci_poll_event(XHCI_TRB_TYPE_TRANSFER_EVENT, &code, NULL))
    {
        print("xHCI: control transfer timed out\n");
        return false;
    }

    // 1 = Success, 13 = Short Packet (fine — we often ask for more than a
    // descriptor actually contains, e.g. requesting 18 bytes for an 8-byte probe).
    return code == 1 || code == 13;
}

static bool xhci_get_descriptor(struct xhci_slot_state *slot, int slot_id, uint8_t desc_type, uint8_t desc_index, void *buf, uint16_t len)
{
    uint16_t w_value = ((uint16_t)desc_type << 8) | desc_index;
    return xhci_control_transfer(slot, slot_id, 0x80, USB_REQ_GET_DESCRIPTOR, w_value, 0, len, buf, true);
}

static bool xhci_set_configuration(struct xhci_slot_state *slot, int slot_id, uint8_t config_value)
{
    return xhci_control_transfer(slot, slot_id, 0x00, USB_REQ_SET_CONFIGURATION, config_value, 0, 0, NULL, false);
}

static void xhci_print_config_interfaces(uint8_t *cfg, uint16_t total_len)
{
    size_t off = 0;
    while (off + 2 <= total_len)
    {
        uint8_t b_length = cfg[off];
        uint8_t b_type = cfg[off + 1];
        if (b_length == 0)
        {
            break;
        }

        if (b_type == USB_DESC_TYPE_INTERFACE && off + 9 <= total_len)
        {
            print("xHCI:   interface ");
            print(itoa(cfg[off + 2]));
            print(" class=");
            print(itoa(cfg[off + 5]));
            print(" subclass=");
            print(itoa(cfg[off + 6]));
            print(" protocol=");
            print(itoa(cfg[off + 7]));
            print("\n");
        }

        off += b_length;
    }
}

// Full phase 2 sequence for one already-connected port: reset, slot, address,
// read descriptors, set configuration.
static void xhci_enumerate_port(int port_index)
{
    print("xHCI: enumerating port ");
    print(itoa(port_index + 1));
    print("\n");

    uint8_t speed = 0;
    if (!xhci_port_reset(port_index, &speed))
    {
        return;
    }

    int slot_id = xhci_cmd_enable_slot();
    if (slot_id <= 0 || slot_id >= XHCI_MAX_TRACKED_SLOTS)
    {
        print("xHCI: slot id out of tracked range\n");
        return;
    }

    struct xhci_slot_state *slot = &g_xhci_slots[slot_id];
    memset(slot, 0, sizeof(*slot));
    slot->in_use = true;
    slot->port = (uint8_t)port_index;

    // 8 works as an initial EP0 max packet size for every USB speed; we
    // correct it below once the device tells us its real value.
    if (!xhci_cmd_address_device(slot, slot_id, (uint8_t)(port_index + 1), speed, 8))
    {
        return;
    }

    uint8_t desc_buf[18];
    if (!xhci_get_descriptor(slot, slot_id, USB_DESC_DEVICE, 0, desc_buf, 8))
    {
        print("xHCI: failed to read first 8 bytes of device descriptor\n");
        return;
    }

    uint8_t real_max_packet = desc_buf[7]; // bMaxPacketSize0
    if (real_max_packet != 8)
    {
        if (!xhci_cmd_evaluate_context_ep0_packet_size(slot, slot_id, real_max_packet))
        {
            return;
        }
    }

    if (!xhci_get_descriptor(slot, slot_id, USB_DESC_DEVICE, 0, desc_buf, 18))
    {
        print("xHCI: failed to read full device descriptor\n");
        return;
    }

    struct usb_device_descriptor *dd = (struct usb_device_descriptor *)desc_buf;
    print("xHCI: device VID=");
    print(xhci_hex32(dd->idVendor));
    print(" PID=");
    print(xhci_hex32(dd->idProduct));
    print(" class=");
    print(itoa(dd->bDeviceClass));
    print(" numConfigs=");
    print(itoa(dd->bNumConfigurations));
    print("\n");

    uint8_t cfg_head[9];
    if (!xhci_get_descriptor(slot, slot_id, USB_DESC_CONFIGURATION, 0, cfg_head, 9))
    {
        print("xHCI: failed to read configuration descriptor header\n");
        return;
    }

    uint16_t total_len = cfg_head[2] | ((uint16_t)cfg_head[3] << 8);
    uint8_t config_value = cfg_head[5];

    if (total_len < 9 || total_len > 256)
    {
        print("xHCI: implausible config wTotalLength\n");
        return;
    }

    uint8_t *cfg_full = kzalloc(total_len);
    if (!xhci_get_descriptor(slot, slot_id, USB_DESC_CONFIGURATION, 0, cfg_full, total_len))
    {
        print("xHCI: failed to read full configuration descriptor\n");
        return;
    }

    xhci_print_config_interfaces(cfg_full, total_len);

    if (!xhci_set_configuration(slot, slot_id, config_value))
    {
        print("xHCI: Set Configuration failed\n");
        return;
    }

    print("xHCI: Set Configuration ok, value=");
    print(itoa(config_value));
    print("\n");
}

static void xhci_enumerate_connected_ports()
{
    for (int port = 0; port < g_xhci.max_ports; port++)
    {
        uint32_t portsc = *(volatile uint32_t *)(g_xhci.op + XHCI_OP_PORTSC_BASE + port * XHCI_OP_PORT_STRIDE);
        if (portsc & XHCI_PORTSC_CCS)
        {
            xhci_enumerate_port(port);
        }
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
    g_xhci_context_size = (hccparams1 & (1u << 2)) ? 64 : 32;

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
    xhci_enumerate_connected_ports();

    return 0;
}
