#include <SDL/SDL.h>
#include <stdint.h>

#include "platform_audio.h"
#include "audio.h"
#include "audio_buffer.h"
#include "log.h"

#define VOLUME 1.0

#define LOG(...) _LOG(APU, __VA_ARGS__)

static struct
{
    unsigned open;
    unsigned playing;
    AudioBuffer_t *audio_buffer;
} status = {0};

static void
sdl_fill_audio(void *data, uint8_t *stream, int bytes)
{
    AudioBuffer_t *ab = (AudioBuffer_t *) data;
    AUDIO_TYPE *out = (AUDIO_TYPE *) stream;
    int i;
    int len = bytes / sizeof(AUDIO_TYPE);

    if(len > 0)
    {
        if(! status.playing)
        {
            for(i = 0; i < len; i++)
            {
                out[i] = 0;
            }

            return;
        }

        audio_buffer_lock(ab);

        for(i = 0; i < len; i++)
        {
            if(ab->rd_index == ab->wr_index)
            {
                audio_buffer_wait(ab);
            }

            if(ab->rd_index != ab->wr_index)
            {
                out[i] = ab->buffer[ab->rd_index] * (AUDIO_RANGE * VOLUME);
            }
            else
            {
                // Underflow
                out[i] = 0;
            }
            ab->rd_index = (ab->rd_index + 1) % AUDIO_BUFFER_SIZE;
        }

#ifdef AUDIO_BUFFER_DEBUG
        ab->rd_size += len;
#endif

        audio_buffer_unlock(ab);
    }
}

static void
sdl_audio_pause(int paused)
{
    ASSERT(status.open, "Audio is closed\n");
    status.playing = ! paused;

    LOG("paused(%d)\n", paused);
}

static void
sdl_audio_open(AudioBuffer_t *audio_buffer)
{
    SDL_AudioSpec as;
    SDL_AudioSpec actual;

    ASSERT(! status.open, "Audio already open\n");

    audio_buffer_init(audio_buffer);

    if(SDL_Init(SDL_INIT_AUDIO) < 0)
    {
        fprintf(stderr, "Couldn't init audio: %s\n", SDL_GetError());
        abort();
    }

    as.freq = AUDIO_SAMPLE_RATE;
    as.format = AUDIO_FORMAT;
    as.channels = 1;
    as.samples = /*AUDIO_SAMPLE_RATE/60*/512 * sizeof(AUDIO_TYPE);
    as.callback = sdl_fill_audio;
    as.userdata = audio_buffer;

    printf("Opening SDL audio @ %d hz\n", as.freq);

    if(SDL_OpenAudio(&as,&actual))
    {
        fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
        abort();
    }

    status.open = 1;
    status.audio_buffer = audio_buffer;

    // Unpause the audio
    sdl_audio_pause(0);
    SDL_PauseAudio(0);
}

static void
sdl_audio_close(void)
{
    if(status.open)
    {
        status.open = 0;
        status.playing = 0;
        audio_buffer_lock(status.audio_buffer);
        audio_buffer_signal(status.audio_buffer);
        audio_buffer_unlock(status.audio_buffer);

        SDL_CloseAudio();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);

        audio_buffer_destroy(status.audio_buffer);
        status.audio_buffer = NULL;
    }
}

const AudioDescriptor_t SDL_AUDIO =
{
    sdl_audio_open,
    sdl_audio_pause,
    sdl_audio_close
};
