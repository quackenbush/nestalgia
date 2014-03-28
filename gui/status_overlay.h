#ifndef __status_overlay_h__
#define __status_overlay_h__

#include "window.h"
#include "display.h"

typedef struct
{
    Window_t window;

    char msg[64];
    int msg_frames;
} StatusOverlay_t;

void status_overlay_init(StatusOverlay_t *s, Display_t *display);
void status_overlay_destroy(StatusOverlay_t *s);

void status_overlay_update(StatusOverlay_t *s, const char *msg);

#endif
