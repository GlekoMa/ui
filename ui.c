#define _AMD64_
#include <debugapi.h>
#include <stdio.h>
#include <stdlib.h>
#include "ui.h"

#define push(stk, val)                                                                                       \
    do                                                                                                       \
    {                                                                                                        \
        expect((stk).idx < (int)(sizeof((stk).items) / sizeof(*(stk).items)));                               \
        (stk).items[(stk).idx] = (val);                                                                      \
        (stk).idx++;                                                                                         \
    } while (0)

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

    while ((char*) *cmd != ctx->command_list.items + ctx->command_list.idx) 
    {
        if ((*cmd)->type == UI_COMMAND_JUMP) 
        { 
            *cmd = (*cmd)->jump.dst;
        } 
        else 
        {
            return 1; 
        }
    }
    return 0;
}

static UI_Command* push_jump(UI_Context *ctx, UI_Command *dst) 
{
    UI_Command *cmd;
    cmd = ui_push_command(ctx, UI_COMMAND_JUMP, sizeof(UI_JumpCommand));
    cmd->jump.dst = dst;
    return cmd;
}

/// container

static bool rect_overlaps_vec2(UI_Rect r, UI_Vec2 p)
{
    return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
}

static void begin_root_container(UI_Context* ctx, UI_Container* cnt)
{
    cnt->head = push_jump(ctx, NULL);
    push(ctx->root_list, cnt);

    // set as hover root if the mouse is overlapping this container and it has a
    // higher zindex than the current hover root
    if (rect_overlaps_vec2(cnt->rect, ctx->mouse_pos) &&
        (!ctx->next_hover_root || cnt->zindex > ctx->next_hover_root->zindex))
    {
        ctx->next_hover_root = cnt;
    }
}

static void end_root_container(UI_Context* ctx, UI_Container* cnt)
{
    cnt->tail = push_jump(ctx, NULL);
}

static void ui_bring_to_front(UI_Context* ctx, UI_Container* cnt)
{
    cnt->zindex = ++ctx->last_zindex;
}

//
// normal
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
    UI_Container* cnt = &ctx->containers[ctx->root_list.idx];

    // only init once, thus user could change zindex eternally by mouse pressing
    if (!cnt->zindex)
    {
        ui_bring_to_front(ctx, cnt);
        cnt->rect.x = pos.x;
        cnt->rect.y = pos.y;
        cnt->rect.w = wh;
        cnt->rect.h = wh;
    }

    begin_root_container(ctx, cnt);
    ui_draw_rect(ctx, cnt->rect, color);
    end_root_container(ctx, cnt);
}

void ui_begin(UI_Context* ctx)
{
    ctx->command_list.idx = 0;
    ctx->root_list.idx = 0;
    ctx->next_hover_root  = NULL;
}

static int compare_zindex(const void* a, const void* b)
{
    return (*(UI_Container**)a)->zindex - (*(UI_Container**)b)->zindex;
}

void ui_end(UI_Context* ctx)
{
    if (ctx->mouse_pressed && 
        ctx->next_hover_root &&
        ctx->next_hover_root->zindex < ctx->last_zindex)
    {
        ui_bring_to_front(ctx, ctx->next_hover_root);
    }

    // sort root containers by zindex
    int n = ctx->root_list.idx;
    qsort(ctx->root_list.items, n, sizeof(UI_Container*), compare_zindex);

    // set root container jump commands
    for (int i = 0; i < n; i++)
    {
        UI_Container* cnt = ctx->root_list.items[i];
        // if this is the first container, then make the first command jump to it.
        // otherwise set the previous container's tail to jump to this one.
        if (i == 0)
        {
            UI_Command* cmd = (UI_Command*)ctx->command_list.items;
            cmd->jump.dst   = (char*)cnt->head + sizeof(UI_JumpCommand);
        }
        else
        {
            UI_Container* prev = ctx->root_list.items[i - 1];
            prev->tail->jump.dst = (char*)cnt->head + sizeof(UI_JumpCommand);
        }
        // make the last container's tail jump to the end of command list
        if (i == n - 1)
        {
            cnt->tail->jump.dst = ctx->command_list.items + ctx->command_list.idx;
        }
    }
}
