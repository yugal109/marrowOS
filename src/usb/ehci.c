#include "ehci.h"
#include "io/pci.h"
#include "io/tsc.h"
#include "memory/paging/paging.h"
#include "memory/heap/kheap.h"
#include "memory/memory.h"
#include "kernel.h"
#include "string/string.h"
#include "keyboard/usbhid.h"
#include "mouse/usbhid_mouse.h"
#include "hidreport.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define EHCI_PCI_BASE_CLASS 0x0C
#define EHCI_PCI_SUBCLASS 0x03
#define EHCI_PCI_PROG_IF 0x20

// Capability registers (from the BAR0 base)
#define EHCI_CAP_CAPLENGTH 0x00
#define EHCI_CAP_HCSPARAMS 0x04
#define EHCI_CAP_HCCPARAMS 0x08

// Operational registers (from BAR0 + CAPLENGTH)
#define EHCI_OP_USBCMD 0x00
#define EHCI_OP_USBSTS 0x04
#define EHCI_OP_USBINTR 0x08
#define EHCI_OP_CTRLDSSEGMENT 0x10
#define EHCI_OP_PERIODICLISTBASE 0x14
#define EHCI_OP_ASYNCLISTADDR 0x18
#define EHCI_OP_CONFIGFLAG 0x40
#define EHCI_OP_PORTSC_BASE 0x44

#define EHCI_USBCMD_RS (1u << 0)
#define EHCI_USBCMD_HCRESET (1u << 1)
#define EHCI_USBCMD_PSE (1u << 4)
#define EHCI_USBCMD_ASE (1u << 5)
#define EHCI_USBCMD_IAAD (1u << 6)
#define EHCI_USBCMD_ITC_8 (8u << 16)

#define EHCI_USBSTS_IAA (1u << 5)
#define EHCI_USBSTS_HCHALTED (1u << 12)
#define EHCI_USBSTS_ALL_ACK 0x3Fu

#define EHCI_PORTSC_CCS (1u << 0)
#define EHCI_PORTSC_CSC (1u << 1)
#define EHCI_PORTSC_PE (1u << 2)
#define EHCI_PORTSC_PEC (1u << 3)
#define EHCI_PORTSC_OCC (1u << 5)
#define EHCI_PORTSC_PR (1u << 8)
#define EHCI_PORTSC_LINE_SHIFT 10
#define EHCI_PORTSC_LINE_MASK (3u << EHCI_PORTSC_LINE_SHIFT)
#define EHCI_PORTSC_PP (1u << 12)
// Write-1-to-clear bits: mask out of read-modify-writes
#define EHCI_PORTSC_RW1C (EHCI_PORTSC_CSC | EHCI_PORTSC_PEC | EHCI_PORTSC_OCC)
#define EHCI_PORTSC_LINE_K_STATE 1

#define EHCI_HCSPARAMS_N_PORTS(x) ((x) & 0xF)
#define EHCI_HCSPARAMS_PPC (1u << 4)
#define EHCI_HCCPARAMS_64BIT (1u << 0)
#define EHCI_HCCPARAMS_EECP(x) (((x) >> 8) & 0xFF)

#define EHCI_LEGSUP_CAP_ID 1
#define EHCI_LEGSUP_BIOS_OWNED (1u << 16)
#define EHCI_LEGSUP_OS_OWNED (1u << 24)
// USBLEGCTLSTS: disable SMIs, ack status
#define EHCI_LEGCTLSTS_DISABLE_AND_ACK 0xE0000000u

#define EHCI_PTR_TERMINATE 1u
#define EHCI_PTR_TYPE_QH (1u << 1)

#define EHCI_QTD_STATUS_ACTIVE (1u << 7)
#define EHCI_QTD_STATUS_HALTED (1u << 6)
#define EHCI_QTD_PID_OUT 0u
#define EHCI_QTD_PID_IN 1u
#define EHCI_QTD_PID_SETUP 2u
#define EHCI_QTD_TOKEN(pid, bytes, toggle, ioc) \
    (EHCI_QTD_STATUS_ACTIVE | ((uint32_t)(pid) << 8) | (3u << 10) | ((ioc) ? (1u << 15) : 0) | ((uint32_t)(bytes) << 16) | ((toggle) ? (1u << 31) : 0))
#define EHCI_QTD_BYTES_LEFT(token) (((token) >> 16) & 0x7FFF)

#define EHCI_QH_EP_CHAR_DTC (1u << 14)
#define EHCI_QH_EP_CHAR_HEAD (1u << 15)
#define EHCI_QH_EP_CHAR_CONTROL (1u << 27)
#define EHCI_QH_EP_CAPS_MULT_1 (1u << 30)

// Endpoint speed (EPS) encoding used by queue heads
#define EHCI_SPEED_FULL 0
#define EHCI_SPEED_LOW 1
#define EHCI_SPEED_HIGH 2

#define USB_REQ_GET_STATUS 0
#define USB_REQ_SET_FEATURE 3
#define USB_REQ_CLEAR_FEATURE 1
#define USB_REQ_SET_ADDRESS 5
#define USB_REQ_GET_DESCRIPTOR 6
#define USB_DESC_HUB 0x29

// Hub class port features and wPortStatus/wPortChange bits
#define HUB_FEAT_PORT_RESET 4
#define HUB_FEAT_PORT_POWER 8
#define HUB_FEAT_C_PORT_CONNECTION 16
#define HUB_FEAT_C_PORT_RESET 20
#define HUB_PORT_STATUS_CONNECTION (1u << 0)
#define HUB_PORT_STATUS_ENABLE (1u << 1)
#define HUB_PORT_STATUS_RESET (1u << 4)
#define HUB_PORT_STATUS_LOW_SPEED (1u << 9)
#define HUB_PORT_STATUS_HIGH_SPEED (1u << 10)
#define EHCI_MAX_HUB_DEPTH 4
#define USB_REQ_SET_CONFIGURATION 9
#define USB_REQ_SET_PROTOCOL 0x0B
#define USB_DESC_DEVICE 1
#define USB_DESC_CONFIGURATION 2
#define USB_DESC_INTERFACE 4
#define USB_DESC_ENDPOINT 5
#define USB_DESC_HID 0x21
#define USB_DESC_HID_REPORT 0x22
#define USB_CLASS_HID 3
#define USB_CLASS_HUB 9
#define HID_SUBCLASS_BOOT 1
#define HID_PROTOCOL_KEYBOARD 1
#define HID_PROTOCOL_MOUSE 2

#define EHCI_MAX_CONTROLLERS 4
#define EHCI_MAX_HID_DEVICES 8
#define EHCI_FRAME_LIST_ENTRIES 1024
#define EHCI_CONTROL_DATA_MAX 512
#define EHCI_DMA_POOL_SIZE (128 * 1024)

struct ehci_qtd
{
    volatile uint32_t next;
    volatile uint32_t alt_next;
    volatile uint32_t token;
    volatile uint32_t buffer[5];
    volatile uint32_t buffer_hi[5];
    uint32_t reserved[3];
};
_Static_assert(sizeof(struct ehci_qtd) == 64, "qTD layout");

struct ehci_qh
{
    volatile uint32_t horiz;
    volatile uint32_t ep_char;
    volatile uint32_t ep_caps;
    volatile uint32_t current_qtd;
    // Overlay: the controller's working copy of the active qTD
    volatile uint32_t next_qtd;
    volatile uint32_t alt_next_qtd;
    volatile uint32_t token;
    volatile uint32_t buffer[5];
    volatile uint32_t buffer_hi[5];
    uint32_t reserved[15];
};
_Static_assert(sizeof(struct ehci_qh) == 128, "QH layout");

// A device's speed and, if low/full-speed behind a hub, its Transaction Translator
struct ehci_device_path
{
    uint8_t speed;
    uint8_t tt_hub_address;
    uint8_t tt_port;
};

struct ehci_controller
{
    struct pci_device *pci;
    volatile uint8_t *cap;
    volatile uint8_t *op;
    uint8_t n_ports;
    uint8_t next_address;

    volatile uint32_t *frame_list;
    struct ehci_qh *async_head;
    uint32_t periodic_chain;

    // One QH and three qTDs, shared by all control transfers
    struct ehci_qh *control_qh;
    struct ehci_qtd *control_qtd[3];
    uint8_t *setup_buf;
    uint8_t *control_data;
};

typedef void (*ehci_hid_report_handler)(const uint8_t *buf, uint32_t len);

struct ehci_hid_device
{
    bool in_use;
    struct ehci_qh *qh;
    struct ehci_qtd *qtd;
    uint8_t *report_buf;
    uint16_t max_packet;
    ehci_hid_report_handler handler;
    // Per device: each mouse has its own report layout
    bool is_mouse;
    struct hid_mouse_layout mouse_layout;
};

struct ehci_hid_ep_info
{
    bool found;
    uint8_t ep_addr;
    uint16_t max_packet;
    uint8_t interface_number;
    // 0 if the interface had no HID descriptor
    uint16_t report_desc_len;
    bool boot_capable;
};

static struct ehci_controller g_ehci[EHCI_MAX_CONTROLLERS];
static int g_ehci_count = 0;
static struct ehci_hid_device g_ehci_hid[EHCI_MAX_HID_DEVICES];

static uint8_t *g_ehci_pool = NULL;
static size_t g_ehci_pool_used = 0;

// Structures use 32-bit pointers, so allocate from one pool checked to be below 4GB
static void *ehci_dma_alloc(size_t size, size_t align)
{
    size_t offset = (g_ehci_pool_used + align - 1) & ~(align - 1);
    if (offset + size > EHCI_DMA_POOL_SIZE)
    {
        panic("EHCI: DMA pool exhausted\n");
    }
    g_ehci_pool_used = offset + size;
    return g_ehci_pool + offset;
}

static uint32_t ehci_ptr(volatile void *p)
{
    return (uint32_t)(uintptr_t)p;
}

static uint32_t ehci_op_read(struct ehci_controller *hc, uint32_t off)
{
    return *(volatile uint32_t *)(hc->op + off);
}

static void ehci_op_write(struct ehci_controller *hc, uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)(hc->op + off) = val;
}

static volatile uint32_t *ehci_portsc(struct ehci_controller *hc, int port)
{
    return (volatile uint32_t *)(hc->op + EHCI_OP_PORTSC_BASE + port * 4);
}

static bool ehci_wait_reg(volatile uint32_t *reg, uint32_t mask, uint32_t want, uint32_t timeout_ms)
{
    for (uint32_t i = 0; i < timeout_ms * 10; i++)
    {
        if ((*reg & mask) == want)
        {
            return true;
        }
        udelay(100);
    }
    return (*reg & mask) == want;
}

static struct pci_device *ehci_pci_find(size_t *search_index)
{
    size_t total = pci_device_count();
    for (; *search_index < total; (*search_index)++)
    {
        struct pci_device *dev = NULL;
        if (pci_device_get(*search_index, &dev) < 0 || !dev)
        {
            continue;
        }
        if (pci_device_base_class(dev) != EHCI_PCI_BASE_CLASS || pci_device_subclass(dev) != EHCI_PCI_SUBCLASS)
        {
            continue;
        }
        if (pci_cfg_read_byte(dev->addr.bus, dev->addr.slot, dev->addr.func, PCI_HEADER_PROG_IF_OFFSET) != EHCI_PCI_PROG_IF)
        {
            continue;
        }
        (*search_index)++;
        return dev;
    }
    return NULL;
}

static void ehci_map_mmio(uint64_t base, uint64_t size)
{
    const int flags = PAGING_IS_PRESENT | PAGING_IS_WRITEABLE | PAGING_CACHE_DISABLED;
    uint64_t start = base & ~0xFFFull;
    uint64_t end = (base + (size ? size : 0x400) + 0xFFF) & ~0xFFFull;
    for (uint64_t page = start; page < end; page += 0x1000)
    {
        paging_map(kernel_desc(), (void *)page, (void *)page, flags);
    }
}

// Firmware emulates PS/2 through this controller via SMIs; take ownership first
static void ehci_bios_handoff(struct ehci_controller *hc, uint32_t hccparams)
{
    struct pci_address *a = &hc->pci->addr;
    uint8_t eecp = EHCI_HCCPARAMS_EECP(hccparams);
    for (int guard = 0; eecp >= 0x40 && guard < 16; guard++)
    {
        uint32_t legsup = pci_cfg_read_dword(a->bus, a->slot, a->func, eecp);
        if ((legsup & 0xFF) != EHCI_LEGSUP_CAP_ID)
        {
            eecp = (legsup >> 8) & 0xFF;
            continue;
        }

        if (legsup & EHCI_LEGSUP_BIOS_OWNED)
        {
            pci_cfg_write_dword(a->bus, a->slot, a->func, eecp, legsup | EHCI_LEGSUP_OS_OWNED);
            for (int i = 0; i < 1000 && (legsup & EHCI_LEGSUP_BIOS_OWNED); i++)
            {
                udelay(1000);
                legsup = pci_cfg_read_dword(a->bus, a->slot, a->func, eecp);
            }

            if (legsup & EHCI_LEGSUP_BIOS_OWNED)
            {
                pci_cfg_write_dword(a->bus, a->slot, a->func, eecp, (legsup & ~EHCI_LEGSUP_BIOS_OWNED) | EHCI_LEGSUP_OS_OWNED);
            }
        }

        pci_cfg_write_dword(a->bus, a->slot, a->func, eecp + 4, EHCI_LEGCTLSTS_DISABLE_AND_ACK);
        return;
    }
}

static struct ehci_qh *ehci_qh_new()
{
    struct ehci_qh *qh = ehci_dma_alloc(sizeof(struct ehci_qh), 32);
    memset((void *)qh, 0, sizeof(*qh));
    qh->horiz = EHCI_PTR_TERMINATE;
    qh->next_qtd = EHCI_PTR_TERMINATE;
    qh->alt_next_qtd = EHCI_PTR_TERMINATE;
    return qh;
}

static struct ehci_qtd *ehci_qtd_new()
{
    struct ehci_qtd *qtd = ehci_dma_alloc(sizeof(struct ehci_qtd), 32);
    memset((void *)qtd, 0, sizeof(*qtd));
    qtd->next = EHCI_PTR_TERMINATE;
    qtd->alt_next = EHCI_PTR_TERMINATE;
    return qtd;
}

static bool ehci_controller_start(struct ehci_controller *hc)
{
    ehci_op_write(hc, EHCI_OP_USBCMD, ehci_op_read(hc, EHCI_OP_USBCMD) & ~EHCI_USBCMD_RS);
    if (!ehci_wait_reg((volatile uint32_t *)(hc->op + EHCI_OP_USBSTS), EHCI_USBSTS_HCHALTED, EHCI_USBSTS_HCHALTED, 20))
    {
        return false;
    }

    ehci_op_write(hc, EHCI_OP_USBCMD, EHCI_USBCMD_HCRESET);
    if (!ehci_wait_reg((volatile uint32_t *)(hc->op + EHCI_OP_USBCMD), EHCI_USBCMD_HCRESET, 0, 250))
    {
        return false;
    }

    hc->frame_list = ehci_dma_alloc(EHCI_FRAME_LIST_ENTRIES * sizeof(uint32_t), 4096);
    for (int i = 0; i < EHCI_FRAME_LIST_ENTRIES; i++)
    {
        hc->frame_list[i] = EHCI_PTR_TERMINATE;
    }
    hc->periodic_chain = EHCI_PTR_TERMINATE;

    // The async list must never be empty: an idle head QH pointing at itself
    hc->async_head = ehci_qh_new();
    hc->async_head->horiz = ehci_ptr(hc->async_head) | EHCI_PTR_TYPE_QH;
    hc->async_head->ep_char = EHCI_QH_EP_CHAR_HEAD | (EHCI_SPEED_HIGH << 12);
    hc->async_head->ep_caps = EHCI_QH_EP_CAPS_MULT_1;
    hc->async_head->token = EHCI_QTD_STATUS_HALTED;

    hc->control_qh = ehci_qh_new();
    for (int i = 0; i < 3; i++)
    {
        hc->control_qtd[i] = ehci_qtd_new();
    }
    hc->setup_buf = ehci_dma_alloc(8, 32);
    hc->control_data = ehci_dma_alloc(EHCI_CONTROL_DATA_MAX, 4096);
    hc->next_address = 1;

    ehci_op_write(hc, EHCI_OP_USBINTR, 0);
    ehci_op_write(hc, EHCI_OP_USBSTS, EHCI_USBSTS_ALL_ACK);
    ehci_op_write(hc, EHCI_OP_CTRLDSSEGMENT, 0);
    ehci_op_write(hc, EHCI_OP_PERIODICLISTBASE, ehci_ptr(hc->frame_list));
    ehci_op_write(hc, EHCI_OP_ASYNCLISTADDR, ehci_ptr(hc->async_head));
    ehci_op_write(hc, EHCI_OP_USBCMD, EHCI_USBCMD_ITC_8 | EHCI_USBCMD_PSE | EHCI_USBCMD_ASE | EHCI_USBCMD_RS);

    if (!ehci_wait_reg((volatile uint32_t *)(hc->op + EHCI_OP_USBSTS), EHCI_USBSTS_HCHALTED, 0, 20))
    {
        return false;
    }

    // Route all root ports to this controller, not a companion
    ehci_op_write(hc, EHCI_OP_CONFIGFLAG, 1);
    udelay(5000);
    return true;
}

static void ehci_fill_buffers(volatile uint32_t *buffer, volatile uint32_t *buffer_hi, void *data, uint32_t len)
{
    uint32_t addr = ehci_ptr(data);
    buffer[0] = addr;
    uint32_t page = addr & ~0xFFFu;
    for (int i = 1; i < 5; i++)
    {
        page += 0x1000;
        buffer[i] = (len > 0 && page < addr + len) ? page : 0;
    }
    for (int i = 0; i < 5; i++)
    {
        buffer_hi[i] = 0;
    }
}

// The controller may cache an unlinked QH; the async-advance doorbell says it let go
static void ehci_async_advance_handshake(struct ehci_controller *hc)
{
    ehci_op_write(hc, EHCI_OP_USBCMD, ehci_op_read(hc, EHCI_OP_USBCMD) | EHCI_USBCMD_IAAD);
    ehci_wait_reg((volatile uint32_t *)(hc->op + EHCI_OP_USBSTS), EHCI_USBSTS_IAA, EHCI_USBSTS_IAA, 20);
    ehci_op_write(hc, EHCI_OP_USBSTS, EHCI_USBSTS_IAA);
}

static uint32_t ehci_qh_ep_char(uint8_t address, uint8_t endpoint, const struct ehci_device_path *path, uint16_t max_packet, bool control)
{
    uint32_t v = address | ((uint32_t)(endpoint & 0xF) << 8) | ((uint32_t)path->speed << 12) | ((uint32_t)max_packet << 16);
    if (control)
    {
        v |= EHCI_QH_EP_CHAR_DTC;
        if (path->speed != EHCI_SPEED_HIGH)
        {
            v |= EHCI_QH_EP_CHAR_CONTROL;
        }
    }
    return v;
}

static uint32_t ehci_qh_ep_caps(const struct ehci_device_path *path, uint8_t s_mask, uint8_t c_mask)
{
    return EHCI_QH_EP_CAPS_MULT_1 | s_mask | ((uint32_t)c_mask << 8) | ((uint32_t)path->tt_hub_address << 16) | ((uint32_t)path->tt_port << 23);
}

static bool ehci_control_transfer(struct ehci_controller *hc, uint8_t address, const struct ehci_device_path *path, uint16_t ep0_max_packet,
                                  uint8_t bm_request_type, uint8_t b_request, uint16_t w_value, uint16_t w_index, uint16_t w_length, void *data)
{
    if (w_length > EHCI_CONTROL_DATA_MAX)
    {
        return false;
    }

    bool data_in = (bm_request_type & 0x80) != 0;
    uint8_t *setup = hc->setup_buf;
    setup[0] = bm_request_type;
    setup[1] = b_request;
    setup[2] = w_value & 0xFF;
    setup[3] = w_value >> 8;
    setup[4] = w_index & 0xFF;
    setup[5] = w_index >> 8;
    setup[6] = w_length & 0xFF;
    setup[7] = w_length >> 8;

    if (w_length && !data_in)
    {
        memcpy(hc->control_data, data, w_length);
    }

    struct ehci_qtd *setup_qtd = hc->control_qtd[0];
    struct ehci_qtd *data_qtd = hc->control_qtd[1];
    struct ehci_qtd *status_qtd = hc->control_qtd[2];

    status_qtd->next = EHCI_PTR_TERMINATE;
    status_qtd->alt_next = EHCI_PTR_TERMINATE;
    ehci_fill_buffers(status_qtd->buffer, status_qtd->buffer_hi, hc->control_data, 0);
    uint32_t status_pid = (w_length == 0 || !data_in) ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT;
    status_qtd->token = EHCI_QTD_TOKEN(status_pid, 0, 1, 1);

    if (w_length)
    {
        data_qtd->next = ehci_ptr(status_qtd);
        data_qtd->alt_next = ehci_ptr(status_qtd);
        ehci_fill_buffers(data_qtd->buffer, data_qtd->buffer_hi, hc->control_data, w_length);
        data_qtd->token = EHCI_QTD_TOKEN(data_in ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT, w_length, 1, 0);
    }

    setup_qtd->next = ehci_ptr(w_length ? data_qtd : status_qtd);
    setup_qtd->alt_next = EHCI_PTR_TERMINATE;
    ehci_fill_buffers(setup_qtd->buffer, setup_qtd->buffer_hi, setup, 8);
    setup_qtd->token = EHCI_QTD_TOKEN(EHCI_QTD_PID_SETUP, 8, 0, 0);

    struct ehci_qh *qh = hc->control_qh;
    qh->ep_char = ehci_qh_ep_char(address, 0, path, ep0_max_packet, true);
    qh->ep_caps = ehci_qh_ep_caps(path, 0, 0);
    qh->current_qtd = 0;
    qh->next_qtd = ehci_ptr(setup_qtd);
    qh->alt_next_qtd = EHCI_PTR_TERMINATE;
    qh->token = 0;

    // Link in after the head, wait for the status stage
    qh->horiz = hc->async_head->horiz;
    __asm__ volatile("" ::: "memory");
    hc->async_head->horiz = ehci_ptr(qh) | EHCI_PTR_TYPE_QH;

    bool ok = false;
    for (int i = 0; i < 5000; i++)
    {
        // data_qtd is stale when unused
        uint32_t tokens = setup_qtd->token | status_qtd->token | (w_length ? data_qtd->token : 0);
        if (tokens & EHCI_QTD_STATUS_HALTED)
        {
            break;
        }
        if (!(status_qtd->token & EHCI_QTD_STATUS_ACTIVE))
        {
            ok = true;
            break;
        }
        udelay(100);
    }

    hc->async_head->horiz = qh->horiz;
    ehci_async_advance_handshake(hc);

    if (!ok)
    {
        return false;
    }

    if (w_length && data_in)
    {
        memcpy(data, hc->control_data, w_length);
    }
    return true;
}

static void ehci_hid_arm(struct ehci_hid_device *dev)
{
    struct ehci_qtd *qtd = dev->qtd;
    qtd->next = EHCI_PTR_TERMINATE;
    qtd->alt_next = EHCI_PTR_TERMINATE;
    ehci_fill_buffers(qtd->buffer, qtd->buffer_hi, dev->report_buf, dev->max_packet);
    // DTC=0: the QH supplies the toggle
    qtd->token = EHCI_QTD_TOKEN(EHCI_QTD_PID_IN, dev->max_packet, 0, 1);
    __asm__ volatile("" ::: "memory");
    dev->qh->next_qtd = ehci_ptr(qtd);
}

static bool ehci_setup_hid_interrupt(struct ehci_controller *hc, uint8_t address, const struct ehci_device_path *path,
                                     struct ehci_hid_ep_info *ep, ehci_hid_report_handler handler,
                                     const struct hid_mouse_layout *mouse_layout)
{
    struct ehci_hid_device *dev = NULL;
    for (int i = 0; i < EHCI_MAX_HID_DEVICES; i++)
    {
        if (!g_ehci_hid[i].in_use)
        {
            dev = &g_ehci_hid[i];
            break;
        }
    }
    if (!dev)
    {
        return false;
    }

    dev->max_packet = ep->max_packet > 64 ? 64 : ep->max_packet;
    dev->handler = handler;
    dev->is_mouse = mouse_layout != NULL;
    if (mouse_layout)
    {
        dev->mouse_layout = *mouse_layout;
    }
    dev->qh = ehci_qh_new();
    dev->qtd = ehci_qtd_new();
    dev->report_buf = ehci_dma_alloc(64, 64);

    // Poll in uframe 0 of every frame; behind a TT, complete-splits in uframes 2-4
    uint8_t c_mask = path->speed == EHCI_SPEED_HIGH ? 0 : 0x1C;
    dev->qh->ep_char = ehci_qh_ep_char(address, ep->ep_addr, path, dev->max_packet, false);
    dev->qh->ep_caps = ehci_qh_ep_caps(path, 0x01, c_mask);
    dev->qh->token = 0;
    ehci_hid_arm(dev);
    dev->in_use = true;

    dev->qh->horiz = hc->periodic_chain;
    __asm__ volatile("" ::: "memory");
    hc->periodic_chain = ehci_ptr(dev->qh) | EHCI_PTR_TYPE_QH;
    for (int i = 0; i < EHCI_FRAME_LIST_ENTRIES; i++)
    {
        hc->frame_list[i] = hc->periodic_chain;
    }

    return true;
}

// Called every timer tick; only peeks at each qTD, so it's safe in the interrupt
void ehci_poll_hid_devices()
{
    for (int i = 0; i < EHCI_MAX_HID_DEVICES; i++)
    {
        struct ehci_hid_device *dev = &g_ehci_hid[i];
        if (!dev->in_use)
        {
            continue;
        }

        uint32_t token = dev->qtd->token;
        if (token & EHCI_QTD_STATUS_ACTIVE)
        {
            continue;
        }

        if (token & EHCI_QTD_STATUS_HALTED)
        {
            // A halted overlay blocks the QH until cleared
            dev->qh->token = 0;
        }
        else
        {
            uint32_t received = dev->max_packet - EHCI_QTD_BYTES_LEFT(token);
            if (dev->is_mouse)
            {
                usbhid_mouse_process_report_layout(&dev->mouse_layout, dev->report_buf, received);
            }
            else
            {
                dev->handler(dev->report_buf, received);
            }
        }

        ehci_hid_arm(dev);
    }
}

// Finds the first interrupt IN endpoint of a keyboard/mouse interface, and whether it's a hub
static void ehci_walk_config(uint8_t *cfg, uint16_t total_len, struct ehci_hid_ep_info *kbd, struct ehci_hid_ep_info *mouse, bool *is_hub)
{
    // Don't require the boot subclass: TinyUSB devices declare 0/0
    bool in_hid = false;
    bool hid_boot = false;
    uint8_t hid_protocol = 0;
    uint16_t hid_report_len = 0;
    uint8_t interface_number = 0;
    for (size_t off = 0; off + 2 <= total_len && cfg[off] != 0; off += cfg[off])
    {
        uint8_t type = cfg[off + 1];
        if (type == USB_DESC_INTERFACE && off + 9 <= total_len)
        {
            uint8_t cls = cfg[off + 5];
            uint8_t sub = cfg[off + 6];
            uint8_t proto = cfg[off + 7];
            interface_number = cfg[off + 2];
            in_hid = (cls == USB_CLASS_HID);
            hid_boot = in_hid && (sub == HID_SUBCLASS_BOOT);
            hid_protocol = in_hid ? proto : 0;
            hid_report_len = 0;
            if (cls == USB_CLASS_HUB)
            {
                *is_hub = true;
            }

        }
        else if (type == USB_DESC_HID && off + 9 <= total_len)
        {
            // Report descriptor length
            hid_report_len = cfg[off + 7] | ((uint16_t)cfg[off + 8] << 8);
        }
        else if (type == USB_DESC_ENDPOINT && off + 7 <= total_len)
        {
            struct ehci_hid_ep_info *target = NULL;
            if (hid_protocol == HID_PROTOCOL_KEYBOARD && !kbd->found)
            {
                target = kbd;
            }
            else if (in_hid && !mouse->found)
            {
                // Protocol 0 is undeclared; the report descriptor decides
                target = mouse;
            }

            uint8_t ep_addr = cfg[off + 2];
            if (target && (ep_addr & 0x80) && (cfg[off + 3] & 0x3) == 3)
            {
                target->found = true;
                target->ep_addr = ep_addr;
                target->max_packet = (cfg[off + 4] | ((uint16_t)cfg[off + 5] << 8)) & 0x7FF;
                target->interface_number = interface_number;
                target->report_desc_len = hid_report_len;
                target->boot_capable = hid_boot;
            }
        }
    }
}

static void ehci_enumerate_device(struct ehci_controller *hc, const struct ehci_device_path *path, int depth);

static bool ehci_hub_port_status(struct ehci_controller *hc, uint8_t hub_addr, const struct ehci_device_path *hub_path, uint16_t hub_mps0,
                                 int port, uint32_t *status)
{
    uint8_t buf[4];
    if (!ehci_control_transfer(hc, hub_addr, hub_path, hub_mps0, 0xA3, USB_REQ_GET_STATUS, 0, port, 4, buf))
    {
        return false;
    }
    *status = buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
    return true;
}

static bool ehci_hub_port_feature(struct ehci_controller *hc, uint8_t hub_addr, const struct ehci_device_path *hub_path, uint16_t hub_mps0,
                                  uint8_t request, int port, uint16_t feature)
{
    return ehci_control_transfer(hc, hub_addr, hub_path, hub_mps0, 0x23, request, feature, port, 0, NULL);
}

// Powers, resets and enumerates each downstream port of a configured hub
static void ehci_probe_hub(struct ehci_controller *hc, uint8_t hub_addr, const struct ehci_device_path *hub_path, uint16_t hub_mps0, int depth)
{
    if (depth >= EHCI_MAX_HUB_DEPTH)
    {
        return;
    }

    uint8_t hd[9];
    if (!ehci_control_transfer(hc, hub_addr, hub_path, hub_mps0, 0xA0, USB_REQ_GET_DESCRIPTOR, USB_DESC_HUB << 8, 0, 9, hd))
    {
        return;
    }
    int n_ports = hd[2];
    uint32_t power_good_ms = (uint32_t)hd[5] * 2;

    for (int port = 1; port <= n_ports; port++)
    {
        ehci_hub_port_feature(hc, hub_addr, hub_path, hub_mps0, USB_REQ_SET_FEATURE, port, HUB_FEAT_PORT_POWER);
    }
    udelay((power_good_ms + 100) * 1000);

    for (int port = 1; port <= n_ports; port++)
    {
        uint32_t status = 0;
        if (!ehci_hub_port_status(hc, hub_addr, hub_path, hub_mps0, port, &status) || !(status & HUB_PORT_STATUS_CONNECTION))
        {
            continue;
        }

        ehci_hub_port_feature(hc, hub_addr, hub_path, hub_mps0, USB_REQ_SET_FEATURE, port, HUB_FEAT_PORT_RESET);
        udelay(20000);
        for (int i = 0; i < 50; i++)
        {
            if (!ehci_hub_port_status(hc, hub_addr, hub_path, hub_mps0, port, &status) || !(status & HUB_PORT_STATUS_RESET))
            {
                break;
            }
            udelay(10000);
        }
        ehci_hub_port_feature(hc, hub_addr, hub_path, hub_mps0, USB_REQ_CLEAR_FEATURE, port, HUB_FEAT_C_PORT_RESET);
        ehci_hub_port_feature(hc, hub_addr, hub_path, hub_mps0, USB_REQ_CLEAR_FEATURE, port, HUB_FEAT_C_PORT_CONNECTION);
        // Spec says 10ms, but ESP32-S3 takes longer to answer EP0
        udelay(120000);

        if (!ehci_hub_port_status(hc, hub_addr, hub_path, hub_mps0, port, &status) || !(status & HUB_PORT_STATUS_ENABLE))
        {
            continue;
        }

        struct ehci_device_path child = {.speed = EHCI_SPEED_FULL, .tt_hub_address = 0, .tt_port = 0};
        if (status & HUB_PORT_STATUS_HIGH_SPEED)
        {
            child.speed = EHCI_SPEED_HIGH;
        }
        else
        {
            child.speed = (status & HUB_PORT_STATUS_LOW_SPEED) ? EHCI_SPEED_LOW : EHCI_SPEED_FULL;
            // Low/full-speed devices go through the nearest high-speed hub's TT
            if (hub_path->speed == EHCI_SPEED_HIGH)
            {
                child.tt_hub_address = hub_addr;
                child.tt_port = port;
            }
            else
            {
                child.tt_hub_address = hub_path->tt_hub_address;
                child.tt_port = hub_path->tt_port;
            }
        }

        ehci_enumerate_device(hc, &child, depth + 1);
    }
}

static void ehci_enumerate_device(struct ehci_controller *hc, const struct ehci_device_path *path, int depth)
{
    uint16_t mps0 = path->speed == EHCI_SPEED_HIGH ? 64 : 8;
    uint8_t desc[18];

    // Freshly reset devices often time out the first request; retry like Linux
    bool got_first_descriptor = false;
    for (int attempt = 0; attempt < 4 && !got_first_descriptor; attempt++)
    {
        if (attempt > 0)
        {
            udelay(50000);
        }
        got_first_descriptor = ehci_control_transfer(hc, 0, path, mps0, 0x80, USB_REQ_GET_DESCRIPTOR, USB_DESC_DEVICE << 8, 0, 8, desc);
    }
    if (!got_first_descriptor)
    {
        return;
    }
    mps0 = desc[7];

    uint8_t address = hc->next_address++;
    if (!ehci_control_transfer(hc, 0, path, mps0, 0x00, USB_REQ_SET_ADDRESS, address, 0, 0, NULL))
    {
        return;
    }
    udelay(10000);

    if (!ehci_control_transfer(hc, address, path, mps0, 0x80, USB_REQ_GET_DESCRIPTOR, USB_DESC_DEVICE << 8, 0, 18, desc))
    {
        return;
    }

    uint8_t cfg_head[9];
    if (!ehci_control_transfer(hc, address, path, mps0, 0x80, USB_REQ_GET_DESCRIPTOR, USB_DESC_CONFIGURATION << 8, 0, 9, cfg_head))
    {
        return;
    }
    uint16_t total_len = cfg_head[2] | ((uint16_t)cfg_head[3] << 8);
    if (total_len < 9 || total_len > EHCI_CONTROL_DATA_MAX)
    {
        return;
    }

    uint8_t cfg[EHCI_CONTROL_DATA_MAX];
    if (!ehci_control_transfer(hc, address, path, mps0, 0x80, USB_REQ_GET_DESCRIPTOR, USB_DESC_CONFIGURATION << 8, 0, total_len, cfg))
    {
        return;
    }

    struct ehci_hid_ep_info kbd = {0};
    struct ehci_hid_ep_info mouse = {0};
    bool is_hub = desc[4] == USB_CLASS_HUB;
    ehci_walk_config(cfg, total_len, &kbd, &mouse, &is_hub);

    if (!ehci_control_transfer(hc, address, path, mps0, 0x00, USB_REQ_SET_CONFIGURATION, cfg_head[5], 0, 0, NULL))
    {
        return;
    }

    if (is_hub)
    {
        ehci_probe_hub(hc, address, path, mps0, depth);
        return;
    }

    if (kbd.found)
    {
        // The keyboard handler expects the 8-byte boot report
        ehci_control_transfer(hc, address, path, mps0, 0x21, USB_REQ_SET_PROTOCOL, 0, kbd.interface_number, 0, NULL);
        if (ehci_setup_hid_interrupt(hc, address, path, &kbd, usbhid_keyboard_process_report, NULL))
        {
            usbhid_keyboard_reset();
        }
        return;
    }

    if (!mouse.found)
    {
        return;
    }

    // Devices start in report protocol, so read the descriptor instead of assuming a layout
    struct hid_mouse_layout layout = {0};
    bool have_layout = false;
    if (mouse.report_desc_len > 0 && mouse.report_desc_len <= EHCI_CONTROL_DATA_MAX)
    {
        uint8_t report_desc[EHCI_CONTROL_DATA_MAX];
        if (ehci_control_transfer(hc, address, path, mps0, 0x81, USB_REQ_GET_DESCRIPTOR,
                                  USB_DESC_HID_REPORT << 8, mouse.interface_number,
                                  mouse.report_desc_len, report_desc))
        {
            have_layout = hid_parse_mouse_layout(report_desc, mouse.report_desc_len, &layout);
        }
    }

    if (!have_layout)
    {
        // No usable layout: only a boot-capable device can be read
        if (!mouse.boot_capable)
        {
            return;
        }

        ehci_control_transfer(hc, address, path, mps0, 0x21, USB_REQ_SET_PROTOCOL, 0, mouse.interface_number, 0, NULL);
    }

    // An invalid layout means the boot layout, set by SET_PROTOCOL above
    if (ehci_setup_hid_interrupt(hc, address, path, &mouse, usbhid_mouse_process_report, &layout))
    {
        usbhid_mouse_attach();
    }
}

static void ehci_probe_root_ports(struct ehci_controller *hc, uint32_t hcsparams)
{
    if (hcsparams & EHCI_HCSPARAMS_PPC)
    {
        for (int port = 0; port < hc->n_ports; port++)
        {
            volatile uint32_t *sc = ehci_portsc(hc, port);
            *sc = (*sc & ~EHCI_PORTSC_RW1C) | EHCI_PORTSC_PP;
        }
        udelay(20000);
    }

    for (int port = 0; port < hc->n_ports; port++)
    {
        volatile uint32_t *sc = ehci_portsc(hc, port);
        uint32_t val = *sc;
        if (!(val & EHCI_PORTSC_CCS))
        {
            continue;
        }

        // K-state before reset = low-speed; only a companion controller can talk to it
        if (((val & EHCI_PORTSC_LINE_MASK) >> EHCI_PORTSC_LINE_SHIFT) == EHCI_PORTSC_LINE_K_STATE)
        {
            continue;
        }

        *sc = (val & ~(EHCI_PORTSC_RW1C | EHCI_PORTSC_PE)) | EHCI_PORTSC_PR;
        udelay(50000);
        *sc = *sc & ~(EHCI_PORTSC_RW1C | EHCI_PORTSC_PR);
        if (!ehci_wait_reg(sc, EHCI_PORTSC_PR, 0, 5))
        {
            continue;
        }
        udelay(10000);

        // Only high-speed devices stay enabled after reset
        if (!(*sc & EHCI_PORTSC_PE))
        {
            continue;
        }

        *sc = (*sc & ~EHCI_PORTSC_RW1C) | EHCI_PORTSC_CSC | EHCI_PORTSC_PEC;

        struct ehci_device_path path = {.speed = EHCI_SPEED_HIGH, .tt_hub_address = 0, .tt_port = 0};
        ehci_enumerate_device(hc, &path, 0);
    }
}

static void ehci_init_controller(struct ehci_controller *hc)
{
    struct pci_device *dev = hc->pci;

    if (dev->bars[0].addr == 0 || dev->bars[0].type != PCI_DEVICE_IO_MEMORY)
    {
        return;
    }

    pci_enable_bus_master(dev);
    ehci_map_mmio(dev->bars[0].addr, dev->bars[0].size);
    hc->cap = (volatile uint8_t *)(uintptr_t)dev->bars[0].addr;
    hc->op = hc->cap + hc->cap[EHCI_CAP_CAPLENGTH];

    uint32_t hcsparams = *(volatile uint32_t *)(hc->cap + EHCI_CAP_HCSPARAMS);
    uint32_t hccparams = *(volatile uint32_t *)(hc->cap + EHCI_CAP_HCCPARAMS);
    hc->n_ports = EHCI_HCSPARAMS_N_PORTS(hcsparams);

    ehci_bios_handoff(hc, hccparams);

    if (!ehci_controller_start(hc))
    {
        return;
    }

    ehci_probe_root_ports(hc, hcsparams);
}

int ehci_init()
{
    g_ehci_pool = kzalloc(EHCI_DMA_POOL_SIZE);
    if (!g_ehci_pool || (uint64_t)(uintptr_t)g_ehci_pool + EHCI_DMA_POOL_SIZE > 0xFFFFFFFFull)
    {
        return -1;
    }

    size_t search_index = 0;
    struct pci_device *dev = NULL;
    while (g_ehci_count < EHCI_MAX_CONTROLLERS && (dev = ehci_pci_find(&search_index)) != NULL)
    {
        struct ehci_controller *hc = &g_ehci[g_ehci_count++];
        memset(hc, 0, sizeof(*hc));
        hc->pci = dev;
        ehci_init_controller(hc);
    }

    if (g_ehci_count == 0)
    {
        return -1;
    }

    return 0;
}
