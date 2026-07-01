#pragma once
#include <vector>
#include "raylib.h"

// Define the available starting configurations
enum class StartState {
    DELTA_SPIKE,
    SQUARE_BLOCK,
    GAUSSIAN_CURVE
};

// Define the Master Parameters container
struct SimConfig {
    int resolution;
    float diffusion_rate;
    StartState initial_state;
};

class Diffusion1D {
private:
    std::vector<float> probabilities;
    std::vector<float> next_probabilities;

    void applyInitialState();

public:
    SimConfig config;
    Diffusion1D(SimConfig master_settings);
    void update();
    void draw(int screen_width, int screen_height);
};