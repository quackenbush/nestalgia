#ifndef __nes_apu_h__
#define __nes_apu_h__

#include <stdint.h>
#include "audio_buffer.h"

// # of CPU cycles per DMA byte access
#define APU_DMA_CYCLES 4

/*
$4015   ---D.NT21   NES APU Status (write)
bit 4   ---D ----   If clear, the DMC's bytes remaining is set to 0, otherwise the DMC sample
                    is restarted only if the DMC's bytes remaining is 0.
bit 3   ---- N---   Noise channel's length counter enabled flag
bit 2   ---- -T--   Triangle channel's length counter enabled flag
bit 1   ---- --2-   Pulse channel 2's length counter enabled flag
bit 0   ---- ---1   Pulse channel 1's length counter enabled flag
Side effects        After the write, the DMC's interrupt flag is cleared

$4015   IF-D.NT21   NES APU Status (read)
bit 7   I--- ----   DMC interrupt flag
bit 6   -F-- ----   Frame interrupt flag
bit 4   ---D ----   DMC bytes remaining is non-zero
bit 3   ---- N---   Noise channel's length counter is non-zero
bit 2   ---- -T--   Triangle channel's length counter is non-zero
bit 1   ---- --2-   Pulse channel 2's length counter is non-zero
bit 0   ---- ---1   Pulse channel 1's length counter is non-zero
Side effects        Clears the frame interrupt flag after being read (but not the DMC interrupt
                    flag).  If an interrupt flag was set at the same moment of the read, it will
                    read back as 1 but it will not be cleared.

*/

typedef union
{
    uint8_t word[4];
    struct
    {
        // $4000/4 ddhe nnnn   duty, loop env/disable length, env disable, vol/env period
        unsigned vol_env_period     : 4;
        unsigned env_decay_disable  : 1;
        unsigned halt_or_reload     : 1;
        unsigned duty               : 2;

        //$4001/5 eppp nsss   enable sweep, period, negative, shift
        unsigned shift          : 3;
        unsigned negative       : 1;
        unsigned sweep_period   : 3;
        unsigned enable_sweep   : 1;

        //$4002/6 pppp pppp   period low
        //$4003/7       ppp   period high
        unsigned period         : 11;

        //$4003/7 llll l      length index
        unsigned length_index   : 5;
    } bits;

} SquareRegs_t;

typedef union
{
    uint8_t word[4];
    struct
    {
        // $4008   clll llll   halt/control, linear counter load
        unsigned linear_load      : 7;
        unsigned halt_or_control  : 1;

        unsigned __reserved     : 8;

        // $400A   pppp pppp   period low
        // $400B         ppp   period high
        unsigned period         : 11;

        // $400B   llll l      length index
        unsigned length_index   : 5;
    } bits;

} TriangleRegs_t;

typedef union
{
    uint8_t word[4];
    struct
    {
        // $400C   --he nnnn   loop env/disable length, env disable, vol/env period
        unsigned vol_env_period    : 4;
        unsigned env_decay_disable : 1;
        unsigned halt_or_reload    : 1;
        unsigned __reserved0       : 2;

        // $400D   ---- ----
        unsigned __reserved1       : 8;

        // $400E   s--- pppp   short mode, period index
        unsigned period_index      : 4;
        unsigned __reserved2       : 3;
        unsigned short_mode        : 1;

        // $400F   llll l---   length index
        unsigned __reserved3       : 3;
        unsigned length_index      : 5;
    } bits;

} NoiseRegs_t;

typedef union
{
    uint8_t word[4];
    struct
    {
        // $4010   il-- ffff   IRQ enable, loop, frequency index
        unsigned period_index   : 4;
        unsigned __reserved0    : 2;
        unsigned loop           : 1;
        unsigned irq_enable     : 1;

        // $4011   -ddd dddd   DAC
        unsigned dac            : 7;
        unsigned __reserved1    : 1;

        // $4012   aaaa aaaa   sample address
        unsigned sample_address : 8;

        // $4013   llll llll   sample length
        unsigned sample_length  : 8;

    } bits;

} DMCRegs_t;

typedef union
{
    uint8_t word;
    struct
    {
        unsigned square1       : 1; // 0    square wave channel 1
        unsigned square2       : 1; // 1    square wave channel 2
        unsigned triangle      : 1; // 2    triangle wave channel
        unsigned noise         : 1; // 3    noise channel
        unsigned dmc           : 1; // 4    DMC (see "DMC.TXT" for details)
        unsigned __reserved    : 1; // 5
        unsigned frame_irq     : 1; // 6    Frame Interrupt
        unsigned dmc_irq       : 1; // 7    IRQ status of DMC (see "DMC.TXT" for details)
    } bits;

} ApuStatusRegs_t;

typedef enum
{
    APU_ADDR_SQUARE1_0  = 0x4000,
    APU_ADDR_SQUARE1_1  = 0x4001,
    APU_ADDR_SQUARE1_2  = 0x4002,
    APU_ADDR_SQUARE1_3  = 0x4003,

    APU_ADDR_SQUARE2_0  = 0x4004,
    APU_ADDR_SQUARE2_1  = 0x4005,
    APU_ADDR_SQUARE2_2  = 0x4006,
    APU_ADDR_SQUARE2_3  = 0x4007,

    APU_ADDR_TRIANGLE_0 = 0x4008,
    APU_ADDR_TRIANGLE_1 = 0x4009,
    APU_ADDR_TRIANGLE_2 = 0x400A,
    APU_ADDR_TRIANGLE_3 = 0x400B,

    APU_ADDR_NOISE_0    = 0x400C,
    APU_ADDR_NOISE_1    = 0x400D,
    APU_ADDR_NOISE_2    = 0x400E,
    APU_ADDR_NOISE_3    = 0x400F,

    APU_ADDR_DMC_0      = 0x4010,
    APU_ADDR_DMC_1      = 0x4011,
    APU_ADDR_DMC_2      = 0x4012,
    APU_ADDR_DMC_3      = 0x4013,

    APU_ADDR_STATUS_4015     = 0x4015,

    APU_ADDR_SEQUENCER_4017  = 0x4017,
} ApuAddr_t;

typedef struct
{
    const char *name;
    int enable;

    uint8_t volume;
    uint8_t dac_value;
    int silence;

    uint16_t cpu_period;
    int count;
    int decrement;
    int reload_count;

    uint16_t length_count;
    uint16_t linear_count;

    int envelope_start_flag;

    struct
    {
        int mask;
        int index;
        const uint8_t *values;
    } duty;

    union
    {
        struct
        {
            uint8_t sweep_count;
            int sweep_reload;
            int sweep_silence;
        } square;

        struct
        {
            uint8_t short_mode;
            uint16_t shift_reg;
        } noise;

        struct
        {
            unsigned halt;
        } triangle;

        struct
        {
            uint8_t sample;
            uint8_t sample_bitcount;
            uint16_t sample_address;
            int16_t sample_bytes;
            int done;
        } dmc;

    } attr;

} ToneChannel_t;

typedef struct
{
    uint8_t (*read_mem_func)(uint16_t addr);
    void    (*increment_cycles_func)(void *ptr, int cycles);
    const char *(*get_state_func)(void *ptr);

    void *arg_ptr;

    AudioBuffer_t audio_buffer;

    int fill_count;

    struct
    {
        uint8_t clock240;
        ApuStatusRegs_t status_regs;

        enum
        {
            MODE_240HZ = 0,
            MODE_192HZ = 1,
        } frame_sequencer_mode;

        int disable_frame_irq_mask;
        int frame_irq;

        SquareRegs_t    square1_regs;
        SquareRegs_t    square2_regs;

        TriangleRegs_t  triangle_regs;
        NoiseRegs_t     noise_regs;
        DMCRegs_t       dmc_regs;

        ToneChannel_t square[2];
        ToneChannel_t triangle;
        ToneChannel_t noise;
        ToneChannel_t dmc;
    } state;

    struct
    {
        unsigned disable_square1;
        unsigned disable_square2;
        unsigned disable_triangle;
        unsigned disable_noise;
        unsigned disable_dmc;
        unsigned dump_wav;
    } options;

    float sample_total;
    float sample_average;
} NESAPU_t;

void nes_apu_init(NESAPU_t *apu);
void nes_apu_reset(NESAPU_t *apu);
void nes_apu_destroy(NESAPU_t *apu);

void nes_apu_pause(NESAPU_t *apu, int paused);

void nes_apu_write(NESAPU_t *apu, uint16_t addr, uint8_t data);
uint8_t nes_apu_read(NESAPU_t *apu, uint16_t addr);

unsigned nes_apu_240hz(NESAPU_t *apu);
//void nes_apu_fill_buffer(NESAPU_t *apu, int portion);
void nes_apu_fill_buffer(NESAPU_t *apu, unsigned);

#endif
