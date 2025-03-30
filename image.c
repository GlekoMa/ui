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

IWICImagingFactory* s_factory = NULL;

void image_init()
{
    HRESULT hr;
    hr = CoInitialize(NULL);
    assert_hr(hr);
    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
                          (void**)&s_factory);
    assert_hr(hr);
}

void image_clean()
{
    IWICImagingFactory_Release(s_factory);
    CoUninitialize();
}

unsigned char* image_load(const char* filename, unsigned* width, unsigned* height)
{
    // convert filename from char to wchar_t
    int filename_len = MultiByteToWideChar(CP_UTF8, 0, filename, -1, NULL, 0);
    wchar_t* filename_w = (wchar_t*)malloc(filename_len * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, filename, -1, filename_w, filename_len);

    IWICBitmapDecoder* decoder = NULL;
    HRESULT hr;
    hr = IWICImagingFactory_CreateDecoderFromFilename(s_factory, filename_w, NULL, GENERIC_READ,
                                                      WICDecodeMetadataCacheOnDemand, &decoder);
    if (SUCCEEDED(hr))
    {
        IWICBitmapFrameDecode* frame = NULL;
        hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
        if (SUCCEEDED(hr))
        {
            IWICFormatConverter* converter = NULL;
            hr = IWICImagingFactory_CreateFormatConverter(s_factory, &converter);
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
