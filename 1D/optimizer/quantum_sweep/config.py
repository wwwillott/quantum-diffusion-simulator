from __future__ import annotations

import math
import tomllib
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any


NUMERIC_PARAMS = (
    "mobility_rate",
    "base_survival_rate",
    "urban_multiplier",
    "ticks_per_day",
    "days_per_tick",
    "resolution",
)

CATEGORICAL_PARAMS = (
    "unitary_coin",
    "init_state",
    "boundary",
    "nodal_retention",
)


@dataclass
class SweepConfig:
    dataset: str
    landscape: str
    start_date: str
    end_date: str
    min_lat: float
    max_lat: float
    min_lon: float
    max_lon: float
    headless_bin: str
    results_dir: str
    jobs: int = 1
    export_fields: bool = True
    baseline: dict[str, Any] = field(default_factory=dict)
    pilot_enabled: bool = True
    pilot_n_samples: int = 128
    pilot_seed: int = 42
    sinkhorn_reg: float = 0.1
    sinkhorn_num_iter_max: int = 2000
    sinkhorn_stop_threshold: float = 1e-6
    sinkhorn_eval_resolution: int = 50
    ofat: dict[str, Any] = field(default_factory=dict)
    resolution_study_enabled: bool = True
    resolution_study_values: list[int] = field(default_factory=list)
    config_path: str = ""

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


def _expand_spec(spec: Any) -> list[Any]:
    if isinstance(spec, dict):
        if "values" in spec:
            return list(spec["values"])
        if {"start", "stop", "step"} <= set(spec.keys()):
            start = float(spec["start"])
            stop = float(spec["stop"])
            step = float(spec["step"])
            if step == 0:
                raise ValueError("step must be non-zero")
            values: list[Any] = []
            # Inclusive stop with float tolerance
            n = int(math.floor((stop - start) / step + 1e-9)) + 1
            for i in range(max(n, 0)):
                v = start + i * step
                if step > 0 and v > stop + abs(step) * 1e-9:
                    break
                if step < 0 and v < stop - abs(step) * 1e-9:
                    break
                # Prefer ints when whole
                if abs(v - round(v)) < 1e-9:
                    values.append(int(round(v)))
                else:
                    values.append(round(v, 10))
            return values
        raise ValueError(f"unsupported range spec: {spec}")
    if isinstance(spec, list):
        return list(spec)
    return [spec]


def expand_param_grid(ofat: dict[str, Any]) -> dict[str, list[Any]]:
    return {name: _expand_spec(spec) for name, spec in ofat.items()}


def load_config(path: str | Path) -> SweepConfig:
    path = Path(path).resolve()
    with path.open("rb") as f:
        raw = tomllib.load(f)

    run = raw.get("run", {})
    baseline = dict(raw.get("baseline", {}))
    pilot = raw.get("pilot", {})
    sinkhorn = raw.get("sinkhorn", {})
    ofat = dict(raw.get("ofat", {}))
    res = raw.get("resolution_study", {})

    cfg = SweepConfig(
        dataset=str(run.get("dataset", "data/time_series_covid19_confirmed_US.csv")),
        landscape=str(run.get("landscape", "data/nasa_pop.asc")),
        start_date=str(run.get("start_date", "2/21/20")),
        end_date=str(run.get("end_date", "6/30/20")),
        min_lat=float(run.get("min_lat", 24.0)),
        max_lat=float(run.get("max_lat", 50.0)),
        min_lon=float(run.get("min_lon", -125.0)),
        max_lon=float(run.get("max_lon", -66.0)),
        headless_bin=str(run.get("headless_bin", "../headless_runner")),
        results_dir=str(run.get("results_dir", "../results/sweeps")),
        jobs=int(run.get("jobs", 1)),
        export_fields=bool(run.get("export_fields", True)),
        baseline=baseline,
        pilot_enabled=bool(pilot.get("enabled", True)),
        pilot_n_samples=int(pilot.get("n_samples", 128)),
        pilot_seed=int(pilot.get("seed", 42)),
        sinkhorn_reg=float(sinkhorn.get("reg", 0.1)),
        sinkhorn_num_iter_max=int(sinkhorn.get("num_iter_max", 2000)),
        sinkhorn_stop_threshold=float(sinkhorn.get("stop_threshold", 1e-6)),
        sinkhorn_eval_resolution=int(sinkhorn.get("eval_resolution", 50)),
        ofat=ofat,
        resolution_study_enabled=bool(res.get("enabled", True)),
        resolution_study_values=[int(v) for v in res.get("values", [])],
        config_path=str(path),
    )
    return cfg


def resolve_path(base: Path, p: str) -> Path:
    path = Path(p)
    if path.is_absolute():
        return path
    return (base / path).resolve()
