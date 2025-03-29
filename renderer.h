#pragma once

#include <windows.h>
#include "ui.h"

HWND g_window;
int g_client_width;
int g_client_height;

void r_init();
void r_clear(UI_Color color);
void r_draw_rect(UI_Rect rect, UI_Color color);
void r_draw_icon(int id, UI_Rect rect, UI_Color color);
void r_draw_text(const wchar_t* text, UI_Vec2 pos, UI_Color color);
int r_get_text_width(const wchar_t* text, int len);
int r_get_text_height(void);
void r_set_clip_rect(UI_Rect rect);
int r_load_image(const char* path);
void r_draw_image(UI_Rect rect, int image_id);
void r_present();
void r_clean();
