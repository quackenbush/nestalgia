#include "nsf.h"
#include "log.h"
#include "nes_mapper.h" // For nes_select_rom_bank

#include <stdint.h>
#include <string.h>

#define LOG(...) _LOG(NES, __VA_ARGS__)

// --------------------------------------------------------------------------------
static const uint8_t NSF_HEADER[5] = {'N', 'E', 'S', 'M', 0x1A};

static const char *NSF_FORMATS[] = {"NTSC", "PAL", "Dual"};

#define NSF_NUM_BANKS       8
#define NSF_MAX_BANKS       16
#define NSF_BASE_ADDR       0x8000
#define NSF_BANKSWITCH_BASE 0X5FF8
#define NSF_TRAP_ADDR       0x7117

typedef struct
{
    char     NESM[5];        // 0000    5   STRING  "NESM",01Ah  ; denotes an NES sound format file
    uint8_t  Version_number; // 0005    1   BYTE    Version number (currently 01h)
    uint8_t  Total_songs;    // 0006    1   BYTE    Total songs   (1=1 song, 2=2 songs, etc)
    uint8_t  Starting_song;  // 0007    1   BYTE    Starting song (1= 1st song, 2=2nd song, etc)

    uint16_t Load_address;   // 0008    2   WORD    (lo/hi) load address of data (8000-FFFF)
    uint16_t Init_address;   // 000a    2   WORD    (lo/hi) init address of data (8000-FFFF)
    uint16_t Play_address;   // 000c    2   WORD    (lo/hi) play address of data (8000-FFFF)

    char     Song[32];       // 000e    32  STRING  The name of the song, null terminated
    char     Artist[32];     // 002e    32  STRING  The artist, if known, null terminated
    char     Copyright[32];  // 004e    32  STRING  The Copyright holder, null terminated

    uint16_t NTSC_speed;     // 006e    2   WORD    (lo/hi) speed, in 1/1000000th sec ticks, NTSC (see text)
    uint8_t  BS_init[NSF_NUM_BANKS]; // 0070    8   BYTE    Bankswitch Init Values (see text, and FDS section)
    uint16_t PAL_speed;      // 0078    2   WORD    (lo/hi) speed, in 1/1000000th sec ticks, PAL (see text)
    uint8_t  PAL_NTSC;       // 007a    1   BYTE    PAL/NTSC bits:
                             //             bit 0: if clear, this is an NTSC tune
                             //             bit 0: if set, this is a PAL tune
                             //             bit 1: if set, this is a dual PAL/NTSC tune
                             //             bits 2-7: not used. they *must* be 0
    uint8_t  Extra_flags;    // 007b    1   BYTE    Extra Sound Chip Support
                             //             bit 0: if set, this song uses VRCVI
                             //             bit 1: if set, this song uses VRCVII
                             //             bit 2: if set, this song uses FDS Sound
                             //             bit 3: if set, this song uses MMC5 audio
                             //             bit 4: if set, this song uses Namco 106
                             //             bit 5: if set, this song uses Sunsoft FME-07
                             //             bits 6,7: future expansion: they *must* be 0
    uint32_t  Expansion;     // 007c    4   ----    4 extra bytes for expansion (must be 00h)
} NSF_t;

typedef struct
{
    NSF_t *nsf;
    NES_t *nes;
    int num_banks;
} NSFState_t;

// --------------------------------------------------------------------------------

static int
nsf_is_bankswitched(NSF_t *nsf)
{
    int i;
    for(i = 0; i < NSF_NUM_BANKS; i++)
    {
        int bankswitch = nsf->BS_init[i];
        if(bankswitch)
        {
            return 1;
        }
    }

    return 0;
}

static void
nsf_trap(N6502_t *cpu)
{
    LOG("Trap @ %04X\n", cpu->regs.PC);
    cpu->stopped = 1;
}

static void
nsf_write(void *p, uint16_t addr, uint8_t data)
{
    NSFState_t *nsf_state = (NSFState_t *) p;
    NES_t *nes = nsf_state->nes;

    if(addr >= NSF_BANKSWITCH_BASE)
    {
        int bank = addr - NSF_BANKSWITCH_BASE;
        int bankswitch = data;

        ASSERT(bankswitch < nsf_state->num_banks, "Bad bankswitch: %d (max:%d)", bankswitch, nsf_state->num_banks);
        LOG("NSF Bank %d <= %d\n", bank, bankswitch);

        nes_select_prg_rom_bank(nes, bank, bankswitch, 1);

    }
    else
    {
        ASSERT(0, "bad nsf write[%04X] <= %02X", addr, data);
    }
}

static void
nsf_jsr(N6502_t *cpu, uint16_t addr)
{
    static const int MAX_INSTRUCTIONS = 10*1000*1000;

    // JSR
    cpu->regs.S = 0xFF;
    cpu->regs.PC = NSF_TRAP_ADDR-1;
    PUSH_PC();
    cpu->regs.PC = addr;
    cpu->stopped = 0;

    // => code will then call RTS
    n6502_run_until_stopped(cpu, MAX_INSTRUCTIONS);
    ASSERT(cpu->stopped, "CPU did not stop after %d instructions", MAX_INSTRUCTIONS);
}

static void
nsf_install_bankswitch(NSF_t *nsf, NES_t *nes)
{
    if(nsf_is_bankswitched(nsf))
    {
        int i;

        for(i = 0; i < NSF_NUM_BANKS; i++)
        {
            int bankswitch = nsf->BS_init[i];
            nes_select_prg_rom_bank(nes, i, bankswitch, 1);
        }
    }
}

static void
nsf_init(NSF_t *nsf, NES_t *nes, int song)
{
    N6502_t *cpu = &nes->cpu;

    LOG("NSF Init Song %d\n", song);
    ASSERT(song >= 1 && song <= nsf->Total_songs, "Bad song: %d", song);

    cpu->regs.P.word = 0x04;

    cpu->regs.A = song - 1;
    cpu->regs.X = 0; // FIXME: NTSC

    nsf_install_bankswitch(nsf, nes);

    nsf_jsr(cpu, nsf->Init_address);
}

static void
nsf_run(NSF_t *nsf, NES_t *nes)
{
    N6502_t *cpu = &nes->cpu;
    int frame = 0;

    nes_pause(nes, 0); // Unpause

    WRITE_MEM(0x4010, 0x10);
    WRITE_MEM(0x4015, 0x0f);

    nsf_install_bankswitch(nsf, nes);

    while(! nes->options.quit)
    {
        nsf_jsr(cpu, nsf->Play_address);

        nes_apu_240hz(&nes->apu);
        nes_apu_240hz(&nes->apu);
        nes_apu_240hz(&nes->apu);
        nes_apu_240hz(&nes->apu);

        nes_apu_fill_buffer(&nes->apu, 735);
        nes_render_frame(nes);

        ++frame;
        NOTIFY_NES(nes, "Frame: %d", frame);
    }
}

static void
nsf_init_from_file(NSFState_t *nsf_state, const char *path)
{
    NES_t *nes = nsf_state->nes;
    NSF_t *nsf = nsf_state->nsf;
    FILE *fp;
    int result;
    uint16_t offset;
    uint8_t byte;

    fp = fopen(path, "rb");
    ASSERT(fp != 0, "Could not open NSF '%s'", path);
    ASSERT(sizeof(NSF_t) == 0x80, "Bad NSF size: %d vs %d", (int) sizeof(NSF_t), 0x80);

    result = fread(nsf, sizeof(NSF_t), 1, fp);

    ASSERT(result == 1, "Bad NSF: '%s'", path);

    ASSERT(memcmp(nsf->NESM, NSF_HEADER, sizeof(NSF_HEADER)) == 0, "Bad NSF Header");

    nes->cartridge_write = nsf_write;
    nes->cartridge_pointer = nsf_state;

    offset = nsf->Load_address - NSF_BASE_ADDR;
    ASSERT(! (nsf_is_bankswitched(nsf) && (nsf->Load_address & 0xfff) != 0),
           "non-aligned NSF load not implemented: %04X", nsf->Load_address);

    nes->num_prg_rom_banks = 1;
    nes->prg_rom_banks = malloc(NSF_MAX_BANKS * 0x1000);
    NOTIFY("FIXME: free this malloc\n");

    while(fread(&byte, 1, 1, fp) == 1)
    {
        nes->prg_rom_banks[offset++] = byte;
    }

    nsf_state->num_banks = (NSF_BASE_ADDR + offset - nsf->Load_address) / 0x1000;

    ASSERT(nsf_state->num_banks < NSF_MAX_BANKS, "Too many banks: %d", nsf_state->num_banks);

    NOTIFY("Loaded NSF: %x => %x (%d bytes) [%d banks]\n",
           nsf->Load_address, NSF_BASE_ADDR + offset - 1,
           NSF_BASE_ADDR + offset - nsf->Load_address,
           nsf_state->num_banks);

    nes_select_prg_rom_bank(nes, 0, 0, 8);

    fclose(fp);
}

void
nsf_play(NES_t *nes, const char *path)
{
    NSF_t nsf;
    N6502_t *cpu = &nes->cpu;
    const char *nsf_format;
    NSFState_t nsf_state = {&nsf, nes};

    cpu->debug_trap = nsf_trap;
    WRITE_MEM(NSF_TRAP_ADDR, OP_DEBUG_TRAP); // Install trap

    nsf_init_from_file(&nsf_state, path);

    if(nsf.PAL_NTSC >= sizeof(NSF_FORMATS) / sizeof(NSF_FORMATS[0]))
    {
        ASSERT(0, "Bad format: %d", nsf.PAL_NTSC);
    }

    nsf_format = NSF_FORMATS[nsf.PAL_NTSC];

    ASSERT(nsf.Load_address >= NSF_BASE_ADDR, "Bad load address: %X", nsf.Load_address);

#if 1
    NOTIFY("Artist:     %s\n", nsf.Artist);
    NOTIFY("Song:       %s\n", nsf.Song);
    NOTIFY("Copyright:  %s\n", nsf.Copyright);
    NOTIFY("Format:     %s\n", nsf_format);
    NOTIFY("Version:    %d\n", nsf.Version_number);
    NOTIFY("Songs:      %d\n", nsf.Total_songs);
    NOTIFY("First Song: %d\n", nsf.Starting_song);
    NOTIFY("Load:       %04X\n", nsf.Load_address);
    NOTIFY("Init:       %04X\n", nsf.Init_address);
    NOTIFY("Play:       %04X\n", nsf.Play_address);
    NOTIFY("NTSC Speed: %04X (%0.2f)\n", nsf.NTSC_speed, (float) 1000000 / nsf.NTSC_speed);
    NOTIFY("PAL Speed:  %04X (%0.2f)\n", nsf.PAL_speed, (float) 1000000 / nsf.PAL_speed);
    NOTIFY("Bankswitch: %02X %02X %02X %02X %02X %02X %02X %02X\n",
           nsf.BS_init[0], nsf.BS_init[1], nsf.BS_init[2], nsf.BS_init[3],
           nsf.BS_init[4], nsf.BS_init[5], nsf.BS_init[6], nsf.BS_init[7]);
#endif

    ASSERT(nsf.PAL_NTSC != 1, "PAL not impl");

    ASSERT(nsf.Extra_flags == 0, "External sound chip not implemented: 0x%0x", nsf.Extra_flags);
    ASSERT(nsf.Expansion == 0, "Expansion not implemented: 0x%04x", nsf.Expansion);

    nsf_init(&nsf, nes, nsf.Starting_song);
    nsf_run(&nsf, nes);
}
