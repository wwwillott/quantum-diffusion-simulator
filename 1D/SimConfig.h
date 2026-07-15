#pragma once

enum class SystemType { 
    CLOSED_UNITARY, 
    OPEN
};

enum class SimMode {
    CLASSICAL,
    QUANTUM,
    QUANTUM_SEARCH
};

// Coins Available
// List A: The Pure Physics Coins
enum class UnitaryCoin2D { 
    GROVER, 
    DFT, 
    HADAMARD_TENSOR,
    ALTERNATING_DFT 
};

// List B: The Epidemiology Coins
enum class NonUnitaryCoin2D { 
    EPIDEMIC_SCALAR 
};

// 1D Specific
enum class CoinState { 
    RIGHT_HEAVY, 
    LEFT_HEAVY, 
    SYMMETRIC 
};

// 2D Specific
enum class InitialState2D {
    PURE_NORTH,
    UNIFORM,
    ALTERNATING_PHASE,
    CHIRAL_WEST,
    HADAMARD_SYMMETRIC
};

// Boundary Types for both
enum class BoundaryType {
    ABSORBING,
    REFLECTIVE
};

// All settings, packaged up
struct SimConfig {
    SimMode mode;
    int resolution;
    int steps_per_frame;
    float diffusion_rate;
    
    CoinState initial_coin;        
    InitialState2D init_state_2d;  
    BoundaryType boundary_condition;
    SystemType system_type_2d = SystemType::CLOSED_UNITARY;
    UnitaryCoin2D unitary_coin = UnitaryCoin2D::GROVER;
    NonUnitaryCoin2D non_unitary_coin = NonUnitaryCoin2D::EPIDEMIC_SCALAR;
    
    int wave_start_pos;
    int target_position;
    int search_target;
    int num_barriers;
    int barrier_positions[4];

    // Bounding Box Overrides
    float min_lat = -90.0f;
    float max_lat = 90.0f;
    float min_lon = -180.0f;
    float max_lon = 180.0f;

    // Virus Timeline Control
    int start_day_index = 30; // Default to Day 30
    int quantum_ticks_per_real_tick = 1; // Ratio for sim tick to real time

    // Nodal Retention Parameters
    bool nodal_retention = true; 
    float mobility_rate = 0.10f; // 10% of the wave moves, 90% stays

    // Epidemiological Scaling Parameters
    float base_survival_rate = 0.95f;
    float urban_multiplier = 0.20f;
};