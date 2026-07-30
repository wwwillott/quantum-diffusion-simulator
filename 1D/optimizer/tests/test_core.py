from quantum_sweep.config import expand_param_grid, load_config
from quantum_sweep.conditional import should_skip_ofat_point
from quantum_sweep.expand import generate_ofat_runs, generate_pilot_runs, run_hash
from quantum_sweep.pareto import select_pareto_knee
from quantum_sweep.metrics import masked_case_nmse, total_case_mass_error
import numpy as np
from pathlib import Path


def test_expand_range_and_values():
    grid = expand_param_grid(
        {
            "mobility_rate": {"start": 0.0, "stop": 0.2, "step": 0.1},
            "unitary_coin": {"values": ["GROVER", "DFT"]},
        }
    )
    assert grid["mobility_rate"] == [0, 0.1, 0.2]
    assert grid["unitary_coin"] == ["GROVER", "DFT"]


def test_pilot_deterministic():
    baseline = {
        "resolution": 20,
        "mobility_rate": 0.1,
        "base_survival_rate": 0.95,
        "urban_multiplier": 0.2,
        "ticks_per_day": 1,
        "nodal_retention": True,
        "unitary_coin": "GROVER",
        "init_state": "ALTERNATING_PHASE",
        "boundary": "ABSORBING",
    }
    ofat = {
        "mobility_rate": {"values": [0.0, 0.1, 0.2]},
        "unitary_coin": {"values": ["GROVER", "DFT"]},
    }
    shared = {
        "dataset": "d.csv",
        "landscape": "l.asc",
        "start_date": "Day1",
        "end_date": "Day2",
        "min_lat": 24,
        "max_lat": 50,
        "min_lon": -125,
        "max_lon": -66,
    }
    a = generate_pilot_runs(baseline, ofat, shared, 16, 42)
    b = generate_pilot_runs(baseline, ofat, shared, 16, 42)
    assert [r["run_id"] for r in a] == [r["run_id"] for r in b]
    assert len({r["run_id"] for r in a}) == len(a)


def test_pareto_knee():
    # Ideal knee near (0.1, 0.1)
    mse = [1.0, 0.1, 0.5, 0.2]
    emd = [0.1, 0.1, 0.5, 0.8]
    assert select_pareto_knee(mse, emd) == 1


def test_ofat_skips_inactive_mobility():
    baseline = {
        "resolution": 20,
        "mobility_rate": 0.1,
        "base_survival_rate": 0.95,
        "urban_multiplier": 0.2,
        "ticks_per_day": 1,
        "nodal_retention": False,
        "unitary_coin": "GROVER",
        "init_state": "ALTERNATING_PHASE",
        "boundary": "ABSORBING",
    }
    reason = should_skip_ofat_point("mobility_rate", 0.3, baseline)
    assert reason is not None

    ofat = {"mobility_rate": {"values": [0.0, 0.3]}, "boundary": {"values": ["ABSORBING", "REFLECTIVE"]}}
    shared = {
        "dataset": "d.csv",
        "landscape": "l.asc",
        "start_date": "Day1",
        "end_date": "Day2",
        "min_lat": 24,
        "max_lat": 50,
        "min_lon": -125,
        "max_lon": -66,
    }
    runs, skipped = generate_ofat_runs(baseline, ofat, shared)
    assert any(s["varied_param"] == "mobility_rate" for s in skipped)
    assert any(r["varied_param"] == "boundary" for r in runs)


def test_nmse_and_mass():
    sim = np.array([[1.0, 0.0], [0.0, 0.0]])
    hist = np.array([[1.0, 0.0], [0.0, 0.0]])
    assert masked_case_nmse(sim, hist) == 0.0
    assert abs(total_case_mass_error(sim * 2, hist) - 1.0) < 1e-9


def test_run_hash_stable():
    a = {"resolution": 10, "mobility_rate": 0.1}
    b = {"mobility_rate": 0.1, "resolution": 10}
    assert run_hash(a) == run_hash(b)


def test_load_default_config():
    path = Path(__file__).resolve().parents[1] / "configs" / "default_sweep.toml"
    cfg = load_config(path)
    assert cfg.start_date
    assert cfg.pilot_n_samples > 0
