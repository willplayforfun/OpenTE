"""Exports the `bldg` table (building definitions) from `Data/data.{}`.

`bldg` is organized as one sub-table per culture/era set (`chi1`, `chi2`,
`eur1`-`eur5`, `ind1`-`ind3`, `mon1`/`mon2`, `per1`-`per3`, `tbaz`), each
containing per-building records with `cost`/`look`/`name`/`desc`. Shared
category/type/flag fields (`cate`/`type`/`mili`/`reli`) live in the `defa`
("default") set, keyed by the same building id.

See `documentation/02-data-catalog.md` and
`documentation/extracted/data_catalog_decoded.md` for the source tables this
was ported from.
"""
from __future__ import annotations

from typing import Any

from ..containers.container import DirNode, find_child
from .common import fv, iter_table_records

# Culture/era sets that hold actual building definitions (excludes `defa`,
# `cher`, `flag`, `regi`, which are shared/sprite-only sets).
CULTURE_SETS = (
    "chi1", "chi2",
    "eur1", "eur2", "eur3", "eur4", "eur5",
    "ind1", "ind2", "ind3",
    "mon1", "mon2",
    "per1", "per2", "per3",
    "tbaz",
)

# Building footprint (`futx`/`futy` in `bldg.{}`) is not yet decoded -- see
# `documentation/CLAUDE.md` gotchas about validating new field hypotheses.
# Stage 1 only needs static rendering at a building's origin tile, so every
# building gets this placeholder 1x1 footprint until Stage 2 resolves the
# real per-building dimensions (tracked in
# `OpenTE/implementation/roadmap.md`'s placeholder inventory).
_PLACEHOLDER_FOOTPRINT = {"width": 1, "height": 1}


def extract_buildings(data: bytes, root: DirNode) -> dict[str, dict[str, Any]]:
    """Returns `{"<culture>.<id>": {...}, ...}` for every building record.

    Field provenance:
      - `name`/`desc`/`cost`/`look` come from the culture-set record itself
        (falling back to `defa` for `name`/`desc` if the culture record
        doesn't override them).
      - `category`/`type`/`military`/`religion` come from the `defa` record
        for the same building id.
    """
    bldg_entry = find_child(root, "bldg")
    if bldg_entry is None or bldg_entry.dir is None:
        raise ValueError("'bldg' table not found in data.{}")
    bldg_root = bldg_entry.dir

    defa_entry = find_child(bldg_root, "defa")
    if defa_entry is None or defa_entry.dir is None:
        raise ValueError("'bldg.defa' table not found in data.{}")
    defa_records = {tag: fields for tag, fields in iter_table_records(data, defa_entry.dir)}

    buildings: dict[str, dict[str, Any]] = {}
    for culture in CULTURE_SETS:
        cset_entry = find_child(bldg_root, culture)
        if cset_entry is None or cset_entry.dir is None:
            continue

        for tag, fields in iter_table_records(data, cset_entry.dir):
            base_fields = defa_records.get(tag, {})
            look = fv(fields, "look")

            buildings[f"{culture}.{tag}"] = {
                "name": fv(fields, "name") or fv(base_fields, "name") or "",
                "desc": fv(fields, "desc") or fv(base_fields, "desc") or "",
                "culture_set": culture,
                "build_cost": fv(fields, "cost", 0),
                "footprint": dict(_PLACEHOLDER_FOOTPRINT),
                "category": fv(base_fields, "cate"),
                "type": fv(base_fields, "type"),
                "military": bool(fv(base_fields, "mili", 0)),
                "religion": bool(fv(base_fields, "reli", 0)),
                "look": look,
                "sprites": {"default": f"bldg.{culture}.{tag}"},
            }

    return buildings
