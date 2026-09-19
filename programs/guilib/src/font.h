#ifndef USERLAND_GRAPHICS_FONT_H
#define USERLAND_GRAPHICS_FONT_H

#include <stdint.h>
#include <stddef.h>
#include "graphics.h"

// 32 is space in the ASCII table; the font character data begins there. We
// subtract this from a character's ASCII code to get its index in the
// character array where that glyph's pixel data starts.
#define FONT_IMAGE_DRAW_SUBTRACT_FROM_INDEX 32
// 9x16 per character.
#define FONT_IMAGE_CHARACTER_WIDTH_PIXEL_SIZE 9
#define FONT_IMAGE_CHARACTER_HEIGHT_PIXEL_SIZE 16
#define FONT_IMAGE_CHARACTER_Y_OFFSET 4

struct font
{
    // Follows the ASCII table, starting from SPACE
    size_t character_count;
    // Each bit represents one pixel of a given character
    uint8_t *character_data;

    size_t bits_width_per_character;
    size_t bits_height_per_character;

    // Amount to subtract from the ASCII character code when drawing
    uint8_t subtract_from_ascii_char_index_for_drawing;

    char filename[200];
};

int font_system_init();
struct font *font_get_system_font();

struct font *font_load(const char *filename);
struct font *font_create(uint8_t *character_data, size_t character_count, size_t bits_width_per_character, size_t bits_height_per_character, uint8_t subtract_from_ascii_char_index_for_drawing);
int font_draw(struct graphics *graphics_info, struct font *font, int screen_x, int screen_y, int character, struct framebuffer_pixel font_color);
int font_draw_text(struct graphics *graphics_info, struct font *font, int screen_x, int screen_y, const char *str, struct framebuffer_pixel font_color);
int font_draw_text_wrap(struct graphics *graphics_info, struct font *font, int screen_x, int screen_y, int width, int height, const char *str, struct framebuffer_pixel font_color);

#endif
