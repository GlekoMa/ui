#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <d3dcompiler.h>
#include <dxgidebug.h>
#include "image.h"
#include "ui.h"

///

#define BUFFER_SIZE 16384
// atlas
#define NUM_CHARS_SYMBOL 4
#define NUM_CHARS_ASCII 95
#define NUM_CHARS_ZH 3522
#define NUM_CHARS NUM_CHARS_SYMBOL + NUM_CHARS_ASCII + NUM_CHARS_ZH + 1 // 1 is 3x3 white square
// image
#define MAX_IMAGE_PATH_RES_ENTRIES 128

///

typedef struct {
    float pos[2];
    float uv[2];
    unsigned char col[4];
    int tex_index; // 0: font atlas texture | 1: image texture
} Vertex;

typedef struct {
    int codepoint;
    UI_Rect src;
    int xoff, yoff, xadvance;
} Atlas;

// store loaded images
typedef struct {
    void* texture;
    int width;
    int height;
} ImageResource;

typedef struct {
    ImageResource* resources;
    int capacity;
    int count;
} ImageCache;

typedef struct {
    const char* path;
    ImageResource* resource;
} ImagePathResEntry;

typedef struct {
    GIFFrameCache* gif_frame_cache;
    int capacity;
    int count;
} GIFCache;

typedef struct {
    const char* path;
    int gif_idx;
} GIFPathResEntry;

typedef struct {
    int client_width;
    int client_height;
    // D3D11 specification
    ID3D11Device*             device;
    ID3D11DeviceContext*      context;
    IDXGISwapChain1*          swapchain;
    ID3D11SamplerState*       sampler_state;
    ID3D11ShaderResourceView* srview;
    ID3D11ShaderResourceView* img_srview;
    ID3D11ShaderResourceView* gif_srview;
    ID3D11Buffer*             vbuffer;
    ID3D11Buffer*             ibuffer;
    ID3D11Buffer*             cbuffer;
    ID3D11BlendState*         blend_state;
    ID3D11RasterizerState*    raster_state;
    ID3D11InputLayout*        layout;
    ID3D11VertexShader*       vshader;
    ID3D11PixelShader*        pshader;
    ID3D11RenderTargetView*   rtview;
    ID3D11Texture2D*          texture;
    // vertex & atlas & image
    Vertex vert_data[BUFFER_SIZE * 4];
    unsigned index_data[BUFFER_SIZE * 6];
    int buf_idx;
    Atlas atlas[NUM_CHARS];
    ImageCache image_cache;
    ImagePathResEntry image_path_res_entries[MAX_IMAGE_PATH_RES_ENTRIES];
    GIFCache gif_cache;
    GIFPathResEntry gif_path_res_entries[MAX_IMAGE_PATH_RES_ENTRIES];
} RendererState;

HWND g_window;

float calculate_delta_time();
void r_init(RendererState* r_state);
void r_clear(RendererState* r_state, UI_Color color);
void r_draw_rect(RendererState* r_state, UI_Rect rect, UI_Color color);
void r_draw_icon(RendererState* r_state, int id, UI_Rect rect, UI_Color color);
void r_draw_text(RendererState* r_state, const wchar_t* text, UI_Vec2 pos, UI_Color color);
int r_get_text_width(RendererState* r_state, const wchar_t* text, int len);
int r_get_text_height(RendererState* r_state);
void r_set_clip_rect(RendererState* r_state, UI_Rect rect);
void r_draw_image(IWICImagingFactory* img_factory, RendererState* r_state, UI_Rect rect, const char* path);
void r_draw_image_gif(IWICImagingFactory* img_factory, RendererState* r_state, UI_Rect rect, const char* path, float delta_time_ms);
void r_present(RendererState* r_state);
void r_clean(RendererState* r_state);
