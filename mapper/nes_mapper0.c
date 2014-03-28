#include "nes_mapper.h"

// Memory Mapper 0 (aka MMC0/NROM)

#define MAPPER_NUM   0
#define MAPPER_NAME  "MMC0"
#define BANK_SIZE_KB 4

#define INFO(...) INFO_MAPPER(MAPPER_NAME __VA_ARGS__)
#define LOG(...)  LOG_MAPPER(MAPPER_NAME __VA_ARGS__)

// --------------------------------------------------------------------------------
static void
mapper_init(NES_t *nes)
{
    nes_select_prg_rom_bank(nes, 0, 0, BANK_SIZE_KB);
    nes_select_prg_rom_bank(nes, 1, nes->num_prg_rom_banks - 1, BANK_SIZE_KB);
}

static void
mapper_write(NES_t *nes, uint16_t addr, uint8_t data)
{
#if 0 // Mapper0 data is read-only
    int bank_16kb;
    LOG("PRG-ROM[%04Xh] <= 0x%02Xh\n", addr, data);
    addr -= 0x8000;
    bank_16kb = addr >> 14;
    nes->memory_map.prg_rom[bank_16kb][addr & 0x3fff] = data;
#endif
}

// --------------------------------------------------------------------------------

NESMapper_t nes_mapper0 = {MAPPER_NUM, MAPPER_NAME, mapper_init, mapper_write};
