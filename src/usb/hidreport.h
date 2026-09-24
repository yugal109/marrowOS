#ifndef USB_HIDREPORT_H
#define USB_HIDREPORT_H

#include <stdint.h>
#include <stdbool.h>

// Where a mouse's buttons and axes sit in its report; bit offsets exclude the report ID byte
struct hid_mouse_layout
{
    bool valid;
    // 0 = no report ID byte
    uint8_t report_id;
    uint16_t button_bit;
    uint8_t button_count;
    uint16_t x_bit;
    uint16_t y_bit;
    uint8_t x_bits;
    uint8_t y_bits;
    uint16_t total_bits;
};

// Locates a mouse's buttons and X/Y in a report descriptor; false if not found
bool hid_parse_mouse_layout(const uint8_t *desc, uint16_t len, struct hid_mouse_layout *out);

// Pulls a signed field of bit_count bits starting at bit_offset out of a report
int32_t hid_extract_signed(const uint8_t *report, uint32_t report_len, uint16_t bit_offset, uint8_t bit_count);

// Pulls an unsigned field, used for the button bitmap
uint32_t hid_extract_unsigned(const uint8_t *report, uint32_t report_len, uint16_t bit_offset, uint8_t bit_count);

#endif
