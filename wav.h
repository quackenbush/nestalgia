#ifndef __wav_h__
#define __wav_h__

#include <stdint.h>

/* RIFF chunk information structure: */
typedef struct
{
    uint8_t id[4];
    uint8_t size[4];
    uint8_t *payload;
} RiffHeader_t;

typedef struct
{
    uint32_t size;  /* size of the chunk not counting the header */
    RiffHeader_t data; /* header and body of chunk */
} RiffChunk_t;

#define RIFF_HEADER_SIZE 8

#endif
