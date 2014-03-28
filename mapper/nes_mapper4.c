#include "nes_mapper.h"
#include <string.h>

#define MAPPER_NUM   4
#define MAPPER_NAME  "MMC3"
#define BANK_SIZE_KB 2

#define INFO(...) INFO_MAPPER(MAPPER_NAME __VA_ARGS__)
#define LOG(...)  LOG_MAPPER(MAPPER_NAME __VA_ARGS__)

// --------------------------------------------------------------------------------
typedef struct
{
    int prg_rom_first_bank;
    int prg_rom_second_bank;

    int reload_count;
    int scanline_count;
    int irq_enable;
    int irq_pending;

    uint8_t vrom_sel[6];

    union
    {
        uint8_t word;
        struct
        {
            unsigned command          : 3; // 0 = Select 2 1K VROM pages at PPU $0000
                                           // 1 = Select 2 1K VROM pages at PPU $0800
                                           // 2 = Select 1K VROM page at PPU $1000
                                           // 3 = Select 1K VROM page at PPU $1400
                                           // 4 = Select 1K VROM page at PPU $1800
                                           // 5 = Select 1K VROM page at PPU $1C00
                                           // 6 = Select first switchable ROM page
                                           // 7 = Select second switchable ROM page
            unsigned __reserved1      : 3;
            unsigned prg_address_sel  : 1; // 0 = $8000 / $A000, 1 = $A000 / $C000
            unsigned chr_address_sel  : 1; // 0 = normal command address, 1 = XOR command address with $1000
        } bits;
    } reg8000;

    union
    {
        uint8_t word;
        struct
        {
            unsigned page_number : 8; // Page number for command (reg8000 2:0)
        } bits;
    } reg8001;

    union
    {
        uint8_t word;
        struct
        {
            unsigned mirror_horz : 1; // 0 = Vertical mirroring, 1 = Horizontal mirroring
            unsigned __reserved  : 7;
        } bits;
    } regA000;

    union
    {
        uint8_t word;
        struct
        {
            unsigned __reserved     : 6;
            unsigned write_protect  : 1; // 0 = Not writable, 1 = Writable
            unsigned saveram_enable : 1; // 0 = Disable $6000-$7FFF, 1 = Enable $6000-$7FFF
        } bits;
    } regA001;

} Mapper4Regs_t;

/*
Notes: - Two of the 8K ROM banks in the PRG area are switchable.
         The other two are "hard-wired" to the last two banks in
         the cart. The default setting is switchable banks at
         $8000 and $A000, with banks 0 and 1 being swapped in
         at reset. Through bit 6 of $8000, the hard-wiring can
         be made to affect $8000 and $E000 instead of $C000 and
         $E000. The switchable banks, whatever their addresses,
         can be swapped through commands 6 and 7.
       - A cart will first write the command and base select number
         to $8000, then the value to be used to $8001.
       - On carts with VROM, the first 8K of VROM is swapped into
         PPU $0000 on reset. On carts without VROM, as always, there
         is 8K of VRAM at PPU $0000.
*/

static void
mapper4_update_prg_rom_banks(NES_t *nes)
{
    Mapper4Regs_t *regs = (Mapper4Regs_t *) &nes->state.mapper_state;
    int src[4];
    int i;

    if(regs->reg8000.bits.prg_address_sel)
    {
        // Mode 1
        src[0] = nes->num_prg_rom_banks * 2 - 2;
        src[1] = regs->prg_rom_second_bank;
        src[2] = regs->prg_rom_first_bank;
        src[3] = nes->num_prg_rom_banks * 2 - 1;
    }
    else
    {
        // Mode 0
        src[0] = regs->prg_rom_first_bank;
        src[1] = regs->prg_rom_second_bank;
        src[2] = nes->num_prg_rom_banks * 2 - 2;
        src[3] = nes->num_prg_rom_banks * 2 - 1;
    }

    for(i = 0; i < 4; i++)
    {
        nes_select_prg_rom_bank(nes, i, src[i], BANK_SIZE_KB);
    }
}

static void
mapper4_update_vrom(NES_t *nes)
{
    static const uint8_t BANKS[] = {0, 0, 1, 1,
                                    2, 3, 4, 5};
    uint8_t bank_map[sizeof(BANKS) / sizeof(BANKS[0])];

    Mapper4Regs_t *regs = (Mapper4Regs_t *) &nes->state.mapper_state;
    uint8_t swap_banks = regs->reg8000.bits.chr_address_sel;
    uint8_t dest_bank;

    for(dest_bank = 0; dest_bank < sizeof(BANKS) / sizeof(BANKS[0]); dest_bank++)
    {
        uint8_t index;

        index = (swap_banks) ? ((dest_bank + 4) & 7) : dest_bank;

        bank_map[dest_bank] = regs->vrom_sel[BANKS[index]];
    }

#if 0
    printf("Banks[%s]: ", swap_banks ? "*" : " ");
    for(dest_bank = 0; dest_bank < sizeof(BANKS) / sizeof(BANKS[0]); dest_bank++)
    {
        printf("%2x ", bank_map[dest_bank]);
    }

    printf("\n");
#endif

    for(dest_bank = 0; dest_bank < sizeof(BANKS) / sizeof(BANKS[0]); dest_bank++)
    {
        int b = bank_map[dest_bank];

        // FIXME: ghettofied logic here...
        if(swap_banks)
        {
            if(dest_bank == 5 || dest_bank == 7)
                b = bank_map[dest_bank - 1] + 1;
        }
        else
        {
            if(dest_bank == 1 || dest_bank == 3)
                b = bank_map[dest_bank - 1] + 1;
        }

        nes_ppu_select_vrom_bank(&nes->ppu, dest_bank, b, 1);
    }
}

static void
mapper4_select_vrom(NES_t *nes, int dest_bank, unsigned src_page)
{
    Mapper4Regs_t *regs = (Mapper4Regs_t *) &nes->state.mapper_state;

    if(src_page >= nes->ppu.num_vrom_banks * 8)
    {
        printf("fix vrom bank: %d\n", src_page);
        src_page %= (nes->ppu.num_vrom_banks * 8);
    }

    regs->vrom_sel[dest_bank] = src_page;

    mapper4_update_vrom(nes);
}

static void
mapper_init(NES_t *nes)
{
    Mapper4Regs_t *regs = (Mapper4Regs_t *) &nes->state.mapper_state;
    NOTIFY("Num PRG-ROM banks: %d\n", nes->num_prg_rom_banks);
    regs->prg_rom_first_bank = 0;
    regs->prg_rom_second_bank = 1;
    regs->scanline_count = 0;
    regs->irq_pending = 0;
    mapper4_update_prg_rom_banks(nes);
}

static void
mapper4_do_command(NES_t *nes)
{
    Mapper4Regs_t *regs = (Mapper4Regs_t *) &nes->state.mapper_state;
    int command = regs->reg8000.bits.command;
    int page = regs->reg8001.bits.page_number;
    LOG("Command %d @ page %d\n", command, page);
    switch(command)
    {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
            mapper4_select_vrom(nes, command, page);
            break;

        case 6: // Select first switchable ROM page
            regs->prg_rom_first_bank = page;
            mapper4_update_prg_rom_banks(nes);
            break;

        case 7: // Select second switchable ROM page
            regs->prg_rom_second_bank = page;
            mapper4_update_prg_rom_banks(nes);
            break;
    }
}

static void
mapper_write(NES_t *nes, uint16_t addr, uint8_t data)
{
    Mapper4Regs_t *regs = (Mapper4Regs_t *) &nes->state.mapper_state;

    LOG("PRG-ROM[%04Xh] <= 0x%02Xh (PC @ %04Xh)\n", addr, data, nes->cpu.regs.PC - 1);

    switch(addr)
    {
        case 0x8000:
            regs->reg8000.word = data;
            break;

        case 0x8001:
            regs->reg8001.word = data;
            mapper4_do_command(nes);
            break;

        case 0xA000:
            regs->regA000.word = data;
            nes->ppu.state.mirroring = (regs->regA000.bits.mirror_horz) ? MirrorHorizontal : MirrorVertical;
            NOTIFY("Mirroring: %s\n", regs->regA000.bits.mirror_horz ? "Horizontal" : "Vertical");
            break;

        case 0xA001:
            // FIXME: implement this
            regs->regA001.word = data;
            NOTIFY("Save ram: %s\n", regs->regA001.bits.saveram_enable ? "Enabled" : "Disabled");
            break;

        case 0xC000:
            LOG("IRQ Reload Count write: %d\n", data);
            regs->reload_count = data;
            break;

        case 0xC001:
            LOG("IRQ Clear\n");
            regs->scanline_count = 0;
            break;

        case 0xE000:
            // Latch IRQ
            regs->irq_enable = 0;
            regs->irq_pending = 0;
            break;

        case 0xE001:
            LOG("Enabled IRQ\n");
            regs->irq_enable = 1;
            break;

        default:
            NOTIFY("WARNING: Mapper write to %04X invalid (data:%02X)\n", addr, data);
            break;
    }
}

static int mapper_scanline(NES_t *nes)
{
    Mapper4Regs_t *regs = (Mapper4Regs_t *) &nes->state.mapper_state;

/*
Every time the MMC3 detects a scanline, the following IRQ Counter logic is executed.  Note this occurs EVEN
IF IRQs are disabled (the IRQ counter is always counting):

- If IRQ Counter is 0...
     a)  reload IRQ counter with IRQ Reload value

- Otherwise...
     a)  Decrement IRQ counter by 1
     b)  If IRQ counter is now 0 and IRQs are enabled, trigger IRQ
*/

    LOG("Scanline:%d c:%d r:%d\n", nes->ppu.scanline, regs->scanline_count, regs->reload_count);

    if(nes->ppu.background_visible || nes->ppu.sprites_visible)
    {
        if(regs->scanline_count == 0)
        {
            regs->scanline_count = regs->reload_count;
        }
        else if(regs->scanline_count > 0)
        {
            regs->scanline_count--;
            if(regs->scanline_count == 0)
            {
                if(regs->irq_enable)
                {
                    LOG("IRQ pending!\n");
                    regs->irq_pending = 1;
                }
            }
        }

        return regs->irq_pending;
    }

    return 0;
}

static void
mapper_restore(NES_t *nes)
{
    mapper4_update_prg_rom_banks(nes);
}

// --------------------------------------------------------------------------------

NESMapper_t nes_mapper4 = {MAPPER_NUM, MAPPER_NAME, mapper_init, mapper_write, mapper_restore, mapper_scanline};
