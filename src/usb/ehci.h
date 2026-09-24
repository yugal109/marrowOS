#ifndef USB_EHCI_H
#define USB_EHCI_H

#include <stddef.h>
#include <stdint.h>

int ehci_init();
void ehci_poll_hid_devices();

enum
{
    EHCI_SERIAL_NONE,
    EHCI_SERIAL_READY,
    EHCI_SERIAL_FAILED
};

// USB serial adapter (CDC-ACM, e.g. CH9102)
int ehci_serial_state();
uint32_t ehci_serial_baud();
// Sends up to EHCI_SERIAL_CHUNK bytes; returns how many were sent, or a negative error
int ehci_serial_write(const void *data, size_t len);

#define EHCI_SERIAL_CHUNK 1024

#endif
