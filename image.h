#pragma once

#define COBJMACROS
#include <wincodec.h>

// store loaded gif
typedef struct {
    void* texture; // as composed texture
    unsigned char* bitmap;
    unsigned x;
    unsigned y;
    unsigned width;
    unsigned height;
    unsigned delay;
} GIFFrame;

typedef struct {
    float loop_current_time; // compare to delay and determine if display next frame
    unsigned* accumulative_delays; // alloced by malloc
    unsigned frame_count;
    unsigned frame_max_width;
    unsigned frame_max_height;
    GIFFrame* frames; // alloced by realloc
    int capacity;
    int count;
} GIFFrameCache;

// not gif
void image_init(IWICImagingFactory** factory);
void image_clean(IWICImagingFactory* factory);
unsigned char* image_load(IWICImagingFactory* factory, const char* filename, unsigned* width, unsigned* height);

// gif
void image_gif_init(IWICImagingFactory* factory, const char* filename, GIFFrameCache* gif_frame_cache);
void image_gif_clean(GIFFrameCache* gif_frame_cache);
int get_current_frame_idx_based_accum_delays(GIFFrameCache* gif_frame_cache);
void image_load_gif_frame(IWICImagingFactory* factory, const char* filename, unsigned idx, GIFFrameCache* gif_frame_cache);
