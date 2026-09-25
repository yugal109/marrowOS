#include "stdlib.h"
#include "stdio.h"
#include "window.h"
#include "graphics.h"
#include "image.h"
#include "font.h"
#include "string.h"
#include "marrowos.h"

#define EDITOR_TEXT_INITIAL_CAPACITY 4096
#define EDITOR_MARGIN 6
#define EDITOR_KEY_BACKSPACE 0x08
#define EDITOR_KEY_ENTER 0x0d

#define TOOLBAR_HEIGHT 30
#define TOOLBAR_MARGIN 8
#define RUN_BUTTON_WIDTH 56
#define RUN_BUTTON_HEIGHT 20
#define TEXT_ORIGIN_Y (TOOLBAR_HEIGHT + EDITOR_MARGIN)

// The terminal panel covers the bottom part of the window, full width
#define PANEL_PERCENT 30
#define PANEL_MIN_HEIGHT 72
#define PANEL_HEADER_HEIGHT 20
#define PANEL_PADDING 8
#define PANEL_HISTORY 32
#define PANEL_LINE_MAX 96
#define PANEL_CLOSE_WIDTH 28

struct framebuffer_pixel color_white = {.red = 0xff, .green = 0xff, .blue = 0xff, .reserved = 0};
struct framebuffer_pixel color_ink = {.red = 0x20, .green = 0x20, .blue = 0x20, .reserved = 0};
struct framebuffer_pixel color_toolbar = {.red = 0xe0, .green = 0xe0, .blue = 0xe0, .reserved = 0};
struct framebuffer_pixel color_toolbar_border = {.red = 0x40, .green = 0x40, .blue = 0x40, .reserved = 0};
struct framebuffer_pixel color_run_button = {.red = 0xc8, .green = 0xdc, .blue = 0xf0, .reserved = 0};
struct framebuffer_pixel color_panel_bg = {.red = 0x1c, .green = 0x1c, .blue = 0x22, .reserved = 0};
struct framebuffer_pixel color_panel_header = {.red = 0x36, .green = 0x38, .blue = 0x42, .reserved = 0};
struct framebuffer_pixel color_panel_text = {.red = 0xe0, .green = 0xe0, .blue = 0xe0, .reserved = 0};
struct framebuffer_pixel color_panel_prompt = {.red = 0x3b, .green = 0xd6, .blue = 0x7a, .reserved = 0};

char *text = NULL;
int text_len = 0;
int text_capacity = 0;
int cursor_index = 0;

// Terminal panel state. The panel is drawn on the editor's own canvas, and keys go to it while it has focus
bool panel_open = false;
bool panel_focused = false;
char panel_history[PANEL_HISTORY][PANEL_LINE_MAX];
int panel_history_count = 0;
char panel_input[PANEL_LINE_MAX];
int panel_input_len = 0;

// Text extent at the last redraw, so shrinking text clears its old rows
int last_bottom_y = 0;

int panel_height(struct window *win)
{
    int height = win->height * PANEL_PERCENT / 100;
    if (height < PANEL_MIN_HEIGHT)
    {
        height = PANEL_MIN_HEIGHT;
    }

    // Always leave the toolbar and a few text rows
    int max_height = win->height - TOOLBAR_HEIGHT - 48;
    if (height > max_height)
    {
        height = max_height;
    }
    if (height < PANEL_HEADER_HEIGHT)
    {
        height = PANEL_HEADER_HEIGHT;
    }
    return height;
}

// First row below the text area: the window bottom, or the panel top when it is open
int text_bottom(struct window *win)
{
    return panel_open ? win->height - panel_height(win) : win->height;
}

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
    int origin_y = TEXT_ORIGIN_Y;
    int width = win->width - EDITOR_MARGIN * 2;
    int area_bottom = text_bottom(win);

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
    if (bottom > area_bottom)
    {
        bottom = area_bottom;
    }
    last_bottom_y = end_y + font->bits_height_per_character;

    int dirty_height = bottom - row_y;
    if (dirty_height <= 0)
    {
        return;
    }

    graphics_draw_rect(canvas, 0, row_y, win->width, dirty_height, color_white);
    // A row is drawn whole once it starts, so stop a row early to stay above the panel
    int draw_height = area_bottom - font->bits_height_per_character + 1 - row_y;
    if (draw_height > 0)
    {
        font_draw_text_wrap(canvas, font, origin_x, row_y, width, draw_height, &text[row_index], color_ink);
    }

    int cursor_x = 0;
    int cursor_y = 0;
    editor_screen_pos(font, origin_x, origin_y, width, cursor_index, &cursor_x, &cursor_y);
    if (!panel_focused && cursor_y + (int)font->bits_height_per_character <= area_bottom)
    {
        graphics_draw_rect(canvas, cursor_x, cursor_y, 2, font->bits_height_per_character, color_ink);
    }

    window_redraw_region(win, 0, row_y, win->width, dirty_height);
}

void toolbar_draw(struct window *win, struct graphics *canvas, struct font *font)
{
    graphics_draw_rect(canvas, 0, 0, win->width, TOOLBAR_HEIGHT, color_toolbar);

    int x = win->width - RUN_BUTTON_WIDTH - TOOLBAR_MARGIN;
    int y = (TOOLBAR_HEIGHT - RUN_BUTTON_HEIGHT) / 2;
    graphics_draw_rect(canvas, x, y, RUN_BUTTON_WIDTH, RUN_BUTTON_HEIGHT, color_run_button);
    graphics_draw_rect(canvas, x, y, RUN_BUTTON_WIDTH, 2, color_toolbar_border);
    graphics_draw_rect(canvas, x, y + RUN_BUTTON_HEIGHT - 2, RUN_BUTTON_WIDTH, 2, color_toolbar_border);
    graphics_draw_rect(canvas, x, y, 2, RUN_BUTTON_HEIGHT, color_toolbar_border);
    graphics_draw_rect(canvas, x + RUN_BUTTON_WIDTH - 2, y, 2, RUN_BUTTON_HEIGHT, color_toolbar_border);
    font_draw_text(canvas, font, x + (RUN_BUTTON_WIDTH - 3 * (int)font->bits_width_per_character) / 2,
                   y + (RUN_BUTTON_HEIGHT - (int)font->bits_height_per_character) / 2, "Run", color_toolbar_border);
}

bool run_button_hit(struct window *win, int x, int y)
{
    int bx = win->width - RUN_BUTTON_WIDTH - TOOLBAR_MARGIN;
    int by = (TOOLBAR_HEIGHT - RUN_BUTTON_HEIGHT) / 2;
    return x >= bx && x < bx + RUN_BUTTON_WIDTH && y >= by && y < by + RUN_BUTTON_HEIGHT;
}

// Drawn text does not clip, so cut it to what fits on the row
void panel_draw_clipped(struct graphics *canvas, struct font *font, int x, int y, const char *str, int max_chars, struct framebuffer_pixel color)
{
    char clipped[PANEL_LINE_MAX];
    int length = strlen(str);
    if (length > max_chars)
    {
        length = max_chars;
    }
    if (length > PANEL_LINE_MAX - 1)
    {
        length = PANEL_LINE_MAX - 1;
    }
    for (int i = 0; i < length; i++)
    {
        clipped[i] = str[i];
    }
    clipped[length] = 0;
    font_draw_text(canvas, font, x, y, clipped, color);
}

void panel_draw(struct window *win, struct graphics *canvas, struct font *font)
{
    int panel_h = panel_height(win);
    int top = win->height - panel_h;
    int char_w = font->bits_width_per_character;
    int char_h = font->bits_height_per_character;

    graphics_draw_rect(canvas, 0, top, win->width, panel_h, color_panel_bg);
    graphics_draw_rect(canvas, 0, top, win->width, PANEL_HEADER_HEIGHT, color_panel_header);
    font_draw_text(canvas, font, PANEL_PADDING, top + (PANEL_HEADER_HEIGHT - char_h) / 2, "Terminal", color_panel_text);
    font_draw_text(canvas, font, win->width - PANEL_CLOSE_WIDTH + (PANEL_CLOSE_WIDTH - char_w) / 2, top + (PANEL_HEADER_HEIGHT - char_h) / 2, "X", color_panel_text);

    int max_chars = (win->width - PANEL_PADDING * 2) / char_w;
    int rows = (panel_h - PANEL_HEADER_HEIGHT - 4) / char_h;
    if (rows < 1 || max_chars < 4)
    {
        window_redraw_region(win, 0, top, win->width, panel_h);
        return;
    }

    // The last row is the prompt, the rows above it are the newest output
    int history_rows = rows - 1;
    int first = panel_history_count > history_rows ? panel_history_count - history_rows : 0;
    int y = top + PANEL_HEADER_HEIGHT + 2;
    for (int i = first; i < panel_history_count; i++)
    {
        panel_draw_clipped(canvas, font, PANEL_PADDING, y, panel_history[i], max_chars, color_panel_text);
        y += char_h;
    }

    // Long input shows its tail, so what is being typed stays visible
    int prompt_y = top + PANEL_HEADER_HEIGHT + 2 + history_rows * char_h;
    font_draw_text(canvas, font, PANEL_PADDING, prompt_y, ">", color_panel_prompt);
    int input_chars = max_chars - 3;
    int start = panel_input_len > input_chars ? panel_input_len - input_chars : 0;
    panel_draw_clipped(canvas, font, PANEL_PADDING + 2 * char_w, prompt_y, &panel_input[start], input_chars, color_panel_text);
    if (panel_focused)
    {
        int caret_x = PANEL_PADDING + (2 + panel_input_len - start) * char_w;
        graphics_draw_rect(canvas, caret_x, prompt_y, 2, char_h, color_panel_text);
    }

    window_redraw_region(win, 0, top, win->width, panel_h);
}

// Repaints everything: toolbar, text area and panel
void editor_layout_redraw(struct window *win, struct graphics *canvas, struct font *font)
{
    graphics_draw_rect(canvas, 0, 0, win->width, win->height, color_white);
    toolbar_draw(win, canvas, font);
    last_bottom_y = 0;
    editor_redraw(win, canvas, font, 0);
    if (panel_open)
    {
        panel_draw(win, canvas, font);
    }
    window_redraw(win);
}

void panel_push_line(const char *line)
{
    if (panel_history_count == PANEL_HISTORY)
    {
        for (int i = 1; i < PANEL_HISTORY; i++)
        {
            strncpy(panel_history[i - 1], panel_history[i], PANEL_LINE_MAX);
        }
        panel_history_count--;
    }
    strncpy(panel_history[panel_history_count], line, PANEL_LINE_MAX - 1);
    panel_history[panel_history_count][PANEL_LINE_MAX - 1] = 0;
    panel_history_count++;
}

// Returns true when the command closed the panel
bool panel_run_command()
{
    char line[PANEL_LINE_MAX];
    line[0] = '>';
    line[1] = ' ';
    strncpy(&line[2], panel_input, PANEL_LINE_MAX - 3);
    line[PANEL_LINE_MAX - 1] = 0;
    panel_push_line(line);

    char command[PANEL_LINE_MAX];
    strncpy(command, panel_input, PANEL_LINE_MAX - 1);
    command[PANEL_LINE_MAX - 1] = 0;
    panel_input_len = 0;
    panel_input[0] = 0;

    if (command[0] == 0)
    {
        return false;
    }

    if (strncmp(command, "clear", 6) == 0)
    {
        panel_history_count = 0;
        return false;
    }
    if (strncmp(command, "exit", 5) == 0)
    {
        return true;
    }

    // Same launcher the Terminal app uses
    if (marrowos_system_run(command) < 0)
    {
        panel_push_line("command not found");
    }
    return false;
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

    editor_layout_redraw(main_win, canvas, font);

    bool mouse_down = false;

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

            if (panel_open && panel_focused)
            {
                if (key == EDITOR_KEY_BACKSPACE)
                {
                    if (panel_input_len > 0)
                    {
                        panel_input[--panel_input_len] = 0;
                    }
                }
                else if (key == EDITOR_KEY_ENTER || key == 0x0a)
                {
                    if (panel_run_command())
                    {
                        panel_open = false;
                        panel_focused = false;
                        editor_layout_redraw(main_win, canvas, font);
                        break;
                    }
                }
                else if (key >= 0x20 && key < 0x7f && panel_input_len < PANEL_LINE_MAX - 4)
                {
                    panel_input[panel_input_len++] = (char)key;
                    panel_input[panel_input_len] = 0;
                }
                panel_draw(main_win, canvas, font);
                break;
            }

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
            // The kernel repeats clicks while the button is held; only the first is a press
            if (mouse_down)
            {
                break;
            }
            mouse_down = true;

            int click_x = event.data.click.x;
            int click_y = event.data.click.y;

            if (click_y < TOOLBAR_HEIGHT)
            {
                if (run_button_hit(main_win, click_x, click_y))
                {
                    bool was_open = panel_open;
                    panel_open = true;
                    panel_focused = true;
                    if (was_open)
                    {
                        editor_redraw(main_win, canvas, font, cursor_index);
                        panel_draw(main_win, canvas, font);
                    }
                    else
                    {
                        editor_layout_redraw(main_win, canvas, font);
                    }
                }
                break;
            }

            if (panel_open && click_y >= main_win->height - panel_height(main_win))
            {
                int panel_top = main_win->height - panel_height(main_win);
                if (click_y < panel_top + PANEL_HEADER_HEIGHT && click_x >= main_win->width - PANEL_CLOSE_WIDTH)
                {
                    panel_open = false;
                    panel_focused = false;
                    editor_layout_redraw(main_win, canvas, font);
                    break;
                }

                if (!panel_focused)
                {
                    panel_focused = true;
                    editor_redraw(main_win, canvas, font, cursor_index);
                }
                panel_draw(main_win, canvas, font);
                break;
            }

            int width = main_win->width - EDITOR_MARGIN * 2;
            int old_cursor = cursor_index;
            cursor_index = editor_index_at_point(font, EDITOR_MARGIN, TEXT_ORIGIN_Y, width, click_x, click_y);
            bool focus_changed = panel_focused;
            panel_focused = false;

            // Covers erasing the old caret and drawing the new one
            editor_redraw(main_win, canvas, font, old_cursor < cursor_index ? old_cursor : cursor_index);
            if (focus_changed && panel_open)
            {
                panel_draw(main_win, canvas, font);
            }
            break;
        }

        case WINDOW_EVENT_TYPE_MOUSE_RELEASE:
            mouse_down = false;
            break;

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
            mouse_down = false;
            editor_layout_redraw(main_win, canvas, font);
            break;

        case WINDOW_EVENT_TYPE_WINDOW_CLOSE:
            return 0;
        }
    }

    return 0;
}
