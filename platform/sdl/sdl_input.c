#include "nes.h"
#include "input.h"

#include <SDL/SDL.h>

static inline void
option_toggle(NES_t *nes, unsigned keypressed, unsigned *bool_option,
              const char *msg, const char *str_true, const char *str_false)
{
    if(keypressed)
    {
        *bool_option = ! *bool_option;
        NOTIFY_NES(nes, msg, *bool_option ? str_true : str_false);
    }
}

static void
set_fullscreen(NES_t *nes, int fullscreen)
{
    nes_pause(nes, 1);
    display_fullscreen(&nes->gui.display, fullscreen);
    nes_pause(nes, 0);
}

static int
input_process_global_key(NES_t *nes, int key, SDLMod mod, int keypressed)
{
#ifdef osx
#define CTRL_MASK (KMOD_LMETA | KMOD_RMETA)
#define ALT_MASK  CTRL_MASK
#else
#define CTRL_MASK (KMOD_LCTRL | KMOD_RCTRL)
#define ALT_MASK  (KMOD_ALT | KMOD_ALT)
#endif

    int ctrl = mod & CTRL_MASK;
    int alt = mod & ALT_MASK;

    if(keypressed)
    {
        if(ctrl)
        {
            // First check if the menubar can handle Ctrl+xxx keys
            if(menubar_key_accelerator(&nes->gui.display.menubar, key))
            {
                return 1;
            }
        }

        switch(key)
        {
            case SDLK_c:
                if(ctrl) // Ctrl-c
                {
                    nes->options.quit = 1;
                    return 1;
                }
                break;

            case SDLK_r:
                if(ctrl) // Ctrl-r
                {
                    nes_soft_reset(nes);
                    return 1;
                }
                break;

            case SDLK_ESCAPE:
                // FIXME: if a window is top-level, escape should hide it, rather than hiding the menubar
                nes->options.escape = 1;
                return 1;

            case SDLK_BACKQUOTE:
                set_fullscreen(nes, ! nes->gui.display.fullscreen);
                return 1;

            case SDLK_TAB:
                if(alt)
                {
                    set_fullscreen(nes, 0);
                    return 1;
                }
                break;

            case SDLK_F5:
                nes->options.restore_state = 1;
                return 1;

            case SDLK_F6:
                nes->options.save_state = 1;
                return 1;

            default:
                break;
        }
    }

    return 0;
}

static void
input_key(NES_t *nes, int key, SDLMod mod, int keypressed)
{
    NESPPU_t *ppu = &nes->ppu;
    Window_t *top_window = nes->gui.display.top_window;

    if(input_process_global_key(nes, key, mod, keypressed))
        return;

    if(top_window == &ppu->gui.nes_window)
    {
        switch(key)
        {
            case SDLK_0:
                option_toggle(nes, keypressed, &ppu->options.sprite0_negative,
                              "Sprite0 %s\n", "negative", "normal");
                break;

            case SDLK_1:
                option_toggle(nes, keypressed, &ppu->options.hide_sprites,
                              "Sprites %s\n", "hidden", "shown");
                break;

            case SDLK_MINUS:
                option_toggle(nes, keypressed, &ppu->options.enable_scanlines,
                              "%s\n", "Scanline emulation", "Line doubling");
                break;

            case SDLK_p:
                if(keypressed)
                {
                    unsigned paused = nes_toggle_pause(nes);
                    nes_notify_status(nes, paused ? "Paused" : "Playing");
                }
                break;

            case SDLK_QUOTE:
                option_toggle(nes, keypressed, &nes->apu.options.disable_dmc,
                              "APU DMC %s\n", "Disabled", "Enabled");
                break;

            case SDLK_m:
                option_toggle(nes, keypressed, &nes->apu.options.disable_square1,
                              "APU Square1 %s\n", "Disabled", "Enabled");
                break;

            case SDLK_COMMA:
                option_toggle(nes, keypressed, &nes->apu.options.disable_square2,
                              "APU Square2 %s\n", "Disabled", "Enabled");
                break;

            case SDLK_PERIOD:
                option_toggle(nes, keypressed, &nes->apu.options.disable_triangle,
                              "APU Triangle %s\n", "Disabled", "Enabled");
                break;

            case SDLK_SLASH:
                option_toggle(nes, keypressed, &nes->apu.options.disable_noise,
                              "APU Noise %s\n", "Disabled", "Enabled");
                break;

            case SDLK_LSHIFT:
                nes->input.key[0].b = keypressed;
                break;

            case SDLK_z:
                nes->input.key[0].a = keypressed;
                break;

            case SDLK_TAB:
                nes->input.key[0].select = keypressed;
                break;

            case SDLK_RETURN:
                nes->input.key[0].start = keypressed;
                break;

            case SDLK_UP:
                nes->input.key[0].up = keypressed;
                nes->input.key[0].mask.up   = keypressed;
                nes->input.key[0].mask.down = !keypressed;
                break;

            case SDLK_DOWN:
                nes->input.key[0].down = keypressed;
                nes->input.key[0].mask.up   = !keypressed;
                nes->input.key[0].mask.down = keypressed;
                break;

            case SDLK_LEFT:
                nes->input.key[0].left = keypressed;
                nes->input.key[0].mask.left  = keypressed;
                nes->input.key[0].mask.right = !keypressed;
                break;

            case SDLK_RIGHT:
                nes->input.key[0].right = keypressed;
                nes->input.key[0].mask.left  = !keypressed;
                nes->input.key[0].mask.right = keypressed;
                break;

            default:
#if 0
                if(keypressed)
                    ("Unknown key pressed: %d\n", key);
#endif
                break;
        }
    }
    else
    {
        if(keypressed)
        {
            if(top_window->on_keydown)
            {
                top_window->on_keydown(top_window, key);
            }
        }
    }
}

void
input_update(NES_t *nes)
{
    SDL_Event event;
    NESPPU_t *ppu = &nes->ppu;

    while(SDL_PollEvent(&event))
    {
        switch(event.type)
        {
            case SDL_QUIT:
                nes->options.quit = 1;
                break;

            case SDL_MOUSEMOTION:
                nes->input.mousex = event.motion.x;
                nes->input.mousey = event.motion.y;

                display_mousemove(ppu->display, nes->input.mousex, nes->input.mousey);
                break;

            case SDL_MOUSEBUTTONDOWN:
                if(event.button.button == SDL_BUTTON_LEFT)
                {
                    nes->input.mousedown = 1;
                    display_mousedown(ppu->display, nes->input.mousex, nes->input.mousey);
                }
                break;

            case SDL_MOUSEBUTTONUP:
                if(event.button.button == SDL_BUTTON_LEFT)
                {
                    nes->input.mousedown = 0;
                    display_mouseup(ppu->display, nes->input.mousex, nes->input.mousey);
                }
                break;

            case SDL_KEYDOWN:
            case SDL_KEYUP:
            {
                SDLKey key = event.key.keysym.sym;
                SDLMod mod = event.key.keysym.mod;
                int keypressed = (event.type == SDL_KEYDOWN);
                input_key(nes, key, mod, keypressed);
                break;
            }
        }
    }
}

void
input_latch_joypads(NES_t *nes)
{
    uint8_t *pad1 = &nes->joypad_latch1;
    uint8_t *pad2 = &nes->joypad_latch2;
    uint8_t *mousedown = &nes->paddle_buttondown;

    *pad1 =
        ((nes->input.key[0].right & nes->input.key[0].mask.right) << 7) |
        ((nes->input.key[0].left  & nes->input.key[0].mask.left ) << 6) |
        ((nes->input.key[0].down  & nes->input.key[0].mask.down ) << 5) |
        ((nes->input.key[0].up    & nes->input.key[0].mask.up   ) << 4) |
        (nes->input.key[0].start  << 3) |
        (nes->input.key[0].select << 2) |
        (nes->input.key[0].b      << 1) |
        (nes->input.key[0].a      << 0);

    *pad2 =
        (nes->input.key[1].right  << 7) |
        (nes->input.key[1].left   << 6) |
        (nes->input.key[1].down   << 5) |
        (nes->input.key[1].up     << 4) |
        (nes->input.key[1].start  << 3) |
        (nes->input.key[1].select << 2) |
        (nes->input.key[1].b      << 1) |
        (nes->input.key[1].a      << 0);

#if 1
    if(nes->ppu.options.enable_paddle)
    {
        /*
        Paddle support

        The paddle position is read via D1 of $4017; the read data is inverted (0=1, 1=0). The first value read is
        the MSB, and the 8th value read is (obviously) the LSB. Valid value ranges are 98 to 242, where 98 represents
        the paddle being turned completely counter-clockwise.

        For example, if %01101011 is read, the value would be NOT'd, making %10010100 which is 146. The paddle also
        contains one button, which is read via D1 of $4016. A value of 1 specifies that the button is being pressed.
        */

        static const int NES_PADDLE_MAX = 242;
        static const int NES_PADDLE_MIN = 98;

        static const int NES_PADDLE_RANGE = NES_PADDLE_MAX - NES_PADDLE_MIN + 1;

#define NES_WINDOW_WIDTH 512
        float pos = (float) (nes->input.mousex * 3/2 - 30) / (NES_WINDOW_WIDTH * 3/4);
        if(pos > 1.0) pos = 1.0;
        if(pos < 0) pos = 0.0;

        unsigned v = (unsigned) (pos * NES_PADDLE_RANGE) + NES_PADDLE_MIN;
        // Bit reversal
        v = ((v & 0x01) << 7) |
            ((v & 0x02) << 5) |
            ((v & 0x04) << 3) |
            ((v & 0x08) << 1) |
            ((v & 0x10) >> 1) |
            ((v & 0x20) >> 3) |
            ((v & 0x40) >> 5) |
            ((v & 0x80) >> 7);
        *pad2 = ~v;
        *mousedown = nes->input.mousedown;
    }
#else
    (void) mousedown;
#endif
}

unsigned
input_time_ms(void)
{
    return SDL_GetTicks();
}

void
input_delay(unsigned delay_ms)
{
    SDL_Delay(delay_ms);
}
