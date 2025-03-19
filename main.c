#pragma comment(lib, "user32")
#pragma comment(lib, "d3d11")
#pragma comment(lib, "dxguid")
#pragma comment(lib, "dxgi")
#pragma comment(lib, "d3dcompiler")

#include <windows.h>
#include "ui.h"
#include "renderer.h"

int g_client_width = 800;
int g_client_height = 600;

UI_Context* g_ctx;

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE)
                DestroyWindow(window);
            return 0;
        case WM_MOUSEMOVE:
            g_ctx->mouse_pos.x = LOWORD(lparam);
            g_ctx->mouse_pos.y = HIWORD(lparam);
            return 0;
        case WM_LBUTTONDOWN:
            g_ctx->mouse_pressed = true;
            return 0;
        case WM_LBUTTONUP:
            g_ctx->mouse_pressed = false;
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}


static void process_frame(UI_Context* ctx)
{
    ui_begin(ctx);
    ui_square(ctx, ui_vec2(100, 100), 100, ui_color(255, 0, 0, 255));
    ui_square(ctx, ui_vec2(150, 100), 100, ui_color(0, 255, 0, 255));
    ui_square(ctx, ui_vec2(125, 150), 100, ui_color(0, 0, 255, 255));
    ui_end(ctx);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nShowCmd)
{
    // Set DPI awareness for better scaling on high DPI displays (Windows 10, v1607)
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Create window
    {
        // Set the client position to screen center
        int screen_width  = GetSystemMetrics(SM_CXSCREEN);
        int screen_height = GetSystemMetrics(SM_CYSCREEN);
        int x             = (screen_width - g_client_width) / 2;
        int y             = (screen_height - g_client_height) / 2;

        // Give the client area rectangle, get back the entire window rectangle
        RECT rect         = { x, y, x + g_client_width, y + g_client_height };
        long window_style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, 0, 0);

        // Register window class then create window
        WNDCLASSW wc     = {};
        wc.lpfnWndProc   = window_proc;
        wc.hInstance     = GetModuleHandleW(NULL);
        wc.lpszClassName = L"3-z-order_class";
        RegisterClassW(&wc);
        g_window = CreateWindowExW(0, wc.lpszClassName, L"3-z-order", window_style,
                                 rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                                 NULL, NULL, wc.hInstance, NULL);
    }

    // Init renderer & context
    r_init();
    g_ctx = malloc(sizeof(UI_Context));
    memset(g_ctx, 0, sizeof(*g_ctx));

    // Show window
    ShowWindow(g_window, SW_SHOWDEFAULT);

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

        // Process frame
        process_frame(g_ctx);

        // Render
        r_clear(ui_color(24, 24, 24, 255));
        UI_Command* cmd = NULL;
        while (ui_next_command(g_ctx, &cmd)) {
            switch(cmd->type) {
                case UI_COMMAND_RECT: r_draw_rect(cmd->rect.rect, cmd->rect.color); break;
            }
        }
        r_present();
    }

    // Clean
    r_clean();
    return 0;
}
