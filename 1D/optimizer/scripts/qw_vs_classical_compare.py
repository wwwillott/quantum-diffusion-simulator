#!/usr/bin/env python3
"""Quantum vs classical comparison studies (separate from sweep results).

Uses the us_retention_refine Pareto knee. Reuses the existing quantum full-seed
run for fair apples-to-apples trajectories; only classical / thinned-seed runs
are newly executed.

Experiments
-----------
1. time_to_quality: daily Sinkhorn EMD for QUANTUM vs CLASSICAL (full seeds)
2. seed_ablation: mean EMD vs seed_keep_fraction for both modes
"""

from __future__ import annotations

import argparse
import csv
import json
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

# Allow running as script from optimizer/
SCRIPT_DIR = Path(__file__).resolve().parent
OPT = SCRIPT_DIR.parent  # optimizer/
ROOT = OPT.parent  # 1D/
sys.path.insert(0, str(OPT))

from quantum_sweep.metrics import (  # noqa: E402
    hist_cases_from_probs,
    masked_case_nmse,
    read_f32_bin,
    reshape_grid,
    sim_cases_from_probs,
    sinkhorn_emd_km,
    total_case_mass_error,
)

REFINE_BASELINE = ROOT / "results/us_retention_refine/selected_baseline.json"
REFINE_QUANTUM_RUN = ROOT / "results/us_retention_refine/runs/23eebc1050f31199"
OUT_ROOT = ROOT / "results/qw_vs_classical"
HEADLESS = ROOT / "headless_runner"
DATASET = ROOT / "data/time_series_covid19_confirmed_US.csv"
LANDSCAPE = ROOT / "data/nasa_pop.asc"

SINKHORN = dict(reg=0.1, num_iter_max=2000, stop_threshold=1e-6, eval_resolution=50)
GEO = dict(min_lat=24.0, max_lat=50.0, min_lon=-125.0, max_lon=-66.0)
SEED_FRACTIONS = [1.0, 0.5, 0.25, 0.1]


def load_knee() -> dict:
    doc = json.loads(REFINE_BASELINE.read_text())
    return doc["baseline"]


def run_label(mode: str, frac: float) -> str:
    return f"{mode.lower()}_seed{frac:g}"


def out_dir(mode: str, frac: float) -> Path:
    return OUT_ROOT / "runs" / run_label(mode, frac)


def build_args(mode: str, frac: float, baseline: dict, dest: Path) -> list[str]:
    return [
        str(HEADLESS),
        "--dataset",
        str(DATASET),
        "--landscape",
        str(LANDSCAPE),
        "--out",
        str(dest),
        "--start-date",
        "2/21/20",
        "--end-date",
        "6/30/20",
        "--resolution",
        str(int(baseline["resolution"])),
        "--mobility",
        str(float(baseline["mobility_rate"])),
        "--base-survival",
        str(float(baseline["base_survival_rate"])),
        "--urban-multiplier",
        str(float(baseline["urban_multiplier"])),
        "--ticks-per-day",
        str(int(baseline["ticks_per_day"])),
        "--days-per-tick",
        str(int(baseline["days_per_tick"])),
        "--nodal-retention",
        "1" if baseline["nodal_retention"] else "0",
        "--unitary-coin",
        str(baseline["unitary_coin"]),
        "--init-state",
        str(baseline["init_state"]),
        "--boundary",
        str(baseline["boundary"]),
        "--mode",
        mode,
        "--seed-keep-fraction",
        str(float(frac)),
        "--min-lat",
        str(GEO["min_lat"]),
        "--max-lat",
        str(GEO["max_lat"]),
        "--min-lon",
        str(GEO["min_lon"]),
        "--max-lon",
        str(GEO["max_lon"]),
    ]


def postprocess(dest: Path) -> dict:
    legacy = list(csv.DictReader((dest / "legacy_metrics.csv").open()))
    meta = json.loads((dest / "run_meta.json").read_text())
    warm = None
    rows_out = []
    nmse_s, emd_s, mass_s = [], [], []
    for row in legacy:
        day = int(row["day_index"])
        sim = reshape_grid(read_f32_bin(dest / f"sim_day_{day}.f32"))
        hist = reshape_grid(read_f32_bin(dest / f"hist_day_{day}.f32"))
        sim_c = sim_cases_from_probs(sim, float(row["max_seed_cases"]))
        hist_c = hist_cases_from_probs(hist, float(row["max_historical_cases"]))
        nmse = masked_case_nmse(sim_c, hist_c)
        mass = total_case_mass_error(sim_c, hist_c)
        emd, _ = sinkhorn_emd_km(
            sim_c,
            hist_c,
            GEO["min_lat"],
            GEO["max_lat"],
            GEO["min_lon"],
            GEO["max_lon"],
            **SINKHORN,
            warmstart=warm,
        )
        warm = {"n": SINKHORN["eval_resolution"]}
        nmse_s.append(nmse)
        emd_s.append(emd)
        mass_s.append(mass)
        rows_out.append(
            {
                "day_index": day,
                "date": row["date"],
                "corrected_nmse": nmse,
                "mass_error": mass,
                "sinkhorn_emd_km": emd,
                "total_prob": float(row["total_prob"]),
                "sim_elapsed_seconds": float(meta.get("elapsed_seconds", float("nan"))),
            }
        )

    with (dest / "daily_metrics.csv").open("w", newline="") as f:
        w = csv.DictWriter(
            f,
            fieldnames=[
                "day_index",
                "date",
                "corrected_nmse",
                "mass_error",
                "sinkhorn_emd_km",
                "total_prob",
                "sim_elapsed_seconds",
            ],
        )
        w.writeheader()
        w.writerows(rows_out)

    summary = {
        "mode": meta.get("mode"),
        "seed_keep_fraction": meta.get("seed_keep_fraction", 1.0),
        "sim_elapsed_seconds": meta.get("elapsed_seconds"),
        "mean_corrected_nmse": sum(nmse_s) / len(nmse_s),
        "mean_sinkhorn_emd_km": sum(emd_s) / len(emd_s),
        "mean_mass_error": sum(mass_s) / len(mass_s),
        "final_sinkhorn_emd_km": emd_s[-1],
        "days_to_emd_below_600": next((i for i, e in enumerate(emd_s) if e < 600), None),
        "days_to_emd_below_700": next((i for i, e in enumerate(emd_s) if e < 700), None),
        "source": str(dest),
    }
    (dest / "summary.json").write_text(json.dumps(summary, indent=2))
    return summary


def ensure_quantum_full_from_refine() -> Path:
    """Reuse refine knee quantum run; do not mutate the sweep folder."""
    dest = out_dir("QUANTUM", 1.0)
    dest.mkdir(parents=True, exist_ok=True)
    src = REFINE_QUANTUM_RUN
    if not (src / "daily_metrics.csv").exists():
        raise FileNotFoundError(f"missing refine quantum run at {src}")

    for name in ("daily_metrics.csv", "legacy_metrics.csv", "run_meta.json"):
        s = src / name
        if s.exists():
            shutil.copy2(s, dest / name)

    note = {
        "reused_from": str(src),
        "mode": "QUANTUM",
        "seed_keep_fraction": 1.0,
        "note": "Existing refine-sweep quantum full-seed run; protocol unchanged.",
    }
    (dest / "provenance.json").write_text(json.dumps(note, indent=2))

    rows = list(csv.DictReader((dest / "daily_metrics.csv").open()))
    emd = [float(r["sinkhorn_emd_km"]) for r in rows]
    nmse = [float(r["corrected_nmse"]) for r in rows]
    mass = [float(r["mass_error"]) for r in rows]
    meta = json.loads((dest / "run_meta.json").read_text())
    summary = {
        "mode": "QUANTUM",
        "seed_keep_fraction": 1.0,
        "sim_elapsed_seconds": meta.get("elapsed_seconds"),
        "mean_corrected_nmse": sum(nmse) / len(nmse),
        "mean_sinkhorn_emd_km": sum(emd) / len(emd),
        "mean_mass_error": sum(mass) / len(mass),
        "final_sinkhorn_emd_km": emd[-1],
        "days_to_emd_below_600": next((i for i, e in enumerate(emd) if e < 600), None),
        "days_to_emd_below_700": next((i for i, e in enumerate(emd) if e < 700), None),
        "source": str(dest),
        "reused_from": str(src),
    }
    (dest / "summary.json").write_text(json.dumps(summary, indent=2))
    return dest


def run_one(mode: str, frac: float, baseline: dict) -> dict:
    dest = out_dir(mode, frac)
    if mode == "QUANTUM" and abs(frac - 1.0) < 1e-12:
        return json.loads(ensure_quantum_full_from_refine().joinpath("summary.json").read_text())

    if (dest / "summary.json").exists() and (dest / "daily_metrics.csv").exists():
        print(f"[skip] {dest.name} already complete")
        return json.loads((dest / "summary.json").read_text())

    dest.mkdir(parents=True, exist_ok=True)
    cmd = build_args(mode, frac, baseline, dest)
    print(f"[run] {mode} seed={frac} -> {dest.name}")
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        (dest / "failure.txt").write_text(proc.stdout + "\n" + proc.stderr)
        raise RuntimeError(f"headless failed for {dest.name}: {proc.stderr[-500:]}")
    return postprocess(dest)


def plot_time_to_quality(summaries_dirs: dict[str, Path], plots: Path) -> None:
    fig, ax = plt.subplots(figsize=(9, 4.8))
    for label, d in summaries_dirs.items():
        rows = list(csv.DictReader((d / "daily_metrics.csv").open()))
        ys = [float(r["sinkhorn_emd_km"]) for r in rows]
        ax.plot(ys, label=label, linewidth=1.6)
    ax.axhline(600, color="gray", ls="--", lw=0.8, label="EMD=600 km")
    ax.axhline(700, color="gray", ls=":", lw=0.8, label="EMD=700 km")
    ax.set_xlabel("Day index")
    ax.set_ylabel("sinkhorn_emd_km")
    ax.set_title("Time-to-quality: quantum vs classical (full seeds, refine knee)")
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(plots / "time_to_quality_emd.png", dpi=140)
    plt.close(fig)


def plot_seed_ablation(rows: list[dict], plots: Path) -> None:
    fig, axes = plt.subplots(1, 2, figsize=(10, 4.2))
    for mode, color in [("QUANTUM", "#4c78a8"), ("CLASSICAL", "#e45756")]:
        pts = sorted(
            [r for r in rows if r.get("mode") == mode],
            key=lambda r: float(r["seed_keep_fraction"]),
        )
        xs = [float(p["seed_keep_fraction"]) for p in pts]
        axes[0].plot(xs, [p["mean_sinkhorn_emd_km"] for p in pts], "o-", color=color, label=mode)
        axes[1].plot(xs, [p["sim_elapsed_seconds"] for p in pts], "o-", color=color, label=mode)
    axes[0].set_xlabel("seed_keep_fraction (top day-0 sites)")
    axes[0].set_ylabel("Mean Sinkhorn EMD (km)")
    axes[0].set_title("Seed ablation — spatial error")
    axes[0].invert_xaxis()
    axes[0].legend()
    axes[1].set_xlabel("seed_keep_fraction")
    axes[1].set_ylabel("C++ sim elapsed (s)")
    axes[1].set_title("Seed ablation — sim runtime")
    axes[1].invert_xaxis()
    axes[1].legend()
    fig.tight_layout()
    fig.savefig(plots / "seed_ablation.png", dpi=140)
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()

    if not HEADLESS.exists():
        raise SystemExit(f"missing headless binary: {HEADLESS}")
    baseline = load_knee()
    OUT_ROOT.mkdir(parents=True, exist_ok=True)
    plots = OUT_ROOT / "plots"
    plots.mkdir(exist_ok=True)

    (OUT_ROOT / "baseline_used.json").write_text(
        json.dumps(
            {
                "from": str(REFINE_BASELINE),
                "baseline": baseline,
                "geo": GEO,
                "seed_fractions": SEED_FRACTIONS,
                "note": "Quantum full-seed reused from us_retention_refine; other cells newly run.",
            },
            indent=2,
        )
    )

    jobs = []
    for mode in ("QUANTUM", "CLASSICAL"):
        for frac in SEED_FRACTIONS:
            jobs.append((mode, frac))

    results: list[dict] = []
    # Run sequentially-ish with small pool; Sinkhorn is heavy
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as ex:
        futs = {ex.submit(run_one, m, f, baseline): (m, f) for m, f in jobs}
        for fut in as_completed(futs):
            m, f = futs[fut]
            try:
                summary = fut.result()
                summary["mode"] = summary.get("mode") or m
                summary["seed_keep_fraction"] = float(summary.get("seed_keep_fraction", f))
                results.append(summary)
                print(
                    f"[ok] {m} seed={f}: mean_emd={summary['mean_sinkhorn_emd_km']:.1f} "
                    f"sim_s={summary.get('sim_elapsed_seconds')}"
                )
            except Exception as e:
                print(f"[fail] {m} seed={f}: {e}")
                raise

    results = sorted(results, key=lambda r: (r["mode"], float(r["seed_keep_fraction"])))
    with (OUT_ROOT / "comparison_summary.csv").open("w", newline="") as f:
        fields = [
            "mode",
            "seed_keep_fraction",
            "mean_sinkhorn_emd_km",
            "mean_corrected_nmse",
            "mean_mass_error",
            "final_sinkhorn_emd_km",
            "days_to_emd_below_600",
            "days_to_emd_below_700",
            "sim_elapsed_seconds",
            "source",
        ]
        w = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
        w.writeheader()
        w.writerows(results)

    (OUT_ROOT / "comparison_summary.json").write_text(json.dumps(results, indent=2))

    plot_time_to_quality(
        {
            "QUANTUM (full seeds)": out_dir("QUANTUM", 1.0),
            "CLASSICAL (full seeds)": out_dir("CLASSICAL", 1.0),
        },
        plots,
    )
    plot_seed_ablation(results, plots)
    print(f"Done. Outputs in {OUT_ROOT}")


if __name__ == "__main__":
    main()
