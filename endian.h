#ifndef __endian_h__
#define __endian_h__

#include <stdint.h>

static inline int
is_big_endian(void)
{
    int test_var = 1;
    uint8_t *test_endian = (uint8_t*)&test_var;

    return (test_endian[0] == 0);
}

/**
 * Write out a little-endian unsigned 32-bit value
 */
static inline void
little_endian_u32(uint8_t *buf, uint32_t v)
{
    buf[0] = v&0xff;
    buf[1] = (v>>8)&0xff;
    buf[2] = (v>>16)&0xff;
    buf[3] = (v>>24)&0xff;
}

/**
 * Write out a little-endian unsigned 16-bit value
 */
static inline void
little_endian_u16(uint8_t *buf, uint16_t v)
{
    buf[0] = v&0xff;
    buf[1] = (v>>8)&0xff;
}

#endif
