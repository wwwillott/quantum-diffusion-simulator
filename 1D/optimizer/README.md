# quantum-sweep

Headless parameter characterization for the 2D quantum epidemic simulator.

## What it does

1. Runs standardized **QUANTUM + OPEN** simulations over a fixed calendar window.
2. Reports **legacy** metrics (GUI-compatible masked MSE and marginal EMD) plus **corrected** metrics (dimensionless masked NMSE, total-case mass error, and geographic-km Sinkhorn EMD).
3. Selects a **Pareto-knee baseline** from a deterministic pilot, then runs **one-factor-at-a-time (OFAT)** sweeps.
4. Runs a separate **resolution / runtime** study (not ranked with model parameters).
5. Resumes completed runs via content hashes; failures are recorded and the sweep continues.

## Build the C++ headless runner

From `1D/`:

```bash
clang++ -std=c++17 -pthread -O2 -I. \
  Simulator2D.cpp DataParser.cpp headless_runner.cpp \
  -o headless_runner
```

Or with CMake (if installed):

```bash
cmake -S . -B build -DBUILD_GUI=ON -DBUILD_TESTS=ON
cmake --build build -j
# binary: build/headless_runner
```

Optional GUI (requires raylib): `build/diffusion_sim` or `./build.sh`.

## Install the Python CLI

```bash
cd optimizer
python3 -m venv .venv
source .venv/bin/activate
pip install -e ".[dev]"
```

## Quick start (mini fixture)

The checked-in [`configs/default_sweep.toml`](configs/default_sweep.toml) uses the mini JHU-like fixture and a **synthetic landscape fallback** (missing ASC path) so local smoke runs stay fast. For production US sweeps, use `us_full_sweep.toml` with `data/nasa_pop.asc`.

Projected NASA landscapes are cached under `data/.landscape_cache/` after the first load.

```bash
# from optimizer/ with venv active; headless_runner built in 1D/
quantum-sweep --config configs/default_sweep.toml --jobs 1
```

Outputs land in `1D/results/sweeps/` (see config `results_dir`):

| Artifact | Meaning |
|----------|---------|
| `manifest.json` | Full provenance, metric definitions, limitations |
| `run_summary.csv` | One row per run with mean metrics |
| `selected_baseline.json` | Pareto-knee baseline after pilot |
| `skipped_runs.json` | Inactive/no-op OFAT combinations |
| `runs/<hash>/daily_metrics.csv` | Per-day legacy + corrected metrics |
| `runs/<hash>/legacy_metrics.csv` | Raw C++ legacy series |
| `runs/<hash>/*.f32` | Native-grid sim/hist fields |
| `plots/*.png` | Pareto, OFAT responses, trajectories, resolution |

## Production US window

Use [`configs/us_full_sweep.toml`](configs/us_full_sweep.toml):

- Dataset: `data/time_series_covid19_confirmed_US.csv`
- Window: **2020-02-21 through 2020-06-30** (inclusive)
- Bounds: continental US
- Pilot: 128 samples; OFAT ranges as agreed in the design discussion
- Requires `data/nasa_pop.asc` for realistic landscape (synthetic capacity fallback if missing)

```bash
quantum-sweep --config configs/us_full_sweep.toml --jobs 4
```

## Metric definitions

| Name | Definition |
|------|------------|
| **legacy_masked_mse** | Mean squared error in raw case units on cells with historical mass (GUI formula) |
| **legacy_marginal_emd** | Sum of 1D Wasserstein distances on row and column marginals (not true 2D OT) |
| **corrected_nmse** | `mean((sim-hist)^2) / mean(hist^2)` on `hist>0` cells |
| **mass_error** | `(sum_sim - sum_hist) / sum_hist` |
| **sinkhorn_emd_km** | Entropic Sinkhorn W₁ with pairwise **km** costs on a downsampled evaluation grid (POT) |

`ticks_per_day` = quantum updates per calendar day (sim faster when > 1).  
`days_per_tick` = calendar days per update group (sim slower when > 1; e.g. 2 means one tick covers two real days).

Ranking uses **mean daily corrected NMSE** and **mean daily Sinkhorn EMD**. Initial/final/min/max are also stored.

## Conditional / inactive parameters

- `mobility_rate` is skipped in OFAT when `nodal_retention=false` (no effect).
- `non_unitary_coin` is a dead enum and is not swept.
- `resolution` is only varied in the resolution study.

## Scientific limitations

- JHU series are **cumulative** confirmed cases, not daily incidence.
- Sinkhorn EMD is an **approximation** on a fixed evaluation resolution with entropic regularization (recorded in the manifest).
- Legacy EMD remains for GUI parity only.
- Changing resolution changes discretization, county collisions, and cost geometry; do not treat it like a epidemiology knob.

## Tests

```bash
# C++ (from 1D/)
clang++ -std=c++17 -pthread -O2 -I. Simulator2D.cpp DataParser.cpp tests/engine_tests.cpp -o engine_tests
./engine_tests

# Python
cd optimizer && pytest -q
```
