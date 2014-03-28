// SDL display implementation

#include <SDL/SDL.h>
#include "display.h"
#include "log.h"

#include "sprites.h"
#include "font-small.h"

#define INFO(...) _INFO(DISPLAY, __VA_ARGS__)

#define CLAMP(A) ((int) (A) <= 0 ? 0 : (A))

#define RGB16(r,g,b) (((r >> 3) << 11) | ((g >> 2) << 5) | ((b >> 3) << 0))
#define RGB32(r,g,b) (((r) << 16) | ((g) << 8) | ((b) << 0))
#define BGR32(r,g,b) (((r) << 8) | ((g) << 16) | ((b) << 24))

#ifdef BPP32
#ifdef osx
#define SDL_MAPPIXEL(r, g, b)    BGR32(r,g,b)
#define SDL_MAPPIXELFS(r, g, b)  RGB32(r,g,b)

#else
#define SDL_MAPPIXEL(r, g, b)    RGB32(r,g,b)
#define SDL_MAPPIXELFS(r, g, b)  RGB32(r,g,b)
#endif

#else
#define SDL_MAPPIXEL(r, g, b)    RGB16(r,g,b)
#define SDL_MAPPIXELFS           SDL_MAPPIXEL
#endif

#define SDL_VIDEO_FLAGS  (SDL_SWSURFACE | SDL_DOUBLEBUF)

#define PIXEL_POINTER(SURFACE, X, Y)                    \
    (((DisplayPixel_t*) (SURFACE)->pixels) +            \
     (SURFACE->offset / sizeof(DisplayPixel_t)) +       \
     ((Y) * SURFACE->pitch / sizeof(DisplayPixel_t)) +  \
     (X))

uint32_t
display_maprgb(Display_t *display, uint8_t r, uint8_t g, uint8_t b)
{
#if 1
    SDL_Surface *surface = (SDL_Surface *) display->p;
    return SDL_MapRGB(surface->format, r, g, b);
#else
    if (display->fullscreen)
        return SDL_MAPPIXELFS(r, g, b);
    else
        return SDL_MAPPIXEL(r, g, b);
#endif
}

void
display_init(Display_t *display, MenuItem_t *menu)
{
    ASSERT(display->depth_in_bytes > 0, "depth_in_bytes is 0\n");

    display->font = &FONT_SMALL;
    display->top_window = display->bottom_window = NULL;

    font_init(display->font);

    display->menubar.visible = 1;
    menubar_init(&display->menubar, menu);

    if(SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        ASSERT(0, "Could not initialize SDL!\n");
    }

    display_fullscreen(display, display->fullscreen);

    SDL_WM_SetCaption(display->title, display->title);

    display_set_menubar(display, 0);

    if(! display->show_cursor)
    {
        // Hide the mouse cursor
        SDL_ShowCursor(SDL_DISABLE);
    }
}

void
display_set_menubar(Display_t *display, int visible)
{
    // Toggle the menubar display
    display->menubar.visible = visible;
    menubar_deselect(&display->menubar);
    SDL_ShowCursor(visible ? SDL_ENABLE : SDL_DISABLE);
}

void
display_destroy(Display_t *display)
{
    unsigned i = 0;

    while(i < display->num_windows)
    {
        window_destroy(display->windows[i--]);
    }

    display->top_window = display->bottom_window = NULL;
    display->num_windows = 0;

    SDL_ShowCursor(SDL_ENABLE);

    if(display->p)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        display->p = NULL;
    }

    SDL_Quit();
}

static void
display_top_zorder(Display_t *display, Window_t *window)
{
    // Move the window to the top of the Z-order
    if(display->top_window != window)
    {
        if(display->bottom_window == window)
            display->bottom_window = window->above;

        if(window->below)
            window->below->above = window->above;

        if(window->above)
            window->above->below = window->below;

        window->below = display->top_window;
        if(display->top_window)
        {
            display->top_window->selected = 0;
            display->top_window->above = window;
        }
        display->top_window = window;
        window->selected = 1;
        window->above = NULL;
    }
}

void
display_bottom_zorder(Display_t *display, Window_t *window)
{
    // FIXME: this should really push the window all the way to the bottom
    display_top_zorder(display, window->below);
}

void
display_select_window(Display_t *display, Window_t *window)
{
    display_top_zorder(display, window);
    window->visible = 1;
}

void
display_hide_window(Display_t *display, Window_t *window)
{
    display_bottom_zorder(display, window);
    window->visible = 0;
}

void
display_mousedown(Display_t *display, int x, int y)
{
    Window_t *window;

    if(display->menubar.visible)
    {
        if(menubar_mousedown(display, &display->menubar, x, y))
        {
            return;
        }
    }
    else
    {
        // User clicked; turn the menubar on
        display_set_menubar(display, 1);
    }

    display->selected.window = NULL;

    // Note this searches in Z-order
    window = display->top_window;
    while(window)
    {
        if(x >= window->x && x < (window->x + window->width) &&
           y >= window->y)
        {
            int window_top = window->y;

            if(window->has_titlebar)
            {
                window_top += TITLEBAR_HEIGHT;
                if(y < window_top)
                {
                    window_click_titlebar(display, window, x, y);
                    return;
                }
            }

            if(y < (window_top + window->height))
            {
                // Clicked inside the window
                if(display->top_window != window)
                {
                    display_top_zorder(display, window);
                }

                // Send the mouseclick downstream
                if(window->on_mousedown)
                {
                    window->on_mousedown(window, x, y - window_top);
                }
                return;
            }
        }

        window = window->below;
    }
}

void
display_mousemove(Display_t *display, int x, int y)
{
    if(display->selected.window)
    {
        display->selected.window->x = display->selected.orig_x + ((int) x - display->selected.x);
        display->selected.window->y = display->selected.orig_y + ((int) y - display->selected.y);
    }
    else
    {
        Window_t *window;

        if(display->menubar.visible)
        {
            if(menubar_mousemove(display, &display->menubar, x, y))
                return;
        }

        // FIXME: this while() loop code should be shared with mousedown

        // Note this searches in Z-order
        window = display->top_window;

        while(window)
        {
            if(x >= window->x && x < (window->x + window->width) &&
               y >= window->y)
            {
                int window_top = window->y;

                if(window->has_titlebar)
                {
                    window_top += TITLEBAR_HEIGHT;
                }

                if(y >= window_top && y < (window_top + window->height))
                {
                    if(window->on_mousedown)
                    {
                        window->on_mousemove(window, x, y - window_top);
                    }
                    return;
                }
            }

            window = window->below;
        }
    }
}

void
display_mouseup(Display_t *display, int x, int y)
{
    if(display->selected.window)
    {
        NOTIFY("Window '%s' deselected @ (%d,%d) [%d, %d]\n",
             display->selected.window->title,
             display->selected.window->x,
             display->selected.window->y,
             x, y);
        display->selected.window = NULL;
    }
}

void
display_add_window(Display_t *display, Window_t *window)
{
    window->font = display->font;

    window->above = NULL;
    window->below = NULL;

    if(display->top_window)
    {
        display->top_window->above = window;
        window->below = display->top_window;
        display->top_window = window;
    }
    else
    {
        display->bottom_window = window;
        display->top_window = window;
    }

    ASSERT(display->num_windows < MAX_WINDOWS, "Too many windows: %d\n", display->num_windows);
    ASSERT(window->width > 0, "Window width is 0\n");
    ASSERT(window->height > 0, "Window height is 0\n");
    ASSERT(window->title, "Window title is NULL\n");
    ASSERT(window->draw, "Window draw() function is NULL\n");
    INFO("window[%s @ %d] <= %p\n", window->title, display->num_windows, window);

    display->windows[display->num_windows++] = window;
}

void
display_draw_sprite(Display_t *display, const Sprite_t *sprite, DisplayPixel_t *origin, int stride, Rect_t *clip)
{
    DisplayPixel_t *row = origin;
    const uint8_t *sprite_pixels = sprite->pixels + (clip->top * sprite->width);
    int x, y;

    // FIXME: this should be constant
    DisplayPixel_t SPRITE_PALETTE[2];
    SPRITE_PALETTE[0] = display_maprgb(display, 0, 0, 0);
    SPRITE_PALETTE[1] = display_maprgb(display, 0xff, 0xff, 0xff);

    for(y = clip->top; y < sprite->height - clip->bottom; y++)
    {
        DisplayPixel_t *output = row;
        const uint8_t *input = sprite_pixels;
        sprite_pixels += sprite->width;
        row += stride;

        for(x = clip->left; x < sprite->width - clip->right; x++)
        {
            if(*input)
            {
                *output = SPRITE_PALETTE[*input];
            }
            input++;
            output++;
        }
    }
}

void
display_line(DisplayPixel_t *origin,
             int x1, int y1,
             int x2, int y2,
             DisplayPixel_t color, int stride, Rect_t *clip)
{
    DisplayPixel_t *pixel = origin + x1 + y1 * stride;
    int x = x1;
    int y = y1;
    int dx;
    int dy;
    int dp;

    dx = (x1 == x2) ? 0 : 1;
    dy = (y1 == y2) ? 0 : 1;
    dp = dx + dy * stride;

    ASSERT(dx == 0 || dy == 0, "Cannot draw diagonal lines: (%d,%d) => (%d,%d)", x1, y1, x2, y2);

    while(x != x2 || y != y2)
    {
        *pixel = color;
        x += dx;
        y += dy;
        pixel += dp;
    }
}

void
display_box(DisplayPixel_t *origin,
            int x1, int y1,
            int x2, int y2,
            DisplayPixel_t color, int stride, Rect_t *clip)
{
    // Top
    display_line(origin, x1, y1, x2, y1, color, stride, clip);
    // Left
    display_line(origin, x1, y1, x1, y2, color, stride, clip);
    // Right
    display_line(origin, x2, y1, x2, y2, color, stride, clip);
    // Bottom
    display_line(origin, x1, y2, x2, y2, color, stride, clip);
}

void
display_fillrect(DisplayPixel_t *origin, int width, int height, DisplayPixel_t color, int stride, Rect_t *clip)
{
    DisplayPixel_t *row = origin;
    int x, y;

    for(y = clip->top; y < height - clip->bottom; y++)
    {
        DisplayPixel_t *pixels = row;
        row += stride;
        for(x = clip->left; x < width - clip->right; x++)
        {
            *pixels++ = color;
        }
    }
}

void
display_reset(Display_t *display)
{
    // FIXME: attempting to clear the screen when roms are reset.  ROMs like Zelda do not work...
#if 0
    SDL_Surface *screen = (SDL_Surface *) display->p;
    const unsigned stride = display->width;

    // Clear the background
    const unsigned screen_size = stride * display->height * display->depth_in_bytes;

    SDL_LockSurface(screen);
    memset(PIXEL_POINTER(screen, 0, 0), 0, screen_size);
    SDL_Flip(screen);
    SDL_UnlockSurface(screen);
#endif
}

static void
display_draw_window(Display_t *display, Window_t *window)
{
    int COLOR_TITLEBAR          = display_maprgb(display, 0x55, 0x55, 0x55);
    int COLOR_TITLEBAR_SELECTED = display_maprgb(display, 0x00, 0x00, 0x99);
    int stride = display->stride;

    SDL_Surface *screen = (SDL_Surface *) display->p;
    int color = (window->selected) ? COLOR_TITLEBAR_SELECTED : COLOR_TITLEBAR;

    DisplayPixel_t *window_origin;
    Rect_t clip;
    int top = window->y + TITLEBAR_HEIGHT;
    int window_bottom = CLAMP(top + window->height - display->height);

    ASSERT(stride >= display->width, "Bad stride: %d %d\n", stride, display->width);

    clip.left = (window->x < 0) ? -window->x : 0;
    clip.top  = (window->y < 0) ? -window->y : 0;

    clip.right = CLAMP(window->x + window->width - display->width);
    clip.bottom = CLAMP(top - display->height);

    window_origin = PIXEL_POINTER(screen, CLAMP(window->x), CLAMP(window->y));
    if(window->has_titlebar)
    {
        window_draw_titlebar(display, window, color, window_origin, stride, &clip);
        window_origin += (TITLEBAR_HEIGHT * stride);
        clip.bottom = window_bottom;
        display_fillrect(window_origin, window->width, window->height,
                         display_maprgb(display, 0x00, 0x00, 0x00), stride, &clip);
        display_box(window_origin, 0, 0, window->width + 1, window->height + 1,
                    color, stride, &clip);
        window_origin += 1 + stride;
    }
    else
    {
        clip.bottom = window_bottom;
    }

    window->draw(display, window, window_origin, stride, &clip);
}

static void
display_draw_windows(Display_t *display, int always_on_top)
{
    Window_t *window = display->bottom_window;
    while(window)
    {
        if(window->visible && (window->always_on_top == always_on_top))
        {
            display_draw_window(display, window);
        }

        window = window->above;
    }
}

void
display_draw(Display_t *display)
{
    SDL_Surface *screen = (SDL_Surface *) display->p;

    //SDL_LockSurface(screen);

    //ASSERT(display->stride >= display->width, "WTF: %d %d\n", display->stride, display->width);

    if(display->windowed)
    {
        // Only clear the screen if we are in windowed mode
        // (NES background in fullscreen should always blit over this)
        const unsigned screen_bytes = display->stride * display->height * display->depth_in_bytes;

        memset(PIXEL_POINTER(screen, 0, 0), 0, screen_bytes);
    }

    // Draw the normal windows, from bottom-to-top
    display_draw_windows(display, 0);
    // Draw the always-on-top windows, from bottom-to-top
    display_draw_windows(display, 1);

    if(display->menubar.visible)
    {
        menubar_draw(display, &display->menubar, PIXEL_POINTER(screen, 0, 0));
    }

    SDL_Flip(screen);
    //SDL_UnlockSurface(screen);
}

void
display_fullscreen(Display_t *display, int fullscreen)
{
    SDL_Surface *screen;
    int sdl_flags = SDL_VIDEO_FLAGS;
    unsigned i;

    display->fullscreen = fullscreen;

    if(fullscreen)
        sdl_flags |= SDL_FULLSCREEN;

    if (!(screen = SDL_SetVideoMode(display->width, display->height, BPP * 8, sdl_flags)))
    {
        ASSERT(0, "Could not initialize screen!\n");
    }

    display->stride = screen->pitch / sizeof(DisplayPixel_t);

#if 0
    const SDL_VideoInfo *info = SDL_GetVideoInfo();
    printf("display params: \n");

#define PARAM(X,...) {printf("%16s : ", X); printf(__VA_ARGS__); printf("\n");}
    PARAM("BPP", "%d", BPP);
    PARAM("dimensions", "%d x %d", display->width, display->height);
    PARAM("fullscreen", "%d", fullscreen);
    PARAM("hw_available", "%d", info->hw_available);
    PARAM("wm_available", "%d", info->wm_available);
    PARAM("bits/bytes-pp", "%d/%d", info->vfmt->BitsPerPixel, info->vfmt->BytesPerPixel);
    PARAM("pitch", "%d", screen->pitch);
    PARAM("w", "%d", screen->w);
    PARAM("stride", "%d", display->stride);
    PARAM("offset", "%d", screen->offset);
    PARAM("sizeof(pixel)", "%zd", sizeof(DisplayPixel_t));
#endif

    NOTIFY("Display: %d x %d @ %d bpp [stride: %d, pitch: %d, clip: (%d,%d) (%d,%d)]\n",
           display->width, display->height, BPP * 8,
           display->stride, screen->pitch,
           screen->clip_rect.x, screen->clip_rect.y,
           screen->clip_rect.w, screen->clip_rect.h);

    INFO("Fullscreen: %d\n", (screen->flags & SDL_FULLSCREEN) != 0);
    INFO("Double buffer: %d\n", (screen->flags & SDL_DOUBLEBUF) != 0);
    INFO("HW: %d\n", (screen->flags & SDL_HWSURFACE) != 0);
    INFO("SW: %d\n", (screen->flags & SDL_SWSURFACE) != 0);

    display->p = screen;

    for(i = 0; i < display->num_windows; i++)
    {
        window_reinit(display->windows[i]);
    }
}
