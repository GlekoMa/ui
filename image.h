#pragma once

#define COBJMACROS
#include <wincodec.h>

// store loaded gif
typedef struct {
    void* texture_view; // allow map to texture view (e.g. ID3D11ShaderResourceView)
    unsigned char* bitmap;
    unsigned left;
    unsigned top;
    unsigned width;
    unsigned height;
    unsigned delay;
} GIFFrame;

// note: type of member is used by realloc. if change, don't forget to update it in image_load_gif_metadata
typedef struct {
    unsigned frame_count;
    GIFFrame* frames;
    unsigned capacity;
    unsigned count;
} GIFCache;

void image_init(IWICImagingFactory** factory);
void image_clean(IWICImagingFactory* factory);
unsigned char* image_load(IWICImagingFactory* factory, const char* filename, unsigned* width, unsigned* height);
void image_load_gif_metadata(IWICImagingFactory* factory, const char* filename, GIFCache* gif_cache);
void image_load_gif_frame(IWICImagingFactory* factory, const char* filename, unsigned idx, GIFCache* gif_cache);
