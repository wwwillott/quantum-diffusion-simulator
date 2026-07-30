"""End-to-end mini sweep against the compiled headless_runner (skipped if missing)."""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
OPT = Path(__file__).resolve().parents[1]
HEADLESS = ROOT / "headless_runner"


@pytest.mark.skipif(not HEADLESS.exists(), reason="headless_runner not built")
def test_e2e_mini_sweep(tmp_path):
    # Tiny config for CI-speed
    cfg = tmp_path / "mini.toml"
    cfg.write_text(
        f"""
[run]
dataset = "{ROOT / 'data' / 'test_mini_jhu.csv'}"
landscape = "{ROOT / 'data' / 'missing.asc'}"
start_date = "Day1"
end_date = "Day4"
min_lat = 24.0
max_lat = 50.0
min_lon = -125.0
max_lon = -66.0
headless_bin = "{HEADLESS}"
results_dir = "{tmp_path / 'out'}"
jobs = 1
export_fields = true

[baseline]
resolution = 16
mobility_rate = 0.10
base_survival_rate = 0.95
urban_multiplier = 0.20
ticks_per_day = 1
nodal_retention = true
unitary_coin = "GROVER"
init_state = "ALTERNATING_PHASE"
boundary = "ABSORBING"

[pilot]
enabled = true
n_samples = 4
seed = 1

[sinkhorn]
reg = 0.2
num_iter_max = 500
stop_threshold = 1.0e-4
eval_resolution = 8

[ofat]
mobility_rate = {{ values = [0.05, 0.10] }}
unitary_coin = {{ values = ["GROVER", "DFT"] }}

[resolution_study]
enabled = true
values = [16, 20]
"""
    )

    env_python = sys.executable
    cmd = [env_python, "-m", "quantum_sweep", "--config", str(cfg), "--jobs", "1"]
    proc = subprocess.run(cmd, cwd=str(OPT), capture_output=True, text=True)
    assert proc.returncode == 0, proc.stdout + "\n" + proc.stderr
    out = tmp_path / "out"
    assert (out / "manifest.json").exists()
    assert (out / "run_summary.csv").exists()
    assert (out / "selected_baseline.json").exists()
