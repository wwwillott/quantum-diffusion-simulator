#include "QuantumWalk1D.h"
#include <cmath>

QuantumWalk1D::QuantumWalk1D(SimConfig master_settings) {
    config = master_settings;
    
    alpha.resize(config.resolution, 0.0f);
    beta.resize(config.resolution, 0.0f);
    next_alpha.resize(config.resolution, 0.0f);
    next_beta.resize(config.resolution, 0.0f);

    applyInitialState();
}

void QuantumWalk1D::applyInitialState() {
    int center = config.resolution / 2;
    float inv_sqrt2 = 1.0f / std::sqrt(2.0f);

    switch (config.initial_coin) {
        case CoinState::RIGHT_HEAVY:
            alpha[center] = {1.0f, 0.0f}; // 100% |0>
            break;
        case CoinState::LEFT_HEAVY:
            beta[center] = {1.0f, 0.0f};  // 100% |1>
            break;
        case CoinState::SYMMETRIC:
            // To get a perfectly symmetric walk, the |1> state needs an imaginary phase
            alpha[center] = {inv_sqrt2, 0.0f};
            beta[center] = {0.0f, inv_sqrt2}; 
            break;
    }
}

void QuantumWalk1D::update() {
    float inv_sqrt2 = 1.0f / std::sqrt(2.0f);

    // --- PHASE 1: THE COIN TOSS (Hadamard + Barrier) ---
    for (int i = 0; i < config.resolution; i++) {
        std::complex<float> a = alpha[i];
        std::complex<float> b = beta[i];

        // Apply the Hadamard Matrix
        std::complex<float> new_a = (a + b) * inv_sqrt2;
        std::complex<float> new_b = (a - b) * inv_sqrt2;

        // Loop through all active barriers
            for (int k = 0; k < config.num_barriers; k++) {
                if (i == config.barrier_positions[k]) {
                    new_a *= -1.0f; 
                    new_b *= -1.0f;
                    break; // Only apply the flip once per coordinate
                }
            }

        alpha[i] = new_a;
        beta[i] = new_b;
    }

    // --- PHASE 2: THE SHIFT OPERATOR ---
    // Clear the next buffers to prevent artifacts
    std::fill(next_alpha.begin(), next_alpha.end(), 0.0f);
    std::fill(next_beta.begin(), next_beta.end(), 0.0f);

    for (int i = 1; i < config.resolution - 1; i++) {
        // Right-moving amplitudes shift to i+1
        next_alpha[i + 1] = alpha[i];
        // Left-moving amplitudes shift to i-1
        next_beta[i - 1] = beta[i];
    }
    
    // Ping-Pong the buffers
    alpha = next_alpha;
    beta = next_beta;

// --- PHASE 3: CALCULATE STANDARD DEVIATION ---
    float mean = 0.0f;
    int center = config.resolution / 2;

    // Step 1: Calculate the Mean (Expected Position)
    for (int i = 0; i < config.resolution; i++) {
        float p = std::norm(alpha[i]) + std::norm(beta[i]);
        float x = (float)(i - center); // Physical position relative to origin
        mean += x * p;
    }

    // Step 2: Calculate the Variance and Standard Deviation
    float variance = 0.0f;
    for (int i = 0; i < config.resolution; i++) {
        float p = std::norm(alpha[i]) + std::norm(beta[i]);
        float x = (float)(i - center);
        variance += (x - mean) * (x - mean) * p;
    }

    // Save it to history
    std_dev_history.push_back(std::sqrt(variance));
}

void QuantumWalk1D::draw(int screen_width, int screen_height) {
    float bar_width = (float)screen_width / config.resolution;
    int bottom_margin = 40; 
    int graph_height = screen_height - bottom_margin;

    // Measurement: Find the maximum probability to auto-scale the graph
    float max_p = 0.001f;
    std::vector<float> probabilities(config.resolution, 0.0f);
    
    for (int i = 0; i < config.resolution; i++) {
        // The Born Rule: P(x) = |alpha|^2 + |beta|^2
        probabilities[i] = std::norm(alpha[i]) + std::norm(beta[i]);
        if (probabilities[i] > max_p) max_p = probabilities[i];
    }

    // Draw the Histogram
    for (int i = 0; i < config.resolution; i++) {
        // Normalize the height so the spikes are always visible
        float normalized_height = probabilities[i] / max_p;
        float bar_height = normalized_height * (graph_height - 20); 

        // Apply a phase barrier visualizer if active
        Color bar_color = { 40, 40, 40, 255 }; 
        if (config.apply_barrier && i == config.barrier_position) {
            bar_color = { 180, 50, 50, 255 }; // Highlight the barrier in dark red
        }

        DrawRectangle(
            (int)(i * bar_width), 
            (int)(graph_height - bar_height), 
            (int)bar_width < 1 ? 1 : (int)bar_width, 
            (int)bar_height, 
            bar_color
        );
    }

    DrawLine(0, graph_height, screen_width, graph_height, { 20, 20, 20, 255 });

    int center = config.resolution / 2;
    int tick_spacing = config.resolution / 10; 
    if (tick_spacing == 0) tick_spacing = 1;

    for (int i = 0; i <= config.resolution; i += tick_spacing) {
        int x_pos = (int)(i * bar_width);
        int position_value = i - center; 
        DrawLine(x_pos, graph_height, x_pos, graph_height + 10, { 20, 20, 20, 255 });
        const char* label = TextFormat("%d", position_value);
        int text_width = MeasureText(label, 20);
        DrawText(label, x_pos - (text_width / 2), graph_height + 15, 20, { 80, 80, 80, 255 });
    }

    // --- DRAW TELEMETRY OVERLAY ---
    int overlay_width = 250;
    int overlay_height = 120;
    int overlay_x = screen_width - overlay_width - 20; // 20px padding from right
    int overlay_y = 20; // 20px padding from top

    // Draw the background panel
    DrawRectangle(overlay_x, overlay_y, overlay_width, overlay_height, { 30, 30, 30, 220 });
    DrawRectangleLines(overlay_x, overlay_y, overlay_width, overlay_height, { 100, 100, 100, 255 });
    
    // Title and current value
    DrawText("Standard Deviation (\u03C3)", overlay_x + 10, overlay_y + 10, 10, { 200, 200, 200, 255 });
    
    if (!std_dev_history.empty()) {
        float current_sigma = std_dev_history.back();
        const char* val_text = TextFormat("%.2f", current_sigma);
        DrawText(val_text, overlay_x + overlay_width - MeasureText(val_text, 10) - 10, overlay_y + 10, 10, { 100, 255, 100, 255 });

        // Draw the line graph
        if (std_dev_history.size() > 1) {
            // Find max value to scale the graph dynamically
            float max_val = 1.0f;
            for (float v : std_dev_history) {
                if (v > max_val) max_val = v;
            }

            // Calculate horizontal step size based on history length
            float graph_y_start = overlay_y + overlay_height;
            float graph_h = overlay_height - 30; // Leave room for title
            float x_step = (float)overlay_width / std_dev_history.size();

            for (size_t i = 1; i < std_dev_history.size(); i++) {
                float v1 = std_dev_history[i - 1];
                float v2 = std_dev_history[i];

                Vector2 p1 = { 
                    overlay_x + (i - 1) * x_step, 
                    graph_y_start - (v1 / max_val * graph_h) 
                };
                Vector2 p2 = { 
                    overlay_x + i * x_step, 
                    graph_y_start - (v2 / max_val * graph_h) 
                };

                // Draw anti-aliased green line
                DrawLineEx(p1, p2, 2.0f, { 100, 220, 120, 255 }); 
            }
        }
    }
}