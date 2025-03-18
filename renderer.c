#define COBJMACROS

#include <d3d11.h>
#include <dxgi1_3.h>
#include <d3dcompiler.h>
#include <dxgidebug.h>
#include "renderer.h"

#define BUFFER_SIZE 16384

typedef struct Renderer_State
{
    ID3D11Device*           device;
    ID3D11DeviceContext*    context;
    IDXGISwapChain1*        swapchain;
    ID3D11Buffer*           vbuffer_pos;
    ID3D11Buffer*           vbuffer_color;
    ID3D11Buffer*           ibuffer;
    ID3D11Buffer*           cbuffer;
    ID3D11InputLayout*      layout;
    ID3D11VertexShader*     vshader;
    ID3D11PixelShader*      pshader;
    ID3D11RenderTargetView* rtview;
    ID3D11Texture2D*        texture;
} Renderer_State;

static float         s_vert_pos_data[BUFFER_SIZE * 8];
static unsigned char s_vert_col_data[BUFFER_SIZE * 16];
static unsigned      s_index_data[BUFFER_SIZE * 6];
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

static void map_vertex_index_buffer(ID3D11DeviceContext* context, ID3D11Buffer* vbuffer_pos,
                             ID3D11Buffer* vbuffer_color, ID3D11Buffer* ibuffer, int client_width,
                             int client_height)
{
    // map: Update vertex (pos) buffer
    D3D11_MAPPED_SUBRESOURCE mapped_pos;
    ID3D11DeviceContext_Map(context, (ID3D11Resource*)vbuffer_pos, 0, D3D11_MAP_WRITE_DISCARD, 0,
                            &mapped_pos);
    memcpy(mapped_pos.pData, s_vert_pos_data, sizeof(float) * 8 * (s_buf_idx));
    ID3D11DeviceContext_Unmap(context, (ID3D11Resource*)vbuffer_pos, 0);

    // map: Update vertex (color) buffer
    D3D11_MAPPED_SUBRESOURCE mapped_color;
    ID3D11DeviceContext_Map(context, (ID3D11Resource*)vbuffer_color, 0, D3D11_MAP_WRITE_DISCARD, 0,
                            &mapped_color);
    memcpy(mapped_color.pData, s_vert_col_data, sizeof(unsigned char) * 16 * (s_buf_idx));
    ID3D11DeviceContext_Unmap(context, (ID3D11Resource*)vbuffer_color, 0);

    // map: Update index buffer
    D3D11_MAPPED_SUBRESOURCE mapped_index;
    ID3D11DeviceContext_Map(context, (ID3D11Resource*)ibuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_index);
    memcpy(mapped_index.pData, s_index_data, sizeof(unsigned) * 6 * (s_buf_idx));
    ID3D11DeviceContext_Unmap(context, (ID3D11Resource*)ibuffer, 0);
}

//
// Renderer functions
//

static void push_rect(UI_Rect rect, UI_Color color)
{
    if (s_buf_idx == BUFFER_SIZE)
    {
        r_present();
    }

    int vertex_pos_idx   = s_buf_idx * 8;
    int vertex_color_idx = s_buf_idx * 16;
    int element_idx      = s_buf_idx * 4;
    int index_idx        = s_buf_idx * 6;
    s_buf_idx++;

    // Update vertex (pos) buffer
    float x = (float)rect.x;
    float y = (float)rect.y;
    float w = (float)rect.w;
    float h = (float)rect.h;

    s_vert_pos_data[vertex_pos_idx + 0] = x;
    s_vert_pos_data[vertex_pos_idx + 1] = y;
    s_vert_pos_data[vertex_pos_idx + 2] = x + w;
    s_vert_pos_data[vertex_pos_idx + 3] = y;
    s_vert_pos_data[vertex_pos_idx + 4] = x;
    s_vert_pos_data[vertex_pos_idx + 5] = y + h;
    s_vert_pos_data[vertex_pos_idx + 6] = x + w;
    s_vert_pos_data[vertex_pos_idx + 7] = y + h;

    // Update vertex (color) buffer
    memcpy(s_vert_col_data + vertex_color_idx + 0, &color, 4);
    memcpy(s_vert_col_data + vertex_color_idx + 4, &color, 4);
    memcpy(s_vert_col_data + vertex_color_idx + 8, &color, 4);
    memcpy(s_vert_col_data + vertex_color_idx + 12, &color, 4);

    // Update index buffer
    s_index_data[index_idx + 0] = element_idx + 0;
    s_index_data[index_idx + 1] = element_idx + 1;
    s_index_data[index_idx + 2] = element_idx + 2;
    s_index_data[index_idx + 3] = element_idx + 2;
    s_index_data[index_idx + 4] = element_idx + 1;
    s_index_data[index_idx + 5] = element_idx + 3;
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

    // Create vertex buffer (pos)
    {
        D3D11_BUFFER_DESC desc = {
            .ByteWidth      = sizeof(s_vert_pos_data),
            .Usage          = D3D11_USAGE_DYNAMIC,
            .BindFlags      = D3D11_BIND_VERTEX_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };

        D3D11_SUBRESOURCE_DATA initial = { .pSysMem = s_vert_pos_data };
        ID3D11Device_CreateBuffer(s_r_state.device, &desc, &initial, &s_r_state.vbuffer_pos);
    }

    // Create vertex buffer (color)
    {
        D3D11_BUFFER_DESC desc = {
            .ByteWidth      = sizeof(s_vert_col_data),
            .Usage          = D3D11_USAGE_DYNAMIC,
            .BindFlags      = D3D11_BIND_VERTEX_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };

        D3D11_SUBRESOURCE_DATA initial = { .pSysMem = s_vert_col_data };
        ID3D11Device_CreateBuffer(s_r_state.device, &desc, &initial, &s_r_state.vbuffer_color);
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

    // Create hlsl
    const char hlsl[] = "cbuffer cbuffer0 : register(b0)                                        \n"
                        "{                                                                      \n"
                        "    float4x4 projection_matrix;                                        \n"
                        "};                                                                     \n"
                        "                                                                       \n"
                        "struct VS_Input                                                        \n"
                        "{                                                                      \n"
                        "    float2 pos : POSITION;                                             \n"
                        "    float4 color : COLOR;                                              \n"
                        "};                                                                     \n"
                        "                                                                       \n"
                        "struct PS_INPUT                                                        \n"
                        "{                                                                      \n"
                        "    float4 pos : SV_POSITION;                                          \n"
                        "    float4 color : COLOR;                                              \n"
                        "};                                                                     \n"
                        "                                                                       \n"
                        "PS_INPUT vs(VS_Input input)                                            \n"
                        "{                                                                      \n"
                        "    PS_INPUT output;                                                   \n"
                        "    output.pos = mul(projection_matrix, float4(input.pos, 0.0f, 1.0f));\n"
                        "    output.color = input.color;                                        \n"
                        "    return output;                                                     \n"
                        "}                                                                      \n"
                        "                                                                       \n"
                        "float4 ps(PS_INPUT input) : SV_TARGET                                  \n"
                        "{                                                                      \n"
                        "    return input.color;                                                \n"
                        "}                                                                      \n";

    // Create input layout, vertex shader, pixel shader
    {
        ID3DBlob* vblob;
        D3DCompile(hlsl, sizeof(hlsl), NULL, NULL, NULL, "vs", "vs_5_0", 0, 0, &vblob, NULL);
        ID3DBlob* pblob;
        D3DCompile(hlsl, sizeof(hlsl), NULL, NULL, NULL, "ps", "ps_5_0", 0, 0, &pblob, NULL);

        // clang-format off
        D3D11_INPUT_ELEMENT_DESC desc[] =
        {
            // SemanticName, Format,                     InputSlot,  AlignedByteOffset
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0,          0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 1,          0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };
        // clang-format on

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
    push_rect(rect, color);
}

static void flush()
{
    // Map vertex & index buffer
    map_vertex_index_buffer(s_r_state.context, s_r_state.vbuffer_pos, s_r_state.vbuffer_color, s_r_state.ibuffer,
                            g_client_width, g_client_height);

    // Setup orthographic projection matrix into constant buffer
    map_mvp_to_cbuffer(s_r_state.context, s_r_state.cbuffer, g_client_width, g_client_height);

    // Set viewport
    D3D11_VIEWPORT viewport = { 0, 0, (FLOAT)g_client_width, (FLOAT)g_client_height, 0, 1 };

    // IA-VS-RS-PS-OM, Draw, Present!
    unsigned      strides[2]  = { sizeof(float) * 2, sizeof(unsigned char) * 4 };
    unsigned      offsets[2]  = { 0, 0 };
    ID3D11Buffer* vbuffers[2] = { s_r_state.vbuffer_pos, s_r_state.vbuffer_color };
    ID3D11DeviceContext_IASetInputLayout(s_r_state.context, s_r_state.layout); // IA: Input Assembly
    ID3D11DeviceContext_IASetPrimitiveTopology(s_r_state.context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetVertexBuffers(s_r_state.context, 0, 2, vbuffers, strides, offsets);
    ID3D11DeviceContext_IASetIndexBuffer(s_r_state.context, s_r_state.ibuffer, DXGI_FORMAT_R32_UINT, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(s_r_state.context, 0, 1, &s_r_state.cbuffer);
    ID3D11DeviceContext_VSSetShader(s_r_state.context, s_r_state.vshader, NULL, 0); // VS: Vertex Shader
    ID3D11DeviceContext_RSSetViewports(s_r_state.context, 1, &viewport); // RS: Rasterizer Stage
    ID3D11DeviceContext_PSSetShader(s_r_state.context, s_r_state.pshader, NULL, 0); // PS: Pixel Shader
    ID3D11DeviceContext_OMSetRenderTargets(s_r_state.context, 1, &s_r_state.rtview, NULL); // OM: Output Merger
    ID3D11DeviceContext_DrawIndexed(s_r_state.context, s_buf_idx * 6, 0, 0);

    // Reset buf_idx
    s_buf_idx = 0;
}

void r_present()
{
    flush();
    IDXGISwapChain1_Present(s_r_state.swapchain, 1, 0);
}

void r_clean()
{
    ID3D11RenderTargetView_Release(s_r_state.rtview);
    ID3D11Buffer_Release(s_r_state.vbuffer_pos);
    ID3D11Buffer_Release(s_r_state.vbuffer_color);
    ID3D11Buffer_Release(s_r_state.ibuffer);
    ID3D11Buffer_Release(s_r_state.cbuffer);
    ID3D11InputLayout_Release(s_r_state.layout);
    ID3D11VertexShader_Release(s_r_state.vshader);
    ID3D11PixelShader_Release(s_r_state.pshader);
    IDXGISwapChain1_Release(s_r_state.swapchain);
    ID3D11DeviceContext_Release(s_r_state.context);
    ID3D11Device_Release(s_r_state.device);
}

