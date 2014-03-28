#ifndef __display_pixel_h__
#define __display_pixel_h__

#ifdef BPP32
typedef uint32_t DisplayPixel_t;
#define BPP 4

#else
typedef uint16_t DisplayPixel_t;
#define BPP 2

#endif

#endif
