#include "nes_ppu.h"
#include "log.h"
#include "display.h"
#include "window.h"
#include <inttypes.h>
#include <string.h>

#define LOG(...)  _LOG(PPU, __VA_ARGS__)
#define INFO(...) _INFO(PPU, __VA_ARGS__)

/*
FIXME:

If the VRAM address increment bit is set (inc. amt. = 32), the only
difference is that the HT counter is no longer being clocked, and the VT
counter is now being clocked by access to 2007.

*/

/*
FIXME:

- MONOCHROME mode

- track frame time over time, so that if you get behind it can catch up and not
  go fast then slow, etc.  Try Mega Man 2 with memcpy MMC1 to see the lag.
  => or, stop the wall-clock time if we get behind and run in slow-motion

- assuming pattern/name tables are ROMs, pre-cache the entire regions

*/


// NTSC: http://slack.net/~ant/libs/ntsc.html#nes_ntsc
// --------------------------------------------------------------------------------

// Double-width/height pixels
#define PIXEL_DOUBLING

#define NES_WINDOW_WIDTH  (NES_WIDTH*XD)

#ifdef PIXEL_DOUBLING
#define XD 2
#define YD 2
#else
#define XD 1
#define YD 1
#endif

// FIXME: this needs to take XPOS into account and needs to take into account HFLIP
//#define PPU_SPRITE_VISIBLE(PPU, SPRITE) (! (PPU->sprite_clipping && SPRITE->x_coord < 8) && SPRITE->y_coord_minus_1 < MAX_SPRITE_Y_COORD)
// FIXME: is this < or <= for bottom coord?
#define PPU_SPRITE_VISIBLE(PPU, SPRITE) ((SPRITE->y_coord_minus_1 <= MAX_SPRITE_Y_COORD) && (SPRITE->x_coord < NES_WIDTH - 1))

#define LOG(...) _LOG(PPU, __VA_ARGS__)

const char *PPU_MIRRORING_STR[4] =
{
    "1ScA",
    "1ScB",
    "Vert",
    "Horz",
};

static void nes_ppu_sprites_window_init(NESPPU_t *ppu);
static void nes_ppu_background_window_init(NESPPU_t *ppu);

// --------------------------------------------------------------------------------

static void
ppu_latch(NESPPU_t *ppu)
{
    ppu->state.vram.V = ppu->state.vram.T;
    ppu->bg_pattern_table = ppu->state.vram.S;
}

static void
nes_ppu_info_window_draw(Display_t *display, Window_t *window, DisplayPixel_t *origin, int stride, Rect_t *clip)
{
    char msg[64];
    NESPPU_t *ppu = (NESPPU_t *) window->p;
    int scroll_x = (int) ((ppu->state.vram.V & 0x1f) << 3) | ppu->state.vram.X;
    int scroll_y = (int) (((ppu->state.vram.V >> 5) & 0x1f) << 3) | (ppu->state.vram.V >> 12);

    sprintf(msg, "Scroll: %03d x %03d\nNT: %d%d, BG: %d, M: %s\nS0 #%3d (%3d,%3d)",
            scroll_x, scroll_y,
            (ppu->state.vram.V >> 11) & 1,
            (ppu->state.vram.V >> 10) & 1,
            ppu->bg_pattern_table,
            PPU_MIRRORING_STR[ppu->state.mirroring],
            ppu->sprite0.index, ppu->sprite0.x, ppu->sprite0.y);

    int font_y_offset = 1;
    font_printstr(ppu->display->font, (origin + 1 + font_y_offset * stride), stride, msg, clip);
}

static void
nes_ppu_info_window_init(NESPPU_t *ppu)
{
    Window_t *window = &ppu->gui.ppu_info_window;
    window->title = "PPU";
    window->width = 260;
    window->height = 50;
    window->p = ppu;
    window->draw = nes_ppu_info_window_draw;
    window->x = 0;
    window->y = 340;
    window_init(window);

    display_add_window(ppu->display, window);
}

// --------------------------------------------------------------------------------

static void
nes_ppu_palette_window_draw(Display_t *display, Window_t *window, DisplayPixel_t *origin, int stride, Rect_t *clip)
{
    static const int SWATCH_SIZE = 14;
    static const int SWATCH_GAP = 3;

    NESPPU_t *ppu = (NESPPU_t *) window->p;
    int palette_offset;

    ASSERT(ppu->display->width == stride, "Bad stride\n");

    // FIXME: the sprite palette "transparent" entries are displaying as gray, not transparent
    // See excitebike
    for(palette_offset = 0; palette_offset <= 1; palette_offset++)
    {
        const uint8_t *palette = &ppu->state.bank2.map.image_palette[palette_offset * PALETTE_SIZE];
        int y_offset = palette_offset * (SWATCH_SIZE + SWATCH_GAP) * stride;
        int i;

        for(i = 0; i < PALETTE_SIZE; i++)
        {
            char num[4];
            sprintf(num, "%02d", palette[i]);
            const DisplayPixel_t color = ppu->gui.screen_palette[palette[i]];
            int x_offset = i * (SWATCH_SIZE + SWATCH_GAP);
            display_fillrect(origin + x_offset + y_offset, SWATCH_SIZE, SWATCH_SIZE, color, stride, clip);
            font_printstr(ppu->display->font, (origin + x_offset + y_offset + 1), stride, num, clip);
        }
    }
}

static void
nes_ppu_palette_window_init(NESPPU_t *ppu)
{
    Window_t *window = &ppu->gui.palette_window;
    window->title = "Palette";
    window->width = 270;
    window->height = 40;
    window->p = ppu;
    window->draw = nes_ppu_palette_window_draw;
    window->x = 500;
    window->y = 500;
    window_init(window);

    display_add_window(ppu->display, window);
}

// --------------------------------------------------------------------------------

static const uint8_t NES_PPU_PALETTE[NES_PPU_PALETTE_SIZE][3] =
{
    { 0x80, 0x80, 0x80 }, { 0x00, 0x3D, 0xA6 }, { 0x00, 0x12, 0xB0 }, { 0x44, 0x00, 0x96 },
    { 0xA1, 0x00, 0x5E }, { 0xC7, 0x00, 0x28 }, { 0xBA, 0x06, 0x00 }, { 0x8C, 0x17, 0x00 },
    { 0x5C, 0x2F, 0x00 }, { 0x10, 0x45, 0x00 }, { 0x05, 0x4A, 0x00 }, { 0x00, 0x47, 0x2E },
    { 0x00, 0x41, 0x66 }, { 0x00, 0x00, 0x00 }, { 0x05, 0x05, 0x05 }, { 0x05, 0x05, 0x05 },
    { 0xC7, 0xC7, 0xC7 }, { 0x00, 0x77, 0xFF }, { 0x21, 0x55, 0xFF }, { 0x82, 0x37, 0xFA },
    { 0xEB, 0x2F, 0xB5 }, { 0xFF, 0x29, 0x50 }, { 0xFF, 0x22, 0x00 }, { 0xD6, 0x32, 0x00 },
    { 0xC4, 0x62, 0x00 }, { 0x35, 0x80, 0x00 }, { 0x05, 0x8F, 0x00 }, { 0x00, 0x8A, 0x55 },
    { 0x00, 0x99, 0xCC }, { 0x21, 0x21, 0x21 }, { 0x09, 0x09, 0x09 }, { 0x09, 0x09, 0x09 },
    { 0xFF, 0xFF, 0xFF }, { 0x0F, 0xD7, 0xFF }, { 0x69, 0xA2, 0xFF }, { 0xD4, 0x80, 0xFF },
    { 0xFF, 0x45, 0xF3 }, { 0xFF, 0x61, 0x8B }, { 0xFF, 0x88, 0x33 }, { 0xFF, 0x9C, 0x12 },
    { 0xFA, 0xBC, 0x20 }, { 0x9F, 0xE3, 0x0E }, { 0x2B, 0xF0, 0x35 }, { 0x0C, 0xF0, 0xA4 },
    { 0x05, 0xFB, 0xFF }, { 0x5E, 0x5E, 0x5E }, { 0x0D, 0x0D, 0x0D }, { 0x0D, 0x0D, 0x0D },
    { 0xFF, 0xFF, 0xFF }, { 0xA6, 0xFC, 0xFF }, { 0xB3, 0xEC, 0xFF }, { 0xDA, 0xAB, 0xEB },
    { 0xFF, 0xA8, 0xF9 }, { 0xFF, 0xAB, 0xB3 }, { 0xFF, 0xD2, 0xB0 }, { 0xFF, 0xEF, 0xA6 },
    { 0xFF, 0xF7, 0x9C }, { 0xD7, 0xE8, 0x95 }, { 0xA6, 0xED, 0xAF }, { 0xA2, 0xF2, 0xDA },
    { 0x99, 0xFF, 0xFC }, { 0xDD, 0xDD, 0xDD }, { 0x11, 0x11, 0x11 }, { 0x11, 0x11, 0x11 },
};

static void
nes_ppu_window_draw(Display_t *display, Window_t *window, DisplayPixel_t *origin, int stride, Rect_t *clip)
{
    NESPPU_t *ppu = (NESPPU_t *) window->p;
    const uint8_t *palette_offset = ppu->state.bank2.map.image_palette;
    DisplayPixel_t *row;

    if(ppu->options.crop_ntsc)
    {
        clip->top = max(clip->top, 8);
        clip->left = max(clip->left, 8);
    }

    int clip_left   = clip->left / XD;
    int clip_right  = /*clip->left +*/ NES_CROPPED_WIDTH(ppu) - (clip->right / XD);
    int clip_top    = clip->top / YD;
    int clip_bottom = /*clip->top +*/ NES_CROPPED_HEIGHT(ppu) - (clip->bottom / YD);

    int x, y;

    row = origin;

    for(y = clip_top; y < clip_bottom; y++)
    {
        DisplayPixel_t *pixels = row;

        uint8_t *src = &ppu->gui.nes_screen[y * NES_WIDTH + clip_left];
        for(x = clip_left; x < clip_right; x++)
        {
            *pixels = ppu->gui.screen_palette[palette_offset[*src++]];
#ifdef PIXEL_DOUBLING
            pixels[1] = *pixels;
            pixels += 2;
#else
            pixels++;
#endif
        }
#if defined(PIXEL_DOUBLING)
#define NES_WINDOW_WIDTH_BYTES ((clip_right - clip_left) * BPP * ppu->display->depth_in_bytes)
        if(ppu->options.enable_scanlines)
        {
            // TV scanline effect by rendering odd lines black
            memset(row + stride, 0x22, NES_WINDOW_WIDTH_BYTES);
        }
        else
        {
            // Line doubling (copy even line to odd line)
            memcpy(row + stride, row, NES_WINDOW_WIDTH_BYTES);
        }
#endif

        row += stride * YD;
    }
}

static void
nes_ppu_init_palette(Window_t *window)
{
    NESPPU_t *ppu = (NESPPU_t *) window->p;
    unsigned i;

    // Initialize the palette
    for(i = 0; i < NES_PPU_PALETTE_SIZE; i++)
    {
        const uint8_t *palette_entry = NES_PPU_PALETTE[i];

        ppu->gui.screen_palette[i] = display_maprgb(ppu->display, palette_entry[0], palette_entry[1], palette_entry[2]);
    }
}

void
nes_ppu_window_init(NESPPU_t *ppu)
{
    Window_t *window = &ppu->gui.nes_window;
    window->title = "NES";
    window->width = NES_CROPPED_WIDTH(ppu) * XD;
    window->height = NES_CROPPED_HEIGHT(ppu) * YD;
    window->p = ppu;
    window->draw = nes_ppu_window_draw;
    window->reinit = nes_ppu_init_palette;
    window->x = 0;
    window->y = 0;
    window_init(window);
    window->has_titlebar = 0;

    window->can_close = 0;

    nes_ppu_init_palette(window);;
    display_add_window(ppu->display, window);
}

void
nes_ppu_init(NESPPU_t *ppu, Display_t *display, N6502_t *cpu)
{
    ppu->display = display;
    ppu->cpu = cpu;

    nes_ppu_window_init(ppu);

    if(ppu->options.display_windowed)
    {
        nes_ppu_info_window_init(ppu);
        nes_ppu_palette_window_init(ppu);
        nes_ppu_sprites_window_init(ppu);
        nes_ppu_background_window_init(ppu);

        // Move NES window to right side of screen
        ppu->gui.nes_window.x = ppu->display->width - ppu->gui.nes_window.width - 2;
    }
}

void
nes_ppu_reset(NESPPU_t *ppu)
{
    memset(&ppu->gui.nes_screen, 0, sizeof(ppu->gui.nes_screen));
    memset(&ppu->gui.nes_background, 0, sizeof(ppu->gui.nes_background));

    ppu->state.vram.first_write = 1;

    ppu->state.control1.word = 0xff; // FIXME: bogus reset value
    ppu->state.control2.word = 0xff; // FIXME: bogus reset value

    ppu->nmi_on_vblank = 0;

#if 0 // FIXME: attempt at reset to black for Zelda (not working)
    ppu->background_color = 0;
    ppu->background_black = 1;
    ppu->background_red   = 0;
    ppu->background_green = 0;
    ppu->background_blue  = 0;
#endif

    {
        int i;
        for(i = 0; i < 8; i++)
            ppu->bank[i] = ppu->state.builtin_bank[i];
    }

    ppu->state.bank_mapping[0] = -1;
    ppu->state.bank_mapping[1] = -1;

    ppu->scanline = 0;
    ppu->in_vblank = 0;
    ppu->num_vrom_banks = 0;
    ppu->vrom_banks = NULL;

    ppu->scanline_start_ppu_cycle = 0;
    ppu->frame_count = 0;
    ppu->status.word = 0;

    ppu->state.vram.V = 0;
    ppu->state.vram.T = 0;
    ppu->state.vram.X = 0;
    ppu->state.vram.S = 0;

    ppu->sprites_visible = 0;
    ppu->background_visible = 0;

    ppu->dirty = 1;
}

static inline uint16_t
nes_ppu_get_vram_address(NESPPU_t *ppu, uint16_t mask)
{
    uint16_t vram_address = ppu->state.vram.V & mask;

    // Name table mirror
    if(vram_address >= 0x2000 && vram_address < 0x3f00)
    {
        // $3xxx => $2xxx
        vram_address &= 0x2fff;

        switch(ppu->state.mirroring)
        {
            case Mirror1ScreenA:
                vram_address &= 0x23ff;
                break;

            case Mirror1ScreenB:
                vram_address &= 0x23ff;
                vram_address |= 0x0400;
                break;


            case MirrorVertical:
                // Vertical Mirroring
                // $20xx => $20xx (no change)
                // $24xx => $24xx (no change)
                // $28xx => $20xx
                // $2Cxx => $24xx
                vram_address &= 0x27ff;
                break;

            case MirrorHorizontal:
            default:
                // Horizontal Mirroring
                // $20xx => $20xx (no change)
                // $24xx => $20xx
                // $28xx => $24xx
                // $2Cxx => $24xx

                if(vram_address >= 0x2C00)
                {
                    // $2Cxx => $24xx
                    vram_address -= 0x800;
                }
                else if(vram_address >= 0x2400)
                {
                    // $24xx => $20xx
                    // $28xx => $24xx
                    vram_address -= 0x400;
                }
                break;
        }
    }

    // Palette mirrors
    else if(vram_address >= 0x3f00)
    {
        vram_address &= 0x3f1f;

        // Transparency palette mirror
        // 3f14, 3f18, etc. => 3f04, 3f08, etc.
        if((vram_address & 0x03) == 0)
        {
            vram_address &= 0x3f0f;
        }
    }

    return vram_address;
}

static inline void
nes_ppu_increment_vram_address(NESPPU_t *ppu)
{
    ppu->state.vram.V = (ppu->state.vram.V + ppu->state.vram.increment) & 0x7FFF;
}

//#define PPU_BANK(PPU, ADDR) ((ADDR) < 0x2000 ? (PPU)->state.bank0.physical[ADDR] : (PPU)->state.bank1.physical[(ADDR) - 0x2000])
static inline uint8_t
PPU_BANK(NESPPU_t *ppu, uint16_t addr)
{
    switch((addr >> 12) & 3)
    {
        case 0:
        case 1:
            return ppu->bank[(addr >> 10) & 0xf][addr & 0x03ff];

        case 2:
        case 3:
        default:
            return ppu->state.bank2.physical[addr & 0x1fff];
    }
}

//#define PPU_BANK_PTR(PPU, ADDR) ((ADDR) < 0x2000 ? &((PPU)->state.bank0.physical[ADDR]) : &((PPU)->state.bank1.physical[(ADDR) - 0x2000]))

static inline uint8_t *
PPU_BANK_PTR(NESPPU_t *ppu, uint16_t addr)
{
    switch((addr >> 12) & 3)
    {
        case 0:
        case 1:
            return &ppu->bank[(addr >> 10) & 0xf][addr & 0x03ff];

        case 2:
        case 3:
        default:
            return &ppu->state.bank2.physical[addr & 0x1fff];
    }
}

uint8_t
nes_ppu_read_vram(NESPPU_t *ppu)
{
    uint16_t vram_address = nes_ppu_get_vram_address(ppu, 0x3fff);
    uint8_t data = ppu->state.vram.read_buffer;
    ppu->state.vram.read_buffer = PPU_BANK(ppu, vram_address);
    if(vram_address >= 0x3f00)
    {
        // Palette read
        data = ppu->state.vram.read_buffer;
        // Mask per blargg ppu test: vram_access
        ppu->state.vram.read_buffer = PPU_BANK(ppu, nes_ppu_get_vram_address(ppu, 0x2fff));
    }

    LOG("PPU VRAM[%04Xh] => %02Xh\n", vram_address, data);
    nes_ppu_increment_vram_address(ppu);

    return data;
}

static inline void
nes_ppu_write_vram(NESPPU_t *ppu, uint8_t data)
{
    uint16_t vram_address = nes_ppu_get_vram_address(ppu, 0x3fff);
    // FIXME: for double VROM games like baseball, this assert fires
    if(ppu->has_vrom && vram_address < VROM_BANK_SIZE)
    {
        //ASSERT(0, "WARNING: writing over VROM[%04Xh]\n", vram_address);
    }

    // Palette can only hold 6 bits
    if(vram_address >= 0x3f00)
    {
        data &= 0x3f;
        INFO("PPU Palette[%04X] #%d <= $%02X\n",
             vram_address, vram_address - 0x3f00, data);
    }
    else
    {
        if(vram_address < 0x2000)
        {
            // Flag the affected pattern as dirty
            // FIXME: this calculation may be bogus
            ppu->pattern_dirty[vram_address >> 13][vram_address >> 4] = 1;
        }

        if(! ppu->in_vblank && (ppu->sprites_visible || ppu->background_visible))
        {
            printf("WARNING: write vram[%04Xh] <= %02Xh outside of VBLANK!\n", vram_address, data);
        }
    }

    uint8_t *p = PPU_BANK_PTR(ppu, vram_address);
    *p = data;
    LOG("PPU VRAM[%04Xh] <= %02Xh\n", ppu->state.vram.T, data);
    nes_ppu_increment_vram_address(ppu);
}

static void
nes_ppu_write_control1(NESPPU_t *ppu, uint8_t data)
{
    PPUControl1Reg_t *reg = &ppu->state.control1;

    ppu->state.vram.T = (ppu->state.vram.T & 0x73ff) | ((data & 3) << 10);

    // FIXME: what is this for?
    //ASSERT((data & PPU_CONTROL1_NAME_TABLE_ADDRESS) == PPU_NAME_TABLE(ppu->state.vram.T),
    //       "Name table bad\n");

    if(reg->word != data)
    {
        reg->word = data;

        // FIXME: decoding these bits is probably a waste of time
        ppu->sprite_height_16  = reg->bits.sprite_size_8x16;
        ppu->state.vram.S            = reg->bits.bg_pattern_table;
        ppu->spr_pattern_table = reg->bits.sprite_pattern_table;
        ppu->state.vram.increment    = reg->bits.increment_by_32 ? 32 : 1;
        ppu->nmi_on_vblank     = reg->bits.nmi_on_vblank;

        //INFO("----------------------------------------\n");
        INFO("PPU Ctrl 1 <= %04Xh @ %3d\n", data, ppu->scanline);
        LOG("NMI on VBLANK:    %d\n", ppu->nmi_on_vblank);
        LOG("Sprite size:      8x%d\n", ppu->sprite_height_16 ? 16 : 8);
        INFO("BG  Pattern Tbl*: %d\n", ppu->state.vram.S);
        INFO("Spr Pattern Tbl:  %d\n", ppu->spr_pattern_table);
        INFO("Name Tbl*:        %d\n", PPU_NAME_TABLE(ppu->state.vram.T));
        INFO("VRAM Increment:   %d\n", ppu->state.vram.increment);
    }
}

void
nes_ppu_write_control2(NESPPU_t *ppu, uint8_t data)
{
    PPUControl2Reg_t *reg = &ppu->state.control2;

    if(reg->word != data)
    {
        reg->word = data;

        // FIXME: decoding these bits is probably a waste of time
        ppu->background_color    = reg->bits.background_color;
        ppu->sprites_visible     = reg->bits.sprites_visible;
        ppu->background_visible  = reg->bits.background_visible;
        ppu->sprite_clipping     = ! reg->bits.sprite_clipping;
        ppu->background_clipping = ! reg->bits.background_clipping;
        ppu->monochrome          = reg->bits.monochrome;

        ppu->background_black = (ppu->background_color == 0);
        ppu->background_red   = (ppu->background_color == (1<<0));
        ppu->background_green = (ppu->background_color == (1<<1));
        ppu->background_blue  = (ppu->background_color == (1<<2));

        INFO("----------------------------------------\n");
        INFO("PPU Ctrl 2 @ SL %d, cycle: %" PRIu64 " <= %04Xh\n",
             ppu->scanline, ppu->cpu->cycle, data);
        LOG("BG Color:         %s\n",
            ppu->background_black ? "black" :
            ppu->background_red ? "red" :
            ppu->background_green ? "green" :
            ppu->background_blue ? "blue" : "???");
        LOG("Sprites Visible:  %d\n", ppu->sprites_visible);
        LOG("BG Visible:       %d\n", ppu->background_visible);
        LOG("Sprite Clipping:  %d\n", ppu->sprite_clipping);
        LOG("BG Clipping:      %d\n", ppu->background_clipping);
        LOG("Monochrome:       %d\n", ppu->monochrome);
    }
}

void
nes_ppu_reg_write(NESPPU_t *ppu, uint16_t addr, uint8_t data)
{
    switch(addr & 0x7)
    {
        case 0x0: // $2000
            nes_ppu_write_control1(ppu, data);
            break;

        case 0x1: // $2001
            nes_ppu_write_control2(ppu, data);
            break;

        case 0x2: // $2002
            // FIXME: warning?
            LOG("PPU[2002h] is read-only <= %02Xh\n", data);
            break;

        case 0x3: // $2003
            ppu->state.spr_ram_address = data;
            break;

        case 0x4: // $2004
            ppu->state.spr_ram.ram[ppu->state.spr_ram_address & 0xff] = data;
            LOG("PPU SPR-RAM[%02Xh] <= %02Xh\n",
                      ppu->state.spr_ram_address, data);
            if(! ppu->in_vblank && (ppu->sprites_visible || ppu->background_visible))
            {
                ASSERT(0, "SPR-RAM write outside of vblank: %04Xh!\n", ppu->state.spr_ram_address);
            }

            ppu->state.spr_ram_address++;
            break;

        case 0x5: // $2005
            if(ppu->state.vram.first_write)
            {
                ppu->state.vram.T = (ppu->state.vram.T & 0x7fe0) | (data >> 3);
                ppu->state.vram.X = data & 0x07;
                ppu->state.vram.first_write = 0;
            }
            else
            {
                ppu->state.vram.T = (ppu->state.vram.T & 0x0c1f) | ((data & 0xf8) << 2) | ((data & 0x7) << 12);
                ppu->state.vram.first_write = 1;
            }

            LOG("PPU VRAM[%04Xh] pan = %04Xh\n", addr, ppu->state.vram.T);
            break;

        case 0x6: // $2006
            if(ppu->state.vram.first_write)
            {
                ppu->state.vram.T = (ppu->state.vram.T & 0x00ff) | ((data & 0x3f) << 8);
                ppu->state.vram.first_write = 0;
            }
            else
            {
                ppu->state.vram.T = (ppu->state.vram.T & 0x7F00) | data;
                ppu_latch(ppu);

                ppu->state.vram.first_write = 1;
            }

            INFO("PPU VRAM[%04Xh] address = %04Xh @ %d\n", addr, ppu->state.vram.V, ppu->scanline);
            break;

        case 0x7: // $2007
            nes_ppu_write_vram(ppu, data);
            break;
    }
}

void
nes_ppu_select_vrom_bank(NESPPU_t *ppu, unsigned vrom_dest, unsigned vrom_src, unsigned size_kb)
{
    unsigned i;

    ASSERT((vrom_dest + 1) * size_kb <= 8, "Bad bank select dest: %d\n", vrom_dest);

    if(vrom_src <= 4 && vrom_src >= ppu->num_vrom_banks * (8 / size_kb))
    {
        // This is only for carts with no VROM (ie dragon-warrior4)
        return;
    }

    ASSERT(vrom_src < ppu->num_vrom_banks * (8 / size_kb), "Bad VROM bank: %d\n", vrom_src);
    ppu->state.bank_mapping[vrom_dest] = vrom_src;

    for(i = 0; i < size_kb; i++)
    {
        ppu->bank[vrom_dest * size_kb + i] = ppu->vrom_banks + ((vrom_src * size_kb) + i) * 1024;
    }

    // FIXME: only mark dirty if the vrom changed
    ppu->dirty = 1;

    INFO("Copied VROM %dKB bank %2d to PPU bank %d @ %4d / %d\n",
         size_kb, vrom_src, vrom_dest, ppu->frame_count, ppu->scanline);
}

void
nes_ppu_restore(NESPPU_t *ppu)
{
    unsigned i;
    for(i = 0; i < 8 /* FIXME: better constant */; i++)
    {
        int bank = ppu->state.bank_mapping[i];
        if(bank >= 0)
        {
            nes_ppu_select_vrom_bank(ppu, i, bank, 1);
        }
        else
        {
            ppu->bank[i] = ppu->state.builtin_bank[i];
        }
    }

    ppu->dirty = 1;
}

static void
sprite0_trigger(void *p)
{
    NESPPU_t *ppu = (NESPPU_t *) p;

    INFO("Triggering sprite0 hit via callback: %" PRIu64 "\n", ppu->cpu->cycle);
    ppu->status.bits.sprite0_collision = 1;
}

#ifdef OLDPPU

void
nes_ppu_check_sprite0_collision(NESPPU_t *ppu)
{
    if(ppu->scanline >= NES_PPU_VERTICAL_RESET)
    {
        const uint8_t row = ppu->scanline - NES_PPU_VERTICAL_RESET;
        const NESPPUSprite_t *sprite0 = &(ppu->state.spr_ram.sprites[0]);
        const uint8_t y_coord = sprite0->y_coord_minus_1 + NES_PPU_SPRITE_YOFFSET;
        const uint8_t pattern_height = ppu->sprite_height_16 ? 16 : 8;
        int sprite_pattern_table = ppu->spr_pattern_table;

        ppu->sprite0.index = sprite0->tile_index;

        if(ppu->background_visible &&
           ppu->sprites_visible &&
           (! ppu->sprite0.hit) &&
           PPU_SPRITE_VISIBLE(ppu, sprite0) &&
           (row >= y_coord) &&
           (row < y_coord + pattern_height)
            )
        {
            const uint8_t flip_v = sprite0->attributes & SPRITE_FLIP_V;
            const uint8_t ypos = row - y_coord;
            const uint8_t Y_s = flip_v ? pattern_height - 1 - ypos  : ypos;
            const uint8_t *sprite_ptr;
            uint8_t sprite_line;

            if(ppu->sprite_height_16)
            {
                sprite_ptr = ppu->bank[(sprite0->tile_index & 1) * 4] +
                    (sprite0->tile_index & ~1) * PPU_PATTERN_SIZE;
                if(ypos >= PATTERN_HEIGHT)
                {
                    sprite_ptr += PATTERN_HEIGHT + Y_s;
                }
                else
                {
                    sprite_ptr += Y_s;
                }
            }
            else
            {
                sprite_ptr = ppu->bank[sprite_pattern_table * 4] +
                    sprite0->tile_index * PPU_PATTERN_SIZE + Y_s;
            }

            sprite_line = sprite_ptr[0] | sprite_ptr[PATTERN_HEIGHT];

            if(sprite_line)
            {
                const uint8_t flip_h = sprite0->attributes & SPRITE_FLIP_H;
                const uint8_t *background = &ppu->gui.nes_screen[NES_WIDTH * row + sprite0->x_coord];
                int x;

                if(ppu->options.force_sprite0)
                {
                    sprite0_trigger(ppu);
                }

                for(x = 0; x < PATTERN_WIDTH; x++)
                {
                    const uint8_t X_s = flip_h ? x : 7 - x;
                    int x_c = sprite0->x_coord + x;
                    int sprite_clip_right = ppu->options.sprite_clip_right;

                    if(
                        // BG/sprite left clipping
                        ! ((ppu->background_clipping || ppu->sprite_clipping) && x_c < 8) &&
                        ! (x_c >= (NES_WIDTH - sprite_clip_right)) &&
                        (*background != 0) &&
                        ((sprite_line >> X_s) & 1)
                        )
                    {
                        ppu->sprite0.x = x_c;
                        ppu->sprite0.y = row;

                        ppu->sprite0.hit = 1;

                        if(ppu->sprite0.x == 0)
                        {
                            // Sprite is at leftmost point => trigger immediately
                            sprite0_trigger(ppu);
                        }
                        else
                        {
                            // Set up a future trigger for cycle-accurate sprite0 timing
                            ppu->cpu->trigger = sprite0_trigger;
                            ppu->cpu->trigger_cycle = ppu->cpu->cycle + (ppu->sprite0.x / 3) - 3;
                            ppu->cpu->trigger_ptr = ppu;

                            if(ppu->options.trigger_hack)
                            {
                                // FIXME: double dragon timing is off, so the status bar twitches
                                // THIS IS POSSIBLY RELATED TO SCREEN EXTRA CYCLES
                                ppu->cpu->trigger_cycle -= 4;
                            }
                        }

#if 0
                        printf("Frame %4d: Sprite0 hit #%d @ (%d,%d), s0 @ [%d,%d], flip: h=%d, v=%d, %02Xh, clip: %d %d\n",
                               ppu->frame_count, sprite0->tile_index,
                               ppu->sprite0_x, ppu->sprite0_y,
                               sprite0->x_coord, sprite0->y_coord_minus_1,
                               flip_h, flip_v, ppu->status.word,
                               ppu->sprite_clipping, ppu->background_clipping);
#endif
                        return;
                    }
                    background++;
                }
            }
        }

#if 0
        if(ppu->status.bits.sprite0_collision == 0)
        {
            NOTIFY("Frame %4d: Sprite0 miss #%d @ [%d,%d], visible: %d %d, clip: %d %d\n",
                   ppu->frame_count, sprite0->tile_index,
                   sprite0->x_coord, sprite0->y_coord_minus_1,
                   ppu->background_visible, ppu->sprites_visible,
                   ppu->sprite_clipping, ppu->background_clipping);
        }
#endif
    }
}

static void
nes_ppu_update_pattern_cache(NESPPU_t *ppu)
{
    unsigned table;
    for(table = 0; table <= 1; table++)
    {
        unsigned i;
        uint8_t *pattern_cache = ppu->pattern_cache[table][0];
        uint8_t *pattern_dirty = ppu->pattern_dirty[table];
        uint8_t *sprite = ppu->bank[0];

        for(i = 0; i < NUM_PATTERNS_PER_TABLE; i++)
        {
            unsigned x, y;
            if(i % 64 == 0)
            {
                sprite = ppu->bank[table * 4 + i / 64];
            }

            if(ppu->dirty || *pattern_dirty || 1)
            {
                *pattern_dirty = 0;

                for(y = 0; y < PATTERN_HEIGHT; y++)
                {
                    uint8_t line0 = sprite[0];
                    uint8_t line1 = sprite[PATTERN_HEIGHT];

                    for(x = 0; x < PATTERN_WIDTH; x++)
                    {
                        *pattern_cache++ = ((line1 & 0x80) >> 6) | ((line0 /*& 0x80*/) >> 7);
                        line0 <<= 1;
                        line1 <<= 1;
                    }
                    sprite++;
                }
            }
            else
            {
                pattern_cache += CACHED_PATTERN_SIZE;
                sprite += PATTERN_HEIGHT;
            }

            pattern_dirty++;
            sprite += PATTERN_HEIGHT;
        }
    }

    ppu->dirty = 0;
}

static inline void
nes_ppu_render_pattern8_cached(uint8_t *pixels,
                               unsigned stride,
                               unsigned offset_x,
                               uint8_t *sprite_ptr, uint8_t palette_offset,
                               uint16_t X, uint8_t Y, uint8_t sprite_height_16,
                               uint8_t upper_color,
                               uint8_t flip_h, uint8_t flip_v,
                               uint8_t priority,
                               uint8_t is_bg)
{
    uint8_t *p = &pixels[Y * stride + X + offset_x];

    const int8_t y_offset       = flip_v ? -16 : 0;
    const int8_t x_increment    = flip_h ? -1 : 1;
    const int8_t x_offset       = flip_h ? 16 : 0;
    const int8_t sprite_offset  = x_offset + y_offset;
    const uint8_t sprite_height = PATTERN_HEIGHT << sprite_height_16;

    uint8_t x, y;

    if(flip_h)
    {
        sprite_ptr += PATTERN_WIDTH - 1;
    }
    if(flip_v)
    {
        sprite_ptr += (CACHED_PATTERN_SIZE << sprite_height_16) - PATTERN_WIDTH;
    }

    upper_color = palette_offset | (upper_color << 2);

    for(y = 0; y < sprite_height; y++)
    {
        if(Y + y >= NES_HEIGHT)
            break;

        for(x = 0; x < PATTERN_WIDTH; x++)
        {
            uint8_t palette_entry = *sprite_ptr;
            sprite_ptr += x_increment;

            if(X + x < NES_WIDTH)
            {
                // Only render non-transparent pixels
                if(palette_entry > 0)
                {
                    if(priority || *p == 0)
                    {
                        *p = upper_color | palette_entry;
                    }
                }
                else if(is_bg)
                {
                    *p = 0;
                }
            }
            p++;
        }

        p += stride - x;
        sprite_ptr += sprite_offset;
    }
}

static inline void
nes_ppu_render_pattern16_cached(NESPPU_t *ppu,
                                DisplayPixel_t *pixels,
                                unsigned stride,
                                unsigned offset_x,
                                uint8_t *sprite_ptr, uint8_t palette_offset,
                                uint8_t *palette,
                                uint16_t X, uint8_t Y, uint8_t sprite_height_16,
                                uint8_t upper_color,
                                uint8_t flip_h, uint8_t flip_v)
{
#define TEMP_STRIDE 8

    const int sprite_height = PATTERN_HEIGHT << sprite_height_16;
    uint8_t temp_pixels[TEMP_STRIDE * 16] = {0};
    uint8_t *temp_ptr = temp_pixels;
    DisplayPixel_t *p = &pixels[Y * stride + X + offset_x];
    int x, y;

    nes_ppu_render_pattern8_cached(temp_pixels, TEMP_STRIDE,
                                   0,
                                   sprite_ptr, palette_offset,
                                   0, 0, sprite_height_16,
                                   upper_color, flip_h, flip_v,
                                   0, 0);

    // FIXME: clipping rect
    for(y = 0; y < sprite_height; y++)
    {
        for(x = 0; x < PATTERN_WIDTH; x++)
        {
            uint8_t pixel = *temp_ptr++;
            if(pixel)
            {
                pixel |= upper_color;
            }

            *p++ = ppu->gui.screen_palette[palette[pixel]];
        }

        p += stride - PATTERN_WIDTH;
    }
}

#else
#error NOT IMPL
#endif

// --------------------------------------------------------------------------------
#define SEPARATOR_SIZE 2

static void
nes_ppu_sprites_window_draw(Display_t *display, Window_t *window, DisplayPixel_t *origin, int stride, Rect_t *clip)
{
    NESPPU_t *ppu = (NESPPU_t *) window->p;
    int i;
    int y = SEPARATOR_SIZE;
    int x = 0;
    int sprite_pattern_table = ppu->spr_pattern_table;

    (void) clip; // FIXME: Need to clip

    for(i = 0; i < NUM_SPRITES; i++)
    {
        NESPPUSprite_t *sprite = &ppu->state.spr_ram.sprites[i];
        uint8_t upper_color = sprite->attributes & SPRITE_PALETTE;
        uint8_t flip_h = sprite->attributes & SPRITE_FLIP_H;
        uint8_t flip_v = sprite->attributes & SPRITE_FLIP_V;
        uint8_t *sprite_ptr;

        if(ppu->sprite_height_16)
        {
            sprite_ptr = ppu->pattern_cache[sprite->tile_index & 1][sprite->tile_index & ~1];
        }
        else
        {
            sprite_ptr = ppu->pattern_cache[sprite_pattern_table][sprite->tile_index];
        }

        nes_ppu_render_pattern16_cached(ppu,
                                        origin,
                                        stride,
                                        0,
                                        sprite_ptr, PALETTE_SIZE,
                                        ppu->state.bank2.map.image_palette,
                                        x, y, ppu->sprite_height_16,
                                        upper_color,
                                        flip_h, flip_v);

        x += (PATTERN_WIDTH + SEPARATOR_SIZE);

        if((i & 7) == 7)
        {
            x = 0;
            y += ((PATTERN_HEIGHT << ppu->sprite_height_16) + SEPARATOR_SIZE);
        }
    }
}

static void
nes_ppu_sprites_window_init(NESPPU_t *ppu)
{
    Window_t *window = &ppu->gui.sprites_window;
    window->title = "Sprites";
    window->width = 90;
    window->height = 160;
    window->p = ppu;
    window->draw = nes_ppu_sprites_window_draw;
    window->x = 180;
    window->y = 410;
    window_init(window);

    display_add_window(ppu->display, window);
}

// --------------------------------------------------------------------------------

static void
nes_ppu_background_window_draw(Display_t *display, Window_t *window, DisplayPixel_t *origin, int stride, Rect_t *clip)
{
    // FIXME: merge with nes_ppu_sprites_window_draw

    NESPPU_t *ppu = (NESPPU_t *) window->p;
    int y = SEPARATOR_SIZE;
    int x = 0;
    int pattern;

    (void) clip; // FIXME: need to clip

    for(pattern = 0; pattern < NUM_PATTERNS_PER_TABLE; pattern++)
    {
        uint8_t *sprite_ptr = ppu->pattern_cache[ppu->bg_pattern_table][pattern];

        nes_ppu_render_pattern16_cached(ppu,
                                        origin,
                                        stride,
                                        0,
                                        sprite_ptr, PALETTE_SIZE,
                                        ppu->state.bank2.map.image_palette,
                                        x, y, 0,
                                        0,
                                        0, 0);

        x += (PATTERN_WIDTH + SEPARATOR_SIZE);

        if(pattern % 16 == 15)
        {
            x = 0;
            y += (PATTERN_HEIGHT + SEPARATOR_SIZE);
        }
    }
}

static void
nes_ppu_background_window_init(NESPPU_t *ppu)
{
    Window_t *window = &ppu->gui.background_window;
    window->title = "Background";
    window->width = 170;
    window->height = 162;
    window->p = ppu;
    window->draw = nes_ppu_background_window_draw;
    window->x = 0;
    window->y = 410;;
    window_init(window);

    display_add_window(ppu->display, window);
}

// --------------------------------------------------------------------------------

static inline void
_nes_ppu_render_background(NESPPU_t *ppu, unsigned y)
{
    LOG("PPU render background: %d\n", y);

    if(ppu->background_visible || ppu->sprites_visible)
    {
        if(y == 0 || ppu->dirty)
        {
            nes_ppu_update_pattern_cache(ppu);
        }
    }

    // Render the background
    if(ppu->background_visible)
    {
        NESPPUMemoryMap_t *ppu_memory = &ppu->state.bank2.map;
        unsigned table;

        for(table = 0; table <= 1; table++)
        {
            NameAttributeTable_t *na_table = &ppu_memory->na_tables[table];
            uint8_t attribute_index;
            unsigned table_x = table * NES_WIDTH;

            uint16_t x;

            attribute_index = (y / ATTRIBUTE_HEIGHT) * (NES_WIDTH / ATTRIBUTE_WIDTH);

            attribute_index--;
            for(x = 0; x < NES_WIDTH; x += 8)
            {
                if((x % ATTRIBUTE_WIDTH) == 0)
                {
                    attribute_index++;
                }

                uint8_t attribute = na_table->attribute[attribute_index];

                if((x & 31) >= 16)
                    attribute >>= 2;
                if((y & 31) >= 16)
                    attribute >>= 4;

                uint16_t sprite_offset = y * 4 + (x / 8);
                uint8_t sprite_num = na_table->name[sprite_offset];
                uint8_t *sprite_ptr = ppu->pattern_cache[ppu->bg_pattern_table][sprite_num];

                nes_ppu_render_pattern8_cached(ppu->gui.nes_background,
                                               BACKGROUND_WIDTH,
                                               table_x,
                                               sprite_ptr, 0,
                                               x, y, 0,
                                               attribute & 0x3,
                                               0,
                                               0,
                                               1,
                                               1);
            }
        }
    }
}

void
nes_ppu_render_foreground(NESPPU_t *ppu)
{
    // Render the foreground sprites
    if(ppu->sprites_visible && ! ppu->options.hide_sprites)
    {
        int sprite_num = NUM_SPRITES;
        NESPPUSprite_t *sprite = &ppu->state.spr_ram.sprites[NUM_SPRITES - 1];
        int sprite_pattern_table = ppu->spr_pattern_table;

        LOG("Rendering sprites\n");

        do
        {
            if(PPU_SPRITE_VISIBLE(ppu, sprite)
               // FIXME: this should take into account hflip and x
               && ! (ppu->sprite_clipping && sprite->x_coord <= 1)
                )
            {
                uint8_t upper_color = sprite->attributes & SPRITE_PALETTE;
                uint8_t flip_h = sprite->attributes & SPRITE_FLIP_H;
                uint8_t flip_v = sprite->attributes & SPRITE_FLIP_V;
                uint8_t priority = (sprite->attributes & SPRITE_PRIORITY) == 0;

                uint8_t *sprite_ptr;

                if(ppu->sprite_height_16)
                {
                    sprite_ptr = ppu->pattern_cache[sprite->tile_index & 1][sprite->tile_index & ~1];
                }
                else
                {
                    sprite_ptr = ppu->pattern_cache[sprite_pattern_table][sprite->tile_index];
                }

                if(ppu->options.sprite0_negative && sprite_num == 1)
                    upper_color = ~upper_color;

                nes_ppu_render_pattern8_cached(ppu->gui.nes_screen,
                                               NES_WIDTH,
                                               0,
                                               sprite_ptr, PALETTE_SIZE,
                                               sprite->x_coord, sprite->y_coord_minus_1 + NES_PPU_SPRITE_YOFFSET, ppu->sprite_height_16,
                                               upper_color,
                                               flip_h, flip_v,
                                               priority,
                                               0);
            }
            --sprite;
        } while(--sprite_num > 0);
    }
}

void
nes_ppu_render_scanline(NESPPU_t *ppu)
{
    if(ppu->scanline >= NES_PPU_VERTICAL_RESET)
    {
        unsigned line = ppu->scanline - NES_PPU_VERTICAL_RESET;
        int clock_vram = ppu->background_visible || ppu->sprites_visible;

        if(line >= NES_HEIGHT)
            return;

        if(clock_vram)
        {
            if(line == 0)
            {
                LOG("Loopy frame start latch\n");
                ppu_latch(ppu);
                ppu->state.vram.first_write = 1;
            }
            else
            {
                LOG("Loopy scanline latch\n");
                ppu->bg_pattern_table = ppu->state.vram.S;
                ppu->state.vram.V = (ppu->state.vram.V & 0xfbe0) | (ppu->state.vram.T & 0x041f);
            }
        }

        int scroll_x = (int) ((ppu->state.vram.V & 0x1f) << 3) | ppu->state.vram.X;
        int scroll_y = (int) (((ppu->state.vram.V >> 5) & 0x1f) << 3) | (ppu->state.vram.V >> 12);
        int v = (ppu->state.vram.V >> 11) & 1;
        int h = (ppu->state.vram.V >> 10) & 1;

        switch(ppu->state.mirroring)
        {
            case MirrorHorizontal:
                if(v)
                    scroll_y |= 256;
                break;

            case MirrorVertical:
                if(h)
                    scroll_x |= 256;
                break;

            default:
                break;
        }

        if((line % PATTERN_HEIGHT) == 0)
        {
            _nes_ppu_render_background(ppu, line);
        }

        if(! ppu->background_visible)
        {
            memset(&ppu->gui.nes_screen[NES_WIDTH * line], 0, BACKGROUND_WIDTH);
            return;
        }

#if 0
        {
            static int last_scroll_x = 0;
            static int last_scroll_y = 0;
            static PPUMirroring_t last_mirroring = 0;
            static int last_bg_table = 0;
            static int last_sprite_height = 0;
            static int last_one_screen = 0;

            if(scroll_x != last_scroll_x || scroll_y != last_scroll_y ||
               ppu->mirroring != last_mirroring ||
               ppu->bg_pattern_table != last_bg_table ||
               ppu->sprite_height_16 != last_sprite_height ||
               ppu->one_screen != last_one_screen)
            {
                last_scroll_x = scroll_x;
                last_scroll_y = scroll_y;
                last_mirroring = ppu->mirroring;
                last_bg_table = ppu->bg_pattern_table;
                last_sprite_height = ppu->sprite_height_16;
                last_one_screen = ppu->one_screen;

                NOTIFY("scroll: %3d %3d tbl=%d %s %d @ %d\n",
                       scroll_x, scroll_y,
                       ppu->bg_pattern_table,
                       PPU_MIRRORING_STR[ppu->mirroring],
                       8 << ppu->sprite_height_16,
                       ppu->scanline);
            }
        }
#endif

        int src_line = scroll_y++;

        if((scroll_y & 255) == 240)
        {
            // Vertical offsets 240-255 (aka Tile Rows 30-31) will cause garbage Tile numbers to
            // be fetched from the Attribute Table (instead of from Name Table), after line 255 it
            // will wrap to line 0, but without producing a carry-out to the Name Table Address.
            scroll_y += 16;
            v = !v;
        }

        if(clock_vram)
        {
            ppu->state.vram.V =
                (ppu->state.vram.V & 0x041f) |
                ((scroll_y & 7) << 12) |
                (((scroll_y >> 3) & 0x1f) << 5);

            if(v)
            {
                ppu->state.vram.V |= (1 << 11);
            }
            if(h)
            {
                ppu->state.vram.V |= (1 << 10);
            }
        }

        scroll_y &= 511;

        int offset_x = 0;
        int len;

        switch(ppu->state.mirroring)
        {
            case MirrorVertical:
                // Vertical mirroring (horizontal scrolling ROMs)

                if(src_line >= 256)
                {
                    src_line -= 256;
                    scroll_x += 256;
                }

                src_line &= 255;
                //scroll_x &= 511;

                if(src_line >= 240)
                {
                    // FIXME: See wavy-stretch-demo.nes to debug this
                    printf("wat: %d <= %d!\n", line, src_line);
                    src_line = 240;
                }

                len = (BACKGROUND_WIDTH - scroll_x);
                break;

            case MirrorHorizontal:
                // Horizontal mirroring (vertical scrolling ROMs)

                //scroll_x &= 255;

                if(src_line >= 256)
                {
                    scroll_x += 256;
                }
                src_line &= 255;

                scroll_x &= 511;

                if(src_line >= 240)
                {
                    INFO("clamping: %d %d\n", src_line, scroll_x);
                    //src_line -= 240;
                    src_line = 0;
                }

                offset_x = (scroll_x & 256);

                len = NES_WIDTH - (scroll_x & 255);
                break;

            case Mirror1ScreenB:
                offset_x = 256;
                scroll_x &= 255;
                scroll_x |= 256;
                // fallthrough
            case Mirror1ScreenA:
            default:
                len = NES_WIDTH - (scroll_x & 255);
                break;
        }

        if(len > NES_WIDTH)
            len = NES_WIDTH;

        memcpy(&ppu->gui.nes_screen[NES_WIDTH * line],
               &ppu->gui.nes_background[BACKGROUND_WIDTH * src_line + scroll_x],
               len);

        if(len < NES_WIDTH)
        {
            memcpy(&ppu->gui.nes_screen[NES_WIDTH * line + len],
                   &ppu->gui.nes_background[BACKGROUND_WIDTH * src_line + offset_x],
                   (NES_WIDTH - len));
        }

        if(ppu->background_clipping)
        {
            // Clip the left 8 BG pixels with the transparent color
            memset(&ppu->gui.nes_screen[NES_WIDTH * line], 0, 8);
        }

        nes_ppu_check_sprite0_collision(ppu);

        if(line == (NES_HEIGHT - 1))
        {
            // FIXME: force re-render if cache dirty
            nes_ppu_render_foreground(ppu);
        }

        if(ppu->options.sync_every_nth_scanline > 0 && (line % ppu->options.sync_every_nth_scanline) == 0)
        {
            // FIXME: enable this when the foreground sprites are rendered inline
            //input_delay(ppu->delay_ms);
            //display_render(gui.nes_screen);
        }
    }

    //ppu->scanline_start_ppu_cycle += PPU_CYCLES_PER_SCANLINE;
}

void
nes_ppu_update_status(NESPPU_t *ppu)
{
    if(ppu->scanline == NES_PPU_VBLANK)
    {
        ppu->in_vblank = 1;
        ppu->status.bits.vblank = 1;
        ppu->state.spr_ram_address = 0;
    }
    else if(ppu->scanline == NES_PPU_VERTICAL_RESET)
    {
        ppu->in_vblank = 0;
        ppu->status.bits.scanline_sprite_count = 0;
        ppu->status.bits.sprite0_collision = 0;
        ppu->sprite0.hit = 0;

        ppu->sprite0.x = -1;
        ppu->sprite0.y = -1;

        INFO("VBLANK off @ scanline %3d, cycle %" PRIu64 "\n", ppu->scanline, ppu->cpu->cycle);
        ppu->status.bits.vblank = 0;
    }
}
