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

typedef int (*TextWidthFunc)(const wchar_t* text, int len);
typedef int (*TextHeightFunc)();

static void process_frame(UI_Context* ctx)
{
    ui_begin(ctx);
    {
        // window 1
        ui_begin_window(ctx, L"window title 1", ui_rect(10, 10, 200, 150));
        {
            ui_layout_row(ctx, 3, 24);
            {
                ui_label(ctx, L"Hello");
                ui_label(ctx, L"Bye");
                ui_label(ctx, L"空山不见人");
            }
            ui_layout_row(ctx, 2, 24);
            {
                ui_image(ctx, "C:/Users/niko1/repos/ui/assets/test.png");
                ui_image(ctx, "C:/Users/niko1/repos/ui/assets/test2.png");
            }
            ui_layout_row(ctx, 1, 24);
            {
                for (int i = 0; i < 10; i++)
                    ui_label(ctx, L"test");
            }
        }
        ui_end_window(ctx);
        // window 2
        ui_begin_window(ctx, L"window title 2", ui_rect(180, 80, 200, 200));
        {
            ui_layout_row(ctx, 1, 24);
            {
                static int check[] = { 0, 1, 0, 1 };
                ui_checkbox(ctx, L"check 1", &check[0]);
                ui_checkbox(ctx, L"check 2", &check[1]);
                ui_checkbox(ctx, L"check 3", &check[2]);
                ui_checkbox(ctx, L"check 4", &check[3]);
            }
        }
        ui_end_window(ctx);
    }
    ui_end(ctx);
}

float calculate_delta_time()
{
    static LARGE_INTEGER freq;
    static LARGE_INTEGER last_time;
    static BOOL first_frame = TRUE;

    if (first_frame) {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&last_time);
        first_frame = FALSE;
        return 0.f;
    }

    LARGE_INTEGER current_time;
    QueryPerformanceCounter(&current_time);

    float delta_time = (float)(current_time.QuadPart - last_time.QuadPart) / (float)freq.QuadPart;
    last_time = current_time;
    return delta_time;
}

__declspec(dllexport) void hot_reloaded_process(IWICImagingFactory* img_factory, RendererState* r_state, UI_Context* ctx)
{
    r_clear(r_state, ui_color(255, 255, 255, 255));

    // Process frame
    process_frame(ctx);

    // Calculate animation delta time
    ctx->animation_dt = calculate_delta_time();

    // Calculate FPS average and draw it at right top corner
    static float fps_average = 0.0f;
    if (ctx->animation_dt > 0.0f) // first frame, animation delta time is zero
    {
        fps_average = fps_average * 0.95f + (1.0f / ctx->animation_dt) * 0.05f;
    }

    if (fps_average)
    {
        wchar_t fps_text[16];
        swprintf(fps_text, 16, L"FPS: %d", (int)(fps_average + 0.5f));
        int fps_text_width = ctx->text_width(ctx->renderer_data, fps_text, 16 * sizeof(wchar_t));
        r_draw_text(r_state, fps_text,
                   ui_vec2(r_state->client_width - fps_text_width - 10, 10),
                   ui_color(0, 0, 0, 255));
    }

    // Render
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
    r_draw_image_gif(img_factory, r_state, ui_rect(50, 100, 50, 50), "C:\\Users\\niko1\\repos\\ui\\assets\\test.gif", ctx->animation_dt);
    r_draw_image_gif(img_factory, r_state, ui_rect(50, 50, 50, 50), "C:\\Users\\niko1\\repos\\ui\\assets\\test2.gif", ctx->animation_dt);
    r_present(r_state);
}

BOOL WINAPI DllMain(HMODULE dll, DWORD reason, LPVOID reserved)
{
    (void)dll;
    (void)reason;
    (void)reserved;
    return 1;
}
