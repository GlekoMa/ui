#include "ui.h"
#include "renderer.h"

UI_Rect ui_rect(int x, int y, int w, int h)
{
    return (UI_Rect){ x, y, w, h };
}

UI_Color ui_color(int r, int g, int b, int a)
{
    return (UI_Color){ r, g, b, a };
}

//
// commandlist
// 

static UI_Command* ui_push_command(UI_Context* ctx, int type, int size)
{
    UI_Command* cmd = (UI_Command*)(ctx->command_list.items + ctx->command_list.idx);
    expect(ctx->command_list.idx + size < UI_COMMANDLIST_SIZE);
    cmd->base.type = type;
    cmd->base.size = size;
    ctx->command_list.idx += size;
    return cmd;
}

int ui_next_command(UI_Context* ctx, UI_Command** cmd)
{
    if (*cmd)
    {
        *cmd = (UI_Command*)(((char*)*cmd) + (*cmd)->base.size);
    }
    else
    {
        *cmd = (UI_Command*)ctx->command_list.items;
    }

    if ((char*)*cmd != ctx->command_list.items + ctx->command_list.idx)
    {
	    return 1;
    }
    else
    {
        return 0;
    }
}

//
// UI functions
//

static void ui_draw_rect(UI_Context* ctx, UI_Rect rect, UI_Color color)
{
    UI_Command* cmd;
    if (rect.w > 0 && rect.h > 0)
    {
        cmd = ui_push_command(ctx, UI_COMMAND_RECT, sizeof(UI_RectCommand));
        cmd->rect.rect = rect;
        cmd->rect.color = color;
    }
}

void ui_draw_box(UI_Context* ctx, UI_Rect rect, UI_Color color, unsigned border_width)
{
    unsigned bw = border_width;
    ui_draw_rect(ctx, ui_rect(rect.x + bw, rect.y, rect.w - bw * 2, bw), color);
    ui_draw_rect(ctx, ui_rect(rect.x + bw, rect.y + rect.h - bw, rect.w - bw * 2, bw), color);
    ui_draw_rect(ctx, ui_rect(rect.x, rect.y, bw, rect.h), color);
    ui_draw_rect(ctx, ui_rect(rect.x + rect.w - bw, rect.y, bw, rect.h), color);
}

void ui_begin(UI_Context *ctx)
{
  ctx->command_list.idx = 0;
}
