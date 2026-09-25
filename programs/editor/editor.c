#include "stdlib.h"
#include "stdio.h"
#include "window.h"
#include "graphics.h"
#include "image.h"
#include "font.h"
#include "string.h"
#include "marrowos.h"
#include "rain.h"

#define EDITOR_TEXT_INITIAL_CAPACITY 4096
#define EDITOR_MARGIN 6
#define EDITOR_KEY_BACKSPACE 0x08
#define EDITOR_KEY_ENTER 0x0d

#define TOOLBAR_HEIGHT 30
#define TOOLBAR_MARGIN 8
#define RUN_BUTTON_WIDTH 56
#define DEMO_BUTTON_WIDTH 56
#define TOOLBAR_BUTTON_GAP 8
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
#define PANEL_MAXIMIZE_WIDTH 28
#define PANEL_ICON_SIZE 10

struct framebuffer_pixel color_white = {.red = 0xff, .green = 0xff, .blue = 0xff, .reserved = 0};
struct framebuffer_pixel color_ink = {.red = 0x20, .green = 0x20, .blue = 0x20, .reserved = 0};
struct framebuffer_pixel color_selection = {.red = 0xb4, .green = 0xd5, .blue = 0xfe, .reserved = 0};
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

// Selection runs between selection_anchor (where the drag started) and cursor_index. It is empty when
// the anchor is -1 or equal to the cursor
int selection_anchor = -1;
bool selecting = false;

// Terminal panel state. The panel is drawn on the editor's own canvas, and keys go to it while it has focus
bool panel_open = false;
bool panel_focused = false;
// When set, the panel covers the whole text area (everything below the toolbar)
bool panel_maximized = false;
char panel_history[PANEL_HISTORY][PANEL_LINE_MAX];
int panel_history_count = 0;
char panel_input[PANEL_LINE_MAX];
int panel_input_len = 0;

// Text extent at the last redraw, so shrinking text clears its old rows
int last_bottom_y = 0;

int panel_height(struct window *win)
{
    if (panel_maximized)
    {
        return win->height - TOOLBAR_HEIGHT;
    }

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

// The selected range as [start, end), or false when nothing is selected
bool selection_bounds(int *start, int *end)
{
    if (selection_anchor < 0 || selection_anchor == cursor_index)
    {
        return false;
    }
    *start = selection_anchor < cursor_index ? selection_anchor : cursor_index;
    *end = selection_anchor < cursor_index ? cursor_index : selection_anchor;
    return true;
}

// Removes text[start, end) and leaves the cursor where it was
void editor_delete_range(int start, int end)
{
    int count = end - start;
    for (int i = start; i + count <= text_len; i++)
    {
        text[i] = text[i + count];
    }
    text_len -= count;
    text[text_len] = 0;
    cursor_index = start;
    selection_anchor = -1;
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

// Paints the highlight behind the selected characters, walking the text the way font_draw_text_wrap does.
// A selected line break gets a one character wide mark at the end of its line
void editor_draw_selection(struct graphics *canvas, struct font *font, int origin_x, int row_y, int width, int row_index, int area_bottom)
{
    int start = 0;
    int end = 0;
    if (!selection_bounds(&start, &end))
    {
        return;
    }

    int char_w = font->bits_width_per_character;
    int char_h = font->bits_height_per_character;
    int x = origin_x;
    int y = row_y;
    int ending_x = origin_x + width;

    for (int i = row_index; i < text_len && i < end; i++)
    {
        if (y + char_h > area_bottom)
        {
            break;
        }
        if (text[i] == EDITOR_KEY_ENTER)
        {
            if (i >= start)
            {
                graphics_draw_rect(canvas, x, y, char_w, char_h, color_selection);
            }
            x = origin_x;
            y += char_h;
            continue;
        }
        if (x >= ending_x)
        {
            x = origin_x;
            y += char_h;
            if (y + char_h > area_bottom)
            {
                break;
            }
        }
        if (i >= start)
        {
            graphics_draw_rect(canvas, x, y, char_w, char_h, color_selection);
        }
        x += char_w;
    }
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
    editor_draw_selection(canvas, font, origin_x, row_y, width, row_index, area_bottom);
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

int run_button_x(struct window *win)
{
    return win->width - RUN_BUTTON_WIDTH - TOOLBAR_MARGIN;
}

// Demo sits just left of Run
int demo_button_x(struct window *win)
{
    return run_button_x(win) - TOOLBAR_BUTTON_GAP - DEMO_BUTTON_WIDTH;
}

void toolbar_button_draw(struct graphics *canvas, struct font *font, int x, int width, const char *label)
{
    int y = (TOOLBAR_HEIGHT - RUN_BUTTON_HEIGHT) / 2;
    graphics_draw_rect(canvas, x, y, width, RUN_BUTTON_HEIGHT, color_run_button);
    graphics_draw_rect(canvas, x, y, width, 2, color_toolbar_border);
    graphics_draw_rect(canvas, x, y + RUN_BUTTON_HEIGHT - 2, width, 2, color_toolbar_border);
    graphics_draw_rect(canvas, x, y, 2, RUN_BUTTON_HEIGHT, color_toolbar_border);
    graphics_draw_rect(canvas, x + width - 2, y, 2, RUN_BUTTON_HEIGHT, color_toolbar_border);
    font_draw_text(canvas, font, x + (width - (int)strlen(label) * (int)font->bits_width_per_character) / 2,
                   y + (RUN_BUTTON_HEIGHT - (int)font->bits_height_per_character) / 2, label, color_toolbar_border);
}

void toolbar_draw(struct window *win, struct graphics *canvas, struct font *font)
{
    graphics_draw_rect(canvas, 0, 0, win->width, TOOLBAR_HEIGHT, color_toolbar);
    toolbar_button_draw(canvas, font, demo_button_x(win), DEMO_BUTTON_WIDTH, "Demo");
    toolbar_button_draw(canvas, font, run_button_x(win), RUN_BUTTON_WIDTH, "Run");
}

static bool toolbar_button_hit(int button_x, int button_width, int x, int y)
{
    int by = (TOOLBAR_HEIGHT - RUN_BUTTON_HEIGHT) / 2;
    return x >= button_x && x < button_x + button_width && y >= by && y < by + RUN_BUTTON_HEIGHT;
}

bool run_button_hit(struct window *win, int x, int y)
{
    return toolbar_button_hit(run_button_x(win), RUN_BUTTON_WIDTH, x, y);
}

bool demo_button_hit(struct window *win, int x, int y)
{
    return toolbar_button_hit(demo_button_x(win), DEMO_BUTTON_WIDTH, x, y);
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

// A 1 pixel outline
void panel_draw_frame(struct graphics *canvas, int x, int y, int width, int height, struct framebuffer_pixel color)
{
    graphics_draw_rect(canvas, x, y, width, 1, color);
    graphics_draw_rect(canvas, x, y + height - 1, width, 1, color);
    graphics_draw_rect(canvas, x, y, 1, height, color);
    graphics_draw_rect(canvas, x + width - 1, y, 1, height, color);
}

// The expand icon is a window with a thick top edge. When expanded it is two overlapping
// squares, the usual "restore" icon
void panel_draw_maximize_icon(struct graphics *canvas, int x, int y)
{
    if (!panel_maximized)
    {
        panel_draw_frame(canvas, x, y, PANEL_ICON_SIZE, PANEL_ICON_SIZE, color_panel_text);
        graphics_draw_rect(canvas, x, y, PANEL_ICON_SIZE, 2, color_panel_text);
        return;
    }

    int small = PANEL_ICON_SIZE - 2;
    graphics_draw_rect(canvas, x + 2, y, small, 1, color_panel_text);
    graphics_draw_rect(canvas, x + PANEL_ICON_SIZE - 1, y, 1, small - 1, color_panel_text);
    panel_draw_frame(canvas, x, y + 2, small, small, color_panel_text);
    graphics_draw_rect(canvas, x, y + 2, small, 2, color_panel_text);
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
    panel_draw_maximize_icon(canvas, win->width - PANEL_CLOSE_WIDTH - PANEL_MAXIMIZE_WIDTH + (PANEL_MAXIMIZE_WIDTH - PANEL_ICON_SIZE) / 2,
                             top + (PANEL_HEADER_HEIGHT - PANEL_ICON_SIZE) / 2);

    int max_chars = (win->width - PANEL_PADDING * 2) / char_w;
    int rows = (panel_h - PANEL_HEADER_HEIGHT - 4) / char_h;
    if (rows < 1 || max_chars < 4)
    {
        window_redraw_region(win, 0, top, win->width, panel_h);
        return;
    }

    // Output starts at the top and the prompt follows it. Once the panel is full the oldest lines scroll off
    int history_rows = panel_history_count < rows - 1 ? panel_history_count : rows - 1;
    int first = panel_history_count - history_rows;
    int y = top + PANEL_HEADER_HEIGHT + 2;
    for (int i = first; i < panel_history_count; i++)
    {
        panel_draw_clipped(canvas, font, PANEL_PADDING, y, panel_history[i], max_chars, color_panel_text);
        y += char_h;
    }

    // Long input shows its tail, so what is being typed stays visible
    int prompt_y = y;
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

// The program the Demo button puts in the editor
static const char demo_source[] =
    "// Rain demo: press Run, then expand the terminal\n"
    "print \"Hello from Rain!\";\n"
    "\n"
    "fun fib(n) {\n"
    "    if (n < 2) return n;\n"
    "    return fib(n - 1) + fib(n - 2);\n"
    "}\n"
    "\n"
    "for (var i = 0; i < 8; i = i + 1) {\n"
    "    print \"fib(\" + toString(i) + \") = \" + toString(fib(i));\n"
    "}\n"
    "\n"
    "fun counter() {\n"
    "    var c = 0;\n"
    "    fun next() {\n"
    "        c = c + 1;\n"
    "        return c;\n"
    "    }\n"
    "    return next;\n"
    "}\n"
    "\n"
    "var tick = counter();\n"
    "tick();\n"
    "tick();\n"
    "print \"closure counted \" + toString(tick());\n"
    "\n"
    "var names = #[\"rain\", \"marrow\", \"os\"];\n"
    "for (name von names) {\n"
    "    print \"hello \" + name;\n"
    "}\n";

// Replaces the editor text with the demo. Enter is stored as a carriage return in this editor
static void editor_load_demo()
{
    int length = (int)strlen(demo_source);
    if (!editor_ensure_capacity(length + 1))
    {
        return;
    }

    for (int i = 0; i < length; i++)
    {
        text[i] = demo_source[i] == '\n' ? EDITOR_KEY_ENTER : demo_source[i];
    }
    text[length] = 0;
    text_len = length;
    cursor_index = 0;
    selection_anchor = -1;
    selecting = false;
    panel_focused = false;
}

// Rain prints in pieces, so text is gathered into lines before it goes to the panel
static char run_line[PANEL_LINE_MAX];
static int run_line_len = 0;

static void run_flush_line()
{
    run_line[run_line_len] = 0;
    panel_push_line(run_line);
    run_line_len = 0;
}

static void editor_rain_write(const char *text, int length, int is_error)
{
    for (int i = 0; i < length; i++)
    {
        char c = text[i];
        if (c == '\n')
        {
            run_flush_line();
            continue;
        }
        if (c == '\r')
        {
            continue;
        }
        if (c == '\t')
        {
            c = ' ';
        }
        if (c < 0x20 || c >= 0x7f)
        {
            c = '?';
        }
        if (run_line_len == PANEL_LINE_MAX - 1)
        {
            run_flush_line();
        }
        run_line[run_line_len++] = c;
    }
}

// Runs the editor text as a Rain program and puts everything it prints into the panel
static void editor_run_script()
{
    panel_history_count = 0;
    panel_input_len = 0;
    panel_input[0] = 0;
    run_line_len = 0;

    // The editor stores Enter as a carriage return, but Rain ends lines (and // comments) at \n
    char *source = malloc(text_len + 1);
    if (!source)
    {
        panel_push_line("not enough memory to run");
        return;
    }
    for (int i = 0; i < text_len; i++)
    {
        source[i] = text[i] == EDITOR_KEY_ENTER ? '\n' : text[i];
    }
    source[text_len] = 0;

    rain_run(source, editor_rain_write);
    free(source);

    if (run_line_len > 0)
    {
        run_flush_line();
    }
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
    int probe_index_override = -1;

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

            // Ctrl+A arrives as code 1: select all the text
            if (key == 0x01 && !(panel_open && panel_focused))
            {
                selection_anchor = 0;
                cursor_index = text_len;
                editor_redraw(main_win, canvas, font, 0);
                break;
            }

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
                        panel_maximized = false;
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

            if (!is_backspace && !is_printable)
            {
                break;
            }
            // Nothing to delete before the first character, unless a selection is being removed
            int pending_start = 0;
            int pending_end = 0;
            if (is_backspace && cursor_index == 0 && !selection_bounds(&pending_start, &pending_end))
            {
                break;
            }

            // Backspace removes a selection whole, and typing replaces it
            int selection_start = 0;
            int selection_end = 0;
            if (selection_bounds(&selection_start, &selection_end))
            {
                editor_delete_range(selection_start, selection_end);
                if (is_backspace)
                {
                    editor_redraw(main_win, canvas, font, selection_start);
                    break;
                }
                probe_index_override = selection_start;
            }

            int probe_index = is_backspace ? cursor_index - 1 : cursor_index;
            if (probe_index_override >= 0)
            {
                probe_index = probe_index_override;
                probe_index_override = -1;
            }

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
            // The kernel repeats clicks while the button is held; only the first is a press.
            // The repeats carry the pointer position, which is how a drag grows the selection
            if (mouse_down)
            {
                if (selecting)
                {
                    int drag_width = main_win->width - EDITOR_MARGIN * 2;
                    int drag_y = event.data.click.y;
                    // Dragging into the toolbar or panel keeps the selection at the edge of the text area
                    int area_bottom = text_bottom(main_win);
                    if (drag_y >= area_bottom)
                    {
                        drag_y = area_bottom - 1;
                    }
                    if (drag_y < TEXT_ORIGIN_Y)
                    {
                        drag_y = TEXT_ORIGIN_Y;
                    }

                    int previous_cursor = cursor_index;
                    cursor_index = editor_index_at_point(font, EDITOR_MARGIN, TEXT_ORIGIN_Y, drag_width, event.data.click.x, drag_y);
                    if (cursor_index != previous_cursor)
                    {
                        editor_redraw(main_win, canvas, font, previous_cursor < cursor_index ? previous_cursor : cursor_index);
                    }
                }
                break;
            }
            mouse_down = true;

            int click_x = event.data.click.x;
            int click_y = event.data.click.y;

            if (click_y < TOOLBAR_HEIGHT)
            {
                if (demo_button_hit(main_win, click_x, click_y))
                {
                    editor_load_demo();
                    editor_layout_redraw(main_win, canvas, font);
                }
                else if (run_button_hit(main_win, click_x, click_y))
                {
                    bool was_open = panel_open;
                    panel_open = true;
                    panel_focused = true;
                    editor_run_script();
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
                bool in_header = click_y < panel_top + PANEL_HEADER_HEIGHT;
                if (in_header && click_x >= main_win->width - PANEL_CLOSE_WIDTH)
                {
                    panel_open = false;
                    panel_focused = false;
                    panel_maximized = false;
                    editor_layout_redraw(main_win, canvas, font);
                    break;
                }

                if (in_header && click_x >= main_win->width - PANEL_CLOSE_WIDTH - PANEL_MAXIMIZE_WIDTH)
                {
                    // Expand over the text area, or shrink back
                    panel_maximized = !panel_maximized;
                    panel_focused = true;
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

            // Whatever was selected before has to be repainted too
            int old_selection_start = 0;
            int old_selection_end = 0;
            int redraw_from = old_cursor;
            if (selection_bounds(&old_selection_start, &old_selection_end))
            {
                redraw_from = old_selection_start < redraw_from ? old_selection_start : redraw_from;
            }

            cursor_index = editor_index_at_point(font, EDITOR_MARGIN, TEXT_ORIGIN_Y, width, click_x, click_y);
            // A new selection starts here and grows while the button stays down
            selection_anchor = cursor_index;
            selecting = true;
            bool focus_changed = panel_focused;
            panel_focused = false;

            // Covers erasing the old caret and selection and drawing the new caret
            editor_redraw(main_win, canvas, font, redraw_from < cursor_index ? redraw_from : cursor_index);
            if (focus_changed && panel_open)
            {
                panel_draw(main_win, canvas, font);
            }
            break;
        }

        case WINDOW_EVENT_TYPE_MOUSE_RELEASE:
            mouse_down = false;
            selecting = false;
            // A click without a drag selects nothing
            if (selection_anchor == cursor_index)
            {
                selection_anchor = -1;
            }
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
            selecting = false;
            editor_layout_redraw(main_win, canvas, font);
            break;

        case WINDOW_EVENT_TYPE_WINDOW_CLOSE:
            return 0;
        }
    }

    return 0;
}
