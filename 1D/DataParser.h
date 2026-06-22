#pragma once
#include <string>
#include <vector>

// The clean data container
struct ViralHotspot {
    float lat;
    float lon;
    int confirmed_cases; 
};

// The parser function declaration
std::vector<ViralHotspot> ParseDiseaseData(const std::string& filepath, int time_step_mode);