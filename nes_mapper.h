#ifndef __nes_mapper_h__
#define __nes_mapper_h__

#include "nes.h"

#define INFO_MAPPER(...) _INFO(MAPPER, __VA_ARGS__)
#define LOG_MAPPER(...)  _LOG(MAPPER, __VA_ARGS__)

void nes_mapper_init(NES_t *nes);
void nes_select_prg_rom_bank(NES_t *nes, unsigned dest_bank, unsigned src_bank, int size_kb);
void nes_mapper_restore(NES_t *nes);
int nes_mapper_scanline(NES_t *nes);

typedef struct
{
    unsigned int num;
    const char *name;
    void (*init_func)    (NES_t *nes);
    void (*write_func)   (NES_t *nes, uint16_t addr, uint8_t data);
    void (*restore_func) (NES_t *nes);
    int  (*scanline_func)(NES_t *nes);
} NESMapper_t;

#endif
