"""Exports `tables/technologies.json`, joining `tech.<ep>.<id>` (flavor text)
with `epis.<ep>.tech.<id>` (gameplay fields) from `Data/data.{}`.

See `OpenTE/spec/data-model.md` ("`tables/technologies.json`") and
`documentation/scripts/te_tech.py` for the source this was ported from.

`excl` (`0` or `600`, purpose unconfirmed -- see
`documentation/08-investigation-needed.md` T0.6/B8) is passed through
opaquely as `extra.excl` rather than mapped to a named field.
"""
from __future__ import annotations

from typing import Any

from ..containers.container import DirNode, find_child
from .common import fv, iter_table_records


def _as_list(value: Any) -> list[Any]:
    if value is None:
        return []
    if isinstance(value, tuple):
        return list(value)
    return [value]


def extract_technologies(data: bytes, root: DirNode) -> dict[str, dict[str, Any]]:
    """Returns `{"<ep>.<id>": {...}, ...}` for every per-episode advance."""
    tech_entry = find_child(root, "tech")
    if tech_entry is None or tech_entry.dir is None:
        raise ValueError("'tech' table not found in data.{}")

    epis_entry = find_child(root, "epis")
    if epis_entry is None or epis_entry.dir is None:
        raise ValueError("'epis' table not found in data.{}")

    # episode -> {id -> flavor text}, from `tech.<ep>.<id>.long`
    descriptions: dict[str, dict[str, str]] = {}
    for ep_entry in tech_entry.dir.children:
        if ep_entry.kind != "dir" or ep_entry.dir is None:
            continue
        ep = ep_entry.tag
        descriptions[ep] = {}
        for tag, fields in iter_table_records(data, ep_entry.dir):
            long_text = fv(fields, "long")
            if long_text is not None:
                descriptions[ep][tag] = long_text

    technologies: dict[str, dict[str, Any]] = {}
    for ep_entry in epis_entry.dir.children:
        if ep_entry.kind != "dir" or ep_entry.dir is None:
            continue
        ep = ep_entry.tag

        ep_tech_entry = find_child(ep_entry.dir, "tech")
        if ep_tech_entry is None or ep_tech_entry.dir is None:
            continue

        for tag, fields in iter_table_records(data, ep_tech_entry.dir):
            technologies[f"{ep}.{tag}"] = {
                "name": "",
                "flavor_text": descriptions.get(ep, {}).get(tag, ""),
                "episode": ep,
                "cost": fv(fields, "pric", 0),
                "available_at_tick": fv(fields, "dela", 0),
                "unlocks": {
                    "buildings": _as_list(fv(fields, "bldg")),
                    "transporters": _as_list(fv(fields, "unit")),
                    "pathways": _as_list(fv(fields, "path")),
                },
                "extra": {"excl": fv(fields, "excl")},
            }

    return technologies
