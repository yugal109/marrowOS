#ifndef USBHID_MOUSE_H
#define USBHID_MOUSE_H

#include "usb/hidreport.h"
#include <stdint.h>

struct mouse;

// The USB HID mouse, registered with mouse.c on first attach
struct mouse *usbhid_mouse_get();

// Registers the USB mouse with mouse.c (creates its cursor window). Call
// once, synchronously, right after configuring its interrupt endpoint —
// not from interrupt context.
int usbhid_mouse_attach();


// Feeds one boot-protocol report
void usbhid_mouse_process_report(const uint8_t *buf, uint32_t len);

// Feeds one report decoded with that device's own layout; NULL/invalid = boot layout
void usbhid_mouse_process_report_layout(const struct hid_mouse_layout *layout, const uint8_t *buf, uint32_t len);

#endif
