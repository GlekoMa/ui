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

void image_load_gif_metadata(IWICImagingFactory* factory, const char* filename, GIFCache* gif_cache)
{
    // convert filename from char to wchar_t
    int filename_len = MultiByteToWideChar(CP_UTF8, 0, filename, -1, NULL, 0);
    wchar_t* filename_w = (wchar_t*)malloc(filename_len * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, filename, -1, filename_w, filename_len);

    IWICBitmapDecoder* decoder = NULL;
    HRESULT hr;
    hr = IWICImagingFactory_CreateDecoderFromFilename(factory, filename_w, NULL, GENERIC_READ,
                                                      WICDecodeMetadataCacheOnDemand, &decoder);
    hr = IWICBitmapDecoder_GetFrameCount(decoder, &gif_cache->frame_count);
    if (SUCCEEDED(hr))
    {
        if (SUCCEEDED(hr))
        {
            IWICBitmapFrameDecode* frame = NULL;
            for (unsigned i = 0; i < gif_cache->frame_count; i++)
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
                            if (gif_cache->count >= gif_cache->capacity)
                            {
                                gif_cache->capacity = gif_cache->capacity ? gif_cache->capacity * 2 : 16;
                                gif_cache->frames = realloc(gif_cache->frames, gif_cache->capacity * sizeof(GIFFrame));
                            }
                            // convert the delay retrieved in 10 ms units to a delay in 1 ms units
                            hr = UIntMult(prop.uiVal, 10, &gif_cache->frames[i].delay);
                            gif_cache->count++;
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
                            gif_cache->frames[i].left = prop.uiVal;
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
                            gif_cache->frames[i].top = prop.uiVal;
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
                            gif_cache->frames[i].width = prop.uiVal;
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
                            gif_cache->frames[i].height = prop.uiVal;
                        }
                        PropVariantClear(&prop);
                    }
                    IWICBitmapFrameDecode_Release(frame);
                }
            }
        }
        IWICBitmapDecoder_Release(decoder);
    }
}

// don't forget free `bitmap` when don't need it anymore
void image_load_gif_frame(IWICImagingFactory* factory, const char* filename, unsigned idx, GIFCache* gif_cache)
{
    // user need to call image_load_gif_metadata first
    // (because this function need width & height of one frame which should have been assigned)
    assert(gif_cache->count);

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

                    hr = IWICFormatConverter_GetSize(converter, &gif_cache->frames[idx].width, &gif_cache->frames[idx].height);
                    if (SUCCEEDED(hr))
                    {
                        unsigned len = (gif_cache->frames[idx].width) * (gif_cache->frames[idx].height) * 4;
                        unsigned char* bitmap = malloc(len);
                        hr = IWICFormatConverter_CopyPixels(converter, NULL, (gif_cache->frames[idx].width) * 4, len, bitmap);
                        if (SUCCEEDED(hr))
                        {
                            gif_cache->frames[idx].bitmap = bitmap;
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
// <<< image <<<

const char hlsl[] =
    "cbuffer cbuffer0 : register(b0)                                        \n"
    "{                                                                      \n"
    "    float4x4 projection_matrix;                                        \n"
    "};                                                                     \n"
    "                                                                       \n"
    "struct VS_Input                                                        \n"
    "{                                                                      \n"
    "    float2 pos : POSITION;                                             \n"
    "    float2 uv : TEXTURE;                                               \n"
    "};                                                                     \n"
    "                                                                       \n"
    "struct PS_INPUT                                                        \n"
    "{                                                                      \n"
    "    float4 pos : SV_POSITION;                                          \n"
    "    float2 uv : TEXCOORD;                                              \n"
    "};                                                                     \n"
    "                                                                       \n"
    "Texture2D mytexture : register(t0);                                    \n"
    "SamplerState mysampler : register(s0);                                 \n"
    "                                                                       \n"
    "PS_INPUT vs(VS_Input input)                                            \n"
    "{                                                                      \n"
    "    PS_INPUT output;                                                   \n"
    "    output.pos = mul(projection_matrix, float4(input.pos, 0.0f, 1.0f));\n"
    "    output.uv = input.uv;                                              \n"
    "    return output;                                                     \n"
    "}                                                                      \n"
    "                                                                       \n"
    "float4 ps(PS_INPUT input) : SV_TARGET                                  \n"
    "{                                                                      \n"
    "    return mytexture.Sample(mysampler, input.uv);                      \n"
    "}                                                                      \n"
;
