/**
 * Demonstration of generation of a WAV file with a generated
 * sine wave sound.
 *
 * More WAV format information can be found here:
 *
 * http://www.dragonwins.com/wav/
 *
 * This code is hereby granted to the public domain.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#define SOUND_DURATION      10.0   // seconds
#define VOLUME              0.2    // 0.0 (silent) to 1.0 (max)

#define CHANNELS            1
#define SAMPLES_PER_SECOND  44000
#define BITS_PER_SAMPLE     16     // make 8, 16, 24, or 32

// I thought I should be able to make BITS_PER_SAMPLE
// anything I wanted, but I get bad playback for non-round
// values.  I'm confident I'm calculating bytes_per_sample
// correctly, and I'm confident I'm calculating the volume
// range correctly, so I'm not sure what gives.  My player
// seems to assume that the bits per sample is the bytes per
// sample divided by 8, and maybe that's just true for WAV.

/* RIFF chunk information structure: */
typedef struct
{
    uint8_t header[4];
    uint8_t size[4];
    uint8_t *payload;
} RiffHeader_t;

typedef struct
{
    uint32_t size;  /* size of the chunk not counting the header */
    RiffHeader_t data; /* header and body of chunk */
} RiffChunk_t;

#define RIFF_HEADER_SIZE (8) /* bytes */

/**
 * Write out a little-endian unsigned 32-bit value
 */
void little_endian_u32(uint8_t *buf, uint32_t v)
{
    buf[0] = v&0xff;
    buf[1] = (v>>8)&0xff;
    buf[2] = (v>>16)&0xff;
    buf[3] = (v>>24)&0xff;
}

/**
 * Write out a little-endian unsigned 24-bit value
 */
void little_endian_u24(uint8_t *buf, uint32_t v)
{
    buf[0] = v&0xff;
    buf[1] = (v>>8)&0xff;
    buf[2] = (v>>16)&0xff;
}

/**
 * Write out a little-endian unsigned 16-bit value
 */
void little_endian_u16(uint8_t *buf, unsigned int v)
{
    buf[0] = v&0xff;
    buf[1] = (v>>8)&0xff;
}

/**
 * Construct the main RIFF chunk
 *
 * This is the outermost chunk which will encapsulate the WAV
 * subchunks.
 */
void make_riff_chunk(RiffChunk_t *riff, RiffChunk_t *fmt, RiffChunk_t *data)
{
    uint32_t total_size;

    riff->data.payload = malloc(4);
    memcpy(riff->data.header, "RIFF", 4);
    riff->size = 4;
    total_size = riff->size +
        (fmt->size + RIFF_HEADER_SIZE) +
        (data->size + RIFF_HEADER_SIZE);
    little_endian_u32(riff->data.size, total_size);  /* chunk size */
    memcpy(riff->data.payload, "WAVE", 4);              /* RIFF type */
}

/**
 * Construct the fmt subchunk
 *
 * This subchunk contains meta-information about the WAV file, number
 * of channels, data rate, etc.
 */
void make_fmt_chunk(RiffChunk_t *fmt)
{
    uint32_t data_rate = CHANNELS * SAMPLES_PER_SECOND;
    unsigned int bytes_per_sample = (BITS_PER_SAMPLE-1) / 8 + 1;
    unsigned int block_alignment = CHANNELS * bytes_per_sample;

    fmt->data.payload = malloc(16);

    memcpy(fmt->data.header, "fmt ", 4);                 /* chunk type */
    fmt->size = 16; // 18 for the 0 extra data size?
    little_endian_u32(fmt->data.size, fmt->size);           /* size */
    little_endian_u16(fmt->data.payload-8+8, 1);                   /* comp type, 1==PCM */
    little_endian_u16(fmt->data.payload-8+10, CHANNELS);           /* channels */
    little_endian_u32(fmt->data.payload-8+12, SAMPLES_PER_SECOND); /* slice rate */
    little_endian_u32(fmt->data.payload-8+16, data_rate);          /* data rate */
    little_endian_u16(fmt->data.payload-8+20, block_alignment);    /* block alignment */
    little_endian_u16(fmt->data.payload-8+22, BITS_PER_SAMPLE);    /* sample depth */
    //little_endian_u16(fmt->data.payload-8+24, 0);                  /* extra data size */
}

static uint8_t *
tone(uint8_t *p, unsigned sample_count, unsigned bytes_per_sample, unsigned f1, unsigned f2)
{
    unsigned i;
    unsigned channel;
    for (i = 0; i < sample_count; i++)
    {
        /* calculate the sample value: */
        float v = 0;
        if(f1 > 0)
            v  = sin(2*3.14159 * i / (SAMPLES_PER_SECOND * 1.0 / f1)) * VOLUME;
        if(f2 > 0)
            v += sin(2*3.14159 * i / (SAMPLES_PER_SECOND * 1.0 / f2)) * VOLUME;

        /* remap v from -1..1 to proper range */
        int sample;
        if (BITS_PER_SAMPLE <= 8) {
            // unsigned in the range [0..max)
            long long range_top = (1LL << BITS_PER_SAMPLE) - 1;
            sample = ((v+1)/2)*range_top;
        } else {
            // signed in the range [-max/2..max/2)
            //int64_t range_top = (1LL << BITS_PER_SAMPLE) - 1;
            //long range_top2 = 1 << (BITS_PER_SAMPLE - 1);
            //sample = ((long)(((v+1)/2)*range_top)) - range_top2;
            int32_t range_top = (1LL << BITS_PER_SAMPLE) - 1;
            int32_t range_top2 = 1 << (BITS_PER_SAMPLE - 1);
            sample = ((((v+1)/2)*range_top)) - range_top2;
            printf("%d\n", sample);
        }

        /* write sample to all channels: */
        for (channel = 0; channel < CHANNELS; channel++) {
            switch (bytes_per_sample) {
                case 1:
                    *p = sample;
                    break;
                case 2:
                    little_endian_u16(p, sample);
                    break;
                case 3:
                    little_endian_u24(p, sample);
                    break;
                case 4:
                    little_endian_u32(p, sample);
                    break;
                default:
                    fprintf(stderr, "bad bytes per sample!\n");
                    exit(2);
            }
            p += bytes_per_sample;
        }
    }

    return p;
}

/**
 * Construct the data subchunk
 *
 * Contains the raw sound data
 */
void make_data_chunk(RiffChunk_t *data)
{
    uint32_t sample_count = SOUND_DURATION * SAMPLES_PER_SECOND;
    unsigned int bytes_per_sample = (BITS_PER_SAMPLE-1) / 8 + 1;

    uint32_t data_size = sample_count * bytes_per_sample * CHANNELS;

    uint8_t *p;

    memcpy(data->data.header, "data", 4);
    data->size = data_size;
    little_endian_u32(data->data.size, data_size);

    data->data.payload = malloc(data_size);
    p = data->data.payload;

/*
       1209 1336 1477 1633
       -------------------
  697  | 1    2    3    A
  770  | 4    5    6    B
  852  | 7    8    9    C
  941  | *    0    #    D
*/
    const struct
    {
        uint16_t f1;
        uint16_t f2;
    }
    TONES[] =
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

    uint8_t number[] = {5,1,2,5,7,3,1,9,0,5};
    unsigned i;

    p = tone(p, 1.0 * SAMPLES_PER_SECOND, bytes_per_sample, TONES[10].f1, TONES[10].f2);

    for(i = 0; i < sizeof(number); i++)
    {
        p = tone(p, 0.2 * SAMPLES_PER_SECOND, bytes_per_sample, TONES[number[i]].f1, TONES[number[i]].f2);
        p = tone(p, 0.3 * SAMPLES_PER_SECOND, bytes_per_sample, 0, 0);

        if(i == 2 || i == 5)
            p = tone(p, 0.2 * SAMPLES_PER_SECOND, bytes_per_sample, 0, 0);
    }

    p = tone(p, 0.5 * SAMPLES_PER_SECOND, bytes_per_sample, 0, 0);
    p = tone(p, 2.0 * SAMPLES_PER_SECOND, bytes_per_sample, TONES[11].f1, TONES[11].f2);
}

void write_chunk(RiffChunk_t *chunk, FILE *fp)
{
    fwrite(&chunk->data, 1, RIFF_HEADER_SIZE, fp);
    fwrite(chunk->data.payload, 1, chunk->size, fp);
}

/**
 * Write out the WAV file
 */
void write_output(RiffChunk_t *riff, RiffChunk_t *fmt, RiffChunk_t *data)
{
    FILE *fp;

    fp = fopen("tone.wav", "wb");
    if (fp == NULL) {
        printf("failed to open output file\n");
        exit(2);
    }

    write_chunk(riff, fp);
    write_chunk(fmt, fp);
    write_chunk(data, fp);

    fclose(fp);
}

/**
 * Main
 */
int main(void)
{
    RiffChunk_t riff, fmt, data;

    make_data_chunk(&data);
    make_fmt_chunk(&fmt);
    make_riff_chunk(&riff, &fmt, &data);

    write_output(&riff, &fmt, &data);

    free(riff.data.payload);
    free(fmt.data.payload);
    free(data.data.payload);

    return 0;
}
