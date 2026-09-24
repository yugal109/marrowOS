#include "usbhid_mouse.h"
#include "mouse/mouse.h"
#include "graphics/graphics.h"
#include "kernel.h"
#include "string/string.h"
#include <stdbool.h>
#include <stdint.h>

static int usbhid_mouse_init(struct mouse *mouse)
{
    return 0;
}

struct mouse usbhid_mouse = {
    .name = {"usbhid_mouse"},
    .init = usbhid_mouse_init,
};

// Tracks the previously-seen button state so a held->not-held transition can
// be detected and reported as a single release event, same as ps2mouse.c.
static MOUSE_CLICK_TYPE usbhid_mouse_prev_click_type = MOUSE_NO_CLICK;
static bool usbhid_mouse_registered = false;


struct mouse *usbhid_mouse_get()
{
    return &usbhid_mouse;
}

void usbhid_mouse_process_report(const uint8_t *buf, uint32_t len)
{
    usbhid_mouse_process_report_layout(NULL, buf, len);
}

void usbhid_mouse_process_report_layout(const struct hid_mouse_layout *layout, const uint8_t *buf, uint32_t len)
{
    if (len < 3)
    {
        return;
    }

    if (!usbhid_mouse_registered)
    {
        return;
    }

    uint8_t buttons;
    int dx;
    int dy;

    if (layout && layout->valid)
    {
        // Layout offsets start after the report ID byte
        if (layout->report_id != 0)
        {
            if (buf[0] != layout->report_id)
            {
                // Another report on the same endpoint
                return;
            }

            buf++;
            len--;
        }

        buttons = (uint8_t)hid_extract_unsigned(buf, len, layout->button_bit, layout->button_count);
        dx = hid_extract_signed(buf, len, layout->x_bit, layout->x_bits);
        dy = hid_extract_signed(buf, len, layout->y_bit, layout->y_bits);
    }
    else
    {
        // No layout: fixed boot layout
        buttons = buf[0];
        dx = (int8_t)buf[1];
        dy = (int8_t)buf[2];
    }

    int x_result = (int)usbhid_mouse.coords.x + dx;
    // USB HID boot mouse reports positive Y as "pointer moves down", which
    // already matches this screen's top-left-origin convention directly —
    // unlike ps2mouse.c's raw PS/2 packets, no sign flip needed here.
    int y_result = (int)usbhid_mouse.coords.y + dy;

    struct graphics_info *screen = graphics_screen_info();
    if (x_result < 0)
    {
        x_result = 0;
    }
    if (y_result < 0)
    {
        y_result = 0;
    }
    if (x_result > (int)(screen->width - usbhid_mouse.graphic.width))
    {
        x_result = screen->width - usbhid_mouse.graphic.width;
    }
    if (y_result > (int)(screen->height - usbhid_mouse.graphic.height))
    {
        y_result = screen->height - usbhid_mouse.graphic.height;
    }

    mouse_position_set(&usbhid_mouse, x_result, y_result);

    MOUSE_CLICK_TYPE click_type = MOUSE_NO_CLICK;
    if (buttons & 0x01)
    {
        click_type = MOUSE_LEFT_BUTTON_CLICKED;
    }
    else if (buttons & 0x02)
    {
        click_type = MOUSE_RIGHT_BUTTON_CLICKED;
    }
    else if (buttons & 0x04)
    {
        click_type = MOUSE_MIDDLE_BUTTON_CLICKED;
    }

    if (click_type != MOUSE_NO_CLICK)
    {
        mouse_click(&usbhid_mouse, click_type);
    }
    else if (usbhid_mouse_prev_click_type != MOUSE_NO_CLICK)
    {
        mouse_released(&usbhid_mouse, usbhid_mouse_prev_click_type);
    }
    usbhid_mouse_prev_click_type = click_type;

    mouse_moved(&usbhid_mouse);
}

// Called once by xhci.c right after it configures a HID boot mouse's
// interrupt endpoint. Separate from process_report so registration (which
// creates the cursor window) happens exactly once, at enumeration time —
// not lazily from inside the timer interrupt where reports get polled.
int usbhid_mouse_attach()
{
    if (usbhid_mouse_registered)
    {
        return 0;
    }

    int res = mouse_register(&usbhid_mouse);
    if (res < 0)
    {
        return res;
    }

    usbhid_mouse_registered = true;
    return 0;
}
