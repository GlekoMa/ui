#pragma warning(disable: 4068)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#define STB_TRUETYPE_IMPLEMENTATION
#include "thirdparty/stb_truetype.h"
#pragma clang diagnostic pop

#define COBJMACROS
#include <windows.h>
#include <shlwapi.h>
#include <stdlib.h>
#include <stdio.h>
#include "ui.h"
#include "renderer.h"

enum { ATLAS_WIDTH = 1200, ATLAS_HEIGHT = 1200 };

//
// Atlas save helper
//

static void get_fixed_dir_from_appdata(RendererState* r_state, const char* dir_name, char* fixed_dir)
{
    char appdata[MAX_PATH];
    GetEnvironmentVariableA("APPDATA", appdata, sizeof(appdata));
    strcpy(fixed_dir, appdata);
    char fixed_dir_suffix[256] = { 0 };
    wsprintfA(fixed_dir_suffix, "\\%s", dir_name);
    strcat(fixed_dir, fixed_dir_suffix);
}

static void save_atlas_cache(RendererState* r_state, const char* fixed_dir, const unsigned char* temp_bitmap)
{
    // For development: Recursively delete the target directory if it exists
    if (PathFileExistsA(fixed_dir))
    {
        char del_path[MAX_PATH + 2]; // +2 for double null termination
        strcpy(del_path, fixed_dir);
        del_path[strlen(fixed_dir) + 1] = 0; // double null terminate

        SHFILEOPSTRUCTA file_op = {
            NULL,
            FO_DELETE,
            del_path,
            NULL,
            FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT,
            FALSE,
            NULL,
            NULL
        };
        SHFileOperationA(&file_op);
    }

    // Create the fixed dir
    CreateDirectoryA(fixed_dir, NULL);

    // Create atlas.raw
    char fixed_atlas_raw_path[MAX_PATH];
    sprintf(fixed_atlas_raw_path, "%s\\atlas.raw", fixed_dir);
    FILE* fp_raw = fopen(fixed_atlas_raw_path, "wb");
    fwrite(temp_bitmap, 1, ATLAS_WIDTH * ATLAS_HEIGHT, fp_raw);
    fclose(fp_raw);

    // Create atlas.dat
    char fixed_atlas_dat_path[MAX_PATH];
    sprintf(fixed_atlas_dat_path, "%s\\atlas.dat", fixed_dir);
    FILE* fp = fopen(fixed_atlas_dat_path, "wb");
    fwrite(r_state->atlas, sizeof(Atlas), NUM_CHARS, fp);
    fclose(fp);
}

static void load_atlas_cache(RendererState* r_state, const char* fixed_dir, unsigned char* temp_bitmap)
{
    // Load raw
    char fixed_atlas_raw_path[MAX_PATH];
    sprintf(fixed_atlas_raw_path, "%s\\atlas.raw", fixed_dir);
    FILE* fp_raw = fopen(fixed_atlas_raw_path, "rb");
    fread(temp_bitmap, 1, ATLAS_WIDTH * ATLAS_HEIGHT, fp_raw);
    fclose(fp_raw);

    // Load dat
    char fixed_atlas_dat_path[MAX_PATH];
    sprintf(fixed_atlas_dat_path, "%s\\atlas.dat", fixed_dir);
    FILE* fp = fopen(fixed_atlas_dat_path, "rb");
    fread(r_state->atlas, sizeof(Atlas), NUM_CHARS, fp);
    fclose(fp);
}

//
// Renderer helper
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
    IDXGIFactory_MakeWindowAssociation(factory, window, DXGI_MWA_NO_ALT_ENTER);
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

static void map_vertex_index_buffer(RendererState* r_state, ID3D11DeviceContext* context, ID3D11Buffer* vbuffer, ID3D11Buffer* ibuffer,
                                    int client_width, int client_height)
{
    // map: Update vertex buffer
    D3D11_MAPPED_SUBRESOURCE mapped_vert;
    ID3D11DeviceContext_Map(context, (ID3D11Resource*)vbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0,
                            &mapped_vert);
    memcpy(mapped_vert.pData, r_state->vert_data, sizeof(Vertex) * 4 * (r_state->buf_idx));
    ID3D11DeviceContext_Unmap(context, (ID3D11Resource*)vbuffer, 0);

    // map: Update index buffer
    D3D11_MAPPED_SUBRESOURCE mapped_index;
    ID3D11DeviceContext_Map(context, (ID3D11Resource*)ibuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_index);
    memcpy(mapped_index.pData, r_state->index_data, sizeof(unsigned) * 6 * (r_state->buf_idx));
    ID3D11DeviceContext_Unmap(context, (ID3D11Resource*)ibuffer, 0);
}

static void get_atlas(RendererState* r_state, unsigned char* temp_bitmap)
{
    // If atlas cache exists, *early return*
    char fixed_dir[MAX_PATH];
    get_fixed_dir_from_appdata(r_state, "ui", fixed_dir);
    if (PathFileExistsA(fixed_dir))
    {
        load_atlas_cache(r_state, fixed_dir, temp_bitmap);
        return;
    }

    // Store 3500 regularly used characters and 22 punctuations for Simplified Chinese.
    // Sourced from https://zh.wiktionary.org/wiki/%E9%99%84%E5%BD%95:%E7%8E%B0%E4%BB%A3%E6%B1%89%E8%AF%AD%E5%B8%B8%E7%94%A8%E5%AD%97%E8%A1%A8
    // And https://zh.wikipedia.org/wiki/%E6%A0%87%E7%82%B9%E7%AC%A6%E5%8F%B7 for punctuation
    short accumulative_offsets_from_0x4E00[] = {
        -19785,8029,4,1,3,1,9,4059,1,6,1,1,1,5,1,7663,1,2,4,1,1,1,1,2,1,2,1,2,1,2,2,1,1,1,1,1,5,2,1,2,3,3,3,2,2,4,1,1,1,2,1,5,2,3,1,2,1,1,1,1,
        1,2,1,1,2,2,1,4,1,1,1,1,5,10,1,2,11,8,2,1,2,1,2,1,2,1,2,1,5,1,6,3,1,1,1,2,2,1,1,1,4,8,5,1,1,4,1,1,3,1,2,1,3,2,1,2,1,1,1,10,
        1,1,5,2,4,2,4,1,4,2,2,2,9,3,2,1,1,6,1,1,1,4,1,1,4,2,4,5,1,4,2,2,2,2,7,3,7,1,1,2,2,2,4,2,1,4,3,6,10,12,5,4,3,2,14,2,3,3,2,1,
        1,1,6,1,6,10,4,1,6,5,1,7,1,5,4,8,4,1,1,2,9,19,5,2,4,1,1,5,2,5,20,2,2,9,7,1,11,2,9,17,1,8,1,5,8,27,4,6,9,20,11,13,14,6,23,15,30,2,2,1,
        1,1,2,1,2,2,4,3,6,2,6,3,3,3,1,1,3,1,2,1,1,1,1,1,3,1,1,3,5,3,4,1,5,3,2,2,2,1,4,4,8,3,1,2,1,2,1,1,4,5,4,2,3,3,3,2,10,2,3,1,
        3,7,2,2,1,3,3,2,1,1,1,2,2,1,1,2,3,1,3,7,1,5,1,1,1,1,2,3,4,4,1,2,3,2,6,1,1,1,1,1,2,5,1,7,3,4,3,2,15,2,2,1,5,3,13,9,19,2,1,1,
        1,1,2,5,1,1,1,6,1,1,12,4,4,2,2,7,6,7,5,22,4,1,1,5,1,2,13,1,1,2,7,3,7,15,1,1,3,1,2,2,4,1,2,4,1,2,1,1,2,1,1,3,2,4,1,1,2,2,1,4,
        5,1,2,1,1,2,1,7,3,3,1,3,2,1,9,3,2,5,3,4,2,19,4,2,1,6,1,1,1,1,1,4,3,2,1,1,1,2,5,3,1,1,1,2,2,1,1,1,1,1,1,2,1,3,1,1,1,3,1,4,
        2,1,2,2,1,1,2,1,1,1,1,1,2,2,2,4,2,1,1,1,6,1,1,1,2,1,1,1,1,2,3,1,3,1,2,1,4,6,2,2,6,5,3,3,1,6,6,11,2,6,1,1,9,6,3,1,2,3,1,3,
        14,1,2,2,5,2,5,5,3,1,3,2,2,5,1,3,6,8,6,3,1,1,3,1,4,8,2,5,5,1,2,7,16,4,3,5,2,1,2,13,5,1,2,4,23,3,1,1,10,8,4,6,2,3,2,1,14,4,1,10,
        12,4,4,10,14,9,5,3,2,23,3,1,8,40,1,2,2,3,6,41,1,1,36,21,20,5,14,16,1,3,2,2,2,9,3,1,3,6,3,1,5,3,2,23,4,5,8,10,4,2,7,3,4,1,1,1,6,3,1,2,
        1,1,1,1,3,2,4,5,8,11,1,1,7,7,9,7,4,5,3,20,1,8,3,17,1,25,1,8,4,15,12,3,6,6,5,23,5,3,4,6,13,24,2,14,6,5,10,1,24,20,15,7,3,2,3,3,3,11,3,6,
        2,6,1,4,2,3,8,2,1,1,2,1,1,2,3,3,1,1,1,10,3,1,1,2,4,2,3,1,1,1,9,2,3,14,1,2,2,1,4,5,2,2,1,1,10,1,3,3,12,3,17,2,11,4,1,5,1,2,1,6,
        2,9,3,19,4,2,2,1,3,17,4,13,8,5,16,3,17,26,2,9,19,8,25,14,1,7,3,21,8,32,71,4,1,2,1,1,4,2,4,1,2,3,12,8,4,2,2,2,1,1,2,1,3,8,1,1,1,1,1,1,
        1,2,1,1,1,1,2,4,1,5,3,1,1,1,3,4,1,1,3,2,2,1,5,6,1,10,1,1,2,4,3,16,1,1,1,1,3,2,3,2,3,1,5,2,3,2,2,2,3,7,13,7,2,2,1,1,1,1,1,1,
        3,3,1,1,1,3,1,2,4,9,2,1,4,10,2,8,6,2,1,18,2,1,4,14,4,6,5,41,5,7,3,11,12,7,6,2,19,4,31,129,16,1,3,1,3,1,1,1,1,2,3,3,1,2,3,7,3,1,1,2,
        1,2,4,4,5,1,2,2,2,1,9,7,1,10,5,8,7,8,1,13,16,1,1,2,2,3,1,1,2,5,2,1,3,5,1,3,1,1,2,2,3,2,1,7,1,6,8,1,1,1,17,1,9,35,1,3,6,2,1,1,
        6,5,4,2,6,4,1,5,1,1,8,2,8,1,24,1,2,13,2,5,1,2,1,3,1,8,2,1,4,1,3,1,3,2,1,5,2,5,1,1,8,9,4,9,6,6,2,1,6,1,10,1,1,7,7,4,6,4,8,2,
        1,1,13,4,2,1,1,6,1,3,5,2,1,2,5,12,8,8,2,3,2,3,13,2,4,1,3,1,2,1,3,3,6,8,5,4,7,11,1,3,3,2,4,3,3,2,8,9,5,1,6,4,7,4,6,1,1,1,2,2,
        2,1,3,3,3,8,7,1,6,6,5,5,5,3,24,9,4,2,7,13,5,1,8,7,20,3,6,20,22,4,6,2,8,20,34,7,1,1,1,4,2,2,16,9,1,3,8,1,1,6,4,2,1,3,1,1,1,4,3,8,
        4,2,2,1,1,1,1,1,3,3,3,3,2,1,1,4,6,7,1,1,2,1,1,1,2,1,5,1,1,2,1,6,1,5,4,4,3,1,5,2,1,1,1,2,3,1,3,2,1,1,2,1,1,1,2,1,3,3,1,2,
        1,1,1,1,3,1,2,2,2,1,3,5,2,1,2,1,5,2,5,3,5,4,5,1,1,2,1,1,3,2,1,4,11,3,5,3,1,3,3,1,1,1,1,5,9,1,2,1,1,4,7,8,1,3,1,5,2,6,1,3,
        3,1,2,4,2,8,2,3,2,1,1,1,6,7,1,2,15,4,2,1,2,4,11,2,6,1,3,7,9,3,1,1,3,10,4,1,8,2,12,2,1,13,10,2,1,3,10,4,15,2,15,1,14,10,1,3,9,6,5,3,
        1,1,2,5,7,6,3,8,1,4,20,26,18,6,23,7,3,2,3,1,6,3,4,3,2,8,2,3,4,1,3,6,4,2,2,3,16,4,6,6,2,3,3,5,1,2,2,4,2,1,9,4,4,4,6,4,8,9,2,3,
        1,1,1,1,3,1,4,5,1,3,8,4,6,2,1,4,1,5,6,1,5,2,1,5,2,6,7,2,5,8,1,6,1,2,5,10,2,2,6,1,1,4,2,4,4,4,5,10,5,1,23,6,37,25,2,5,3,2,1,1,
        8,1,2,2,10,4,2,2,7,2,2,1,1,3,2,3,1,5,3,3,2,1,3,2,1,5,1,1,1,5,6,3,1,1,4,3,5,2,1,14,1,2,3,5,7,5,2,3,2,1,5,1,7,1,4,7,1,13,11,1,
        1,1,1,1,8,4,5,7,5,2,1,11,6,2,1,3,4,2,2,3,1,10,9,13,1,1,3,1,5,1,2,1,2,4,4,1,18,2,1,2,1,13,11,4,1,17,11,4,1,1,5,2,1,3,13,9,2,2,5,3,
        3,2,6,14,3,4,5,11,8,1,4,27,3,15,21,6,4,5,20,5,6,2,2,14,1,6,1,12,12,28,45,13,21,2,9,7,19,20,1,8,16,15,16,25,3,116,1,1,1,4,11,8,4,9,2,3,22,1,1,1,
        1,1,3,15,2,1,7,6,1,1,11,30,1,2,8,2,4,8,2,3,2,1,4,2,6,10,4,32,2,2,1,7,7,5,1,6,1,5,4,9,1,5,2,14,4,3,1,1,1,3,6,6,9,4,6,5,1,7,9,2,
        4,2,4,1,1,3,1,3,5,5,1,2,1,1,1,1,5,5,1,2,9,6,3,3,1,1,2,3,2,6,3,2,6,1,1,4,10,7,5,4,3,7,5,8,9,1,1,1,3,4,1,1,3,1,3,3,2,6,13,3,
        1,4,6,3,1,10,6,1,3,2,7,6,2,4,2,1,2,1,1,1,5,1,3,3,11,6,5,1,5,7,9,3,7,3,3,2,4,2,2,10,5,6,4,3,9,1,2,1,5,6,5,4,2,9,19,2,38,1,4,2,
        4,7,12,6,8,5,7,4,17,6,2,1,6,4,3,3,1,3,1,11,14,4,9,4,1,12,9,2,6,13,26,4,10,7,1,22,4,6,14,5,18,13,18,63,59,31,2,2,1,5,1,2,4,2,1,10,1,4,4,3,
        22,1,1,1,10,1,3,5,1,6,16,1,2,4,5,2,1,4,2,12,17,11,4,1,12,10,6,22,2,16,6,3,7,22,6,5,5,5,6,13,23,11,7,16,33,36,2,5,4,1,1,1,1,4,10,1,4,1,12,2,
        6,1,5,2,9,3,4,1,6,1,43,3,7,3,9,6,8,7,7,2,1,11,1,1,2,1,7,4,18,8,5,1,13,1,1,1,2,6,10,1,69,3,2,2,11,5,14,2,4,1,2,5,4,15,3,19,13,22,2,1,
        3,7,18,17,1,8,34,1,17,19,36,53,6,1,1,2,8,8,1,33,2,2,3,6,3,1,2,5,1,1,1,2,2,1,3,10,7,3,5,5,3,9,1,4,10,4,14,9,2,6,2,1,5,5,7,3,1,3,7,3,
        2,7,2,3,8,3,3,3,7,8,6,4,5,38,5,2,3,1,1,13,6,14,18,5,24,2,1,4,2,2,1,39,3,14,6,1,2,2,5,1,1,1,2,2,1,1,3,4,15,1,3,2,4,1,3,2,3,8,2,20,
        1,8,7,7,1,5,4,1,26,6,2,3,6,13,11,10,4,21,3,2,1,6,8,28,4,7,3,4,2,2,1,5,11,1,2,1,10,1,7,2,4,22,4,4,6,2,5,16,8,14,1,2,14,13,3,1,1,3,6,1,
        7,8,9,1,2,1,10,3,4,16,19,15,3,7,57,2,2,10,14,7,1,1,1,5,3,5,10,1,8,1,14,44,2,1,2,1,2,3,3,2,2,4,1,3,3,7,5,2,1,2,2,4,1,8,3,2,3,11,2,1,
        12,6,19,8,1,1,2,7,17,29,2,1,3,5,2,2,1,9,4,1,4,1,1,4,1,2,6,26,12,11,3,5,1,1,3,2,8,2,10,6,7,5,6,3,5,2,9,2,2,4,16,13,2,4,1,1,1,2,2,5,
        2,26,2,5,2,13,8,2,10,8,2,2,4,22,12,6,8,13,3,6,16,49,7,14,38,8,2,12,9,5,1,7,5,1,5,4,3,8,5,12,11,1,3,3,3,1,15,12,15,22,2,5,4,4,63,211,95,2,2,2,
        1,3,1,1,3,2,1,1,2,2,1,1,1,3,2,4,1,1,1,1,1,2,3,1,1,2,1,1,2,3,1,3,1,1,1,3,1,4,2,1,3,3,3,1,1,2,1,4,1,2,1,5,1,8,5,1,1,1,2,2,
        3,3,4,4,1,4,3,4,4,2,22,1,4,2,3,8,7,1,4,4,24,4,6,10,3,3,21,4,4,4,9,6,4,8,9,7,11,1,4,1,2,2,7,1,3,5,2,1,1,26,5,3,2,2,3,8,1,1,8,4,
        2,16,25,1,2,3,2,1,10,2,2,1,2,3,1,1,2,1,4,1,4,1,3,2,6,4,1,1,1,2,3,6,2,8,4,2,2,3,6,8,1,3,3,2,5,5,4,3,1,5,1,1,2,3,4,21,2,7,6,12,
        1,1,4,4,1,16,9,2,9,1,1,3,1,1,10,5,9,3,1,1,11,11,13,2,8,25,7,3,6,1,8,4,5,1,6,1,5,2,10,1,11,2,4,1,4,1,1,2,14,17,23,1,2,1,7,4,4,9,2,5,
        7,3,1,8,1,6,1,2,2,2,6,4,10,6,2,5,3,4,3,1,6,1,5,6,8,8,1,1,1,1,4,5,25,4,1,8,1,1,2,14,3,7,2,2,6,4,2,1,2,1,3,8,8,1,17,34,6,1,5,2,
        1,3,10,3,2,16,4,9,8,1,18,8,1,1,15,7,1,2,1,21,26,4,6,2,8,1,5,4,13,9,14,3,22,6,7,5,5,13,7,15,37,2,4,3,17,1,16,1,12,1,42,10,6,3,20,15,5,32,1,5,
        15,23,22,39,22,1,1,1,9,17,6,8,4,1,2,1,1,8,2,7,2,7,7,1,6,5,17,6,1,2,2,9,5,2,9,10,11,5,2,2,6,10,1,2,2,1,4,5,26,12,2,3,2,9,2,7,20,2,13,10,
        18,27,6,6,5,46,28,13,30,5,7,1,7,3,2,8,2,2,3,1,2,1,4,7,10,3,7,2,5,4,6,15,2,4,16,1,3,1,15,4,11,15,5,1,9,14,2,19,5,53,32,2,5,59,1,2,1,1,2,1,
        9,17,3,26,137,1,9,211,6,53,1,2,1,3,1,4,1,1,1,2,1,3,2,1,1,2,1,1,1,1,1,3,1,1,2,1,1,3,4,4,2,3,3,1,3,1,3,1,5,1,1,2,2,1,2,1,2,1,2,1,
        2,1,3,2,2,1,2,2,1,2,1,2,2,1,7,2,6,1,1,2,2,4,1,4,3,3,10,5,6,21,9,1,14,1,18,145,1,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,1,2,1,1,
        1,2,2,1,2,1,1,1,6,1,1,2,1,1,4,2,2,2,1,3,2,1,1,2,2,5,5,4,1,1,1,10,4,5,1,20,4,16,1,10,5,8,1,5,10,2,2,9,2,5,4,2,2,3,16,5,19,7,1,7,
        17,2,4,3,27,6,1,5,15,14,28,1,3,3,8,172,1,1,1,3,2,1,1,4,7,2,2,4,2,1,2,1,7,1,2,2,1,3,2,1,2,1,4,5,1,2,5,1,8,4,1,3,1,2,1,2,1,6,2,1,
        3,4,1,2,1,1,1,1,12,5,7,2,4,3,1,1,1,3,3,1,5,1,2,2,3,3,1,2,1,1,1,2,12,10,3,1,2,4,5,6,2,4,4,12,2,8,1,7,10,1,17,2,15,1,3,4,4,13,6,9,
        4,3,23,5,16,28,51,1,5,5,6,6,2,5,2,5,4,2,1,7,8,2,2,7,53,3,2,1,1,1,2,163,532,1,10,6,4,1,1,1,2,3,1,1,2,5,3,2,8,3,3,2,2,1,10,11,1,1,4,2,
        10,5,4,2,2,4,2,1,3,1,3,1,2,1,4,9,1,7,2,1,1,1,2,5,1,1,1,9,2,5,7,3,6,12,7,13,6,9,105,2,3,1,1,1,2,2,3,1,1,1,1,2,3,1,4,9,2,4,11,18,
        1,1,1,1,1,5,4,5,1,1,2,3,1,1,3,5,12,1,2,4,1,1,11,1,1,14,1,9,1,4,4,1,3,11,15,8,2,1,3,1,1,1,5,3,6,19,2,9,3,1,2,5,2,7,2,4,1,14,2,20,
        6,1,25,4,3,5,2,1,1,7,11,2,21,2,19,13,58,2,4,6,2,128,1,1,2,1,1,2,1,1,2,1,2,1,1,1,1,2,7,2,3,1,1,4,1,3,4,42,4,6,6,1,49,85,8,1,2,1,1,3,
        1,4,2,3,2,4,1,5,2,2,3,4,3,211,2,1,1,1,2,1,2,3,2,1,2,4,2,2,1,5,3,2,6,3,7,3,4,43,5,59,41,5,1,2,11,5,296,5,27,8,7,13,12,9,9,8,321,1,1,2,
        2,1,7,2,4,2,8,2,4,2,4,1,5,21,2,10,15,39,18,3,9,9,1,3,3,4,54,5,13,27,21,47,5,21,6,24418,7,1,3,14,1,4
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

    for (int i = 0; i < NUM_CHARS_SYMBOL; i++) { r_state->atlas[i+1].codepoint = codepoints_symbol[i]; }
    for (int i = 0; i < NUM_CHARS_ASCII; i++) { r_state->atlas[i+1+NUM_CHARS_SYMBOL].codepoint = codepoints_ascii[i]; }
    for (int i = 0; i < NUM_CHARS_ZH; i++) { r_state->atlas[i+1+NUM_CHARS_SYMBOL+NUM_CHARS_ASCII].codepoint = codepoints_zh[i]; }

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
            r_state->atlas[i+1].src.x = cdata[i].x0;
            r_state->atlas[i+1].src.y = cdata[i].y0;
            r_state->atlas[i+1].src.w = cdata[i].x1 - cdata[i].x0;
            r_state->atlas[i+1].src.h = cdata[i].y1 - cdata[i].y0;
            r_state->atlas[i+1].xoff = (int)cdata[i].xoff;
            r_state->atlas[i+1].yoff = (int)cdata[i].yoff;
            r_state->atlas[i+1].xadvance = (int)cdata[i].xadvance;
        }

        // Add a special 3x3 white square at the right-bottom corner of the bitmap as ATLAS_WHITE
        {
            int white_box_size = 3;
            int white_box_x = ATLAS_WIDTH - white_box_size;
            int white_box_y = ATLAS_HEIGHT - white_box_size;
            for (int y = 0; y < white_box_size; y++)
            {
                for (int x = 0; x < white_box_size; x++)
                {
                    temp_bitmap[(white_box_y + y) * ATLAS_WIDTH + (white_box_x + x)] = 255;
                }
            }
            r_state->atlas[0].src.x = white_box_x;
            r_state->atlas[0].src.y = white_box_y;
            r_state->atlas[0].src.w = white_box_size;
            r_state->atlas[0].src.h = white_box_size;
            r_state->atlas[0].xoff = 0;
            r_state->atlas[0].yoff = 0;
            r_state->atlas[0].xadvance = white_box_size;
        }
    }
    save_atlas_cache(r_state, fixed_dir, temp_bitmap);
}

//
// Renderer
//

static void flush(RendererState* r_state, UI_Rect* viewport_rect, ID3D11ShaderResourceView* srview, 
        ID3D11RenderTargetView* rtview, ID3D11Buffer* vbuffer, ID3D11Buffer* ibuffer, int index_count)
{
    ID3D11ShaderResourceView* use_srview = srview ? srview : r_state->srview;
    ID3D11RenderTargetView* use_rtview = rtview ? rtview : r_state->rtview;
    ID3D11Buffer* use_vbuffer = vbuffer ? vbuffer : r_state->vbuffer;
    ID3D11Buffer* use_ibuffer = vbuffer ? ibuffer : r_state->ibuffer;
    int use_index_count = index_count ? index_count : r_state->buf_idx * 6;

    // Set viewport
    D3D11_VIEWPORT viewport = { .MinDepth = 0, .MaxDepth = 1 };
    if (viewport_rect)
    {
        viewport.TopLeftX = (FLOAT)viewport_rect->x;
        viewport.TopLeftY = (FLOAT)viewport_rect->y;
        viewport.Width = (FLOAT)viewport_rect->w;
        viewport.Height = (FLOAT)viewport_rect->h;
    }
    else
    {
        viewport.TopLeftX = 0;
        viewport.TopLeftY = 0;
        viewport.Width = (FLOAT)r_state->client_width;
        viewport.Height = (FLOAT)r_state->client_height;
    }
    // Setup orthographic projection matrix into constant buffer (use viewport setting)
    map_mvp_to_cbuffer(r_state->context, r_state->cbuffer, (int)viewport.Width, (int)viewport.Height);

    // Map vertex & index buffer
    if (!vbuffer && !ibuffer)
        map_vertex_index_buffer(r_state, r_state->context, r_state->vbuffer, r_state->ibuffer, r_state->client_width, r_state->client_height);

    // IA-VS-RS-PS-OM, Draw, Present!
    unsigned stride = sizeof(Vertex);
    unsigned offset = 0;
    ID3D11DeviceContext_IASetInputLayout(r_state->context, r_state->layout); // IA: Input Assembly
    ID3D11DeviceContext_IASetPrimitiveTopology(r_state->context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetVertexBuffers(r_state->context, 0, 1, &use_vbuffer, &stride, &offset);
    ID3D11DeviceContext_IASetIndexBuffer(r_state->context, use_ibuffer, DXGI_FORMAT_R32_UINT, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(r_state->context, 0, 1, &r_state->cbuffer);
    ID3D11DeviceContext_VSSetShader(r_state->context, r_state->vshader, NULL, 0); // VS: Vertex Shader
    ID3D11DeviceContext_RSSetViewports(r_state->context, 1, &viewport); // RS: Rasterizer Stage
    ID3D11DeviceContext_RSSetState(r_state->context, r_state->raster_state);
    ID3D11DeviceContext_PSSetShader(r_state->context, r_state->pshader, NULL, 0); // PS: Pixel Shader
    ID3D11DeviceContext_PSSetShaderResources(r_state->context, 0, 1, &use_srview);
    ID3D11DeviceContext_PSSetSamplers(r_state->context, 0, 1, &r_state->sampler_state);
    ID3D11DeviceContext_OMSetRenderTargets(r_state->context, 1, &use_rtview, NULL); // OM: Output Merger
	ID3D11DeviceContext_OMSetBlendState(r_state->context, r_state->blend_state, NULL, 0xffffffff);
    ID3D11DeviceContext_DrawIndexed(r_state->context, use_index_count, 0, 0);

    // Reset buf_idx
    r_state->buf_idx = 0;
}

static void push_rect(RendererState* r_state, UI_Rect dst, UI_Rect src, UI_Color color, int tex_index)
{
    if (r_state->buf_idx == BUFFER_SIZE) { flush(r_state, NULL, NULL, NULL, NULL, NULL, 0); }

    int vert_idx  = r_state->buf_idx * 4;
    int index_idx = r_state->buf_idx * 6;
    r_state->buf_idx++;

    // Update vertex buffer (pos)
    // (adding +0.5 to fix the integer to float conversion precision loss)
    r_state->vert_data[vert_idx + 0].pos[0] = (float)dst.x + 0.5f;
    r_state->vert_data[vert_idx + 0].pos[1] = (float)dst.y + 0.5f;
    r_state->vert_data[vert_idx + 1].pos[0] = (float)dst.x + 0.5f + dst.w;
    r_state->vert_data[vert_idx + 1].pos[1] = (float)dst.y + 0.5f;
    r_state->vert_data[vert_idx + 2].pos[0] = (float)dst.x + 0.5f;
    r_state->vert_data[vert_idx + 2].pos[1] = (float)dst.y + 0.5f + dst.h;
    r_state->vert_data[vert_idx + 3].pos[0] = (float)dst.x + 0.5f + dst.w;
    r_state->vert_data[vert_idx + 3].pos[1] = (float)dst.y + 0.5f + dst.h;

    // Update vbuffer (uv)
    if (tex_index == 0)
    {
        float x = src.x / (float)ATLAS_WIDTH;
        float y = src.y / (float)ATLAS_HEIGHT;
        float w = src.w / (float)ATLAS_WIDTH;
        float h = src.h / (float)ATLAS_HEIGHT;
        r_state->vert_data[vert_idx + 0].uv[0] = x;
        r_state->vert_data[vert_idx + 0].uv[1] = y;
        r_state->vert_data[vert_idx + 1].uv[0] = x + w;
        r_state->vert_data[vert_idx + 1].uv[1] = y;
        r_state->vert_data[vert_idx + 2].uv[0] = x;
        r_state->vert_data[vert_idx + 2].uv[1] = y + h;
        r_state->vert_data[vert_idx + 3].uv[0] = x + w;
        r_state->vert_data[vert_idx + 3].uv[1] = y + h;
    }
    else
    {
        r_state->vert_data[vert_idx + 0].uv[0] = 0;
        r_state->vert_data[vert_idx + 0].uv[1] = 0;
        r_state->vert_data[vert_idx + 1].uv[0] = 1;
        r_state->vert_data[vert_idx + 1].uv[1] = 0;
        r_state->vert_data[vert_idx + 2].uv[0] = 0;
        r_state->vert_data[vert_idx + 2].uv[1] = 1;
        r_state->vert_data[vert_idx + 3].uv[0] = 1;
        r_state->vert_data[vert_idx + 3].uv[1] = 1;
    }

     // Update vbuffer (col & tex_index)
     for (int i = 0; i < 4; i++)
     {
         memcpy((char*)(r_state->vert_data + vert_idx + i) + offsetof(Vertex, col), &color, 4);
         r_state->vert_data[vert_idx + i].tex_index = tex_index;
     }

    // Update index buffer
    r_state->index_data[index_idx + 0] = vert_idx + 0;
    r_state->index_data[index_idx + 1] = vert_idx + 1;
    r_state->index_data[index_idx + 2] = vert_idx + 2;
    r_state->index_data[index_idx + 3] = vert_idx + 2;
    r_state->index_data[index_idx + 4] = vert_idx + 1;
    r_state->index_data[index_idx + 5] = vert_idx + 3;
}

void r_init(RendererState* r_state)
{
    // Create device and context
    // (Need to add BGRA support for compatibility with Direct2D)
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifndef NDEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, levels, ARRAYSIZE(levels),
                      D3D11_SDK_VERSION, &r_state->device, NULL, &r_state->context);

#ifndef NDEBUG
    // for debug builds enable VERY USEFUL debug break on API errors
    {
        ID3D11InfoQueue* info;
        ID3D11Device_QueryInterface(r_state->device, &IID_ID3D11InfoQueue, (void**)&info);
        ID3D11InfoQueue_SetBreakOnSeverity(info, D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        ID3D11InfoQueue_SetBreakOnSeverity(info, D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
        ID3D11InfoQueue_Release(info);
    }

    // enable debug break for DXGI too
    {
        IDXGIInfoQueue* dxgiInfo;
        HRESULT hr = DXGIGetDebugInterface1(0, &IID_IDXGIInfoQueue, (void**)&dxgiInfo);
        expect((SUCCEEDED(hr)));
        IDXGIInfoQueue_SetBreakOnSeverity(dxgiInfo, DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        IDXGIInfoQueue_SetBreakOnSeverity(dxgiInfo, DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, TRUE);
        IDXGIInfoQueue_Release(dxgiInfo);
    }
#endif

    // Create swap chain
    create_swapchain(g_window, r_state->device, &r_state->swapchain);

    // Create sampler state
    {
        D3D11_SAMPLER_DESC desc = {
            .Filter         = D3D11_FILTER_MIN_MAG_MIP_POINT,
            .AddressU       = D3D11_TEXTURE_ADDRESS_WRAP,
            .AddressV       = D3D11_TEXTURE_ADDRESS_WRAP,
            .AddressW       = D3D11_TEXTURE_ADDRESS_WRAP,
            .ComparisonFunc = D3D11_COMPARISON_NEVER,
        };
        ID3D11Device_CreateSamplerState(r_state->device, &desc, &r_state->sampler_state);
    }

    // Create texture buffer (font)
    {
        unsigned char* temp_bitmap = malloc(ATLAS_WIDTH * ATLAS_HEIGHT);
        get_atlas(r_state, temp_bitmap);
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
        ID3D11Device_CreateTexture2D(r_state->device, &desc, &texture_subresource_data, &texture);
        ID3D11Device_CreateShaderResourceView(r_state->device, (ID3D11Resource*)texture, 0, &r_state->srview);
        ID3D11Texture2D_Release(texture);
        free(temp_bitmap);
    }

    // Create vertex buffer
    {
        D3D11_BUFFER_DESC desc = {
            .ByteWidth      = sizeof(r_state->vert_data),
            .Usage          = D3D11_USAGE_DYNAMIC,
            .BindFlags      = D3D11_BIND_VERTEX_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };

        D3D11_SUBRESOURCE_DATA initial = { .pSysMem = r_state->vert_data };
        ID3D11Device_CreateBuffer(r_state->device, &desc, &initial, &r_state->vbuffer);
    }

    // Create index buffer
    {
        D3D11_BUFFER_DESC desc = {
            .ByteWidth      = sizeof(r_state->index_data),
            .Usage          = D3D11_USAGE_DYNAMIC,
            .BindFlags      = D3D11_BIND_INDEX_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };

        D3D11_SUBRESOURCE_DATA initial = { .pSysMem = r_state->index_data };
        ID3D11Device_CreateBuffer(r_state->device, &desc, &initial, &r_state->ibuffer);
    }

    // Create constant buffer for delivering MVP (Model View Projection)
    {
        D3D11_BUFFER_DESC desc = {
            .ByteWidth      = sizeof(float) * 4 * 4, // float mvp[4][4]
            .Usage          = D3D11_USAGE_DYNAMIC,
            .BindFlags      = D3D11_BIND_CONSTANT_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };
        ID3D11Device_CreateBuffer(r_state->device, &desc, NULL, &r_state->cbuffer);
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
                .DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA,
                .BlendOpAlpha = D3D11_BLEND_OP_ADD,
                .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL
            }
        };
        ID3D11Device_CreateBlendState(r_state->device, &desc, &r_state->blend_state);
    }

    // Create rasterizer state
    {
        D3D11_RASTERIZER_DESC desc = {
            .FillMode = D3D11_FILL_SOLID,
            .CullMode = D3D11_CULL_NONE,
            .ScissorEnable = TRUE,
        };
        ID3D11Device_CreateRasterizerState(r_state->device, &desc, &r_state->raster_state);
    }

    // Create input layout, vertex shader, pixel shader
    {
        D3D11_INPUT_ELEMENT_DESC desc[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, offsetof(Vertex, pos),       D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "UV",       0, DXGI_FORMAT_R32G32_FLOAT,   0, offsetof(Vertex, uv),        D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(Vertex, col),       D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXINDEX", 0, DXGI_FORMAT_R32_SINT,       0, offsetof(Vertex, tex_index), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        #include "d3d11_vshader.h"
        #include "d3d11_pshader.h"
        ID3D11Device_CreateVertexShader(r_state->device, d3d11_vshader, sizeof(d3d11_vshader), NULL, &r_state->vshader);
        ID3D11Device_CreatePixelShader(r_state->device, d3d11_pshader, sizeof(d3d11_pshader), NULL, &r_state->pshader);
        ID3D11Device_CreateInputLayout(r_state->device, desc, ARRAYSIZE(desc), d3d11_vshader, sizeof(d3d11_vshader), &r_state->layout);
    }

    // Prepare a void render target view
    r_state->rtview = NULL;

    // Create render target view for backbuffer texture
    ID3D11Texture2D* texture;
    IDXGISwapChain1_GetBuffer(r_state->swapchain, 0, &IID_ID3D11Texture2D, (void**)&texture);
    ID3D11Device_CreateRenderTargetView(r_state->device, (ID3D11Resource*)texture, NULL, &r_state->rtview);
    ID3D11Texture2D_Release(texture);

    // If we don't set clipping, and there are no subsequent controls
    // to set clipping either, D3D11 will render nothing
    r_set_clip_rect(r_state, unclipped_rect);
}

void r_clear(RendererState* r_state, UI_Color color)
{
    float color_f[4] = { color.r / 255.f, color.g / 255.f, color.b / 255.f, color.a / 255.f };
    ID3D11DeviceContext_ClearRenderTargetView(r_state->context, r_state->rtview, color_f);

    // draw dot background
    UI_Rect client_rect = ui_rect(0, 0, r_state->client_width, r_state->client_height);
    UI_Color dot_color = ui_color(
        (int)(color.r * 0.9f),
        (int)(color.g * 0.9f),
        (int)(color.b * 0.9f),
        (int)(color.a)
    );
    const int dot_spacing = 15;
    const int dot_size = 2;
    for (int y = client_rect.y + dot_spacing; y < client_rect.y + client_rect.h; y += dot_spacing) {
        for (int x = client_rect.x + dot_spacing; x < client_rect.x + client_rect.w; x += dot_spacing) {
            UI_Rect dot = ui_rect(x, y, dot_size, dot_size);
            r_draw_rect(r_state, dot, dot_color);
        }
    }

    // draw border
    UI_Color border_color = ui_color(0, 0, 255, 255);
    r_draw_rect(r_state, ui_rect(client_rect.x + 1, client_rect.y, client_rect.w - 2, 1), border_color);
    r_draw_rect(r_state, ui_rect(client_rect.x + 1, client_rect.y + client_rect.h - 1, client_rect.w - 2, 1), border_color);
    r_draw_rect(r_state, ui_rect(client_rect.x, client_rect.y, 1, client_rect.h), border_color);
    r_draw_rect(r_state, ui_rect(client_rect.x + client_rect.w - 1, client_rect.y, 1, client_rect.h), border_color);
}

void r_draw_rect(RendererState* r_state, UI_Rect rect, UI_Color color)
{
    push_rect(r_state, rect, r_state->atlas[0].src, color, 0);
}

void r_draw_icon(RendererState* r_state, int id, UI_Rect rect, UI_Color color)
{
    UI_Rect src = r_state->atlas[id].src;
    int x = rect.x + (rect.w - src.w) / 2;
    int y = rect.y + (rect.h - src.h) / 2;
    push_rect(r_state, ui_rect(x, y, src.w, src.h), src, color, 0);
}

// TODO: need better performance
static int find_atlas_state_idx_by_codepoint(RendererState* r_state, int codepoint)
{
    if (codepoint < 127)
    {
        return codepoint - 32 + NUM_CHARS_SYMBOL + 1;
    }
    else
    {
        for (int i = 0; i < NUM_CHARS; i++)
        {
            if (r_state->atlas[i].codepoint == codepoint)
                return i;
        }
    }
    // TODO: need handle error
    return 63 - 32 + NUM_CHARS_SYMBOL + 1; // '?'
}

void r_draw_text(RendererState* r_state, const wchar_t* text, UI_Vec2 pos, UI_Color color)
{
    UI_Rect dst = { pos.x, pos.y + (int)(24 * 0.8), 0, 0 };
    for (const wchar_t* p = text; *p; p++)
    {
        int chr = find_atlas_state_idx_by_codepoint(r_state, *p);
        UI_Rect src = r_state->atlas[chr].src;
        Atlas* b = &r_state->atlas[chr];

        dst.x += (int)b->xoff;
        dst.y += (int)b->yoff;
        dst.w = src.w;
        dst.h = src.h;
        push_rect(r_state, dst, src, color, 0);

        dst.x += (int)(b->xadvance - b->xoff);
        dst.y -= (int)b->yoff;
    }
}

int r_get_text_width(RendererState* r_state, const wchar_t* text, int len)
{
    int res = 0;
    for (const wchar_t* p = text; *p && len--; p++)
    {
        int chr = find_atlas_state_idx_by_codepoint(r_state, *p);
        res += r_state->atlas[chr].xadvance;
    }
    return res;
}

int r_get_text_height(RendererState* r_state)
{
    return 24;
}

void r_set_clip_rect(RendererState* r_state, UI_Rect rect)
{
    // TODO: Because of this flush, the memory usage will grow to huge (e.g 15MB => 35~80MB).
    //       The key reason is `map_vertex_index_buffer`.
    flush(r_state, NULL, NULL, NULL, NULL, NULL, 0);
    D3D11_RECT scissor_rect = {
        .left = rect.x,
        .top = rect.y,
        .right = rect.x + rect.w,
        .bottom = rect.y + rect.h,
    };
    ID3D11DeviceContext_RSSetScissorRects(r_state->context, 1, &scissor_rect);
}

static ImageResource* r_load_image(IWICImagingFactory* img_factory, RendererState* r_state, const char* path)
{
    // load image data
    unsigned width, height;
    unsigned char* data;
    data = image_load(img_factory, path, &width, &height);
    expect(data);

    // create texture view
    ID3D11Texture2D* texture;
    {
        D3D11_TEXTURE2D_DESC desc =
        {
            .Width = width,
            .Height = height,
            .MipLevels = 1,
            .ArraySize = 1,
            .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
            .SampleDesc.Count = 1,
            .Usage = D3D11_USAGE_IMMUTABLE,
            .BindFlags = D3D11_BIND_SHADER_RESOURCE,
        };
        D3D11_SUBRESOURCE_DATA initial =
        {
            .pSysMem = data,
            .SysMemPitch = width * 4,
        };
        ID3D11Device_CreateTexture2D(r_state->device, &desc, &initial, &texture);
    }
    free(data);

    // add to cache
    if (r_state->image_cache.count >= r_state->image_cache.capacity)
    {
        r_state->image_cache.capacity = r_state->image_cache.capacity ? r_state->image_cache.capacity * 2 : 16;
        r_state->image_cache.resources = realloc(r_state->image_cache.resources, r_state->image_cache.capacity * sizeof(ImageResource));
    }

    int n = r_state->image_cache.count;
    expect(n < 128);
    r_state->image_cache.resources[n] = (ImageResource)
    {
        .texture = (void*)texture,
        .width = width,
        .height = height
    };
    r_state->image_path_res_entries[n].path = path;
    r_state->image_path_res_entries[n].resource = &r_state->image_cache.resources[n];
    r_state->image_cache.count++;
    return r_state->image_path_res_entries[n].resource;
}

void r_draw_image(IWICImagingFactory* img_factory, RendererState* r_state, UI_Rect rect, const char* path)
{
    flush(r_state, NULL, NULL, NULL, NULL, NULL, 0);
    
    // load img resource
    ImageResource* img = 0;
    for (int i = 0; i < MAX_IMAGE_PATH_RES_ENTRIES; i++)
    {
        if (r_state->image_path_res_entries[i].path == path)
        {
            img = r_state->image_path_res_entries[i].resource;
        }
    }
    if (!img)
        img = r_load_image(img_factory, r_state, path);

    // calculate aspect ratio preserved dimensions
    float img_ratio = (float)img->width / img->height;
    float rect_ratio = (float)rect.w / rect.h;

    UI_Rect dst;
    if (img_ratio > rect_ratio)
    {
        dst.w = rect.w;
        dst.h = (int)(rect.w / img_ratio);
    }
    else
    {
        dst.h = rect.h;
        dst.w = (int)(rect.h * img_ratio);
    }

    // center in rect
    dst.x = rect.x + (rect.w - dst.w) / 2;
    dst.y = rect.y + (rect.h - dst.h) / 2;

    // set image texture index and create srview
    ID3D11Device_CreateShaderResourceView(r_state->device, (ID3D11Resource*)img->texture, NULL, &r_state->img_srview);
    push_rect(r_state, dst, ui_rect(0,0,1,1), ui_color(255,255,255,255), 1);
    flush(r_state, NULL, r_state->img_srview, NULL, NULL, NULL, 0);
    ID3D11ShaderResourceView_Release(r_state->img_srview);
}

static void r_load_image_gif_frame(IWICImagingFactory* img_factory, RendererState* r_state, int gif_idx, const char* path, int frame_idx)
{
    // create composed texture & rtview
    {
        D3D11_TEXTURE2D_DESC desc = {
                .Width     = r_state->gif_cache.gif_frame_cache[gif_idx].frame_max_width,
                .Height    = r_state->gif_cache.gif_frame_cache[gif_idx].frame_max_height,
                .MipLevels = 1,
                .ArraySize = 1,
                .Format    = DXGI_FORMAT_R8G8B8A8_UNORM,
                .SampleDesc.Count = 1,
                .Usage     = D3D11_USAGE_DEFAULT,
                .BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
        };
        ID3D11Device_CreateTexture2D(r_state->device, &desc, NULL, (ID3D11Texture2D**)&r_state->gif_cache.gif_frame_cache[gif_idx].frames[frame_idx].texture);
    }
    ID3D11RenderTargetView* composed_rtview;
    ID3D11Device_CreateRenderTargetView(r_state->device, (ID3D11Resource*)r_state->gif_cache.gif_frame_cache[gif_idx].frames[frame_idx].texture, NULL, &composed_rtview);

    // create temporary srview with frame data
    ID3D11Texture2D* frame_texture;
    {
        D3D11_TEXTURE2D_DESC desc = {
            .Width = r_state->gif_cache.gif_frame_cache[gif_idx].frames[frame_idx].width,
            .Height = r_state->gif_cache.gif_frame_cache[gif_idx].frames[frame_idx].height,
            .MipLevels = 1,
            .ArraySize = 1,
            .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
            .SampleDesc.Count = 1,
            .Usage = D3D11_USAGE_DEFAULT,
            .BindFlags = D3D11_BIND_SHADER_RESOURCE,
        };
        image_load_gif_frame(img_factory, path, frame_idx, &r_state->gif_cache.gif_frame_cache[gif_idx]);
        D3D11_SUBRESOURCE_DATA data = {
            .pSysMem = r_state->gif_cache.gif_frame_cache[gif_idx].frames[frame_idx].bitmap,
            .SysMemPitch = r_state->gif_cache.gif_frame_cache[gif_idx].frames[frame_idx].width * 4,
        };
        ID3D11Device_CreateTexture2D(r_state->device, &desc, &data, &frame_texture);
        free(r_state->gif_cache.gif_frame_cache[gif_idx].frames[frame_idx].bitmap);
    }
    ID3D11ShaderResourceView* frame_srview;
    ID3D11Device_CreateShaderResourceView(r_state->device, (ID3D11Resource*)frame_texture, NULL, &frame_srview);

    // create vbuffer, ibuffer and viewport to draw
    ID3D11Buffer* frame_vbuffer;
    {
        UI_Rect rect =
        {
            .x = r_state->gif_cache.gif_frame_cache[gif_idx].frames[frame_idx].x,
            .y = r_state->gif_cache.gif_frame_cache[gif_idx].frames[frame_idx].y,
            .w = r_state->gif_cache.gif_frame_cache[gif_idx].frames[frame_idx].width,
            .h = r_state->gif_cache.gif_frame_cache[gif_idx].frames[frame_idx].height
        };
        Vertex data[4] =
        {
            { {(float)rect.x,            (float)rect.y},            { 0, 0 }, { 255, 255, 255, 255 }, 1 },
            { {(float)(rect.x + rect.w), (float)rect.y},            { 1, 0 }, { 255, 255, 255, 255 }, 1 },
            { {(float)rect.x,            (float)(rect.y + rect.h)}, { 0, 1 }, { 255, 255, 255, 255 }, 1 },
            { {(float)(rect.x + rect.w), (float)(rect.y + rect.h)}, { 1, 1 }, { 255, 255, 255, 255 }, 1 }
        };
        D3D11_BUFFER_DESC desc =
        {
            .ByteWidth = sizeof(data),
            .Usage     = D3D11_USAGE_IMMUTABLE,
            .BindFlags = D3D11_BIND_VERTEX_BUFFER,
        };
        D3D11_SUBRESOURCE_DATA initial = { .pSysMem = data };
        ID3D11Device_CreateBuffer(r_state->device, &desc, &initial, &frame_vbuffer);
    }
    ID3D11Buffer* frame_ibuffer;
    {
        unsigned data[6] = { 0, 1, 2, 2, 1, 3 };
        D3D11_BUFFER_DESC desc = {
            .ByteWidth      = sizeof(data),
            .Usage          = D3D11_USAGE_DYNAMIC,
            .BindFlags      = D3D11_BIND_INDEX_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };
        D3D11_SUBRESOURCE_DATA initial = { .pSysMem = data };
        ID3D11Device_CreateBuffer(r_state->device, &desc, &initial, &frame_ibuffer);
    }
    UI_Rect viewport_rect = { 0, 0, r_state->gif_cache.gif_frame_cache[gif_idx].frame_max_width, r_state->gif_cache.gif_frame_cache[gif_idx].frame_max_height };

    // if not the first frame, compose previous texture and current frame; then draw
    if (frame_idx > 0)
    {
        ID3D11DeviceContext_CopyResource(r_state->context, (ID3D11Resource*)r_state->gif_cache.gif_frame_cache[gif_idx].frames[frame_idx].texture, (ID3D11Resource*)r_state->gif_cache.gif_frame_cache[gif_idx].frames[frame_idx - 1].texture);
    }
    flush(r_state, &viewport_rect, frame_srview, composed_rtview, frame_vbuffer, frame_ibuffer, 6);

    // composed texture has two characters: as render target (output) and as shader resource (input)
    // DirectX doesn't allow it to be both, so we need to release it (as a render target) before using it as a shader resource
    ID3D11RenderTargetView* null_rtview = NULL;
    ID3D11DeviceContext_OMSetRenderTargets(r_state->context, 1, &null_rtview, NULL);

    // clean
    ID3D11Texture2D_Release(frame_texture);
    ID3D11Buffer_Release(frame_vbuffer);
    ID3D11Buffer_Release(frame_ibuffer);
    ID3D11ShaderResourceView_Release(frame_srview);
    ID3D11RenderTargetView_Release(composed_rtview);
}

// NOTE: current the rendering of test.gif is incorrect.
//       (maybe is related to disposal method, see MSDN sample WicAnimatedGif.cpp)
void r_draw_image_gif(IWICImagingFactory* img_factory, RendererState* r_state, UI_Rect rect, const char* path, float delta_time_ms)
{
    flush(r_state, NULL, NULL, NULL, NULL, NULL, 0);

    // load gif frame cache
    int gif_idx = -1;
    for (int i = 0; i < MAX_IMAGE_PATH_RES_ENTRIES; i++)
    {
        if (r_state->gif_path_res_entries[i].path == path)
        {
            gif_idx = r_state->gif_path_res_entries[i].gif_idx;
        }
    }
    if (gif_idx == -1)
    {
        r_state->gif_path_res_entries[r_state->gif_cache.count].path = path;
        r_state->gif_path_res_entries[r_state->gif_cache.count].gif_idx = (
            gif_idx = r_state->gif_cache.count
        );
        r_state->gif_cache.count++;
    }

    // check & increase gif cache capacity
    if (r_state->gif_cache.count >= r_state->gif_cache.capacity)
    {
        r_state->gif_cache.capacity = r_state->gif_cache.capacity ? r_state->gif_cache.capacity * 2 : 16;
        r_state->gif_cache.gif_frame_cache = realloc(r_state->gif_cache.gif_frame_cache, r_state->gif_cache.capacity * sizeof(GIFFrameCache));
        memset(r_state->gif_cache.gif_frame_cache, 0, sizeof(r_state->gif_cache.gif_frame_cache[0]) * r_state->gif_cache.capacity);
    }

    // load (init) current gif metadata if not have loaded (inited) yet
    if (!r_state->gif_cache.gif_frame_cache[gif_idx].capacity)
    {
        image_gif_init(img_factory, path, &r_state->gif_cache.gif_frame_cache[gif_idx]);
    }

    // update frame loop current time & accumulative delays
    float delta_time = delta_time_ms * 1000;
    r_state->gif_cache.gif_frame_cache[gif_idx].loop_current_time += delta_time;
    int idx = get_current_frame_idx_based_accum_delays(&r_state->gif_cache.gif_frame_cache[gif_idx]);

    // check if current frame is last, if so reset idx and time to 0
    if (idx == r_state->gif_cache.gif_frame_cache[gif_idx].frame_count)
    {
        r_state->gif_cache.gif_frame_cache[gif_idx].loop_current_time = 0;
        idx = 0;
    }

    // only load frame if it's not already loaded
    if (!r_state->gif_cache.gif_frame_cache[gif_idx].frames[idx].texture)
    {
        r_load_image_gif_frame(img_factory, r_state, gif_idx, path, idx);
    }

    // calculate aspect ratio preserved dimensions
    UI_Rect dst;
    {
        float img_ratio = (float)r_state->gif_cache.gif_frame_cache[gif_idx].frame_max_width / r_state->gif_cache.gif_frame_cache[gif_idx].frame_max_height;
        float rect_ratio = (float)rect.w / rect.h;

        if (img_ratio > rect_ratio)
        {
            dst.w = rect.w;
            dst.h = (int)(rect.w / img_ratio);
        }
        else
        {
            dst.h = rect.h;
            dst.w = (int)(rect.h * img_ratio);
        }
        // center in rect
        dst.x = rect.x + (rect.w - dst.w) / 2;
        dst.y = rect.y + (rect.h - dst.h) / 2;
    }

    // draw
    ID3D11Device_CreateShaderResourceView(r_state->device, (ID3D11Resource*)r_state->gif_cache.gif_frame_cache[gif_idx].frames[idx].texture, NULL, &r_state->gif_srview);
    push_rect(r_state, dst, ui_rect(0,0,1,1), ui_color(255,255,255,255), 1);
    flush(r_state, NULL, r_state->gif_srview, NULL, NULL, NULL, 0);
    ID3D11ShaderResourceView_Release(r_state->gif_srview);
}

void r_present(RendererState* r_state)
{
    flush(r_state, NULL, NULL, NULL, NULL, NULL, 0);
    IDXGISwapChain1_Present(r_state->swapchain, 1, 0);
}

void r_clean(RendererState* r_state)
{

    for (int i = 0; i < r_state->image_cache.count; i++)
        if (r_state->image_cache.resources[i].texture)
            ID3D11Texture2D_Release((ID3D11Texture2D*)r_state->image_cache.resources[i].texture);
    for (int i = 0; i < r_state->gif_cache.count; i++)
        for (int j = 0; j < r_state->gif_cache.gif_frame_cache[i].count; j++)
            if (r_state->gif_cache.gif_frame_cache[i].frames[j].texture)
                ID3D11Texture2D_Release((ID3D11Texture2D*)r_state->gif_cache.gif_frame_cache[i].frames[j].texture);
    ID3D11RenderTargetView_Release(r_state->rtview);
    ID3D11RasterizerState_Release(r_state->raster_state);
    ID3D11SamplerState_Release(r_state->sampler_state);
    ID3D11ShaderResourceView_Release(r_state->srview);
    ID3D11Buffer_Release(r_state->vbuffer);
    ID3D11Buffer_Release(r_state->ibuffer);
    ID3D11Buffer_Release(r_state->cbuffer);
    ID3D11BlendState_Release(r_state->blend_state);
    ID3D11InputLayout_Release(r_state->layout);
    ID3D11VertexShader_Release(r_state->vshader);
    ID3D11PixelShader_Release(r_state->pshader);
    IDXGISwapChain1_Release(r_state->swapchain);
    ID3D11DeviceContext_Release(r_state->context);
    ID3D11Device_Release(r_state->device);
}
