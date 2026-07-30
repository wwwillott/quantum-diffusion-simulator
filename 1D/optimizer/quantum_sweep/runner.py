from __future__ import annotations

import csv
import json
import subprocess
import time
import traceback
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from typing import Any, Callable

from .cache import is_complete, run_dir, write_failure
from .metrics import (
    DayMetrics,
    hist_cases_from_probs,
    masked_case_nmse,
    read_f32_bin,
    reshape_grid,
    sim_cases_from_probs,
    sinkhorn_emd_km,
    summarize_series,
    total_case_mass_error,
)


def build_cli_args(run: dict[str, Any], out_dir: Path, export_fields: bool) -> list[str]:
    args = [
        "--dataset",
        str(run["dataset"]),
        "--landscape",
        str(run["landscape"]),
        "--out",
        str(out_dir),
        "--start-date",
        str(run["start_date"]),
        "--end-date",
        str(run["end_date"]),
        "--resolution",
        str(int(run["resolution"])),
        "--mobility",
        str(float(run["mobility_rate"])),
        "--base-survival",
        str(float(run["base_survival_rate"])),
        "--urban-multiplier",
        str(float(run["urban_multiplier"])),
        "--ticks-per-day",
        str(int(run["ticks_per_day"])),
        "--days-per-tick",
        str(int(run.get("days_per_tick", 1))),
        "--nodal-retention",
        "1" if run["nodal_retention"] else "0",
        "--unitary-coin",
        str(run["unitary_coin"]),
        "--init-state",
        str(run["init_state"]),
        "--boundary",
        str(run["boundary"]),
        "--min-lat",
        str(float(run["min_lat"])),
        "--max-lat",
        str(float(run["max_lat"])),
        "--min-lon",
        str(float(run["min_lon"])),
        "--max-lon",
        str(float(run["max_lon"])),
    ]
    if not export_fields:
        args.append("--no-export-fields")
    return args


def postprocess_run(
    out_dir: Path,
    run: dict[str, Any],
    sinkhorn_kwargs: dict[str, Any],
) -> dict[str, Any]:
    legacy_path = out_dir / "legacy_metrics.csv"
    if not legacy_path.exists():
        raise FileNotFoundError(f"missing {legacy_path}")

    rows = list(csv.DictReader(legacy_path.open()))
    day_metrics: list[DayMetrics] = []
    warm = None

    for row in rows:
        day_index = int(row["day_index"])
        date = row["date"]
        legacy_mse = float(row["legacy_masked_mse"])
        legacy_emd = float(row["legacy_marginal_emd"])
        total_prob = float(row["total_prob"])
        max_seed = float(row["max_seed_cases"])
        max_hist = float(row["max_historical_cases"])

        sim_path = out_dir / f"sim_day_{day_index}.f32"
        hist_path = out_dir / f"hist_day_{day_index}.f32"
        if not sim_path.exists() or not hist_path.exists():
            raise FileNotFoundError(f"missing field dumps for day {day_index}")

        sim = reshape_grid(read_f32_bin(sim_path))
        hist = reshape_grid(read_f32_bin(hist_path))
        sim_cases = sim_cases_from_probs(sim, max_seed)
        hist_cases = hist_cases_from_probs(hist, max_hist)

        nmse = masked_case_nmse(sim_cases, hist_cases)
        mass_err = total_case_mass_error(sim_cases, hist_cases)
        sink, meta = sinkhorn_emd_km(
            sim_cases,
            hist_cases,
            min_lat=float(run["min_lat"]),
            max_lat=float(run["max_lat"]),
            min_lon=float(run["min_lon"]),
            max_lon=float(run["max_lon"]),
            reg=float(sinkhorn_kwargs.get("reg", 0.1)),
            num_iter_max=int(sinkhorn_kwargs.get("num_iter_max", 2000)),
            stop_threshold=float(sinkhorn_kwargs.get("stop_threshold", 1e-6)),
            eval_resolution=int(sinkhorn_kwargs.get("eval_resolution", 50)),
            warmstart=warm,
        )
        warm = {"n": meta["eval_resolution"]}

        day_metrics.append(
            DayMetrics(
                day_index=day_index,
                date=date,
                legacy_masked_mse=legacy_mse,
                legacy_marginal_emd=legacy_emd,
                corrected_nmse=nmse,
                mass_error=mass_err,
                sinkhorn_emd_km=sink,
                total_prob=total_prob,
            )
        )

    daily_csv = out_dir / "daily_metrics.csv"
    with daily_csv.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(
            [
                "day_index",
                "date",
                "legacy_masked_mse",
                "legacy_marginal_emd",
                "corrected_nmse",
                "mass_error",
                "sinkhorn_emd_km",
                "total_prob",
            ]
        )
        for d in day_metrics:
            w.writerow(
                [
                    d.day_index,
                    d.date,
                    d.legacy_masked_mse,
                    d.legacy_marginal_emd,
                    d.corrected_nmse,
                    d.mass_error,
                    d.sinkhorn_emd_km,
                    d.total_prob,
                ]
            )

    summary = {
        "run_id": run["run_id"],
        "phase": run.get("phase"),
        "varied_param": run.get("varied_param"),
        "varied_value": run.get("varied_value"),
        "status": "ok",
        "legacy_masked_mse": summarize_series([d.legacy_masked_mse for d in day_metrics]),
        "legacy_marginal_emd": summarize_series([d.legacy_marginal_emd for d in day_metrics]),
        "corrected_nmse": summarize_series([d.corrected_nmse for d in day_metrics]),
        "mass_error": summarize_series([d.mass_error for d in day_metrics]),
        "sinkhorn_emd_km": summarize_series([d.sinkhorn_emd_km for d in day_metrics]),
        "sinkhorn_settings": sinkhorn_kwargs,
        "params": {k: run[k] for k in run if k not in {"phase", "varied_param", "varied_value"}},
    }

    # Merge into run_meta.json
    meta_path = out_dir / "run_meta.json"
    meta = {}
    if meta_path.exists():
        meta = json.loads(meta_path.read_text())
    meta.update(
        {
            "status": "ok",
            "summary": summary,
            "sinkhorn_settings": sinkhorn_kwargs,
        }
    )
    meta_path.write_text(json.dumps(meta, indent=2))
    (out_dir / "summary.json").write_text(json.dumps(summary, indent=2))
    return summary


def execute_one(
    headless_bin: Path,
    run: dict[str, Any],
    results_root: Path,
    export_fields: bool,
    sinkhorn_kwargs: dict[str, Any],
    work_dir: Path,
) -> dict[str, Any]:
    run_id = run["run_id"]
    out = run_dir(results_root, run_id)
    out.mkdir(parents=True, exist_ok=True)

    if is_complete(results_root, run_id):
        summary_path = out / "summary.json"
        if summary_path.exists():
            return json.loads(summary_path.read_text())
        # Recompute corrected metrics if only legacy completed
        return postprocess_run(out, run, sinkhorn_kwargs)

    cmd = [str(headless_bin), *build_cli_args(run, out, export_fields=True)]
    t0 = time.time()
    try:
        proc = subprocess.run(
            cmd,
            cwd=str(work_dir),
            capture_output=True,
            text=True,
            check=False,
        )
        elapsed = time.time() - t0
        if proc.returncode != 0:
            write_failure(
                results_root,
                run_id,
                {
                    "run_id": run_id,
                    "returncode": proc.returncode,
                    "stdout": proc.stdout[-4000:],
                    "stderr": proc.stderr[-4000:],
                    "elapsed_seconds": elapsed,
                    "params": run,
                },
            )
            return {
                "run_id": run_id,
                "status": "failed",
                "error": proc.stderr[-500:] or proc.stdout[-500:],
                "phase": run.get("phase"),
                "varied_param": run.get("varied_param"),
                "varied_value": run.get("varied_value"),
            }

        summary = postprocess_run(out, run, sinkhorn_kwargs)
        summary["elapsed_seconds"] = elapsed
        return summary
    except Exception as e:
        write_failure(
            results_root,
            run_id,
            {
                "run_id": run_id,
                "error": str(e),
                "traceback": traceback.format_exc(),
                "params": run,
            },
        )
        return {
            "run_id": run_id,
            "status": "failed",
            "error": str(e),
            "phase": run.get("phase"),
            "varied_param": run.get("varied_param"),
            "varied_value": run.get("varied_value"),
        }


def run_batch(
    headless_bin: Path,
    runs: list[dict[str, Any]],
    results_root: Path,
    export_fields: bool,
    sinkhorn_kwargs: dict[str, Any],
    work_dir: Path,
    jobs: int = 1,
    progress: Callable[[dict[str, Any]], None] | None = None,
) -> list[dict[str, Any]]:
    results_root.mkdir(parents=True, exist_ok=True)
    summaries: list[dict[str, Any]] = []

    def _one(run: dict[str, Any]) -> dict[str, Any]:
        return execute_one(
            headless_bin, run, results_root, export_fields, sinkhorn_kwargs, work_dir
        )

    if jobs <= 1:
        for run in runs:
            s = _one(run)
            summaries.append(s)
            if progress:
                progress(s)
        return summaries

    with ThreadPoolExecutor(max_workers=jobs) as ex:
        futs = {ex.submit(_one, run): run for run in runs}
        for fut in as_completed(futs):
            s = fut.result()
            summaries.append(s)
            if progress:
                progress(s)
    return summaries
