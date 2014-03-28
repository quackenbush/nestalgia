#include "window.h"
#include "log.h"
#include <string.h>
#include "display.h"

void
window_init(Window_t *window)
{
    ASSERT(window->title, "Missing title");
    ASSERT(window->draw, "Missing draw: Window [%s]", window->title);
    //ASSERT(window->font, "Missing font: Window [%s]", window->title);
    ASSERT(window->width > 0, "width <= 0: Window [%s]", window->title);
    ASSERT(window->height > 0, "height <= 0: Window [%s]", window->title);

    // Default values
    window->visible = 1;
    window->can_close = 1;
    window->has_titlebar = 1;
}

static void
window_get_close_rect(Window_t *window, Rect_t *rect)
{
    // FIXME: add right/bottom
    rect->top = (TITLEBAR_HEIGHT - SPRITE_X.height) / 2;
    rect->left = window->width - 1 - /*clip->left -*/ SPRITE_X.width;
}

void
window_click_titlebar(Display_t *display, Window_t *window, int x, int y)
{
    if(window->can_close)
    {
        Rect_t close_rect;
        window_get_close_rect(window, &close_rect);
        //INFO("%d %d | %d %d\n", x, close_rect.left, y, close_rect.top);
        if((x - window->x) >= close_rect.left && (y - window->y) >= close_rect.top)
        {
            NOTIFY("Hiding window %s\n", window->title);
            window->visible = 0;
            return;
        }
    }

    display->selected.window = window;

    display->selected.x = x;
    display->selected.y = y;
    display->selected.orig_x = window->x;
    display->selected.orig_y = window->y;

    display_select_window(display, window);

    NOTIFY("Selected window %s @ (%d, %d)\n", window->title, x, y);
}

static void
window_render_close_button(Display_t *display, Window_t *window, DisplayPixel_t *origin, int stride, Rect_t *clip)
{
    if(window->can_close)
    {
        Rect_t close_rect;
        Rect_t sprite_clip = *clip;
        window_get_close_rect(window, &close_rect);

        // FIXME: wrong (drag title bar all the way to the left to see the sprite wrap around)
        sprite_clip.left = 0;
        display_draw_sprite(display, &SPRITE_X, origin + close_rect.top * stride + close_rect.left, stride, &sprite_clip);
    }
}

void
window_draw_titlebar(Display_t *display, Window_t *window, DisplayPixel_t color, DisplayPixel_t *origin, int stride, Rect_t *clip)
{
    display_fillrect(origin, window->width + 2, TITLEBAR_HEIGHT, color, stride, clip);

    int font_y_offset = (TITLEBAR_HEIGHT - display->font->letter_height) / 2;
    font_printstr(display->font, (origin + 1 + 2 + font_y_offset * stride), stride, window->title, clip);

    window_render_close_button(display, window, origin, stride, clip);
}

void
window_destroy(Window_t *window)
{
    window->above = window->below = NULL;
}

void
window_reinit(Window_t *window)
{
    if (window->reinit)
    {
        window->reinit(window);
    }
}
