#pragma once
#include <string>
#include <vector>

struct ViralHotspot {
    float lat;
    float lon;
    std::vector<int> cases_history; 
};

struct GeoNode {
    float lat;
    float lon;
    int population;
};

// Function declarations
std::vector<ViralHotspot> ParseDiseaseData(const std::string& filepath, int time_step_mode, int start_day_index);
std::vector<GeoNode> ParsePopulationData(const std::string& filepath);