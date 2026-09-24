#include "hidreport.h"
#include <stddef.h>

// Item prefix: tag(4) type(2) size(2); size code 3 = 4 bytes
#define HID_ITEM_SIZE(prefix) (((prefix) & 0x03) == 3 ? 4 : ((prefix) & 0x03))
#define HID_ITEM_TAG_TYPE(prefix) ((prefix) & 0xFC)

// Tag|type with the size bits masked off
#define HID_ITEM_USAGE_PAGE 0x04
#define HID_ITEM_USAGE 0x08
#define HID_ITEM_REPORT_SIZE 0x74
#define HID_ITEM_REPORT_ID 0x84
#define HID_ITEM_REPORT_COUNT 0x94
#define HID_ITEM_INPUT 0x80
#define HID_ITEM_COLLECTION 0xA0
#define HID_ITEM_END_COLLECTION 0xC0

// bType, bits 2-3 of the prefix: 0 = Main, 1 = Global, 2 = Local
#define HID_ITEM_TYPE(prefix) (((prefix) >> 2) & 0x03)
#define HID_ITEM_TYPE_MAIN 0

#define HID_USAGE_PAGE_GENERIC_DESKTOP 0x01
#define HID_USAGE_PAGE_BUTTON 0x09
#define HID_USAGE_X 0x30
#define HID_USAGE_Y 0x31

// Input item data bits
#define HID_INPUT_CONSTANT (1u << 0)

#define HID_MAX_LOCAL_USAGES 16

static uint32_t hid_item_value(const uint8_t *data, uint8_t size)
{
    uint32_t value = 0;
    for (uint8_t i = 0; i < size; i++)
    {
        value |= (uint32_t)data[i] << (i * 8);
    }
    return value;
}

bool hid_parse_mouse_layout(const uint8_t *desc, uint16_t len, struct hid_mouse_layout *out)
{
    struct hid_mouse_layout found = {0};

    uint32_t usage_page = 0;
    uint32_t report_size = 0;
    uint32_t report_count = 0;
    uint32_t bit_offset = 0;

    uint32_t local_usages[HID_MAX_LOCAL_USAGES];
    uint8_t local_usage_count = 0;

    bool have_x = false;
    bool have_y = false;
    bool have_buttons = false;

    uint16_t off = 0;
    while (off < len)
    {
        uint8_t prefix = desc[off];
        // Long items: skip
        if (prefix == 0xFE)
        {
            if (off + 1 >= len)
            {
                break;
            }
            off += 3 + desc[off + 1];
            continue;
        }

        uint8_t size = HID_ITEM_SIZE(prefix);
        if (off + 1 + size > len)
        {
            break;
        }

        uint32_t value = hid_item_value(&desc[off + 1], size);
        uint8_t tag_type = HID_ITEM_TAG_TYPE(prefix);

        switch (tag_type)
        {
        case HID_ITEM_USAGE_PAGE:
            usage_page = value;
            break;

        case HID_ITEM_USAGE:
            if (local_usage_count < HID_MAX_LOCAL_USAGES)
            {
                local_usages[local_usage_count++] = value;
            }
            break;

        case HID_ITEM_REPORT_SIZE:
            report_size = value;
            break;

        case HID_ITEM_REPORT_COUNT:
            report_count = value;
            break;

        case HID_ITEM_REPORT_ID:
            // Each ID starts its own report; keep going until one has X and Y
            if (!have_x || !have_y)
            {
                found.report_id = (uint8_t)value;
                bit_offset = 0;
                have_buttons = false;
                found.button_count = 0;
                found.button_bit = 0;
            }
            break;

        case HID_ITEM_INPUT:
        {
            bool is_data = !(value & HID_INPUT_CONSTANT);
            if (is_data && report_size > 0 && report_count > 0)
            {
                if (usage_page == HID_USAGE_PAGE_BUTTON && !have_buttons)
                {
                    have_buttons = true;
                    found.button_bit = (uint16_t)bit_offset;
                    found.button_count = (uint8_t)report_count;
                }
                else if (usage_page == HID_USAGE_PAGE_GENERIC_DESKTOP)
                {
                    // Usages map onto the fields of this item in order
                    for (uint8_t i = 0; i < local_usage_count && i < report_count; i++)
                    {
                        uint32_t field_bit = bit_offset + (uint32_t)i * report_size;
                        if (local_usages[i] == HID_USAGE_X && !have_x)
                        {
                            have_x = true;
                            found.x_bit = (uint16_t)field_bit;
                            found.x_bits = (uint8_t)report_size;
                        }
                        else if (local_usages[i] == HID_USAGE_Y && !have_y)
                        {
                            have_y = true;
                            found.y_bit = (uint16_t)field_bit;
                            found.y_bits = (uint8_t)report_size;
                        }
                    }
                }
            }

            bit_offset += report_size * report_count;
            break;
        }

        default:
            // Output and Feature items describe separate report streams, so
            // they do not shift the input report's bit offsets.
            break;
        }

        // Only Main items reset local state
        if (HID_ITEM_TYPE(prefix) == HID_ITEM_TYPE_MAIN)
        {
            local_usage_count = 0;
        }

        off += 1 + size;
    }

    if (!have_x || !have_y)
    {
        return false;
    }

    found.total_bits = (uint16_t)bit_offset;
    found.valid = true;
    *out = found;
    return true;
}

uint32_t hid_extract_unsigned(const uint8_t *report, uint32_t report_len, uint16_t bit_offset, uint8_t bit_count)
{
    if (bit_count == 0 || bit_count > 32)
    {
        return 0;
    }

    uint32_t value = 0;
    for (uint8_t i = 0; i < bit_count; i++)
    {
        uint32_t bit = (uint32_t)bit_offset + i;
        uint32_t byte = bit / 8;
        if (byte >= report_len)
        {
            break;
        }

        if (report[byte] & (1u << (bit % 8)))
        {
            value |= (1u << i);
        }
    }

    return value;
}

int32_t hid_extract_signed(const uint8_t *report, uint32_t report_len, uint16_t bit_offset, uint8_t bit_count)
{
    uint32_t raw = hid_extract_unsigned(report, report_len, bit_offset, bit_count);
    if (bit_count == 0 || bit_count >= 32)
    {
        return (int32_t)raw;
    }

    // Sign-extend from the field's top bit
    uint32_t sign_bit = 1u << (bit_count - 1);
    if (raw & sign_bit)
    {
        raw |= ~((1u << bit_count) - 1);
    }

    return (int32_t)raw;
}
