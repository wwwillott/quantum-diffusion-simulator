#pragma once
#include <vector>
#include <complex>
#include "raylib.h"

// Define the initial coin state for the walker
enum class CoinState {
    RIGHT_HEAVY, // |0>
    LEFT_HEAVY,  // |1>
    SYMMETRIC    // (|0> + i|1>) / sqrt(2)
};

struct SimConfig {
    int resolution;
    CoinState initial_coin;
    
    // Algorithmic Parameters
    bool apply_barrier;
    int barrier_position; 
};

class QuantumWalk1D {
private:
    // The Tensor State: Every position holds a right and left amplitude
    std::vector<std::complex<float>> alpha; // |0> Right-moving
    std::vector<std::complex<float>> beta;  // |1> Left-moving
    
    std::vector<std::complex<float>> next_alpha;
    std::vector<std::complex<float>> next_beta;

    // Track standard deviation
    std::vector<float> std_dev_history;
    
    SimConfig config;

    void applyInitialState();

public:
    QuantumWalk1D(SimConfig master_settings);
    void update();
    void draw(int screen_width, int screen_height);
};