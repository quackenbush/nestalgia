#ifndef __display_h__
#define __display_h__

#include <stdint.h>

#define MAX_WINDOWS 16

#include "bitmap_font.h"
#include "window.h"
#include "sprites.h"
#include "menubar.h"
#include "display_pixel.h"

typedef struct Display
{
    const char *title;

    int width;
    int height;
    int stride;

    int fullscreen;

    unsigned depth_in_bytes;
    unsigned show_cursor;

    void *p;

    void *background;

    unsigned windowed;

    MenuBar_t menubar;

    struct
    {
        unsigned down;
        int x;
        int y;
    } mouse;

    struct
    {
        Window_t *window;
        int x;
        int y;

        int orig_x;
        int orig_y;
    } selected;

    BitmapFont_t *font;

    unsigned num_windows;
    Window_t *windows[MAX_WINDOWS];

    Window_t *bottom_window;
    Window_t *top_window;
} Display_t;

uint32_t display_maprgb(Display_t *display, uint8_t r, uint8_t g, uint8_t b);

void display_init(Display_t *display, MenuItem_t *menu);
void display_set_menubar(Display_t *display, int visible);
void display_destroy(Display_t *display);
void display_reset(Display_t *display);

void display_add_window(Display_t *display, Window_t *window);
void display_select_window(Display_t *display, Window_t *window);
void display_hide_window(Display_t *display, Window_t *window);

void display_mousedown(Display_t *display, int x, int y);
void display_mousemove(Display_t *display, int x, int y);
void display_mouseup(Display_t *display, int x, int y);

void display_draw(Display_t *display);

void display_line(DisplayPixel_t *origin, int x1, int y1, int x2, int y2, DisplayPixel_t color, int stride, Rect_t *clip);
void display_box (DisplayPixel_t *origin, int x1, int y1, int x2, int y2, DisplayPixel_t color, int stride, Rect_t *clip);
void display_fillrect(DisplayPixel_t *origin, int width, int height, DisplayPixel_t color, int stride, Rect_t *clip);
void display_draw_sprite(Display_t *display, const Sprite_t *sprite, DisplayPixel_t *origin, int stride, Rect_t *clip);

void display_fullscreen(Display_t *display, int fullscreen);

#endif
