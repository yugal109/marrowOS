#include "usbhid.h"
#include "keyboard.h"
#include "memory/memory.h"
#include <stdbool.h>
#include <stddef.h>

// Edge detection for the boot keyboard report's 6-key array, so a held key
// is only pushed once instead of every poll. Only one USB keyboard is
// tracked at a time, matching xhci.c's single g_xhci_keyboard_slot.
static uint8_t prev_keys[6];

static char usbhid_keycode_to_char(uint8_t keycode, bool shift)
{
    static const char base[] = {
        'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
        '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
        '\r', 0, '\b', '\t', ' ',
        '-', '=', '[', ']', '\\', 0, ';', '\'', '`', ',', '.', '/'};
    static const char shifted[] = {
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
        '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
        '\r', 0, '\b', '\t', ' ',
        '_', '+', '{', '}', '|', 0, ':', '"', '~', '<', '>', '?'};

    if (keycode < 0x04 || keycode > 0x38)
    {
        return 0;
    }

    size_t idx = keycode - 0x04;
    return shift ? shifted[idx] : base[idx];
}

void usbhid_keyboard_reset()
{
    memset(prev_keys, 0, sizeof(prev_keys));
}

void usbhid_keyboard_process_report(const uint8_t *buf, uint32_t len)
{
    const uint8_t *report = buf;
    if (len == 9)
    {
        report = buf + 1;
    }
    else if (len != 8)
    {
        return;
    }

    uint8_t modifiers = report[0];
    bool shift = (modifiers & 0x22) != 0; // left or right shift bit
    bool ctrl = (modifiers & 0x11) != 0;  // left or right control bit

    uint8_t new_keys[6];
    memcpy(new_keys, (void *)&report[2], 6);

    for (int i = 0; i < 6; i++)
    {
        uint8_t code = new_keys[i];
        if (code == 0)
        {
            continue;
        }

        bool already_down = false;
        for (int j = 0; j < 6; j++)
        {
            if (prev_keys[j] == code)
            {
                already_down = true;
                break;
            }
        }

        if (!already_down)
        {
            char c = usbhid_keycode_to_char(code, shift);
            if (ctrl && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
            {
                // Same convention as a terminal: Ctrl+A is 1, Ctrl+B is 2 ... Ctrl+Z is 26
                c = (char)((c | 0x20) - 'a' + 1);
            }
            if (c != 0)
            {
                keyboard_push(c);
            }
        }
    }

    memcpy(prev_keys, new_keys, 6);
}
