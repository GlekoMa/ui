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
#include "ui.h"
#include "renderer.h"
#include "image.h"

typedef int (*TextWidthFunc)(const wchar_t* text, int len);
typedef int (*TextHeightFunc)();

static void process_frame(UI_Context* ctx)
{
    ui_begin(ctx);
    {
        // window 1
        ui_begin_window(ctx, L"window title 1", ui_rect(100, 100, 350, 200));
        {
            ui_layout_row(ctx, 3, 24);
            {
                ui_label(ctx, L"Hello");
                ui_label(ctx, L"Bye");
                ui_label(ctx, L"空山不见人");
                ui_label(ctx, L"Do you know");
                static int check = 0;
                ui_checkbox(ctx, L"checkbox-dododo", &check);
            }
        }
        ui_end_window(ctx);
        // window 2
        ui_begin_window(ctx, L"window title 2", ui_rect(550, 150, 150, 200));
        {
            ui_layout_row(ctx, 2, 24);
            {
                ui_label(ctx, L"jackdoyouknow");
                ui_label(ctx, L"jackdoyouknow");
                ui_label(ctx, L"jackdoyouknow");
                ui_label(ctx, L"jackdoyouknow");
                ui_label(ctx, L"jackdoyouknow");
                ui_label(ctx, L"jackdoyouknow");
                ui_label(ctx, L"jackdoyouknow");
                ui_label(ctx, L"jackdoyouknow");
                ui_label(ctx, L"jackdoyouknow");
                ui_label(ctx, L"jackdoyouknow");
                ui_label(ctx, L"jackdoyouknow");
                ui_label(ctx, L"jackdoyouknow");
                ui_label(ctx, L"jackdoyouknow");
                ui_label(ctx, L"jackdoyouknow");
                ui_label(ctx, L"jackdoyouknow");
                ui_label(ctx, L"jackdoyouknow");
                ui_label(ctx, L"jackdoyouknow");
                ui_label(ctx, L"jackdoyouknow");
                ui_label(ctx, L"jackdoyouknow");
                ui_label(ctx, L"jackdoyouknow");
                ui_label(ctx, L"jackdoyouknow");
                ui_label(ctx, L"jackdoyouknow");
                ui_label(ctx, L"jackdoyouknow");
                ui_label(ctx, L"jackdoyouknow");
                ui_label(ctx, L"jackdoyouknow");
                ui_label(ctx, L"jackdoyouknow");
                ui_label(ctx, L"jackdoyouknow");
                ui_label(ctx, L"jackdoyouknow");
                ui_image(ctx, "C:/Users/niko1/repos/ui/assets/test.png");
                ui_image(ctx, "C:/Users/niko1/repos/ui/assets/test2.png");
            }
        }
        ui_end_window(ctx);
    }
    ui_end(ctx);
}

__declspec(dllexport) void hot_reloaded_process(IWICImagingFactory* img_factory, RendererState* r_state, UI_Context* ctx,
        TextWidthFunc width_func, TextHeightFunc height_func)
{
    ctx->text_width = width_func;
    ctx->text_height = height_func;

    // Process frame
    process_frame(ctx);

    // Render
    r_clear(r_state, ui_color(255, 255, 255, 255));
    UI_Command* cmd = NULL;
    while (ui_next_command(ctx, &cmd))
    {
        switch(cmd->type)
        {
            case UI_COMMAND_RECT: r_draw_rect(r_state, cmd->rect.rect, cmd->rect.color); break;
            case UI_COMMAND_TEXT: r_draw_text(r_state, cmd->text.str, cmd->text.pos, cmd->text.color); break;
            case UI_COMMAND_CLIP: r_set_clip_rect(r_state, cmd->clip.rect); break;
            case UI_COMMAND_IMAGE: r_draw_image(img_factory, r_state, cmd->image.rect, cmd->image.path); break;
        }
    }
    r_present(r_state);
}

BOOL WINAPI DllMain(HMODULE dll, DWORD reason, LPVOID reserved)
{
    (void)dll;
    (void)reason;
    (void)reserved;
    return 1;
}
