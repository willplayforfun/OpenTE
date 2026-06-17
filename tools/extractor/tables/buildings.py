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
from ..containers.record import parse_record
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


def _get_footprint(bldg_data: bytes, bldg_root: DirNode,
                   culture: str, building_id: str, look: str | None) -> dict[str, int]:
    """Read `futx`/`futy` from `bldg.{}` → <culture>/<id>/rot0/terr (or regi).

    Lookup order:
    1. <culture>/<id>/rot0/terr  — culture-specific path (e.g. eur1/mark)
    2. cher/<look>/rot0/terr     — shared-sprite redirect when look is set
    3. <alt_culture>/<id>/rot0/terr — any other culture that has the same
       building id; handles cases like mon1/mon2 markets which have cher=0
       but no market entries in bldg.{}/mon1/ (the original engine likely
       falls through to a global default; we approximate by reusing whatever
       culture does have the data, since futx/futy are identical across
       cultures for the same building type).

    RE-confirmed: footprint lives in the `terr` (or `regi`) child of the
    building's `rot0` record in `bldg.{}`, read by
    `GhostBuilding::PhysicalInterface::GetFootprint` @ VA 0x444bb0 via tags
    `futx` (int32, flag 0x41) and `futy` (int32, flag 0x41).
    """
    def _read_futx_futy(culture_key: str, bldg_key: str) -> dict[str, int] | None:
        ce = find_child(bldg_root, culture_key)
        if ce is None or ce.dir is None:
            return None
        be = find_child(ce.dir, bldg_key)
        if be is None or be.dir is None:
            return None
        rot0 = find_child(be.dir, "rot0")
        if rot0 is None or rot0.dir is None:
            return None
        for layer in ("terr", "regi"):
            le = find_child(rot0.dir, layer)
            if le is None or le.dir is None:
                continue
            fields, _ = parse_record(bldg_data, le.abs_off)
            fx = fields.get("futx")
            fy = fields.get("futy")
            if fx is not None and fy is not None:
                return {"width": fx.value, "height": fy.value}
        return None

    result = _read_futx_futy(culture, building_id)
    if result:
        return result

    if look:
        result = _read_futx_futy("cher", look)
        if result:
            return result

    # Fallback: try any other culture that has this building id. Covers
    # mon1/mon2 markets (and similar gaps) where the primary culture has no
    # market entries in bldg.{} and cher=0/look=None, so the two paths above
    # both fail. All cultures store the same futx/futy for a given building
    # type, so borrowing from another culture is correct.
    for alt_culture in CULTURE_SETS:
        if alt_culture == culture:
            continue
        result = _read_futx_futy(alt_culture, building_id)
        if result:
            return result

    return {"width": 1, "height": 1}


def extract_buildings(data: bytes, root: DirNode,
                      bldg_data: bytes | None = None,
                      bldg_root: DirNode | None = None) -> dict[str, dict[str, Any]]:
    """Returns `{"<culture>.<id>": {...}, ...}` for every building record.

    Field provenance:
      - `name`/`desc`/`cost`/`look` come from the culture-set record itself
        (falling back to `defa` for `name`/`desc` if the culture record
        doesn't override them).
      - `category`/`type`/`military`/`religion` come from the `defa` record
        for the same building id.
      - `footprint` (`futx`/`futy`) comes from `bldg.{}` (the sprite
        container), not from `data.{}`. Pass `bldg_data`/`bldg_root` to get
        real dimensions; without them every building defaults to 1×1.
    """
    bldg_entry = find_child(root, "bldg")
    if bldg_entry is None or bldg_entry.dir is None:
        raise ValueError("'bldg' table not found in data.{}")
    data_bldg_root = bldg_entry.dir

    defa_entry = find_child(data_bldg_root, "defa")
    if defa_entry is None or defa_entry.dir is None:
        raise ValueError("'bldg.defa' table not found in data.{}")
    defa_records = {tag: fields for tag, fields in iter_table_records(data, defa_entry.dir)}

    buildings: dict[str, dict[str, Any]] = {}
    for culture in CULTURE_SETS:
        cset_entry = find_child(data_bldg_root, culture)
        if cset_entry is None or cset_entry.dir is None:
            continue

        for tag, fields in iter_table_records(data, cset_entry.dir):
            base_fields = defa_records.get(tag, {})
            look = fv(fields, "look")

            if bldg_data is not None and bldg_root is not None:
                footprint = _get_footprint(bldg_data, bldg_root, culture, tag, look)
            else:
                footprint = {"width": 1, "height": 1}

            buildings[f"{culture}.{tag}"] = {
                "name": fv(fields, "name") or fv(base_fields, "name") or "",
                "desc": fv(fields, "desc") or fv(base_fields, "desc") or "",
                "culture_set": culture,
                "build_cost": fv(fields, "cost", 0),
                "footprint": footprint,
                "category": fv(base_fields, "cate"),
                "type": fv(base_fields, "type"),
                "military": bool(fv(base_fields, "mili", 0)),
                "religion": bool(fv(base_fields, "reli", 0)),
                "look": look,
                "sprites": {"default": f"bldg.{culture}.{tag}"},
            }

    return buildings
