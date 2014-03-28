#ifndef __bitmap_font__
#define __bitmap_font__

#include <stdint.h>
#include "rect.h"
#include "display_pixel.h"

// Bitmap tiled fonts
typedef struct
{
    const char **pixels;

    uint16_t letter_width;
    uint16_t letter_height;
    unsigned bpp;
    unsigned min_char;
    unsigned max_char;
} BitmapFont_t;

void font_init(BitmapFont_t *font);
void font_printchar(BitmapFont_t *font, DisplayPixel_t *buffer, unsigned buffer_stride, uint8_t c, DisplayPixel_t color);
unsigned font_printstr(BitmapFont_t *font, DisplayPixel_t *buffer, unsigned buffer_stride,
                       const char *str, Rect_t *clip);

#endif
