from __future__ import annotations

from pathlib import Path
from typing import Any

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


def _ok_summaries(summaries: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [s for s in summaries if s.get("status", "ok") == "ok" and "corrected_nmse" in s]


def plot_pareto(summaries: list[dict[str, Any]], selected_id: str | None, out: Path) -> None:
    pts = _ok_summaries(summaries)
    if not pts:
        return
    x = [s["corrected_nmse"]["mean"] for s in pts]
    y = [s["sinkhorn_emd_km"]["mean"] for s in pts]
    fig, ax = plt.subplots(figsize=(6, 5))
    ax.scatter(x, y, c="#4c78a8", alpha=0.7, label="pilot")
    if selected_id:
        for s in pts:
            if s.get("run_id") == selected_id:
                ax.scatter(
                    [s["corrected_nmse"]["mean"]],
                    [s["sinkhorn_emd_km"]["mean"]],
                    c="#e45756",
                    s=80,
                    label="Pareto knee",
                    zorder=5,
                )
                break
    ax.set_xlabel("Mean corrected NMSE")
    ax.set_ylabel("Mean Sinkhorn EMD (km)")
    ax.set_title("Pilot Pareto selection")
    ax.legend()
    fig.tight_layout()
    fig.savefig(out, dpi=140)
    plt.close(fig)


def plot_ofat_responses(summaries: list[dict[str, Any]], out_dir: Path) -> None:
    pts = [s for s in _ok_summaries(summaries) if s.get("phase") == "ofat"]
    by_param: dict[str, list[dict[str, Any]]] = {}
    for s in pts:
        p = s.get("varied_param")
        if not p or p == "baseline":
            continue
        by_param.setdefault(p, []).append(s)

    for param, rows in by_param.items():
        # Sort by varied value when numeric
        def key(r: dict[str, Any]):
            v = r.get("varied_value")
            try:
                return (0, float(v))
            except Exception:
                return (1, str(v))

        rows = sorted(rows, key=key)
        xs = [r.get("varied_value") for r in rows]
        nmse = [r["corrected_nmse"]["mean"] for r in rows]
        emd = [r["sinkhorn_emd_km"]["mean"] for r in rows]

        fig, axes = plt.subplots(2, 1, figsize=(7, 6), sharex=True)
        axes[0].plot(range(len(xs)), nmse, marker="o", color="#4c78a8")
        axes[0].set_ylabel("Mean corrected NMSE")
        axes[0].set_title(f"OFAT response: {param}")
        axes[1].plot(range(len(xs)), emd, marker="o", color="#f58518")
        axes[1].set_ylabel("Mean Sinkhorn EMD (km)")
        axes[1].set_xticks(range(len(xs)))
        axes[1].set_xticklabels([str(x) for x in xs], rotation=45, ha="right")
        axes[1].set_xlabel(param)
        fig.tight_layout()
        fig.savefig(out_dir / f"ofat_{param}.png", dpi=140)
        plt.close(fig)


def plot_trajectories(run_dirs: list[Path], labels: list[str], out: Path, metric: str = "corrected_nmse") -> None:
    fig, ax = plt.subplots(figsize=(8, 4.5))
    for d, label in zip(run_dirs, labels):
        path = d / "daily_metrics.csv"
        if not path.exists():
            continue
        import csv

        rows = list(csv.DictReader(path.open()))
        ys = [float(r[metric]) for r in rows]
        ax.plot(ys, label=label)
    ax.set_xlabel("Day index")
    ax.set_ylabel(metric)
    ax.set_title(f"Daily {metric}")
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(out, dpi=140)
    plt.close(fig)


def plot_resolution_study(summaries: list[dict[str, Any]], out: Path) -> None:
    pts = [s for s in _ok_summaries(summaries) if s.get("phase") == "resolution"]
    if not pts:
        return
    pts = sorted(pts, key=lambda s: int(s.get("varied_value", 0)))
    xs = [int(s["varied_value"]) for s in pts]
    nmse = [s["corrected_nmse"]["mean"] for s in pts]
    emd = [s["sinkhorn_emd_km"]["mean"] for s in pts]
    elapsed = [s.get("elapsed_seconds", np.nan) for s in pts]

    fig, axes = plt.subplots(1, 3, figsize=(12, 3.8))
    axes[0].plot(xs, nmse, marker="o")
    axes[0].set_title("NMSE vs resolution")
    axes[0].set_xlabel("resolution")
    axes[1].plot(xs, emd, marker="o", color="#f58518")
    axes[1].set_title("Sinkhorn EMD vs resolution")
    axes[1].set_xlabel("resolution")
    axes[2].plot(xs, elapsed, marker="o", color="#54a24b")
    axes[2].set_title("Runtime vs resolution")
    axes[2].set_xlabel("resolution")
    fig.tight_layout()
    fig.savefig(out, dpi=140)
    plt.close(fig)
