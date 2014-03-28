#ifndef __window_h__
#define __window_h__

#include <stdint.h>
#include "bitmap_font.h"
#include "rect.h"
#include "display_pixel.h"

#define TITLEBAR_HEIGHT (window->font->letter_height + 4)

struct Display; // Forward declaration

typedef struct Window
{
    const char *title;
    BitmapFont_t *font;

    int width;
    int height;

    int x;
    int y;

    int selected;

    int visible;
    int has_titlebar;
    int can_close;
    int always_on_top;

    void *p; // User pointer
    void (*reinit)(struct Window *window);
    void (*draw)(struct Display *display, struct Window *window, DisplayPixel_t *origin, int stride, Rect_t *clip);
    void (*on_mousemove)(struct Window *window, int x, int y);
    void (*on_mousedown)(struct Window *window, int x, int y);
    void (*on_keydown)(struct Window *window, int key);

    struct Window *above;
    struct Window *below;
} Window_t;

void window_init(Window_t *window);
void window_reinit(Window_t *window);
void window_draw_titlebar(struct Display *display, Window_t *window, DisplayPixel_t color, DisplayPixel_t *origin, int stride, Rect_t *clip);
void window_click_titlebar(struct Display *display, Window_t *window, int x, int y);

void window_destroy(Window_t *window);

#endif
