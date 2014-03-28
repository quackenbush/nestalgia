#include "nes_mapper.h"
#include <string.h>

#define MAPPER_NUM        1
#define MAPPER_NAME       "MMC1"
#define BANK_SIZE_KB      4
#define VROM_BANK_SIZE_KB 4

#define INFO(...) INFO_MAPPER(MAPPER_NAME __VA_ARGS__)
#define LOG(...)  LOG_MAPPER(MAPPER_NAME __VA_ARGS__)

// --------------------------------------------------------------------------------
typedef struct
{
    unsigned bit_count;
    unsigned data;

    unsigned prg_rom_256kb; // Only for SUROM

    union
    {
        uint8_t word;
        struct
        {
            unsigned mirroring        : 2; // 00 = 1ScA,     01 = 1ScB
                                           // 10 = vertical, 11 = horizontal
            unsigned prgrom_low_bank  : 1; // 0 = high PRG-ROM bank, 1 = low PROG-ROM bank
            unsigned prgrom_16k       : 1; // 0 = 32KB PRG-ROM banks, 1 = 16KB PRG-ROM banks
            unsigned chrrom_4k        : 1; // 0 = single 8KB CHR-ROM bank, 1 = dual 4KB CHR-ROM banks
            unsigned __reserved1      : 3;
        } bits;
    } reg0;

    union
    {
        uint8_t word;
        struct
        {
            unsigned vrom_bank_select : 5; // Source bank to switch
            unsigned __reserved       : 3;
        } bits;
    } reg1;

    union
    {
        uint8_t word;
        struct
        {
            unsigned vrom_bank_select : 5; // Source bank to switch
            unsigned __reserved       : 3;
        } bits;
    } reg2;

    union
    {
        uint8_t word;
        struct
        {
            unsigned rom_bank_select : 4; // Source bank to switch
            unsigned __reserved      : 4;
        } bits;
    } reg3;
} Mapper1Regs_t;

static void
mapper_init(NES_t *nes)
{
    nes_select_prg_rom_bank(nes, 0, 0, BANK_SIZE_KB);
    nes_select_prg_rom_bank(nes, 1, nes->num_prg_rom_banks - 1, BANK_SIZE_KB);
}

static unsigned
mapper1_is_512kb(NES_t *nes)
{
    return (nes->num_prg_rom_banks >= 32);
}

static uint8_t
mapper1_vrom_bank_masked(NES_t *nes, uint8_t data)
{
    uint8_t mask = 0x1f;
    Mapper1Regs_t *mapper1_regs = (Mapper1Regs_t *) &nes->state.mapper_state;

    LOG("masking vrom bank: 0x%02x\n", data);
    if(mapper1_is_512kb(nes))
    {
        // SUROM 256KB select
        mapper1_regs->prg_rom_256kb = (data >> 4) & 1;

        mask = 0x01;//(nes->ppu.num_vrom_banks - 1);
    }

    return data & mask;
}

static void
mapper1_update_prg_rom_bank(NES_t *nes)
{
    Mapper1Regs_t *mapper1_regs = (Mapper1Regs_t *) &nes->state.mapper_state;
    int bank = mapper1_regs->reg3.bits.rom_bank_select;

    if(mapper1_regs->prg_rom_256kb)
    {
        bank |= 0x10;
    }

    if(mapper1_regs->reg0.bits.prgrom_16k)
    {
        unsigned selected_low_bank = mapper1_regs->reg0.bits.prgrom_low_bank;

        LOG("Switching bank %d 0x%02X\n", selected_low_bank, bank);
        nes_select_prg_rom_bank(nes, ! selected_low_bank, bank, BANK_SIZE_KB);
        if(selected_low_bank)
        {
            // The upper bank is always the last bank within the current 256kb block
            unsigned upper_bank = (bank & 0x10) | ((nes->num_prg_rom_banks - 1) & 0xf);
            nes_select_prg_rom_bank(nes, 1, upper_bank, BANK_SIZE_KB);
        }
    }
    else
    {
        nes_select_prg_rom_bank(nes, 0, bank, BANK_SIZE_KB);
        nes_select_prg_rom_bank(nes, 1, bank + 1, BANK_SIZE_KB);
    }
}

static void
mapper_write(NES_t *nes, uint16_t addr, uint8_t data)
{
    Mapper1Regs_t *mapper1_regs = (Mapper1Regs_t *) &nes->state.mapper_state;
    int reg;
    int reset;

    LOG("PRG-ROM[%04Xh] <= 0x%02Xh (PC @ %04Xh)\n", addr, data, nes->cpu.regs.PC - 1);

    addr -= 0x8000;
    reg = addr >> 13;

    reset = data & 0x80;

    if(reset)
    {
        LOG("Reset\n");
        mapper1_regs->data = 0;
        mapper1_regs->reg0.word |= 0x0C;

        //mapper1_regs->reg0.bits.prgrom_low_bank = 1;
        //mapper1_regs->reg0.bits.prgrom_16k = 1;

        mapper1_regs->prg_rom_256kb = 0;

        mapper1_regs->bit_count = 0;
        mapper1_update_prg_rom_bank(nes);
    }
    else
    {
        mapper1_regs->data |= (data & 1) << (mapper1_regs->bit_count);
        LOG("WRITE %02X, Masked: %02X @ %d\n", data, mapper1_regs->data, mapper1_regs->bit_count);
        mapper1_regs->bit_count++;
    }

    if(mapper1_regs->bit_count == 5)
    {
        mapper1_regs->bit_count = 0;

        switch(reg)
        {
            case 0:
                mapper1_regs->reg0.word = mapper1_regs->data;
                LOG("Reg0 Latch: %02X\n", mapper1_regs->reg0.word);

                nes->ppu.state.mirroring = mapper1_regs->reg0.bits.mirroring;
                nes->ppu.dirty = 1;

                LOG("----------------------------------------\n");
                LOG("Mirroring: %s @ F %03d / S %03d %02d\n",
                    PPU_MIRRORING_STR[nes->ppu.state.mirroring],
                    nes->ppu.frame_count,
                    nes->ppu.scanline,
                    PPU_NAME_TABLE(nes->ppu.state.vram.V));
                LOG("PRG-ROM: %04Xh %dKB\n",
                    mapper1_regs->reg0.bits.prgrom_low_bank ? 0x8000 : 0xC000,
                    mapper1_regs->reg0.bits.prgrom_16k ? 16 : 32);
                LOG("CHR-ROM: %dKB\n",
                    mapper1_regs->reg0.bits.chrrom_4k ? 4 : 8);
                LOG("----------------------------------------\n");
                break;

            case 1:
            {
                int vrom_bank;

                mapper1_regs->reg1.word = mapper1_regs->data;
                LOG("Reg1 Latch: %02X\n", mapper1_regs->reg1.word);

                LOG("----------------------------------------\n");
                LOG("VROM Bank $0000 Switch: %d (%d KB)\n",
                    mapper1_regs->reg1.bits.vrom_bank_select,
                    mapper1_regs->reg0.bits.chrrom_4k ? 4 : 8);
                LOG("----------------------------------------\n");

                vrom_bank = mapper1_vrom_bank_masked(nes, mapper1_regs->reg1.bits.vrom_bank_select);

                if(mapper1_regs->reg0.bits.chrrom_4k)
                {
                    nes_ppu_select_vrom_bank(&nes->ppu, 0, vrom_bank, VROM_BANK_SIZE_KB);
                }
                else
                {
                    vrom_bank &= ~1;

                    LOG("8KB copy (second VROM bank): %d/%d\n", mapper1_regs->reg1.bits.vrom_bank_select, vrom_bank);
                    nes_ppu_select_vrom_bank(&nes->ppu, 0, vrom_bank, VROM_BANK_SIZE_KB);
                    nes_ppu_select_vrom_bank(&nes->ppu, 1, vrom_bank + 1, VROM_BANK_SIZE_KB);
                }

                break;
            }

            case 2:
            {
                int vrom_bank;

                mapper1_regs->reg2.word = mapper1_regs->data;
                LOG("Reg2 Latch: %02X\n", mapper1_regs->reg2.word);

                LOG("----------------------------------------\n");
                LOG("VROM Bank $1000 Switch: %d\n",
                    mapper1_regs->reg2.bits.vrom_bank_select);
                LOG("----------------------------------------\n");

                if(mapper1_regs->reg0.bits.chrrom_4k)
                {
                    vrom_bank = mapper1_vrom_bank_masked(nes, mapper1_regs->reg2.bits.vrom_bank_select);
                    nes_ppu_select_vrom_bank(&nes->ppu, 1, vrom_bank, VROM_BANK_SIZE_KB);
                }
                else
                {
                    LOG("8KB VROM switch ignored @ $1000: 0x%02x\n", mapper1_regs->reg2.bits.vrom_bank_select);
                }

                break;
            }

            case 3:
                mapper1_regs->reg3.word = mapper1_regs->data;
                LOG("Reg3 Latch: %02X\n", mapper1_regs->reg3.word);

                LOG("----------------------------------------\n");
                LOG("Switching ROM Bank %d (%dKB) into PRG-ROM $%04Xh (PC: $%04X)\n",
                    mapper1_regs->reg3.bits.rom_bank_select,
                    mapper1_regs->reg0.bits.prgrom_16k ? 16 : 32,
                    mapper1_regs->reg0.bits.prgrom_low_bank ? 0x8000 : 0xC000,
                    nes->cpu.regs.PC);
                LOG("----------------------------------------\n");

                break;
        }

        mapper1_update_prg_rom_bank(nes);

        mapper1_regs->data = 0;
    }
}

static void
mapper_restore(NES_t *nes)
{
    mapper1_update_prg_rom_bank(nes);
}

// --------------------------------------------------------------------------------

NESMapper_t nes_mapper1 = {MAPPER_NUM, MAPPER_NAME, mapper_init, mapper_write, mapper_restore};
