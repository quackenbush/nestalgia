#include "status_overlay.h"
#include <string.h>
#include "log.h"

#define STATUS_OVERLAY_MSG_FRAMES 120

static void
status_overlay_draw(Display_t *display, Window_t *window, DisplayPixel_t *origin, int stride, Rect_t *clip)
{
    StatusOverlay_t *s = (StatusOverlay_t *) window;

    if(s->msg_frames > 0 && s->msg[0])
    {
        s->msg_frames--;
        font_printstr(window->font, origin, stride, s->msg, clip);
    }
    else
    {
        window->visible = 0;
    }
}

#define SHIM 12

void
status_overlay_init(StatusOverlay_t *s, Display_t *display)
{
    s->msg[0] = 0;
    s->window.width = 200;
    s->window.height = 20;
    s->window.always_on_top = 1;
    s->window.title = "Status";
    s->window.draw = status_overlay_draw;
    display_add_window(display, &s->window);

    s->window.has_titlebar = 0;
    s->window.x = SHIM;
    s->window.y = display->height - s->window.height - SHIM;

    status_overlay_update(s, "TEST OVERLAY");
}

void
status_overlay_destroy(StatusOverlay_t *s)
{
}

void
status_overlay_update(StatusOverlay_t *s, const char *msg)
{
    strncpy(s->msg, msg, sizeof(s->msg));
    s->msg[sizeof(s->msg) - 1] = 0;

    s->msg_frames = STATUS_OVERLAY_MSG_FRAMES;
    s->window.visible = 1;
}
