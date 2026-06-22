#pragma once
#include <vector>
#include <complex>
#include "raylib.h"
#include "SimConfig.h"

class Simulator1D {
private:
    // Classical Memory
    std::vector<float> classical_p;
    std::vector<float> next_classical_p;

    // Quantum Memory
    std::vector<std::complex<float>> alpha;
    std::vector<std::complex<float>> beta;
    std::vector<std::complex<float>> next_alpha;
    std::vector<std::complex<float>> next_beta;
    
    std::vector<float> std_dev_history;
    SimConfig config;

    void applyInitialState();

public:
    Simulator1D(SimConfig master_settings);
    void update();
    void draw(int screen_width, int screen_height, bool show_info = false);
};