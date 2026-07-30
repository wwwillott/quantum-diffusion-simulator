from __future__ import annotations

import hashlib
from typing import Any

from .config import CATEGORICAL_PARAMS, NUMERIC_PARAMS, expand_param_grid
from .conditional import should_skip_ofat_point


def canonical_run_dict(settings: dict[str, Any], shared: dict[str, Any]) -> dict[str, Any]:
    keys = [
        "dataset",
        "landscape",
        "start_date",
        "end_date",
        "min_lat",
        "max_lat",
        "min_lon",
        "max_lon",
        "resolution",
        "mobility_rate",
        "base_survival_rate",
        "urban_multiplier",
        "ticks_per_day",
        "days_per_tick",
        "nodal_retention",
        "unitary_coin",
        "init_state",
        "boundary",
    ]
    out: dict[str, Any] = {}
    for k in keys:
        if k in settings:
            out[k] = settings[k]
        elif k in shared:
            out[k] = shared[k]
    # Legacy-compatible hashing: omit days_per_tick when it is the default 1 so
    # older us_sweep run_ids remain stable.
    if out.get("days_per_tick", 1) == 1:
        out.pop("days_per_tick", None)
    return out


def run_hash(run: dict[str, Any]) -> str:
    parts = []
    for k in sorted(run.keys()):
        v = run[k]
        if isinstance(v, float):
            parts.append(f"{k}={v:.10g}")
        else:
            parts.append(f"{k}={v}")
    blob = "|".join(parts).encode("utf-8")
    return hashlib.sha256(blob).hexdigest()[:16]


def _balanced_levels(values: list[Any], n: int, seed: int) -> list[Any]:
    if not values:
        return []
    # Deterministic round-robin with seed offset
    offset = seed % len(values)
    out = []
    for i in range(n):
        out.append(values[(offset + i) % len(values)])
    return out


def generate_pilot_runs(
    baseline: dict[str, Any],
    ofat: dict[str, Any],
    shared: dict[str, Any],
    n_samples: int,
    seed: int,
) -> list[dict[str, Any]]:
    """Deterministic balanced multi-factor pilot samples."""
    grid = expand_param_grid(ofat)
    # Include only parameters present in ofat / baseline
    params = [p for p in list(NUMERIC_PARAMS) + list(CATEGORICAL_PARAMS) if p in grid or p in baseline]
    levels = {p: grid.get(p, [baseline.get(p)]) for p in params}

    runs: list[dict[str, Any]] = []
    # Latin-ish: for sample i, take level i % len for each param with staggered offsets
    for i in range(n_samples):
        settings = dict(baseline)
        for j, p in enumerate(params):
            vals = levels[p]
            if not vals:
                continue
            idx = (i * (j + 3) + seed) % len(vals)
            settings[p] = vals[idx]
        # Keep resolution fixed during pilot (resolution studied separately)
        if "resolution" in baseline:
            settings["resolution"] = baseline["resolution"]
        run = canonical_run_dict(settings, shared)
        run["phase"] = "pilot"
        run["varied_param"] = "pilot"
        run["varied_value"] = i
        run["run_id"] = run_hash(run)
        runs.append(run)

    # Deduplicate while preserving order
    seen = set()
    unique = []
    for r in runs:
        if r["run_id"] in seen:
            continue
        seen.add(r["run_id"])
        unique.append(r)
    return unique


def generate_ofat_runs(
    baseline: dict[str, Any],
    ofat: dict[str, Any],
    shared: dict[str, Any],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    """Return (runs, skipped) for one-factor-at-a-time sweeps around baseline."""
    grid = expand_param_grid(ofat)
    runs: list[dict[str, Any]] = []
    skipped: list[dict[str, Any]] = []

    # Always include the baseline once
    base_run = canonical_run_dict(baseline, shared)
    base_run["phase"] = "ofat"
    base_run["varied_param"] = "baseline"
    base_run["varied_value"] = None
    base_run["run_id"] = run_hash(base_run)
    runs.append(base_run)

    for param, values in grid.items():
        if param == "resolution":
            continue  # handled in resolution study
        for value in values:
            # Skip baseline duplicate
            if baseline.get(param) == value:
                continue
            reason = should_skip_ofat_point(param, value, baseline)
            trial = dict(baseline)
            trial[param] = value
            run = canonical_run_dict(trial, shared)
            run["phase"] = "ofat"
            run["varied_param"] = param
            run["varied_value"] = value
            run["run_id"] = run_hash(run)
            if reason:
                skipped.append({**run, "skip_reason": reason})
                continue
            runs.append(run)

    return runs, skipped


def generate_resolution_runs(
    baseline: dict[str, Any],
    values: list[int],
    shared: dict[str, Any],
) -> list[dict[str, Any]]:
    runs = []
    for v in values:
        trial = dict(baseline)
        trial["resolution"] = int(v)
        run = canonical_run_dict(trial, shared)
        run["phase"] = "resolution"
        run["varied_param"] = "resolution"
        run["varied_value"] = int(v)
        run["run_id"] = run_hash(run)
        runs.append(run)
    return runs
