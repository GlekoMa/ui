#pragma comment(lib, "user32")
#pragma comment(lib, "shell32")
#pragma comment(lib, "shlwapi")
#pragma comment(lib, "dwmapi")
#pragma comment(lib, "d3d11")
#pragma comment(lib, "dxguid")
#pragma comment(lib, "dxgi")
#pragma comment(lib, "d3dcompiler")
#pragma comment(lib, "ole32")
#pragma comment(lib, "windowscodecs")

#include <windows.h>
#include <dwmapi.h>
#include <stdio.h>
#include "ui.h"
#include "renderer.h"
#include "image.h"

#define CLIENT_WIDTH 400
#define CLIENT_HEIGHT 300

UI_Context* g_ctx;
RendererState r_state = { 0 };

static POINT s_drag_start_pos;

//
// hot reload helper
//

typedef void (*HotReloadedProcess)(IWICImagingFactory* img_factory, RendererState* r_state, UI_Context* ctx);

typedef struct {
    HINSTANCE dll;
    FILETIME last_write_time;
    HotReloadedProcess func;
} HotReloader;

static FILETIME get_dll_write_time(const char* path)
{
    FILETIME time = { 0 };
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (GetFileAttributesExA(path, GetFileExInfoStandard, &data))
    {
        time = data.ftLastWriteTime;
    }
    return time;
}

static bool check_and_reload(HotReloader* hr)
{
    FILETIME current_time = get_dll_write_time("process.dll");

    if (CompareFileTime(&current_time, &hr->last_write_time) == 0)
    {
        return false;
    }
    else
    {
        if (hr->dll) {
            FreeLibrary(hr->dll);
        }
        CopyFile("process.dll", "_process.dll", 0);

        // load new dll
        hr->dll = LoadLibrary("_process.dll");
        expect(hr->dll);
        hr->func = (HotReloadedProcess)GetProcAddress(hr->dll, "hot_reloaded_process");
        expect(hr->func);

        // update last write time
        hr->last_write_time = current_time;
        return true;
    }
}

//
// window proc
//

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
        case WM_NCCALCSIZE:
            // remove the standard window frame
            if (!wparam)
                return DefWindowProcW(window, message, wparam, lparam);
            return 0;
        case WM_CREATE:
            // force app to send a WM_NCCALCSIZE message
            SetWindowPos(window, NULL, 0, 0, 0, 0,
                         SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER);
            return 0;
        case WM_SETCURSOR:
            // show an arrow instead of the busy cursor
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            return 0;
        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE)
                DestroyWindow(window);
            return 0;
        case WM_MOUSEMOVE:
            g_ctx->mouse_pos.x = LOWORD(lparam);
            g_ctx->mouse_pos.y = HIWORD(lparam);

            // Handle window dragging
            if (g_ctx->mouse_held && !g_ctx->lclicked)
            {
                POINT cursor_pos;
                GetCursorPos(&cursor_pos);
                int dx = cursor_pos.x - s_drag_start_pos.x;
                int dy = cursor_pos.y - s_drag_start_pos.y;
                if (dx != 0 || dy != 0)
                {
                    RECT rect;
                    GetWindowRect(window, &rect);
                    SetWindowPos(window, NULL, rect.left + dx, rect.top + dy, 0, 0,
                                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                    // update s_drag_start_pos to current pos
                    s_drag_start_pos = cursor_pos;
                }
            }
            return 0;
        case WM_LBUTTONDOWN:
            g_ctx->mouse_held = true;
            g_ctx->mouse_lclick = true;
            GetCursorPos(&s_drag_start_pos);
            SetCapture(window);
            return 0;
        case WM_LBUTTONUP:
            g_ctx->mouse_held = false;
            return 0;
        case WM_RBUTTONDOWN:
            g_ctx->mouse_rclick = true;
            return 0;
        case WM_MOUSEWHEEL:
            short delta = GET_WHEEL_DELTA_WPARAM(wparam);
            g_ctx->scroll_delta.y += delta / -10;
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

//
// context relate functions
//

int text_width(void* renderer_data, const wchar_t* text, int len)
{
    RendererState* r_state = (RendererState*)renderer_data;
    if (len < 0)
    {
        len = (int)wcslen(text);
    }
    return r_get_text_width(r_state, text, len);
}

int text_height(void* renderer_data)
{
    RendererState* r_state = (RendererState*)renderer_data;
    return r_get_text_height(r_state);
}

//
// main
//

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nShowCmd)
{
    // Create window
    {
        // Set the client position to screen center
        int screen_width  = GetSystemMetrics(SM_CXSCREEN);
        int screen_height = GetSystemMetrics(SM_CYSCREEN);
        int x             = (screen_width - CLIENT_WIDTH) / 2;
        int y             = (screen_height - CLIENT_HEIGHT) / 2;

        // Give the client area rectangle, get back the entire window rectangle
        RECT rect         = { x, y, x + CLIENT_WIDTH, y + CLIENT_HEIGHT };
        long window_style = 0;
        AdjustWindowRectEx(&rect, 0, 0, 0);

        // Register window class then create window
        WNDCLASSW wc     = {};
        wc.lpfnWndProc   = window_proc;
        wc.hInstance     = GetModuleHandleW(NULL);
        wc.lpszClassName = L"ui";
        RegisterClassW(&wc);
        g_window = CreateWindowExW(0, wc.lpszClassName, L"ui", window_style,
                                 rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                                 NULL, NULL, wc.hInstance, NULL);
        // Disable window animation (e.g. pop up)
        BOOL attrib = TRUE;
        DwmSetWindowAttribute(g_window, DWMWA_TRANSITIONS_FORCEDISABLED, &attrib, sizeof(attrib));
    }

    // Init image factory & renderer & context
    IWICImagingFactory* img_factory = NULL;
    image_init(&img_factory);

    r_init(&r_state);
    r_state.client_width = CLIENT_WIDTH;
    r_state.client_height = CLIENT_HEIGHT;

    g_ctx = malloc(sizeof(UI_Context));
    g_ctx->renderer_data = &r_state;
    g_ctx->text_width = text_width;
    g_ctx->text_height = text_height;
    ui_init(g_ctx);

    // Show window
    ShowWindow(g_window, SW_SHOWDEFAULT);

    // Init hot reloader
    HotReloader hr = { 0 };
    check_and_reload(&hr);

    // Run message and render loop
    for (;;)
    {
        // Handle messages
        MSG msg;
        if (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                break;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            continue;
        }

        // Hot reload: process frame & render
        check_and_reload(&hr);
        expect(hr.func);
        hr.func(img_factory, &r_state, g_ctx);
    }

    // Clean
    FreeLibrary(hr.dll);
    r_clean(&r_state);
    int gif_idx = 0;
    image_gif_clean(&r_state.gif_cache.gif_frame_cache[gif_idx]);
    image_clean(img_factory);
    return 0;
}
