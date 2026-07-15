#include "Simulator2D.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include "DataParser.h"

Simulator2D::Simulator2D(SimConfig master_settings) {
    config = master_settings;
    int N = config.resolution;
    int total_nodes = N * N;
    
    amp_N.resize(total_nodes, {0.0f, 0.0f});
    amp_S.resize(total_nodes, {0.0f, 0.0f});
    amp_E.resize(total_nodes, {0.0f, 0.0f});
    amp_W.resize(total_nodes, {0.0f, 0.0f});
    amp_C.resize(total_nodes, {0.0f, 0.0f});
    
    next_amp_N.resize(total_nodes, {0.0f, 0.0f});
    next_amp_S.resize(total_nodes, {0.0f, 0.0f});
    next_amp_E.resize(total_nodes, {0.0f, 0.0f});
    next_amp_W.resize(total_nodes, {0.0f, 0.0f});
    next_amp_C.resize(total_nodes, {0.0f, 0.0f});

    prev_probs.resize(total_nodes, 0.0f);
    node_growth_rate.resize(total_nodes, 1.0f);

    applyInitialState();

    load_ascii_mask("/Users/willott/School/REU/Simulator/1D/data/nasa_pop.asc");
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
        prev_probs[i] = std::norm(amp_N[i]) + std::norm(amp_S[i]) + std::norm(amp_E[i]) + std::norm(amp_W[i]) + std::norm(amp_C[i]);
    }

    // --- PHASE 1: LOCAL MIXING ---
    if (config.mode == SimMode::QUANTUM) {
        const std::complex<float> I(0.0f, 1.0f); 

        for (int i = 0; i < total_nodes; i++) {
            std::complex<float> n = amp_N[i];
            std::complex<float> s = amp_S[i];
            std::complex<float> e = amp_E[i];
            std::complex<float> w = amp_W[i];
            std::complex<float> c = amp_C[i];

            std::complex<float> n_mix, s_mix, e_mix, w_mix;

            // 1. ALWAYS run the Unitary Coin Mixing first
            if (config.unitary_coin == UnitaryCoin2D::GROVER) {
                std::complex<float> half_sum = (n + s + e + w) * 0.5f;
                n_mix = half_sum - n; s_mix = half_sum - s;
                e_mix = half_sum - e; w_mix = half_sum - w;
            } 
            else if (config.unitary_coin == UnitaryCoin2D::DFT) {
                n_mix = 0.5f * (n + s + e + w);
                s_mix = 0.5f * (n + I * s - e - I * w);
                e_mix = 0.5f * (n - s + e - w);
                w_mix = 0.5f * (n - I * s - e + I * w);
            }
            else if (config.unitary_coin == UnitaryCoin2D::HADAMARD_TENSOR) {
                n_mix = 0.5f * (n + s + e + w);
                s_mix = 0.5f * (n - s + e - w);
                e_mix = 0.5f * (n + s - e - w);
                w_mix = 0.5f * (n - s - e + w);
            }
            else if (config.unitary_coin == UnitaryCoin2D::ALTERNATING_DFT) {
                bool is_even_step = (current_step % 2 == 0);
                if (is_even_step) {
                    n_mix = 0.5f * (n + s + e + w);
                    s_mix = 0.5f * (n + I * s - e - I * w);
                    e_mix = 0.5f * (n - s + e - w);
                    w_mix = 0.5f * (n - I * s - e + I * w);
                } else {
                    n_mix = 0.5f * (n + s + e + w);
                    s_mix = 0.5f * (n - I * s - e + I * w);
                    e_mix = 0.5f * (n - s + e - w);
                    w_mix = 0.5f * (n + I * s - e - I * w);
                }
            }

            // 2. ONLY apply the biological scalar AFTER mixing
            if (config.system_type_2d == SystemType::OPEN) {
                float local_R0 = node_growth_rate[i];
                
                // --- NEW: LOGISTIC DAMPENER (CARRYING CAPACITY) ---
                // Calculate current probability mass at this node
                float current_p = std::norm(n) + std::norm(s) + std::norm(e) + std::norm(w) + std::norm(c);
                
                // Scale normalized probability back to real-world simulated humans
                float simulated_humans = current_p * max_seed_cases; 
                float capacity = node_capacity[i];

                float effective_R0 = local_R0;
                if (capacity > 0.0f) {
                    float saturation = simulated_humans / capacity;
                    // As saturation approaches 1.0, growth decays
                    effective_R0 = local_R0 * std::max(0.0f, 1.0f - saturation);
                } else {
                    effective_R0 = 0.0f; // Dead zones cannot grow
                }
                
                if (config.nodal_retention) {
                    float m_amp = std::sqrt(config.mobility_rate);
                    float stay_amp = std::sqrt(1.0f - config.mobility_rate);
                    float split_amp = m_amp * 0.5f; 
                    
                    // --- CRITICAL: Use effective_R0 instead of local_R0 ---
                    std::complex<float> g_c = c * effective_R0;
                    std::complex<float> g_n = n_mix * effective_R0;
                    std::complex<float> g_s = s_mix * effective_R0;
                    std::complex<float> g_e = e_mix * effective_R0;
                    std::complex<float> g_w = w_mix * effective_R0;
                    
                    std::complex<float> unmixed_sum = (n + s + e + w) * effective_R0;
                    amp_C[i] = (g_c * stay_amp) + (unmixed_sum * 0.5f * stay_amp);
                    
                    // --- NEW FIX: The City Leak matches your UI selection! ---
                    std::complex<float> leak_N = {0.0f, 0.0f};
                    std::complex<float> leak_S = {0.0f, 0.0f};
                    std::complex<float> leak_E = {0.0f, 0.0f};
                    std::complex<float> leak_W = {0.0f, 0.0f};

                    switch(config.init_state_2d) {
                        case InitialState2D::PURE_NORTH:
                            leak_N = g_c * m_amp; // All leak goes North
                            break;
                        case InitialState2D::UNIFORM:
                            leak_N = g_c * split_amp; leak_S = g_c * split_amp;
                            leak_E = g_c * split_amp; leak_W = g_c * split_amp;
                            break;
                        case InitialState2D::ALTERNATING_PHASE:
                            leak_N = g_c * split_amp; leak_S = -g_c * split_amp;
                            leak_E = g_c * split_amp; leak_W = -g_c * split_amp;
                            break;
                        case InitialState2D::CHIRAL_WEST:
                            leak_N = g_c * split_amp;       leak_S = g_c * split_amp * I;
                            leak_E = -g_c * split_amp;      leak_W = -g_c * split_amp * I;
                            break;
                        case InitialState2D::HADAMARD_SYMMETRIC:
                            leak_N = g_c * split_amp;       leak_S = g_c * split_amp * I;
                            leak_E = g_c * split_amp * I;   leak_W = -g_c * split_amp;
                            break;
                    }

                    // Send out the travelers, leak the correctly shaped residents
                    amp_N[i] = (g_n * m_amp) + leak_N;
                    amp_S[i] = (g_s * m_amp) + leak_S;
                    amp_E[i] = (g_e * m_amp) + leak_E;
                    amp_W[i] = (g_w * m_amp) + leak_W;
                } else {
                    // --- CRITICAL: Use effective_R0 here too! ---
                    amp_N[i] = n_mix * effective_R0;
                    amp_S[i] = s_mix * effective_R0;
                    amp_E[i] = e_mix * effective_R0;
                    amp_W[i] = w_mix * effective_R0;
                    amp_C[i] = {0.0f, 0.0f}; // Clear center if toggle is off
                }
            } else {
                // FIXED: Sandbox mode passes the waves cleanly
                amp_N[i] = n_mix;
                amp_S[i] = s_mix;
                amp_E[i] = e_mix;
                amp_W[i] = w_mix;
            }
        }
    }

    else if (config.mode == SimMode::CLASSICAL) {
        for (int i = 0; i < total_nodes; i++) {
            // 1. Sum total probability at this node (including the resident pool)
            float current_p = std::norm(amp_N[i]) + std::norm(amp_S[i]) + std::norm(amp_E[i]) + std::norm(amp_W[i]) + std::norm(amp_C[i]);
            
            float effective_R0 = 1.0f;

            // 2. Apply Logistic Dampener if in OPEN system
            if (config.system_type_2d == SystemType::OPEN) {
                float local_R0 = node_growth_rate[i];
                float simulated_humans = current_p * max_seed_cases;
                float capacity = node_capacity[i];

                if (capacity > 0.0f) {
                    float saturation = simulated_humans / capacity;
                    effective_R0 = local_R0 * std::max(0.0f, 1.0f - saturation);
                } else {
                    effective_R0 = 0.0f; // Dead zones
                }
            }

            // Apply growth/decay scalar
            float next_total_p = current_p * effective_R0;

            // 3. Handle Nodal Retention vs Full Diffusion
            if (config.nodal_retention) {
                float stay_p = next_total_p * (1.0f - config.mobility_rate);
                float move_p = next_total_p * config.mobility_rate;
                float dir_p = move_p * 0.25f; // Split moving population 4 ways

                amp_C[i] = { std::sqrt(stay_p), 0.0f };
                amp_N[i] = { std::sqrt(dir_p), 0.0f };
                amp_S[i] = { std::sqrt(dir_p), 0.0f };
                amp_E[i] = { std::sqrt(dir_p), 0.0f };
                amp_W[i] = { std::sqrt(dir_p), 0.0f };
            } else {
                float dir_p = next_total_p * 0.25f; // Split all population 4 ways

                amp_C[i] = { 0.0f, 0.0f };
                amp_N[i] = { std::sqrt(dir_p), 0.0f };
                amp_S[i] = { std::sqrt(dir_p), 0.0f };
                amp_E[i] = { std::sqrt(dir_p), 0.0f };
                amp_W[i] = { std::sqrt(dir_p), 0.0f };
            }
        }
    }

    // --- PHASE 2: THE 2D SHIFT OPERATOR (Shared by Quantum & Classical) ---
    std::fill(next_amp_N.begin(), next_amp_N.end(), std::complex<float>(0.0f, 0.0f));
    std::fill(next_amp_S.begin(), next_amp_S.end(), std::complex<float>(0.0f, 0.0f));
    std::fill(next_amp_E.begin(), next_amp_E.end(), std::complex<float>(0.0f, 0.0f));
    std::fill(next_amp_W.begin(), next_amp_W.end(), std::complex<float>(0.0f, 0.0f));
    std::fill(next_amp_C.begin(), next_amp_C.end(), std::complex<float>(0.0f, 0.0f));

    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            int current_idx = y * N + x;

            next_amp_C[current_idx] = amp_C[current_idx]; // <--- Resident wave stays

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
    amp_C = next_amp_C;

    // --- NEW: APPLY GEOGRAPHIC ABSORBING MASK ---
    if (config.system_type_2d == SystemType::OPEN) {
        for (int i = 0; i < N * N; i++) {
            amp_N[i] *= land_mask[i];
            amp_S[i] *= land_mask[i];
            amp_E[i] *= land_mask[i];
            amp_W[i] *= land_mask[i];
            amp_C[i] *= land_mask[i];
        }
    }

   // --- PHASE 3: UPDATE HISTORICAL OVERLAY ---
    if (show_historical_overlay || track_masked_mse || track_emd) {
        std::fill(historical_probs.begin(), historical_probs.end(), 0.0f);

        for (const auto& point : historical_dataset) {
            if (point.lat < config.min_lat || point.lat > config.max_lat || point.lon < config.min_lon || point.lon > config.max_lon) continue;

            float pct_x = (point.lon - config.min_lon) / (config.max_lon - config.min_lon);
            float pct_y = (config.max_lat - point.lat) / (config.max_lat - config.min_lat);

            int grid_x = static_cast<int>(pct_x * (N - 1));
            int grid_y = static_cast<int>(pct_y * (N - 1));

            if (grid_x >= 0 && grid_x < N && grid_y >= 0 && grid_y < N) {
                int day_idx = current_step / config.quantum_ticks_per_real_tick;
                if (day_idx >= point.cases_history.size()) day_idx = point.cases_history.size() - 1;

                float hist_weight = static_cast<float>(point.cases_history[day_idx]) / static_cast<float>(max_historical_cases);
                
                int idx = grid_y * N + grid_x;
                float current_val = historical_probs[idx] + hist_weight;
                historical_probs[idx] = std::min(current_val, 1.0f); 
            }
        }
    }

    // --- PHASE 4: CALCULATE ADVANCED TELEMETRY ---
    if (show_historical_overlay || track_masked_mse || track_emd) {
        
        // 1. Masked MSE
        if (track_masked_mse) {
            float mse_sum = 0.0f;
            int mask_count = 0;
            for (int i = 0; i < total_nodes; i++) {
                if (historical_probs[i] > 0.0f) { // Only check nodes with historical data
                    float sim_p = std::norm(amp_N[i]) + std::norm(amp_S[i]) + std::norm(amp_E[i]) + std::norm(amp_W[i]) + std::norm(amp_C[i]);
                    
                    // --- FIX: Scale up to REAL case numbers ---
                    float sim_cases = sim_p * max_seed_cases;
                    float real_cases = historical_probs[i] * max_historical_cases;
                    
                    float diff = sim_cases - real_cases;
                    mse_sum += (diff * diff);
                    mask_count++;
                }
            }
            if (mask_count > 0) masked_mse_history.push_back(mse_sum / mask_count);
            else masked_mse_history.push_back(0.0f);
        }

        // 2. Marginal Sliced Earth Mover's Distance (O(N) Approximation)
        if (track_emd) {
            std::vector<float> sim_x(N, 0.0f), sim_y(N, 0.0f);
            std::vector<float> hist_x(N, 0.0f), hist_y(N, 0.0f);
            float total_sim = 0.0f, total_hist = 0.0f;

            for (int y = 0; y < N; y++) {
                for (int x = 0; x < N; x++) {
                    int idx = y * N + x;
                    float sim_val = (std::norm(amp_N[idx]) + std::norm(amp_S[idx]) + std::norm(amp_E[idx]) + std::norm(amp_W[idx]) + std::norm(amp_C[idx])) * max_seed_cases / max_historical_cases;
                    float hist_val = historical_probs[idx];
                    
                    sim_x[x] += sim_val; sim_y[y] += sim_val;
                    hist_x[x] += hist_val; hist_y[y] += hist_val;
                    total_sim += sim_val; total_hist += hist_val;
                }
            }

            float emd_total = 0.0f;
            float cdf_sim_x = 0.0f, cdf_hist_x = 0.0f;
            float cdf_sim_y = 0.0f, cdf_hist_y = 0.0f;
            
            for (int i = 0; i < N; i++) {
                cdf_sim_x += sim_x[i] / (total_sim > 0 ? total_sim : 1.0f);
                cdf_hist_x += hist_x[i] / (total_hist > 0 ? total_hist : 1.0f);
                emd_total += std::abs(cdf_sim_x - cdf_hist_x);

                cdf_sim_y += sim_y[i] / (total_sim > 0 ? total_sim : 1.0f);
                cdf_hist_y += hist_y[i] / (total_hist > 0 ? total_hist : 1.0f);
                emd_total += std::abs(cdf_sim_y - cdf_hist_y);
            }
            emd_history.push_back(emd_total);
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
        float current_p = std::norm(amp_N[i]) + std::norm(amp_S[i]) + std::norm(amp_E[i]) + std::norm(amp_W[i]) + std::norm(amp_C[i]);
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

    // DRAW TELEMETRY OVERLAYS
    int overlay_width = 300;
    int overlay_height = 120;
    int overlay_x = screen_width - overlay_width - 20; 
    int current_y = 20; 

    auto draw_graph = [&](const char* title, const std::vector<float>& history, Color col) {
        if (history.empty()) return;
        DrawRectangle(overlay_x, current_y, overlay_width, overlay_height, { 30, 30, 30, 220 });
        DrawRectangleLines(overlay_x, current_y, overlay_width, overlay_height, col);
        DrawText(title, overlay_x + 10, current_y + 10, 10, { 200, 200, 200, 255 });
        
        float current_val = history.back();
        const char* val_text = TextFormat("%.6f", current_val);
        DrawText(val_text, overlay_x + overlay_width - MeasureText(val_text, 10) - 10, current_y + 10, 10, col);

        if (history.size() > 1) {
            float max_val = 0.0001f;
            for (float v : history) if (v > max_val) max_val = v;

            float graph_y_start = current_y + overlay_height;
            float graph_h = overlay_height - 30; 
            float x_step = (float)overlay_width / history.size();

            for (size_t i = 1; i < history.size(); i++) {
                Vector2 p1 = { overlay_x + (i - 1) * x_step, graph_y_start - (history[i - 1] / max_val * graph_h) };
                Vector2 p2 = { overlay_x + i * x_step, graph_y_start - (history[i] / max_val * graph_h) };
                DrawLineEx(p1, p2, 2.0f, col); 
            }
        }
        current_y += overlay_height + 10; // Shift down for the next graph
    };

    if (track_masked_mse) draw_graph("Masked MSE (Hotspots Only)", masked_mse_history, { 255, 100, 100, 255 });
    if (track_emd) draw_graph("Earth Mover's Distance (Marginal Proxy)", emd_history, { 100, 255, 100, 255 });

    // DRAW THE METRICS MENU
    if (show_metrics_menu) {
        int menu_w = 450;
        int menu_h = 240; // Increased height to fit 3 options
        int menu_x = (screen_width - menu_w) / 2;
        int menu_y = (screen_height - menu_h) / 2;

        DrawRectangle(0, 0, screen_width, screen_height, { 10, 10, 10, 150 }); // Darken background
        DrawRectangle(menu_x, menu_y, menu_w, menu_h, { 25, 30, 40, 255 });
        DrawRectangleLines(menu_x, menu_y, menu_w, menu_h, { 100, 150, 200, 255 });

        DrawText("TELEMETRY TRACKERS", menu_x + menu_w/2 - MeasureText("TELEMETRY TRACKERS", 20)/2, menu_y + 20, 20, WHITE);
        
        Color col1 = track_masked_mse ? Color{ 0, 200, 255, 255 } : GRAY;
        DrawText("[1] Masked MSE", menu_x + 80, menu_y + 70, 20, col1);

        Color col2 = track_emd ? Color{ 0, 200, 255, 255 } : GRAY;
        DrawText("[2] Earth Mover's Distance", menu_x + 80, menu_y + 110, 20, col2);

        Color col3 = show_historical_overlay ? Color{ 0, 200, 255, 255 } : GRAY;
        DrawText("[3] Historical Overlay (Red Map)", menu_x + 80, menu_y + 150, 20, col3);

        DrawText("Press Z to close and resume", menu_x + menu_w/2 - MeasureText("Press Z to close and resume", 15)/2, menu_y + 200, 15, LIGHTGRAY);
    }
    // DRAW THE ENGINE MODE MENU
    if (show_mode_menu) {
        int menu_w = 450;
        int menu_h = 200; 
        int menu_x = (screen_width - menu_w) / 2;
        int menu_y = (screen_height - menu_h) / 2;

        DrawRectangle(0, 0, screen_width, screen_height, { 10, 10, 10, 150 }); 
        DrawRectangle(menu_x, menu_y, menu_w, menu_h, { 40, 25, 30, 255 }); // Slight red tint
        DrawRectangleLines(menu_x, menu_y, menu_w, menu_h, { 200, 100, 100, 255 });

        DrawText("COMPUTE ENGINE SELECTION", menu_x + menu_w/2 - MeasureText("COMPUTE ENGINE SELECTION", 20)/2, menu_y + 20, 20, WHITE);
        
        Color col1 = (config.mode == SimMode::QUANTUM) ? Color{ 255, 100, 100, 255 } : GRAY;
        DrawText("[1] Quantum Walk Virtual Machine", menu_x + 50, menu_y + 80, 20, col1);

        Color col2 = (config.mode == SimMode::CLASSICAL) ? Color{ 255, 100, 100, 255 } : GRAY;
        DrawText("[2] Classical Diffusion Engine", menu_x + 50, menu_y + 120, 20, col2);

        DrawText("Press M to close and resume", menu_x + menu_w/2 - MeasureText("Press M to close and resume", 15)/2, menu_y + 170, 15, LIGHTGRAY);
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
    max_seed_cases = 1.0f; 

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
                physics_weight = static_cast<float>(point.cases_history[0]) / max_seed_cases;
                hist_weight = static_cast<float>(point.cases_history[0]) / static_cast<float>(max_historical_cases);
            }
            
            float magnitude = 0.5f * std::sqrt(physics_weight);

            if (config.nodal_retention) {
                amp_C[idx] += std::complex<float>(std::sqrt(physics_weight), 0.0f);
            } else {
                // --- NEW FIX: Day 0 matches the UI UI toggle! ---
                const std::complex<float> I(0.0f, 1.0f);
                
                switch(config.init_state_2d) {
                    case InitialState2D::PURE_NORTH:
                        amp_N[idx] += std::complex<float>(magnitude * 2.0f, 0.0f);
                        break;
                    case InitialState2D::UNIFORM:
                        amp_N[idx] += std::complex<float>(magnitude, 0.0f); 
                        amp_S[idx] += std::complex<float>(magnitude, 0.0f);
                        amp_E[idx] += std::complex<float>(magnitude, 0.0f); 
                        amp_W[idx] += std::complex<float>(magnitude, 0.0f);
                        break;
                    case InitialState2D::ALTERNATING_PHASE:
                        amp_N[idx] += std::complex<float>(magnitude, 0.0f); 
                        amp_S[idx] += std::complex<float>(-magnitude, 0.0f);
                        amp_E[idx] += std::complex<float>(magnitude, 0.0f); 
                        amp_W[idx] += std::complex<float>(-magnitude, 0.0f);
                        break;
                    case InitialState2D::CHIRAL_WEST:
                        amp_N[idx] += std::complex<float>(magnitude, 0.0f); 
                        amp_S[idx] += std::complex<float>(0.0f, magnitude);
                        amp_E[idx] += std::complex<float>(-magnitude, 0.0f); 
                        amp_W[idx] += std::complex<float>(0.0f, -magnitude);
                        break;
                    case InitialState2D::HADAMARD_SYMMETRIC:
                        amp_N[idx] += std::complex<float>(magnitude, 0.0f);  
                        amp_S[idx] += std::complex<float>(0.0f, magnitude);
                        amp_E[idx] += std::complex<float>(0.0f, magnitude); 
                        amp_W[idx] += std::complex<float>(-magnitude, 0.0f);
                        break;
                }
            }

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
            node_growth_rate[i] = config.base_survival_rate + (config.urban_multiplier * pop_ratio);
        } else {
            node_growth_rate[i] = config.base_survival_rate;
        }
    }
}

void Simulator2D::load_ascii_mask(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cout << "\n[!] ERROR: Could not find '" << filepath << "'! Defaulting to Waterless Earth.\n\n";
        land_mask.assign(config.resolution * config.resolution, 1.0f);
        node_growth_rate.assign(config.resolution * config.resolution, config.base_survival_rate);
        return;
    }

    std::cout << "\n[+] SUCCESS: NASA Population Map found and loading...\n\n";

    std::string label;
    int ncols, nrows;
    float xllcorner, yllcorner, cellsize, nodata;

    file >> label >> ncols;
    file >> label >> nrows;
    file >> label >> xllcorner;
    file >> label >> yllcorner;
    file >> label >> cellsize;
    file >> label >> nodata;

    std::vector<std::vector<float>> raw_grid(nrows, std::vector<float>(ncols));
    float max_pop = 0.0f; // Track the highest population in the world

    for (int r = 0; r < nrows; r++) {
        for (int c = 0; c < ncols; c++) {
            file >> raw_grid[r][c];
            if (raw_grid[r][c] != nodata && raw_grid[r][c] > max_pop) {
                max_pop = raw_grid[r][c];
            }
        }
    }
    file.close();

    int N = config.resolution;
    land_mask.assign(N * N, 0.0f);
    node_growth_rate.assign(N * N, config.base_survival_rate); 
    node_capacity.assign(N * N, 0.0f);
    
    float max_lat_asc = yllcorner + (nrows * cellsize); 
    float log_max = std::log10(max_pop + 1.0f); 

    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            int idx = y * N + x;
            
            float lon = config.min_lon + (x / (float)(N - 1)) * (config.max_lon - config.min_lon);
            float lat = config.max_lat - (y / (float)(N - 1)) * (config.max_lat - config.min_lat);

            int asc_c = (lon - xllcorner) / cellsize;
            int asc_r = (max_lat_asc - lat) / cellsize; 

            if (asc_r >= 0 && asc_r < nrows && asc_c >= 0 && asc_c < ncols) {
                float cell_value = raw_grid[asc_r][asc_c];
                
                if (cell_value < 0.0f || cell_value == nodata) {
                    land_mask[idx] = 0.0f;
                    node_growth_rate[idx] = 0.0f;
                    node_capacity[idx] = 0.0f; 
                } else {
                    land_mask[idx] = 1.0f;
                    node_capacity[idx] = cell_value;

                    float log_pop = std::log10(cell_value + 1.0f);
                    float pop_ratio = log_pop / log_max;
                    
                    node_growth_rate[idx] = config.base_survival_rate + (config.urban_multiplier * pop_ratio);
                }
            } else {
                land_mask[idx] = 0.0f; 
            }
        }
    }
}