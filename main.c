#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h> // strcmp
#include "common.h"

#include "nes.h"
#include "c64/c64_harness.h"
#include "nsf.h"

#define LOG(...)  _LOG(MAIN, __VA_ARGS__)
#define INFO(...) _INFO(MAIN, __VA_ARGS__)

#ifndef VERSION
#error "VERSION must be defined"
#endif

static const char *APP_NAME = "NEStalgia emulator build " VERSION;

// Logging vars
FILE *debug_fp = NULL;
FILE *info_fp = NULL;
uint32_t log_zone_mask = 0;

static struct
{
    char *rom_path;
} nestalgia_state = {0};

#ifndef WIN32
#include <argp.h>

enum
{
    OPT_start = 1000,
    OPT_DELAY,
    OPT_SYNC,
    OPT_NOCROP,
    OPT_SPRITE0,
    OPT_BLARGG,
    OPT_DEBUG,
    OPT_NOAUDIO,
    OPT_WAV,
    OPT_PC,
    OPT_FS,
};

static struct argp_option options[] =
{
    {"step",        's', 0,              0, "Step the CPU" },
    {"dump",        'd', 0,              0, "Dump a CPU log file" },
    {"test",        't', 0,              0, "Run the C64 test suite" },
    {"override",    'o', "MODULE",       0, "Override the C64 test module" },
    {"max",         'm', "MAX",          0, "Maximum CPU instructions" },
    {"heartbeat",   'b', "HEARTBEAT",    0, "Enable CPU heartbeat at #instructions" },
    {"frames",      'f', "FRAMES",       0, "Maximum number of NES frames to execute" },
    {"log",         'l', "ZONE",         0, "Enable logging of a zone" },
    {"delay",       OPT_DELAY, "DELAY",  0, "Extra delay per frame (in ms)" },
    {"sync",        OPT_SYNC,  "SYNC",   0, "Frame sync every nth scanline" },
    {"novsync",     'v',  0,             0, "No frame vsync (run as fast as possible)" },
    {"paddle",      'p',  0,             0, "Enable paddle controller via the mouse" },
    {"sprite0",     OPT_SPRITE0,  0,     0, "Force PPU sprite0 triggering" },
    {"nocrop",      OPT_NOCROP, 0,       0, "Emit uncropped NES frames" },
    {"blargg",      OPT_BLARGG, "MODE",  0, "Run in Blargg self-checking test mode" },
    {"debug",       OPT_DEBUG, 0,        0, "Enable debugging" },
    {"noaudio",     OPT_NOAUDIO, 0,      0, "Disable audio" },
    {"wav",         OPT_WAV, 0,          0, "Dump an audio wav file" },
    {"pc",          OPT_PC, "PC",        0, "Force the 6502 PC to a different reset address" },
    {"fullscreen",  OPT_FS, 0,           0, "Start in fullscreen, rather than windowed mode" },
    { 0 }
};

static error_t nes_parse_opt (int key, char *arg, struct argp_state *state);

static struct argp argp = { options, nes_parse_opt,
                            "ROM",
                            NULL };

static const char *LogZoneNames[] =
{
#define LOG_ZONE(X) #X,
    LOG_ZONES
#undef LOG_ZONE
};

void
log_enable_zone(const char *arg)
{
    int i;

    if(strlen(arg) == 1 && arg[0] == '*')
    {
        log_zone_mask = ~0;
        NOTIFY("Enabled logging for all zones\n");

        return;
    }

    for(i = 0; i < lzNumZones; i++)
    {
        if(strcasecmp(arg, LogZoneNames[i]) == 0)
        {
            log_zone_mask |= (1 << i);
            NOTIFY("Enabled log zone '%s' (%d)\n", LogZoneNames[i], i);

            return;
        }
    }

    ASSERT(0, "Log zone '%s' not available\n", arg);
}

static error_t
nes_parse_opt (int key, char *arg, struct argp_state *state)
{
    NES_t *nes = state->input;

    switch (key)
    {
        case 's':
            nes->cpu.options.step = 1;
            log_enable_zone("6502");
            break;

        case 'd':
            nes->cpu.options.dump = 1;
            log_enable_zone("6502");
            break;

        case 't':
            nes->cpu.options.test = 1;
            nes->cpu.options.log = 1;
            break;

        case 'o':
            nes->cpu.options.override = arg;
            break;

        case 'l':
            log_enable_zone(arg);
            break;

        case 'm':
            nes->options.max_instructions = atoll(arg);
            NOTIFY("Max CPU instructions: %" PRIu64 "\n", nes->options.max_instructions);
            break;

        case 'f':
            nes->options.max_frames = atoi(arg);
            NOTIFY("Max frames: %u\n", nes->options.max_frames);
            break;

        case 'b':
            nes->cpu.heartbeat_at = atoll(arg);
            NOTIFY("Enabled CPU heartbeat @ %" PRIu64 " instructions\n", nes->cpu.heartbeat_at);
            break;

        case 'v':
            nes->ppu.options.no_vsync = 1;
            NOTIFY("Disabled vsync\n");
            break;

        case 'p':
            nes->ppu.options.enable_paddle = 1;
            NOTIFY("Enabled paddle support\n");
            break;

        case OPT_FS:
            nes->gui.display.fullscreen = 1;
            break;

        case OPT_DELAY:
            nes->ppu.options.additional_delay_ms = atoi(arg);
            break;

        case OPT_SYNC:
            nes->ppu.options.sync_every_nth_scanline = atoi(arg);
            break;

        case OPT_NOCROP:
            nes->ppu.options.crop_ntsc = 0;
            NOTIFY("Emitting uncropped NES frames\n");
            break;

        case OPT_BLARGG:
            nes->options.blargg_test = atoi(arg);
            ASSERT(nes->options.blargg_test >= 1 && nes->options.blargg_test <= 2, "Bad Blargg mode: %d\n", nes->options.blargg_test);
            nes->options.max_frames = 5000;
            nes->ppu.options.no_vsync = 1;
            nes->ppu.options.sprite_clip_right = 1; // FIXME: this hack is needed for Blargg PPU tests, but fails in nesmas
            nes->ppu.options.trigger_hack = 0; // FIXME: timing mismatch between Blargg/Double Dragon
            NOTIFY("Running in Blargg self-checking test mode\n");
            break;

        case OPT_SPRITE0:
            nes->ppu.options.force_sprite0 = 1;
            NOTIFY("Forcing PPU sprite0 triggering\n");
            break;

        case OPT_DEBUG:
            nes->ppu.options.display_windowed = 1;
            break;

        case OPT_NOAUDIO:
            nes->options.disable_audio = 1;
            NOTIFY("Audio disabled\n");
            break;

        case OPT_WAV:
            nes->apu.options.dump_wav = 1;
            NOTIFY("Dumping audio wav\n");
            break;

        case OPT_PC:
            nes->options.reset_pc = htoi(arg);
            NOTIFY("Set the Reset PC to %04X\n", nes->options.reset_pc);
            break;

        case ARGP_KEY_ARG:
            ASSERT(nestalgia_state.rom_path == NULL, "Can only specify one ROM: [%s]\n", arg);
            nestalgia_state.rom_path = arg;
            break;

        case ARGP_KEY_END:
            if(! nes->cpu.options.test && ! nestalgia_state.rom_path)
                argp_usage(state);
            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static void
parse_options(NES_t *nes, int argc, char *argv[])
{
    argp.doc = APP_NAME;

    // Default options values
    nes->options.max_instructions = 5000 * 1000000LL;
    nes->options.max_frames = 0;
    //nes->ppu.options.crop_ntsc = 1; // FIXME: NTSC cropping is a bit pessimistic
    nes->ppu.options.trigger_hack = 1; // FIXME: hack for Double Dragon

    argp_parse(&argp, argc, argv, 0, 0, nes);
}
#endif

// OSX-only
#include <SDL/SDL.h>

int
main(int argc, char *argv[])
{
    NES_t *nes;

    nes = calloc(1, sizeof(NES_t));

#ifdef WIN32
    if(argc == 2)
    {
        nestalgia_state.rom_path = argv[1];
    }
    else
    {
        nestalgia_state.rom_path = "rom.nes";
    }
    //nes->ppu.options.display_windowed = 1;
#else
    parse_options(nes, argc, argv);
#endif

    if(! nes->cpu.options.test)
    {
        NOTIFY("%s\n", APP_NAME);
    }

    nes->gui.display.title = APP_NAME;

    if(nes->ppu.options.display_windowed)
    {
        nes->ppu.options.display_width = 800;
        nes->ppu.options.display_height = 600;
    }
    else
    {
        nes->ppu.options.display_width = (NES_CROPPED_WIDTH(&nes->ppu) * 2);
        nes->ppu.options.display_height = (NES_CROPPED_HEIGHT(&nes->ppu) * 2);
    }

    info_fp = stdout;
    if(nes->cpu.options.dump)
    {
#ifndef DEBUG
        ASSERT(0, "Can't dump outside of DEBUG mode\n");
#else
        const char *DUMP_FILE = "6502.log";
        debug_fp = fopen(DUMP_FILE, "w");
        info_fp = debug_fp;
        assert(debug_fp != 0);
        NOTIFY("Dumping CPU log to %s\n", DUMP_FILE);
#endif
    }

    if(0)
    {
        debug_fp = stdout;
    }

    n6502_init(&nes->cpu);

    // FIXME: move into nsf.c
#define NSF_SUFFIX ".nsf"
    if(nestalgia_state.rom_path && strlen(nestalgia_state.rom_path) > strlen(NSF_SUFFIX) &&
       strcasecmp(NSF_SUFFIX, nestalgia_state.rom_path + strlen(nestalgia_state.rom_path) - strlen(NSF_SUFFIX)) == 0)
    {
        nes_init(nes, 1);
        nsf_play(nes, nestalgia_state.rom_path);
    }
    else if(nes->cpu.options.test)
    {
        nes_soft_reset(nes);
        c64_install_harness(&nes->cpu);
        n6502_run_until_stopped(&nes->cpu, nes->options.max_instructions);
    }
    else
    {
        unsigned frame_num = 0;

        // OSX-only
        SDL_Init(SDL_INIT_EVERYTHING);
        atexit(SDL_Quit);
        // -OSX-only

        nes_init(nes, 1);
        nes_load_rom(nes, nestalgia_state.rom_path);
        nes_hard_reset(nes);
        nes_soft_reset(nes);
        if(nes->options.reset_pc > 0)
        {
            nes->cpu.regs.PC = nes->options.reset_pc;
        }

        while((nes->options.max_frames == 0) || (frame_num < nes->options.max_frames))
        {
            INFO("Frame: %d\n", frame_num);
            nes_run_frame(nes);

            if(nes->options.quit)
            {
                nes_quit(nes);
                break;
            }
            frame_num++;
        }

        if(nes->options.blargg_test)
        {
            NOTIFY("Blargg test did not complete within %d frames\n", nes->options.max_frames);
            return 1;
        }
    }

    free(nes);

    return 0;
}
