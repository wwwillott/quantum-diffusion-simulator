#include "Diffusion1D.h"
#include <cmath>

Diffusion1D::Diffusion1D(SimConfig master_settings) {
    config = master_settings;
    
    probabilities.resize(config.resolution, 0.0f);
    next_probabilities.resize(config.resolution, 0.0f);

    applyInitialState();
}

void Diffusion1D::applyInitialState() {
    int center = config.resolution / 2;

    switch (config.initial_state) {
        case StartState::DELTA_SPIKE:
            probabilities[center] = 1.0f;
            break;

        case StartState::SQUARE_BLOCK:
            {
                int block_width = config.resolution / 10;
                for (int i = center - block_width; i < center + block_width; i++) {
                    probabilities[i] = 1.0f;
                }
            }
            break;

        case StartState::GAUSSIAN_CURVE:
            {
                float spread = config.resolution / 20.0f;
                for (int i = 0; i < config.resolution; i++) {
                    float dist = (float)(i - center);
                    probabilities[i] = std::exp(-(dist * dist) / (2.0f * spread * spread));
                }
            }
            break;
    }
}

void Diffusion1D::update() {
    for (int i = 1; i < config.resolution - 1; i++) {
        float p = probabilities[i];
        float p_left = probabilities[i - 1];
        float p_right = probabilities[i + 1];

        next_probabilities[i] = p + config.diffusion_rate * (p_right - 2.0f * p + p_left);
    }
    
    probabilities = next_probabilities;
}

void Diffusion1D::draw(int screen_width, int screen_height) {
    float bar_width = (float)screen_width / config.resolution;
    
    // Reserve space at the bottom for the axis
    int bottom_margin = 40; 
    int graph_height = screen_height - bottom_margin;

    // 1. Draw the Histogram
    for (int i = 0; i < config.resolution; i++) {
        float p = probabilities[i];
        
        // Scale height relative to the new graph_height
        float bar_height = p * graph_height; 

        DrawRectangle(
            (int)(i * bar_width), 
            (int)(graph_height - bar_height), 
            (int)bar_width < 1 ? 1 : (int)bar_width, 
            (int)bar_height, 
            { 40, 40, 40, 255 } 
        );
    }

    // 2. Draw the Axis Baseline
    DrawLine(0, graph_height, screen_width, graph_height, { 20, 20, 20, 255 });

    // 3. Draw Tick Marks and Labels
    int center = config.resolution / 2;
    
    // Dynamically space ticks so we always get ~10 labels across the screen
    int tick_spacing = config.resolution / 10; 
    if (tick_spacing == 0) tick_spacing = 1;

    for (int i = 0; i <= config.resolution; i += tick_spacing) {
        int x_pos = (int)(i * bar_width);
        
        // Shift the index so the center of the screen represents x = 0
        int position_value = i - center; 

        // Draw the little tick mark dropping down from the axis
        DrawLine(x_pos, graph_height, x_pos, graph_height + 10, { 20, 20, 20, 255 });

        // Format the number into text
        const char* label = TextFormat("%d", position_value);
        
        // Measure text width to perfectly center it under the tick
        int text_width = MeasureText(label, 20);
        
        DrawText(
            label, 
            x_pos - (text_width / 2), 
            graph_height + 15, 
            20, 
            { 80, 80, 80, 255 }
        );
    }
}