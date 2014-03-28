#include "nes_mapper.h"

#define MAPPER_NUM        3
#define MAPPER_NAME       "CNROM"
#define BANK_SIZE_KB      4
#define VROM_BANK_SIZE_KB 4

#define INFO(...) INFO_MAPPER(MAPPER_NAME __VA_ARGS__)
#define LOG(...)  LOG_MAPPER(MAPPER_NAME __VA_ARGS__)

// --------------------------------------------------------------------------------
#define CHR_MASK 0xff

static void
mapper_init(NES_t *nes)
{
    nes_select_prg_rom_bank(nes, 0, 0, BANK_SIZE_KB);
    nes_select_prg_rom_bank(nes, 1, nes->num_prg_rom_banks - 1, BANK_SIZE_KB);
}

static void
mapper_write(NES_t *nes, uint16_t addr, uint8_t data)
{
    int chr_bank = (data & CHR_MASK) << 1;
    int mask = (nes->ppu.num_vrom_banks * 2 - 1);

    LOG("Selecting CHR bank %d (%d), scanline %03d\n",
        data,
        chr_bank,
        nes->ppu.scanline);

    nes_ppu_select_vrom_bank(&nes->ppu, 0, chr_bank & mask, VROM_BANK_SIZE_KB);
    nes_ppu_select_vrom_bank(&nes->ppu, 1, (chr_bank + 1) & mask, VROM_BANK_SIZE_KB);
}

// --------------------------------------------------------------------------------

NESMapper_t nes_mapper3 = {MAPPER_NUM, MAPPER_NAME, mapper_init, mapper_write};
