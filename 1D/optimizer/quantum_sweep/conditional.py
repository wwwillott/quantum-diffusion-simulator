from __future__ import annotations

from typing import Any


def activation_rule(param: str, settings: dict[str, Any]) -> str | None:
    """Return a skip reason if `param` is inactive under `settings`, else None."""
    retention = bool(settings.get("nodal_retention", True))

    if param == "mobility_rate" and not retention:
        return "mobility_rate has no effect when nodal_retention is false"

    # init_state shapes seeding when retention is off, and leak shape when on —
    # always active for OPEN epidemic runs. Keep for documentation completeness.
    if param == "nodal_retention":
        return None

    return None


def should_skip_ofat_point(param: str, value: Any, baseline: dict[str, Any]) -> str | None:
    trial = dict(baseline)
    trial[param] = value
    return activation_rule(param, trial)


def document_conditional_rules() -> list[dict[str, str]]:
    return [
        {
            "parameter": "mobility_rate",
            "active_when": "nodal_retention == true",
            "note": "Without retention the walk fully redistributes each tick; mobility is unused.",
        },
        {
            "parameter": "init_state",
            "active_when": "always (OPEN epidemic)",
            "note": "Controls day-0 amplitude pattern when retention is off, and center-leak phases when on.",
        },
        {
            "parameter": "non_unitary_coin",
            "active_when": "never (dead enum)",
            "note": "Not swept; OPEN growth uses node_growth_rate scalars instead.",
        },
    ]
