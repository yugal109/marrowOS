#include "stdlib.h"
#include "stdio.h"
#include "window.h"
#include "graphics.h"
#include "image.h"
#include "font.h"

#define EDITOR_TEXT_INITIAL_CAPACITY 4096
#define EDITOR_MARGIN 6
#define EDITOR_KEY_BACKSPACE 0x08
#define EDITOR_KEY_ENTER 0x0d

struct framebuffer_pixel color_white = {.red = 0xff, .green = 0xff, .blue = 0xff, .reserved = 0};
struct framebuffer_pixel color_ink = {.red = 0x20, .green = 0x20, .blue = 0x20, .reserved = 0};

char *text = NULL;
int text_len = 0;
int text_capacity = 0;
int cursor_index = 0;

// Text extent at the last redraw, so shrinking text clears its old rows
int last_bottom_y = 0;

bool editor_ensure_capacity(int needed)
{
    if (needed < text_capacity)
    {
        return true;
    }

    int new_capacity = text_capacity ? text_capacity * 2 : EDITOR_TEXT_INITIAL_CAPACITY;
    while (new_capacity <= needed)
    {
        new_capacity *= 2;
    }

    char *new_text = realloc(text, new_capacity);
    if (!new_text)
    {
        return false;
    }

    text = new_text;
    text_capacity = new_capacity;
    return true;
}

void editor_insert(char c)
{
    if (!editor_ensure_capacity(text_len + 1))
    {
        return;
    }
    for (int i = text_len; i > cursor_index; i--)
    {
        text[i] = text[i - 1];
    }
    text[cursor_index] = c;
    text_len++;
    cursor_index++;
    text[text_len] = 0;
}

void editor_backspace()
{
    if (cursor_index == 0)
    {
        return;
    }
    for (int i = cursor_index - 1; i < text_len - 1; i++)
    {
        text[i] = text[i + 1];
    }
    text_len--;
    cursor_index--;
    text[text_len] = 0;
}

// Mirrors font_draw_text_wrap's wrap/newline logic, so it agrees with it
void editor_screen_pos(struct font *font, int origin_x, int origin_y, int width, int target_index, int *out_x, int *out_y)
{
    int current_x = origin_x;
    int current_y = origin_y;
    int ending_x = origin_x + width;

    for (int i = 0; i < target_index && i < text_len; i++)
    {
        if (text[i] == EDITOR_KEY_ENTER)
        {
            current_x = origin_x;
            current_y += font->bits_height_per_character;
            continue;
        }
        if (current_x >= ending_x)
        {
            current_x = origin_x;
            current_y += font->bits_height_per_character;
        }
        current_x += font->bits_width_per_character;
    }

    *out_x = current_x;
    *out_y = current_y;
}

// Nearest index to a clicked point. Row match is weighted far above
// column, so a click always lands on the right line first.
int editor_index_at_point(struct font *font, int origin_x, int origin_y, int width, int click_x, int click_y)
{
    int current_x = origin_x;
    int current_y = origin_y;
    int ending_x = origin_x + width;
    int char_h = font->bits_height_per_character;
    int char_w = font->bits_width_per_character;

    int best_index = 0;
    int best_dist = -1;

    for (int i = 0; i <= text_len; i++)
    {
        int row_diff = click_y - current_y;
        if (row_diff < 0)
        {
            row_diff = -row_diff;
        }
        int col_diff = click_x - current_x;
        if (col_diff < 0)
        {
            col_diff = -col_diff;
        }
        int dist = row_diff * 100000 + col_diff;

        if (best_dist < 0 || dist < best_dist)
        {
            best_dist = dist;
            best_index = i;
        }

        if (i == text_len)
        {
            break;
        }

        if (text[i] == EDITOR_KEY_ENTER)
        {
            current_x = origin_x;
            current_y += char_h;
            continue;
        }
        if (current_x >= ending_x)
        {
            current_x = origin_x;
            current_y += char_h;
        }
        current_x += char_w;
    }

    return best_index;
}

// First character of target_index's row, and that row's y
void editor_row_start(struct font *font, int origin_x, int origin_y, int width, int target_index, int *out_index, int *out_y)
{
    int current_x = origin_x;
    int current_y = origin_y;
    int ending_x = origin_x + width;
    int row_index = 0;
    int row_y = origin_y;

    for (int i = 0; i < target_index && i < text_len; i++)
    {
        if (text[i] == EDITOR_KEY_ENTER)
        {
            current_x = origin_x;
            current_y += font->bits_height_per_character;
            row_index = i + 1;
            row_y = current_y;
            continue;
        }
        if (current_x >= ending_x)
        {
            current_x = origin_x;
            current_y += font->bits_height_per_character;
            row_index = i;
            row_y = current_y;
        }
        current_x += font->bits_width_per_character;
    }

    *out_index = row_index;
    *out_y = row_y;
}

// Repaints from from_index's row down. Redrawing the whole window per
// keystroke cost ~480k pixel writes a character.
void editor_redraw(struct window *win, struct graphics *canvas, struct font *font, int from_index)
{
    int origin_x = EDITOR_MARGIN;
    int origin_y = EDITOR_MARGIN;
    int width = win->width - EDITOR_MARGIN * 2;

    if (from_index < 0)
    {
        from_index = 0;
    }

    int row_index = 0;
    int row_y = origin_y;
    editor_row_start(font, origin_x, origin_y, width, from_index, &row_index, &row_y);

    // Clear down to the text, not the window bottom
    int end_x = 0;
    int end_y = 0;
    editor_screen_pos(font, origin_x, origin_y, width, text_len, &end_x, &end_y);

    int bottom = end_y + font->bits_height_per_character;
    if (last_bottom_y > bottom)
    {
        bottom = last_bottom_y;
    }
    if (bottom > win->height)
    {
        bottom = win->height;
    }
    last_bottom_y = end_y + font->bits_height_per_character;

    int dirty_height = bottom - row_y;
    if (dirty_height <= 0)
    {
        return;
    }

    graphics_draw_rect(canvas, 0, row_y, win->width, dirty_height, color_white);
    font_draw_text_wrap(canvas, font, origin_x, row_y, width, win->height - EDITOR_MARGIN - row_y, &text[row_index], color_ink);

    int cursor_x = 0;
    int cursor_y = 0;
    editor_screen_pos(font, origin_x, origin_y, width, cursor_index, &cursor_x, &cursor_y);
    graphics_draw_rect(canvas, cursor_x, cursor_y, 2, font->bits_height_per_character, color_ink);

    window_redraw_region(win, 0, row_y, win->width, dirty_height);
}

int main(int argc, char **argv)
{
    graphics_image_formats_init();
    font_system_init();

    struct window *main_win = window_create("Editor", 560, 400, 0, 557);
    if (!main_win)
    {
        return -1;
    }

    struct graphics *canvas = (struct graphics *)window_graphics(main_win);
    if (!canvas || !graphics_get_pixel_buffer(canvas))
    {
        printf("Failed to access editor canvas\n");
        return -1;
    }

    struct font *font = font_get_system_font();
    if (!font)
    {
        printf("Failed to load system font\n");
        return -1;
    }

    if (!editor_ensure_capacity(0))
    {
        printf("Failed to allocate text buffer\n");
        return -1;
    }
    text[0] = 0;

    editor_redraw(main_win, canvas, font, 0);

    struct window_event event = {0};
    while (1)
    {
        int res = window_get_event(&event);
        if (res < 0)
        {
            // Queue is empty; yield briefly without hurting typing latency
            usleep(2);
            continue;
        }

        switch (event.type)
        {
        case WINDOW_EVENT_TYPE_KEY_PRESS:
        {
            int key = event.data.keypress.key;
            bool is_backspace = key == EDITOR_KEY_BACKSPACE;
            bool is_printable = key == EDITOR_KEY_ENTER || (key >= 0x20 && key < 0x7f);

            if (is_backspace && cursor_index == 0)
            {
                break;
            }
            if (!is_backspace && !is_printable)
            {
                break;
            }

            int probe_index = is_backspace ? cursor_index - 1 : cursor_index;

            if (is_backspace)
            {
                editor_backspace();
            }
            else
            {
                editor_insert((char)key);
            }

            editor_redraw(main_win, canvas, font, probe_index);
            break;
        }

        case WINDOW_EVENT_TYPE_MOUSE_CLICK:
        {
            int width = main_win->width - EDITOR_MARGIN * 2;
            int old_cursor = cursor_index;
            cursor_index = editor_index_at_point(font, EDITOR_MARGIN, EDITOR_MARGIN, width, event.data.click.x, event.data.click.y);

            // Covers erasing the old caret and drawing the new one
            editor_redraw(main_win, canvas, font, old_cursor < cursor_index ? old_cursor : cursor_index);
            break;
        }

        case WINDOW_EVENT_TYPE_RESIZE:
            // Resize reallocates the body buffer, so re-fetch it. The text
            // buffer is untouched, so nothing is lost.
            main_win->width = event.data.resize.width;
            main_win->height = event.data.resize.height;
            canvas->width = event.data.resize.width;
            canvas->height = event.data.resize.height;
            canvas->pixels = NULL;
            if (!graphics_get_pixel_buffer(canvas))
            {
                printf("Failed to remap editor canvas after resize\n");
                return -1;
            }
            editor_redraw(main_win, canvas, font, 0);
            break;

        case WINDOW_EVENT_TYPE_WINDOW_CLOSE:
            return 0;
        }
    }

    return 0;
}
