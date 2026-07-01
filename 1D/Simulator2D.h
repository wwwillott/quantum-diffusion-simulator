#pragma once
#include <vector>
#include <complex>
#include "raylib.h"
#include "SimConfig.h"
#include "DataParser.h"

class Simulator2D {
private:
    std::vector<std::complex<float>> amp_N;
    std::vector<std::complex<float>> amp_S;
    std::vector<std::complex<float>> amp_E;
    std::vector<std::complex<float>> amp_W;

    std::vector<std::complex<float>> next_amp_N;
    std::vector<std::complex<float>> next_amp_S;
    std::vector<std::complex<float>> next_amp_E;
    std::vector<std::complex<float>> next_amp_W;

    std::vector<float> prev_probs;
    std::vector<float> std_dev_x_hist;
    std::vector<float> std_dev_y_hist;
    std::vector<float> std_dev_total_hist;

    void applyInitialState();

public:
    SimConfig config;
    Simulator2D(SimConfig master_settings);
    int current_step = 0;
    std::vector<float> node_growth_rate;
    
    // The Historical Timeline Data
    std::vector<ViralHotspot> historical_dataset; 
    std::vector<float> historical_probs; 
    int max_historical_cases = 1;
    bool show_historical_overlay = false;

    void inject_dataset(const std::vector<ViralHotspot>& dataset);
    void inject_landscape(const std::vector<GeoNode>& pop_data);
    void update();
    void draw(int screen_width, int screen_height, bool show_info = false, bool is_paused = false, Vector2 mouse_pos = {-1, -1}, bool is_mouse_down = false);
};