"""Exports the `even` table (random/scripted event definitions) from
`Data/data.{}`.

Only `name` plus the original's numeric tuning parameters (`scop` = scope,
`maxd`/`mind`/`medd` = max/min/medium duration, `spec`/`luxd`/`food`/`mild`
= effect-magnitude knobs whose exact per-event meaning isn't decoded) are
present in the source data -- there is no `desc` field. Event
*triggers/effects* are not part of the original format; per
[simulation.md](../../spec/simulation.md) the clone defines its own event
scripting, so these raw parameters are passed through under `params` as a
starting point/reference rather than mapped to named clone fields.
"""
from __future__ import annotations

from typing import Any

from ..containers.container import DirNode, find_child
from .common import iter_table_records


def extract_events(data: bytes, root: DirNode) -> dict[str, dict[str, Any]]:
    """Returns `{event_id: {"name": ..., "params": {...}}, ...}` for every
    record in `even`."""
    even_entry = find_child(root, "even")
    if even_entry is None or even_entry.dir is None:
        raise ValueError("'even' table not found in data.{}")

    events: dict[str, dict[str, Any]] = {}
    for tag, fields in iter_table_records(data, even_entry.dir):
        params = {name: field.value for name, field in fields.items() if name != "name"}
        events[tag] = {
            "name": fields.get("name").value if "name" in fields else "",
            "params": params,
        }

    return events
