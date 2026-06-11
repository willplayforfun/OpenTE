"""Exports the `comm` table (commodity definitions) from `Data/data.{}`.

Each of the 135 commodities becomes one JSON object keyed by its 4-letter ID
(e.g. "brsw" = Bronze Sword), with its base price and food/luxury/military/
medicine category flags. See `documentation/01-container-format.md` and
`documentation/scripts/te_record.py` for how these fields were decoded.
"""
from __future__ import annotations

from typing import Any

from ..containers.container import DirNode, find_child, read_record
from ..containers.record import parse_record


def extract_commodities(data: bytes, root: DirNode) -> dict[str, dict[str, Any]]:
    """Returns {commodity_id: {field_name: value, ...}, ...} for every
    record in the `comm` table.

    Iteration follows `next_off` (the flat-array layout) and stops once it
    reaches the table's own trailing directory header -- see
    `documentation/CLAUDE.md` container-format gotcha #2 ("don't use
    tag-name matching to detect end of table").
    """
    comm_entry = find_child(root, "comm")
    if comm_entry is None or comm_entry.dir is None:
        raise ValueError("'comm' table not found in data.{}")
    comm_node = comm_entry.dir

    table_off = min(child.abs_off for child in comm_node.children)

    commodities: dict[str, dict[str, Any]] = {}
    off = table_off
    while off < comm_node.offset:
        flag, tag, _rel, _count = read_record(data, off)
        if flag != 1:
            break
        fields, next_off = parse_record(data, off)
        commodities[tag] = {name: field.value for name, field in fields.items()}
        off = next_off

    return commodities
