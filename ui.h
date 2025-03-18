#pragma once

#define UI_COMMANDLIST_SIZE (256 * 1024)
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
    UI_COMMAND_RECT = 1,
    UI_COMMAND_MAX
};

///

typedef struct { int x, y, w, h; } UI_Rect;
typedef struct { unsigned char r, g, b, a; } UI_Color;

typedef struct { int type, size; } UI_BaseCommand;
typedef struct { UI_BaseCommand base; UI_Rect rect; UI_Color color; } UI_RectCommand;

typedef union {
    int type;
    UI_BaseCommand base;
    UI_RectCommand rect;
} UI_Command;

typedef struct UI_Context UI_Context;
struct UI_Context {
    ui_stack(char, UI_COMMANDLIST_SIZE) command_list;
};

///

UI_Rect ui_rect(int x, int y, int w, int h);
UI_Color ui_color(int r, int g, int b, int a);
void ui_draw_box(UI_Context* ctx, UI_Rect rect, UI_Color color, unsigned border_width);
int ui_next_command(UI_Context* ctx, UI_Command** cmd);
void ui_begin(UI_Context *ctx);
