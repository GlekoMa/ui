#pragma once

#include <wchar.h>
#include <stdbool.h>

#define UI_COMMANDLIST_SIZE (256 * 1024)
#define UI_ROOTLIST_SIZE 32
#define UI_CONTAINERPOOL_SIZE 48
#define UI_CONTAINERSTACK_SIZE 32
#define UI_IDSTACK_SIZE 32
#define UI_CLIPSTACK_SIZE 32
#define UI_MAX_WIDTHS 16

#define ui_min(a, b) ((a) < (b) ? (a) : (b))
#define ui_max(a, b) ((a) > (b) ? (a) : (b))
#define ui_stack(T, n) struct { int idx; T items[n]; }
#define expect(x)            \
    do                       \
    {                        \
        if (!(x))            \
        {                    \
            __debugbreak();  \
        }                    \
    } while (0)

///

enum {
  UI_CLIP_PART = 1,
  UI_CLIP_ALL
};

enum {
    UI_COLOR_TEXT,
    UI_COLOR_BORDER,
    UI_COLOR_WINDOWBG,
    UI_COLOR_MAX
};

enum {
    UI_ICON_CLOSE = 1,
    UI_ICON_CHECK,
    UI_ICON_COLLAPSED,
    UI_ICON_EXPANDED,
};

enum {
    UI_COMMAND_JUMP = 1,
    UI_COMMAND_CLIP,
    UI_COMMAND_RECT,
    UI_COMMAND_TEXT,
    UI_COMMAND_MAX
};

///

typedef struct { int x, y; } UI_Vec2;
typedef struct { int x, y, w, h; } UI_Rect;
typedef struct { unsigned char r, g, b, a; } UI_Color;

typedef unsigned UI_Id;
typedef struct { UI_Id id; int last_update; } UI_PoolItem;

typedef struct { int type, size; } UI_BaseCommand;
typedef struct { UI_BaseCommand base; void *dst; } UI_JumpCommand;
typedef struct { UI_BaseCommand base; UI_Rect rect; } UI_ClipCommand;
typedef struct { UI_BaseCommand base; UI_Rect rect; UI_Color color; } UI_RectCommand;
typedef struct { UI_BaseCommand base; UI_Vec2 pos; UI_Color color; wchar_t str[1]; } UI_TextCommand;

typedef union {
    int type;
    UI_BaseCommand base;
    UI_JumpCommand jump;
    UI_ClipCommand clip;
    UI_RectCommand rect;
    UI_TextCommand text;
} UI_Command;

typedef struct {
    UI_Command *head, *tail;
    UI_Rect rect;
    int zindex;
} UI_Container;

typedef struct {
    UI_Rect body;
    UI_Vec2 position;
    UI_Vec2 size;
    int widths[UI_MAX_WIDTHS];
    int items;
    int item_index;
    int next_row;
} UI_Layout;

typedef struct {
    UI_Vec2 size;
    int padding;
    int spacing;
    UI_Color colors[UI_COLOR_MAX];
} UI_Style;

typedef struct UI_Context UI_Context;
struct UI_Context {
    // callback
    int (*text_width)(const wchar_t* str, int len);
    int (*text_height)();
    void (*draw_frame)(UI_Context* ctx, UI_Rect rect, int colorid);
    // core state
    UI_Style* style;
    UI_Id last_id;
    int last_zindex;
    int frame;
    UI_Container* next_hover_root;
    // stack
    ui_stack(char, UI_COMMANDLIST_SIZE) command_list;
    ui_stack(UI_Container*, UI_ROOTLIST_SIZE) root_list;
    ui_stack(UI_Container*, UI_CONTAINERSTACK_SIZE) container_stack;
    ui_stack(UI_Id, UI_IDSTACK_SIZE) id_stack;
    ui_stack(UI_Rect, UI_CLIPSTACK_SIZE) clip_stack;
    // retained state pools
    UI_Container containers[UI_CONTAINERPOOL_SIZE];
    UI_PoolItem container_pool[UI_CONTAINERPOOL_SIZE];
    UI_Layout layout;
    // input state
    UI_Vec2 mouse_pos;
    bool mouse_pressed;
};

///

UI_Vec2 ui_vec2(int x, int y);
UI_Rect ui_rect(int x, int y, int w, int h);
UI_Color ui_color(int r, int g, int b, int a);

int ui_next_command(UI_Context* ctx, UI_Command** cmd);
void ui_begin_window(UI_Context* ctx, const char* title, UI_Rect rect);
void ui_end_window(UI_Context* ctx);

void ui_layout_row(UI_Context* ctx, int items, int height);
void ui_label(UI_Context* ctx, const wchar_t* text);

void ui_init(UI_Context* ctx);
void ui_begin(UI_Context* ctx);
void ui_end(UI_Context* ctx);
