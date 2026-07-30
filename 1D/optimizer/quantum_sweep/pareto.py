from __future__ import annotations

import math
from typing import Iterable

import numpy as np


def pareto_front_indices(mse: np.ndarray, emd: np.ndarray) -> np.ndarray:
    """Indices of non-dominated points (minimize both)."""
    n = len(mse)
    keep = np.ones(n, dtype=bool)
    for i in range(n):
        if not keep[i]:
            continue
        for j in range(n):
            if i == j or not keep[j]:
                continue
            if (mse[j] <= mse[i] and emd[j] <= emd[i]) and (mse[j] < mse[i] or emd[j] < emd[i]):
                keep[i] = False
                break
    return np.where(keep)[0]


def normalize_minmax(x: np.ndarray) -> np.ndarray:
    lo = np.nanmin(x)
    hi = np.nanmax(x)
    if not np.isfinite(lo) or not np.isfinite(hi) or hi - lo < 1e-15:
        return np.zeros_like(x, dtype=float)
    return (x - lo) / (hi - lo)


def select_pareto_knee(
    mse: Iterable[float],
    emd: Iterable[float],
) -> int:
    """
    Select the Pareto-front knee after min-max normalizing both metrics.
    Deterministic: among ties, choose smallest normalized MSE then EMD then index.
    """
    mse_a = np.asarray(list(mse), dtype=float)
    emd_a = np.asarray(list(emd), dtype=float)
    if len(mse_a) == 0:
        raise ValueError("empty metric arrays")

    front = pareto_front_indices(mse_a, emd_a)
    nmse = normalize_minmax(mse_a)
    nemd = normalize_minmax(emd_a)

    # Ideal point at (0,0) in normalized space; knee = closest on front
    best_i = int(front[0])
    best_key = None
    for i in front:
        dist = math.hypot(float(nmse[i]), float(nemd[i]))
        key = (dist, float(nmse[i]), float(nemd[i]), int(i))
        if best_key is None or key < best_key:
            best_key = key
            best_i = int(i)
    return best_i
