#include "nes_mapper.h"

#define MAPPER_NUM        87
#define MAPPER_NAME       "Mapper87"
#define BANK_SIZE_KB      4
#define VROM_BANK_SIZE_KB 4

#define INFO(...) INFO_MAPPER(MAPPER_NAME __VA_ARGS__)
#define LOG(...)  LOG_MAPPER(MAPPER_NAME __VA_ARGS__)

// --------------------------------------------------------------------------------
/*
  $6000-7FFF:  [.... ..AB]
    B = High CHR Bit
    A = Low CHR Bit

This reg selects 8k CHR @ $0000.  Note the reversed bit orders.  Most games using this mapper
only have 16k CHR, so the 'B' bit is usually unused.
*/
#define CHR_MASK 0x3

static void
mapper_init(NES_t *nes)
{
    nes_select_prg_rom_bank(nes, 0, 0, BANK_SIZE_KB);
    nes_select_prg_rom_bank(nes, 1, nes->num_prg_rom_banks - 1, BANK_SIZE_KB);

    NOTIFY("WARNING: mapper87 is not tested, so surely the vrom switching is wrong\n");
    nes_ppu_select_vrom_bank(&nes->ppu, 0, 2, VROM_BANK_SIZE_KB);
    nes_ppu_select_vrom_bank(&nes->ppu, 1, 3, VROM_BANK_SIZE_KB);
}

static void
mapper_write(NES_t *nes, uint16_t addr, uint8_t data)
{
    printf("write: %x %x\n", addr, data);
    if(addr >= 0x6000 && addr < 0x8000)
    {
        int chr_bank = (data & CHR_MASK) << 1;

        NOTIFY("Selecting CHR bank %d (%d), scanline %03d\n",
               data,
               chr_bank,
               nes->ppu.scanline);

        nes_ppu_select_vrom_bank(&nes->ppu, 0, chr_bank, VROM_BANK_SIZE_KB);
        nes_ppu_select_vrom_bank(&nes->ppu, 1, (chr_bank + 1) & 1, VROM_BANK_SIZE_KB);
    }
}

// --------------------------------------------------------------------------------

NESMapper_t nes_mapper87 = {MAPPER_NUM, MAPPER_NAME, mapper_init, mapper_write};
