#ifndef __audio_buffer_h__
#define __audio_buffer_h__

#include <stdint.h>

#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_BUFFER_SIZE (AUDIO_SAMPLE_RATE / 4)
#define AUDIO_16BIT

#ifdef AUDIO_16BIT
#define AUDIO_FORMAT      AUDIO_S16SYS
#define AUDIO_RANGE       32767
#define AUDIO_TYPE        uint16_t

#else
#define AUDIO_FORMAT      AUDIO_S8
#define AUDIO_RANGE       127
#define AUDIO_TYPE        uint8_t
#endif

typedef struct
{
    float buffer[AUDIO_BUFFER_SIZE];

    int rd_index;
    int wr_index;

    void *lock;
    void *cond;
} AudioBuffer_t;

void audio_buffer_init(AudioBuffer_t *buffer);
void audio_buffer_destroy(AudioBuffer_t *buffer);

void audio_buffer_lock(AudioBuffer_t *buffer);
void audio_buffer_unlock(AudioBuffer_t *buffer);

void audio_buffer_signal(AudioBuffer_t *buffer);
void audio_buffer_wait(AudioBuffer_t *buffer);

#endif
