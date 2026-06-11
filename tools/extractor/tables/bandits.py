"""Exports the `band` table (bandit/raider types) from `Data/data.{}`.

See `OpenTE/spec/data-model.md` ("`tables/bandits.json`") and
`documentation/extracted/data_catalog_decoded.md` ("`band`") for the schema
this was ported from.
"""
from __future__ import annotations

from typing import Any

from ..containers.container import DirNode, find_child
from .common import fv, iter_table_records


def extract_bandits(data: bytes, root: DirNode) -> dict[str, dict[str, Any]]:
    """Returns `{bandit_id: {...}, ...}` for every record in `band`."""
    band_entry = find_child(root, "band")
    if band_entry is None or band_entry.dir is None:
        raise ValueError("'band' table not found in data.{}")

    bandits: dict[str, dict[str, Any]] = {}
    for tag, fields in iter_table_records(data, band_entry.dir):
        bandits[tag] = {
            "name": fv(fields, "name", ""),
            "desc": fv(fields, "desc", ""),
            "atta": fv(fields, "atta", 0),
            "defe": fv(fields, "defe", 0),
            "forc": fv(fields, "forc", 0),
            "rang": fv(fields, "rang", 0),
            "spee": fv(fields, "spee", 0),
            "amphibious": bool(fv(fields, "aqua", 0)),
        }

    return bandits
