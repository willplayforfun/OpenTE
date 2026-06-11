"""Exports the `guar` table (guard/military unit types) from `Data/data.{}`.

See `OpenTE/spec/data-model.md` ("`tables/guards.json`") and
`documentation/extracted/data_catalog_decoded.md` ("`guar`") for the schema
this was ported from.
"""
from __future__ import annotations

from typing import Any

from ..containers.container import DirNode, find_child
from .common import fv, iter_table_records


def extract_guards(data: bytes, root: DirNode) -> dict[str, dict[str, Any]]:
    """Returns `{guard_id: {...}, ...}` for every record in `guar`."""
    guar_entry = find_child(root, "guar")
    if guar_entry is None or guar_entry.dir is None:
        raise ValueError("'guar' table not found in data.{}")

    guards: dict[str, dict[str, Any]] = {}
    for tag, fields in iter_table_records(data, guar_entry.dir):
        guards[tag] = {
            "name": fv(fields, "name", ""),
            "desc": fv(fields, "desc", ""),
            "atta": fv(fields, "atta", 0),
            "defe": fv(fields, "defe", 0),
            "forc": fv(fields, "forc", 0),
            "rang": fv(fields, "rang", 0),
            "spee": fv(fields, "spee", 0),
            "amphibious": bool(fv(fields, "aqua", 0)),
            "cost": fv(fields, "cost", 0),
            "produced_by_building": fv(fields, "bldg"),
            "required_weapon": fv(fields, "comm"),
        }

    return guards
