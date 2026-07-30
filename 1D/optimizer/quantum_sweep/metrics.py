from __future__ import annotations

import struct
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path

import numpy as np

try:
    import ot
except ImportError:  # pragma: no cover
    ot = None


def read_f32_bin(path: Path) -> np.ndarray:
    raw = path.read_bytes()
    if len(raw) < 4:
        raise ValueError(f"empty f32 file: {path}")
    (n,) = struct.unpack("<I", raw[:4])
    arr = np.frombuffer(raw, dtype="<f4", count=n, offset=4).astype(np.float64)
    if arr.size != n:
        raise ValueError(f"size mismatch in {path}")
    return arr


def reshape_grid(flat: np.ndarray) -> np.ndarray:
    n = int(round(np.sqrt(flat.size)))
    if n * n != flat.size:
        raise ValueError(f"not a square grid: {flat.size}")
    return flat.reshape(n, n)


def sim_cases_from_probs(sim_probs: np.ndarray, max_seed_cases: float) -> np.ndarray:
    return sim_probs * float(max_seed_cases)


def hist_cases_from_probs(hist_probs: np.ndarray, max_historical_cases: float) -> np.ndarray:
    return hist_probs * float(max_historical_cases)


def masked_case_nmse(sim_cases: np.ndarray, hist_cases: np.ndarray) -> float:
    """Dimensionless masked NMSE on observed (hist>0) cells."""
    mask = hist_cases > 0
    if not np.any(mask):
        return 0.0
    diff = sim_cases[mask] - hist_cases[mask]
    denom = float(np.mean(hist_cases[mask] ** 2))
    if denom <= 0:
        return float(np.mean(diff**2))
    return float(np.mean(diff**2) / denom)


def total_case_mass_error(sim_cases: np.ndarray, hist_cases: np.ndarray) -> float:
    """Relative total-mass error: (sum_sim - sum_hist) / max(sum_hist, eps)."""
    s = float(np.sum(sim_cases))
    h = float(np.sum(hist_cases))
    return (s - h) / max(h, 1e-12)


def downsample_grid(grid: np.ndarray, eval_n: int) -> np.ndarray:
    """Block-average downsample to eval_n x eval_n (sums conserved approximately)."""
    n = grid.shape[0]
    if eval_n >= n:
        return grid.copy()
    # Simple strided mean via reshape when divisible; else zoom with summing
    ys = np.linspace(0, n, eval_n + 1).astype(int)
    xs = np.linspace(0, n, eval_n + 1).astype(int)
    out = np.zeros((eval_n, eval_n), dtype=np.float64)
    for i in range(eval_n):
        for j in range(eval_n):
            block = grid[ys[i] : ys[i + 1], xs[j] : xs[j + 1]]
            out[i, j] = block.sum()
    return out


def cell_centers_km(
    n: int,
    min_lat: float,
    max_lat: float,
    min_lon: float,
    max_lon: float,
) -> np.ndarray:
    """Return (n*n, 2) lat/lon centers converted to local km coordinates."""
    lats = np.linspace(max_lat, min_lat, n)  # row 0 = max_lat (matches engine mapping)
    lons = np.linspace(min_lon, max_lon, n)
    lon_grid, lat_grid = np.meshgrid(lons, lats)
    # Equirectangular km relative to map center
    lat0 = 0.5 * (min_lat + max_lat)
    lon0 = 0.5 * (min_lon + max_lon)
    km_per_deg_lat = 111.32
    km_per_deg_lon = 111.32 * np.cos(np.deg2rad(lat0))
    x = (lon_grid - lon0) * km_per_deg_lon
    y = (lat_grid - lat0) * km_per_deg_lat
    return np.column_stack([x.ravel(), y.ravel()])


@lru_cache(maxsize=8)
def _cached_cost(n: int, min_lat: float, max_lat: float, min_lon: float, max_lon: float) -> np.ndarray:
    coords = cell_centers_km(n, min_lat, max_lat, min_lon, max_lon)
    # pairwise Euclidean km
    diff = coords[:, None, :] - coords[None, :, :]
    return np.sqrt(np.sum(diff * diff, axis=-1))


def sinkhorn_emd_km(
    sim_mass: np.ndarray,
    hist_mass: np.ndarray,
    min_lat: float,
    max_lat: float,
    min_lon: float,
    max_lon: float,
    reg: float = 0.1,
    num_iter_max: int = 2000,
    stop_threshold: float = 1e-6,
    eval_resolution: int = 50,
    warmstart: dict | None = None,
) -> tuple[float, dict]:
    """
    Entropic Sinkhorn Wasserstein-1 approximation in geographic kilometers.
    Masses are normalized to probability distributions.
    """
    if ot is None:
        raise ImportError("Python Optimal Transport (pot) is required for Sinkhorn EMD")

    sim_g = downsample_grid(sim_mass, eval_resolution)
    hist_g = downsample_grid(hist_mass, eval_resolution)
    n = sim_g.shape[0]

    a = sim_g.ravel().astype(np.float64)
    b = hist_g.ravel().astype(np.float64)
    # Sinkhorn requires strictly positive masses; add a tiny floor then renormalize.
    eps_mass = 1e-12
    a = np.maximum(a, 0.0) + eps_mass
    b = np.maximum(b, 0.0) + eps_mass
    a = a / a.sum()
    b = b / b.sum()

    M = _cached_cost(n, float(min_lat), float(max_lat), float(min_lon), float(max_lon))
    # Scale regularization relative to median cost for stability
    med = float(np.median(M[M > 0])) if np.any(M > 0) else 1.0
    eps = float(reg) * med

    kwargs = {}
    if warmstart and "log_u" in warmstart and warmstart.get("n") == n:
        # POT sinkhorn2 doesn't expose warm start easily; keep kwargs for manifest only.
        kwargs["warmstart_note"] = "requested"

    dist = float(
        ot.sinkhorn2(
            a,
            b,
            M,
            reg=eps,
            numItermax=num_iter_max,
            stopThr=stop_threshold,
        )
    )
    meta = {
        "reg": reg,
        "reg_scaled": eps,
        "num_iter_max": num_iter_max,
        "stop_threshold": stop_threshold,
        "eval_resolution": n,
        "median_cost_km": med,
    }
    return dist, meta


@dataclass
class DayMetrics:
    day_index: int
    date: str
    legacy_masked_mse: float
    legacy_marginal_emd: float
    corrected_nmse: float
    mass_error: float
    sinkhorn_emd_km: float
    total_prob: float


def summarize_series(values: list[float]) -> dict[str, float]:
    arr = np.asarray(values, dtype=float)
    if arr.size == 0:
        return {"mean": np.nan, "final": np.nan, "min": np.nan, "max": np.nan, "initial": np.nan}
    return {
        "mean": float(np.mean(arr)),
        "final": float(arr[-1]),
        "min": float(np.min(arr)),
        "max": float(np.max(arr)),
        "initial": float(arr[0]),
    }
