#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "wav.h"
#include "endian.h"
#include "wav_audio.h"
#include "log.h"

#define VOLUME             1.0

#define CHANNELS           1
#define SAMPLES_PER_SECOND 44100
#define BITS_PER_SAMPLE    16 // make 8, 16, 24, or 32

#define AUDIO_RANGE (1 << (BITS_PER_SAMPLE - 1))
// FIXME: check malloc results

struct
{
    const char *filename;
    FILE *fp;

    RiffChunk_t riff;
    RiffChunk_t fmt;
    RiffChunk_t data;

    int sample_count;
} wav_state;

/**
 * Construct the main RIFF chunk
 *
 * This is the outermost chunk which will encapsulate the WAV
 * subchunks.
 */
static void
make_riff_chunk(RiffChunk_t *riff, RiffChunk_t *fmt, RiffChunk_t *data)
{
    uint32_t total_size;

    if(! riff->data.payload)
    {
        riff->data.payload = malloc(4);
    }

    memcpy(riff->data.id, "RIFF", 4);
    riff->size = 4;
    total_size = riff->size +
        (fmt->size + RIFF_HEADER_SIZE) +
        (data->size + RIFF_HEADER_SIZE);
    little_endian_u32(riff->data.size, total_size);  /* chunk size */
    memcpy(riff->data.payload, "WAVE", 4);           /* RIFF type */
}

/**
 * Construct the fmt subchunk
 *
 * This subchunk contains meta-information about the WAV file, number
 * of channels, data rate, etc.
 */
static void
make_fmt_chunk(RiffChunk_t *fmt)
{
    uint32_t data_rate = CHANNELS * SAMPLES_PER_SECOND;
    unsigned int bytes_per_sample = (BITS_PER_SAMPLE-1) / 8 + 1;
    unsigned int block_alignment = CHANNELS * bytes_per_sample;

    if(! fmt->data.payload)
    {
        fmt->data.payload = malloc(16);
    }

    memcpy(fmt->data.id, "fmt ", 4);                               /* chunk type */
    fmt->size = 16; // 18 for the 0 extra data size?
    little_endian_u32(fmt->data.size, fmt->size);                  /* size */
    little_endian_u16(fmt->data.payload-8+8, 1);                   /* comp type, 1==PCM */
    little_endian_u16(fmt->data.payload-8+10, CHANNELS);           /* channels */
    little_endian_u32(fmt->data.payload-8+12, SAMPLES_PER_SECOND); /* slice rate */
    little_endian_u32(fmt->data.payload-8+16, data_rate);          /* data rate */
    little_endian_u16(fmt->data.payload-8+20, block_alignment);    /* block alignment */
    little_endian_u16(fmt->data.payload-8+22, BITS_PER_SAMPLE);    /* sample depth */
    //little_endian_u16(fmt->data.payload-8+24, 0);                /* extra data size */
}

static void
make_data_chunk(RiffChunk_t *data, int sample_count)
{
    unsigned int bytes_per_sample = (BITS_PER_SAMPLE-1) / 8 + 1;

    uint32_t data_size = sample_count * bytes_per_sample * CHANNELS;

    memcpy(data->data.id, "data", 4);
    data->size = data_size;
    little_endian_u32(data->data.size, data_size);
}

static void
write_chunk(RiffChunk_t *chunk, int write_payload)
{
    fwrite(&chunk->data, 1, RIFF_HEADER_SIZE, wav_state.fp);
    if(write_payload)
        fwrite(chunk->data.payload, 1, chunk->size, wav_state.fp);
}

//uint32_t sample_count = SOUND_DURATION * SAMPLES_PER_SECOND;

static void
write_header(void)
{
    make_data_chunk(&wav_state.data, wav_state.sample_count);
    make_fmt_chunk(&wav_state.fmt);
    make_riff_chunk(&wav_state.riff, &wav_state.fmt, &wav_state.data);

    write_chunk(&wav_state.riff, 1);
    write_chunk(&wav_state.fmt, 1);
    write_chunk(&wav_state.data, 0);
}

void
wav_init(void)
{
    static const char *output_filename = "nes.wav";
    wav_state.filename = output_filename;
    wav_state.fp = fopen(output_filename, "wb");

    ASSERT(wav_state.fp, "Failed to open output file: %s\n", output_filename);

    write_header();
}

void
wav_destroy(void)
{
    if(wav_state.fp)
    {
        // Rewrite the header with the correct sample count
        rewind(wav_state.fp);
        write_header();

        fclose(wav_state.fp);

        NOTIFY("Wrote %d samples to %s\n", wav_state.sample_count, wav_state.filename);

        wav_state.fp = NULL;

        free(wav_state.riff.data.payload);
        free(wav_state.fmt.data.payload);

        memset(&wav_state, 0, sizeof(wav_state));
    }
}

void
wav_output(float value)
{
    // FIXME: assuming 16-bit
    uint16_t sample_value = value * (AUDIO_RANGE * VOLUME);

    ++wav_state.sample_count;
    fwrite(&sample_value, sizeof(sample_value), 1, wav_state.fp);
}
