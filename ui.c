#define _AMD64_
#include <debugapi.h>
#include <stdio.h>
#include "ui.h"

UI_Vec2 ui_vec2(int x, int y)
{
    return (UI_Vec2){ x, y };
}

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

///

static bool rect_overlaps_vec2(UI_Rect r, UI_Vec2 p)
{
    return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
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

void ui_square(UI_Context* ctx, UI_Vec2 pos, UI_Color color, unsigned wh)
{
    UI_Container* cnt = &ctx->containers[ctx->container_idx];
    {
        cnt->rect.x = pos.x;
        cnt->rect.y = pos.y;
        cnt->rect.w = wh;
        cnt->rect.h = wh;
        // We have 3 square (zindex is 2, 1, 0) in this demo. And the drawing order is 2, 1, 0. We will sort
        // the drawing order later to achieve a descending order based on the z-index.
        cnt->zindex = (2 - ctx->container_idx);
    }
    ctx->container_idx++;

    if (rect_overlaps_vec2(cnt->rect, ctx->mouse_pos) && ctx->mouse_pressed)
    {
        char dbg_str[128];
        sprintf(dbg_str, "mouse pressed and overlaps the rect whose zindex is %d\n", cnt->zindex);
        OutputDebugStringA(dbg_str);
    }
    ui_draw_rect(ctx, cnt->rect, color);
}

void ui_begin(UI_Context* ctx)
{
  ctx->command_list.idx = 0;
  ctx->container_idx = 0;
}
