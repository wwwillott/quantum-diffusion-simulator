#include "DataParser.h"
#include "SimConfig.h"
#include "Simulator2D.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct RunArgs {
    std::string dataset = "data/time_series_covid19_confirmed_US.csv";
    std::string landscape = "data/nasa_pop.asc";
    std::string out_dir = "results/tmp_run";
    std::string start_date = "2/21/20";
    std::string end_date = "6/30/20";
    int resolution = 100;
    float mobility_rate = 0.10f;
    float base_survival_rate = 0.95f;
    float urban_multiplier = 0.20f;
    int ticks_per_day = 1;
    int days_per_tick = 1;
    bool nodal_retention = true;
    std::string unitary_coin = "GROVER";
    std::string init_state = "ALTERNATING_PHASE";
    std::string boundary = "ABSORBING";
    std::string mode = "QUANTUM";
    float seed_keep_fraction = 1.0f;
    float min_lat = 24.0f;
    float max_lat = 50.0f;
    float min_lon = -125.0f;
    float max_lon = -66.0f;
    bool export_fields = true;
};

void usage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " [options]\n"
        << "  --dataset PATH\n"
        << "  --landscape PATH\n"
        << "  --out DIR\n"
        << "  --start-date M/D/YY\n"
        << "  --end-date M/D/YY\n"
        << "  --resolution N\n"
        << "  --mobility F\n"
        << "  --base-survival F\n"
        << "  --urban-multiplier F\n"
        << "  --ticks-per-day N\n"
        << "  --days-per-tick N\n"
        << "  --nodal-retention 0|1\n"
        << "  --unitary-coin GROVER|DFT|HADAMARD_TENSOR|ALTERNATING_DFT\n"
        << "  --init-state PURE_NORTH|UNIFORM|ALTERNATING_PHASE|CHIRAL_WEST|HADAMARD_SYMMETRIC\n"
        << "  --boundary ABSORBING|REFLECTIVE\n"
        << "  --mode QUANTUM|CLASSICAL\n"
        << "  --seed-keep-fraction F   (physics seeds only; default 1.0)\n"
        << "  --min-lat F --max-lat F --min-lon F --max-lon F\n"
        << "  --no-export-fields\n";
}

UnitaryCoin2D parse_coin(const std::string& s) {
    if (s == "GROVER") return UnitaryCoin2D::GROVER;
    if (s == "DFT") return UnitaryCoin2D::DFT;
    if (s == "HADAMARD_TENSOR") return UnitaryCoin2D::HADAMARD_TENSOR;
    if (s == "ALTERNATING_DFT") return UnitaryCoin2D::ALTERNATING_DFT;
    throw std::invalid_argument("unknown unitary coin: " + s);
}

InitialState2D parse_init(const std::string& s) {
    if (s == "PURE_NORTH") return InitialState2D::PURE_NORTH;
    if (s == "UNIFORM") return InitialState2D::UNIFORM;
    if (s == "ALTERNATING_PHASE") return InitialState2D::ALTERNATING_PHASE;
    if (s == "CHIRAL_WEST") return InitialState2D::CHIRAL_WEST;
    if (s == "HADAMARD_SYMMETRIC") return InitialState2D::HADAMARD_SYMMETRIC;
    throw std::invalid_argument("unknown init state: " + s);
}

BoundaryType parse_boundary(const std::string& s) {
    if (s == "ABSORBING") return BoundaryType::ABSORBING;
    if (s == "REFLECTIVE") return BoundaryType::REFLECTIVE;
    throw std::invalid_argument("unknown boundary: " + s);
}

SimMode parse_mode(const std::string& s) {
    if (s == "QUANTUM") return SimMode::QUANTUM;
    if (s == "CLASSICAL") return SimMode::CLASSICAL;
    throw std::invalid_argument("unknown mode: " + s);
}

bool parse_args(int argc, char** argv, RunArgs& args) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto need = [&](const char* name) -> std::string {
            if (i + 1 >= argc) throw std::invalid_argument(std::string("missing value for ") + name);
            return argv[++i];
        };
        try {
            if (a == "--dataset") args.dataset = need("--dataset");
            else if (a == "--landscape") args.landscape = need("--landscape");
            else if (a == "--out") args.out_dir = need("--out");
            else if (a == "--start-date") args.start_date = need("--start-date");
            else if (a == "--end-date") args.end_date = need("--end-date");
            else if (a == "--resolution") args.resolution = std::stoi(need("--resolution"));
            else if (a == "--mobility") args.mobility_rate = std::stof(need("--mobility"));
            else if (a == "--base-survival") args.base_survival_rate = std::stof(need("--base-survival"));
            else if (a == "--urban-multiplier") args.urban_multiplier = std::stof(need("--urban-multiplier"));
            else if (a == "--ticks-per-day") args.ticks_per_day = std::stoi(need("--ticks-per-day"));
            else if (a == "--days-per-tick") args.days_per_tick = std::stoi(need("--days-per-tick"));
            else if (a == "--nodal-retention") args.nodal_retention = std::stoi(need("--nodal-retention")) != 0;
            else if (a == "--unitary-coin") args.unitary_coin = need("--unitary-coin");
            else if (a == "--init-state") args.init_state = need("--init-state");
            else if (a == "--boundary") args.boundary = need("--boundary");
            else if (a == "--mode") args.mode = need("--mode");
            else if (a == "--seed-keep-fraction") args.seed_keep_fraction = std::stof(need("--seed-keep-fraction"));
            else if (a == "--min-lat") args.min_lat = std::stof(need("--min-lat"));
            else if (a == "--max-lat") args.max_lat = std::stof(need("--max-lat"));
            else if (a == "--min-lon") args.min_lon = std::stof(need("--min-lon"));
            else if (a == "--max-lon") args.max_lon = std::stof(need("--max-lon"));
            else if (a == "--no-export-fields") args.export_fields = false;
            else if (a == "--help" || a == "-h") {
                usage(argv[0]);
                return false;
            } else {
                throw std::invalid_argument("unknown argument: " + a);
            }
        } catch (const std::exception& e) {
            std::cerr << "Argument error: " << e.what() << "\n";
            usage(argv[0]);
            return false;
        }
    }
    return true;
}

void write_f32_bin(const std::string& path, const std::vector<float>& data) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("failed to write " + path);
    uint32_t n = static_cast<uint32_t>(data.size());
    out.write(reinterpret_cast<const char*>(&n), sizeof(n));
    out.write(reinterpret_cast<const char*>(data.data()), sizeof(float) * data.size());
}

void write_json_escape(std::ostream& os, const std::string& s) {
    os << '"';
    for (char c : s) {
        if (c == '"' || c == '\\') os << '\\' << c;
        else if (c == '\n') os << "\\n";
        else os << c;
    }
    os << '"';
}

}  // namespace

int main(int argc, char** argv) {
    RunArgs args;
    if (!parse_args(argc, argv, args)) return 2;

    auto t0 = std::chrono::steady_clock::now();

    // Peek date headers across full file to resolve calendar dates to absolute indices.
    DiseaseParseResult header_probe = ParseDiseaseDataBounded(args.dataset, 0, -1);
    if (header_probe.date_headers.empty()) {
        std::cerr << "ERROR: failed to read date headers from dataset\n";
        return 1;
    }

    int start_idx = FindDateIndex(header_probe.date_headers, args.start_date);
    int end_idx = FindDateIndex(header_probe.date_headers, args.end_date);
    if (start_idx < 0 || end_idx < 0 || end_idx < start_idx) {
        std::cerr << "ERROR: could not resolve start/end dates in dataset: "
                  << args.start_date << " .. " << args.end_date << "\n";
        return 1;
    }

    DiseaseParseResult data = ParseDiseaseDataBounded(args.dataset, start_idx, end_idx);
    if (data.hotspots.empty()) {
        std::cerr << "ERROR: no hotspots in selected window\n";
        return 1;
    }

    const int num_days = static_cast<int>(data.date_headers.size());
    if (num_days < 1) {
        std::cerr << "ERROR: empty date window\n";
        return 1;
    }

    SimConfig cfg;
    cfg.mode = parse_mode(args.mode);
    cfg.system_type_2d = SystemType::OPEN;
    cfg.resolution = args.resolution;
    cfg.mobility_rate = args.mobility_rate;
    cfg.base_survival_rate = args.base_survival_rate;
    cfg.urban_multiplier = args.urban_multiplier;
    cfg.quantum_ticks_per_real_tick = args.ticks_per_day;
    cfg.days_per_tick = args.days_per_tick;
    cfg.nodal_retention = args.nodal_retention;
    cfg.unitary_coin = parse_coin(args.unitary_coin);
    cfg.init_state_2d = parse_init(args.init_state);
    cfg.boundary_condition = parse_boundary(args.boundary);
    cfg.seed_keep_fraction = args.seed_keep_fraction;
    cfg.min_lat = args.min_lat;
    cfg.max_lat = args.max_lat;
    cfg.min_lon = args.min_lon;
    cfg.max_lon = args.max_lon;
    cfg.start_day_index = start_idx;
    cfg.landscape_path = args.landscape;
    cfg.steps_per_frame = 1;

    try {
        cfg.validate();
    } catch (const std::exception& e) {
        std::cerr << "ERROR: invalid config: " << e.what() << "\n";
        return 1;
    }

    // Ensure output directory exists (best-effort; Python creates it normally).
#ifdef _WIN32
    _mkdir(args.out_dir.c_str());
#else
    std::string mkdir_cmd = "mkdir -p \"" + args.out_dir + "\"";
    (void)std::system(mkdir_cmd.c_str());
#endif

    Simulator2D sim(cfg);
    sim.inject_dataset(data.hotspots);
    sim.current_step = 0;

    std::ofstream metrics_csv(args.out_dir + "/legacy_metrics.csv");
    if (!metrics_csv) {
        std::cerr << "ERROR: cannot write metrics csv\n";
        return 1;
    }
    metrics_csv << "day_index,date,sim_step,legacy_masked_mse,legacy_marginal_emd,total_prob,max_seed_cases,max_historical_cases\n";

    std::vector<float> sim_probs;
    std::vector<float> hist_probs;

    auto sample_day = [&](int day_index, const std::string& date_label) -> bool {
        sim.rebuild_historical_probs_for_day(day_index);
        if (!sim.state_is_finite()) {
            std::cerr << "ERROR: non-finite state at day " << day_index << "\n";
            return false;
        }

        float mse = sim.compute_legacy_masked_mse();
        float emd = sim.compute_legacy_marginal_emd();
        float tp = sim.total_probability();

        if (!std::isfinite(mse) || !std::isfinite(emd) || !std::isfinite(tp)) {
            std::cerr << "ERROR: non-finite metrics at day " << day_index << "\n";
            return false;
        }

        metrics_csv << day_index << "," << date_label << "," << sim.current_step << ","
                    << mse << "," << emd << "," << tp << ","
                    << sim.max_seed_cases << "," << sim.max_historical_cases << "\n";

        if (args.export_fields) {
            sim.get_probabilities(sim_probs);
            sim.get_historical_probs(hist_probs);
            std::ostringstream sp, hp;
            sp << args.out_dir << "/sim_day_" << day_index << ".f32";
            hp << args.out_dir << "/hist_day_" << day_index << ".f32";
            write_f32_bin(sp.str(), sim_probs);
            write_f32_bin(hp.str(), hist_probs);
        }
        return true;
    };

    // Day 0: seeded state before any updates.
    if (!sample_day(0, data.date_headers[0])) return 1;

    for (int day = 1; day < num_days; day++) {
        // days_per_tick>1: advance physics only every N calendar days (slower sim vs data).
        if (day % args.days_per_tick == 0) {
            for (int t = 0; t < args.ticks_per_day; t++) {
                sim.update();
                if (!sim.state_is_finite()) {
                    std::cerr << "ERROR: non-finite state during day " << day << " tick " << t << "\n";
                    return 1;
                }
            }
        }
        if (!sample_day(day, data.date_headers[day])) return 1;
    }

    auto t1 = std::chrono::steady_clock::now();
    double elapsed =
        std::chrono::duration_cast<std::chrono::duration<double>>(t1 - t0).count();

    std::ofstream meta(args.out_dir + "/run_meta.json");
    if (!meta) {
        std::cerr << "ERROR: cannot write run_meta.json\n";
        return 1;
    }
    meta << "{\n";
    meta << "  \"status\": \"ok\",\n";
    meta << "  \"elapsed_seconds\": " << elapsed << ",\n";
    meta << "  \"num_days\": " << num_days << ",\n";
    meta << "  \"start_date\": ";
    write_json_escape(meta, args.start_date);
    meta << ",\n  \"end_date\": ";
    write_json_escape(meta, args.end_date);
    meta << ",\n  \"start_day_index\": " << start_idx << ",\n";
    meta << "  \"end_day_index\": " << end_idx << ",\n";
    meta << "  \"resolution\": " << args.resolution << ",\n";
    meta << "  \"ticks_per_day\": " << args.ticks_per_day << ",\n";
    meta << "  \"days_per_tick\": " << args.days_per_tick << ",\n";
    meta << "  \"mobility_rate\": " << args.mobility_rate << ",\n";
    meta << "  \"base_survival_rate\": " << args.base_survival_rate << ",\n";
    meta << "  \"urban_multiplier\": " << args.urban_multiplier << ",\n";
    meta << "  \"nodal_retention\": " << (args.nodal_retention ? "true" : "false") << ",\n";
    meta << "  \"unitary_coin\": ";
    write_json_escape(meta, args.unitary_coin);
    meta << ",\n  \"init_state\": ";
    write_json_escape(meta, args.init_state);
    meta << ",\n  \"boundary\": ";
    write_json_escape(meta, args.boundary);
    meta << ",\n  \"mode\": ";
    write_json_escape(meta, args.mode);
    meta << ",\n  \"seed_keep_fraction\": " << args.seed_keep_fraction << ",\n";
    meta << "  \"max_seed_cases\": " << sim.max_seed_cases << ",\n";
    meta << "  \"max_historical_cases\": " << sim.max_historical_cases << ",\n";
    meta << "  \"min_lat\": " << args.min_lat << ",\n";
    meta << "  \"max_lat\": " << args.max_lat << ",\n";
    meta << "  \"min_lon\": " << args.min_lon << ",\n";
    meta << "  \"max_lon\": " << args.max_lon << ",\n";
    meta << "  \"hotspot_count\": " << data.hotspots.size() << "\n";
    meta << "}\n";

    std::cout << "OK days=" << num_days << " elapsed_s=" << elapsed << " out=" << args.out_dir
              << "\n";
    return 0;
}
