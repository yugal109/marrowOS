#ifndef USBHID_H
#define USBHID_H

#include <stdint.h>

// Clears the held-key edge-detection state. Call whenever a HID boot
// keyboard endpoint is (re)configured, so a fresh device doesn't inherit
// stale "already down" state from a previous one.
void usbhid_keyboard_reset();

// Feeds one raw interrupt IN report from a HID boot-protocol keyboard.
// Handles both the plain 8-byte report and the 9-byte report-ID-prefixed
// variant some firmware (e.g. Arduino's composite HID wrapper) sends.
void usbhid_keyboard_process_report(const uint8_t *buf, uint32_t len);

#endif
