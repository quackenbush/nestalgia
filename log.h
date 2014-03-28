#ifndef __log_h__
#define __log_h__

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h> // needed for the uint32_t below (ugh)

#ifndef min
#define min(A, B) ((A) < (B) ? (A) : (B))
#endif

#ifndef max
#define max(A, B) ((A) > (B) ? (A) : (B))
#endif

extern FILE *debug_fp;
extern FILE *info_fp;
extern uint32_t log_zone_mask;

#define LOG_ZONES    \
    LOG_ZONE(MAIN)   \
    LOG_ZONE(MISC)   \
    LOG_ZONE(6502)   \
    LOG_ZONE(C64)    \
    LOG_ZONE(MEM)    \
    LOG_ZONE(NES)    \
    LOG_ZONE(MAPPER) \
    LOG_ZONE(PPU)    \
    LOG_ZONE(APU)    \
    LOG_ZONE(DISPLAY)

// FIXME: might need to enforce that the first lzName is at entry 0
enum LogZone
{
#define LOG_ZONE(X) lz##X,
    LOG_ZONES
#undef LOG_ZONE

    lzNumZones
};

#define LOGGABLE(ZONE) (log_zone_mask & (1<<(lz##ZONE)))

#ifdef DEBUG
#define _LOG(ZONE, ...)  { if(debug_fp && LOGGABLE(ZONE)) fprintf(debug_fp, __VA_ARGS__); }
#define _INFO(ZONE, ...) { if(LOGGABLE(ZONE)) fprintf(info_fp, #ZONE " | " __VA_ARGS__); }
#else
#define _LOG(ZONE, ...)
#define _INFO(ZONE, ...) if(LOGGABLE(ZONE)) printf(#ZONE " | " __VA_ARGS__)
#endif

#define NOTIFY(...) printf(__VA_ARGS__)

#define ASSERT(COND, ...) { if(!(COND)) { fprintf(stderr, "FAIL @ %s:%d in %s()  ", __FILE__, __LINE__, __FUNCTION__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); abort(); } }

#endif
