#include "cond_lock.h"

void
cond_init(CondLock_t *lock)
{
    lock->cond = SDL_CreateCond();
    lock->mutex = SDL_CreateMutex();
}

void
cond_destroy(CondLock_t *lock)
{
    SDL_DestroyCond(lock->cond);
    lock->cond = NULL;

    SDL_DestroyMutex(lock->mutex);
    lock->mutex = NULL;
}

void
cond_lock(CondLock_t *lock)
{
    SDL_mutexP(lock->mutex);
}

void
cond_unlock(CondLock_t *lock)
{
    SDL_mutexV(lock->mutex);
}

void
cond_signal(CondLock_t *lock)
{
    SDL_CondSignal(lock->cond);
}

void
cond_wait(CondLock_t *lock)
{
    SDL_CondWait(lock->cond, lock->mutex);
}
