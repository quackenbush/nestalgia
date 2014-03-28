#ifndef __audio_h__
#define __audio_h__

#include "audio_buffer.h"

typedef struct
{
    void (*audio_open)(AudioBuffer_t *buffer);
    void (*audio_pause)(int paused);
    void (*audio_close)(void);
} AudioDescriptor_t;

#endif
