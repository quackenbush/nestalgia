#ifndef __ines_h__
#define __ines_h__

#include <string.h>

#define DUMP4(A) A[0], A[1], A[2], A[3]

// --------------------------------------------------------------------------------
/*
.NES File Format (http://wiki.nesdev.com/w/index.php/INES)

Byte     Contents
---------------------------------------------------------------------------
0-3      String "NES^Z" used to recognize .NES files.
4        Number of 16kB ROM banks.
5        Number of 8kB VROM banks.
6        bit 0     1 for vertical mirroring, 0 for horizontal mirroring.
         bit 1     1 for battery-backed RAM at $6000-$7FFF.
         bit 2     1 for a 512-byte trainer at $7000-$71FF.
         bit 3     1 for a four-screen VRAM layout.
         bit 4-7   Four lower bits of ROM Mapper Type.
7        bit 0     1 for VS-System cartridges.
         bit 1-3   Reserved, must be zeroes!
         bit 4-7   Four higher bits of ROM Mapper Type.
8        Number of 8kB RAM banks. For compatibility with the previous
         versions of the .NES format, assume 1x8kB RAM page when this
         byte is zero.
9        bit 0     1 for PAL cartridges, otherwise assume NTSC.
         bit 1-7   Reserved, must be zeroes!
10-15    Reserved, must be zeroes!
16-...   ROM banks, in ascending order. If a trainer is present, its
         512 bytes precede the ROM bank contents.
...-EOF  VROM banks, in ascending order.
---------------------------------------------------------------------------
*/
typedef struct
{
    // 00h
    uint8_t file_id[4];

    // 04h
    uint8_t num_16k_prg_rom_banks;
    uint8_t num_8k_vrom_banks;
    uint8_t rom_control_byte1;
    uint8_t rom_control_byte2;

    // 08h
    uint8_t num_8k_prg_ram_banks; // FIXME: how is this different than SRAM?
    uint8_t flags9;
    uint8_t flags10;
    uint8_t reserved_0Bh;
    uint8_t reserved_0Ch;
    uint8_t reserved_0Dh;
    uint8_t reserved_0Eh;
    uint8_t reserved_0Fh;
} iNES_t;

static const uint8_t INES_HEADER[4] = {'N', 'E', 'S', 0x1A};

static inline int
ines_load(iNES_t *ines, FILE *fp)
{
    int result = fread(ines, sizeof(iNES_t), 1, fp);
    if(result != 1)
        return 0;

    if(memcmp(INES_HEADER, ines->file_id, sizeof(INES_HEADER)) != 0)
    {
        //"Bad iNES header: %c%c%c%x\n", DUMP4(ines->file_id);
        return 0;
    }

    return 1;
}

static inline int
ines_mapper_num(iNES_t *ines)
{
    int mapper = (ines->rom_control_byte1 >> 4);
    int last_4bytes = (ines->reserved_0Ch |
                       ines->reserved_0Dh |
                       ines->reserved_0Eh |
                       ines->reserved_0Fh);

    // FIXME: need to check iNES v2.0

    // If the last 4 header bytes are nonzero, it's probably an old !DiskDude! dump, which only
    // uses the bottom nibble (mappers 0-15)
    if(last_4bytes == 0)
    {
        mapper |= (ines->rom_control_byte2 & 0xf0);
    }

    return mapper;
}

#endif
