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

int g_client_width = 800;
int g_client_height = 600;
UI_Context* g_ctx;

static POINT s_drag_start_pos;

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
            if (g_ctx->mouse_held && !g_ctx->focus) 
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
            g_ctx->mouse_click = true;
            GetCursorPos(&s_drag_start_pos);
            SetCapture(window);
            return 0;
        case WM_LBUTTONUP:
            g_ctx->mouse_held = false;
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


static void process_frame(UI_Context* ctx)
{
    ui_begin(ctx);
    {
        // window 1
        ui_begin_window(g_ctx, L"window title 1", ui_rect(100, 100, 150, 200));
        {
            ui_layout_row(ctx, 3, 24);
            {
                ui_label(g_ctx, L"Hello");
                ui_label(g_ctx, L"Bye");
                ui_label(g_ctx, L"不害臊的姑娘");
                ui_label(g_ctx, L"Do you know");
            }
        }
        ui_end_window(g_ctx);
        // window 2
        ui_begin_window(g_ctx, L"window title 2", ui_rect(150, 150, 150, 200));
        {
            ui_layout_row(ctx, 2, 24);
            {
                ui_label(g_ctx, L"jackdoyouknow");
                ui_label(g_ctx, L"jackdoyouknow");
                ui_label(g_ctx, L"jackdoyouknow");
                ui_label(g_ctx, L"jackdoyouknow");
                ui_label(g_ctx, L"jackdoyouknow");
                ui_label(g_ctx, L"jackdoyouknow");
                ui_label(g_ctx, L"jackdoyouknow");
                ui_label(g_ctx, L"jackdoyouknow");
                ui_label(g_ctx, L"jackdoyouknow");
                ui_label(g_ctx, L"jackdoyouknow");
                ui_label(g_ctx, L"jackdoyouknow");
                ui_label(g_ctx, L"jackdoyouknow");
                ui_label(g_ctx, L"jackdoyouknow");
                ui_label(g_ctx, L"jackdoyouknow");
                ui_label(g_ctx, L"jackdoyouknow");
                ui_label(g_ctx, L"jackdoyouknow");
                ui_label(g_ctx, L"jackdoyouknow");
                ui_label(g_ctx, L"jackdoyouknow");
                ui_label(g_ctx, L"jackdoyouknow");
                // ui_image(ctx, r_load_image("C:/Users/niko1/repos/ui/assets/test.png"));
                // ui_image(ctx, r_load_image("C:/Users/niko1/repos/ui/assets/test2.png"));
            }
        }
        ui_end_window(g_ctx);
    }
    ui_end(ctx);
}

static int text_width(const wchar_t* text, int len)
{
    if (len < 0)
    {
        len = (int)wcslen(text);
    }
    return r_get_text_width(text, len);
}

static int text_height()
{
    return r_get_text_height();
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nShowCmd)
{
    // Create window
    {
        // Set the client position to screen center
        int screen_width  = GetSystemMetrics(SM_CXSCREEN);
        int screen_height = GetSystemMetrics(SM_CYSCREEN);
        int x             = (screen_width - g_client_width) / 2;
        int y             = (screen_height - g_client_height) / 2;

        // Give the client area rectangle, get back the entire window rectangle
        RECT rect         = { x, y, x + g_client_width, y + g_client_height };
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
    image_init(); 
    r_init();
    g_ctx = malloc(sizeof(UI_Context));
    ui_init(g_ctx);

    g_ctx->text_width = text_width;
    g_ctx->text_height = text_height;

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
        r_clear(ui_color(255, 255, 255, 255));
        UI_Command* cmd = NULL;
        while (ui_next_command(g_ctx, &cmd)) 
        {
            switch(cmd->type) 
            {
                case UI_COMMAND_RECT: r_draw_rect(cmd->rect.rect, cmd->rect.color); break;
                case UI_COMMAND_TEXT: r_draw_text(cmd->text.str, cmd->text.pos, cmd->text.color); break;
                case UI_COMMAND_CLIP: r_set_clip_rect(cmd->clip.rect); break;
                case UI_COMMAND_IMAGE: r_draw_image(cmd->image.rect, cmd->image.image_id); break;
            }
        }
        r_present();
    }

    // Clean
    r_clean();
    image_clean();
    return 0;
}
