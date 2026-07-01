#include "Simulator1D.h"
#include <cmath>

Simulator1D::Simulator1D(SimConfig master_settings) {
    config = master_settings;
    int res = config.resolution;
    
    // Allocate all memory
    classical_p.resize(res, 0.0f);
    next_classical_p.resize(res, 0.0f);
    alpha.resize(res, 0.0f);
    beta.resize(res, 0.0f);
    next_alpha.resize(res, 0.0f);
    next_beta.resize(res, 0.0f);

    applyInitialState();
}

void Simulator1D::applyInitialState() {
    int start_idx = config.wave_start_pos; 

    if (config.mode == SimMode::CLASSICAL) {
        classical_p[start_idx] = 1.0f; 
    } 
    else if (config.mode == SimMode::QUANTUM_SEARCH) {
        // Create a flat, global uniform superposition
        float flat_amp = 1.0f / std::sqrt(2.0f * config.resolution);
        for (int i = 0; i < config.resolution; i++) {
            alpha[i] = {flat_amp, 0.0f};
            beta[i] = {flat_amp, 0.0f};
        }
    }
    else {
        // Standard Quantum Point-Source
        float inv_sqrt2 = 1.0f / std::sqrt(2.0f);
        if (config.initial_coin == CoinState::SYMMETRIC) {
            alpha[start_idx] = {inv_sqrt2, 0.0f};
            beta[start_idx] = {0.0f, inv_sqrt2}; 
        } else {
            alpha[start_idx] = {1.0f, 0.0f}; 
        }
    }
}

void Simulator1D::update() {
    // --- PHYSICS ENGINE ---
    if (config.mode == SimMode::CLASSICAL) {
        float current_sum = 0.0f;
        for (float p : classical_p) current_sum += p;
        
        if (current_sum < 0.0001f) {
            for (int i = 0; i < config.resolution; i++) {
                classical_p[i] = std::norm(alpha[i]) + std::norm(beta[i]);
            }
        }

        for (int i = 1; i < config.resolution - 1; i++) {
            float p = classical_p[i];
            float p_left = classical_p[i - 1];
            float p_right = classical_p[i + 1];
            next_classical_p[i] = p + config.diffusion_rate * (p_right - 2.0f * p + p_left);
        }
        classical_p = next_classical_p;
    } 
    else if (config.mode == SimMode::QUANTUM || config.mode == SimMode::QUANTUM_SEARCH) {
        float inv_sqrt2 = 1.0f / std::sqrt(2.0f);
        
        // 1. Coin Toss (with Oracle Integration)
        for (int i = 0; i < config.resolution; i++) {
            std::complex<float> a = alpha[i];
            std::complex<float> b = beta[i];

            // THE SEARCH ORACLE: Flip phase at the target BEFORE the coin mixes it
            if (config.mode == SimMode::QUANTUM_SEARCH && i == config.search_target) {
                a *= -1.0f;
                b *= -1.0f;
            }

            // Apply Hadamard
            std::complex<float> new_a = (a + b) * inv_sqrt2;
            std::complex<float> new_b = (a - b) * inv_sqrt2;

            // THE BARRIERS: Phase inversion AFTER the mix (for the Firebreak demo)
            if (config.mode == SimMode::QUANTUM) {
                for (int k = 0; k < config.num_barriers; k++) {
                    if (i == config.barrier_positions[k]) {
                        new_a *= -1.0f; 
                        new_b *= -1.0f;
                        break; 
                    }
                }
            }

            alpha[i] = new_a; 
            beta[i] = new_b;
        }

    

        // 2. Shift
        std::fill(next_alpha.begin(), next_alpha.end(), 0.0f);
        std::fill(next_beta.begin(), next_beta.end(), 0.0f);
        for (int i = 1; i < config.resolution - 1; i++) {
            next_alpha[i + 1] = alpha[i];
            next_beta[i - 1] = beta[i];
        }
        alpha = next_alpha; beta = next_beta;
    }

    // --- TELEMETRY CALCULATIONS ---
    float mean = 0.0f;
    int center = config.resolution / 2;

    for (int i = 0; i < config.resolution; i++) {
        float p = (config.mode == SimMode::CLASSICAL) ? 
                  classical_p[i] : (std::norm(alpha[i]) + std::norm(beta[i]));
        float x = (float)(i - center);
        mean += x * p;
    }

    float variance = 0.0f;
    for (int i = 0; i < config.resolution; i++) {
        float p = (config.mode == SimMode::CLASSICAL) ? 
                  classical_p[i] : (std::norm(alpha[i]) + std::norm(beta[i]));
        float x = (float)(i - center);
        variance += (x - mean) * (x - mean) * p;
    }
    
    std_dev_history.push_back(std::sqrt(variance));
}

void Simulator1D::draw(int screen_width, int screen_height, bool show_info) {
    float bar_width = (float)screen_width / config.resolution;
    int graph_height = screen_height - 40;

    // Build a universal probability array for rendering
    std::vector<float> draw_p(config.resolution, 0.0f);
    float max_p = 0.005f;
    
    for (int i = 0; i < config.resolution; i++) {
        draw_p[i] = (config.mode == SimMode::CLASSICAL) ? classical_p[i] : (std::norm(alpha[i]) + std::norm(beta[i]));
        if (draw_p[i] > max_p) max_p = draw_p[i];
    }

    // ==========================================
    // 1. DRAW HISTOGRAM (Uniform wave color)
    // ==========================================
    for (int i = 0; i < config.resolution; i++) {
        float bar_height = (draw_p[i] / max_p) * (graph_height - 20); 
        
        DrawRectangle(
            (int)(i * bar_width), 
            (int)(graph_height - bar_height), 
            (int)bar_width < 1 ? 1 : (int)bar_width, 
            (int)bar_height, 
            { 40, 40, 40, 255 } // Solid grey for the entire wave
        );
    }

    // ==========================================
    // 2. DRAW SCENARIO OVERLAYS (Barrier & Target)
    // ==========================================
    
    // Draw multiple static red barrier lines
    if (config.mode == SimMode::QUANTUM) {
        for (int k = 0; k < config.num_barriers; k++) {
            int bp = config.barrier_positions[k];
            if (bp >= 0 && bp < config.resolution) {
                float barrier_x = (bp * bar_width) + (bar_width / 2.0f);
                DrawLineEx({ barrier_x, 0.0f }, { barrier_x, (float)graph_height }, 2.0f, { 180, 50, 50, 200 });
            }
        }
    }

    // Draw the green target dot
    if (config.target_position >= 0) {
        float target_x = (config.target_position * bar_width) + (bar_width / 2.0f);
        
        // Draw a small green circle right on the baseline
        DrawCircle(
            (int)target_x, 
            graph_height, 
            5.0f, // Radius
            { 50, 180, 50, 255 } // Dark green
        );
    }

    // Draw the blue search target dot
    if (config.mode == SimMode::QUANTUM_SEARCH && config.search_target >= 0) {
        float target_x = (config.search_target * bar_width) + (bar_width / 2.0f);
        DrawCircle((int)target_x, graph_height, 5.0f, { 50, 100, 200, 255 }); // Blue
    }

    // ==========================================
    // 3. DRAW AXIS & TICKS
    // ==========================================
    DrawLine(0, graph_height, screen_width, graph_height, { 20, 20, 20, 255 });
    int center = config.resolution / 2;
    int tick_spacing = config.resolution / 10; 
    if (tick_spacing == 0) tick_spacing = 1;

    for (int i = 0; i <= config.resolution; i += tick_spacing) {
        int x_pos = (int)(i * bar_width);
        DrawLine(x_pos, graph_height, x_pos, graph_height + 10, { 20, 20, 20, 255 });
        const char* label = TextFormat("%d", i - center);
        DrawText(label, x_pos - (MeasureText(label, 20) / 2), graph_height + 15, 20, { 80, 80, 80, 255 });
    }

    // Draw Telemetry Overlay (Same as before)
    int overlay_width = 250;
    int overlay_height = 120;
    int overlay_x = screen_width - overlay_width - 20; 
    int overlay_y = 20; 

    DrawRectangle(overlay_x, overlay_y, overlay_width, overlay_height, { 30, 30, 30, 220 });
    DrawRectangleLines(overlay_x, overlay_y, overlay_width, overlay_height, { 100, 100, 100, 255 });
    DrawText("Standard Deviation (\u03C3)", overlay_x + 10, overlay_y + 10, 10, { 200, 200, 200, 255 });
    
    if (!std_dev_history.empty()) {
        float current_sigma = std_dev_history.back();
        const char* val_text = TextFormat("%.2f", current_sigma);
        DrawText(val_text, overlay_x + overlay_width - MeasureText(val_text, 10) - 10, overlay_y + 10, 10, { 100, 255, 100, 255 });

        if (std_dev_history.size() > 1) {
            float max_val = 1.0f;
            for (float v : std_dev_history) { if (v > max_val) max_val = v; }
            float graph_y_start = overlay_y + overlay_height;
            float graph_h = overlay_height - 30; 
            float x_step = (float)overlay_width / std_dev_history.size();

            for (size_t i = 1; i < std_dev_history.size(); i++) {
                Vector2 p1 = { overlay_x + (i - 1) * x_step, graph_y_start - (std_dev_history[i - 1] / max_val * graph_h) };
                Vector2 p2 = { overlay_x + i * x_step, graph_y_start - (std_dev_history[i] / max_val * graph_h) };
                DrawLineEx(p1, p2, 2.0f, { 100, 220, 120, 255 }); 
            }
        }
    }

    if (show_info) {
        // 1. Draw the translucent background panel
        int pad = 50;
        DrawRectangle(pad, pad, screen_width - (pad * 2), screen_height - (pad * 2), { 20, 25, 30, 240 });
        DrawRectangleLines(pad, pad, screen_width - (pad * 2), screen_height - (pad * 2), { 100, 150, 200, 255 });
        
        int cursor_y = pad + 30;
        int col_1 = pad + 40;
        int col_2 = pad + 400; // For side-by-side matrices

        DrawText("SYSTEM STATE DIAGNOSTICS", col_1, cursor_y, 20, YELLOW);
        cursor_y += 40;

        if (config.mode == SimMode::CLASSICAL) {
            DrawText("MODE: Classical Random Walk", col_1, cursor_y, 20, WHITE);
            cursor_y += 40;
            DrawText("STATE EQUATION:", col_1, cursor_y, 20, LIGHTGRAY);
            DrawText("P(x, t+1) = P(x, t) + D * [ P(x+1, t) - 2P(x, t) + P(x-1, t) ]", col_1, cursor_y + 30, 20, GREEN);
        } 
        else {
            // --- QUANTUM INFORMATION OVERLAY ---
            
            // 1. Initial State
            DrawText("INITIAL COIN STATE:", col_1, cursor_y, 20, LIGHTGRAY);
            if (config.initial_coin == CoinState::RIGHT_HEAVY) {
                DrawText("|Psi_0> = |x_0> (x) [ 1 ]", col_1, cursor_y + 30, 20, WHITE);
                DrawText("                    [ 0 ]", col_1, cursor_y + 50, 20, WHITE);
            } else {
                DrawText("|Psi_0> = |x_0> (x) [ 1/sqrt(2) ]", col_1, cursor_y + 30, 20, WHITE);
                DrawText("                    [ i/sqrt(2) ]", col_1, cursor_y + 50, 20, WHITE);
            }

            // 2. The Coin Operator (Hadamard)
            DrawText("GLOBAL COIN OPERATOR ( I (x) H ):", col_2, cursor_y, 20, LIGHTGRAY);
            DrawText("    [  1   1 ]", col_2, cursor_y + 30, 20, WHITE);
            DrawText("H = [  1  -1 ] * 1/sqrt(2)", col_2, cursor_y + 50, 20, WHITE);
            
            cursor_y += 100;

            // 3. The Shift Operator
            DrawText("SHIFT OPERATOR (S):", col_1, cursor_y, 20, LIGHTGRAY);
            DrawText("S = sum( |x+1><x| (x) |0><0|  +  |x-1><x| (x) |1><1| )", col_1, cursor_y + 30, 20, WHITE);

            // 4. Scenario-Specific Operators (Oracles / Barriers)
            if (config.mode == SimMode::QUANTUM_SEARCH) {
                DrawText("SEARCH ORACLE (U_w):", col_2, cursor_y, 20, ORANGE);
                DrawText(TextFormat("I - 2|%d><%d| (x) I_c", config.search_target, config.search_target), col_2, cursor_y + 30, 20, WHITE);
            } 
            else if (config.num_barriers > 0) {
                DrawText("PHASE BARRIERS ACTIVE:", col_2, cursor_y, 20, RED);
                DrawText("Phase inversion (-1) applied at target coordinates.", col_2, cursor_y + 30, 15, WHITE);
            }
            
            cursor_y += 100;

            // 5. Global State Equation
            DrawText("GLOBAL WAVEFUNCTION:", col_1, cursor_y, 20, LIGHTGRAY);
            DrawText("|Psi_t> = sum_x ( alpha_x |x> (x) |0>  +  beta_x |x> (x) |1> )", col_1, cursor_y + 30, 20, GREEN);
            
            cursor_y += 60;
            DrawText("BORN RULE (Probability Density):", col_1, cursor_y, 20, LIGHTGRAY);
            DrawText("P(x) = |alpha_x|^2 + |beta_x|^2", col_1, cursor_y + 30, 20, GREEN);
        }
        
        // Footer
        DrawText("Press [I] to close diagnostics", screen_width / 2 - 120, screen_height - pad - 30, 15, DARKGRAY);
    }
}