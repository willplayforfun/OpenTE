"""Exports the `abil` table (special-merchant abilities) from `Data/data.{}`.

`abil` is a flat array of 13 records indexed 0-12 by a raw int32 (not a
4-letter id) -- see `documentation/CLAUDE.md` container-format gotcha #8.
`epis.<ep>.merc.<i>.abil` indexes into this array.

See `OpenTE/spec/data-model.md` ("`tables/abilities.json`") and
`documentation/scripts/te_abil.py` for the source this was ported from.
"""
from __future__ import annotations

from typing import Any

from ..containers.container import DirNode, find_child
from ..containers.record import iter_records
from .common import fv

_NUM_ABILITIES = 13


def extract_abilities(data: bytes, root: DirNode) -> list[dict[str, Any]]:
    """Returns a 13-element list (index 0-12) of ability dicts."""
    abil_entry = find_child(root, "abil")
    if abil_entry is None or abil_entry.dir is None:
        raise ValueError("'abil' table not found in data.{}")
    abil_node = abil_entry.dir

    table_off = min(child.abs_off for child in abil_node.children)

    abilities: list[dict[str, Any]] = []
    for idx, (_tag, _off, fields) in enumerate(iter_records(data, table_off, len(abil_node.children))):
        if idx >= _NUM_ABILITIES:
            break  # the 14th "record" is the table's own directory spillover
        abilities.append({
            "id": idx,
            "name": fv(fields, "name", ""),
            "desc": fv(fields, "desc", ""),
            "bonus": fv(fields, "bonu", 0),
            "rank": fv(fields, "rank", 0),
        })

    return abilities
