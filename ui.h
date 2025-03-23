#pragma once

#include <stdbool.h>

#define UI_COMMANDLIST_SIZE (256 * 1024)
#define UI_ROOTLIST_SIZE 32
#define UI_CONTAINERPOOL_SIZE 48

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
  UI_ICON_CLOSE = 1,
  UI_ICON_CHECK,
  UI_ICON_COLLAPSED,
  UI_ICON_EXPANDED,
};

enum {
    UI_COMMAND_JUMP = 1,
    UI_COMMAND_RECT,
    UI_COMMAND_MAX
};

///

typedef struct { int x, y; } UI_Vec2;
typedef struct { int x, y, w, h; } UI_Rect;
typedef struct { unsigned char r, g, b, a; } UI_Color;

typedef struct { int type, size; } UI_BaseCommand;
typedef struct { UI_BaseCommand base; void *dst; } UI_JumpCommand;
typedef struct { UI_BaseCommand base; UI_Rect rect; UI_Color color; } UI_RectCommand;

typedef union {
    int type;
    UI_BaseCommand base;
    UI_JumpCommand jump;
    UI_RectCommand rect;
} UI_Command;

typedef struct {
    UI_Command *head, *tail;
    UI_Rect rect;
    int zindex;
} UI_Container;

typedef struct UI_Context UI_Context;
struct UI_Context {
    // core state
    int last_zindex;
    UI_Container* next_hover_root;
    // stack
    ui_stack(char, UI_COMMANDLIST_SIZE) command_list;
    ui_stack(UI_Container*, UI_ROOTLIST_SIZE) root_list;
    // retained state pools
    UI_Container containers[UI_CONTAINERPOOL_SIZE];
    // input state
    UI_Vec2 mouse_pos;
    bool mouse_pressed;
};

///

UI_Vec2 ui_vec2(int x, int y);
UI_Rect ui_rect(int x, int y, int w, int h);
UI_Color ui_color(int r, int g, int b, int a);
void ui_square(UI_Context* ctx, UI_Vec2 pos, unsigned wh, UI_Color color);
int ui_next_command(UI_Context* ctx, UI_Command** cmd);
void ui_begin(UI_Context* ctx);
void ui_end(UI_Context* ctx);
