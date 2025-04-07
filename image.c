#include "image.h"

#define assert(x)            \
    do                       \
    {                        \
        if (!(x))            \
        {                    \
            __debugbreak();  \
        }                    \
    } while (0)
#define assert_hr(hr) assert(SUCCEEDED(hr))

void image_init(IWICImagingFactory** factory)
{
    HRESULT hr;
    hr = CoInitialize(NULL);
    assert_hr(hr);
    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
                          (void**)factory);
    assert_hr(hr);
}

void image_clean(IWICImagingFactory* factory)
{
    IWICImagingFactory_Release(factory);
    CoUninitialize();
}

unsigned char* image_load(IWICImagingFactory* factory, const char* filename, unsigned* width, unsigned* height)
{
    // convert filename from char to wchar_t
    int filename_len = MultiByteToWideChar(CP_UTF8, 0, filename, -1, NULL, 0);
    wchar_t* filename_w = (wchar_t*)malloc(filename_len * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, filename, -1, filename_w, filename_len);

    IWICBitmapDecoder* decoder = NULL;
    HRESULT hr;
    hr = IWICImagingFactory_CreateDecoderFromFilename(factory, filename_w, NULL, GENERIC_READ,
                                                      WICDecodeMetadataCacheOnDemand, &decoder);
    if (SUCCEEDED(hr))
    {
        IWICBitmapFrameDecode* frame = NULL;
        hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
        if (SUCCEEDED(hr))
        {
            IWICFormatConverter* converter = NULL;
            hr = IWICImagingFactory_CreateFormatConverter(factory, &converter);
            if (SUCCEEDED(hr))
            {
                hr = IWICFormatConverter_Initialize(converter, (IWICBitmapSource*)frame, &GUID_WICPixelFormat32bppRGBA,
                                                    WICBitmapDitherTypeNone, NULL, 0.0f, WICBitmapPaletteTypeCustom);
                if (SUCCEEDED(hr))
                {
                    hr = IWICFormatConverter_GetSize(converter, width, height);
                    if (SUCCEEDED(hr))
                    {
                        unsigned len = (*width) * (*height) * 4;
                        unsigned char* bitmap = malloc(len);
                        hr = IWICFormatConverter_CopyPixels(converter, NULL, (*width) * 4, len, bitmap);
                        if (SUCCEEDED(hr))
                        {
                            return bitmap;
                        }
                    }
                }
                IWICFormatConverter_Release(converter);
            }
            IWICBitmapFrameDecode_Release(frame);
        }
        IWICBitmapDecoder_Release(decoder);
    }
    return NULL;
}

// Need to init COM and factory first (call image_init)
void image_gif_init(IWICImagingFactory* factory, const char* filename, GIFFrameCache* gif_frame_cache)
{
    // convert filename from char to wchar_t
    int filename_len = MultiByteToWideChar(CP_UTF8, 0, filename, -1, NULL, 0);
    wchar_t* filename_w = (wchar_t*)malloc(filename_len * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, filename, -1, filename_w, filename_len);

    IWICBitmapDecoder* decoder = NULL;
    HRESULT hr;
    hr = IWICImagingFactory_CreateDecoderFromFilename(factory, filename_w, NULL, GENERIC_READ,
                                                      WICDecodeMetadataCacheOnDemand, &decoder);
    hr = IWICBitmapDecoder_GetFrameCount(decoder, &gif_frame_cache->frame_count);
    if (SUCCEEDED(hr))
    {
        if (SUCCEEDED(hr))
        {
            IWICBitmapFrameDecode* frame = NULL;
            for (unsigned i = 0; i < gif_frame_cache->frame_count; i++)
            {
                hr = IWICBitmapDecoder_GetFrame(decoder, i, &frame);
                if (SUCCEEDED(hr))
                {
                    PROPVARIANT prop;
                    PropVariantInit(&prop);
                    IWICMetadataQueryReader* metadata = NULL;
                    IWICBitmapFrameDecode_GetMetadataQueryReader(frame, &metadata);
                    // get delay
                    hr = IWICMetadataQueryReader_GetMetadataByName(metadata, L"/grctlext/Delay", &prop);
                    assert_hr(hr);
                    if (SUCCEEDED(hr))
                    {
                        hr = (prop.vt == VT_UI2 ? S_OK : E_FAIL);
                        if (SUCCEEDED(hr))
                        {
                            if (gif_frame_cache->count >= gif_frame_cache->capacity)
                            {
                                size_t old_capacity = gif_frame_cache->capacity;
                                gif_frame_cache->capacity = gif_frame_cache->capacity ? gif_frame_cache->capacity * 2 : 16;
                                gif_frame_cache->frames = realloc(gif_frame_cache->frames, gif_frame_cache->capacity * sizeof(GIFFrame));

                                // Initialize new frames
                                for (size_t i = old_capacity; i < gif_frame_cache->capacity; i++) {
                                    gif_frame_cache->frames[i].texture = NULL;
                                    gif_frame_cache->frames[i].bitmap = NULL;
                                }
                            }
                            // convert the delay retrieved in 10 ms units to a delay in 1 ms units
                            hr = UIntMult(prop.uiVal, 10, &gif_frame_cache->frames[i].delay);
                            gif_frame_cache->count++;
                        }
                        PropVariantClear(&prop);
                    }
                    // get left
                    hr = IWICMetadataQueryReader_GetMetadataByName(metadata, L"/imgdesc/Left", &prop);
                    if (SUCCEEDED(hr))
                    {
                        hr = (prop.vt == VT_UI2 ? S_OK : E_FAIL);
                        if (SUCCEEDED(hr))
                        {
                            gif_frame_cache->frames[i].x = prop.uiVal;
                        }
                        PropVariantClear(&prop);
                    }
                    // get top
                    hr = IWICMetadataQueryReader_GetMetadataByName(metadata, L"/imgdesc/Top", &prop);
                    if (SUCCEEDED(hr))
                    {
                        hr = (prop.vt == VT_UI2 ? S_OK : E_FAIL);
                        if (SUCCEEDED(hr))
                        {
                            gif_frame_cache->frames[i].y = prop.uiVal;
                        }
                        PropVariantClear(&prop);
                    }
                    // get width
                    hr = IWICMetadataQueryReader_GetMetadataByName(metadata, L"/imgdesc/Width", &prop);
                    if (SUCCEEDED(hr))
                    {
                        hr = (prop.vt == VT_UI2 ? S_OK : E_FAIL);
                        if (SUCCEEDED(hr))
                        {
                            gif_frame_cache->frames[i].width = prop.uiVal;
                        }
                        PropVariantClear(&prop);
                    }
                    // get height
                    hr = IWICMetadataQueryReader_GetMetadataByName(metadata, L"/imgdesc/Height", &prop);
                    if (SUCCEEDED(hr))
                    {
                        hr = (prop.vt == VT_UI2 ? S_OK : E_FAIL);
                        if (SUCCEEDED(hr))
                        {
                            gif_frame_cache->frames[i].height = prop.uiVal;
                        }
                        PropVariantClear(&prop);
                    }
                    IWICBitmapFrameDecode_Release(frame);
                }
            }
            // get max width / height
            gif_frame_cache->frame_max_width = 0;
            gif_frame_cache->frame_max_height = 0;
            for (unsigned i = 0; i < gif_frame_cache->frame_count; i++)
            {
                if (gif_frame_cache->frames[i].width > gif_frame_cache->frame_max_width)
                    gif_frame_cache->frame_max_width = gif_frame_cache->frames[i].width;
                if (gif_frame_cache->frames[i].height > gif_frame_cache->frame_max_height)
                    gif_frame_cache->frame_max_height = gif_frame_cache->frames[i].height;
            }
            // get accumulative delays
            gif_frame_cache->accumulative_delays =
                malloc(sizeof(gif_frame_cache->frames[0]).delay * gif_frame_cache->frame_count);
            for (unsigned i = 0; i < gif_frame_cache->frame_count; i++)
            {
                gif_frame_cache->accumulative_delays[i] =
                    gif_frame_cache->frames[i].delay + (i == 0 ? 0 : gif_frame_cache->accumulative_delays[i - 1]);
            }
        }
        IWICBitmapDecoder_Release(decoder);
    }
}

void image_gif_clean(GIFFrameCache* gif_frame_cache)
{
    free(gif_frame_cache->accumulative_delays);
    free(gif_frame_cache->frames);
}

// don't forget free `bitmap` when don't need it anymore
void image_load_gif_frame(IWICImagingFactory* factory, const char* filename, unsigned idx, GIFFrameCache* gif_frame_cache)
{
    // user need to call image_load_gif_metadata first
    assert(gif_frame_cache->count);

    // convert filename from char to wchar_t
    int filename_len = MultiByteToWideChar(CP_UTF8, 0, filename, -1, NULL, 0);
    wchar_t* filename_w = (wchar_t*)malloc(filename_len * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, filename, -1, filename_w, filename_len);

    IWICBitmapDecoder* decoder = NULL;
    HRESULT hr;
    hr = IWICImagingFactory_CreateDecoderFromFilename(factory, filename_w, NULL, GENERIC_READ,
                                                      WICDecodeMetadataCacheOnDemand, &decoder);
    if (SUCCEEDED(hr))
    {
        // get frame
        IWICBitmapFrameDecode* frame = NULL;
        hr = IWICBitmapDecoder_GetFrame(decoder, idx, &frame);
        if (SUCCEEDED(hr))
        {
            // get bitmap of current frame
            IWICFormatConverter* converter = NULL;
            hr = IWICImagingFactory_CreateFormatConverter(factory, &converter);
            if (SUCCEEDED(hr))
            {
                hr = IWICFormatConverter_Initialize(converter, (IWICBitmapSource*)frame, &GUID_WICPixelFormat32bppRGBA,
                                                    WICBitmapDitherTypeNone, NULL, 0.0f, WICBitmapPaletteTypeCustom);
                if (SUCCEEDED(hr))
                {

                    hr = IWICFormatConverter_GetSize(converter, &gif_frame_cache->frames[idx].width, &gif_frame_cache->frames[idx].height);
                    if (SUCCEEDED(hr))
                    {
                        unsigned len = (gif_frame_cache->frames[idx].width) * (gif_frame_cache->frames[idx].height) * 4;
                        unsigned char* bitmap = malloc(len);
                        hr = IWICFormatConverter_CopyPixels(converter, NULL, (gif_frame_cache->frames[idx].width) * 4, len, bitmap);
                        if (SUCCEEDED(hr))
                        {
                            gif_frame_cache->frames[idx].bitmap = bitmap;
                        }
                    }
                }
                IWICFormatConverter_Release(converter);
            }
            IWICBitmapFrameDecode_Release(frame);
        }
        IWICBitmapDecoder_Release(decoder);
    }
}

int get_current_frame_idx_based_accum_delays(GIFFrameCache* gif_frame_cache)
{
    unsigned frame_count = gif_frame_cache->frame_count;
    float loop_current_time = gif_frame_cache->loop_current_time;
    unsigned* accumulative_delays = gif_frame_cache->accumulative_delays;
    for (unsigned i = 0; i < frame_count; i++)
    {
        if (loop_current_time == 0) 
        { 
            return 0;
        };
        if (loop_current_time > (i == 0 ? 0.f : accumulative_delays[i - 1]) && loop_current_time <= accumulative_delays[i])
        {
            return i;
        }
    }
    // in fact this idx is invalid, we just use it as an exceeding flag
    return gif_frame_cache->frame_count;
}
