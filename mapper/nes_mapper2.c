#include "nes_mapper.h"

#define MAPPER_NUM   2
#define MAPPER_NAME  "UxROM"
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
    int rom_bank = data & (nes->num_prg_rom_banks - 1);

    LOG("PRG-ROM[%04Xh] <= 0x%02Xh (PC @ %04Xh)\n", addr, data, nes->cpu.regs.PC - 1);

    LOG("Selecting ROM bank %d\n", rom_bank);

    nes_select_prg_rom_bank(nes, 0, rom_bank, BANK_SIZE_KB);
}

// --------------------------------------------------------------------------------

NESMapper_t nes_mapper2 = {MAPPER_NUM, MAPPER_NAME, mapper_init, mapper_write};
NESMapper_t nes_mapper71 = {71, "Camerica", mapper_init, mapper_write};
