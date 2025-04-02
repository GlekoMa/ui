#define COBJMACROS
#include <wincodec.h>
void image_init(IWICImagingFactory** factory);
void image_clean(IWICImagingFactory* factory);
unsigned char* image_load(IWICImagingFactory* factory, const char* filename, unsigned* width, unsigned* height);
