#pragma once

#include <cmath>
#include <stdexcept>
#include <string>

enum class SystemType {
    CLOSED_UNITARY,
    OPEN
};

enum class SimMode {
    CLASSICAL,
    QUANTUM,
    QUANTUM_SEARCH
};

enum class UnitaryCoin2D {
    GROVER,
    DFT,
    HADAMARD_TENSOR,
    ALTERNATING_DFT
};

enum class NonUnitaryCoin2D {
    EPIDEMIC_SCALAR
};

enum class CoinState {
    RIGHT_HEAVY,
    LEFT_HEAVY,
    SYMMETRIC
};

enum class InitialState2D {
    PURE_NORTH,
    UNIFORM,
    ALTERNATING_PHASE,
    CHIRAL_WEST,
    HADAMARD_SYMMETRIC
};

enum class BoundaryType {
    ABSORBING,
    REFLECTIVE
};

struct SimConfig {
    SimMode mode = SimMode::QUANTUM;
    int resolution = 100;
    int steps_per_frame = 5;
    float diffusion_rate = 0.45f;

    CoinState initial_coin = CoinState::SYMMETRIC;
    InitialState2D init_state_2d = InitialState2D::ALTERNATING_PHASE;
    BoundaryType boundary_condition = BoundaryType::ABSORBING;
    SystemType system_type_2d = SystemType::CLOSED_UNITARY;
    UnitaryCoin2D unitary_coin = UnitaryCoin2D::GROVER;
    NonUnitaryCoin2D non_unitary_coin = NonUnitaryCoin2D::EPIDEMIC_SCALAR;

    int wave_start_pos = 200;
    int target_position = -1;
    int search_target = 200;
    int num_barriers = 0;
    int barrier_positions[4] = {250, 250, 250, 250};

    float min_lat = -90.0f;
    float max_lat = 90.0f;
    float min_lon = -180.0f;
    float max_lon = 180.0f;

    int start_day_index = 30;
    int quantum_ticks_per_real_tick = 1;
    // Calendar days advanced per quantum tick group (1 = 1:1 with ticks_per_day mapping).
    // days_per_tick=2 means one sim update covers two real days (slower physics vs data).
    int days_per_tick = 1;

    bool nodal_retention = true;
    float mobility_rate = 0.10f;

    float base_survival_rate = 0.95f;
    float urban_multiplier = 0.20f;

    // Fraction of positive day-0 hotspots used for physics seeding only.
    // Historical comparison maps always use the full dataset. Default 1 = unchanged.
    float seed_keep_fraction = 1.0f;

    std::string landscape_path = "data/nasa_pop.asc";

    void validate() const {
        if (resolution < 10) {
            throw std::invalid_argument("resolution must be >= 10");
        }
        if (steps_per_frame < 1) {
            throw std::invalid_argument("steps_per_frame must be >= 1");
        }
        if (quantum_ticks_per_real_tick < 1) {
            throw std::invalid_argument("quantum_ticks_per_real_tick must be >= 1");
        }
        if (days_per_tick < 1) {
            throw std::invalid_argument("days_per_tick must be >= 1");
        }
        if (start_day_index < 0) {
            throw std::invalid_argument("start_day_index must be >= 0");
        }
        if (!(mobility_rate >= 0.0f && mobility_rate <= 1.0f) || !std::isfinite(mobility_rate)) {
            throw std::invalid_argument("mobility_rate must be in [0, 1]");
        }
        if (!std::isfinite(base_survival_rate)) {
            throw std::invalid_argument("base_survival_rate must be finite");
        }
        if (!std::isfinite(urban_multiplier)) {
            throw std::invalid_argument("urban_multiplier must be finite");
        }
        if (!(seed_keep_fraction > 0.0f && seed_keep_fraction <= 1.0f) ||
            !std::isfinite(seed_keep_fraction)) {
            throw std::invalid_argument("seed_keep_fraction must be in (0, 1]");
        }
        if (!(min_lat < max_lat) || !(min_lon < max_lon)) {
            throw std::invalid_argument("geographic bounds require min < max");
        }
        if (!std::isfinite(min_lat) || !std::isfinite(max_lat) ||
            !std::isfinite(min_lon) || !std::isfinite(max_lon)) {
            throw std::invalid_argument("geographic bounds must be finite");
        }
    }
};
