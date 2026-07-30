from __future__ import annotations

import json
from pathlib import Path
from typing import Any


def run_dir(results_root: Path, run_id: str) -> Path:
    return results_root / "runs" / run_id


def is_complete(results_root: Path, run_id: str) -> bool:
    meta = run_dir(results_root, run_id) / "run_meta.json"
    summary = run_dir(results_root, run_id) / "daily_metrics.csv"
    if not meta.exists() or not summary.exists():
        return False
    try:
        data = json.loads(meta.read_text())
        return data.get("status") == "ok"
    except Exception:
        return False


def load_meta(results_root: Path, run_id: str) -> dict[str, Any] | None:
    path = run_dir(results_root, run_id) / "run_meta.json"
    if not path.exists():
        return None
    try:
        return json.loads(path.read_text())
    except Exception:
        return None


def write_failure(results_root: Path, run_id: str, payload: dict[str, Any]) -> None:
    d = run_dir(results_root, run_id)
    d.mkdir(parents=True, exist_ok=True)
    payload = dict(payload)
    payload["status"] = "failed"
    (d / "run_meta.json").write_text(json.dumps(payload, indent=2))
