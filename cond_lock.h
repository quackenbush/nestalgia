#ifndef __cond_lock_h__
#define __cond_lock_h__

#include "platform_cond_lock.h"

void cond_init(CondLock_t *lock);
void cond_destroy(CondLock_t *lock);

void cond_lock(CondLock_t *lock);
void cond_unlock(CondLock_t *lock);

void cond_signal(CondLock_t *lock);
void cond_wait(CondLock_t *lock);

#endif
