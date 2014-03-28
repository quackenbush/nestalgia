#ifndef __platform_cond_lock_h__
#define __platform_cond_lock_h__

#include <SDL/SDL_thread.h>

typedef struct
{
    int init;
    SDL_mutex *mutex;
    SDL_cond *cond;
    void *data;
} CondLock_t;

#endif
