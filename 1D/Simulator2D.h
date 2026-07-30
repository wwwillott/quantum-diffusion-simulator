#pragma once

#include <complex>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "DataParser.h"
#include "SimConfig.h"

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

    struct LandscapeCacheEntry {
        std::vector<float> land_mask;
        std::vector<float> node_growth_rate;
        std::vector<float> node_capacity;
    };

    static std::mutex landscape_cache_mutex_;
    static std::unordered_map<std::string, LandscapeCacheEntry> landscape_cache_;
    static std::string make_landscape_cache_key(const std::string& filepath, const SimConfig& cfg);

public:
    SimConfig config;
    explicit Simulator2D(SimConfig master_settings);
    int current_step = 0;
    std::vector<float> node_growth_rate;
    std::vector<float> node_capacity;

    std::vector<ViralHotspot> historical_dataset;
    std::vector<float> historical_probs;
    int max_historical_cases = 1;
    float max_seed_cases = 1.0f;
    bool show_historical_overlay = false;

    bool show_metrics_menu = false;
    bool show_mode_menu = false;
    bool track_masked_mse = false;
    bool track_emd = false;

    std::vector<float> masked_mse_history;
    std::vector<float> emd_history;

    void inject_dataset(const std::vector<ViralHotspot>& dataset);
    void inject_landscape(const std::vector<GeoNode>& pop_data);
    void update();

    // Probability / state accessors for headless evaluation
    void get_probabilities(std::vector<float>& out) const;
    void get_historical_probs(std::vector<float>& out) const;
    void rebuild_historical_probs_for_day(int day_idx);
    float compute_legacy_masked_mse() const;
    float compute_legacy_marginal_emd() const;
    bool state_is_finite() const;
    float total_probability() const;

    // Friends / accessors used by the renderer
    const std::vector<std::complex<float>>& get_amp_N() const { return amp_N; }
    const std::vector<std::complex<float>>& get_amp_S() const { return amp_S; }
    const std::vector<std::complex<float>>& get_amp_E() const { return amp_E; }
    const std::vector<std::complex<float>>& get_amp_W() const { return amp_W; }
    const std::vector<std::complex<float>>& get_amp_C() const { return amp_C; }
    const std::vector<float>& get_prev_probs() const { return prev_probs; }
    std::vector<float>& mutable_std_dev_x_hist() { return std_dev_x_hist; }
    std::vector<float>& mutable_std_dev_y_hist() { return std_dev_y_hist; }
    std::vector<float>& mutable_std_dev_total_hist() { return std_dev_total_hist; }
};
