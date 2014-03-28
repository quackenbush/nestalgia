#include "nes_apu.h"
#include "audio.h"
#include "log.h"
#include "nes.h" // FIXME: needed only for CPU clock frequency
#include "platform_audio.h"
#include <string.h>

#include "wav_audio.h"

#define TRACE(...) _LOG(APU, __VA_ARGS__)
#define INFO(...)  _INFO(APU, __VA_ARGS__)
//#define INFO(...) _INFO(__VA_ARGS__)
#define DUMP_TRIANGLE(...) printf(__VA_ARGS__)

#define AUDIO_FRAME_SIZE (AUDIO_SAMPLE_RATE / 60) // HACK: tied to NTSC frequency

#define SQUARE1(FIELD)    (apu->state.square1_regs.bits.FIELD)
#define SQUARE2(FIELD)    (apu->state.square2_regs.bits.FIELD)
#define TRIANGLE(FIELD)   (apu->state.triangle_regs.bits.FIELD)
#define NOISE(FIELD)      (apu->state.noise_regs.bits.FIELD)
#define DMC(FIELD)        (apu->state.dmc_regs.bits.FIELD)

#define STATUS(FIELD)     (apu->state.status_regs.bits.FIELD)

// FIXME: run APU after every CPU instruction so that the square timer div2 test should work

// FIXME: does volume need to be latched inside the wave generators or is it live?

// FIXME: This table is different than Brad Taylor's table (line 867)
static const uint8_t LENGTH_COUNT[32] =
{
    0x0A, 0xFE,
    0x14, 0x02,
    0x28, 0x04,
    0x50, 0x06,
    0xA0, 0x08,
    0x3C, 0x0A,
    0x0E, 0x0C,
    0x1A, 0x0E,
    0x0C, 0x10,
    0x18, 0x12,
    0x30, 0x14,
    0x60, 0x16,
    0xC0, 0x18,
    0x48, 0x1A,
    0x10, 0x1C,
    0x20, 0x1E,
};

static const uint16_t NOISE_PERIOD[16] =
{
    0x004,
    0x008,
    0x010,
    0x020,
    0x040,
    0x060,
    0x080,
    0x0A0,
    0x0CA,
    0x0FE,
    0x17C,
    0x1FC,
    0x2FA,
    0x3F8,
    0x7F2,
    0xFE4,
};

static const uint16_t DMC_PERIOD[16] =
{
    0x1AC,
    0x17C,
    0x154,
    0x140,
    0x11E,
    0x0FE,
    0x0E2,
    0x0D6,
    0x0BE,
    0x0A0,
    0x08E,
    0x080,
    0x06A,
    0x054,
    0x048,
    0x036,
};

typedef struct
{
    int divider;
    int mask;
    const uint8_t duty_cycle[32];
} ApuDuty_t;

static const ApuDuty_t APU_SQUARE_DUTY[4] =
{
    // 12.5%
    {8, 7, {0, 1, 0, 0, 0, 0, 0, 0}},
    // 25.0%
    {8, 7, {0, 1, 1, 0, 0, 0, 0, 0}},
    // 50.0%
    {8, 7, {0, 1, 1, 1, 1, 0, 0, 0}},
    // 75.0%
    {8, 7, {0, 1, 1, 1, 1, 1, 1, 0}},
};

static const ApuDuty_t APU_TRIANGLE =
{32, 31,
 {0xF, 0xE, 0xD, 0xC, 0xB, 0xA, 0x9, 0x8, 0x7, 0x6, 0x5, 0x4, 0x3, 0x2, 0x1, 0x0,
  0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF}};

static const ApuDuty_t APU_NOISE =
{2, 1, {0, 1}};

// FIXME: is 2/1 any different?
static const ApuDuty_t APU_DMC =
{16, 15, {0}};

void
channel_enable(ToneChannel_t *channel, int enable)
{
    channel->enable = enable;
    if(! enable)
    {
        // FIXME: clear the phase count?
        channel->count = 0; // HAKK
        channel->length_count = 0;
    }
}

static void
channel_init(ToneChannel_t *channel, const char *name)
{
    channel->name = name;
}

void
nes_apu_init(NESAPU_t *apu)
{
    memset(&apu->state, 0, sizeof(apu->state));

    AUDIO_DESCRIPTOR.audio_open(&apu->audio_buffer);
    if(apu->options.dump_wav)
    {
        wav_init();
    }
}

void
nes_apu_pause(NESAPU_t *apu, int paused)
{
    AUDIO_DESCRIPTOR.audio_pause(paused);
}

void
nes_apu_reset(NESAPU_t *apu)
{
    TriangleRegs_t triangle_save = apu->state.triangle_regs;

    memset(&apu->state, 0, sizeof(apu->state));

    // Blargg len_ctrs_enabled claims triangle regs don't get cleared @ reset
    apu->state.triangle_regs = triangle_save;

    channel_init(&apu->state.square[0], "Square1");
    channel_init(&apu->state.square[1], "Square2");
    channel_init(&apu->state.triangle, "Triangle");
    channel_init(&apu->state.noise, "Noise");
    channel_init(&apu->state.dmc, "DMC");

    apu->state.frame_sequencer_mode = MODE_192HZ;
    apu->state.clock240 = 0;

    apu->state.noise.attr.noise.shift_reg = 0x001; // Noise seed value
}

void
nes_apu_destroy(NESAPU_t *apu)
{
    AUDIO_DESCRIPTOR.audio_pause(1);
    AUDIO_DESCRIPTOR.audio_close();

    if(apu->options.dump_wav)
    {
        wav_destroy();
    }
}

static void
dmc_latch_sample_address(NESAPU_t *apu)
{
    apu->state.dmc.attr.dmc.done = 0;

    apu->state.dmc.attr.dmc.sample_address = 0xC000 + (DMC(sample_address) * 0x40);
    apu->state.dmc.attr.dmc.sample_bytes = (DMC(sample_length) * 0x10) + 1;

    INFO("DMC latch: %04X / %d bytes\n",
          apu->state.dmc.attr.dmc.sample_address, apu->state.dmc.attr.dmc.sample_bytes);
}

static void
dmc_next_sample(NESAPU_t *apu)
{
    ToneChannel_t *chan = &apu->state.dmc;

    if(chan->attr.dmc.sample_bytes == 0)
    {
        if(DMC(loop))
        {
            dmc_latch_sample_address(apu);
        }
        else
        {
            INFO("DMC irq set\n");
            STATUS(dmc_irq) = 1;
            chan->attr.dmc.done = 1;
            return;
        }
    }

    chan->attr.dmc.sample = apu->read_mem_func(chan->attr.dmc.sample_address);
    apu->increment_cycles_func(apu->arg_ptr, APU_DMA_CYCLES);

    TRACE("DMC update: %02X @ %04X [%d bytes left]\n",
          chan->attr.dmc.sample, chan->attr.dmc.sample_address, chan->attr.dmc.sample_bytes - 1);

    chan->attr.dmc.sample_bitcount = 8;
    chan->attr.dmc.sample_address++;
    if(chan->attr.dmc.sample_address == 0x0000)
    {
        // wrap around
        ASSERT(0, "DMC wrap around");
        chan->attr.dmc.sample_address = 0x8000;
    }

    chan->attr.dmc.sample_bytes--;
}

static inline uint8_t
tone_wave(ToneChannel_t *t)
{
    int skip;

    skip = (! t->enable
            || t->reload_count < 8
            || t->length_count == 0
            || t->cpu_period < 8
            || t->silence
            || t->attr.square.sweep_silence
        );

    if(! skip)
    {
        t->count -= t->decrement; /* Take one sample */

        while(t->count <= 0)
        {
            t->count = t->reload_count + t->count;

            t->duty.index = (t->duty.index + 1) & t->duty.mask;
            t->dac_value = t->duty.values[t->duty.index] * t->volume;
        }
    }

    return t->dac_value;
}

static inline uint8_t
noise_wave(ToneChannel_t *t)
{
    int skip;

    skip = (! t->enable
            //t->reload_count < 1
            || t->length_count == 0
            //|| t->cpu_period == 0
        );

    if(! skip)
    {
        t->count -= t->decrement; /* Take one sample */

        if(t->count <= 0)
        {
            uint16_t b0, b1;
            uint16_t shift_reg = t->attr.noise.shift_reg;

            //t->count = t->reload_count + t->count;
            t->count = t->reload_count; // HAKK

            b0 = shift_reg & 1;

            if(t->attr.noise.short_mode)
            {
                b1 = (shift_reg >> 6) & 1;
            }
            else
            {
                b1 = (shift_reg >> 1) & 1;
            }

            shift_reg = ((b0 ^ b1) << 14) | (shift_reg >> 1);
            t->attr.noise.shift_reg = shift_reg;
        }
    }

    return (t->attr.noise.shift_reg & 1) ? 0 : t->volume;
}

static inline uint8_t
dmc_wave(NESAPU_t *apu)
{
    ToneChannel_t *t = &apu->state.dmc;
    int skip;

    skip = (! t->enable
            || t->reload_count < 1
            //|| t->length_count == 0
            || t->attr.dmc.done
            //|| t->cpu_period == 0
        );

    if(! skip)
    {
        t->count -= t->decrement; /* Take one sample */

        if(t->count <= 0)
        {
            // FIXME: implement DMC silence flag
            int dmc_silence = 0;
            int dmc_bit;

            t->count = t->reload_count + t->count;
            if(t->attr.dmc.sample_bitcount == 0)
            {
                dmc_next_sample(apu);
                if(t->attr.dmc.done)
                    dmc_silence = 1;
            }

            if(! dmc_silence)
            {
                dmc_bit = (t->attr.dmc.sample & 1);
                if(dmc_bit)
                {
                    // Increment
                    if(t->dac_value >= 126)
                    {
                        t->dac_value = 127;
                    }
                    else
                    {
                        t->dac_value += 2;
                    }
                }
                else
                {
                    // Decrement
                    if(t->dac_value <= 2)
                    {
                        t->dac_value = 0;
                    }
                    else
                    {
                        t->dac_value -= 2;
                    }
                }
            }

            // FIXME: is this clocked unconditionally or only if (! dmc_silence)?
            TRACE("%02X [%d] %02d\n", t->attr.dmc.sample, t->attr.dmc.sample_bitcount, t->dac_value);
            t->attr.dmc.sample_bitcount--;
            t->attr.dmc.sample >>= 1;
        }
    }

    return t->dac_value;
}

void tone_update(ToneChannel_t *t, int cpu_period, const ApuDuty_t *duty)
{
    // FIXME: using floating point borks Zelda intro music
    //float freq = (float) NES_NTSC_CYCLES_PER_SECOND / (duty->divider * cpu_period);
    int freq = NES_NTSC_CYCLES_PER_SECOND / (duty->divider * cpu_period);
    t->cpu_period = cpu_period;
    t->decrement = (duty->mask + 1);
    t->reload_count = (AUDIO_SAMPLE_RATE / freq);

    t->duty.mask = duty->mask;
    t->duty.values = duty->duty_cycle;
}

static void
clock_length_counter(NESAPU_t *apu, ToneChannel_t *chan, int halt_or_reload)
{
    if(! chan->enable)
    {
        chan->length_count = 0;
    }
    else if(! halt_or_reload && chan->length_count > 0)
    {
        chan->length_count--;
        {
            INFO("%s Chan [%8s] count: %2d\n", apu->get_state_func(apu->arg_ptr), chan->name, chan->length_count);
        }
    }
}


static void
nes_apu_clock_length_counters(NESAPU_t *apu)
{
    // Clock the length counters

    clock_length_counter(apu, &apu->state.square[0], SQUARE1(halt_or_reload));

    clock_length_counter(apu, &apu->state.square[1], SQUARE2(halt_or_reload));

    clock_length_counter(apu, &apu->state.triangle, TRIANGLE(halt_or_control));

    clock_length_counter(apu, &apu->state.noise, NOISE(halt_or_reload));
}

uint8_t
nes_apu_read(NESAPU_t *apu, uint16_t addr)
{
    ApuStatusRegs_t data;
    data.word = 0;
#define READ_STATUS(FIELD) data.bits.FIELD

    switch(addr)
    {
        case APU_ADDR_STATUS_4015:
            data.word = apu->state.status_regs.word;

            READ_STATUS(__reserved) = 0;
            READ_STATUS(dmc)        = (apu->state.dmc.attr.dmc.sample_bytes != 0);
            READ_STATUS(noise)      = (apu->state.noise.length_count != 0);
            READ_STATUS(triangle)   = (apu->state.triangle.length_count != 0);
            READ_STATUS(square2)    = (apu->state.square[1].length_count != 0);
            READ_STATUS(square1)    = (apu->state.square[0].length_count != 0);

            INFO("%s Status read: %02X => %c%c | %c%c%c%c%c\n",
                  apu->get_state_func(apu->arg_ptr),
                  data.word,
                  READ_STATUS(dmc_irq)   ? 'D' : ' ',
                  READ_STATUS(frame_irq) ? 'F' : ' ',
                  READ_STATUS(dmc)       ? 'd' : ' ',
                  READ_STATUS(noise)     ? 'n' : ' ',
                  READ_STATUS(triangle)  ? 't' : ' ',
                  READ_STATUS(square2)   ? '2' : ' ',
                  READ_STATUS(square1)   ? '1' : ' ');

            STATUS(dmc_irq) = 0; // Clear the DMC interrupt flag
            STATUS(frame_irq) = 0; // Clear the Frame interrupt flag
            break;
    }

    return data.word;
}

static void
square_reg_write(SquareRegs_t *regs, ToneChannel_t *chan,
                 uint8_t reg_offset, uint8_t data)
{
    regs->word[reg_offset] = data;

    // Try Contra and Metroid for examples of when this is written
    if(reg_offset != 1)
        tone_update(chan, (regs->bits.period + 1)<<1, &APU_SQUARE_DUTY[regs->bits.duty]);

    if(reg_offset == 1)
        chan->attr.square.sweep_reload = 1;

    if(reg_offset == 3)
    {
        if(chan->enable)
        {
            chan->length_count = LENGTH_COUNT[regs->bits.length_index];

            chan->envelope_start_flag = 1;
        }
    }
}

static void
triangle_reg_write(NESAPU_t *apu, uint8_t offset, uint8_t data)
{
    ToneChannel_t *chan = &apu->state.triangle;
    apu->state.triangle_regs.word[offset] = data;

    if(offset == 3)
    {
        chan->attr.triangle.halt = 1;
        if(chan->enable)
        {
            chan->length_count = LENGTH_COUNT[TRIANGLE(length_index)];
        }

        chan->volume = 1;
    }

    if(offset >= 2)
        tone_update(chan, TRIANGLE(period) + 1, &APU_TRIANGLE);
}

static void
noise_reg_write(NESAPU_t *apu, uint8_t offset, uint8_t data)
{
    ToneChannel_t *chan = &apu->state.noise;

    apu->state.noise_regs.word[offset] = data;

    // FIXME: is this update unconditional?
    if(offset == 2)
    {
        tone_update(chan, NOISE_PERIOD[NOISE(period_index)] + 1, &APU_NOISE);
        chan->attr.noise.short_mode = NOISE(short_mode);
    }

    if(offset == 3)
    {
        if(chan->enable)
        {
            chan->length_count = LENGTH_COUNT[NOISE(length_index)];
            chan->envelope_start_flag = 1;
        }
    }
}

static void
dmc_reg_write(NESAPU_t *apu, uint8_t offset, uint8_t data)
{
    apu->state.dmc_regs.word[offset] = data;

    if(offset == 0)
    {
        tone_update(&apu->state.dmc, DMC_PERIOD[DMC(period_index)], &APU_DMC);
        TRACE("DMC period: %d, decrement: %d, reload_count: %d\n",
              apu->state.dmc.cpu_period, apu->state.dmc.decrement, apu->state.dmc.reload_count);
    }
    else if(offset == 1)
    {
        apu->state.dmc.dac_value = DMC(dac);
        TRACE("DMC DAC update => %02X | %d\n", apu->state.dmc.dac_value, apu->state.dmc.dac_value);
    }
}

void
nes_apu_write(NESAPU_t *apu, uint16_t addr, uint8_t data)
{
    INFO("APU[$%04X] <= $%02X\n", addr, data);

    switch(addr)
    {
        case APU_ADDR_SQUARE1_0:
        case APU_ADDR_SQUARE1_1:
        case APU_ADDR_SQUARE1_2:
        case APU_ADDR_SQUARE1_3:
            square_reg_write(&apu->state.square1_regs, &apu->state.square[0], addr & 3, data);
            break;

        case APU_ADDR_SQUARE2_0:
        case APU_ADDR_SQUARE2_1:
        case APU_ADDR_SQUARE2_2:
        case APU_ADDR_SQUARE2_3:
            square_reg_write(&apu->state.square2_regs, &apu->state.square[1], addr & 3, data);
            break;

        case APU_ADDR_TRIANGLE_0:
        case APU_ADDR_TRIANGLE_1:
        case APU_ADDR_TRIANGLE_2:
        case APU_ADDR_TRIANGLE_3:
            triangle_reg_write(apu, addr & 3, data);
            break;

        case APU_ADDR_NOISE_0:
        case APU_ADDR_NOISE_1:
        case APU_ADDR_NOISE_2:
        case APU_ADDR_NOISE_3:
            noise_reg_write(apu, addr & 3, data);
            break;

        case APU_ADDR_DMC_0:
        case APU_ADDR_DMC_1:
        case APU_ADDR_DMC_2:
        case APU_ADDR_DMC_3:
            dmc_reg_write(apu, addr & 3, data);
            break;

        case APU_ADDR_STATUS_4015:
        {
            int save_dmc_irq = STATUS(dmc_irq);

            apu->state.status_regs.word = data; // Update status
            apu->state.frame_irq = 0; // Clear the frame IRQ

            INFO("%s Status write: %02X => %c%c%c%c%c\n",
                  apu->get_state_func(apu->arg_ptr),
                  data,
                  STATUS(dmc)      ? 'd' : ' ',
                  STATUS(noise)    ? 'n' : ' ',
                  STATUS(triangle) ? 't' : ' ',
                  STATUS(square2)  ? '2' : ' ',
                  STATUS(square1)  ? '1' : ' ');

            STATUS(dmc_irq) = save_dmc_irq;

            channel_enable(&apu->state.square[0], STATUS(square1));
            channel_enable(&apu->state.square[1], STATUS(square2));
            channel_enable(&apu->state.triangle, STATUS(triangle));
            channel_enable(&apu->state.noise, STATUS(noise));

            int dmc_enable = STATUS(dmc);
            if(dmc_enable)
            {
                if(apu->state.dmc.attr.dmc.sample_bytes == 0)
                {
                    // Restart the DMC
                    dmc_latch_sample_address(apu);
                }
                else
                {
                    dmc_enable = 0;
                }
            }
            else
            {
                apu->state.dmc.attr.dmc.sample_bytes = 0;
            }

            channel_enable(&apu->state.dmc, dmc_enable);
            break;
        }

        case APU_ADDR_SEQUENCER_4017:
        {
            unsigned frame_sequencer_mode = (data >> 7) & 1;
            int int_disable = (data >> 6) & 1;

            if(int_disable)
            {
                STATUS(frame_irq) = 0;
            }

            if(frame_sequencer_mode == MODE_192HZ)
            {
                int_disable = 1;
            }

            if(apu->state.frame_sequencer_mode != frame_sequencer_mode)
                INFO("%s Frame sequencer mode changed: %dHz\n", apu->get_state_func(apu->arg_ptr), (frame_sequencer_mode == MODE_192HZ) ? 192 : 240);

            apu->state.frame_sequencer_mode = frame_sequencer_mode;
            apu->state.disable_frame_irq_mask = int_disable;

            // Reset the frame sequencer dividers
            if(apu->state.frame_sequencer_mode == MODE_240HZ)
            {
                apu->state.clock240 = 4;
            }
            else
            {
                apu->state.clock240 = 0;
                // FIXME: this borks smb1 jump sweep and/or Zelda intro music
                //apu->state.clock240 = 1;
                //nes_apu_clock_length_counters(apu);
            }

            break;
        }

        default:
            break;
    }
}

static void
check_sweep_overflow(SquareRegs_t *regs, ToneChannel_t *chan)
{
    // NOTE: this is a crazy corner case for supporting:
    // * Contra sweep silencing on explosion
    // * Dragon Warrior 4 intro
    // * SMB1 downpipe silencing
    int period = regs->bits.period;
    int offset = (period >> regs->bits.shift);
    int ones_complement = 0;

    if(regs->bits.negative)
    {
        offset = ~offset;

        if(! ones_complement)
            offset++;
    }

    period += offset;

    if(regs->bits.period < 8
       || period >= 0x800)
    {
        // Sweep overflow => force silence

        //regs->bits.enable_sweep = 0;
        chan->attr.square.sweep_silence = 1;
    }
    else
    {
        chan->attr.square.sweep_silence = 0;
    }
}

static float
nes_apu_dac_mix(NESAPU_t *apu)
{
    float square1 = tone_wave(&apu->state.square[0]);
    float square2 = tone_wave(&apu->state.square[1]);
    float triangle = tone_wave(&apu->state.triangle);
    float noise = noise_wave(&apu->state.noise);
    float dmc = dmc_wave(apu);

    if(apu->options.disable_square1)
        square1 = 0;
    if(apu->options.disable_square2)
        square2 = 0;
    if(apu->options.disable_triangle)
        triangle = 0;
    if(apu->options.disable_noise)
        noise = 0;
    if(apu->options.disable_dmc)
        dmc = 0;

    float square_out = 95.88 / (8128 / (square1 + square2) + 100);
    float tnd = (triangle / 8227 + noise / 12241 + dmc / 22638);
    float tnd_out = 0;
    if(tnd != 0)
    {
        tnd_out = 159.79 / (1 / tnd + 100);
    }

    return (square_out + tnd_out);
}

void
nes_apu_fill_buffer(NESAPU_t *apu, unsigned fill_size)
{
    unsigned i;
    AudioBuffer_t *ab = &apu->audio_buffer;

    float buf[fill_size];
    int last = 0;

    for(i = 0; i < fill_size; i++)
    {
        float dac = nes_apu_dac_mix(apu);

        buf[i] = dac;
#if 1
        apu->sample_total += dac;
#endif
    }

    // HAKK
    apu->fill_count += fill_size;

    if(apu->fill_count >= 735)
    {
#if 1
        apu->sample_average = apu->sample_total / 735;
        apu->sample_total = 0;
#endif
        apu->fill_count -= 735;
        last = 1;
    }

    audio_buffer_lock(ab);

    for(i = 0; i < fill_size; i++)
    {
        ab->buffer[ab->wr_index] = buf[i];

        if(apu->options.dump_wav)
        {
            wav_output(buf[i]);
        }

        ab->wr_index = (ab->wr_index + 1) % AUDIO_BUFFER_SIZE;
    }

    if(last)
    {
        audio_buffer_signal(ab);
    }

    audio_buffer_unlock(ab);
}

static void
envelope_clock(ToneChannel_t *chan, int vol_env_period,
               int halt_or_reload, int constant_volume)
{
    if(chan->envelope_start_flag)
    {
        chan->volume = 0xf;
        chan->linear_count = 0;
        chan->envelope_start_flag = 0;
    }
    else
    {
        // Decrement when we hit the decay value
        chan->linear_count++;
        if(chan->linear_count >= vol_env_period)
        {
            chan->linear_count = 0;
            if(chan->volume > 0)
            {
                chan->volume--;
            }
            else if(halt_or_reload)
            {
                // Reload
                chan->volume = 0xf;
            }
        }
    }

    if(constant_volume)
        chan->volume = vol_env_period;
}

static void
nes_apu_clock_envelopes(NESAPU_t *apu)
{
    envelope_clock(&apu->state.square[0],
                   SQUARE1(vol_env_period), SQUARE1(halt_or_reload), SQUARE1(env_decay_disable));

    envelope_clock(&apu->state.square[1],
                   SQUARE2(vol_env_period), SQUARE2(halt_or_reload), SQUARE2(env_decay_disable));

    envelope_clock(&apu->state.noise,
                   NOISE(vol_env_period),   NOISE(halt_or_reload),   NOISE(env_decay_disable));
}

static void
nes_apu_clock_triangle_linear_counter(NESAPU_t *apu)
{
    // Triangle
    if(apu->state.triangle.attr.triangle.halt)
    {
        apu->state.triangle.linear_count = TRIANGLE(linear_load);
    }
    else if(apu->state.triangle.linear_count > 0)
    {
        apu->state.triangle.linear_count--;
    }

    apu->state.triangle.silence = (apu->state.triangle.linear_count == 0);

    if(! TRIANGLE(halt_or_control))
    {
        apu->state.triangle.attr.triangle.halt = 0;
    }
}

static void
clock_square_sweep(SquareRegs_t *regs, ToneChannel_t *chan, int ones_complement)
{
    int period = regs->bits.period;
    int shift = regs->bits.shift;
    int offset = (period >> shift);

    ++chan->attr.square.sweep_count;
    if(chan->attr.square.sweep_count > regs->bits.sweep_period)
    {
        chan->attr.square.sweep_count = 0;

        if(regs->bits.period >= 8 &&
           regs->bits.enable_sweep &&
           shift > 0 &&
           chan->length_count != 0)
        {
            if(regs->bits.negative)
            {
                offset = ~offset;

                if(!ones_complement)
                    offset++;
            }

            period += offset;

            if(period <= 0x7ff)
            {
                regs->bits.period = period;
                tone_update(chan, (period + 1)<<1, &APU_SQUARE_DUTY[regs->bits.duty]);
            }
        }
    }

    check_sweep_overflow(regs, chan);

    if(chan->attr.square.sweep_reload)
    {
        chan->attr.square.sweep_count = 0;
        chan->attr.square.sweep_reload = 0;
    }
}

static void
nes_apu_clock_sweep(NESAPU_t *apu)
{
    clock_square_sweep(&apu->state.square1_regs, &apu->state.square[0], 1);
    clock_square_sweep(&apu->state.square2_regs, &apu->state.square[1], 0);
}

unsigned
nes_apu_240hz(NESAPU_t *apu)
{
    int mode240 = (apu->state.frame_sequencer_mode == MODE_240HZ);

    /*

    f = set interrupt flag
    l = clock length counters and sweep units
    e = clock envelopes and triangle's linear counter

    mode 0: 4-step  effective rate (approx)
    ---------------------------------------
        - - - f      60 Hz
        - l - l     120 Hz
        e e e e     240 Hz

    mode 1: 5-step  effective rate (approx)
    ---------------------------------------
        - - - - -   (interrupt flag never set)
        l - l - -    96 Hz
        e e e e -   192 Hz
    */

    if(apu->state.clock240 <= 3)
    {
        nes_apu_clock_envelopes(apu);
        nes_apu_clock_triangle_linear_counter(apu);
    }

    if(mode240)
    {
        switch(apu->state.clock240)
        {
            case 1:
            case 3:
                nes_apu_clock_length_counters(apu);
                nes_apu_clock_sweep(apu);
                break;

            default:
                break;
        }
    }
    else
    {
        switch(apu->state.clock240)
        {
            case 0:
            case 2:
                nes_apu_clock_length_counters(apu);
                nes_apu_clock_sweep(apu);
                break;

            default:
                break;
        }
    }

    apu->state.clock240++;

    if(mode240)
    {
        if(apu->state.clock240 >= 4)
        {
            apu->state.clock240 = 0;

            // 60Hz Frame Sequencer Interrupt
            if(! STATUS(frame_irq))
            {
                INFO("APU frame interrupt!\n");
            }
            apu->state.frame_irq = 1;
        }
    }
    else
    {
        if(apu->state.clock240 >= 5)
            apu->state.clock240 = 0;
    }

    STATUS(frame_irq) = apu->state.frame_irq & (! apu->state.disable_frame_irq_mask);
    return STATUS(frame_irq) | STATUS(dmc_irq);
}
