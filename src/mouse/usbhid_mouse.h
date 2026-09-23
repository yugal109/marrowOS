#ifndef USBHID_MOUSE_H
#define USBHID_MOUSE_H

#include <stdint.h>

struct mouse;

// Registered with mouse.c the first time xhci.c finds and configures a HID
// boot-protocol mouse — mirrors ps2_mouse_get()'s role for the PS/2 driver.
struct mouse *usbhid_mouse_get();

// Registers the USB mouse with mouse.c (creates its cursor window). Call
// once, synchronously, right after configuring its interrupt endpoint —
// not from interrupt context.
int usbhid_mouse_attach();

// Feeds one raw interrupt IN report from a HID boot-protocol mouse. Boot
// mouse reports are 3-4 bytes: [buttons][dx][dy][wheel, optional].
void usbhid_mouse_process_report(const uint8_t *buf, uint32_t len);

#endif
