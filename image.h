#define COBJMACROS
#include <wincodec.h>
void image_init();
void image_clean();
unsigned char* image_load(const char* filename, unsigned* width, unsigned* height);
