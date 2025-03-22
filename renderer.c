#define COBJMACROS
#include <d3d11.h>
#include <dxgi1_3.h>
#include <d3dcompiler.h>
#include <dxgidebug.h>
#include "renderer.h"
#include "atlas.inl"

#define BUFFER_SIZE 16384

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

static Vertex s_vert_data[BUFFER_SIZE * 4];
static unsigned s_index_data[BUFFER_SIZE * 6];
static int s_buf_idx = 0;
static Renderer_State s_r_state;

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
        D3D11_TEXTURE2D_DESC desc = {
            .Width     = ATLAS_WIDTH,
            .Height    = ATLAS_HEIGHT,
            .MipLevels = 1,
            .ArraySize = 1,
            .Format    = DXGI_FORMAT_R8_UNORM,
            .SampleDesc.Count   = 1,
            .Usage     = D3D11_USAGE_IMMUTABLE,
            .BindFlags = D3D11_BIND_SHADER_RESOURCE,
        };
        D3D11_SUBRESOURCE_DATA texture_subresource_data = {
            .pSysMem = atlas_texture,
            .SysMemPitch = ATLAS_WIDTH * 1,
        };
        ID3D11Texture2D* texture;
        ID3D11Device_CreateTexture2D(s_r_state.device, &desc, &texture_subresource_data, &texture);
        ID3D11Device_CreateShaderResourceView(s_r_state.device, (ID3D11Resource*)texture, 0, &s_r_state.texture_view);
        ID3D11Texture2D_Release(texture);
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
    push_rect(rect, atlas[ATLAS_WHITE], color);
}

void r_draw_icon(int id, UI_Rect rect, UI_Color color) 
{
    UI_Rect src = atlas[id];
    int x = rect.x + (rect.w - src.w) / 2;
    int y = rect.y + (rect.h - src.h) / 2;
    push_rect(ui_rect(x, y, src.w, src.h), src, color);
}

void r_draw_text(const char* text, UI_Vec2 pos, UI_Color color) 
{
    UI_Rect dst = { pos.x, pos.y, 0, 0 };
    for (const char* p = text; *p; p++)
    {
        if ((*p & 0xc0) == 0x80) { continue; }
        int chr = ui_min((unsigned char)*p, 127);
        UI_Rect src = atlas[ATLAS_FONT + chr];
        dst.w = src.w;
        dst.h = src.h;
        push_rect(dst, src, color);
        dst.x += dst.w;
    }
}

int r_get_text_width(const char* text, int len)
{
    int res = 0;
    for (const char* p = text; *p && len--; p++)
    {
        if ((*p & 0xc0) == 0x80) { continue; }
        int chr = ui_min((unsigned char)*p, 127);
        res += atlas[ATLAS_FONT + chr].w;
    }
    return res;
}

int r_get_text_height(void) 
{
    return 18;
}

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

