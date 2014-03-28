#include <string.h>
#include "bitmap_font.h"
#include "log.h"

#define INFO(...) _INFO(MISC, __VA_ARGS__)

void font_init(BitmapFont_t *font)
{
    INFO("loaded font\n");
}

void font_printchar(BitmapFont_t *font, DisplayPixel_t *buffer, unsigned buffer_stride, uint8_t c, DisplayPixel_t color)
{
    unsigned y;
    DisplayPixel_t *b = buffer;
    const char *f;

    if(c < font->min_char || c > font->max_char)
        c = ' ';

    f = font->pixels[c - font->min_char];

    ASSERT(buffer_stride >= 16, "Bad font buffer stride: %d", buffer_stride);

    for(y = 0; y < font->letter_height; y++)
    {
        // FIXME: Assuming buffer is 2 byte per pixel
        unsigned x;
        if(font->bpp == 16)
        {
            for(x = 0; x < font->letter_width; x++)
            {
                if(*f)
                {
                    *b++ = *f;
                }
                else
                {
                    // Transparent
                    b++;
                }

                f += 2;
            }
        }
        else if(font->bpp == 1)
        {
            uint8_t z = *f++;
            for(x = 0; x < font->letter_width; x++)
            {
                if(z & 0x80)
                {
                    // Non-transparent color
                    *b = color;
                }
                b++;
                z <<= 1;
            }
        }
        else
        {
            ASSERT(0, "Bad font bpp: %d", font->bpp);
        }

        b += buffer_stride - font->letter_width;
    }
}

unsigned
font_printstr(BitmapFont_t *font, DisplayPixel_t *buffer, unsigned buffer_stride,
              const char *str, Rect_t *clip)
{
    const unsigned newline_size_bytes = buffer_stride * (font->letter_height);
    const unsigned char_width_pixels = font->letter_width;
    unsigned xpos = 0;
    unsigned ypos = 0;

    uint8_t c;
    int shadowed_font = 1;

    // FIXME: newline wrapping doesn't work
    while((c = *str++) != 0)
    {
        int newline = 0;

        if(c == '\n')
        {
            newline = 1;
        }
        else
        {
            if(shadowed_font)
            {
                // Draw shadow by drawing the char in black first
                font_printchar(font, buffer + 1 + buffer_stride, buffer_stride, c, 0x0);
            }
            font_printchar(font, buffer, buffer_stride, c, ~0);
            buffer += char_width_pixels;

            xpos += char_width_pixels;
            if(xpos >= buffer_stride)
            {
                // wrapped past the right-hand column; force a newline
                buffer -= buffer_stride;
                newline = 1;
            }
        }

        if(newline)
        {
            ypos += font->letter_height;
            buffer += newline_size_bytes;
            // stride back to lefthand column
            buffer -= xpos % buffer_stride;
            xpos = 0;
        }
    }

    return ypos;
}
