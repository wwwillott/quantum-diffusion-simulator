#include "DataParser.h"
#include <fstream>
#include <iostream>

std::vector<ViralHotspot> ParseDiseaseData(const std::string& filepath, int time_step_mode) {
    std::vector<ViralHotspot> hotspots;
    std::ifstream file(filepath);
    std::string line;

    if (!file.is_open()) {
        std::cerr << "ERROR: Could not open dataset at " << filepath << std::endl;
        return hotspots; // Return empty if file fails
    }

    // 1. Skip the Header Row
    std::getline(file, line); 

    // 2. Read line by line
    while (std::getline(file, line)) {
        std::vector<std::string> columns;
        std::string current_cell = "";
        bool in_quotes = false;

        // 3. The State-Machine Tokenizer
        for (char c : line) {
            if (c == '"') {
                in_quotes = !in_quotes; 
            } 
            else if (c == ',' && !in_quotes) {
                columns.push_back(current_cell);
                current_cell = "";
            } 
            else {
                current_cell += c;
            }
        }
        columns.push_back(current_cell); 

        // 4. Data Extraction & Safety Checks
        if (columns.size() == 3) {
            // It's our custom test file format: [0]Lat, [1]Lon, [2]Cases
            try {
                float lat = std::stof(columns[0]);
                float lon = std::stof(columns[1]);
                int cases = std::stoi(columns[2]);
                hotspots.push_back({lat, lon, cases});
            } 
            catch (...) { continue; }
        }
        else if (columns.size() > 4) {
            // It's the standard JHU format: [0]Prov, [1]Country, [2]Lat, [3]Lon, [4+]Dates
            if (columns[2].empty() || columns[3].empty()) continue;

            try {
                float lat = std::stof(columns[2]);
                float lon = std::stof(columns[3]);
                
                // --- THE TRUE DELTA AGGREGATOR ---
                // (No more random numbers!)
                int start_col = 4; // The first day of recorded data
                int step_size = 1; // Default to Daily
                
                if (time_step_mode == 1) step_size = 7; // Weekly
                else if (time_step_mode == 2) step_size = 30; // Monthly
                
                int end_col = start_col + step_size;
                
                // Safety check: Make sure the CSV actually has enough days
                if (end_col >= columns.size()) {
                    end_col = columns.size() - 1; 
                }

                int start_cases = std::stoi(columns[start_col]);
                int end_cases = std::stoi(columns[end_col]);
                
                // Calculate the true delta (new cases only)
                int new_cases = end_cases - start_cases;
                
                // JHU occasionally adjusts case counts downward. 
                // Clamp it to 0 so we don't inject negative probabilities.
                if (new_cases < 0) new_cases = 0;

                hotspots.push_back({lat, lon, new_cases});
            } 
            catch (...) { continue; }
        }
    }

    return hotspots;
}