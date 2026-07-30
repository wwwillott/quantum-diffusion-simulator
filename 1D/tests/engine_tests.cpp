#include "DataParser.h"
#include "SimConfig.h"
#include "Simulator2D.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

static int g_failures = 0;

#define EXPECT_TRUE(cond)                                                          \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << " " #cond "\n"; \
            g_failures++;                                                          \
        }                                                                          \
    } while (0)

#define EXPECT_NEAR(a, b, tol)                                                     \
    do {                                                                           \
        double _a = (a), _b = (b);                                                 \
        if (std::fabs(_a - _b) > (tol)) {                                          \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__                    \
                      << " | " << _a << " - " << _b << " | > " << (tol) << "\n";   \
            g_failures++;                                                          \
        }                                                                          \
    } while (0)

static void test_config_validation() {
    SimConfig cfg;
    cfg.resolution = 100;
    cfg.validate();

    cfg.mobility_rate = 1.5f;
    bool threw = false;
    try {
        cfg.validate();
    } catch (...) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

static void test_date_index_and_bounds() {
    std::vector<std::string> headers = {"1/22/20", "2/21/20", "6/30/20", "3/9/23"};
    EXPECT_TRUE(FindDateIndex(headers, "2/21/20") == 1);
    EXPECT_TRUE(FindDateIndex(headers, "02/21/2020") == 1);
    EXPECT_TRUE(FindDateIndex(headers, "6/30/20") == 2);
    EXPECT_TRUE(FindDateIndex(headers, "1/1/99") == -1);
    EXPECT_TRUE(NormalizeDateKey("2/21/20") == NormalizeDateKey("02/21/2020"));
}

static void test_parse_mini_dataset() {
    auto result = ParseDiseaseDataBounded("data/test_mini_jhu.csv", 0, -1);
    EXPECT_TRUE(!result.hotspots.empty());
    EXPECT_TRUE(!result.date_headers.empty());

    // Bounded parse should truncate history length.
    if (!result.date_headers.empty()) {
        int end = std::min(2, static_cast<int>(result.date_headers.size()) - 1);
        auto bounded = ParseDiseaseDataBounded("data/test_mini_jhu.csv", 0, end);
        EXPECT_TRUE(static_cast<int>(bounded.date_headers.size()) == end + 1);
        if (!bounded.hotspots.empty()) {
            EXPECT_TRUE(static_cast<int>(bounded.hotspots[0].cases_history.size()) == end + 1);
        }
    }
}

static void test_deterministic_stepping() {
    SimConfig cfg;
    cfg.mode = SimMode::QUANTUM;
    cfg.system_type_2d = SystemType::OPEN;
    cfg.resolution = 20;
    cfg.landscape_path = "data/missing_should_fallback.asc";  // triggers waterless fallback
    cfg.min_lat = 24.0f;
    cfg.max_lat = 50.0f;
    cfg.min_lon = -125.0f;
    cfg.max_lon = -66.0f;
    cfg.quantum_ticks_per_real_tick = 1;
    cfg.mobility_rate = 0.1f;
    cfg.nodal_retention = true;

    auto data = ParseDiseaseDataBounded("data/test_mini_jhu.csv", 0, 5);
    EXPECT_TRUE(!data.hotspots.empty());

    Simulator2D a(cfg);
    Simulator2D b(cfg);
    a.inject_dataset(data.hotspots);
    b.inject_dataset(data.hotspots);

    for (int i = 0; i < 10; i++) {
        a.update();
        b.update();
    }

    std::vector<float> pa, pb;
    a.get_probabilities(pa);
    b.get_probabilities(pb);
    EXPECT_TRUE(pa.size() == pb.size());
    for (size_t i = 0; i < pa.size(); i++) {
        EXPECT_NEAR(pa[i], pb[i], 1e-6);
    }
    EXPECT_TRUE(a.state_is_finite());
}

static void test_day_zero_alignment() {
    SimConfig cfg;
    cfg.mode = SimMode::QUANTUM;
    cfg.system_type_2d = SystemType::OPEN;
    cfg.resolution = 20;
    cfg.landscape_path = "data/missing_should_fallback.asc";
    cfg.min_lat = 24.0f;
    cfg.max_lat = 50.0f;
    cfg.min_lon = -125.0f;
    cfg.max_lon = -66.0f;

    auto data = ParseDiseaseDataBounded("data/test_mini_jhu.csv", 0, 3);
    Simulator2D sim(cfg);
    sim.inject_dataset(data.hotspots);
    EXPECT_TRUE(sim.current_step == 0);

    sim.rebuild_historical_probs_for_day(0);
    float mse0 = sim.compute_legacy_masked_mse();
    float emd0 = sim.compute_legacy_marginal_emd();
    EXPECT_TRUE(std::isfinite(mse0));
    EXPECT_TRUE(std::isfinite(emd0));

    // After one update without tracking flags, histories stay empty.
    sim.update();
    EXPECT_TRUE(sim.masked_mse_history.empty());
    EXPECT_TRUE(sim.current_step == 1);
}

static void test_legacy_metric_parity_flags() {
    SimConfig cfg;
    cfg.mode = SimMode::QUANTUM;
    cfg.system_type_2d = SystemType::OPEN;
    cfg.resolution = 16;
    cfg.landscape_path = "data/missing_should_fallback.asc";
    cfg.min_lat = 24.0f;
    cfg.max_lat = 50.0f;
    cfg.min_lon = -125.0f;
    cfg.max_lon = -66.0f;
    cfg.quantum_ticks_per_real_tick = 1;

    auto data = ParseDiseaseDataBounded("data/test_mini_jhu.csv", 0, 4);
    Simulator2D sim(cfg);
    sim.inject_dataset(data.hotspots);
    sim.track_masked_mse = true;
    sim.track_emd = true;

    sim.update();
    EXPECT_TRUE(sim.masked_mse_history.size() == 1);
    EXPECT_TRUE(sim.emd_history.size() == 1);

    // Manual recompute for same day index used by update (current_step=1 -> day 1).
    sim.rebuild_historical_probs_for_day(1);
    EXPECT_NEAR(sim.masked_mse_history.back(), sim.compute_legacy_masked_mse(), 1e-5);
    EXPECT_NEAR(sim.emd_history.back(), sim.compute_legacy_marginal_emd(), 1e-5);
}

int main() {
    test_config_validation();
    test_date_index_and_bounds();
    test_parse_mini_dataset();
    test_deterministic_stepping();
    test_day_zero_alignment();
    test_legacy_metric_parity_flags();

    if (g_failures == 0) {
        std::cout << "All engine tests passed.\n";
        return 0;
    }
    std::cerr << g_failures << " assertion(s) failed.\n";
    return 1;
}
