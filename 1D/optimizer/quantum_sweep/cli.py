from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path
from typing import Any

from . import __version__
from .cache import run_dir
from .conditional import document_conditional_rules
from .config import load_config, resolve_path
from .expand import generate_ofat_runs, generate_pilot_runs, generate_resolution_runs
from .pareto import select_pareto_knee
from .plots import plot_ofat_responses, plot_pareto, plot_resolution_study, plot_trajectories
from .runner import run_batch


def _shared_from_cfg(cfg, root: Path) -> dict[str, Any]:
    return {
        "dataset": str(resolve_path(root, cfg.dataset)),
        "landscape": str(resolve_path(root, cfg.landscape)),
        "start_date": cfg.start_date,
        "end_date": cfg.end_date,
        "min_lat": cfg.min_lat,
        "max_lat": cfg.max_lat,
        "min_lon": cfg.min_lon,
        "max_lon": cfg.max_lon,
    }


def _write_summary_csv(path: Path, summaries: list[dict[str, Any]]) -> None:
    fields = [
        "run_id",
        "status",
        "phase",
        "varied_param",
        "varied_value",
        "mean_corrected_nmse",
        "mean_sinkhorn_emd_km",
        "mean_legacy_masked_mse",
        "mean_legacy_marginal_emd",
        "mean_mass_error",
        "elapsed_seconds",
    ]
    with path.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for s in summaries:
            row = {
                "run_id": s.get("run_id"),
                "status": s.get("status", "ok"),
                "phase": s.get("phase"),
                "varied_param": s.get("varied_param"),
                "varied_value": s.get("varied_value"),
                "elapsed_seconds": s.get("elapsed_seconds"),
            }
            if s.get("status", "ok") == "ok" and "corrected_nmse" in s:
                row.update(
                    {
                        "mean_corrected_nmse": s["corrected_nmse"]["mean"],
                        "mean_sinkhorn_emd_km": s["sinkhorn_emd_km"]["mean"],
                        "mean_legacy_masked_mse": s["legacy_masked_mse"]["mean"],
                        "mean_legacy_marginal_emd": s["legacy_marginal_emd"]["mean"],
                        "mean_mass_error": s["mass_error"]["mean"],
                    }
                )
            w.writerow(row)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="2D quantum epidemic parameter sweep tool")
    parser.add_argument("--config", required=True, help="Path to TOML sweep config")
    parser.add_argument("--jobs", type=int, default=None, help="Parallel worker count (default from config)")
    parser.add_argument("--skip-pilot", action="store_true", help="Skip pilot; use config baseline as-is")
    parser.add_argument("--skip-ofat", action="store_true")
    parser.add_argument("--skip-resolution", action="store_true")
    parser.add_argument("--version", action="version", version=f"quantum-sweep {__version__}")
    args = parser.parse_args(argv)

    cfg_path = Path(args.config).resolve()
    cfg = load_config(cfg_path)
    # Paths in TOML are relative to the config file's directory.
    root = cfg_path.parent
    # Simulator 1D root: optimizer/configs -> ../.. ; optimizer -> ..
    if root.name == "configs" and root.parent.name == "optimizer":
        sim_root = root.parent.parent
    elif root.name == "optimizer":
        sim_root = root.parent
    else:
        sim_root = root

    jobs = args.jobs if args.jobs is not None else cfg.jobs
    results_root = resolve_path(root, cfg.results_dir)
    results_root.mkdir(parents=True, exist_ok=True)
    plots_dir = results_root / "plots"
    plots_dir.mkdir(parents=True, exist_ok=True)

    headless_bin = resolve_path(root, cfg.headless_bin)
    if not headless_bin.exists():
        for candidate in (
            sim_root / "headless_runner",
            sim_root / "build" / "headless_runner",
            resolve_path(sim_root, cfg.headless_bin),
        ):
            if candidate.exists():
                headless_bin = candidate
                break
        else:
            print(f"ERROR: headless binary not found at {headless_bin}", file=sys.stderr)
            print(
                "Build it first: clang++ -std=c++17 -pthread -O2 -I. "
                "Simulator2D.cpp DataParser.cpp headless_runner.cpp -o headless_runner",
                file=sys.stderr,
            )
            return 2

    shared = _shared_from_cfg(cfg, root)
    # If dataset path relative to config dir doesn't exist, try sim_root
    if not Path(shared["dataset"]).exists():
        shared["dataset"] = str(resolve_path(sim_root, cfg.dataset))
    if not Path(shared["landscape"]).exists():
        shared["landscape"] = str(resolve_path(sim_root, cfg.landscape))

    baseline = dict(cfg.baseline)
    sinkhorn_kwargs = {
        "reg": cfg.sinkhorn_reg,
        "num_iter_max": cfg.sinkhorn_num_iter_max,
        "stop_threshold": cfg.sinkhorn_stop_threshold,
        "eval_resolution": cfg.sinkhorn_eval_resolution,
    }

    all_summaries: list[dict[str, Any]] = []
    selected_baseline = dict(baseline)
    selected_run_id = None

    def progress(s: dict[str, Any]) -> None:
        status = s.get("status", "ok")
        print(f"[{status}] {s.get('phase')} {s.get('varied_param')}={s.get('varied_value')} id={s.get('run_id')}")

    # --- Pilot ---
    if cfg.pilot_enabled and not args.skip_pilot:
        print(f"Running pilot with {cfg.pilot_n_samples} samples...")
        pilot_runs = generate_pilot_runs(
            baseline, cfg.ofat, shared, cfg.pilot_n_samples, cfg.pilot_seed
        )
        pilot_summaries = run_batch(
            headless_bin,
            pilot_runs,
            results_root,
            cfg.export_fields,
            sinkhorn_kwargs,
            work_dir=sim_root,
            jobs=jobs,
            progress=progress,
        )
        all_summaries.extend(pilot_summaries)
        ok = [s for s in pilot_summaries if s.get("status", "ok") == "ok" and "corrected_nmse" in s]
        if not ok:
            print("ERROR: no successful pilot runs", file=sys.stderr)
            return 1
        mse = [s["corrected_nmse"]["mean"] for s in ok]
        emd = [s["sinkhorn_emd_km"]["mean"] for s in ok]
        knee = select_pareto_knee(mse, emd)
        chosen = ok[knee]
        selected_run_id = chosen["run_id"]
        raw_params = dict(chosen.get("params", {}))
        keep_keys = {
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
        }
        selected_baseline = {k: raw_params[k] for k in keep_keys if k in raw_params}
        for k, v in baseline.items():
            selected_baseline.setdefault(k, v)
        print(f"Selected Pareto-knee baseline run_id={selected_run_id}")
        plot_pareto(ok, selected_run_id, plots_dir / "pilot_pareto.png")
        (results_root / "selected_baseline.json").write_text(
            json.dumps(
                {
                    "run_id": selected_run_id,
                    "baseline": selected_baseline,
                    "mean_corrected_nmse": chosen["corrected_nmse"]["mean"],
                    "mean_sinkhorn_emd_km": chosen["sinkhorn_emd_km"]["mean"],
                },
                indent=2,
            )
        )
    else:
        (results_root / "selected_baseline.json").write_text(
            json.dumps({"run_id": None, "baseline": selected_baseline}, indent=2)
        )

    # --- OFAT ---
    skipped = []
    if not args.skip_ofat:
        print("Running OFAT sweeps...")
        ofat_runs, skipped = generate_ofat_runs(selected_baseline, cfg.ofat, shared)
        ofat_summaries = run_batch(
            headless_bin,
            ofat_runs,
            results_root,
            cfg.export_fields,
            sinkhorn_kwargs,
            work_dir=sim_root,
            jobs=jobs,
            progress=progress,
        )
        all_summaries.extend(ofat_summaries)
        plot_ofat_responses(ofat_summaries, plots_dir)
        # Trajectory for baseline + a few extremes
        ok_ofat = [s for s in ofat_summaries if s.get("status", "ok") == "ok"]
        if ok_ofat:
            dirs = [run_dir(results_root, s["run_id"]) for s in ok_ofat[:5]]
            labels = [f"{s.get('varied_param')}={s.get('varied_value')}" for s in ok_ofat[:5]]
            plot_trajectories(dirs, labels, plots_dir / "trajectories_nmse.png", "corrected_nmse")
            plot_trajectories(dirs, labels, plots_dir / "trajectories_emd.png", "sinkhorn_emd_km")

    # --- Resolution study ---
    if cfg.resolution_study_enabled and not args.skip_resolution and cfg.resolution_study_values:
        print("Running resolution study...")
        res_runs = generate_resolution_runs(selected_baseline, cfg.resolution_study_values, shared)
        res_summaries = run_batch(
            headless_bin,
            res_runs,
            results_root,
            cfg.export_fields,
            sinkhorn_kwargs,
            work_dir=sim_root,
            jobs=jobs,
            progress=progress,
        )
        all_summaries.extend(res_summaries)
        plot_resolution_study(res_summaries, plots_dir / "resolution_study.png")

    _write_summary_csv(results_root / "run_summary.csv", all_summaries)
    (results_root / "skipped_runs.json").write_text(json.dumps(skipped, indent=2, default=str))

    manifest = {
        "tool_version": __version__,
        "config_path": str(cfg_path),
        "config": cfg.to_dict(),
        "selected_baseline": selected_baseline,
        "selected_run_id": selected_run_id,
        "headless_bin": str(headless_bin),
        "results_dir": str(results_root),
        "jobs": jobs,
        "n_summaries": len(all_summaries),
        "n_failed": sum(1 for s in all_summaries if s.get("status") == "failed"),
        "n_skipped": len(skipped),
        "conditional_rules": document_conditional_rules(),
        "metric_definitions": {
            "legacy_masked_mse": "Engine GUI-compatible masked MSE in case units on hist>0 cells",
            "legacy_marginal_emd": "Sum of 1D Wasserstein distances on row/column marginals",
            "corrected_nmse": "Mean((sim-hist)^2) / mean(hist^2) on hist>0 cells (dimensionless)",
            "mass_error": "(sum_sim - sum_hist) / sum_hist",
            "sinkhorn_emd_km": "Entropic Sinkhorn W1 on geographic-km cost matrix (POT)",
        },
        "limitations": [
            "JHU data are cumulative confirmed cases, not incidence.",
            "Sinkhorn EMD is an entropic approximation on a downsampled evaluation grid.",
            "Legacy EMD is a marginal proxy, not true 2D transport.",
            "Resolution changes discretization and should not be ranked with model parameters.",
        ],
    }
    (results_root / "manifest.json").write_text(json.dumps(manifest, indent=2, default=str))
    print(f"Done. Results in {results_root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
