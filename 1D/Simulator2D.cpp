#include "Simulator2D.h"
#include <cmath>
#include <algorithm>
#include "DataParser.h"

Simulator2D::Simulator2D(SimConfig master_settings) {
    config = master_settings;
    int N = config.resolution;
    int total_nodes = N * N;
    
    amp_N.resize(total_nodes, {0.0f, 0.0f});
    amp_S.resize(total_nodes, {0.0f, 0.0f});
    amp_E.resize(total_nodes, {0.0f, 0.0f});
    amp_W.resize(total_nodes, {0.0f, 0.0f});
    
    next_amp_N.resize(total_nodes, {0.0f, 0.0f});
    next_amp_S.resize(total_nodes, {0.0f, 0.0f});
    next_amp_E.resize(total_nodes, {0.0f, 0.0f});
    next_amp_W.resize(total_nodes, {0.0f, 0.0f});

    prev_probs.resize(total_nodes, 0.0f);
    node_growth_rate.resize(total_nodes, 1.0f);

    applyInitialState();
}

void Simulator2D::applyInitialState() {
    int N = config.resolution;
    int center_x = N / 2;
    int center_y = N / 2;
    int center_idx = (center_y * N) + center_x;

    switch(config.init_state_2d) {
        case InitialState2D::PURE_NORTH:
            amp_N[center_idx] = {1.0f, 0.0f};
            break;
        case InitialState2D::UNIFORM:
            amp_N[center_idx] = {0.5f, 0.0f}; amp_S[center_idx] = {0.5f, 0.0f};
            amp_E[center_idx] = {0.5f, 0.0f}; amp_W[center_idx] = {0.5f, 0.0f};
            break;
        case InitialState2D::ALTERNATING_PHASE:
            amp_N[center_idx] = {0.5f, 0.0f}; amp_S[center_idx] = {-0.5f, 0.0f};
            amp_E[center_idx] = {0.5f, 0.0f}; amp_W[center_idx] = {-0.5f, 0.0f};
            break;
        case InitialState2D::CHIRAL_WEST:
            amp_N[center_idx] = { 0.5f, 0.0f}; amp_S[center_idx] = { 0.0f, 0.5f};
            amp_E[center_idx] = {-0.5f, 0.0f}; amp_W[center_idx] = { 0.0f,-0.5f};
            break;
        case InitialState2D::HADAMARD_SYMMETRIC: 
            amp_N[center_idx] = { 0.5f,  0.0f}; amp_S[center_idx] = { 0.0f,  0.5f};
            amp_E[center_idx] = { 0.0f,  0.5f}; amp_W[center_idx] = {-0.5f,  0.0f};
            break;
    }
}

void Simulator2D::update() {
    current_step++;
    int N = config.resolution;
    int total_nodes = N * N;

    // Save current probabilities BEFORE modifying arrays
    for (int i = 0; i < total_nodes; i++) {
        prev_probs[i] = std::norm(amp_N[i]) + std::norm(amp_S[i]) + std::norm(amp_E[i]) + std::norm(amp_W[i]);
    }

    // --- PHASE 1: LOCAL MIXING ---
    if (config.mode == SimMode::QUANTUM) {
        const std::complex<float> I(0.0f, 1.0f); 

        for (int i = 0; i < total_nodes; i++) {
            std::complex<float> n = amp_N[i];
            std::complex<float> s = amp_S[i];
            std::complex<float> e = amp_E[i];
            std::complex<float> w = amp_W[i];

            if (config.system_type_2d == SystemType::CLOSED_UNITARY) {
                if (config.unitary_coin == UnitaryCoin2D::GROVER) {
                    std::complex<float> half_sum = (n + s + e + w) * 0.5f;
                    amp_N[i] = half_sum - n; amp_S[i] = half_sum - s;
                    amp_E[i] = half_sum - e; amp_W[i] = half_sum - w;
                } 
                else if (config.unitary_coin == UnitaryCoin2D::DFT) {
                    amp_N[i] = 0.5f * (n + s + e + w);
                    amp_S[i] = 0.5f * (n + I * s - e - I * w);
                    amp_E[i] = 0.5f * (n - s + e - w);
                    amp_W[i] = 0.5f * (n - I * s - e + I * w);
                }
                else if (config.unitary_coin == UnitaryCoin2D::HADAMARD_TENSOR) {
                    amp_N[i] = 0.5f * (n + s + e + w);
                    amp_S[i] = 0.5f * (n - s + e - w);
                    amp_E[i] = 0.5f * (n + s - e - w);
                    amp_W[i] = 0.5f * (n - s - e + w);
                }
                else if (config.unitary_coin == UnitaryCoin2D::ALTERNATING_DFT) {
                    bool is_even_step = (current_step % 2 == 0);
                    if (is_even_step) {
                        amp_N[i] = 0.5f * (n + s + e + w);
                        amp_S[i] = 0.5f * (n + I * s - e - I * w);
                        amp_E[i] = 0.5f * (n - s + e - w);
                        amp_W[i] = 0.5f * (n - I * s - e + I * w);
                    } else {
                        amp_N[i] = 0.5f * (n + s + e + w);
                        amp_S[i] = 0.5f * (n - I * s - e + I * w);
                        amp_E[i] = 0.5f * (n - s + e - w);
                        amp_W[i] = 0.5f * (n + I * s - e - I * w);
                    }
                }
            }
            else if (config.system_type_2d == SystemType::OPEN) {
                if (config.non_unitary_coin == NonUnitaryCoin2D::EPIDEMIC_SCALAR) {
                    std::complex<float> half_sum = (n + s + e + w) * 0.5f;
                    std::complex<float> n_mix = half_sum - n;
                    std::complex<float> s_mix = half_sum - s;
                    std::complex<float> e_mix = half_sum - e;
                    std::complex<float> w_mix = half_sum - w;

                    float local_R0 = node_growth_rate[i];
                    amp_N[i] = n_mix * local_R0;
                    amp_S[i] = s_mix * local_R0;
                    amp_E[i] = e_mix * local_R0;
                    amp_W[i] = w_mix * local_R0;
                }
            }
        }
    } 
    else if (config.mode == SimMode::CLASSICAL) {
        // --- NEW: CLASSICAL DIFFUSION ENGINE ---
        for (int i = 0; i < total_nodes; i++) {
            // Reconstruct probability from amplitudes
            float p = std::norm(amp_N[i]) + std::norm(amp_S[i]) + std::norm(amp_E[i]) + std::norm(amp_W[i]);

            // Apply biological scalar for Open Systems
            if (config.system_type_2d == SystemType::OPEN) {
                p *= node_growth_rate[i];
            }

            // Distribute probability evenly to N, S, E, W
            float next_p_dir = p * 0.25f;

            // Convert back to real amplitude so the shift operator can safely move it
            float out_amp = std::sqrt(next_p_dir);
            amp_N[i] = { out_amp, 0.0f };
            amp_S[i] = { out_amp, 0.0f };
            amp_E[i] = { out_amp, 0.0f };
            amp_W[i] = { out_amp, 0.0f };
        }
    }

    // --- PHASE 2: THE 2D SHIFT OPERATOR (Shared by Quantum & Classical) ---
    std::fill(next_amp_N.begin(), next_amp_N.end(), std::complex<float>(0.0f, 0.0f));
    std::fill(next_amp_S.begin(), next_amp_S.end(), std::complex<float>(0.0f, 0.0f));
    std::fill(next_amp_E.begin(), next_amp_E.end(), std::complex<float>(0.0f, 0.0f));
    std::fill(next_amp_W.begin(), next_amp_W.end(), std::complex<float>(0.0f, 0.0f));

    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            int current_idx = y * N + x;

            if (config.boundary_condition == BoundaryType::ABSORBING) {
                if (y > 0)     next_amp_N[(y - 1) * N + x] = amp_N[current_idx];
                if (y < N - 1) next_amp_S[(y + 1) * N + x] = amp_S[current_idx];
                if (x < N - 1) next_amp_E[y * N + (x + 1)] = amp_E[current_idx];
                if (x > 0)     next_amp_W[y * N + (x - 1)] = amp_W[current_idx];
            } 
            else {
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

    amp_N = next_amp_N;
    amp_S = next_amp_S;
    amp_E = next_amp_E;
    amp_W = next_amp_W;

   // --- PHASE 3: UPDATE HISTORICAL OVERLAY ---
    if (show_historical_overlay) {
        std::fill(historical_probs.begin(), historical_probs.end(), 0.0f);

        for (const auto& point : historical_dataset) {
            if (point.lat < config.min_lat || point.lat > config.max_lat || point.lon < config.min_lon || point.lon > config.max_lon) continue;

            float pct_x = (point.lon - config.min_lon) / (config.max_lon - config.min_lon);
            float pct_y = (config.max_lat - point.lat) / (config.max_lat - config.min_lat);

            int grid_x = static_cast<int>(pct_x * (N - 1));
            int grid_y = static_cast<int>(pct_y * (N - 1));

            if (grid_x >= 0 && grid_x < N && grid_y >= 0 && grid_y < N) {
                int day_idx = current_step;
                if (day_idx >= point.cases_history.size()) day_idx = point.cases_history.size() - 1; 

                // REVERTED TO LINEAR SCALE
                float hist_weight = static_cast<float>(point.cases_history[day_idx]) / static_cast<float>(max_historical_cases);
                
                int idx = grid_y * N + grid_x;
                float current_val = historical_probs[idx] + hist_weight;
                historical_probs[idx] = std::min(current_val, 1.0f); 
            }
        }
    }
}

void Simulator2D::draw(int screen_width, int screen_height, bool show_info, bool is_paused, Vector2 mouse_pos, bool is_mouse_down) {
    int N = config.resolution;
    float grid_pixel_size = std::min(screen_width, screen_height) - 80.0f;
    float cell_size = grid_pixel_size / N;
    float offset_x = (screen_width - grid_pixel_size) / 2.0f;
    float offset_y = (screen_height - grid_pixel_size) / 2.0f;

    std::vector<float> display_probs(N * N, 0.0f);
    float max_p = 0.0001f;

    float expected_x = 0.0f, expected_x2 = 0.0f;
    float expected_y = 0.0f, expected_y2 = 0.0f;
    float sum_p = 0.0f;
    int center = N / 2;

    for (int i = 0; i < N * N; i++) {
        float current_p = std::norm(amp_N[i]) + std::norm(amp_S[i]) + std::norm(amp_E[i]) + std::norm(amp_W[i]);
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

    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            int idx = y * N + x;
            if (display_probs[idx] == 0.0f) continue; 

            float base_ratio = display_probs[idx] / max_p;
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

            // Red Historical Overlay Layer
            if (show_historical_overlay && historical_probs[idx] > 0.0f) {
                unsigned char alpha_val = (unsigned char)(std::pow(historical_probs[idx], 0.3f) * 200);
                Color overlayColor = { 255, 50, 0, alpha_val }; 
                
                DrawRectangle(
                    (int)(offset_x + x * cell_size), 
                    (int)(offset_y + y * cell_size), 
                    (int)std::ceil(cell_size), 
                    (int)std::ceil(cell_size), 
                    overlayColor
                );
            }
        }
    }

    if (config.boundary_condition == BoundaryType::REFLECTIVE) {
        DrawRectangleLinesEx({ offset_x - 4.0f, offset_y - 4.0f, grid_pixel_size + 8.0f, grid_pixel_size + 8.0f }, 4.0f, { 100, 200, 255, 200 }); 
    } else {
        DrawRectangleLinesEx({ offset_x - 1.0f, offset_y - 1.0f, grid_pixel_size + 2.0f, grid_pixel_size + 2.0f }, 1.0f, { 180, 50, 50, 150 });
    }

    if (show_info) {
        DrawRectangle(50, 50, 500, 300, { 20, 25, 30, 240 });
        DrawRectangleLines(50, 50, 500, 300, { 100, 150, 200, 255 });
        DrawText("2D DIAGNOSTICS [WIP]", 70, 70, 20, YELLOW);
        
        const char* modeStr = (config.mode == SimMode::QUANTUM) ? "QUANTUM ENGINE ACTIVE" : "CLASSICAL ENGINE ACTIVE";
        DrawText(modeStr, 70, 110, 15, WHITE);
        
        const char* boundStr = (config.boundary_condition == BoundaryType::REFLECTIVE) ? "Reflective Walls (Trapped)" : "Absorbing Walls (Void)";
        DrawText(TextFormat("Boundary System: %s", boundStr), 70, 140, 15, LIGHTGRAY);
    }
}

void Simulator2D::inject_dataset(const std::vector<ViralHotspot>& dataset) {
    int N = config.resolution;
    
    std::fill(amp_N.begin(), amp_N.end(), std::complex<float>(0.0f, 0.0f));
    std::fill(amp_S.begin(), amp_S.end(), std::complex<float>(0.0f, 0.0f));
    std::fill(amp_E.begin(), amp_E.end(), std::complex<float>(0.0f, 0.0f));
    std::fill(amp_W.begin(), amp_W.end(), std::complex<float>(0.0f, 0.0f));
    std::fill(prev_probs.begin(), prev_probs.end(), 0.0f);

    float min_lat = config.min_lat;
    float max_lat = config.max_lat;
    float min_lon = config.min_lon;
    float max_lon = config.max_lon;

    historical_dataset = dataset;
    max_historical_cases = 1; 
    float max_seed_cases = 1.0f; 

    for (const auto& point : dataset) {
        if (point.lat < min_lat || point.lat > max_lat || point.lon < min_lon || point.lon > max_lon) continue; 
        
        if (!point.cases_history.empty()) {
            if (point.cases_history[0] > max_seed_cases) {
                max_seed_cases = point.cases_history[0];
            }
        }

        for (int cases : point.cases_history) {
            if (cases > max_historical_cases) {
                max_historical_cases = cases;
            }
        }
    }

    historical_probs.assign(N * N, 0.0f);

    for (const auto& point : dataset) {
        if (point.lat < min_lat || point.lat > max_lat || point.lon < min_lon || point.lon > max_lon) continue; 

        float pct_x = (point.lon - min_lon) / (max_lon - min_lon);
        float pct_y = (max_lat - point.lat) / (max_lat - min_lat);

        int grid_x = static_cast<int>(pct_x * (N - 1));
        int grid_y = static_cast<int>(pct_y * (N - 1));

        if (grid_x >= 0 && grid_x < N && grid_y >= 0 && grid_y < N) {
            int idx = grid_y * N + grid_x;

            float physics_weight = 0.0f;
            float hist_weight = 0.0f;

            if (!point.cases_history.empty()) {
                // NORMALIZED AGAINST START DAY FOR PHYSICS
                physics_weight = static_cast<float>(point.cases_history[0]) / max_seed_cases;
                
                // REVERTED TO LINEAR SCALE FOR OVERLAY
                hist_weight = static_cast<float>(point.cases_history[0]) / static_cast<float>(max_historical_cases);
            }
            
            float magnitude = 0.5f * std::sqrt(physics_weight);

            amp_N[idx] = { magnitude, 0.0f}; amp_S[idx] = {-magnitude, 0.0f};
            amp_E[idx] = { magnitude, 0.0f}; amp_W[idx] = {-magnitude, 0.0f};

            prev_probs[idx] = physics_weight; 
            historical_probs[idx] = std::min(historical_probs[idx] + hist_weight, 1.0f);
        }
    }
}

void Simulator2D::inject_landscape(const std::vector<GeoNode>& pop_data) {
    int N = config.resolution;
    float min_lat = config.min_lat;
    float max_lat = config.max_lat;
    float min_lon = config.min_lon;
    float max_lon = config.max_lon;

    std::vector<int> grid_population(N * N, 0);
    int max_pop = 0;

    for (const auto& point : pop_data) {
        if (point.lat < min_lat || point.lat > max_lat || point.lon < min_lon || point.lon > max_lon) continue; 

        float pct_x = (point.lon - min_lon) / (max_lon - min_lon);
        float pct_y = (max_lat - point.lat) / (max_lat - min_lat);

        int grid_x = static_cast<int>(pct_x * (N - 1));
        int grid_y = static_cast<int>(pct_y * (N - 1));

        if (grid_x >= 0 && grid_x < N && grid_y >= 0 && grid_y < N) {
            int idx = grid_y * N + grid_x;
            grid_population[idx] += point.population;
            
            if (grid_population[idx] > max_pop) max_pop = grid_population[idx];
        }
    }

    float log_max = std::log10(max_pop + 1.0f);

    for (int i = 0; i < N * N; i++) {
        if (grid_population[i] > 0) {
            float log_pop = std::log10(grid_population[i] + 1.0f);
            float pop_ratio = log_pop / log_max;
            node_growth_rate[i] = 0.95f + (0.20f * pop_ratio);
        } else {
            node_growth_rate[i] = 0.95f;
        }
    }
}