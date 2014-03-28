#ifndef __nes_h__
#define __nes_h__

#include "n6502.h"
#include "ines.h"
#include "nes_ppu.h"
#include "nes_apu.h"
#include "nes_rom_chooser.h"
#include "status_overlay.h"

#define PRG_ROM_BANK_SIZE (16*1024)

#define NES_DMA_CYCLES 512

// http://wiki.nesdev.com/w/index.php/PPU_ntsc_pal_difference

// NTSC NES: 1.78977267 MHz
// PAL  NES: 1.662607   MHz
#define NES_NTSC_CYCLES_PER_SECOND 1789773
#define NES_PAL_CYCLES_PER_SECOND  1662607

// FIXME: cycles per scanline either needs to be more accurate or needs to be offset
//        once per frame
// Should be ~29780.67
#define NES_NTSC_PPU_CYCLES_PER_FRAME    29781
#define NES_NTSC_PPU_CYCLES_PER_SCANLINE 113

#define NES_WORK_RAM_SIZE 0x0800
#define NES_SRAM_SIZE     0x2000

#define NOTIFY_NES(NES, FMT, ...) \
    { \
        char __MSG[128]; \
        sprintf(__MSG, FMT, __VA_ARGS__); \
        nes_notify_status(NES, __MSG);  \
    }

/*
CPU Memory Map (16bit buswidth, 0-FFFFh)
  0000h-07FFh   Internal 2K Work RAM (mirrored to 800h-1FFFh)
  2000h-2007h   Internal PPU Registers (mirrored to 2008h-3FFFh)
  4000h-4017h   Internal APU Registers
  4018h-5FFFh   Cartridge Expansion Area almost 8K
  6000h-7FFFh   Cartridge SRAM Area 8K
  8000h-FFFFh   Cartridge PRG-ROM Area 32K

*/

typedef struct
{
    Window_t cpu_window;
    Window_t apu_window;
    Window_t mem_window;
    StatusOverlay_t status_overlay;
    Display_t display;
    NESRomChooser_t chooser;
} NESGUI_t;

typedef struct _NES_t
{
    N6502_t cpu;

    const char *rom_path;

    struct
    {
        struct
        {
            uint8_t up;
            uint8_t down;
            uint8_t left;
            uint8_t right;
            uint8_t select;
            uint8_t start;
            uint8_t a;
            uint8_t b;

            struct
            {
                uint8_t up;
                uint8_t down;
                uint8_t left;
                uint8_t right;
            } mask;
        } key[2];

        int mousex;
        int mousey;

        int mousedown;
    } input;

    struct
    {
        uint8_t ram[NES_WORK_RAM_SIZE];
        uint8_t sram[NES_SRAM_SIZE];

        uint8_t mapper_state[128];
    } state;

    uint8_t *prg_rom[8];

    NESPPU_t ppu;
    NESAPU_t apu;

    unsigned num_prg_rom_banks;
    uint8_t *prg_rom_banks;
    unsigned mapper_num;

    int trainer;
    int sram;
    int battery_backed_sram;

    uint64_t scanline_start_cycle;

    void    (*cartridge_write)(void *p, uint16_t addr, uint8_t data);
    void    (*prg_rom_write)(struct _NES_t *nes, uint16_t addr, uint8_t data);
    uint8_t (*prg_rom_read) (struct _NES_t *nes, uint16_t addr);

    void *cartridge_pointer;

    uint8_t joypad_latch1;
    uint8_t joypad_data1;

    uint8_t joypad_latch2;
    uint8_t joypad_data2;

    uint8_t paddle_buttondown;

    int64_t frame_cpu_cycle;
    int64_t frame_start_cpu_cycle;
    int64_t frame_surplus_cpu_cycles;

    struct
    {
        unsigned disable_audio;
        unsigned blargg_test;
        uint16_t reset_pc;

        int64_t max_instructions;
        uint32_t max_frames;

        char *override;

        int save_state;
        int restore_state;

        int soft_reset_delay;
        int quit;
        int escape;
    } options;

    const char *next_rom;

    NESGUI_t gui;
} NES_t;

void nes_init(NES_t *nes, int install_memory_map);
void nes_unload(NES_t *nes);
void nes_quit(NES_t *nes);

void nes_hard_reset(NES_t *nes);
void nes_soft_reset(NES_t *nes);

void nes_inspect_rom(iNES_t *ines, const char *rom_path);
void nes_load_rom(NES_t *nes, const char *rom_path);

void nes_run_frame(NES_t *nes);
void nes_render_frame(NES_t *nes);

void nes_pause(NES_t *nes, int paused);
unsigned nes_toggle_pause(NES_t *nes);

void nes_notify_status(NES_t *nes, const char *msg);

void nes_save_state(NES_t *nes, const char *path);
void nes_restore_state(NES_t *nes, const char *path);

#endif
