#pragma once

enum class SimMode {
    CLASSICAL,
    QUANTUM,
    QUANTUM_SEARCH
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
    ALTERNATING_PHASE
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
};