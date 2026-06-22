#pragma once
#include <vector>
#include <complex>
#include "raylib.h"
#include "SimConfig.h"
#include "DataParser.h"


class Simulator2D {
private:
    // Flattened 2D Memory: Size will be (resolution * resolution)
    // Indexing formula: index = (y * resolution) + x
    
    // Quantum Memory (Directional Amplitudes)
    std::vector<std::complex<float>> amp_N;
    std::vector<std::complex<float>> amp_S;
    std::vector<std::complex<float>> amp_E;
    std::vector<std::complex<float>> amp_W;

    // Shift Buffers
    std::vector<std::complex<float>> next_amp_N;
    std::vector<std::complex<float>> next_amp_S;
    std::vector<std::complex<float>> next_amp_E;
    std::vector<std::complex<float>> next_amp_W;

    std::vector<float> prev_probs;

    std::vector<float> std_dev_x_hist;
    std::vector<float> std_dev_y_hist;
    std::vector<float> std_dev_total_hist;
    
    SimConfig config;

    void applyInitialState();

public:
    Simulator2D(SimConfig master_settings);

public:
    void inject_dataset(const std::vector<ViralHotspot>& dataset);
    
    void update();
void draw(int screen_width, int screen_height, bool show_info = false, bool is_paused = false, Vector2 mouse_pos = {-1, -1}, bool is_mouse_down = false);};