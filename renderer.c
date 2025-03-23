#include "ui.h"
#pragma warning(disable: 4068)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#define STB_TRUETYPE_IMPLEMENTATION
#include "thirdparty/stb_truetype.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "thirdparty/stb_image_write.h"
#pragma clang diagnostic pop

#define COBJMACROS
#include <d3d11.h>
#include <dxgi1_3.h>
#include <d3dcompiler.h>
#include <dxgidebug.h>
#include <stdio.h>
#include "renderer.h"

#define BUFFER_SIZE 16384

// atlas
#define NUM_CHARS_SYMBOL 4
#define NUM_CHARS_ASCII 95
#define NUM_CHARS_ZH 3500
// the addition '1' is the data of 3x3 white square (as atlas[0])
#define NUM_CHARS NUM_CHARS_SYMBOL + NUM_CHARS_ASCII + NUM_CHARS_ZH + 1

typedef struct {
    ID3D11Device*             device;
    ID3D11DeviceContext*      context;
    IDXGISwapChain1*          swapchain;
    ID3D11SamplerState*       sampler_state;
    ID3D11ShaderResourceView* texture_view;
    ID3D11Buffer*             vbuffer;
    ID3D11Buffer*             ibuffer;
    ID3D11Buffer*             cbuffer;
	ID3D11BlendState*         blend_state;
    ID3D11InputLayout*        layout;
    ID3D11VertexShader*       vshader;
    ID3D11PixelShader*        pshader;
    ID3D11RenderTargetView*   rtview;
    ID3D11Texture2D*          texture;
} Renderer_State;

typedef struct {
    float pos[2];
    float uv[2];
    unsigned char col[4];
} Vertex;

typedef struct {
    int codepoint;
    UI_Rect src;
    int xoff, yoff, xadvance;
} Atlas;

static Vertex s_vert_data[BUFFER_SIZE * 4];
static unsigned s_index_data[BUFFER_SIZE * 6];
static int s_buf_idx = 0;
static Renderer_State s_r_state;

enum { ATLAS_WIDTH = 1200, ATLAS_HEIGHT = 1200 };

static Atlas s_atlas[NUM_CHARS];

//
// Renderer helper functions
//

// Note: Argument `p_swapchain` must be double pointer. Because the swapchain is NULL before be
// inited, we can't create swapchain in the address of NULL.
static void create_swapchain(HWND window, ID3D11Device* device, IDXGISwapChain1** p_swapchain)
{
    IDXGIFactory2* factory;
    {
        IDXGIDevice* dxgi_device;
        ID3D11Device_QueryInterface(device, &IID_IDXGIDevice, (void**)&dxgi_device);
        IDXGIAdapter* dxgi_adapter;
        IDXGIDevice_GetAdapter(dxgi_device, &dxgi_adapter);
        IDXGIAdapter_GetParent(dxgi_adapter, &IID_IDXGIFactory2, (void**)&factory);
        IDXGIAdapter_Release(dxgi_adapter);
        IDXGIDevice_Release(dxgi_device);
    }
    DXGI_SWAP_CHAIN_DESC1 desc = {
        .Format      = DXGI_FORMAT_R8G8B8A8_UNORM,
        .SampleDesc  = { 1, 0 },
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = 2,
        .SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD,
    };
    IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown*)device, window, &desc, NULL, NULL, p_swapchain);
    IDXGIFactory2_Release(factory);
}

static void map_mvp_to_cbuffer(ID3D11DeviceContext* context, ID3D11Buffer* cbuffer, int client_width,
                        int client_height)
{
    D3D11_MAPPED_SUBRESOURCE mapped;
    ID3D11DeviceContext_Map(context, (ID3D11Resource*)cbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);

    float L = 0.f;
    float R = (float)client_width;
    float T = 0.f;
    float B = (float)client_height;

    // Set orthographic projection matrix
    float mvp[4][4] = {
        { 2.0f / (R - L),    0.0f,              0.0f, 0.0f },
        { 0.0f,              2.0f / (T - B),    0.0f, 0.0f },
        { 0.0f,              0.0f,              0.5f, 0.0f },
        { (R + L) / (L - R), (T + B) / (B - T), 0.5f, 1.0f },
    };

    memcpy(mapped.pData, mvp, sizeof(mvp));
    ID3D11DeviceContext_Unmap(context, (ID3D11Resource*)cbuffer, 0);
}

static void map_vertex_index_buffer(ID3D11DeviceContext* context, ID3D11Buffer* vbuffer, ID3D11Buffer* ibuffer,
                                    int client_width, int client_height)
{
    // map: Update vertex buffer
    D3D11_MAPPED_SUBRESOURCE mapped_vert;
    ID3D11DeviceContext_Map(context, (ID3D11Resource*)vbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0,
                            &mapped_vert);
    memcpy(mapped_vert.pData, s_vert_data, sizeof(Vertex) * 4 * (s_buf_idx));
    ID3D11DeviceContext_Unmap(context, (ID3D11Resource*)vbuffer, 0);

    // map: Update index buffer
    D3D11_MAPPED_SUBRESOURCE mapped_index;
    ID3D11DeviceContext_Map(context, (ID3D11Resource*)ibuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_index);
    memcpy(mapped_index.pData, s_index_data, sizeof(unsigned) * 6 * (s_buf_idx));
    ID3D11DeviceContext_Unmap(context, (ID3D11Resource*)ibuffer, 0);
}

static void get_atlas(unsigned char* temp_bitmap)
{
    // Store 3500 regularly used characters for Simplified Chinese.
    // Sourced from https://zh.wiktionary.org/wiki/%E9%99%84%E5%BD%95:%E7%8E%B0%E4%BB%A3%E6%B1%89%E8%AF%AD%E5%B8%B8%E7%94%A8%E5%AD%97%E8%A1%A8
    // TODO: Add symbol support like "《", "》"...
    short accumulative_offsets_from_0x4E00[] = {
        0,1,2,4,1,1,1,1,2,1,2,1,2,1,2,2,1,1,1,1,1,5,2,1,2,3,3,3,2,2,4,1,1,1,2,1,5,2,3,1,2,1,1,1,1,1,2,1,1,2,2,1,4,1,1,1,1,5,10,1,
        2,11,8,2,1,2,1,2,1,2,1,2,1,5,1,6,3,1,1,1,2,2,1,1,1,4,8,5,1,1,4,1,1,3,1,2,1,3,2,1,2,1,1,1,10,1,1,5,2,4,2,4,1,4,2,2,2,9,3,2,
        1,1,6,1,1,1,4,1,1,4,2,4,5,1,4,2,2,2,2,7,3,7,1,1,2,2,2,4,2,1,4,3,6,10,12,5,4,3,2,14,2,3,3,2,1,1,1,6,1,6,10,4,1,6,5,1,7,1,5,4,
        8,4,1,1,2,9,19,5,2,4,1,1,5,2,5,20,2,2,9,7,1,11,2,9,17,1,8,1,5,8,27,4,6,9,20,11,13,14,6,23,15,30,2,2,1,1,1,2,1,2,2,4,3,6,2,6,3,3,3,1,
        1,3,1,2,1,1,1,1,1,3,1,1,3,5,3,4,1,5,3,2,2,2,1,4,4,8,3,1,2,1,2,1,1,4,5,4,2,3,3,3,2,10,2,3,1,3,7,2,2,1,3,3,2,1,1,1,2,2,1,1,
        2,3,1,3,7,1,5,1,1,1,1,2,3,4,4,1,2,3,2,6,1,1,1,1,1,2,5,1,7,3,4,3,2,15,2,2,1,5,3,13,9,19,2,1,1,1,1,2,5,1,1,1,6,1,1,12,4,4,2,2,
        7,6,7,5,22,4,1,1,5,1,2,13,1,1,2,7,3,7,15,1,1,3,1,2,2,4,1,2,4,1,2,1,1,2,1,1,3,2,4,1,1,2,2,1,4,5,1,2,1,1,2,1,7,3,3,1,3,2,1,9,
        3,2,5,3,4,2,19,4,2,1,6,1,1,1,1,1,4,3,2,1,1,1,2,5,3,1,1,1,2,2,1,1,1,1,1,1,2,1,3,1,1,1,3,1,4,2,1,2,2,1,1,2,1,1,1,1,1,2,2,2,
        4,2,1,1,1,6,1,1,1,2,1,1,1,1,2,3,1,3,1,2,1,4,6,2,2,6,5,3,3,1,6,6,11,2,6,1,1,9,6,3,1,2,3,1,3,14,1,2,2,5,2,5,5,3,1,3,2,2,5,1,
        3,6,8,6,3,1,1,3,1,4,8,2,5,5,1,2,7,16,4,3,5,2,1,2,13,5,1,2,4,23,3,1,1,10,8,4,6,2,3,2,1,14,4,1,10,12,4,4,10,14,9,5,3,2,23,3,1,8,40,1,
        2,2,3,6,41,1,1,36,21,20,5,14,16,1,3,2,2,2,9,3,1,3,6,3,1,5,3,2,23,4,5,8,10,4,2,7,3,4,1,1,1,6,3,1,2,1,1,1,1,3,2,4,5,8,11,1,1,7,7,9,
        7,4,5,3,20,1,8,3,17,1,25,1,8,4,15,12,3,6,6,5,23,5,3,4,6,13,24,2,14,6,5,10,1,24,20,15,7,3,2,3,3,3,11,3,6,2,6,1,4,2,3,8,2,1,1,2,1,1,2,3,
        3,1,1,1,10,3,1,1,2,4,2,3,1,1,1,9,2,3,14,1,2,2,1,4,5,2,2,1,1,10,1,3,3,12,3,17,2,11,4,1,5,1,2,1,6,2,9,3,19,4,2,2,1,3,17,4,13,8,5,16,
        3,17,26,2,9,19,8,25,14,1,7,3,21,8,32,71,4,1,2,1,1,4,2,4,1,2,3,12,8,4,2,2,2,1,1,2,1,3,8,1,1,1,1,1,1,1,2,1,1,1,1,2,4,1,5,3,1,1,1,3,
        4,1,1,3,2,2,1,5,6,1,10,1,1,2,4,3,16,1,1,1,1,3,2,3,2,3,1,5,2,3,2,2,2,3,7,13,7,2,2,1,1,1,1,1,1,3,3,1,1,1,3,1,2,4,9,2,1,4,10,2,
        8,6,2,1,18,2,1,4,14,4,6,5,41,5,7,3,11,12,7,6,2,19,4,31,129,16,1,3,1,3,1,1,1,1,2,3,3,1,2,3,7,3,1,1,2,1,2,4,4,5,1,2,2,2,1,9,7,1,10,5,
        8,7,8,1,13,16,1,1,2,2,3,1,1,2,5,2,1,3,5,1,3,1,1,2,2,3,2,1,7,1,6,8,1,1,1,17,1,9,35,1,3,6,2,1,1,6,5,4,2,6,4,1,5,1,1,8,2,8,1,24,
        1,2,13,2,5,1,2,1,3,1,8,2,1,4,1,3,1,3,2,1,5,2,5,1,1,8,9,4,9,6,6,2,1,6,1,10,1,1,7,7,4,6,4,8,2,1,1,13,4,2,1,1,6,1,3,5,2,1,2,5,
        12,8,8,2,3,2,3,13,2,4,1,3,1,2,1,3,3,6,8,5,4,7,11,1,3,3,2,4,3,3,2,8,9,5,1,6,4,7,4,6,1,1,1,2,2,2,1,3,3,3,8,7,1,6,6,5,5,5,3,24,
        9,4,2,7,13,5,1,8,7,20,3,6,20,22,4,6,2,8,20,34,7,1,1,1,4,2,2,16,9,1,3,8,1,1,6,4,2,1,3,1,1,1,4,3,8,4,2,2,1,1,1,1,1,3,3,3,3,2,1,1,
        4,6,7,1,1,2,1,1,1,2,1,5,1,1,2,1,6,1,5,4,4,3,1,5,2,1,1,1,2,3,1,3,2,1,1,2,1,1,1,2,1,3,3,1,2,1,1,1,1,3,1,2,2,2,1,3,5,2,1,2,
        1,5,2,5,3,5,4,5,1,1,2,1,1,3,2,1,4,11,3,5,3,1,3,3,1,1,1,1,5,9,1,2,1,1,4,7,8,1,3,1,5,2,6,1,3,3,1,2,4,2,8,2,3,2,1,1,1,6,7,1,
        2,15,4,2,1,2,4,11,2,6,1,3,7,9,3,1,1,3,10,4,1,8,2,12,2,1,13,10,2,1,3,10,4,15,2,15,1,14,10,1,3,9,6,5,3,1,1,2,5,7,6,3,8,1,4,20,26,18,6,23,
        7,3,2,3,1,6,3,4,3,2,8,2,3,4,1,3,6,4,2,2,3,16,4,6,6,2,3,3,5,1,2,2,4,2,1,9,4,4,4,6,4,8,9,2,3,1,1,1,1,3,1,4,5,1,3,8,4,6,2,1,
        4,1,5,6,1,5,2,1,5,2,6,7,2,5,8,1,6,1,2,5,10,2,2,6,1,1,4,2,4,4,4,5,10,5,1,23,6,37,25,2,5,3,2,1,1,8,1,2,2,10,4,2,2,7,2,2,1,1,3,2,
        3,1,5,3,3,2,1,3,2,1,5,1,1,1,5,6,3,1,1,4,3,5,2,1,14,1,2,3,5,7,5,2,3,2,1,5,1,7,1,4,7,1,13,11,1,1,1,1,1,8,4,5,7,5,2,1,11,6,2,1,
        3,4,2,2,3,1,10,9,13,1,1,3,1,5,1,2,1,2,4,4,1,18,2,1,2,1,13,11,4,1,17,11,4,1,1,5,2,1,3,13,9,2,2,5,3,3,2,6,14,3,4,5,11,8,1,4,27,3,15,21,
        6,4,5,20,5,6,2,2,14,1,6,1,12,12,28,45,13,21,2,9,7,19,20,1,8,16,15,16,25,3,116,1,1,1,4,11,8,4,9,2,3,22,1,1,1,1,1,3,15,2,1,7,6,1,1,11,30,1,2,8,
        2,4,8,2,3,2,1,4,2,6,10,4,32,2,2,1,7,7,5,1,6,1,5,4,9,1,5,2,14,4,3,1,1,1,3,6,6,9,4,6,5,1,7,9,2,4,2,4,1,1,3,1,3,5,5,1,2,1,1,1,
        1,5,5,1,2,9,6,3,3,1,1,2,3,2,6,3,2,6,1,1,4,10,7,5,4,3,7,5,8,9,1,1,1,3,4,1,1,3,1,3,3,2,6,13,3,1,4,6,3,1,10,6,1,3,2,7,6,2,4,2,
        1,2,1,1,1,5,1,3,3,11,6,5,1,5,7,9,3,7,3,3,2,4,2,2,10,5,6,4,3,9,1,2,1,5,6,5,4,2,9,19,2,38,1,4,2,4,7,12,6,8,5,7,4,17,6,2,1,6,4,3,
        3,1,3,1,11,14,4,9,4,1,12,9,2,6,13,26,4,10,7,1,22,4,6,14,5,18,13,18,63,59,31,2,2,1,5,1,2,4,2,1,10,1,4,4,3,22,1,1,1,10,1,3,5,1,6,16,1,2,4,5,
        2,1,4,2,12,17,11,4,1,12,10,6,22,2,16,6,3,7,22,6,5,5,5,6,13,23,11,7,16,33,36,2,5,4,1,1,1,1,4,10,1,4,1,12,2,6,1,5,2,9,3,4,1,6,1,43,3,7,3,9,
        6,8,7,7,2,1,11,1,1,2,1,7,4,18,8,5,1,13,1,1,1,2,6,10,1,69,3,2,2,11,5,14,2,4,1,2,5,4,15,3,19,13,22,2,1,3,7,18,17,1,8,34,1,17,19,36,53,6,1,1,
        2,8,8,1,33,2,2,3,6,3,1,2,5,1,1,1,2,2,1,3,10,7,3,5,5,3,9,1,4,10,4,14,9,2,6,2,1,5,5,7,3,1,3,7,3,2,7,2,3,8,3,3,3,7,8,6,4,5,38,5,
        2,3,1,1,13,6,14,18,5,24,2,1,4,2,2,1,39,3,14,6,1,2,2,5,1,1,1,2,2,1,1,3,4,15,1,3,2,4,1,3,2,3,8,2,20,1,8,7,7,1,5,4,1,26,6,2,3,6,13,11,
        10,4,21,3,2,1,6,8,28,4,7,3,4,2,2,1,5,11,1,2,1,10,1,7,2,4,22,4,4,6,2,5,16,8,14,1,2,14,13,3,1,1,3,6,1,7,8,9,1,2,1,10,3,4,16,19,15,3,7,57,
        2,2,10,14,7,1,1,1,5,3,5,10,1,8,1,14,44,2,1,2,1,2,3,3,2,2,4,1,3,3,7,5,2,1,2,2,4,1,8,3,2,3,11,2,1,12,6,19,8,1,1,2,7,17,29,2,1,3,5,2,
        2,1,9,4,1,4,1,1,4,1,2,6,26,12,11,3,5,1,1,3,2,8,2,10,6,7,5,6,3,5,2,9,2,2,4,16,13,2,4,1,1,1,2,2,5,2,26,2,5,2,13,8,2,10,8,2,2,4,22,12,
        6,8,13,3,6,16,49,7,14,38,8,2,12,9,5,1,7,5,1,5,4,3,8,5,12,11,1,3,3,3,1,15,12,15,22,2,5,4,4,63,211,95,2,2,2,1,3,1,1,3,2,1,1,2,2,1,1,1,3,2,
        4,1,1,1,1,1,2,3,1,1,2,1,1,2,3,1,3,1,1,1,3,1,4,2,1,3,3,3,1,1,2,1,4,1,2,1,5,1,8,5,1,1,1,2,2,3,3,4,4,1,4,3,4,4,2,22,1,4,2,3,
        8,7,1,4,4,24,4,6,10,3,3,21,4,4,4,9,6,4,8,9,7,11,1,4,1,2,2,7,1,3,5,2,1,1,26,5,3,2,2,3,8,1,1,8,4,2,16,25,1,2,3,2,1,10,2,2,1,2,3,1,
        1,2,1,4,1,4,1,3,2,6,4,1,1,1,2,3,6,2,8,4,2,2,3,6,8,1,3,3,2,5,5,4,3,1,5,1,1,2,3,4,21,2,7,6,12,1,1,4,4,1,16,9,2,9,1,1,3,1,1,10,
        5,9,3,1,1,11,11,13,2,8,25,7,3,6,1,8,4,5,1,6,1,5,2,10,1,11,2,4,1,4,1,1,2,14,17,23,1,2,1,7,4,4,9,2,5,7,3,1,8,1,6,1,2,2,2,6,4,10,6,2,
        5,3,4,3,1,6,1,5,6,8,8,1,1,1,1,4,5,25,4,1,8,1,1,2,14,3,7,2,2,6,4,2,1,2,1,3,8,8,1,17,34,6,1,5,2,1,3,10,3,2,16,4,9,8,1,18,8,1,1,15,
        7,1,2,1,21,26,4,6,2,8,1,5,4,13,9,14,3,22,6,7,5,5,13,7,15,37,2,4,3,17,1,16,1,12,1,42,10,6,3,20,15,5,32,1,5,15,23,22,39,22,1,1,1,9,17,6,8,4,1,2,
        1,1,8,2,7,2,7,7,1,6,5,17,6,1,2,2,9,5,2,9,10,11,5,2,2,6,10,1,2,2,1,4,5,26,12,2,3,2,9,2,7,20,2,13,10,18,27,6,6,5,46,28,13,30,5,7,1,7,3,2,
        8,2,2,3,1,2,1,4,7,10,3,7,2,5,4,6,15,2,4,16,1,3,1,15,4,11,15,5,1,9,14,2,19,5,53,32,2,5,59,1,2,1,1,2,1,9,17,3,26,137,1,9,211,6,53,1,2,1,3,1,
        4,1,1,1,2,1,3,2,1,1,2,1,1,1,1,1,3,1,1,2,1,1,3,4,4,2,3,3,1,3,1,3,1,5,1,1,2,2,1,2,1,2,1,2,1,2,1,3,2,2,1,2,2,1,2,1,2,2,1,7,
        2,6,1,1,2,2,4,1,4,3,3,10,5,6,21,9,1,14,1,18,145,1,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,1,2,1,1,1,2,2,1,2,1,1,1,6,1,1,2,1,1,4,
        2,2,2,1,3,2,1,1,2,2,5,5,4,1,1,1,10,4,5,1,20,4,16,1,10,5,8,1,5,10,2,2,9,2,5,4,2,2,3,16,5,19,7,1,7,17,2,4,3,27,6,1,5,15,14,28,1,3,3,8,
        172,1,1,1,3,2,1,1,4,7,2,2,4,2,1,2,1,7,1,2,2,1,3,2,1,2,1,4,5,1,2,5,1,8,4,1,3,1,2,1,2,1,6,2,1,3,4,1,2,1,1,1,1,12,5,7,2,4,3,1,
        1,1,3,3,1,5,1,2,2,3,3,1,2,1,1,1,2,12,10,3,1,2,4,5,6,2,4,4,12,2,8,1,7,10,1,17,2,15,1,3,4,4,13,6,9,4,3,23,5,16,28,51,1,5,5,6,6,2,5,2,
        5,4,2,1,7,8,2,2,7,53,3,2,1,1,1,2,163,532,1,10,6,4,1,1,1,2,3,1,1,2,5,3,2,8,3,3,2,2,1,10,11,1,1,4,2,10,5,4,2,2,4,2,1,3,1,3,1,2,1,4,
        9,1,7,2,1,1,1,2,5,1,1,1,9,2,5,7,3,6,12,7,13,6,9,105,2,3,1,1,1,2,2,3,1,1,1,1,2,3,1,4,9,2,4,11,18,1,1,1,1,1,5,4,5,1,1,2,3,1,1,3,
        5,12,1,2,4,1,1,11,1,1,14,1,9,1,4,4,1,3,11,15,8,2,1,3,1,1,1,5,3,6,19,2,9,3,1,2,5,2,7,2,4,1,14,2,20,6,1,25,4,3,5,2,1,1,7,11,2,21,2,19,
        13,58,2,4,6,2,128,1,1,2,1,1,2,1,1,2,1,2,1,1,1,1,2,7,2,3,1,1,4,1,3,4,42,4,6,6,1,49,85,8,1,2,1,1,3,1,4,2,3,2,4,1,5,2,2,3,4,3,211,2,
        1,1,1,2,1,2,3,2,1,2,4,2,2,1,5,3,2,6,3,7,3,4,43,5,59,41,5,1,2,11,5,296,5,27,8,7,13,12,9,9,8,321,1,1,2,2,1,7,2,4,2,8,2,4,2,4,1,5,21,2,
        10,15,39,18,3,9,9,1,3,3,4,54,5,13,27,21,47,5,21,6
    };


    // Get atlas codepoint property
    int codepoints_symbol[NUM_CHARS_SYMBOL] = { 0x2714, 0x2716, 0x25B6, 0x25BC };
    int codepoints_ascii[NUM_CHARS_ASCII];
    for (int i = 0; i < NUM_CHARS_ASCII; i++) { codepoints_ascii[i] = 32 + i; }
    int codepoints_zh[NUM_CHARS_ZH];
    for (int i = 0, offset = 0; i < NUM_CHARS_ZH; i++)
    {
        offset += accumulative_offsets_from_0x4E00[i];
        codepoints_zh[i] += 0x4E00 + offset;
    };

    for (int i = 0; i < NUM_CHARS_SYMBOL; i++) { s_atlas[i+1].codepoint = codepoints_symbol[i]; }
    for (int i = 0; i < NUM_CHARS_ASCII; i++) { s_atlas[i+1+NUM_CHARS_SYMBOL].codepoint = codepoints_ascii[i]; }
    for (int i = 0; i < NUM_CHARS_ZH; i++) { s_atlas[i+1+NUM_CHARS_SYMBOL+NUM_CHARS_ASCII].codepoint = codepoints_zh[i]; }

    // Get atlas other properties & Fill bitmap
    {
        unsigned char* ttf_buffer_symbol_ascii;
        unsigned char* ttf_buffer_zh;
        {
            int ttf_buffer_symbol_ascii_size = 1<<22; // 4 MB
            ttf_buffer_symbol_ascii = malloc(ttf_buffer_symbol_ascii_size);
            FILE* fp = fopen("c:/windows/fonts/seguisym.ttf", "rb");
            fread(ttf_buffer_symbol_ascii, 1, ttf_buffer_symbol_ascii_size, fp);
            fclose(fp);

            int ttf_buffer_zh_size = 10 * (1<<20); // 10 MB
            ttf_buffer_zh = malloc(ttf_buffer_zh_size);
            fp = fopen("c:/windows/fonts/simhei.ttf", "rb");
            fread(ttf_buffer_zh, 1, ttf_buffer_zh_size, fp);
            fclose(fp);
        }

        stbtt_pack_context pack_ctx;
        stbtt_packedchar cdata_symbol[NUM_CHARS_SYMBOL];
        stbtt_packedchar cdata_ascii[NUM_CHARS_ASCII];
        stbtt_packedchar cdata_zh[NUM_CHARS_ZH];
        float ascii_font_size = 24.0;
        stbtt_PackBegin(&pack_ctx, temp_bitmap, ATLAS_WIDTH, ATLAS_HEIGHT, 0, 1, NULL);
        {
            // get symbol & ascii
            stbtt_pack_range pack_range_ascii_symbol[] = {
                {
                    .font_size = ascii_font_size,
                    .array_of_unicode_codepoints = codepoints_symbol,
                    .num_chars = NUM_CHARS_SYMBOL,
                    .chardata_for_range = cdata_symbol,
                },
                {
                    .font_size = ascii_font_size,
                    .array_of_unicode_codepoints = codepoints_ascii,
                    .num_chars = NUM_CHARS_ASCII,
                    .chardata_for_range = cdata_ascii,
                },
            };
            stbtt_PackFontRanges(&pack_ctx, ttf_buffer_symbol_ascii, 0, pack_range_ascii_symbol,
                                 sizeof(pack_range_ascii_symbol) / sizeof(pack_range_ascii_symbol[0]));

            // get chinese
            stbtt_pack_range pack_range_zh[] = {
                {
                    .font_size = ascii_font_size * 0.8f,
                    .array_of_unicode_codepoints = codepoints_zh,
                    .num_chars = NUM_CHARS_ZH,
                    .chardata_for_range = cdata_zh,
                }
            };
            stbtt_PackFontRanges(&pack_ctx, ttf_buffer_zh, 0, pack_range_zh,
                                 sizeof(pack_range_zh) / sizeof(pack_range_zh[0]));
        }
        stbtt_PackEnd(&pack_ctx);

        free(ttf_buffer_symbol_ascii);
        free(ttf_buffer_zh);

        // Join the cdata
        stbtt_packedchar cdata[NUM_CHARS - 1]; // yeah, the '1' is reserved to our white square
        size_t offset = 0;
        memcpy(cdata + offset, cdata_symbol, sizeof(stbtt_packedchar) * NUM_CHARS_SYMBOL);
        offset += NUM_CHARS_SYMBOL;
        memcpy(cdata + offset, cdata_ascii, sizeof(stbtt_packedchar) * NUM_CHARS_ASCII);
        offset += NUM_CHARS_ASCII;
        memcpy(cdata + offset, cdata_zh, sizeof(stbtt_packedchar) * NUM_CHARS_ZH);

        // Convert cdata to atlas
        for (int i = 0; i < NUM_CHARS - 1; i++)
        {
            s_atlas[i+1].src.x = cdata[i].x0;
            s_atlas[i+1].src.y = cdata[i].y0;
            s_atlas[i+1].src.w = cdata[i].x1 - cdata[i].x0;
            s_atlas[i+1].src.h = cdata[i].y1 - cdata[i].y0;
            s_atlas[i+1].xoff = (int)cdata[i].xoff;
            s_atlas[i+1].yoff = (int)cdata[i].yoff;
            s_atlas[i+1].xadvance = (int)cdata[i].xadvance;
        }

        // Add a special 3x3 white square at the right-bottom corner of the bitmap as ATLAS_WHITE
        {
            int white_box_size = 3;
            int white_box_x = ATLAS_WIDTH - white_box_size;
            int white_box_y = ATLAS_HEIGHT - white_box_size;
            for (int y = 0; y < white_box_size; y++) {
                for (int x = 0; x < white_box_size; x++) {
                    temp_bitmap[(white_box_y + y) * ATLAS_WIDTH + (white_box_x + x)] = 255;
                }
            }
            s_atlas[0].src.x = white_box_x;
            s_atlas[0].src.y = white_box_y;
            s_atlas[0].src.w = white_box_size;
            s_atlas[0].src.h = white_box_size;
            s_atlas[0].xoff = 0;
            s_atlas[0].yoff = 0;
            s_atlas[0].xadvance = white_box_size;
        }
    }
    stbi_write_png("atlas.png", ATLAS_WIDTH, ATLAS_HEIGHT, 1, temp_bitmap, ATLAS_WIDTH);
}

//
// Renderer functions
//

static void flush()
{
    // Map vertex & index buffer
    map_vertex_index_buffer(s_r_state.context, s_r_state.vbuffer, s_r_state.ibuffer, g_client_width, g_client_height);

    // Setup orthographic projection matrix into constant buffer
    map_mvp_to_cbuffer(s_r_state.context, s_r_state.cbuffer, g_client_width, g_client_height);

    // Set viewport
    D3D11_VIEWPORT viewport = { 0, 0, (FLOAT)g_client_width, (FLOAT)g_client_height, 0, 1 };

    // IA-VS-RS-PS-OM, Draw, Present!
    unsigned stride = sizeof(Vertex);
    unsigned offset = 0;
    ID3D11DeviceContext_IASetInputLayout(s_r_state.context, s_r_state.layout); // IA: Input Assembly
    ID3D11DeviceContext_IASetPrimitiveTopology(s_r_state.context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetVertexBuffers(s_r_state.context, 0, 1, &s_r_state.vbuffer, &stride, &offset);
    ID3D11DeviceContext_IASetIndexBuffer(s_r_state.context, s_r_state.ibuffer, DXGI_FORMAT_R32_UINT, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(s_r_state.context, 0, 1, &s_r_state.cbuffer);
    ID3D11DeviceContext_VSSetShader(s_r_state.context, s_r_state.vshader, NULL, 0); // VS: Vertex Shader
    ID3D11DeviceContext_RSSetViewports(s_r_state.context, 1, &viewport); // RS: Rasterizer Stage
    ID3D11DeviceContext_PSSetShader(s_r_state.context, s_r_state.pshader, NULL, 0); // PS: Pixel Shader
    ID3D11DeviceContext_PSSetShaderResources(s_r_state.context, 0, 1, &s_r_state.texture_view);
    ID3D11DeviceContext_PSSetSamplers(s_r_state.context, 0, 1, &s_r_state.sampler_state);
    ID3D11DeviceContext_OMSetRenderTargets(s_r_state.context, 1, &s_r_state.rtview, NULL); // OM: Output Merger
	ID3D11DeviceContext_OMSetBlendState(s_r_state.context, s_r_state.blend_state, NULL, 0xffffffff);
    ID3D11DeviceContext_DrawIndexed(s_r_state.context, s_buf_idx * 6, 0, 0);

    // Reset buf_idx
    s_buf_idx = 0;
}

static void push_rect(UI_Rect dst, UI_Rect src, UI_Color color)
{
    if (s_buf_idx == BUFFER_SIZE) { flush(); }

    int vert_idx  = s_buf_idx * 4;
    int index_idx = s_buf_idx * 6;
    s_buf_idx++;

    // Update vbuffer (pos)
    s_vert_data[vert_idx + 0].pos[0] = (float)dst.x;
    s_vert_data[vert_idx + 0].pos[1] = (float)dst.y;
    s_vert_data[vert_idx + 1].pos[0] = (float)dst.x + dst.w;
    s_vert_data[vert_idx + 1].pos[1] = (float)dst.y;
    s_vert_data[vert_idx + 2].pos[0] = (float)dst.x;
    s_vert_data[vert_idx + 2].pos[1] = (float)dst.y + dst.h;
    s_vert_data[vert_idx + 3].pos[0] = (float)dst.x + dst.w;
    s_vert_data[vert_idx + 3].pos[1] = (float)dst.y + dst.h;

    // Update vbuffer (uv)
    float x = src.x / (float)ATLAS_WIDTH;
    float y = src.y / (float)ATLAS_HEIGHT;
    float w = src.w / (float)ATLAS_WIDTH;
    float h = src.h / (float)ATLAS_HEIGHT;
    s_vert_data[vert_idx + 0].uv[0] = x;
    s_vert_data[vert_idx + 0].uv[1] = y;
    s_vert_data[vert_idx + 1].uv[0] = x + w;
    s_vert_data[vert_idx + 1].uv[1] = y;
    s_vert_data[vert_idx + 2].uv[0] = x;
    s_vert_data[vert_idx + 2].uv[1] = y + h;
    s_vert_data[vert_idx + 3].uv[0] = x + w;
    s_vert_data[vert_idx + 3].uv[1] = y + h;

    // Update vbuffer (col)
    memcpy((char*)(s_vert_data + vert_idx + 0) + offsetof(Vertex, col), &color, 4);
    memcpy((char*)(s_vert_data + vert_idx + 1) + offsetof(Vertex, col), &color, 4);
    memcpy((char*)(s_vert_data + vert_idx + 2) + offsetof(Vertex, col), &color, 4);
    memcpy((char*)(s_vert_data + vert_idx + 3) + offsetof(Vertex, col), &color, 4);

    // Update index buffer
    s_index_data[index_idx + 0] = vert_idx + 0;
    s_index_data[index_idx + 1] = vert_idx + 1;
    s_index_data[index_idx + 2] = vert_idx + 2;
    s_index_data[index_idx + 3] = vert_idx + 2;
    s_index_data[index_idx + 4] = vert_idx + 1;
    s_index_data[index_idx + 5] = vert_idx + 3;
}

void r_init()
{
    // Create device and context
    // (Need to add BGRA support for compatibility with Direct2D)
    UINT              flags    = D3D11_CREATE_DEVICE_DEBUG | D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, levels, ARRAYSIZE(levels),
                      D3D11_SDK_VERSION, &s_r_state.device, NULL, &s_r_state.context);

    // Create swap chain
    create_swapchain(g_window, s_r_state.device, &s_r_state.swapchain);

    // Create sampler state
    {
        D3D11_SAMPLER_DESC desc = {
            .Filter         = D3D11_FILTER_MIN_MAG_MIP_POINT,
            .AddressU       = D3D11_TEXTURE_ADDRESS_WRAP,
            .AddressV       = D3D11_TEXTURE_ADDRESS_WRAP,
            .AddressW       = D3D11_TEXTURE_ADDRESS_WRAP,
            .ComparisonFunc = D3D11_COMPARISON_NEVER,
        };
        ID3D11Device_CreateSamplerState(s_r_state.device, &desc, &s_r_state.sampler_state);
    }

    // Create texture buffer (atlas)
    {
        unsigned char* temp_bitmap = malloc(ATLAS_WIDTH * ATLAS_HEIGHT);
        get_atlas(temp_bitmap);
        D3D11_TEXTURE2D_DESC desc = {
            .Width            = ATLAS_WIDTH,
            .Height           = ATLAS_HEIGHT,
            .MipLevels        = 1,
            .ArraySize        = 1,
            .Format           = DXGI_FORMAT_R8_UNORM,
            .SampleDesc.Count = 1,
            .Usage            = D3D11_USAGE_IMMUTABLE,
            .BindFlags        = D3D11_BIND_SHADER_RESOURCE,
        };
        D3D11_SUBRESOURCE_DATA texture_subresource_data = {
            .pSysMem = temp_bitmap,
            .SysMemPitch = ATLAS_WIDTH * 1,
        };
        ID3D11Texture2D* texture;
        ID3D11Device_CreateTexture2D(s_r_state.device, &desc, &texture_subresource_data, &texture);
        ID3D11Device_CreateShaderResourceView(s_r_state.device, (ID3D11Resource*)texture, 0, &s_r_state.texture_view);
        ID3D11Texture2D_Release(texture);
        free(temp_bitmap);
    }

    // Create vertex buffer
    {
        D3D11_BUFFER_DESC desc = {
            .ByteWidth      = sizeof(s_vert_data),
            .Usage          = D3D11_USAGE_DYNAMIC,
            .BindFlags      = D3D11_BIND_VERTEX_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };

        D3D11_SUBRESOURCE_DATA initial = { .pSysMem = s_vert_data };
        ID3D11Device_CreateBuffer(s_r_state.device, &desc, &initial, &s_r_state.vbuffer);
    }

    // Create index buffer
    {
        D3D11_BUFFER_DESC desc = {
            .ByteWidth      = sizeof(s_index_data),
            .Usage          = D3D11_USAGE_DYNAMIC,
            .BindFlags      = D3D11_BIND_INDEX_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };

        D3D11_SUBRESOURCE_DATA initial = { .pSysMem = s_index_data };
        ID3D11Device_CreateBuffer(s_r_state.device, &desc, &initial, &s_r_state.ibuffer);
    }

    // Create constant buffer for delivering MVP (Model View Projection)
    {
        D3D11_BUFFER_DESC desc = {
            .ByteWidth      = sizeof(float) * 4 * 4, // float mvp[4][4]
            .Usage          = D3D11_USAGE_DYNAMIC,
            .BindFlags      = D3D11_BIND_CONSTANT_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };
        ID3D11Device_CreateBuffer(s_r_state.device, &desc, NULL, &s_r_state.cbuffer);
    }

    // Create blend state
    {
        D3D11_BLEND_DESC desc = {
            .RenderTarget[0] = {
                .BlendEnable = TRUE,
                .SrcBlend = D3D11_BLEND_SRC_ALPHA,
                .DestBlend = D3D11_BLEND_INV_SRC_ALPHA,
                .BlendOp = D3D11_BLEND_OP_ADD,
                .SrcBlendAlpha = D3D11_BLEND_ONE,
                .DestBlendAlpha = D3D11_BLEND_ZERO,
                .BlendOpAlpha = D3D11_BLEND_OP_ADD,
                .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL
            }
        };
        ID3D11Device_CreateBlendState(s_r_state.device, &desc, &s_r_state.blend_state);
    }

    // Create hlsl
    const char hlsl[] = ""
                        "Texture2D mytexture : register(t0);                                                     \n"
                        "SamplerState mysampler : register(s0);                                                  \n"
                        "                                                                                        \n"
                        "cbuffer cbuffer0 : register(b0)                                                         \n"
                        "{                                                                                       \n"
                        "    float4x4 projection_matrix;                                                         \n"
                        "};                                                                                      \n"
                        "                                                                                        \n"
                        "struct VS_Input                                                                         \n"
                        "{                                                                                       \n"
                        "    float2 pos : POSITION;                                                              \n"
                        "    float2 uv : UV;                                                                     \n"
                        "    float4 col : COLOR;                                                                 \n"
                        "};                                                                                      \n"
                        "                                                                                        \n"
                        "struct PS_INPUT                                                                         \n"
                        "{                                                                                       \n"
                        "    float4 pos : SV_POSITION;                                                           \n"
                        "    float2 uv : TEXCOORD;                                                               \n"
                        "    float4 col : COLOR;                                                                 \n"
                        "};                                                                                      \n"
                        "                                                                                        \n"
                        "PS_INPUT vs(VS_Input input)                                                             \n"
                        "{                                                                                       \n"
                        "    PS_INPUT output;                                                                    \n"
                        "    output.pos = mul(projection_matrix, float4(input.pos, 0.0f, 1.0f));                 \n"
                        "    output.uv = input.uv;                                                               \n"
                        "    output.col = input.col;                                                             \n"
                        "    return output;                                                                      \n"
                        "}                                                                                       \n"
                        "                                                                                        \n"
                        "float4 ps(PS_INPUT input) : SV_TARGET                                                   \n"
                        "{                                                                                       \n"
                        "    return float4(mytexture.Sample(mysampler, input.uv).rrrr) * input.col;              \n"
                        "}                                                                                       \n";

    // Create input layout, vertex shader, pixel shader
    {
        ID3DBlob* vblob;
        D3DCompile(hlsl, sizeof(hlsl), NULL, NULL, NULL, "vs", "vs_5_0", 0, 0, &vblob, NULL);
        ID3DBlob* pblob;
        D3DCompile(hlsl, sizeof(hlsl), NULL, NULL, NULL, "ps", "ps_5_0", 0, 0, &pblob, NULL);
        D3D11_INPUT_ELEMENT_DESC desc[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, offsetof(Vertex, pos), D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "UV",       0, DXGI_FORMAT_R32G32_FLOAT,   0, offsetof(Vertex, uv),  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(Vertex, col), D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };
        ID3D11Device_CreateInputLayout(s_r_state.device, desc, ARRAYSIZE(desc),
                                       ID3D10Blob_GetBufferPointer(vblob), ID3D10Blob_GetBufferSize(vblob),
                                       &s_r_state.layout);
        ID3D11Device_CreateVertexShader(s_r_state.device, ID3D10Blob_GetBufferPointer(vblob),
                                        ID3D10Blob_GetBufferSize(vblob), NULL, &s_r_state.vshader);
        ID3D11Device_CreatePixelShader(s_r_state.device, ID3D10Blob_GetBufferPointer(pblob),
                                       ID3D10Blob_GetBufferSize(pblob), NULL, &s_r_state.pshader);

        ID3D10Blob_Release(pblob);
        ID3D10Blob_Release(vblob);
    }

    // Prepare a void render target view
    s_r_state.rtview = NULL;

    // Create render target view for backbuffer texture
    ID3D11Texture2D* texture;
    IDXGISwapChain1_GetBuffer(s_r_state.swapchain, 0, &IID_ID3D11Texture2D, (void**)&texture);
    ID3D11Device_CreateRenderTargetView(s_r_state.device, (ID3D11Resource*)texture, NULL, &s_r_state.rtview);
    ID3D11Texture2D_Release(texture);
}

void r_clear(UI_Color color)
{
    float color_f[4] = { color.r / 255.f, color.g / 255.f, color.b / 255.f, color.a / 255.f };
    ID3D11DeviceContext_ClearRenderTargetView(s_r_state.context, s_r_state.rtview, color_f);
}

void r_draw_rect(UI_Rect rect, UI_Color color)
{
    push_rect(rect, s_atlas[0].src, color);
}

void r_draw_icon(int id, UI_Rect rect, UI_Color color)
{
    UI_Rect src = s_atlas[id].src;
    int x = rect.x + (rect.w - src.w) / 2;
    int y = rect.y + (rect.h - src.h) / 2;
    push_rect(ui_rect(x, y, src.w, src.h), src, color);
}

void r_draw_text(const char* text, UI_Vec2 pos, UI_Color color)
{
    UI_Rect dst = { pos.x, pos.y + (int)(24 * 0.8), 0, 0 };
    for (const char* p = text; *p; p++)
    {
        // 32 is the first 32 char of ascii table (which we didn't get)
        int chr = (unsigned char)*p - 32 + NUM_CHARS_SYMBOL + 1;
        UI_Rect src = s_atlas[chr].src;
        Atlas* b = &s_atlas[chr];

        dst.x += (int)b->xoff;
        dst.y += (int)b->yoff;
        dst.w = src.w;
        dst.h = src.h;
        push_rect(dst, src, color);

        dst.x += (int)(b->xadvance - b->xoff);
        dst.y -= (int)b->yoff;
    }
}

// TODO: need better performance
static int find_atlas_idx_by_codepoint(int codepoint)
{
    if (codepoint < 127)
    {
        return codepoint - 32 + NUM_CHARS_SYMBOL + 1;
    }
    else
    {
        for (int i = 0; i < NUM_CHARS; i++)
        {
            if (s_atlas[i].codepoint == codepoint)
                return i;
        }
    }
    // TODO: need handle error
    return 63 - 32 + NUM_CHARS_SYMBOL + 1; // '?'
}

void r_draw_text_w(const wchar_t* text, UI_Vec2 pos, UI_Color color)
{
    UI_Rect dst = { pos.x, pos.y + (int)(24 * 0.8), 0, 0 };
    for (const wchar_t* p = text; *p; p++)
    {
        // 32 is the first 32 char of ascii table (which we didn't get)
        int chr = find_atlas_idx_by_codepoint(*p);
        UI_Rect src = s_atlas[chr].src;
        Atlas* b = &s_atlas[chr];

        dst.x += (int)b->xoff;
        dst.y += (int)b->yoff;
        dst.w = src.w;
        dst.h = src.h;
        push_rect(dst, src, color);

        dst.x += (int)(b->xadvance - b->xoff);
        dst.y -= (int)b->yoff;
    }
}

// int r_get_text_width(const char* text, int len)
// {
//     int res = 0;
//     for (const char* p = text; *p && len--; p++)
//     {
//         int chr = ui_min((unsigned char)*p, 127);
//         res += atlas[ATLAS_FONT + chr].w;
//     }
//     return res;
// }
//
// int r_get_text_height(void)
// {
//     return 18;
// }

void r_present()
{
    flush();
    IDXGISwapChain1_Present(s_r_state.swapchain, 1, 0);
}

void r_clean()
{
    ID3D11RenderTargetView_Release(s_r_state.rtview);
    ID3D11SamplerState_Release(s_r_state.sampler_state);
    ID3D11ShaderResourceView_Release(s_r_state.texture_view);
    ID3D11Buffer_Release(s_r_state.vbuffer);
    ID3D11Buffer_Release(s_r_state.ibuffer);
    ID3D11Buffer_Release(s_r_state.cbuffer);
    ID3D11BlendState_Release(s_r_state.blend_state);
    ID3D11InputLayout_Release(s_r_state.layout);
    ID3D11VertexShader_Release(s_r_state.vshader);
    ID3D11PixelShader_Release(s_r_state.pshader);
    IDXGISwapChain1_Release(s_r_state.swapchain);
    ID3D11DeviceContext_Release(s_r_state.context);
    ID3D11Device_Release(s_r_state.device);
}
