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

struct DiseaseParseResult {
    std::vector<ViralHotspot> hotspots;
    std::vector<std::string> date_headers;  // calendar dates for cases_history indices
    int first_date_column = -1;
    int start_day_index = 0;
    int end_day_index = -1;  // inclusive absolute day index from first date column; -1 = through end
};

// GUI-compatible entry point (preserves prior behavior: start_day_index offset, through EOF)
std::vector<ViralHotspot> ParseDiseaseData(const std::string& filepath, int time_step_mode, int start_day_index);

// Date-aware parser: start/end are absolute day indices from the first date column (0 = first date).
// If end_day_index < 0, reads through the last column.
DiseaseParseResult ParseDiseaseDataBounded(const std::string& filepath,
                                           int start_day_index,
                                           int end_day_index = -1);

// Resolve a calendar date string like "2/21/20" or "02/21/2020" to a day index, or -1 if not found.
int FindDateIndex(const std::vector<std::string>& date_headers, const std::string& date_str);

// Normalize date strings for comparison (M/D/YY vs M/D/YYYY).
std::string NormalizeDateKey(const std::string& date_str);

std::vector<GeoNode> ParsePopulationData(const std::string& filepath);
