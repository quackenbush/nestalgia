/*
  SDL Sound Example

  written by WolfCoder (2010)
*/

/* Includes */
#include <stdio.h>
#include <SDL/SDL.h>
#include <SDL/SDL_audio.h>

#define SAMPLE_RATE 44100

/* Middle C Square Generator */
//#define FREQ1 261.626

/*
       1209 1336 1477 1633
       -------------------
  697  | 1    2    3    A
  770  | 4    5    6    B
  852  | 7    8    9    C
  941  | *    0    #    D
*/

typedef enum
{
    TONE_0 = 0,
    TONE_1,
    TONE_2,
    TONE_3,
    TONE_4,
    TONE_5,
    TONE_6,
    TONE_7,
    TONE_8,
    TONE_9,

    TONE_DIAL,
    TONE_RING,
} PhoneTone_t;

static const struct
{
    uint16_t f1;
    uint16_t f2;
}
PHONE_TONES[] =
{
    {941, 1336}, // 0
    {697, 1209}, // 1
    {697, 1336}, // 2
    {697, 1477}, // 3
    {770, 1209}, // 4
    {770, 1336}, // 5
    {770, 1477}, // 6
    {852, 1209}, // 7
    {852, 1336}, // 8
    {852, 1477}, // 9
    {440, 350},  // DIAL
    {440, 480},  // RING
};

typedef struct
{
    int freq;
    int count;
    int max;
} ToneStatus_t;

struct
{
    PhoneTone_t tone;
    ToneStatus_t s1;
    ToneStatus_t s2;
    unsigned quiet;
} status;

#include <math.h>
#define PI 3.14159

float square_wave(ToneStatus_t *t)
{
    t->count++; /* Take one sample */

    if(t->count >= t->max)
        t->count = 0;

    return (t->count < t->max/2) ? -1 : 1;
}

float sine_wave(ToneStatus_t *t)
{
    t->count++; /* Take one sample */

    if(t->count >= t->max)
        t->count = 0;

    return sin(2 * PI * (float) t->count / t->max);
}

void tone_status_init(ToneStatus_t *t, int freq)
{
    t->freq = freq;
    t->max = (SAMPLE_RATE / t->freq);
    t->count = 0;
}

void phone_tone_set(PhoneTone_t tone)
{
    status.tone = tone;
    tone_status_init(&status.s1, PHONE_TONES[tone].f1);
    tone_status_init(&status.s2, PHONE_TONES[tone].f2);
    status.quiet = 0;
}

#define VOLUME 0.1

#define FUNC sine_wave
//#define FUNC square_wave

//#define CLIP 1.8

static inline float clip(float a)
{
#ifdef CLIP
    if(a < -CLIP)
        return -CLIP;
    if(a > CLIP)
        return CLIP;
#endif
    return a;
}

int16_t freq_gen(void)
{
    if(status.quiet)
        return 0;

    return clip(FUNC(&status.s1) +
                FUNC(&status.s2)) / 2 * (32767 * VOLUME);
}

/* Buffer fill-upper */
void fill_audio(void *data, uint8_t *stream, int len)
{
    int16_t *buff;
    int i;

    buff = (int16_t*) stream;
    len /= sizeof(buff[0]); /* Because we're now using shorts */

    for(i = 0; i < len; i++)
    {
        buff[i] = freq_gen();
    }
}

/* Open the audio device to what we want */
void open_audio(void)
{
    SDL_AudioSpec as;
    SDL_AudioSpec actual;
    /* Open SDL */
    SDL_Init(SDL_INIT_AUDIO);
    /* Fill out what we want */
    as.freq = SAMPLE_RATE;
    as.format = AUDIO_S16SYS;
    //as.format = AUDIO_S16LSB;
    as.channels = 1;
    as.samples = 4096;
    as.callback = fill_audio;

    printf("Opening @ %d hz\n", as.freq);

    /* Get it */
    if(SDL_OpenAudio(&as,&actual))
    {
        fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
        abort();
    }

    SDL_PauseAudio(0); // Unpause
    printf("Playing\n");

    unsigned i;
#define NUMBER_TIME 200
#define QUIET_TIME 400

    const struct
    {
        PhoneTone_t tone;
        int duration_ms;
        int quiet_ms;
    }
    NUMBER[] =
    {
        { TONE_DIAL, 750, 0},

        { 5, NUMBER_TIME, QUIET_TIME },
        { 1, NUMBER_TIME, QUIET_TIME },
        { 2, NUMBER_TIME, QUIET_TIME + 200 },

        { 5, NUMBER_TIME, QUIET_TIME },
        { 7, NUMBER_TIME, QUIET_TIME },
        { 3, NUMBER_TIME, QUIET_TIME + 200},

        { 1, NUMBER_TIME, QUIET_TIME },
        { 9, NUMBER_TIME, QUIET_TIME },
        { 0, NUMBER_TIME, QUIET_TIME },
        { 5, NUMBER_TIME, QUIET_TIME + 500},

        { TONE_RING, 1000, 2000},

        { TONE_RING, 1000, 0},
    };

    SDL_Delay(500);
    for(i = 0; i < sizeof(NUMBER) / sizeof(NUMBER[0]); i++)
    {
        phone_tone_set(NUMBER[i].tone);
        SDL_Delay(NUMBER[i].duration_ms);
        status.quiet = 1;
        SDL_Delay(NUMBER[i].quiet_ms);
    }
}

/* Clean up things and close device */
void close_audio(void)
{
    SDL_CloseAudio();
    SDL_Quit();
}

int main(int argc, char *argv[])
{
    open_audio();

    /* Get a character */
    fgetc(stdin);

    close_audio();
    printf("Done\n");

    return 0;
}
