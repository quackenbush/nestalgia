#ifndef __input_h__
#define __input_h__

#include "nes.h"

void input_update(NES_t *nes);
void input_latch_joypads(NES_t *nes);

unsigned input_time_ms(void);
void input_delay(unsigned delay_ms);

#endif
