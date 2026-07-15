#pragma once
#include <vector>
#include <complex>
#include <fstream>
#include "raylib.h"
#include "SimConfig.h"
#include "DataParser.h"

class Simulator2D {
private:
    std::vector<std::complex<float>> amp_N;
    std::vector<std::complex<float>> amp_S;
    std::vector<std::complex<float>> amp_E;
    std::vector<std::complex<float>> amp_W;
    std::vector<std::complex<float>> amp_C;

    std::vector<std::complex<float>> next_amp_N;
    std::vector<std::complex<float>> next_amp_S;
    std::vector<std::complex<float>> next_amp_E;
    std::vector<std::complex<float>> next_amp_W;
    std::vector<std::complex<float>> next_amp_C;

    std::vector<float> prev_probs;
    std::vector<float> std_dev_x_hist;
    std::vector<float> std_dev_y_hist;
    std::vector<float> std_dev_total_hist;

    std::vector<float> mse_history;

    std::vector<float> land_mask;
    void load_ascii_mask(const std::string& filepath);

    void applyInitialState();

public:
    SimConfig config;
    Simulator2D(SimConfig master_settings);
    int current_step = 0;
    std::vector<float> node_growth_rate;
    std::vector<float> node_capacity; 
    
    // The Historical Timeline Data
    std::vector<ViralHotspot> historical_dataset; 
    std::vector<float> historical_probs; 
    int max_historical_cases = 1;
    float max_seed_cases = 1.0f;
    bool show_historical_overlay = false;
    
    // Telemetry Variables
    bool show_metrics_menu = false;
    bool show_mode_menu = false;
    bool track_masked_mse = false;
    bool track_emd = false;
    
    std::vector<float> masked_mse_history;
    std::vector<float> emd_history;

    void inject_dataset(const std::vector<ViralHotspot>& dataset);
    void inject_landscape(const std::vector<GeoNode>& pop_data);
    void update();
    void draw(int screen_width, int screen_height, bool show_info = false, bool is_paused = false, Vector2 mouse_pos = {-1, -1}, bool is_mouse_down = false);
};