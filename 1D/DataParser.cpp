#include "DataParser.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

// Helper function to safely split CSV lines that contain quotation marks
std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> result;
    std::string current_field;
    bool inside_quotes = false;

    for (char c : line) {
        if (c == '\"') {
            inside_quotes = !inside_quotes;
        } else if (c == ',' && !inside_quotes) {
            result.push_back(current_field);
            current_field.clear();
        } else {
            current_field += c;
        }
    }
    result.push_back(current_field);
    return result;
}

std::vector<ViralHotspot> ParseDiseaseData(const std::string& filepath, int timeStepMode, int start_day_index) {
    std::vector<ViralHotspot> dataset;
    std::ifstream file(filepath);
    
    if (!file.is_open()) {
        std::cerr << "CRITICAL ERROR: Could not open JHU dataset at " << filepath << std::endl;
        return dataset;
    }

    std::string line;
    
    if (!std::getline(file, line)) return dataset;
    
    std::vector<std::string> headers = split_csv_line(line);
    int lat_idx = -1;
    int lon_idx = -1;
    int first_date_idx = -1;

    for (size_t i = 0; i < headers.size(); i++) {
        std::string h = headers[i];
        h.erase(std::remove(h.begin(), h.end(), '\r'), h.end()); 
        
        if (h == "Lat") lat_idx = i;
        else if (h == "Long" || h == "Long_") lon_idx = i;
        
        if (first_date_idx == -1 && !h.empty() && std::isdigit(h[0])) {
            first_date_idx = i;
        }
    }

    if (lat_idx == -1 || lon_idx == -1 || first_date_idx == -1) {
        std::cerr << "CRITICAL ERROR: Failed to map JHU columns. Check CSV format." << std::endl;
        return dataset;
    }

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        std::vector<std::string> fields = split_csv_line(line);
        if (fields.size() <= first_date_idx) continue;

        try {
            if (fields[lat_idx].empty() || fields[lon_idx].empty()) continue;

            ViralHotspot spot;
            spot.lat = std::stof(fields[lat_idx]);
            spot.lon = std::stof(fields[lon_idx]);
            
            // --- NEW: THE FULL TIMELINE EXTRACTION ---
            int seed_day_index = first_date_idx + start_day_index;
            if (seed_day_index >= fields.size()) seed_day_index = fields.size() - 1;
            if (seed_day_index < first_date_idx) seed_day_index = first_date_idx;
            
            bool has_cases = false;
            
            // Scrape every single remaining day in the CSV
            for (size_t col = seed_day_index; col < fields.size(); col++) {
                std::string cases_str = fields[col];
                cases_str.erase(std::remove(cases_str.begin(), cases_str.end(), '\r'), cases_str.end());
                
                int count = cases_str.empty() ? 0 : std::stoi(cases_str);
                spot.cases_history.push_back(count);
                
                if (count > 0) has_cases = true;
            }

            if (has_cases) {
                dataset.push_back(spot);
            }
        } 
        catch (const std::exception& e) {
            continue;
        }
    }

    return dataset;
}

std::vector<GeoNode> ParsePopulationData(const std::string& filepath) {
    std::vector<GeoNode> dataset;
    std::ifstream file(filepath);
    
    if (!file.is_open()) {
        std::cerr << "CRITICAL ERROR: Could not open Population dataset at " << filepath << std::endl;
        return dataset;
    }

    std::string line;
    if (!std::getline(file, line)) return dataset;
    
    std::vector<std::string> headers = split_csv_line(line);
    int lat_idx = -1;
    int lon_idx = -1;
    int pop_idx = -1;

    for (size_t i = 0; i < headers.size(); i++) {
        std::string h = headers[i];
        h.erase(std::remove(h.begin(), h.end(), '\r'), h.end()); 
        
        if (h == "Lat") lat_idx = i;
        else if (h == "Long" || h == "Long_") lon_idx = i;
        else if (h == "Population") pop_idx = i;
    }

    if (lat_idx == -1 || lon_idx == -1 || pop_idx == -1) return dataset;

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::vector<std::string> fields = split_csv_line(line);
        
        int max_req_idx = std::max({lat_idx, lon_idx, pop_idx});
        if (fields.size() <= max_req_idx) continue;

        try {
            if (fields[lat_idx].empty() || fields[lon_idx].empty() || fields[pop_idx].empty()) continue;

            GeoNode node;
            node.lat = std::stof(fields[lat_idx]);
            node.lon = std::stof(fields[lon_idx]);
            
            std::string pop_str = fields[pop_idx];
            pop_str.erase(std::remove(pop_str.begin(), pop_str.end(), '\r'), pop_str.end());
            
            node.population = std::stoi(pop_str);
            if (node.population > 0) dataset.push_back(node);
        } 
        catch (const std::exception& e) { continue; }
    }
    return dataset;
}