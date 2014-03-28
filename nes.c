#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include "nes.h"
#include "input.h"
#include "log.h"
#include "nes_mapper.h"

// TEST ROMS:
// http://www.bspquakeeditor.com/users/sort/testroms/
// http://www.slack.net/~ant/nes-tests/
// http://wiki.nesdev.com/w/index.php/Emulator_Tests

/*
  ROM Naming terminology:

  Standard Codes:
	[a] - Alternate
	[b] - Bad Dump
	[BF] - Bung Fix
	[c] - Cracked
	[f] - Other Fix
	[h] - Hack
	[o] - Overdump
	[p] - Pirate
	[t] - Trained
	[T] - Translation
	(Unl) - Unlicensed
	[x] - Bad Checksum
	ZZZ_ - Unclassified
	[!] - Verified Good Dump
	(???k) - ROM Size
*/

#define LOG_NES(...)  _LOG(NES, __VA_ARGS__)
#define INFO_NES(...) _INFO(NES, __VA_ARGS__)

#define LOG_MEM(...)  _LOG(MEM, __VA_ARGS__)
#define INFO_MEM(...) _INFO(MEM, __VA_ARGS__)

#define INFO_MAPPER(...) _INFO(MAPPER, __VA_ARGS__)

#define INFO_PPU(...) _INFO(PPU, __VA_ARGS__)

// FIXME: use an alloc() function that can abort()

#define LOG_READ(...)  { LOG_MEM("READ:  "); LOG_MEM(__VA_ARGS__); }
#define LOG_WRITE(...) { LOG_MEM("WRITE: "); LOG_MEM(__VA_ARGS__); }

#define LOG_PPU(...) _LOG(PPU, __VA_ARGS__)

static NES_t *global_nes = 0;

static const char SRAM_HEADER[4] = {'S', 'R', 'A', 'M'};

typedef struct
{
    char header[sizeof(SRAM_HEADER)];
    uint32_t size;
    uint8_t sram[NES_SRAM_SIZE];
} NESSRAM_t;

static const char *
nes_get_state(void *ptr)
{
    NES_t *nes = (NES_t *) ptr;
    if(! nes)
        return NULL;

    static char msg[32];
    sprintf(msg, "%3d:%3d @ %5" PRIu64,
            nes->ppu.frame_count,
            nes->ppu.scanline,
            (nes->cpu.cycle - nes->frame_start_cpu_cycle));

    return msg;
}

void
nes_save_sram(NES_t *nes)
{
    if(nes->battery_backed_sram)
    {
        char sram_path[128];
        snprintf(sram_path, sizeof(sram_path), "%s.sram", nes->rom_path);
        sram_path[sizeof(sram_path)-1] = 0;
        if(strlen(sram_path) < sizeof(sram_path) - 1)
        {
            FILE *fp = fopen(sram_path, "wb");
            if(fp)
            {
                NESSRAM_t sram;

                memcpy(sram.header, SRAM_HEADER, sizeof(SRAM_HEADER));
                sram.size = NES_SRAM_SIZE;
                memcpy(&sram.sram, nes->state.sram, sram.size);

                fwrite(&sram, sizeof(sram), 1, fp);
                NOTIFY("Wrote SRAM to %s\n", sram_path);
                fclose(fp);

                return;
            }
        }

        NOTIFY("ERROR: Could not write SRAM to %s\n", sram_path);
    }
    else
    {
        INFO_NES("No SRAM marked for this rom\n");
    }
}

void
nes_restore_sram(NES_t *nes)
{
    if(nes->battery_backed_sram)
    {
        char sram_path[128];
        snprintf(sram_path, sizeof(sram_path), "%s.sram", nes->rom_path);
        sram_path[sizeof(sram_path)-1] = 0;
        if(strlen(sram_path) < sizeof(sram_path) - 1)
        {
            FILE *fp = fopen(sram_path, "rb");
            if(fp)
            {
                NESSRAM_t sram;

                int read_size = fread(&sram, sizeof(sram), 1, fp);

                ASSERT(read_size == 1, "Failed reading SRAM from %s\n", sram_path);

                ASSERT(memcmp(sram.header, SRAM_HEADER, sizeof(SRAM_HEADER)) == 0, "Bad SRAM header in %s\n", sram_path);
                ASSERT(sram.size == NES_SRAM_SIZE, "Bad SRAM size\n");
                memcpy(nes->state.sram, &sram.sram, sram.size);

                NOTIFY("Read SRAM from %s\n", sram_path);
                fclose(fp);

                return;
            }
        }

        INFO_NES("Could not read SRAM from %s\n", sram_path);
    }
    else
    {
        INFO_NES("No SRAM marked for this rom\n");
    }
}

void
nes_quit(NES_t *nes)
{
    if(nes->options.blargg_test)
    {
        NOTIFY("ABORTED from Blargg test\n");
        exit(1);
    }

    nes_save_sram(nes);

    nes_unload(nes);

    NOTIFY("Quit: %d frames\n", nes->ppu.frame_count);
    if(! nes->options.disable_audio)
        nes_apu_destroy(&nes->apu);
    display_destroy(nes->ppu.display);
    nes_rom_chooser_destroy(&nes->gui.chooser);
}

// --------------------------------------------------------------------------------

static void
nes_spr_ram_dma(NES_t *nes, uint16_t src_addr)
{
    NESPPU_t *ppu = &nes->ppu;
    int i = 0;

    if(! ppu->in_vblank && (ppu->sprites_visible || ppu->background_visible))
    {
        ASSERT(0, "DMA outside of vblank: %04Xh!\n", src_addr);
    }

    nes->cpu.cycle += NES_DMA_CYCLES;

    LOG_WRITE("VRAM Sprite DMA Transfer: PC=%04Xh, cycle=%" PRIu64 ", SRC= %04X => SPR-RAM[%02Xh]\n",
              nes->cpu.regs.PC - 1, nes->cpu.cycle, src_addr, ppu->state.spr_ram_address);
    for(i = 0; i < 256; i++)
    {
        uint8_t src_byte = nes->state.ram[src_addr & 0x7fff];
        uint8_t *dest = ppu->state.spr_ram.ram + ((ppu->state.spr_ram_address + i) & 0xff);
        LOG_WRITE("DMA SPR-RAM[%02Xh] <= %02Xh\n", i, src_byte);
        *dest = src_byte;
        src_addr++;
    }
}

// --------------------------------------------------------------------------------

static inline void
nes_check_blargg(NES_t *nes, uint16_t offset, uint8_t data)
{
#define BLARGG_OFFSET_STATUS  0x0
#define BLARGG_OFFSET_STR     0x4

    if(nes->options.blargg_test)
    {
        if(offset == BLARGG_OFFSET_STATUS)
        {
            const uint8_t blargg_status = global_nes->state.sram[BLARGG_OFFSET_STATUS];
            NOTIFY("Blargg status: 0x%02X\n", blargg_status);

            if(blargg_status != 0xff &&
               blargg_status != 0x80)
            {
                const char *blargg_str = (const char *) &global_nes->state.sram[BLARGG_OFFSET_STR];
                const int len = strlen(blargg_str);
                if(len > 0)
                {
                    NOTIFY("Blargg message: (len:%d)\n", len);
                    NOTIFY("--------------------\n");
                    NOTIFY("%s\n", blargg_str);
                    NOTIFY("--------------------\n");

                    if(blargg_status == 0x81)
                    {
                        NOTIFY("Forcing Soft Reset\n");
                        nes->options.soft_reset_delay = 5;
                    }
                    else if(blargg_status == 0)
                    {
                        NOTIFY("Blargg PASSED\n");
                        exit(0);
                    }
                    else
                    {
                        NOTIFY("Blargg FAILED: %02X\n", blargg_status);
                        exit(1);
                    }
                }
            }
        }
    }
}

static inline void
nes_check_blargg_work_ram(NES_t *nes, uint16_t addr, uint8_t data)
{
#define BLARGG_ADDR_PPU     0x4015
#define BLARGG_ADDR_STATUS  0x00f8
#define BLARGG_ADDR_STATUS2 0x00f0

    if(nes->options.blargg_test)
    {
        if(addr == BLARGG_ADDR_PPU && data == 0x01)
        {
            const uint16_t addr = (nes->options.blargg_test == 1) ? BLARGG_ADDR_STATUS : BLARGG_ADDR_STATUS2;
            const uint8_t blargg_status = global_nes->state.ram[addr];
            NOTIFY("Blargg work ram status: 0x%02X\n", blargg_status);

            if(blargg_status != 0)
            {
                if(blargg_status == 0x01)
                {
                    NOTIFY("Blargg work ram PASSED\n");
                    exit(0);
                }
                else
                {
                    NOTIFY("Blargg FAILED: %02X\n", blargg_status);
                    exit(1);
                }
            }
        }
    }
}

// --------------------------------------------------------------------------------

static void
nes_write_mem(uint16_t addr, uint8_t data)
{
    switch(addr >> 12)
    {
        // 0000-07FFh   Internal 2K Work RAM (mirrored to 800h-1FFFh)
        case 0x0:
        case 0x1:
            LOG_WRITE("Work RAM[%04Xh] <= %02Xh\n", addr, data);
            global_nes->state.ram[addr & 0x07ff] = data;
            break;

        // 2000h-2007h   Internal PPU Registers (mirrored to 2008h-3FFFh)
        case 0x2:
        case 0x3:
        {
            NESPPU_t *ppu = &global_nes->ppu;

            nes_ppu_reg_write(ppu, addr, data);

            break;
        }

        // 4000h-4017h   Internal APU Registers
        // 4018h-5FFFh   Cartridge Expansion Area almost 8K
        case 0x4:
        case 0x5:
            if(addr < 0x4018)
            {
                switch(addr)
                {
                    case 0x4014:
                        nes_spr_ram_dma(global_nes, data << 8);
                        break;

                    case 0x4016:
                        if(data & 1)
                        {
                            input_latch_joypads(global_nes);
                            INFO_NES("Joypad[%04Xh] capture\n", addr);
                        }
                        else
                        {
                            global_nes->joypad_data1 = global_nes->joypad_latch1;
                            global_nes->joypad_data2 = global_nes->joypad_latch2;
                            INFO_NES("Joypad[%04Xh] latch\n", addr);
                        }

                        break;

                    default:
                        LOG_WRITE("APU[%04Xh] <= %02Xh\n", addr, data);
                        nes_check_blargg_work_ram(global_nes, addr, data);
                        break;
                }

                nes_apu_write(&global_nes->apu, addr, data);
            }
            else
            {
                // FIXME: see Rad Racer
                LOG_WRITE("Cartridge expansion area[%04Xh] <= %02Xh\n", addr, data);
                if(global_nes->cartridge_write)
                {
                    global_nes->cartridge_write(global_nes->cartridge_pointer, addr, data);
                }
            }
            break;

        // 6000h-7FFFh   Cartridge SRAM Area 8K
        case 0x6:
        case 0x7:
            LOG_WRITE("SRAM[%04X] <= %02X, PC @ %04X\n", addr, data, global_nes->cpu.regs.PC - 2);

#define NES_SRAM_BASE 0x6000
            global_nes->state.sram[addr - NES_SRAM_BASE] = data;

            nes_check_blargg(global_nes, addr - NES_SRAM_BASE, data);

            break;

        // 8000h-FFFFh   Cartridge PRG-ROM Area 32K
        default:
            if(global_nes->prg_rom_write)
            {
                global_nes->prg_rom_write(global_nes, addr, data);
            }
            break;

    }
}

static uint8_t
nes_read_mem(uint16_t addr)
{
    uint8_t data = 0;

    switch(addr >> 12)
    {
        // 0000-07FFh   Internal 2K Work RAM (mirrored to 800h-1FFFh)
        case 0x0:
        case 0x1:
            //LOG("READ Work RAM[%04Xh]\n", addr);
            data = global_nes->state.ram[addr & 0x07ff];
            break;

        // 2000h-2007h   Internal PPU Registers (mirrored to 2008h-3FFFh)
        case 0x2:
        case 0x3:
        {
            NESPPU_t *ppu = &global_nes->ppu;

            switch(addr & 0x7)
            {
#if 0
                // FIXME: Can Ctrl1/2 actually be read?
                case 0x0:
                    data = ppu->control1;
                    LOG_READ("PPU Ctrl1[%02Xh] => %02Xh\n", addr, data);
                    break;

                case 0x1:
                    data = ppu->control2;
                    LOG_READ("PPU Ctrl2[%02Xh] => %02Xh\n", addr, data);
                    break;
#endif

                case 0x2: // $2002
                    // Reset the address latch
                    ppu->state.vram.first_write = 1;

                    data = ppu->status.word;
                    ppu->status.bits.vblank = 0;

#if 0
                    printf("Read 2002 @ %04Xh, value=%02Xh, scanline=%d, cycle %" PRIu64 ")\n",
                           global_nes->cpu.regs.PC, data, ppu->scanline, global_nes->cpu.cycle);
#endif

                    LOG_NES("Frame: %d, SL: %d, PPU Status[%02Xh] => %02Xh\n",
                            ppu->frame_count, ppu->scanline, addr, data);
                    break;

                case 0x4: // $2004
                    data = ppu->state.spr_ram.ram[ppu->state.spr_ram_address & 0xff];

                    if(! ppu->in_vblank && (ppu->sprites_visible || ppu->background_visible))
                    {
                        //ASSERT(0, "SPR-RAM read outside of vblank: %04Xh!\n", ppu->state.spr_ram_address);
                    }

                    LOG_READ("PPU SPR-RAM[%02Xh] => %02Xh\n", ppu->state.spr_ram_address, data);
                    break;

                case 0x7: // $2007
                    data = nes_ppu_read_vram(ppu);
                    break;

                default:
                    printf("WARNING: PPU[%02Xh] is write-only\n", addr);
                    break;
            }

            break;
        }

        // 4000h-4017h   Internal APU Registers
        // 4018h-5FFFh   Cartridge Expansion Area almost 8K
        case 0x4:
        case 0x5:
            data = 0xbf;

            if(addr < 0x4018)
            {
                switch(addr)
                {
                    case 0x4014:
                        ASSERT(0, "Sprite DMA Transfer???\n");
                        break;

                    case 0x4016:
/*
7654 3210  Inputs ($4016, $4017)
|||| ||||
|||| |||+- Data from controller 1
|||| ||+-- Data from controller 3 (Famicom) or 0 (NES)
|||| |+--- Expansion controller D2 (Famicom) or 0 (NES)
|||| +---- Expansion controller D3 (e.g. Zapper)
|||+------ Expansion controller D4 (e.g. Zapper)
+++------- D7-D5 of last value on data bus
           (usually 010, from upper bits of $4016)
*/
                        data = global_nes->joypad_data1 & 1;
                        data |= global_nes->paddle_buttondown << 1;
                        data |= 0x40;

                        INFO_NES("Joypad[%04Xh] => %02Xh\n", addr, data);
                        global_nes->joypad_data1 >>= 1;
                        global_nes->joypad_data1 |= 0x80;
                        break;

                    case 0x4017:
                        data = (global_nes->joypad_data2 & 1) << 1;
                        //data |= 0x40;
                        //data = 2;
                        // HACK: disable joypad2
                        LOG_READ("Joypad[%04Xh] => %02Xh\n", addr, data);
                        global_nes->joypad_data2 >>= 1;
                        break;

                    default:
                        return nes_apu_read(&global_nes->apu, addr);
                }
            }
            else
            {
                LOG_READ("Cartridge expansion area: %04Xh\n", addr);
                // FIXME: Rad Racer
                //ASSERT(0, "Cartridge expansion\n");
            }
            break;

        // 6000h-7FFFh   Cartridge SRAM Area 8K
        case 0x6:
        case 0x7:
            data = global_nes->state.sram[addr - 0x6000];
            LOG_READ("Cartridge SRAM: %04Xh => %02Xh\n", addr, data);
            break;

        // 8000h-FFFFh   Cartridge PRG-ROM Area 32K
        default:
        {
            int bank;
            //LOG("READ PRG-ROM[%04Xh]\n", addr);
            addr -= 0x8000;
            bank = (addr >> 12) & 0x7;
            data = global_nes->prg_rom[bank][addr & 0xfff];
            break;
        }
    }

    return data;
}

static void
nes_install_memory_map(NES_t *nes)
{
    nes->cpu.read_mem  = nes_read_mem;
    nes->cpu.write_mem = nes_write_mem;

    global_nes = nes;

    INFO_NES("Installed NES memory map\n");
}

static void ines_dump_cart(NES_t *nes, iNES_t *ines)
{
    NOTIFY("---------------------\n");
    //INFO("File ID:      %c%c%c%x\n", DUMP4(ines->file_id));
    NOTIFY("16K PRG-ROM:  %d\n", ines->num_16k_prg_rom_banks);
    NOTIFY("8K  VROM:     %d\n", ines->num_8k_vrom_banks);
    NOTIFY("8K  PRG-RAM:  %d\n", ines->num_8k_prg_ram_banks);
    NOTIFY("SRAM:         %d/%d\n", nes->sram, nes->battery_backed_sram);
    NOTIFY("Trainer:      %d\n", nes->trainer);
    NOTIFY("Four screen:  %d\n", nes->ppu.state.four_screen);
    NOTIFY("ROM Control:  %x %x\n", ines->rom_control_byte1, ines->rom_control_byte2);
    NOTIFY("Display:      %s\n", nes->ppu.state.pal ? "PAL" : "NTSC");
    NOTIFY("Mirroring:    %s\n", PPU_MIRRORING_STR[nes->ppu.state.mirroring]);
    NOTIFY("Mapper:       %d\n", nes->mapper_num);
    NOTIFY("---------------------\n");
}

static void
nes_load_ines(NES_t *nes, FILE *fp)
{
    iNES_t ines;
    int result;
    unsigned i;

    if(! ines_load(&ines, fp))
    {
        ASSERT(0, "ROM load fail");
    }

    nes->mapper_num = ines_mapper_num(&ines);

    if(ines.num_8k_prg_ram_banks == 0)
        ines.num_8k_prg_ram_banks = 1;

    if(((ines.rom_control_byte2 >> 2) & 3) == 2)
    {
        ASSERT(0, "iNES 2.0\n");
    }

    nes->sram = (ines.flags10 >> 4) & 1;
    nes->battery_backed_sram = (ines.rom_control_byte1 & 2) != 0;
    nes->trainer = (ines.rom_control_byte1 & 4) != 0;
    nes->ppu.state.four_screen = (ines.rom_control_byte1 & 8) != 0;
    nes->ppu.state.pal = (ines.flags9 & 1);
    nes->ppu.state.mirroring = ines.rom_control_byte1 & 1 ? MirrorVertical : MirrorHorizontal;

    ines_dump_cart(nes, &ines);

    ASSERT(ines.num_16k_prg_rom_banks >= 1, "Need at least 1 PRG-ROM bank\n");
    ASSERT(! nes->trainer, "Trainer not impl\n");
    ASSERT(! nes->ppu.state.four_screen, "4-screen not impl\n");
    if(nes->ppu.state.pal)
    {
        fprintf(stderr, "WARNING: PAL not impl\n");
    }

    nes->num_prg_rom_banks = ines.num_16k_prg_rom_banks;
    nes->prg_rom_banks = malloc(ines.num_16k_prg_rom_banks * PRG_ROM_BANK_SIZE);

    for(i = 0; i < ines.num_16k_prg_rom_banks; i++)
    {
        result = fread(nes->prg_rom_banks + i * PRG_ROM_BANK_SIZE, PRG_ROM_BANK_SIZE, 1, fp);
        ASSERT(result == 1, "Bad PRG ROM 16K read: %d\n", i);
    }

    nes->ppu.has_vrom = (ines.num_8k_vrom_banks > 0);
    nes->ppu.num_vrom_banks = ines.num_8k_vrom_banks;

    if(nes->ppu.has_vrom)
    {
        nes->ppu.vrom_banks = malloc(VROM_BANK_SIZE * ines.num_8k_vrom_banks);
    }
    else
    {
        // FIXME: always allocate 1 VROM Bank?
        nes->ppu.vrom_banks = malloc(VROM_BANK_SIZE * 1);
    }

    for(i = 0; i < ines.num_8k_vrom_banks; i++)
    {
        result = fread(nes->ppu.vrom_banks + i * VROM_BANK_SIZE, VROM_BANK_SIZE, 1, fp);
        ASSERT(result == 1, "Bad VROM 8K read: %d\n", i);
    }

    // FIXME: move into mapper init
    if(ines.num_8k_vrom_banks >= 1)
    {
        nes_ppu_select_vrom_bank(&nes->ppu, 0, 0, 4);
        nes_ppu_select_vrom_bank(&nes->ppu, 1, 1, 4);
    }

    nes_mapper_init(nes);

    nes_restore_sram(nes);
}

void
nes_unload(NES_t *nes)
{
    if(nes->prg_rom_banks)
    {
        free(nes->prg_rom_banks);
        nes->prg_rom_banks = NULL;
    }

    if(nes->ppu.vrom_banks)
    {
        free(nes->ppu.vrom_banks);
        nes->ppu.vrom_banks = NULL;
    }

    nes->num_prg_rom_banks = 0;

    NOTIFY("Unloaded %s\n", nes->rom_path);
}

// --------------------------------------------------------------------------------

static void
nes_cpu_window_draw(Display_t *display, Window_t *window, DisplayPixel_t *origin, int stride, Rect_t *clip)
{
    char msg[128];
    char *m = msg;
    NES_t *nes = (NES_t *) window->p;
    int font_y_offset = 1;

    m += sprintf(m, "PC: %04X\n", nes->cpu.regs.PC);
    m += sprintf(m, "A:%02X X:%02X Y:%02X P:%02X SP:%02X\n",
                 nes->cpu.regs.A, nes->cpu.regs.X, nes->cpu.regs.Y,
                 nes->cpu.regs.P.word, nes->cpu.regs.S);

    font_printstr(nes->gui.display.font, (origin + 1 + font_y_offset * stride), stride, msg, clip);
}

static void
nes_cpu_window_init(NES_t *nes)
{
    Window_t *window = &nes->gui.cpu_window;
    window->title = "CPU";
    window->width = 260;
    window->height = 40;
    window->p = nes;
    window->y = 265;
    window->draw = nes_cpu_window_draw;
    window_init(window);

    display_add_window(&nes->gui.display, window);
}

// --------------------------------------------------------------------------------

static void
nes_apu_sine_wave(NES_t *nes, DisplayPixel_t *origin, int stride, Rect_t *clip)
{
#define NUM_VISUALIZATION_SAMPLES 128

#if 0 // this crashes in gdb
    static float values[NUM_VISUALIZATION_SAMPLES] = {0};
    const DisplayPixel_t pixel = display_maprgb(&nes->gui.display, 0xff, 0xff, 0xdd);
    unsigned i;

    for(i = 0; i < NUM_VISUALIZATION_SAMPLES - 1; i++)
    {
        values[i] = values[i + 1];
    }

    values[NUM_VISUALIZATION_SAMPLES - 1] = nes->apu.sample_average;

    for(i = 0; i < sizeof(values); i++)
    {
        int amplitude = (int) (values[i] * 16);

        display_line(origin, 0, 32 - amplitude, 0, 32 + amplitude,
                     pixel,
                     stride, clip);
        origin++;
    }
#endif
}

static void
nes_apu_window_draw(Display_t *display, Window_t *window, DisplayPixel_t *origin, int stride, Rect_t *clip)
{
    NES_t *nes = (NES_t *) window->p;
    NESAPU_t *apu = &nes->apu;
    char msg[256];
    char *m = msg;

    int font_y_offset = 1;

    m += sprintf(m, "Mode:   %d Hz, %s\n",
                 (apu->state.frame_sequencer_mode == MODE_240HZ) ? 240 : 192,
                 apu->state.frame_irq ? "IRQ" : "[-]");

    m += sprintf(m, "Volume: %X %X %X %X %X\n",
                 apu->state.square[0].volume,
                 apu->state.square[1].volume,
                 apu->state.triangle.volume,
                 apu->state.noise.volume,
                 apu->state.dmc.volume);

    m += sprintf(m, "Freq:   %4d %4d %4d %4d %4d\n",
                 apu->state.square[0].cpu_period,
                 apu->state.square[1].cpu_period,
                 apu->state.triangle.cpu_period,
                 apu->state.noise.cpu_period,
                 apu->state.dmc.cpu_period);

    m += sprintf(m, "Length: %3X %3X %3X %3X %3X\n",
                 apu->state.square[0].length_count,
                 apu->state.square[1].length_count,
                 apu->state.triangle.length_count,
                 apu->state.noise.length_count,
                 apu->state.dmc.length_count);

    font_printstr(nes->gui.display.font, (origin + 1 + font_y_offset * stride), stride, msg, clip);
    nes_apu_sine_wave(nes, origin, stride, clip);
}

static void
nes_apu_window_init(NES_t *nes)
{
    Window_t *window = &nes->gui.apu_window;
    window->title = "APU";
    window->width = 260;
    window->height = 80;
    window->p = nes;
    window->y = 30;
    window->draw = nes_apu_window_draw;
    window_init(window);

    display_add_window(&nes->gui.display, window);
}

// --------------------------------------------------------------------------------

static void
nes_mem_window_draw(Display_t *display, Window_t *window, DisplayPixel_t *origin, int stride, Rect_t *clip)
{
    NES_t *nes = (NES_t *) window->p;
    uint8_t *ram = nes->state.ram;
    DisplayPixel_t addr;
    DisplayPixel_t *row = origin;
    DisplayPixel_t *output = row;

    //0000h-07FFh   Internal 2K Work RAM (mirrored to 800h-1FFFh)
    for(addr = 0x0000; addr < 0x0800; addr++)
    {
        // FIXME assumes 16bpp
        DisplayPixel_t pixel;
        uint8_t value = ram[addr];

        if((addr & 63) == 0)
        {
            output = row;
            row += stride * 2;
        }

        // Convert from RGB 332 to RGB 565
        pixel = ((value >> 5) << 13) | (((value >> 2) & 7) << 8) | ((value & 3) << 2);

        output[0] = pixel;
        output[1] = pixel;
        output[stride + 0] = pixel;
        output[stride + 1] = pixel;
        output += 2;
    }
}

static void
nes_mem_window_init(NES_t *nes)
{
    Window_t *window = &nes->gui.mem_window;
    window->title = "Memory";
    window->width = 128;
    window->height = 64;
    window->p = nes;
    window->draw = nes_mem_window_draw;
    window->x = 290;
    window->y = 500;;
    window_init(window);

    display_add_window(&nes->gui.display, window);
}

// --------------------------------------------------------------------------------
static void
nes_rom_select(NESRomChooser_t *chooser, const char *path)
{
    NES_t *nes = (NES_t *) chooser->p;
    INFO_NES("Selecting ROM: %s\n", path);
    nes->next_rom = path;
}

// --------------------------------------------------------------------------------

static void
menu_quit(MenuItem_t *item)
{
    global_nes->options.quit = 1;
}

static void
menu_load(MenuItem_t *item)
{
    NES_t *nes = global_nes; // FIXME: global_nes should be nixed
    display_select_window(&nes->gui.display, &nes->gui.chooser.window);
}

MenuItem_t MENU_WINDOW = {
    .text = "Window",
    .type = mitSubMenu,
};

MenuItem_t MENU_FILE_QUIT = {
    .text = "Quit",
    .type = mitEntry,
    .key_accelerator = 'q',
    .on_select = menu_quit,
};

MenuItem_t MENU_FILE_SAVE = {
    .text = "Save State",
    .type = mitEntry,
    .next = &MENU_FILE_QUIT,
};

MenuItem_t MENU_FILE_LOAD = {
    .text = "Load ROM",
    .type = mitEntry,
    .on_select = menu_load,
    .key_accelerator = 'l',
    .next = &MENU_FILE_SAVE,
};

MenuItem_t MENU_FILE = {
    .text = "File",
    .type = mitSubMenu,
    .next = &MENU_WINDOW,
    .child_menu = &MENU_FILE_LOAD,
};

// --------------------------------------------------------------------------------

static void
nes_gui_init(NES_t *nes)
{
    nes->gui.display.width = nes->ppu.options.display_width;
    nes->gui.display.height = nes->ppu.options.display_height;
    nes->gui.display.depth_in_bytes = 2; // FIXME 16bpp
    nes->gui.display.windowed = nes->ppu.options.display_windowed;

    nes->gui.display.show_cursor = ! nes->ppu.options.enable_paddle;

    display_init(&nes->gui.display, &MENU_FILE);
    status_overlay_init(&nes->gui.status_overlay, &nes->gui.display);

    if(nes->ppu.options.display_windowed)
    {
        nes_cpu_window_init(nes);
        nes_mem_window_init(nes);
    }

    nes->gui.chooser.p = nes;
    nes->gui.chooser.on_select = nes_rom_select;
    nes_rom_chooser_init(&nes->gui.chooser, &nes->gui.display);
    nes->gui.chooser.window.y = 20;
}

static void
nes_ppu_dump_state(void *obj)
{
    NES_t *nes = (NES_t *) obj;
    int scanline;
    int64_t cpu_frame_cycles;
    int64_t ppu_scanline_start_cycle;
    int64_t ppu_scanline_cycle;

    // Convert from "Brad Taylor PPU scanline #" to "nestest scanline #"
    scanline = nes->ppu.scanline - 21;

    if(scanline < -1)
    {
        scanline += 262;
    }

    cpu_frame_cycles = (nes->cpu.cycle - nes->frame_start_cpu_cycle);
    ppu_scanline_start_cycle = nes->ppu.scanline * PPU_CYCLES_PER_SCANLINE;
    ppu_scanline_cycle = cpu_frame_cycles * 3 - ppu_scanline_start_cycle;

#if 0
    NOTIFY("CPUT:%3d CPUF:%3d SL:%3d SLC:%3d PPU:%3d\n",
           (int)nes->cpu.cycle, (int)cpu_frame_cycles, nes->ppu.scanline,
           (int)ppu_scanline_start_cycle, (int)ppu_scanline_cycle);

    if(nes->ppu.scanline >= 200)
        ASSERT(0, "stop");
#endif

    _INFO(6502, "CYC:%3d SL:%3d\n",
          (int16_t) ppu_scanline_cycle,
          scanline);
}

static void
nes_increment_cycles(void *p, int cycles)
{
    NES_t *nes = (NES_t *) p;
    nes->cpu.cycle += cycles;
}

void
nes_init(NES_t *nes, int install_memory_map)
{
    if(install_memory_map)
    {
        nes_install_memory_map(nes);
    }

    nes->cpu.dump_state_func = nes_ppu_dump_state;
    nes->cpu.dump_state_ptr = nes;

    nes_gui_init(nes);

    nes_ppu_init(&nes->ppu, &nes->gui.display, &nes->cpu);

    if(! nes->options.disable_audio)
    {
        nes->apu.read_mem_func = &nes_read_mem;
        nes->apu.increment_cycles_func = &nes_increment_cycles;
        nes->apu.get_state_func = &nes_get_state;
        nes->apu.arg_ptr = nes;
        nes_apu_init(&nes->apu);

        nes_apu_window_init(nes);
    }
}

void
nes_load_rom(NES_t *nes, const char *rom_path)
{
    FILE *fp;

    nes->rom_path = rom_path;

    memset(&nes->state, 0, sizeof(nes->state));

    fp = fopen(rom_path, "rb");
    ASSERT(fp != 0, "Could not open '%s'", rom_path);

    if(1)
    {
        nes_ppu_reset(&nes->ppu);
        nes_load_ines(nes, fp);
    }

    fclose(fp);

    NOTIFY_NES(nes, "Loaded %s [Mapper #%d]", rom_path, nes->mapper_num);

    display_select_window(nes->ppu.display, &nes->ppu.gui.nes_window);

    // Hide the menubar
    nes->gui.display.menubar.visible = 0;
    menubar_deselect(&nes->gui.display.menubar);

    nes_pause(nes, 0); // Unpause
}

void
nes_hard_reset(NES_t *nes)
{
    display_reset(nes->ppu.display);
}

void
nes_soft_reset(NES_t *nes)
{
    nes->frame_surplus_cpu_cycles = 0;

    n6502_reset(&nes->cpu);

    if(! nes->options.disable_audio)
        nes_apu_reset(&nes->apu);
}

void
nes_render_frame(NES_t *nes)
{
    NESPPU_t *ppu = &nes->ppu;
    unsigned desired_frametime_ms;
    unsigned current_time_ms;
    const int ntsc = 1;

    if(ntsc)
    {
        // NTSC is 60FPS, which is 16.67ms / frame
        desired_frametime_ms = (ppu->frame_count % 3) ? 17 : 16;
    }

    if(ppu->last_frame_ms == 0)
    {
        ppu->last_frame_ms = input_time_ms();
    }

    INFO_PPU("NES PPU Render\n");

    input_update(nes);

    current_time_ms = input_time_ms();
    ppu->last_frame_ms += desired_frametime_ms;

    if(! ppu->options.no_vsync && current_time_ms < ppu->last_frame_ms)
    {
        input_delay(ppu->last_frame_ms - current_time_ms);
    }

    display_draw(ppu->display);

    //else
    //{
    //    Late render => drift forward
    //    ppu->last_frame_ms = current_time_ms + desired_frametime_ms;
    //}

    if(ppu->options.additional_delay_ms > 0)
    {
        input_delay(ppu->options.additional_delay_ms);
    }

    ppu->frame_count++;
}

void
nes_run_frame(NES_t *nes)
{
    if(nes->next_rom)
    {
        nes_unload(nes);
        nes_load_rom(nes, nes->next_rom);
        nes_hard_reset(nes);
        nes->next_rom = NULL;
    }

    if(nes->options.save_state)
    {
        nes->options.save_state = 0;
        nes_save_state(nes, "save.state");
    }

    if(nes->options.restore_state)
    {
        nes->options.restore_state = 0;
        nes_restore_state(nes, "save.state");
    }

    if(nes->options.soft_reset_delay > 0)
    {
        if(--nes->options.soft_reset_delay == 0)
        {
            nes_soft_reset(nes);
        }
    }

    if(nes->options.escape)
    {
        Window_t *top_window = nes->gui.display.top_window;

        if(top_window && top_window->has_titlebar && top_window->visible)
        {
            // Hide the topmost (ie focused) window
            display_hide_window(&nes->gui.display, top_window);
        }
        else
        {
            display_set_menubar(&nes->gui.display, ! nes->gui.display.menubar.visible);
        }

        nes->options.escape = 0;
    }

    if(! nes->ppu.options.paused)
    {
        int32_t scanline;
        int offset = 0;

        nes->ppu.scanline = 0;
        nes->ppu.scanline_start_ppu_cycle = 0;
        nes->frame_cpu_cycle = 0;

        /*
        // FIXME: odd frames get an extra cpu cycle?
        if(nes->ppu.frame_count & 1)
        {
            nes->frame_surplus_cpu_cycles++;
        }
        */

        if(nes->frame_surplus_cpu_cycles)
        {
            int64_t s = nes->cpu.cycle;

            n6502_run(&nes->cpu, nes->frame_surplus_cpu_cycles, 0);

            offset = nes->cpu.cycle - s - nes->frame_surplus_cpu_cycles;
            nes->frame_surplus_cpu_cycles = 0;
        }

        nes->frame_start_cpu_cycle = nes->cpu.cycle - offset;
        nes->frame_cpu_cycle = offset;
        nes->ppu.scanline_start_ppu_cycle -= offset * 3;

        INFO_NES("Frame: %d, cycle %" PRIu64 "\n", nes->ppu.frame_count, nes->frame_start_cpu_cycle);
        for(scanline = 0; scanline < NES_PPU_SCANLINES; scanline++)
        {
            int mapper_irq;

            nes->ppu.scanline = scanline;
            nes->scanline_start_cycle = nes->cpu.cycle;


            INFO_PPU("Starting scanline: %3d, C %8" PRIu64 ")\n",
                     scanline, nes->scanline_start_cycle);

            nes_ppu_update_status(&nes->ppu);

            if(scanline == NES_PPU_VBLANK)
            {
                LOG_NES("PPU In VBLANK @ cycle %" PRIu64 "\n", nes->scanline_start_cycle);
                if(nes->ppu.nmi_on_vblank)
                {
                    n6502_nmi(&nes->cpu);
                    // FIXME
                    //nes->cpu.cycle += 7;
                }
            }

            int64_t next_ppu_scanline_cycle = nes->ppu.scanline_start_ppu_cycle + PPU_CYCLES_PER_SCANLINE;
            int64_t max_scanline_cpu_cycles = (next_ppu_scanline_cycle - (nes->frame_cpu_cycle * 3) + 2) / 3;

            if(max_scanline_cpu_cycles > 0)
            {
                int hard_limit = 0;

                // FIXME: Just check for last scanline
                if((max_scanline_cpu_cycles + nes->frame_cpu_cycle) >= NES_NTSC_PPU_CYCLES_PER_FRAME)
                {
                    hard_limit = 1;
                }

                n6502_run(&nes->cpu, max_scanline_cpu_cycles, hard_limit);
            }

            nes->frame_cpu_cycle = nes->cpu.cycle - nes->frame_start_cpu_cycle;

            if(scanline >= NES_PPU_VERTICAL_RESET)
            {
                nes_ppu_render_scanline(&nes->ppu);

                if((scanline % 60) == 0) // Quarter frame @ 60Hz == 240Hz
                {
                    int apu_irq = nes_apu_240hz(&nes->apu);

                    if(apu_irq)
                    {
                        //NOTIFY("Triggering APU IRQ\n");
                        n6502_irq(&nes->cpu);
                    }
                }

                mapper_irq = nes_mapper_scanline(nes);

                if(mapper_irq)
                {
                    n6502_irq(&nes->cpu);
                }

            }
            // HACK: 245 is an even divisor of 735 == (44100/60)
            // FIXME: for better accuracy (ie to pass PCM.demo), use every scanline
            //        and render either 2 or 3 samples on that scanline
            {
                int num_samples = (scanline % 5 == 4) ? 2 : 3;
                if(scanline == 0)
                    num_samples++;
                nes_apu_fill_buffer(&nes->apu, num_samples);
            }

            nes->ppu.scanline_start_ppu_cycle = next_ppu_scanline_cycle;
        }

        nes->frame_surplus_cpu_cycles = NES_NTSC_PPU_CYCLES_PER_FRAME - nes->frame_cpu_cycle;
        if(nes->frame_surplus_cpu_cycles < 0)
            nes->frame_surplus_cpu_cycles = 0;

        INFO_NES("Frame end: %d, cycle %" PRIu64 ", delta %" PRIu64 " cycles\n",
                 nes->ppu.frame_count, nes->cpu.cycle, nes->cpu.cycle - nes->frame_start_cpu_cycle);
    }

    nes_render_frame(nes);
}

void
nes_pause(NES_t *nes, int paused)
{
    nes->ppu.options.paused = paused;
    if(! nes->options.disable_audio)
        nes_apu_pause(&nes->apu, paused);
}

unsigned
nes_toggle_pause(NES_t *nes)
{
    int paused = !nes->ppu.options.paused;

    nes_pause(nes, paused);

    return paused;
}

void
nes_notify_status(NES_t *nes, const char *msg)
{
    status_overlay_update(&nes->gui.status_overlay, msg);
}

static const char NES_SAVE_STATE_HEADER[4] = {'S', 'R', 'A', 'M'};

#define NES_MEM_SIZE 0x1000

#define SIZEOF_TYPE(STRUCT, MEMBER) sizeof(((STRUCT *) NULL)->MEMBER)
typedef struct
{
    char header[sizeof(NES_SAVE_STATE_HEADER)];
    uint32_t size;
    uint8_t nes_state[SIZEOF_TYPE(NES_t, state)];
    uint8_t cpu_state[SIZEOF_TYPE(N6502_t, regs)];
    uint8_t ppu_state[SIZEOF_TYPE(NESPPU_t, state)];
    uint8_t apu_state[SIZEOF_TYPE(NESAPU_t, state)];
} NESSaveState_t;

#define COPY_STATE(DEST, SRC) memcpy(&(DEST), &(SRC), sizeof(SRC))

void
nes_save_state(NES_t *nes, const char *path)
{
    FILE *fp = fopen(path, "wb");
    NESSaveState_t state;

    if(! fp)
    {
        NOTIFY("Failed saving state to %s\n", path);
        return;
    }

    state.size = 1234; printf("FIXME\n");

    COPY_STATE(state.header, NES_SAVE_STATE_HEADER);
    COPY_STATE(state.cpu_state, nes->cpu.regs);
    COPY_STATE(state.nes_state, nes->state);
    COPY_STATE(state.ppu_state, nes->ppu.state);
    COPY_STATE(state.apu_state, nes->apu.state);

    fwrite(&state, sizeof(state), 1, fp);
    fclose(fp);
    NOTIFY_NES(nes, "Saved state to %s\n", path);
}

void
nes_restore_state(NES_t *nes, const char *path)
{
    FILE *fp = fopen(path, "rb");
    NESSaveState_t state;
    int read_size;

    if(! fp)
    {
        NOTIFY("Failed restoring state from %s\n", path);
        return;
    }

    printf("FIXME: this should only be allowed internally or at frame start\n");

    read_size = fread(&state, sizeof(state), 1, fp);
    ASSERT(read_size == 1, "Failed reading save state from %s\n", path);

    ASSERT(memcmp(state.header, NES_SAVE_STATE_HEADER, sizeof(NES_SAVE_STATE_HEADER)) == 0, "Bad save state header in %s\n", path);

    COPY_STATE(nes->cpu.regs, state.cpu_state);
    COPY_STATE(nes->state, state.nes_state);
    COPY_STATE(nes->ppu.state, state.ppu_state);
    COPY_STATE(nes->apu.state, state.apu_state);

    nes_mapper_restore(nes);
    nes_ppu_restore(&nes->ppu);

    fclose(fp);
    NOTIFY_NES(nes, "Restored state from %s\n", path);
}
