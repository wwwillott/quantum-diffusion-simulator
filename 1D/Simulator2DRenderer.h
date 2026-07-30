#pragma once

#include "Simulator2D.h"
#include "raylib.h"

void DrawSimulator2D(Simulator2D& sim,
                     int screen_width,
                     int screen_height,
                     bool show_info = false,
                     bool is_paused = false,
                     Vector2 mouse_pos = {-1, -1},
                     bool is_mouse_down = false);
