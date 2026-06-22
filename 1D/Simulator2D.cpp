#include "Simulator2D.h"
#include <cmath>
#include <algorithm>
#include "DataParser.h"

Simulator2D::Simulator2D(SimConfig master_settings) {
    config = master_settings;
    int N = config.resolution;
    int total_nodes = N * N;
    
    // Allocate all memory
    amp_N.resize(total_nodes, {0.0f, 0.0f});
    amp_S.resize(total_nodes, {0.0f, 0.0f});
    amp_E.resize(total_nodes, {0.0f, 0.0f});
    amp_W.resize(total_nodes, {0.0f, 0.0f});
    
    next_amp_N.resize(total_nodes, {0.0f, 0.0f});
    next_amp_S.resize(total_nodes, {0.0f, 0.0f});
    next_amp_E.resize(total_nodes, {0.0f, 0.0f});
    next_amp_W.resize(total_nodes, {0.0f, 0.0f});

    prev_probs.resize(total_nodes, 0.0f);

    applyInitialState();
}

void Simulator2D::applyInitialState() {
    int N = config.resolution;
    // Locate the exact center coordinate in the flattened array
    int center_x = N / 2;
    int center_y = N / 2;
    int center_idx = (center_y * N) + center_x;

    // Inject the selected initial coin state
    switch(config.init_state_2d) {
        case InitialState2D::PURE_NORTH:
            amp_N[center_idx] = {1.0f, 0.0f};
            break;
            
        case InitialState2D::UNIFORM: // The Localization Trap
            amp_N[center_idx] = {0.5f, 0.0f};
            amp_S[center_idx] = {0.5f, 0.0f};
            amp_E[center_idx] = {0.5f, 0.0f};
            amp_W[center_idx] = {0.5f, 0.0f};
            break;
            
        case InitialState2D::ALTERNATING_PHASE: // The Perfect Symmetric Ring
            amp_N[center_idx] = {0.5f, 0.0f};
            amp_S[center_idx] = {-0.5f, 0.0f};
            amp_E[center_idx] = {0.5f, 0.0f};
            amp_W[center_idx] = {-0.5f, 0.0f};
            break;
    }
}

void Simulator2D::update() {
    if (config.mode == SimMode::QUANTUM) {
        int N = config.resolution;
        int total_nodes = N * N;

        // Save the current probabilities BEFORE we shift them
        for (int i = 0; i < total_nodes; i++) {
            prev_probs[i] = std::norm(amp_N[i]) + std::norm(amp_S[i]) + std::norm(amp_E[i]) + std::norm(amp_W[i]);
        }

        // --- PHASE 1: THE GROVER COIN TOSS ---
        for (int i = 0; i < total_nodes; i++) {
            std::complex<float> n = amp_N[i];
            std::complex<float> s = amp_S[i];
            std::complex<float> e = amp_E[i];
            std::complex<float> w = amp_W[i];

            // 1. Calculate 0.5 * Sum (The true Grover Inversion Term)
            std::complex<float> half_sum = (n + s + e + w) * 0.5f;

            // 2. Invert (Half Sum - Original)
            amp_N[i] = half_sum - n;
            amp_S[i] = half_sum - s;
            amp_E[i] = half_sum - e;
            amp_W[i] = half_sum - w;
        }

        // --- PHASE 2: THE 2D SHIFT OPERATOR ---
        std::fill(next_amp_N.begin(), next_amp_N.end(), std::complex<float>(0.0f, 0.0f));
        std::fill(next_amp_S.begin(), next_amp_S.end(), std::complex<float>(0.0f, 0.0f));
        std::fill(next_amp_E.begin(), next_amp_E.end(), std::complex<float>(0.0f, 0.0f));
        std::fill(next_amp_W.begin(), next_amp_W.end(), std::complex<float>(0.0f, 0.0f));

        for (int y = 0; y < N; y++) {
            for (int x = 0; x < N; x++) {
                int current_idx = y * N + x;

                if (config.boundary_condition == BoundaryType::ABSORBING) {
                    // THE VOID: Waves fall off the edge
                    if (y > 0)     next_amp_N[(y - 1) * N + x] = amp_N[current_idx];
                    if (y < N - 1) next_amp_S[(y + 1) * N + x] = amp_S[current_idx];
                    if (x < N - 1) next_amp_E[y * N + (x + 1)] = amp_E[current_idx];
                    if (x > 0)     next_amp_W[y * N + (x - 1)] = amp_W[current_idx];
                } 
                else {
                    // THE BOX: Waves bounce and invert direction
                    if (y > 0) next_amp_N[(y - 1) * N + x] += amp_N[current_idx]; 
                    else       next_amp_S[current_idx]     += amp_N[current_idx]; 

                    if (y < N - 1) next_amp_S[(y + 1) * N + x] += amp_S[current_idx];
                    else           next_amp_N[current_idx]     += amp_S[current_idx]; 

                    if (x < N - 1) next_amp_E[y * N + (x + 1)] += amp_E[current_idx];
                    else           next_amp_W[current_idx]     += amp_E[current_idx]; 

                    if (x > 0) next_amp_W[y * N + (x - 1)] += amp_W[current_idx];
                    else       next_amp_E[current_idx]     += amp_W[current_idx]; 
                }
            }
        }

        // Ping-Pong the buffers
        amp_N = next_amp_N;
        amp_S = next_amp_S;
        amp_E = next_amp_E;
        amp_W = next_amp_W;
    }
}

void Simulator2D::draw(int screen_width, int screen_height, bool show_info, bool is_paused, Vector2 mouse_pos, bool is_mouse_down) {
    
    int N = config.resolution;
    
    // 1. Calculate the Perfect Square Grid
    float grid_pixel_size = std::min(screen_width, screen_height) - 80.0f;
    float cell_size = grid_pixel_size / N;

    float offset_x = (screen_width - grid_pixel_size) / 2.0f;
    float offset_y = (screen_height - grid_pixel_size) / 2.0f;

    // Build the time-averaged probability density array
    std::vector<float> display_probs(N * N, 0.0f);
    float max_p = 0.0001f;

    // Statistical Trackers
    float expected_x = 0.0f, expected_x2 = 0.0f;
    float expected_y = 0.0f, expected_y2 = 0.0f;
    float sum_p = 0.0f;
    int center = N / 2;

    for (int i = 0; i < N * N; i++) {
        float current_p = std::norm(amp_N[i]) + std::norm(amp_S[i]) + std::norm(amp_E[i]) + std::norm(amp_W[i]);
        display_probs[i] = (current_p + prev_probs[i]) * 0.5f;

        if (display_probs[i] > max_p) max_p = display_probs[i];

        // Piggyback the loop to calculate spatial variance
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

    // Push the new Standard Deviations to history
    if (sum_p > 0.0f) {
        expected_x /= sum_p; expected_x2 /= sum_p;
        expected_y /= sum_p; expected_y2 /= sum_p;
    }
    
    if (!is_paused) {
        float var_x = expected_x2 - (expected_x * expected_x);
        float var_y = expected_y2 - (expected_y * expected_y);
        float sigma_x = (var_x > 0.0f) ? std::sqrt(var_x) : 0.0f;
        float sigma_y = (var_y > 0.0f) ? std::sqrt(var_y) : 0.0f;
        
        std_dev_x_hist.push_back(sigma_x);
        std_dev_y_hist.push_back(sigma_y);
        std_dev_total_hist.push_back(std::sqrt(var_x + var_y));
    }

    // 2. Draw Perimeter Tick Marks and Labels
    int tick_spacing = N / 10; 
    if (tick_spacing == 0) tick_spacing = 1;

    Color tickColor = { 150, 150, 150, 255 };

    for (int i = 0; i <= N; i += tick_spacing) {
        float pos = i * cell_size;
        int coord_val = i - center; 
        
        const char* label = TextFormat("%d", coord_val);
        int text_width = MeasureText(label, 10);
        
        DrawLineEx({ offset_x + pos, offset_y + grid_pixel_size }, { offset_x + pos, offset_y + grid_pixel_size + 8 }, 1.0f, tickColor);
        DrawText(label, (int)(offset_x + pos) - (text_width / 2), (int)(offset_y + grid_pixel_size + 12), 10, tickColor);

        DrawLineEx({ offset_x, offset_y + pos }, { offset_x - 8, offset_y + pos }, 1.0f, tickColor);
        DrawText(label, (int)(offset_x - 12) - text_width, (int)(offset_y + pos) - 5, 10, tickColor);
    }

    DrawRectangle((int)offset_x, (int)offset_y, (int)grid_pixel_size, (int)grid_pixel_size, { 5, 5, 10, 255 });

    // 3. Render the 2D Heatmap
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            int idx = y * N + x;
            if (display_probs[idx] == 0.0f) continue; 

            // --- THE VISUAL FIX ---
            // Calculate the linear ratio (0.0 to 1.0)
            float base_ratio = display_probs[idx] / max_p;
            
            // Apply a power curve. The smaller the exponent, the brighter the dark waves become.
            // 0.3f is a highly aggressive boost so you can see every single quantum ripple.
            float intensity = std::pow(base_ratio, 0.3f); 
            
            unsigned char color_val = (unsigned char)(intensity * 255);
            Color cellColor = { 0, (unsigned char)(color_val * 0.8f), color_val, 255 };

            DrawRectangle(
                (int)(offset_x + x * cell_size), 
                (int)(offset_y + y * cell_size), 
                (int)std::ceil(cell_size), 
                (int)std::ceil(cell_size), 
                cellColor
            );
        }
    }

    // 4. Draw the Physical Wall Boundaries
    if (config.boundary_condition == BoundaryType::REFLECTIVE) {
        DrawRectangleLinesEx({ offset_x - 4.0f, offset_y - 4.0f, grid_pixel_size + 8.0f, grid_pixel_size + 8.0f }, 4.0f, { 100, 200, 255, 200 }); 
    } else {
        DrawRectangleLinesEx({ offset_x - 1.0f, offset_y - 1.0f, grid_pixel_size + 2.0f, grid_pixel_size + 2.0f }, 1.0f, { 180, 50, 50, 150 });
    }

    // 5. Draw the HUD
    if (show_info) {
        DrawRectangle(50, 50, 500, 300, { 20, 25, 30, 240 });
        DrawRectangleLines(50, 50, 500, 300, { 100, 150, 200, 255 });
        DrawText("2D DIAGNOSTICS [WIP]", 70, 70, 20, YELLOW);
        DrawText("Grover Coin (Inversion about Average) Active", 70, 110, 15, WHITE);
        
        const char* boundStr = (config.boundary_condition == BoundaryType::REFLECTIVE) ? "Reflective Walls (Trapped)" : "Absorbing Walls (Void)";
        DrawText(TextFormat("Boundary System: %s", boundStr), 70, 140, 15, LIGHTGRAY);
    }

    // 6. Draw Standard Deviation Telemetry Overlay (Top Right)
    int overlay_width = 300;
    int overlay_height = 160;
    int overlay_x = screen_width - overlay_width - 20; 
    int overlay_y = 20; 

    DrawRectangle(overlay_x, overlay_y, overlay_width, overlay_height, { 30, 30, 30, 220 });
    DrawRectangleLines(overlay_x, overlay_y, overlay_width, overlay_height, { 100, 100, 100, 255 });
    DrawText("Standard Deviation", overlay_x + 10, overlay_y + 10, 10, { 200, 200, 200, 255 });

    if (!std_dev_total_hist.empty()) {
        DrawText(TextFormat("X: %.2f", std_dev_x_hist.back()), overlay_x + 10, overlay_y + 30, 10, { 176, 224, 230, 255 }); 
        DrawText(TextFormat("Y: %.2f", std_dev_y_hist.back()), overlay_x + 80, overlay_y + 30, 10, { 100, 255, 100, 255 });  
        DrawText(TextFormat("Tot: %.2f", std_dev_total_hist.back()), overlay_x + 150, overlay_y + 30, 10, { 100, 255, 255, 255 });

        if (std_dev_total_hist.size() > 1) {
            float max_val = 1.0f;
            for (float v : std_dev_total_hist) { if (v > max_val) max_val = v; }

            float graph_y_start = overlay_y + overlay_height;
            float graph_h = overlay_height - 50; 
            float x_step = (float)overlay_width / std_dev_total_hist.size();

            for (size_t j = 1; j < std_dev_total_hist.size(); j++) {
                float px1 = overlay_x + (j - 1) * x_step;
                float px2 = overlay_x + j * x_step;

                float y_x1 = graph_y_start - (std_dev_x_hist[j - 1] / max_val * graph_h);
                float y_x2 = graph_y_start - (std_dev_x_hist[j] / max_val * graph_h);
                
                float y_y1 = graph_y_start - (std_dev_y_hist[j - 1] / max_val * graph_h);
                float y_y2 = graph_y_start - (std_dev_y_hist[j] / max_val * graph_h);

                float y_t1 = graph_y_start - (std_dev_total_hist[j - 1] / max_val * graph_h);
                float y_t2 = graph_y_start - (std_dev_total_hist[j] / max_val * graph_h);

                // Alternating Draw Order Trick
                // If the lines overlap perfectly, swapping the draw order every segment creates a dashed effect!
                if (j % 2 == 0) {
                    DrawLineEx({ px1, y_x1 }, { px2, y_x2 }, 2.0f, { 176, 224, 230, 255 }); // Blue X first
                    DrawLineEx({ px1, y_y1 }, { px2, y_y2 }, 2.0f, { 100, 255, 100, 255 }); // Green Y on top
                } else {
                    DrawLineEx({ px1, y_y1 }, { px2, y_y2 }, 2.0f, { 100, 255, 100, 255 }); // Green Y first
                    DrawLineEx({ px1, y_x1 }, { px2, y_x2 }, 2.0f, { 176, 224, 230, 255 }); // Blue X on top
                }

                // Total standard deviation drawn last so it stays on top of the individual axes
                DrawLineEx({ px1, y_t1 }, { px2, y_t2 }, 2.0f, { 100, 255, 255, 255 }); 
            }
        }
    }

    // 7. Interactive Grid Painting (Only active when paused/setup)
    if (is_paused) {
        float mouse_x = mouse_pos.x;
        float mouse_y = mouse_pos.y;

        // Check if the mouse is physically hovering inside the 2D grid bounds
        if (mouse_x >= offset_x && mouse_x < offset_x + grid_pixel_size &&
            mouse_y >= offset_y && mouse_y < offset_y + grid_pixel_size) {

            // Calculate the exact 2D array index the mouse is hovering over
            int grid_x = (int)((mouse_x - offset_x) / cell_size);
            int grid_y = (int)((mouse_y - offset_y) / cell_size);

            // Double check array bounds to prevent segfaults
            if (grid_x >= 0 && grid_x < N && grid_y >= 0 && grid_y < N) {
                
                // Draw the goldenrod bounding box
                DrawRectangleLines(
                    (int)(offset_x + grid_x * cell_size), 
                    (int)(offset_y + grid_y * cell_size), 
                    (int)std::ceil(cell_size), 
                    (int)std::ceil(cell_size), 
                    GOLD // Raylib's built-in goldenrod color
                );

                // If the user clicks or drags, paint the state based on the UI selection
                if (is_mouse_down) {
                    int idx = grid_y * N + grid_x;
                    
                    // Dynamically inject the correct phase state
                    switch (config.init_state_2d) { 
                        case InitialState2D::PURE_NORTH:
                            amp_N[idx] = {1.0f, 0.0f};
                            amp_S[idx] = {0.0f, 0.0f};
                            amp_E[idx] = {0.0f, 0.0f};
                            amp_W[idx] = {0.0f, 0.0f};
                            break;
                            
                        case InitialState2D::UNIFORM:
                            amp_N[idx] = {0.5f, 0.0f};
                            amp_S[idx] = {0.5f, 0.0f};
                            amp_E[idx] = {0.5f, 0.0f};
                            amp_W[idx] = {0.5f, 0.0f};
                            break;
                            
                        case InitialState2D::ALTERNATING_PHASE:
                            amp_N[idx] = { 0.5f, 0.0f};
                            amp_S[idx] = {-0.5f, 0.0f};
                            amp_E[idx] = { 0.5f, 0.0f};
                            amp_W[idx] = {-0.5f, 0.0f};
                            break;
                    }
                    
                    // Force the prev_probs to 1.0 so the blue pixel instantly appears on screen while paused
                    prev_probs[idx] = 1.0f; 
                }
            }
        }
    }
}

void Simulator2D::inject_dataset(const std::vector<ViralHotspot>& dataset) {
    int N = config.resolution;
    
    // Clear the grid first
    std::fill(amp_N.begin(), amp_N.end(), std::complex<float>(0.0f, 0.0f));
    std::fill(amp_S.begin(), amp_S.end(), std::complex<float>(0.0f, 0.0f));
    std::fill(amp_E.begin(), amp_E.end(), std::complex<float>(0.0f, 0.0f));
    std::fill(amp_W.begin(), amp_W.end(), std::complex<float>(0.0f, 0.0f));
    std::fill(prev_probs.begin(), prev_probs.end(), 0.0f);

    // INSTEAD OF HARDCODED BOUNDS, PULL FROM CONFIG:
    float min_lat = config.min_lat;
    float max_lat = config.max_lat;
    float min_lon = config.min_lon;
    float max_lon = config.max_lon;

    // --- Find the Maximum Case Count (Only for nodes INSIDE the bounds) ---
    int max_cases = 1; 
    for (const auto& point : dataset) {
        // FILTER: Ignore points outside our bounding box for the max calculation
        if (point.lat < min_lat || point.lat > max_lat || point.lon < min_lon || point.lon > max_lon) {
            continue; 
        }
        if (point.confirmed_cases > max_cases) {
            max_cases = point.confirmed_cases;
        }
    }

    for (const auto& point : dataset) {
        // --- NEW: The Geographic Filter ---
        // If the coordinate is outside our box, completely ignore it.
        if (point.lat < min_lat || point.lat > max_lat || point.lon < min_lon || point.lon > max_lon) {
            continue; 
        }

        // Calculate the percentage across our CUSTOM map bounds
        float pct_x = (point.lon - min_lon) / (max_lon - min_lon);
        float pct_y = (max_lat - point.lat) / (max_lat - min_lat);

        int grid_x = static_cast<int>(pct_x * (N - 1));
        int grid_y = static_cast<int>(pct_y * (N - 1));

        if (grid_x >= 0 && grid_x < N && grid_y >= 0 && grid_y < N) {
            int idx = grid_y * N + grid_x;

            // --- The Born Rule Fix ---
            float weight = static_cast<float>(point.confirmed_cases) / static_cast<float>(max_cases);
            
            // Amplitude must be the square root of the target probability
            float magnitude = 0.5f * std::sqrt(weight);

            // Inject the scaled Alternating Phase state
            amp_N[idx] = { magnitude, 0.0f};
            amp_S[idx] = {-magnitude, 0.0f};
            amp_E[idx] = { magnitude, 0.0f};
            amp_W[idx] = {-magnitude, 0.0f};

            // Scale the initial visual brightness so you can see the difference while paused
            prev_probs[idx] = weight; 
        }
    }
}