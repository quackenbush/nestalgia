#include "nes_mapper.h"

extern NESMapper_t nes_mapper0;
extern NESMapper_t nes_mapper1;
extern NESMapper_t nes_mapper2;
extern NESMapper_t nes_mapper3;
extern NESMapper_t nes_mapper4;
extern NESMapper_t nes_mapper7;
extern NESMapper_t nes_mapper71;
extern NESMapper_t nes_mapper87;

const NESMapper_t *MAPPERS[] =
{
    &nes_mapper0,
    &nes_mapper1,
    &nes_mapper2,
    &nes_mapper3,
    &nes_mapper4,
    &nes_mapper7,
    &nes_mapper71,
    &nes_mapper87,
};

void
nes_select_prg_rom_bank(NES_t *nes, unsigned dest_bank, unsigned src_bank, int size_kb)
{
    int i;
    INFO_MAPPER("Switching PRG-ROM %d from %d\n", dest_bank, src_bank);

    ASSERT(dest_bank * size_kb <= 8, "Bad dest PRG-ROM bank: %d\n", dest_bank);

    // FIXME: assertion hacked for 4KB banks only
    if(size_kb == 4)
    {
        ASSERT(src_bank < nes->num_prg_rom_banks, "Bad src PRG-ROM bank: %d (%d available)\n",
               src_bank, nes->num_prg_rom_banks);
    }

    for(i = 0; i < size_kb; i++)
    {
        nes->prg_rom[dest_bank * size_kb + i] = nes->prg_rom_banks + (src_bank * size_kb + i) * 0x1000;
    }
}

static const NESMapper_t *
nes_mapper_get(NES_t *nes)
{
    size_t i;
    for(i = 0; i < sizeof(MAPPERS) / sizeof(MAPPERS[0]); i++)
    {
        const NESMapper_t *mapper = MAPPERS[i];
        if(nes->mapper_num == mapper->num)
            return mapper;
    }

    return NULL;
}

void
nes_mapper_init(NES_t *nes)
{
    const NESMapper_t *mapper = nes_mapper_get(nes);

    if(mapper)
    {
        nes->prg_rom_write = mapper->write_func;
        NOTIFY("Selected Mapper %d: %s\n", mapper->num, mapper->name);
        mapper->init_func(nes);

        return;
    }

    ASSERT(0, "Unsupported mapper: %d\n", nes->mapper_num);
}

void
nes_mapper_restore(NES_t *nes)
{
    const NESMapper_t *mapper = nes_mapper_get(nes);

    if(mapper->restore_func)
    {
        mapper->restore_func(nes);
    }
}

int
nes_mapper_scanline(NES_t *nes)
{
    const NESMapper_t *mapper = nes_mapper_get(nes);

    if(mapper->scanline_func)
    {
        return mapper->scanline_func(nes);
    }

    return 0;
}
