#include "Simulator2DRenderer.h"

#include <algorithm>
#include <cmath>
#include <vector>

void DrawSimulator2D(Simulator2D& sim,
                     int screen_width,
                     int screen_height,
                     bool show_info,
                     bool is_paused,
                     Vector2 mouse_pos,
                     bool is_mouse_down) {
    (void)mouse_pos;
    (void)is_mouse_down;

    const SimConfig& config = sim.config;
    int N = config.resolution;
    float grid_pixel_size = std::min(screen_width, screen_height) - 80.0f;
    float cell_size = grid_pixel_size / N;
    float offset_x = (screen_width - grid_pixel_size) / 2.0f;
    float offset_y = (screen_height - grid_pixel_size) / 2.0f;

    const auto& amp_N = sim.get_amp_N();
    const auto& amp_S = sim.get_amp_S();
    const auto& amp_E = sim.get_amp_E();
    const auto& amp_W = sim.get_amp_W();
    const auto& amp_C = sim.get_amp_C();
    const auto& prev_probs = sim.get_prev_probs();

    std::vector<float> display_probs(N * N, 0.0f);
    float max_p = 0.0001f;

    float expected_x = 0.0f, expected_x2 = 0.0f;
    float expected_y = 0.0f, expected_y2 = 0.0f;
    float sum_p = 0.0f;
    int center = N / 2;

    for (int i = 0; i < N * N; i++) {
        float current_p = std::norm(amp_N[i]) + std::norm(amp_S[i]) + std::norm(amp_E[i]) +
                          std::norm(amp_W[i]) + std::norm(amp_C[i]);
        display_probs[i] = (current_p + prev_probs[i]) * 0.5f;

        if (display_probs[i] > max_p) max_p = display_probs[i];

        int grid_y = i / N;
        int grid_x = i % N;
        float cx = (float)(grid_x - center);
        float cy = (float)(grid_y - center);

        sum_p += current_p;
        expected_x += cx * current_p;
        expected_x2 += cx * cx * current_p;
        expected_y += cy * current_p;
        expected_y2 += cy * cy * current_p;
    }

    if (sum_p > 0.0f) {
        expected_x /= sum_p;
        expected_x2 /= sum_p;
        expected_y /= sum_p;
        expected_y2 /= sum_p;
    }

    if (!is_paused) {
        float var_x = expected_x2 - (expected_x * expected_x);
        float var_y = expected_y2 - (expected_y * expected_y);
        float sigma_x = (var_x > 0.0f) ? std::sqrt(var_x) : 0.0f;
        float sigma_y = (var_y > 0.0f) ? std::sqrt(var_y) : 0.0f;

        sim.mutable_std_dev_x_hist().push_back(sigma_x);
        sim.mutable_std_dev_y_hist().push_back(sigma_y);
        sim.mutable_std_dev_total_hist().push_back(std::sqrt(var_x + var_y));
    }

    int tick_spacing = N / 10;
    if (tick_spacing == 0) tick_spacing = 1;
    Color tickColor = {150, 150, 150, 255};

    for (int i = 0; i <= N; i += tick_spacing) {
        float pos = i * cell_size;
        int coord_val = i - center;

        const char* label = TextFormat("%d", coord_val);
        int text_width = MeasureText(label, 10);

        DrawLineEx({offset_x + pos, offset_y + grid_pixel_size},
                   {offset_x + pos, offset_y + grid_pixel_size + 8}, 1.0f, tickColor);
        DrawText(label, (int)(offset_x + pos) - (text_width / 2),
                 (int)(offset_y + grid_pixel_size + 12), 10, tickColor);

        DrawLineEx({offset_x, offset_y + pos}, {offset_x - 8, offset_y + pos}, 1.0f, tickColor);
        DrawText(label, (int)(offset_x - 12) - text_width, (int)(offset_y + pos) - 5, 10, tickColor);
    }

    DrawRectangle((int)offset_x, (int)offset_y, (int)grid_pixel_size, (int)grid_pixel_size,
                  {5, 5, 10, 255});

    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            int idx = y * N + x;
            if (display_probs[idx] == 0.0f) continue;

            float base_ratio = display_probs[idx] / max_p;
            float intensity = std::pow(base_ratio, 0.3f);

            unsigned char color_val = (unsigned char)(intensity * 255);
            Color cellColor = {0, (unsigned char)(color_val * 0.8f), color_val, 255};

            DrawRectangle((int)(offset_x + x * cell_size), (int)(offset_y + y * cell_size),
                          (int)std::ceil(cell_size), (int)std::ceil(cell_size), cellColor);

            if (sim.show_historical_overlay && sim.historical_probs[idx] > 0.0f) {
                unsigned char alpha_val =
                    (unsigned char)(std::pow(sim.historical_probs[idx], 0.3f) * 200);
                Color overlayColor = {255, 50, 0, alpha_val};

                DrawRectangle((int)(offset_x + x * cell_size), (int)(offset_y + y * cell_size),
                              (int)std::ceil(cell_size), (int)std::ceil(cell_size), overlayColor);
            }
        }
    }

    if (config.boundary_condition == BoundaryType::REFLECTIVE) {
        DrawRectangleLinesEx(
            {offset_x - 4.0f, offset_y - 4.0f, grid_pixel_size + 8.0f, grid_pixel_size + 8.0f}, 4.0f,
            {100, 200, 255, 200});
    } else {
        DrawRectangleLinesEx(
            {offset_x - 1.0f, offset_y - 1.0f, grid_pixel_size + 2.0f, grid_pixel_size + 2.0f}, 1.0f,
            {180, 50, 50, 150});
    }

    if (show_info) {
        DrawRectangle(50, 50, 500, 300, {20, 25, 30, 240});
        DrawRectangleLines(50, 50, 500, 300, {100, 150, 200, 255});
        DrawText("2D DIAGNOSTICS [WIP]", 70, 70, 20, YELLOW);

        const char* modeStr =
            (config.mode == SimMode::QUANTUM) ? "QUANTUM ENGINE ACTIVE" : "CLASSICAL ENGINE ACTIVE";
        DrawText(modeStr, 70, 110, 15, WHITE);

        const char* boundStr = (config.boundary_condition == BoundaryType::REFLECTIVE)
                                   ? "Reflective Walls (Trapped)"
                                   : "Absorbing Walls (Void)";
        DrawText(TextFormat("Boundary System: %s", boundStr), 70, 140, 15, LIGHTGRAY);
    }

    int overlay_width = 300;
    int overlay_height = 120;
    int overlay_x = screen_width - overlay_width - 20;
    int current_y = 20;

    auto draw_graph = [&](const char* title, const std::vector<float>& history, Color col) {
        if (history.empty()) return;
        DrawRectangle(overlay_x, current_y, overlay_width, overlay_height, {30, 30, 30, 220});
        DrawRectangleLines(overlay_x, current_y, overlay_width, overlay_height, col);
        DrawText(title, overlay_x + 10, current_y + 10, 10, {200, 200, 200, 255});

        float current_val = history.back();
        const char* val_text = TextFormat("%.6f", current_val);
        DrawText(val_text, overlay_x + overlay_width - MeasureText(val_text, 10) - 10,
                 current_y + 10, 10, col);

        if (history.size() > 1) {
            float max_val = 0.0001f;
            for (float v : history)
                if (v > max_val) max_val = v;

            float graph_y_start = current_y + overlay_height;
            float graph_h = overlay_height - 30;
            float x_step = (float)overlay_width / history.size();

            for (size_t i = 1; i < history.size(); i++) {
                Vector2 p1 = {overlay_x + (i - 1) * x_step,
                              graph_y_start - (history[i - 1] / max_val * graph_h)};
                Vector2 p2 = {overlay_x + i * x_step,
                              graph_y_start - (history[i] / max_val * graph_h)};
                DrawLineEx(p1, p2, 2.0f, col);
            }
        }
        current_y += overlay_height + 10;
    };

    if (sim.track_masked_mse)
        draw_graph("Masked MSE (Hotspots Only)", sim.masked_mse_history, {255, 100, 100, 255});
    if (sim.track_emd)
        draw_graph("Earth Mover's Distance (Marginal Proxy)", sim.emd_history,
                   {100, 255, 100, 255});

    if (sim.show_metrics_menu) {
        int menu_w = 450;
        int menu_h = 240;
        int menu_x = (screen_width - menu_w) / 2;
        int menu_y = (screen_height - menu_h) / 2;

        DrawRectangle(0, 0, screen_width, screen_height, {10, 10, 10, 150});
        DrawRectangle(menu_x, menu_y, menu_w, menu_h, {25, 30, 40, 255});
        DrawRectangleLines(menu_x, menu_y, menu_w, menu_h, {100, 150, 200, 255});

        DrawText("TELEMETRY TRACKERS",
                 menu_x + menu_w / 2 - MeasureText("TELEMETRY TRACKERS", 20) / 2, menu_y + 20, 20,
                 WHITE);

        Color col1 = sim.track_masked_mse ? Color{0, 200, 255, 255} : GRAY;
        DrawText("[1] Masked MSE", menu_x + 80, menu_y + 70, 20, col1);

        Color col2 = sim.track_emd ? Color{0, 200, 255, 255} : GRAY;
        DrawText("[2] Earth Mover's Distance", menu_x + 80, menu_y + 110, 20, col2);

        Color col3 = sim.show_historical_overlay ? Color{0, 200, 255, 255} : GRAY;
        DrawText("[3] Historical Overlay (Red Map)", menu_x + 80, menu_y + 150, 20, col3);

        DrawText("Press Z to close and resume",
                 menu_x + menu_w / 2 - MeasureText("Press Z to close and resume", 15) / 2,
                 menu_y + 200, 15, LIGHTGRAY);
    }

    if (sim.show_mode_menu) {
        int menu_w = 450;
        int menu_h = 200;
        int menu_x = (screen_width - menu_w) / 2;
        int menu_y = (screen_height - menu_h) / 2;

        DrawRectangle(0, 0, screen_width, screen_height, {10, 10, 10, 150});
        DrawRectangle(menu_x, menu_y, menu_w, menu_h, {40, 25, 30, 255});
        DrawRectangleLines(menu_x, menu_y, menu_w, menu_h, {200, 100, 100, 255});

        DrawText("COMPUTE ENGINE SELECTION",
                 menu_x + menu_w / 2 - MeasureText("COMPUTE ENGINE SELECTION", 20) / 2,
                 menu_y + 20, 20, WHITE);

        Color col1 = (config.mode == SimMode::QUANTUM) ? Color{255, 100, 100, 255} : GRAY;
        DrawText("[1] Quantum Walk Virtual Machine", menu_x + 50, menu_y + 80, 20, col1);

        Color col2 = (config.mode == SimMode::CLASSICAL) ? Color{255, 100, 100, 255} : GRAY;
        DrawText("[2] Classical Diffusion Engine", menu_x + 50, menu_y + 120, 20, col2);

        DrawText("Press M to close and resume",
                 menu_x + menu_w / 2 - MeasureText("Press M to close and resume", 15) / 2,
                 menu_y + 170, 15, LIGHTGRAY);
    }
}
