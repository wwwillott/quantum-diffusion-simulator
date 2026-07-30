#include "Simulator2D.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <sstream>

std::mutex Simulator2D::landscape_cache_mutex_;
std::unordered_map<std::string, Simulator2D::LandscapeCacheEntry> Simulator2D::landscape_cache_;

std::string Simulator2D::make_landscape_cache_key(const std::string& filepath, const SimConfig& cfg) {
    std::ostringstream oss;
    oss << filepath << "|" << cfg.resolution << "|"
        << cfg.min_lat << "|" << cfg.max_lat << "|"
        << cfg.min_lon << "|" << cfg.max_lon << "|"
        << cfg.base_survival_rate << "|" << cfg.urban_multiplier;
    return oss.str();
}

Simulator2D::Simulator2D(SimConfig master_settings) {
    master_settings.validate();
    config = master_settings;
    int N = config.resolution;
    int total_nodes = N * N;

    amp_N.resize(total_nodes, {0.0f, 0.0f});
    amp_S.resize(total_nodes, {0.0f, 0.0f});
    amp_E.resize(total_nodes, {0.0f, 0.0f});
    amp_W.resize(total_nodes, {0.0f, 0.0f});
    amp_C.resize(total_nodes, {0.0f, 0.0f});

    next_amp_N.resize(total_nodes, {0.0f, 0.0f});
    next_amp_S.resize(total_nodes, {0.0f, 0.0f});
    next_amp_E.resize(total_nodes, {0.0f, 0.0f});
    next_amp_W.resize(total_nodes, {0.0f, 0.0f});
    next_amp_C.resize(total_nodes, {0.0f, 0.0f});

    prev_probs.resize(total_nodes, 0.0f);
    node_growth_rate.resize(total_nodes, 1.0f);
    node_capacity.resize(total_nodes, 0.0f);
    land_mask.resize(total_nodes, 1.0f);
    historical_probs.resize(total_nodes, 0.0f);

    applyInitialState();
    load_ascii_mask(config.landscape_path);
}

void Simulator2D::applyInitialState() {
    int N = config.resolution;
    int center_x = N / 2;
    int center_y = N / 2;
    int center_idx = (center_y * N) + center_x;

    switch (config.init_state_2d) {
        case InitialState2D::PURE_NORTH:
            amp_N[center_idx] = {1.0f, 0.0f};
            break;
        case InitialState2D::UNIFORM:
            amp_N[center_idx] = {0.5f, 0.0f};
            amp_S[center_idx] = {0.5f, 0.0f};
            amp_E[center_idx] = {0.5f, 0.0f};
            amp_W[center_idx] = {0.5f, 0.0f};
            break;
        case InitialState2D::ALTERNATING_PHASE:
            amp_N[center_idx] = {0.5f, 0.0f};
            amp_S[center_idx] = {-0.5f, 0.0f};
            amp_E[center_idx] = {0.5f, 0.0f};
            amp_W[center_idx] = {-0.5f, 0.0f};
            break;
        case InitialState2D::CHIRAL_WEST:
            amp_N[center_idx] = {0.5f, 0.0f};
            amp_S[center_idx] = {0.0f, 0.5f};
            amp_E[center_idx] = {-0.5f, 0.0f};
            amp_W[center_idx] = {0.0f, -0.5f};
            break;
        case InitialState2D::HADAMARD_SYMMETRIC:
            amp_N[center_idx] = {0.5f, 0.0f};
            amp_S[center_idx] = {0.0f, 0.5f};
            amp_E[center_idx] = {0.0f, 0.5f};
            amp_W[center_idx] = {-0.5f, 0.0f};
            break;
    }
}

void Simulator2D::update() {
    current_step++;
    int N = config.resolution;
    int total_nodes = N * N;

    for (int i = 0; i < total_nodes; i++) {
        prev_probs[i] = std::norm(amp_N[i]) + std::norm(amp_S[i]) + std::norm(amp_E[i]) +
                        std::norm(amp_W[i]) + std::norm(amp_C[i]);
    }

    if (config.mode == SimMode::QUANTUM) {
        const std::complex<float> I(0.0f, 1.0f);

        for (int i = 0; i < total_nodes; i++) {
            std::complex<float> n = amp_N[i];
            std::complex<float> s = amp_S[i];
            std::complex<float> e = amp_E[i];
            std::complex<float> w = amp_W[i];
            std::complex<float> c = amp_C[i];

            std::complex<float> n_mix, s_mix, e_mix, w_mix;

            if (config.unitary_coin == UnitaryCoin2D::GROVER) {
                std::complex<float> half_sum = (n + s + e + w) * 0.5f;
                n_mix = half_sum - n;
                s_mix = half_sum - s;
                e_mix = half_sum - e;
                w_mix = half_sum - w;
            } else if (config.unitary_coin == UnitaryCoin2D::DFT) {
                n_mix = 0.5f * (n + s + e + w);
                s_mix = 0.5f * (n + I * s - e - I * w);
                e_mix = 0.5f * (n - s + e - w);
                w_mix = 0.5f * (n - I * s - e + I * w);
            } else if (config.unitary_coin == UnitaryCoin2D::HADAMARD_TENSOR) {
                n_mix = 0.5f * (n + s + e + w);
                s_mix = 0.5f * (n - s + e - w);
                e_mix = 0.5f * (n + s - e - w);
                w_mix = 0.5f * (n - s - e + w);
            } else if (config.unitary_coin == UnitaryCoin2D::ALTERNATING_DFT) {
                bool is_even_step = (current_step % 2 == 0);
                if (is_even_step) {
                    n_mix = 0.5f * (n + s + e + w);
                    s_mix = 0.5f * (n + I * s - e - I * w);
                    e_mix = 0.5f * (n - s + e - w);
                    w_mix = 0.5f * (n - I * s - e + I * w);
                } else {
                    n_mix = 0.5f * (n + s + e + w);
                    s_mix = 0.5f * (n - I * s - e + I * w);
                    e_mix = 0.5f * (n - s + e - w);
                    w_mix = 0.5f * (n + I * s - e - I * w);
                }
            }

            if (config.system_type_2d == SystemType::OPEN) {
                float local_R0 = node_growth_rate[i];
                float current_p = std::norm(n) + std::norm(s) + std::norm(e) + std::norm(w) + std::norm(c);
                float simulated_humans = current_p * max_seed_cases;
                float capacity = node_capacity[i];

                float effective_R0 = local_R0;
                if (capacity > 0.0f) {
                    float saturation = simulated_humans / capacity;
                    effective_R0 = local_R0 * std::max(0.0f, 1.0f - saturation);
                } else {
                    effective_R0 = 0.0f;
                }

                if (config.nodal_retention) {
                    float m_amp = std::sqrt(config.mobility_rate);
                    float stay_amp = std::sqrt(1.0f - config.mobility_rate);
                    float split_amp = m_amp * 0.5f;

                    std::complex<float> g_c = c * effective_R0;
                    std::complex<float> g_n = n_mix * effective_R0;
                    std::complex<float> g_s = s_mix * effective_R0;
                    std::complex<float> g_e = e_mix * effective_R0;
                    std::complex<float> g_w = w_mix * effective_R0;

                    std::complex<float> unmixed_sum = (n + s + e + w) * effective_R0;
                    amp_C[i] = (g_c * stay_amp) + (unmixed_sum * 0.5f * stay_amp);

                    std::complex<float> leak_N = {0.0f, 0.0f};
                    std::complex<float> leak_S = {0.0f, 0.0f};
                    std::complex<float> leak_E = {0.0f, 0.0f};
                    std::complex<float> leak_W = {0.0f, 0.0f};

                    switch (config.init_state_2d) {
                        case InitialState2D::PURE_NORTH:
                            leak_N = g_c * m_amp;
                            break;
                        case InitialState2D::UNIFORM:
                            leak_N = g_c * split_amp;
                            leak_S = g_c * split_amp;
                            leak_E = g_c * split_amp;
                            leak_W = g_c * split_amp;
                            break;
                        case InitialState2D::ALTERNATING_PHASE:
                            leak_N = g_c * split_amp;
                            leak_S = -g_c * split_amp;
                            leak_E = g_c * split_amp;
                            leak_W = -g_c * split_amp;
                            break;
                        case InitialState2D::CHIRAL_WEST:
                            leak_N = g_c * split_amp;
                            leak_S = g_c * split_amp * I;
                            leak_E = -g_c * split_amp;
                            leak_W = -g_c * split_amp * I;
                            break;
                        case InitialState2D::HADAMARD_SYMMETRIC:
                            leak_N = g_c * split_amp;
                            leak_S = g_c * split_amp * I;
                            leak_E = g_c * split_amp * I;
                            leak_W = -g_c * split_amp;
                            break;
                    }

                    amp_N[i] = (g_n * m_amp) + leak_N;
                    amp_S[i] = (g_s * m_amp) + leak_S;
                    amp_E[i] = (g_e * m_amp) + leak_E;
                    amp_W[i] = (g_w * m_amp) + leak_W;
                } else {
                    amp_N[i] = n_mix * effective_R0;
                    amp_S[i] = s_mix * effective_R0;
                    amp_E[i] = e_mix * effective_R0;
                    amp_W[i] = w_mix * effective_R0;
                    amp_C[i] = {0.0f, 0.0f};
                }
            } else {
                amp_N[i] = n_mix;
                amp_S[i] = s_mix;
                amp_E[i] = e_mix;
                amp_W[i] = w_mix;
            }
        }
    } else if (config.mode == SimMode::CLASSICAL) {
        for (int i = 0; i < total_nodes; i++) {
            float current_p = std::norm(amp_N[i]) + std::norm(amp_S[i]) + std::norm(amp_E[i]) +
                              std::norm(amp_W[i]) + std::norm(amp_C[i]);

            float effective_R0 = 1.0f;

            if (config.system_type_2d == SystemType::OPEN) {
                float local_R0 = node_growth_rate[i];
                float simulated_humans = current_p * max_seed_cases;
                float capacity = node_capacity[i];

                if (capacity > 0.0f) {
                    float saturation = simulated_humans / capacity;
                    effective_R0 = local_R0 * std::max(0.0f, 1.0f - saturation);
                } else {
                    effective_R0 = 0.0f;
                }
            }

            float next_total_p = current_p * effective_R0;

            if (config.nodal_retention) {
                float stay_p = next_total_p * (1.0f - config.mobility_rate);
                float move_p = next_total_p * config.mobility_rate;
                float dir_p = move_p * 0.25f;

                amp_C[i] = {std::sqrt(stay_p), 0.0f};
                amp_N[i] = {std::sqrt(dir_p), 0.0f};
                amp_S[i] = {std::sqrt(dir_p), 0.0f};
                amp_E[i] = {std::sqrt(dir_p), 0.0f};
                amp_W[i] = {std::sqrt(dir_p), 0.0f};
            } else {
                float dir_p = next_total_p * 0.25f;
                amp_C[i] = {0.0f, 0.0f};
                amp_N[i] = {std::sqrt(dir_p), 0.0f};
                amp_S[i] = {std::sqrt(dir_p), 0.0f};
                amp_E[i] = {std::sqrt(dir_p), 0.0f};
                amp_W[i] = {std::sqrt(dir_p), 0.0f};
            }
        }
    }

    std::fill(next_amp_N.begin(), next_amp_N.end(), std::complex<float>(0.0f, 0.0f));
    std::fill(next_amp_S.begin(), next_amp_S.end(), std::complex<float>(0.0f, 0.0f));
    std::fill(next_amp_E.begin(), next_amp_E.end(), std::complex<float>(0.0f, 0.0f));
    std::fill(next_amp_W.begin(), next_amp_W.end(), std::complex<float>(0.0f, 0.0f));
    std::fill(next_amp_C.begin(), next_amp_C.end(), std::complex<float>(0.0f, 0.0f));

    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            int current_idx = y * N + x;
            next_amp_C[current_idx] = amp_C[current_idx];

            if (config.boundary_condition == BoundaryType::ABSORBING) {
                if (y > 0) next_amp_N[(y - 1) * N + x] = amp_N[current_idx];
                if (y < N - 1) next_amp_S[(y + 1) * N + x] = amp_S[current_idx];
                if (x < N - 1) next_amp_E[y * N + (x + 1)] = amp_E[current_idx];
                if (x > 0) next_amp_W[y * N + (x - 1)] = amp_W[current_idx];
            } else {
                if (y > 0) next_amp_N[(y - 1) * N + x] += amp_N[current_idx];
                else next_amp_S[current_idx] += amp_N[current_idx];
                if (y < N - 1) next_amp_S[(y + 1) * N + x] += amp_S[current_idx];
                else next_amp_N[current_idx] += amp_S[current_idx];
                if (x < N - 1) next_amp_E[y * N + (x + 1)] += amp_E[current_idx];
                else next_amp_W[current_idx] += amp_E[current_idx];
                if (x > 0) next_amp_W[y * N + (x - 1)] += amp_W[current_idx];
                else next_amp_E[current_idx] += amp_W[current_idx];
            }
        }
    }

    amp_N = next_amp_N;
    amp_S = next_amp_S;
    amp_E = next_amp_E;
    amp_W = next_amp_W;
    amp_C = next_amp_C;

    if (config.system_type_2d == SystemType::OPEN) {
        for (int i = 0; i < N * N; i++) {
            amp_N[i] *= land_mask[i];
            amp_S[i] *= land_mask[i];
            amp_E[i] *= land_mask[i];
            amp_W[i] *= land_mask[i];
            amp_C[i] *= land_mask[i];
        }
    }

    if (show_historical_overlay || track_masked_mse || track_emd) {
        // days_per_tick > 1 slows the calendar relative to physics (more real days per tick).
        int day_idx = (current_step * config.days_per_tick) / config.quantum_ticks_per_real_tick;
        rebuild_historical_probs_for_day(day_idx);

        if (track_masked_mse) {
            masked_mse_history.push_back(compute_legacy_masked_mse());
        }
        if (track_emd) {
            emd_history.push_back(compute_legacy_marginal_emd());
        }
    }
}

void Simulator2D::get_probabilities(std::vector<float>& out) const {
    int total = config.resolution * config.resolution;
    out.resize(total);
    for (int i = 0; i < total; i++) {
        out[i] = std::norm(amp_N[i]) + std::norm(amp_S[i]) + std::norm(amp_E[i]) +
                 std::norm(amp_W[i]) + std::norm(amp_C[i]);
    }
}

void Simulator2D::get_historical_probs(std::vector<float>& out) const {
    out = historical_probs;
}

void Simulator2D::rebuild_historical_probs_for_day(int day_idx) {
    int N = config.resolution;
    std::fill(historical_probs.begin(), historical_probs.end(), 0.0f);

    for (const auto& point : historical_dataset) {
        if (point.lat < config.min_lat || point.lat > config.max_lat ||
            point.lon < config.min_lon || point.lon > config.max_lon) {
            continue;
        }

        float pct_x = (point.lon - config.min_lon) / (config.max_lon - config.min_lon);
        float pct_y = (config.max_lat - point.lat) / (config.max_lat - config.min_lat);

        int grid_x = static_cast<int>(pct_x * (N - 1));
        int grid_y = static_cast<int>(pct_y * (N - 1));

        if (grid_x >= 0 && grid_x < N && grid_y >= 0 && grid_y < N) {
            int use_day = day_idx;
            if (use_day < 0) use_day = 0;
            if (use_day >= static_cast<int>(point.cases_history.size())) {
                use_day = static_cast<int>(point.cases_history.size()) - 1;
            }
            if (use_day < 0) continue;

            float hist_weight = static_cast<float>(point.cases_history[use_day]) /
                                static_cast<float>(max_historical_cases);
            int idx = grid_y * N + grid_x;
            historical_probs[idx] = std::min(historical_probs[idx] + hist_weight, 1.0f);
        }
    }
}

float Simulator2D::compute_legacy_masked_mse() const {
    int total_nodes = config.resolution * config.resolution;
    float mse_sum = 0.0f;
    int mask_count = 0;

    for (int i = 0; i < total_nodes; i++) {
        if (historical_probs[i] > 0.0f) {
            float sim_p = std::norm(amp_N[i]) + std::norm(amp_S[i]) + std::norm(amp_E[i]) +
                          std::norm(amp_W[i]) + std::norm(amp_C[i]);
            float sim_cases = sim_p * max_seed_cases;
            float real_cases = historical_probs[i] * max_historical_cases;
            float diff = sim_cases - real_cases;
            mse_sum += (diff * diff);
            mask_count++;
        }
    }
    return mask_count > 0 ? mse_sum / mask_count : 0.0f;
}

float Simulator2D::compute_legacy_marginal_emd() const {
    int N = config.resolution;
    std::vector<float> sim_x(N, 0.0f), sim_y(N, 0.0f);
    std::vector<float> hist_x(N, 0.0f), hist_y(N, 0.0f);
    float total_sim = 0.0f, total_hist = 0.0f;

    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            int idx = y * N + x;
            float sim_val = (std::norm(amp_N[idx]) + std::norm(amp_S[idx]) + std::norm(amp_E[idx]) +
                             std::norm(amp_W[idx]) + std::norm(amp_C[idx])) *
                            max_seed_cases / max_historical_cases;
            float hist_val = historical_probs[idx];

            sim_x[x] += sim_val;
            sim_y[y] += sim_val;
            hist_x[x] += hist_val;
            hist_y[y] += hist_val;
            total_sim += sim_val;
            total_hist += hist_val;
        }
    }

    float emd_total = 0.0f;
    float cdf_sim_x = 0.0f, cdf_hist_x = 0.0f;
    float cdf_sim_y = 0.0f, cdf_hist_y = 0.0f;

    for (int i = 0; i < N; i++) {
        cdf_sim_x += sim_x[i] / (total_sim > 0 ? total_sim : 1.0f);
        cdf_hist_x += hist_x[i] / (total_hist > 0 ? total_hist : 1.0f);
        emd_total += std::abs(cdf_sim_x - cdf_hist_x);

        cdf_sim_y += sim_y[i] / (total_sim > 0 ? total_sim : 1.0f);
        cdf_hist_y += hist_y[i] / (total_hist > 0 ? total_hist : 1.0f);
        emd_total += std::abs(cdf_sim_y - cdf_hist_y);
    }
    return emd_total;
}

bool Simulator2D::state_is_finite() const {
    int total = config.resolution * config.resolution;
    for (int i = 0; i < total; i++) {
        auto check = [](const std::complex<float>& z) {
            return std::isfinite(z.real()) && std::isfinite(z.imag());
        };
        if (!check(amp_N[i]) || !check(amp_S[i]) || !check(amp_E[i]) ||
            !check(amp_W[i]) || !check(amp_C[i])) {
            return false;
        }
    }
    return true;
}

float Simulator2D::total_probability() const {
    float sum = 0.0f;
    int total = config.resolution * config.resolution;
    for (int i = 0; i < total; i++) {
        sum += std::norm(amp_N[i]) + std::norm(amp_S[i]) + std::norm(amp_E[i]) +
               std::norm(amp_W[i]) + std::norm(amp_C[i]);
    }
    return sum;
}

void Simulator2D::inject_dataset(const std::vector<ViralHotspot>& dataset) {
    int N = config.resolution;

    std::fill(amp_N.begin(), amp_N.end(), std::complex<float>(0.0f, 0.0f));
    std::fill(amp_S.begin(), amp_S.end(), std::complex<float>(0.0f, 0.0f));
    std::fill(amp_E.begin(), amp_E.end(), std::complex<float>(0.0f, 0.0f));
    std::fill(amp_W.begin(), amp_W.end(), std::complex<float>(0.0f, 0.0f));
    std::fill(amp_C.begin(), amp_C.end(), std::complex<float>(0.0f, 0.0f));
    std::fill(prev_probs.begin(), prev_probs.end(), 0.0f);

    float min_lat = config.min_lat;
    float max_lat = config.max_lat;
    float min_lon = config.min_lon;
    float max_lon = config.max_lon;

    historical_dataset = dataset;
    max_historical_cases = 1;
    max_seed_cases = 1.0f;

    // Eligible day-0 seed sites (physics only). Historical maps always use full data.
    struct SeedCand {
        size_t index;
        int day0_cases;
    };
    std::vector<SeedCand> seed_cands;
    seed_cands.reserve(dataset.size());

    for (size_t pi = 0; pi < dataset.size(); ++pi) {
        const auto& point = dataset[pi];
        if (point.lat < min_lat || point.lat > max_lat || point.lon < min_lon || point.lon > max_lon) {
            continue;
        }

        if (!point.cases_history.empty() && point.cases_history[0] > 0) {
            seed_cands.push_back(SeedCand{pi, point.cases_history[0]});
        }

        for (int cases : point.cases_history) {
            if (cases > max_historical_cases) {
                max_historical_cases = cases;
            }
        }
    }

    std::sort(seed_cands.begin(), seed_cands.end(),
              [](const SeedCand& a, const SeedCand& b) {
                  if (a.day0_cases != b.day0_cases) return a.day0_cases > b.day0_cases;
                  return a.index < b.index;
              });

    size_t keep_n = seed_cands.size();
    if (config.seed_keep_fraction < 1.0f && !seed_cands.empty()) {
        keep_n = static_cast<size_t>(std::ceil(
            static_cast<double>(seed_cands.size()) * static_cast<double>(config.seed_keep_fraction)));
        if (keep_n < 1) keep_n = 1;
        if (keep_n > seed_cands.size()) keep_n = seed_cands.size();
    }
    std::vector<char> seed_keep(dataset.size(), 0);
    for (size_t i = 0; i < keep_n; ++i) {
        seed_keep[seed_cands[i].index] = 1;
        if (seed_cands[i].day0_cases > max_seed_cases) {
            max_seed_cases = static_cast<float>(seed_cands[i].day0_cases);
        }
    }
    // fraction==1 preserves legacy max_seed over all in-bounds day-0 cases
    if (config.seed_keep_fraction >= 1.0f) {
        max_seed_cases = 1.0f;
        for (const auto& point : dataset) {
            if (point.lat < min_lat || point.lat > max_lat || point.lon < min_lon || point.lon > max_lon) {
                continue;
            }
            if (!point.cases_history.empty() && point.cases_history[0] > max_seed_cases) {
                max_seed_cases = static_cast<float>(point.cases_history[0]);
            }
        }
        std::fill(seed_keep.begin(), seed_keep.end(), 1);
    }

    historical_probs.assign(N * N, 0.0f);

    for (size_t pi = 0; pi < dataset.size(); ++pi) {
        const auto& point = dataset[pi];
        if (point.lat < min_lat || point.lat > max_lat || point.lon < min_lon || point.lon > max_lon) {
            continue;
        }

        float pct_x = (point.lon - min_lon) / (max_lon - min_lon);
        float pct_y = (max_lat - point.lat) / (max_lat - min_lat);

        int grid_x = static_cast<int>(pct_x * (N - 1));
        int grid_y = static_cast<int>(pct_y * (N - 1));

        if (grid_x >= 0 && grid_x < N && grid_y >= 0 && grid_y < N) {
            int idx = grid_y * N + grid_x;

            float physics_weight = 0.0f;
            float hist_weight = 0.0f;

            if (!point.cases_history.empty()) {
                hist_weight = static_cast<float>(point.cases_history[0]) /
                              static_cast<float>(max_historical_cases);
                if (seed_keep[pi]) {
                    physics_weight = static_cast<float>(point.cases_history[0]) / max_seed_cases;
                }
            }

            float magnitude = 0.5f * std::sqrt(physics_weight);

            if (physics_weight > 0.0f) {
            if (config.nodal_retention) {
                amp_C[idx] += std::complex<float>(std::sqrt(physics_weight), 0.0f);
            } else {
                switch (config.init_state_2d) {
                    case InitialState2D::PURE_NORTH:
                        amp_N[idx] += std::complex<float>(magnitude * 2.0f, 0.0f);
                        break;
                    case InitialState2D::UNIFORM:
                        amp_N[idx] += std::complex<float>(magnitude, 0.0f);
                        amp_S[idx] += std::complex<float>(magnitude, 0.0f);
                        amp_E[idx] += std::complex<float>(magnitude, 0.0f);
                        amp_W[idx] += std::complex<float>(magnitude, 0.0f);
                        break;
                    case InitialState2D::ALTERNATING_PHASE:
                        amp_N[idx] += std::complex<float>(magnitude, 0.0f);
                        amp_S[idx] += std::complex<float>(-magnitude, 0.0f);
                        amp_E[idx] += std::complex<float>(magnitude, 0.0f);
                        amp_W[idx] += std::complex<float>(-magnitude, 0.0f);
                        break;
                    case InitialState2D::CHIRAL_WEST:
                        amp_N[idx] += std::complex<float>(magnitude, 0.0f);
                        amp_S[idx] += std::complex<float>(0.0f, magnitude);
                        amp_E[idx] += std::complex<float>(-magnitude, 0.0f);
                        amp_W[idx] += std::complex<float>(0.0f, -magnitude);
                        break;
                    case InitialState2D::HADAMARD_SYMMETRIC:
                        amp_N[idx] += std::complex<float>(magnitude, 0.0f);
                        amp_S[idx] += std::complex<float>(0.0f, magnitude);
                        amp_E[idx] += std::complex<float>(0.0f, magnitude);
                        amp_W[idx] += std::complex<float>(-magnitude, 0.0f);
                        break;
                }
            }

            prev_probs[idx] = physics_weight;
            }
            historical_probs[idx] = std::min(historical_probs[idx] + hist_weight, 1.0f);
        }
    }
}

void Simulator2D::inject_landscape(const std::vector<GeoNode>& pop_data) {
    int N = config.resolution;
    float min_lat = config.min_lat;
    float max_lat = config.max_lat;
    float min_lon = config.min_lon;
    float max_lon = config.max_lon;

    std::vector<int> grid_population(N * N, 0);
    int max_pop = 0;

    for (const auto& point : pop_data) {
        if (point.lat < min_lat || point.lat > max_lat || point.lon < min_lon || point.lon > max_lon) {
            continue;
        }

        float pct_x = (point.lon - min_lon) / (max_lon - min_lon);
        float pct_y = (max_lat - point.lat) / (max_lat - min_lat);

        int grid_x = static_cast<int>(pct_x * (N - 1));
        int grid_y = static_cast<int>(pct_y * (N - 1));

        if (grid_x >= 0 && grid_x < N && grid_y >= 0 && grid_y < N) {
            int idx = grid_y * N + grid_x;
            grid_population[idx] += point.population;
            if (grid_population[idx] > max_pop) max_pop = grid_population[idx];
        }
    }

    float log_max = std::log10(max_pop + 1.0f);

    for (int i = 0; i < N * N; i++) {
        if (grid_population[i] > 0) {
            float log_pop = std::log10(grid_population[i] + 1.0f);
            float pop_ratio = log_pop / log_max;
            node_growth_rate[i] = config.base_survival_rate + (config.urban_multiplier * pop_ratio);
        } else {
            node_growth_rate[i] = config.base_survival_rate;
        }
    }
}

void Simulator2D::load_ascii_mask(const std::string& filepath) {
    std::string key = make_landscape_cache_key(filepath, config);
    {
        std::lock_guard<std::mutex> lock(landscape_cache_mutex_);
        auto it = landscape_cache_.find(key);
        if (it != landscape_cache_.end()) {
            land_mask = it->second.land_mask;
            node_growth_rate = it->second.node_growth_rate;
            node_capacity = it->second.node_capacity;
            return;
        }
    }

    // Persistent disk cache for projected grids (avoids re-reading ~200MB ASC per process).
    auto disk_path_for = [&](const std::string& tag) {
        std::hash<std::string> h;
        std::ostringstream name;
        name << "data/.landscape_cache/" << std::hex << h(key) << "_" << tag << ".bin";
        return name.str();
    };

    auto try_load_disk = [&]() -> bool {
        std::ifstream lm(disk_path_for("mask"), std::ios::binary);
        std::ifstream gr(disk_path_for("growth"), std::ios::binary);
        std::ifstream cp(disk_path_for("cap"), std::ios::binary);
        if (!lm || !gr || !cp) return false;
        int N = config.resolution;
        int total = N * N;
        land_mask.resize(total);
        node_growth_rate.resize(total);
        node_capacity.resize(total);
        lm.read(reinterpret_cast<char*>(land_mask.data()), sizeof(float) * total);
        gr.read(reinterpret_cast<char*>(node_growth_rate.data()), sizeof(float) * total);
        cp.read(reinterpret_cast<char*>(node_capacity.data()), sizeof(float) * total);
        return lm && gr && cp;
    };

    if (try_load_disk()) {
        LandscapeCacheEntry entry;
        entry.land_mask = land_mask;
        entry.node_growth_rate = node_growth_rate;
        entry.node_capacity = node_capacity;
        std::lock_guard<std::mutex> lock(landscape_cache_mutex_);
        landscape_cache_[key] = std::move(entry);
        return;
    }

    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "\n[!] ERROR: Could not find '" << filepath
                  << "'! Defaulting to uniform land with synthetic capacity.\n\n";
        int N = config.resolution;
        land_mask.assign(N * N, 1.0f);
        node_growth_rate.assign(N * N, config.base_survival_rate);
        // Synthetic capacity keeps OPEN-mode logistic growth usable without NASA ASC.
        node_capacity.assign(N * N, 1.0e6f);
        return;
    }

    std::string label;
    int ncols, nrows;
    float xllcorner, yllcorner, cellsize, nodata;

    file >> label >> ncols;
    file >> label >> nrows;
    file >> label >> xllcorner;
    file >> label >> yllcorner;
    file >> label >> cellsize;
    file >> label >> nodata;

    std::vector<std::vector<float>> raw_grid(nrows, std::vector<float>(ncols));
    float max_pop = 0.0f;

    for (int r = 0; r < nrows; r++) {
        for (int c = 0; c < ncols; c++) {
            file >> raw_grid[r][c];
            if (raw_grid[r][c] != nodata && raw_grid[r][c] > max_pop) {
                max_pop = raw_grid[r][c];
            }
        }
    }
    file.close();

    int N = config.resolution;
    land_mask.assign(N * N, 0.0f);
    node_growth_rate.assign(N * N, config.base_survival_rate);
    node_capacity.assign(N * N, 0.0f);

    float max_lat_asc = yllcorner + (nrows * cellsize);
    float log_max = std::log10(max_pop + 1.0f);

    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            int idx = y * N + x;

            float lon = config.min_lon + (x / (float)(N - 1)) * (config.max_lon - config.min_lon);
            float lat = config.max_lat - (y / (float)(N - 1)) * (config.max_lat - config.min_lat);

            int asc_c = static_cast<int>((lon - xllcorner) / cellsize);
            int asc_r = static_cast<int>((max_lat_asc - lat) / cellsize);

            if (asc_r >= 0 && asc_r < nrows && asc_c >= 0 && asc_c < ncols) {
                float cell_value = raw_grid[asc_r][asc_c];

                if (cell_value < 0.0f || cell_value == nodata) {
                    land_mask[idx] = 0.0f;
                    node_growth_rate[idx] = 0.0f;
                    node_capacity[idx] = 0.0f;
                } else {
                    land_mask[idx] = 1.0f;
                    node_capacity[idx] = cell_value;
                    float log_pop = std::log10(cell_value + 1.0f);
                    float pop_ratio = log_pop / log_max;
                    node_growth_rate[idx] = config.base_survival_rate + (config.urban_multiplier * pop_ratio);
                }
            } else {
                land_mask[idx] = 0.0f;
            }
        }
    }

    LandscapeCacheEntry entry;
    entry.land_mask = land_mask;
    entry.node_growth_rate = node_growth_rate;
    entry.node_capacity = node_capacity;

    {
        std::lock_guard<std::mutex> lock(landscape_cache_mutex_);
        landscape_cache_[key] = entry;
    }

    // Best-effort disk cache write
    (void)std::system("mkdir -p data/.landscape_cache");
    auto write_bin = [&](const std::string& tag, const std::vector<float>& data) {
        std::hash<std::string> h;
        std::ostringstream name;
        name << "data/.landscape_cache/" << std::hex << h(key) << "_" << tag << ".bin";
        std::ofstream out(name.str(), std::ios::binary);
        if (!out) return;
        out.write(reinterpret_cast<const char*>(data.data()), sizeof(float) * data.size());
    };
    write_bin("mask", land_mask);
    write_bin("growth", node_growth_rate);
    write_bin("cap", node_capacity);
}
