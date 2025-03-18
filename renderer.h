#pragma once

#include <windows.h>
#include "ui.h"

HWND g_window;
int g_client_width;
int g_client_height;

void r_init();
void r_clear(UI_Color color);
void r_draw_rect(UI_Rect rect, UI_Color color);
void r_present();
void r_clean();
