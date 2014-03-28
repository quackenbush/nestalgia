#include <SDL/SDL_mutex.h>
#include "audio_buffer.h"

void
audio_buffer_init(AudioBuffer_t *buffer)
{
    buffer->lock = SDL_CreateMutex();
    buffer->cond = SDL_CreateCond();

    buffer->rd_index = 0;
    buffer->wr_index = 0;
};

void
audio_buffer_destroy(AudioBuffer_t *buffer)
{
    SDL_DestroyMutex(buffer->lock);
}

void
audio_buffer_lock(AudioBuffer_t *buffer)
{
    SDL_mutexP(buffer->lock);
}

void
audio_buffer_unlock(AudioBuffer_t *buffer)
{
    SDL_mutexV(buffer->lock);
}

void
audio_buffer_signal(AudioBuffer_t *buffer)
{
    SDL_CondSignal(buffer->cond);
}

void
audio_buffer_wait(AudioBuffer_t *buffer)
{
    SDL_CondWait(buffer->cond, buffer->lock);
}
