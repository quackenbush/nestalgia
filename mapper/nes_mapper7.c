#include "nes_mapper.h"

#define MAPPER_NUM   7
#define MAPPER_NAME  "AxROM"
#define BANK_SIZE_KB 4

#define INFO(...) INFO_MAPPER(MAPPER_NAME __VA_ARGS__)
#define LOG(...)  LOG_MAPPER(MAPPER_NAME __VA_ARGS__)

// --------------------------------------------------------------------------------
// AxROM: Games by RARE, e.g. Battletoads and Marble Madness
static const PPUMirroring_t MIRROR_TABLE[2] = {Mirror1ScreenA, Mirror1ScreenB};
#define ROM_MASK 0x7

static void
mapper_init(NES_t *nes)
{
    nes->ppu.state.mirroring = Mirror1ScreenA;

    nes_select_prg_rom_bank(nes, 0, 0, BANK_SIZE_KB);
    nes_select_prg_rom_bank(nes, 1, 1, BANK_SIZE_KB);
}

static void
mapper_write(NES_t *nes, uint16_t addr, uint8_t data)
{
    int rom_bank = (data & ROM_MASK) << 1;
    int rom_mirroring = (data >> 4) & 1;
    PPUMirroring_t mirroring = MIRROR_TABLE[rom_mirroring];

    LOG("PRG-ROM[%04Xh] <= 0x%02Xh (PC @ %04Xh)\n", addr, data, nes->cpu.regs.PC - 1);

    nes_select_prg_rom_bank(nes, 0, rom_bank, BANK_SIZE_KB);
    nes_select_prg_rom_bank(nes, 1, rom_bank + 1, BANK_SIZE_KB);

    if(mirroring != nes->ppu.state.mirroring)
    {
        nes->ppu.state.mirroring = mirroring;
        nes->ppu.dirty = 1;
    }

    INFO("Selected 32KB ROM bank %d, mirroring %s\n",
         rom_bank,
         PPU_MIRRORING_STR[mirroring]);
}

// --------------------------------------------------------------------------------

NESMapper_t nes_mapper7 = {MAPPER_NUM, MAPPER_NAME, mapper_init, mapper_write};
