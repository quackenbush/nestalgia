#ifndef __nes_ppu_h__
#define __nes_ppu_h__

#include <stdint.h>
#include "display.h"
#include "n6502.h" // So that we can track CPU cycles

#define OLDPPU

#define VRAM_SIZE         (2*1024)
#define VROM_SIZE         (0x1000)
#define VROM_BANK_SIZE    (8*1024)

#define SPR_RAM_SIZE      256

#define NUM_SPRITES       64
#define PATTERN_HEIGHT    8
#define PATTERN_WIDTH     8

#define PPU_PATTERN_SIZE  16
#define NUM_PATTERNS_PER_TABLE (0x1000 / PPU_PATTERN_SIZE)
#define CACHED_PATTERN_SIZE    (PATTERN_WIDTH * PATTERN_HEIGHT)

#define NES_WIDTH         256
#define NES_HEIGHT        240

#define NES_CROPPED_WIDTH(PPU)  (NES_WIDTH - ((PPU)->options.crop_ntsc ? 16 : 0))
#define NES_CROPPED_HEIGHT(PPU) (NES_HEIGHT - ((PPU)->options.crop_ntsc ? 16 : 0))

#define ATTRIBUTE_WIDTH   32
#define ATTRIBUTE_HEIGHT  32

#define PPU_CYCLES_PER_SCANLINE 341

// Selected palette
#define PALETTE_SIZE      16

// Total palette size
#define NES_PPU_PALETTE_SIZE 64

#define MAX_SPRITE_Y_COORD 239

// NTSC PPU: 256x240@60Hz
// PAL  PPU: 256x240@50Hz
#define NES_PPU_FRAMES_PER_SECOND 60

#define NES_PPU_SCANLINES         262
#define NES_PPU_VBLANK            0
#define NES_PPU_VERTICAL_RESET    20

#define NES_PPU_SPRITE_YOFFSET 1

#define PPU_NAME_TABLE(X)  (((X) >> 10) & 3)

// --------------------------------------------------------------------------------
// PPU Control Register 1
typedef union
{
    uint8_t word;

    struct
    {
        unsigned name_table_address   : 2; // 00: $2000 (VRAM)
                                           // 01: $2400 (VRAM)
                                           // 10: $2800 (VRAM)
                                           // 11: $2C00 (VRAM)

        unsigned increment_by_32      : 1; // Increment by {1, 32}

        unsigned sprite_pattern_table : 1; // 0: $0000 (VRAM), 1: $1000 (VRAM)
        unsigned bg_pattern_table     : 1; // 0: $0000 (VRAM), 1: $1000 (VRAM)

        unsigned sprite_size_8x16     : 1; // 0: 8x8, 1: 8x16
        unsigned master_slave         : 1; // UNUSED
        unsigned nmi_on_vblank        : 1;

    } bits;
} PPUControl1Reg_t;

// --------------------------------------------------------------------------------
// PPU Control Register 2
typedef union
{
    uint8_t word;

    struct
    {
        unsigned monochrome          : 1;

        unsigned background_clipping : 1; // 0: Background clipped in left 8-pixel column
                                          // 1: Background not clipped
        unsigned sprite_clipping     : 1; // 0: Sprites clipped in left 8-pixel column
                                          // 1: Sprites not clipped

        unsigned background_visible  : 1;
        unsigned sprites_visible     : 1;

        unsigned background_color    : 3; // 000: None
                                          // 001: Green
                                          // 010: Blue
                                          // 100: Red

    } bits;
} PPUControl2Reg_t;

// --------------------------------------------------------------------------------
#define SPRITE_PALETTE  (3 << 0) // 1-0  Sprite Palette       (0-3=Sprite Palette 0-3)
#define SPRITE_PRIORITY (1 << 5) // 5    Background Priority  (0=Sprite In front of BG, 1=Sprite Behind BG)
#define SPRITE_FLIP_H   (1 << 6) // 6    Horizontal Flip      (0=Normal, 1=Mirror)
#define SPRITE_FLIP_V   (1 << 7) // 7    Vertical Flip        (0=Normal, 1=Mirror)

/*
PPU Memory Map (14bit buswidth, 0-3FFFh)
  0000h-0FFFh   Pattern Table 0 (4K) (256 Tiles)
  1000h-1FFFh   Pattern Table 1 (4K) (256 Tiles)
  2000h-23FFh   Name Table 0 and Attribute Table 0 (1K) (32x30 BG Map)
  2400h-27FFh   Name Table 1 and Attribute Table 1 (1K) (32x30 BG Map)
  2800h-2BFFh   Name Table 2 and Attribute Table 2 (1K) (32x30 BG Map)
  2C00h-2FFFh   Name Table 3 and Attribute Table 3 (1K) (32x30 BG Map)
  3000h-3EFFh   Mirror of 2000h-2EFFh
  3F00h-3F1Fh   Background and Sprite Palettes (25 entries used)
  3F20h-3FFFh   Mirrors of 3F00h-3F1Fh
Note: The NES contains only 2K built-in VRAM, which can be used for whatever purpose (for example, as two Name Tables, or as one Name Table plus 64 Tiles). Palette Memory is built-in as well. Any additional VRAM (or, more regulary, VROM) is located in the cartridge, which may also contain mapping hardware to access more than 12K of video memory.

Palette Memory (25 entries used)
  3F00h        Background Color (Color 0)
  3F01h-3F03h  Background Palette 0 (Color 1-3)
  3F05h-3F07h  Background Palette 1 (Color 1-3)
  3F09h-3F0Bh  Background Palette 2 (Color 1-3)
  3F0Dh-3F0Fh  Background Palette 3 (Color 1-3)
  3F11h-3F13h  Sprite Palette 0 (Color 1-3)
  3F15h-3F17h  Sprite Palette 1 (Color 1-3)
  3F19h-3F1Bh  Sprite Palette 2 (Color 1-3)
  3F1Dh-3F1Fh  Sprite Palette 3 (Color 1-3)

Palette Gaps and Mirrors
  3F04h,3F08h,3F0Ch - Three general purpose 6bit data registers.
  3F10h,3F14h,3F18h,3F1Ch - Mirrors of 3F00h,3F04h,3F08h,3F0Ch.
  3F20h-3FFFh - Mirrors of 3F00h-3F1Fh.

*/

typedef struct
{
    uint8_t y_coord_minus_1;
    uint8_t tile_index;
    uint8_t attributes;
    uint8_t x_coord;
} NESPPUSprite_t;

typedef struct
{
    uint8_t name[0x3c0];
    uint8_t attribute[0x40];
} NameAttributeTable_t;

typedef struct
{
    NameAttributeTable_t na_tables[4];

    uint8_t na_mirror[0xf00];

    uint8_t image_palette[PALETTE_SIZE * 2]; // Image/sprite palettes
} NESPPUMemoryMap_t;

typedef enum
{
    Mirror1ScreenA = 0,
    Mirror1ScreenB,
    MirrorVertical,
    MirrorHorizontal,
} PPUMirroring_t;

extern const char *PPU_MIRRORING_STR[4];

#define BACKGROUND_WIDTH (NES_WIDTH * 2)

typedef struct
{
    struct
    {
        uint8_t nes_screen[NES_WIDTH * NES_HEIGHT];
        uint8_t nes_background[BACKGROUND_WIDTH * NES_HEIGHT]; // East/west background; FIXME: North/south background

        uint32_t screen_palette[NES_PPU_PALETTE_SIZE];

        Window_t nes_window;
        Window_t ppu_info_window;
        Window_t palette_window;
        Window_t sprites_window;
        Window_t background_window;
    } gui;

    struct
    {
        PPUMirroring_t mirroring; // FIXME: this should be moved into NES struct
        unsigned pal; // 0 = NTSC, 1 = PAL
        uint8_t four_screen;

        PPUControl1Reg_t control1;
        PPUControl2Reg_t control2;

        uint8_t spr_ram_address;
        int bank_mapping[8];

        union
        {
            uint8_t ram[SPR_RAM_SIZE];
            NESPPUSprite_t sprites[NUM_SPRITES];
        } spr_ram;

        union
        {
            uint8_t physical[0x2000];

            NESPPUMemoryMap_t map;
        } bank2;

        uint8_t builtin_bank[8][1024];

        struct
        {
            uint16_t V; // active VRAM address
            uint16_t T; // VRAM address latch
            uint16_t X; // fine X scroll register
            uint8_t S;

            uint8_t read_buffer;
            uint8_t increment;

            uint8_t first_write;
        } vram;
    } state;

    unsigned num_vrom_banks;
    uint8_t *vrom_banks;
    uint8_t has_vrom;

    uint8_t *bank[8];

    uint8_t in_vblank;

    uint8_t nmi_on_vblank;
    uint8_t sprite_height_16;
    uint8_t spr_pattern_table;
    uint8_t bg_pattern_table;

    uint8_t background_color;
    uint8_t background_black;
    uint8_t background_red;
    uint8_t background_green;
    uint8_t background_blue;
    uint8_t sprites_visible;
    uint8_t background_visible;
    uint8_t sprite_clipping;
    uint8_t background_clipping;
    uint8_t monochrome;

    unsigned last_frame_ms;

    unsigned scanline;
    int64_t scanline_start_ppu_cycle;

    unsigned frame_count;

#ifndef OLDPPU
    struct
    {
        int index[16];
        int count;
    } scanline_sprites;
#endif

    struct
    {
        int x;
        int y;
        int index;

        int hit;
    } sprite0;

    struct
    {
        unsigned display_width;
        unsigned display_height;
        unsigned display_windowed;
        unsigned enable_scanlines;

        int sprite_clip_right; // HACK for Blargg PPU testing
        int trigger_hack; // HACK for double dragon sprite0 triggering
        unsigned crop_ntsc;
        unsigned force_sprite0;
        unsigned additional_delay_ms;
        unsigned no_vsync;
        unsigned sync_every_nth_scanline;

        unsigned hide_sprites;
        unsigned sprite0_negative;
        unsigned paused;

        unsigned enable_paddle; // FIXME: move to input.c
    } options;

    uint8_t pattern_cache[2][NUM_PATTERNS_PER_TABLE][CACHED_PATTERN_SIZE];
    uint8_t pattern_dirty[2][NUM_PATTERNS_PER_TABLE];
    unsigned dirty;

    union
    {
        uint8_t word;
        struct
        {
            unsigned __reserved            : 4;

            unsigned vram_write_flag       : 1;
            unsigned scanline_sprite_count : 1;
            unsigned sprite0_collision     : 1;
            unsigned vblank                : 1;
        } bits;
    } status;

    Display_t *display;
    N6502_t *cpu;
} NESPPU_t;

void nes_ppu_init(NESPPU_t *ppu, Display_t *display, N6502_t *cpu);
void nes_ppu_reset(NESPPU_t *ppu);
void nes_ppu_restore(NESPPU_t *ppu);

void nes_ppu_select_vrom_bank(NESPPU_t *ppu, unsigned vrom_dest, unsigned vrom_src, unsigned size_kb);

void nes_ppu_update_status(NESPPU_t *ppu);
void nes_ppu_render(NESPPU_t *ppu);
void nes_ppu_latch_joypads(NESPPU_t *ppu, uint8_t *pad1, uint8_t *pad2, uint8_t *mousedown);
void nes_ppu_render_scanline(NESPPU_t *ppu);

void nes_ppu_reg_write(NESPPU_t *ppu, uint16_t addr, uint8_t data);
uint8_t nes_ppu_read_vram(NESPPU_t *ppu);

#endif
