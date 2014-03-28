#ifndef __sprites_h__
#define __sprites_h__

#include <stdint.h>

typedef struct
{
    const uint8_t *pixels;
    int width;
    int height;
} Sprite_t;

Sprite_t SPRITE_X;

#endif
