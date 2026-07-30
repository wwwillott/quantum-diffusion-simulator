#include "DataParser.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>

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

static std::string strip_cr(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    return s;
}

std::string NormalizeDateKey(const std::string& date_str) {
    std::string s = strip_cr(date_str);
    // Trim whitespace
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();

    int month = 0, day = 0, year = 0;
    char sep1 = 0, sep2 = 0;
    std::istringstream iss(s);
    if (!(iss >> month >> sep1 >> day >> sep2 >> year)) {
        return s;  // fallback: return cleaned original
    }
    if (year < 100) year += 2000;
    std::ostringstream oss;
    oss << year << '-' << month << '-' << day;
    return oss.str();
}

int FindDateIndex(const std::vector<std::string>& date_headers, const std::string& date_str) {
    std::string target = NormalizeDateKey(date_str);
    for (size_t i = 0; i < date_headers.size(); i++) {
        if (NormalizeDateKey(date_headers[i]) == target) {
            return static_cast<int>(i);
        }
        // Exact match for fixture labels like Day1
        if (strip_cr(date_headers[i]) == strip_cr(date_str)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

DiseaseParseResult ParseDiseaseDataBounded(const std::string& filepath,
                                           int start_day_index,
                                           int end_day_index) {
    DiseaseParseResult result;
    result.start_day_index = start_day_index;
    result.end_day_index = end_day_index;

    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "CRITICAL ERROR: Could not open JHU dataset at " << filepath << std::endl;
        return result;
    }

    std::string line;
    if (!std::getline(file, line)) return result;

    std::vector<std::string> headers = split_csv_line(line);
    int lat_idx = -1;
    int lon_idx = -1;
    int first_date_idx = -1;

    for (size_t i = 0; i < headers.size(); i++) {
        std::string h = strip_cr(headers[i]);

        if (h == "Lat") lat_idx = static_cast<int>(i);
        else if (h == "Long" || h == "Long_") lon_idx = static_cast<int>(i);

        // JHU uses calendar dates (1/22/20). Fixtures may use Day1, Day2, ...
        if (first_date_idx == -1 && !h.empty()) {
            bool looks_like_date = std::isdigit(static_cast<unsigned char>(h[0]));
            bool looks_like_day_label =
                h.size() > 3 && (h.compare(0, 3, "Day") == 0 || h.compare(0, 3, "day") == 0) &&
                std::isdigit(static_cast<unsigned char>(h[3]));
            if (looks_like_date || looks_like_day_label) {
                first_date_idx = static_cast<int>(i);
            }
        }
    }

    if (lat_idx == -1 || lon_idx == -1 || first_date_idx == -1) {
        std::cerr << "CRITICAL ERROR: Failed to map JHU columns. Check CSV format." << std::endl;
        return result;
    }

    result.first_date_column = first_date_idx;

    int last_date_col = static_cast<int>(headers.size()) - 1;
    int abs_start = first_date_idx + std::max(0, start_day_index);
    int abs_end = (end_day_index < 0) ? last_date_col : (first_date_idx + end_day_index);

    if (abs_start > last_date_col) abs_start = last_date_col;
    if (abs_end > last_date_col) abs_end = last_date_col;
    if (abs_end < abs_start) abs_end = abs_start;

    for (int col = abs_start; col <= abs_end; col++) {
        result.date_headers.push_back(strip_cr(headers[col]));
    }

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        std::vector<std::string> fields = split_csv_line(line);
        if (static_cast<int>(fields.size()) <= abs_start) continue;

        try {
            if (fields[lat_idx].empty() || fields[lon_idx].empty()) continue;

            ViralHotspot spot;
            spot.lat = std::stof(fields[lat_idx]);
            spot.lon = std::stof(fields[lon_idx]);

            bool has_cases = false;
            for (int col = abs_start; col <= abs_end; col++) {
                if (col >= static_cast<int>(fields.size())) {
                    spot.cases_history.push_back(0);
                    continue;
                }
                std::string cases_str = strip_cr(fields[col]);
                int count = cases_str.empty() ? 0 : std::stoi(cases_str);
                spot.cases_history.push_back(count);
                if (count > 0) has_cases = true;
            }

            if (has_cases) {
                result.hotspots.push_back(spot);
            }
        } catch (const std::exception&) {
            continue;
        }
    }

    return result;
}

std::vector<ViralHotspot> ParseDiseaseData(const std::string& filepath, int timeStepMode,
                                           int start_day_index) {
    (void)timeStepMode;  // unused historically; preserved for GUI compatibility
    DiseaseParseResult result = ParseDiseaseDataBounded(filepath, start_day_index, -1);
    return result.hotspots;
}

std::vector<GeoNode> ParsePopulationData(const std::string& filepath) {
    std::vector<GeoNode> dataset;
    std::ifstream file(filepath);

    if (!file.is_open()) {
        std::cerr << "CRITICAL ERROR: Could not open Population dataset at " << filepath
                  << std::endl;
        return dataset;
    }

    std::string line;
    if (!std::getline(file, line)) return dataset;

    std::vector<std::string> headers = split_csv_line(line);
    int lat_idx = -1;
    int lon_idx = -1;
    int pop_idx = -1;

    for (size_t i = 0; i < headers.size(); i++) {
        std::string h = strip_cr(headers[i]);

        if (h == "Lat") lat_idx = static_cast<int>(i);
        else if (h == "Long" || h == "Long_") lon_idx = static_cast<int>(i);
        else if (h == "Population") pop_idx = static_cast<int>(i);
    }

    if (lat_idx == -1 || lon_idx == -1 || pop_idx == -1) return dataset;

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::vector<std::string> fields = split_csv_line(line);

        int max_req_idx = std::max({lat_idx, lon_idx, pop_idx});
        if (static_cast<int>(fields.size()) <= max_req_idx) continue;

        try {
            if (fields[lat_idx].empty() || fields[lon_idx].empty() || fields[pop_idx].empty())
                continue;

            GeoNode node;
            node.lat = std::stof(fields[lat_idx]);
            node.lon = std::stof(fields[lon_idx]);

            std::string pop_str = strip_cr(fields[pop_idx]);
            node.population = std::stoi(pop_str);
            if (node.population > 0) dataset.push_back(node);
        } catch (const std::exception&) {
            continue;
        }
    }
    return dataset;
}
