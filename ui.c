#include "renderer.h"
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

#define pop(stk)                                                                                             \
    do                                                                                                       \
    {                                                                                                        \
        expect((stk).idx > 0);                                                                               \
        (stk).idx--;                                                                                         \
    } while (0)

static UI_Style default_style = {
    // size | padding | spacing | title_height
    { 68, 10 }, 5, 4, 26,
    {
        { 56,  58,  66,  255 }, // UI_COLOR_TEXT
        { 18,  18,  18,  255 }, // UI_COLOR_TITLETEXT
        { 238, 238, 238, 255 }, // UI_COLOR_TITLEBG
        { 206, 206, 206, 255 }, // UI_COLOR_BORDER
        { 250, 250, 250, 255 }, // UI_COLOR_WINDOWBG
    }
};

static UI_Rect unclipped_rect = { 0, 0, 0x1000000, 0x1000000 };

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

static UI_Rect expand_rect(UI_Rect rect, int n)
{
    return ui_rect(rect.x - n, rect.y - n, rect.w + n * 2, rect.h + n * 2);
}

//
// function declare
//

static void ui_draw_control_text(UI_Context* ctx, const wchar_t* str, UI_Rect rect, int colorid);

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

static void ui_set_clip(UI_Context* ctx, UI_Rect rect) {
  UI_Command* cmd;
  cmd = ui_push_command(ctx, UI_COMMAND_CLIP, sizeof(UI_ClipCommand));
  cmd->clip.rect = rect;
}

//
// id stack
//

// 32bit fnv-1a hash
#define HASH_INITIAL 2166136261

static void hash(UI_Id* hash, const void* data, int size)
{
    const unsigned char* p = data;
    while (size--)
    {
        *hash = (*hash ^ *p++) * 16777619;
    }
}

static UI_Id ui_get_id(UI_Context* ctx, const void* data, int size)
{
    int idx = ctx->id_stack.idx;
    UI_Id res = (idx > 0) ? ctx->id_stack.items[idx - 1] : HASH_INITIAL;
    hash(&res, data, size);
    ctx->last_id = res;
    return res;
}

static void ui_pop_id(UI_Context* ctx)
{
    pop(ctx->id_stack);
}

//
// clip stack
//

static UI_Rect intersect_rects(UI_Rect r1, UI_Rect r2) 
{
    int x1 = ui_max(r1.x, r2.x);
    int y1 = ui_max(r1.y, r2.y);
    int x2 = ui_min(r1.x + r1.w, r2.x + r2.w);
    int y2 = ui_min(r1.y + r1.h, r2.y + r2.h);

    // if not intersect, return rect whose width/height is 0.
    if (x2 < x1) { x2 = x1; }
    if (y2 < y1) { y2 = y1; }

    return ui_rect(x1, y1, x2 - x1, y2 - y1);
}

static UI_Rect ui_get_clip_rect(UI_Context* ctx)
{
    expect(ctx->clip_stack.idx > 0);
    return ctx->clip_stack.items[ctx->clip_stack.idx - 1];
}

static void ui_pop_clip_rect(UI_Context *ctx) 
{
    pop(ctx->clip_stack);
}

static void ui_push_clip_rect(UI_Context* ctx, UI_Rect rect)
{
    UI_Rect last = ui_get_clip_rect(ctx);
    UI_Rect inter = intersect_rects(rect, last);
    push(ctx->clip_stack, inter);
}

static int ui_check_clip(UI_Context* ctx, UI_Rect r) 
{
    UI_Rect cr = ui_get_clip_rect(ctx);
    if (r.x > cr.x + cr.w || r.x + r.w < cr.x ||
        r.y > cr.y + cr.h || r.y + r.h < cr.y   ) { return UI_CLIP_ALL; }
    if (r.x >= cr.x && r.x + r.w <= cr.x + cr.w &&
        r.y >= cr.y && r.y + r.h <= cr.y + cr.h ) { return 0; }
    return UI_CLIP_PART;
}

//
// pool
//

static void ui_pool_update(UI_Context* ctx, UI_PoolItem* items, int idx)
{
    items[idx].last_update = ctx->frame;
}

static int ui_pool_init(UI_Context* ctx, UI_PoolItem* items, int len, UI_Id id)
{
    // finds the oldest item in the pool (the one with the lowest `last_update` value)
    int n = -1, f = ctx->frame;
    for (int i = 0; i < len; i++)
    {
        if (items[i].last_update < f)
        {
            f = items[i].last_update;
            n = i;
        }
    }

    expect(n > -1);
    items[n].id = id;
    ui_pool_update(ctx, items, n);
    return n;
}

static int ui_pool_get(UI_PoolItem* items, int len, UI_Id id)
{
    int i;
    for (i = 0; i < len; i++)
    {
        if (items[i].id == id)
        {
            return i;
        }
    }
    return -1;
}

// 
// layout
//

void ui_layout_row(UI_Context* ctx, int items, int height)
{
    UI_Layout* layout = &ctx->layout;
    expect(items <= UI_MAX_WIDTHS);

    for (int i = 0; i < items; i++)
    {
        int cnt_width = ctx->container_stack.items[ctx->container_stack.idx - 1]->rect.w;
        int p = ctx->style->padding;
        int s = ctx->style->spacing;
        layout->widths[i] = (int)((cnt_width - p * 2 - s * (items - 1)) / items);
    }
    layout->items      = items;
    layout->position   = ui_vec2(0, layout->next_row);
    layout->size.y     = height;
    layout->item_index = 0;
}

static UI_Rect ui_layout_next(UI_Context* ctx)
{
    UI_Layout* layout = &ctx->layout;
    UI_Rect    res;

    // handle next row
    if (layout->item_index == layout->items) {
        ui_layout_row(ctx, layout->items, layout->size.y);
    }

    // get res
    res.x = layout->position.x;
    res.y = layout->position.y;
    res.w = layout->widths[layout->item_index];
    res.h = layout->size.y;

    // update layout
    layout->position.x += res.w + ctx->style->spacing;
    layout->next_row = res.y + res.h + ctx->style->spacing;
    layout->item_index++;

    // apply body offset
    res.x += layout->body.x;
    res.y += layout->body.y;

    return res;
}

//
// container
//

static UI_Container* ui_get_current_container(UI_Context* ctx)
{
    expect(ctx->container_stack.idx > 0);
    return ctx->container_stack.items[ctx->container_stack.idx - 1];
}

static void pop_container(UI_Context* ctx)
{
    pop(ctx->container_stack);
    ui_pop_id(ctx);
}

static void ui_bring_to_front(UI_Context* ctx, UI_Container* cnt)
{
    cnt->zindex = ++ctx->last_zindex;
}

static UI_Container* get_container(UI_Context* ctx, UI_Id id)
{
    UI_Container* cnt;
    // try to get existing container from pool
    int idx = ui_pool_get(ctx->container_pool, UI_CONTAINERPOOL_SIZE, id);
    if (idx >= 0)
    {
        ui_pool_update(ctx, ctx->container_pool, idx);
        return &ctx->containers[idx];
    }
    else
    {
        // container not found in pool: init new container
        idx = ui_pool_init(ctx, ctx->container_pool, UI_CONTAINERPOOL_SIZE, id);
        cnt = &ctx->containers[idx];
        memset(cnt, 0, sizeof(*cnt));
        ui_bring_to_front(ctx, cnt);
        return cnt;
    }
}

static bool rect_overlaps_vec2(UI_Rect r, UI_Vec2 p)
{
    return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
}

static void begin_root_container(UI_Context* ctx, UI_Container* cnt)
{
    cnt->head = push_jump(ctx, NULL);
    push(ctx->root_list, cnt);
    push(ctx->container_stack, cnt);

    // set as hover root if the mouse is overlapping this container and it has a
    // higher zindex than the current hover root
    if (rect_overlaps_vec2(cnt->rect, ctx->mouse_pos) &&
        (!ctx->next_hover_root || cnt->zindex > ctx->next_hover_root->zindex))
    {
        ctx->next_hover_root = cnt;
    }

    // clipping is reset here in case a root-container is made within another
    // root-containers's begin/end block; this prevents the inner root-container
    // being clipped to the outer
    push(ctx->clip_stack, unclipped_rect);
}

static void end_root_container(UI_Context* ctx)
{
    UI_Container* cnt = ui_get_current_container(ctx);
    cnt->tail = push_jump(ctx, NULL);
    // pop base clip rect and container
    ui_pop_clip_rect(ctx);
    pop_container(ctx);
}

//
// window
//

void ui_begin_window(UI_Context* ctx, const wchar_t* title, UI_Rect rect)
{
    UI_Id id  = ui_get_id(ctx, title, (int)(sizeof(wchar_t) * wcslen(title)));
    UI_Container* cnt = get_container(ctx, id);
    push(ctx->id_stack, id);

    if (cnt->rect.w == 0) { cnt->rect = rect; }
    begin_root_container(ctx, cnt);
    UI_Rect body = cnt->rect;

    // draw frame
    ctx->draw_frame(ctx, cnt->rect, UI_COLOR_WINDOWBG);

    // draw title bar
    UI_Rect tr = rect;
    tr.h = ctx->style->title_height;
    ctx->draw_frame(ctx, tr, UI_COLOR_TITLEBG);
    ui_draw_control_text(ctx, title, tr, UI_COLOR_TITLETEXT);
    body.y += tr.h;
    body.h -= tr.h;

    // set layout
    memset(&ctx->layout, 0, sizeof(ctx->layout));
    ctx->layout.body = expand_rect(body, -ctx->style->padding);
}

void ui_end_window(UI_Context* ctx)
{
    end_root_container(ctx);
}

//
// draw
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

static void ui_draw_text(UI_Context* ctx, const wchar_t* str, int len, UI_Vec2 pos, UI_Color color)
{
    UI_Command* cmd;
    UI_Rect rect = ui_rect(pos.x, pos.y, ctx->text_width(str, len), ctx->text_height());
    int clipped = ui_check_clip(ctx, rect);
    if (clipped == UI_CLIP_ALL) { return; }
    if (clipped == UI_CLIP_PART) { ui_set_clip(ctx, ui_get_clip_rect(ctx)); }
    // add command
    if (len < 0) { len = (int)wcslen(str); }
    cmd = ui_push_command(ctx, UI_COMMAND_TEXT, sizeof(UI_TextCommand) + sizeof(wchar_t) * len);
    memcpy(cmd->text.str, str, sizeof(wchar_t) * len);
    cmd->text.str[len] = L'\0';
    cmd->text.pos      = pos;
    cmd->text.color    = color;
    // reset clipping if it was set
    if (clipped) { ui_set_clip(ctx, unclipped_rect); }
}

static void ui_draw_box(UI_Context* ctx, UI_Rect rect, UI_Color color)
{
    ui_draw_rect(ctx, ui_rect(rect.x + 1, rect.y, rect.w - 2, 1), color);
    ui_draw_rect(ctx, ui_rect(rect.x + 1, rect.y + rect.h - 1, rect.w - 2, 1), color);
    ui_draw_rect(ctx, ui_rect(rect.x, rect.y, 1, rect.h), color);
    ui_draw_rect(ctx, ui_rect(rect.x + rect.w - 1, rect.y, 1, rect.h), color);
}

static void draw_frame(UI_Context* ctx, UI_Rect rect, int colorid)
{
    ui_draw_rect(ctx, rect, ctx->style->colors[colorid]);
    ui_draw_box(ctx, expand_rect(rect, 1), ctx->style->colors[UI_COLOR_BORDER]);
}

//
// control
//

static void ui_draw_control_text(UI_Context* ctx, const wchar_t* str, UI_Rect rect, int colorid)
{
    ui_push_clip_rect(ctx, rect);
    {
        UI_Vec2 pos = {
            .x = rect.x + ctx->style->padding,
            .y = rect.y + (rect.h - ctx->text_height()) / 2,
        };
        ui_draw_text(ctx, str, -1, pos, ctx->style->colors[colorid]);
    }
    ui_pop_clip_rect(ctx);
}

void ui_label(UI_Context* ctx, const wchar_t* text)
{
    UI_Rect rect = ui_layout_next(ctx);
    ui_draw_control_text(ctx, text, rect, UI_COLOR_TEXT);
}

//
// init
//

void ui_init(UI_Context* ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->draw_frame = draw_frame;
    ctx->style = &default_style;
}

void ui_begin(UI_Context* ctx)
{
    ctx->command_list.idx = 0;
    ctx->root_list.idx = 0;
    ctx->next_hover_root  = NULL;
    ctx->frame++;
}

static int compare_zindex(const void* a, const void* b)
{
    return (*(UI_Container**)a)->zindex - (*(UI_Container**)b)->zindex;
}

void ui_end(UI_Context* ctx)
{
    // check stacks
    expect(ctx->container_stack.idx == 0);
    expect(ctx->clip_stack.idx      == 0);
    expect(ctx->id_stack.idx        == 0);
    
    // bring hover root to front if mouse was pressed
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
