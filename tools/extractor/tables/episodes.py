"""Exports `tables/episodes.json` from `Data/data.{}`'s `epis.<ep>.*`
sub-tables: `comm` (production recipes/obsolescence), `merc` (famous
merchants), `tech` (available advances), `dmnd` (building commodity demand),
`path` (pathway movement costs), and `regi` (regions).

See `OpenTE/spec/data-model.md` ("`tables/episodes.json`") and
`documentation/scripts/te_episode.py` / `te_production.py` / `te_abil.py` /
`te_tech.py` for the sources this was ported from.
"""
from __future__ import annotations

from typing import Any

from ..containers.container import DirNode, find_child
from ..containers.record import parse_record
from .common import fv, iter_table_records

_MOVEMENT_NETWORKS = ("none", "road", "trai", "rail", "cana", "wate")


def _extract_inputs(inpu_value: Any) -> list[dict[str, Any]]:
    """Decodes an `inpu` field value into `[{"commodity": id, "amount": qty}, ...]`.

    `inpu` is None (no recipe), a single `(commodity, qty)` pair, or a tuple
    of two such pairs (`((c1, q1), (c2, q2))`) -- see
    `documentation/CLAUDE.md` container-format gotcha #7.
    """
    if inpu_value is None:
        return []
    if isinstance(inpu_value, tuple) and len(inpu_value) == 2:
        first, second = inpu_value
        if isinstance(first, tuple):
            return [{"commodity": c, "amount": q} for c, q in inpu_value]
        return [{"commodity": first, "amount": second}]
    return []


def _extract_commodities(data: bytes, comm_dir: DirNode) -> dict[str, dict[str, Any]]:
    commodities: dict[str, dict[str, Any]] = {}
    for tag, fields in iter_table_records(data, comm_dir):
        rate = fv(fields, "rate")
        outputs = [{"commodity": tag, "amount": rate}] if rate is not None else []
        commodities[tag] = {
            "inputs": _extract_inputs(fv(fields, "inpu")),
            "outputs": outputs,
            "produced_by_building": fv(fields, "prod"),
            "research_required": fv(fields, "tech"),
            "base_price_override": fv(fields, "base"),
            "obsolescence": {
                "superseded_by": fv(fields, "supe"),
                "inferior_to": fv(fields, "infe"),
                "removed": fv(fields, "remo"),
            },
        }
    return commodities


def _extract_merchants(data: bytes, merc_dir: DirNode) -> list[dict[str, Any]]:
    merchants = []
    for _tag, fields in iter_table_records(data, merc_dir):
        merchants.append({
            "name": fv(fields, "name", ""),
            "hire_cost": fv(fields, "cost", 0),
            "ability": fv(fields, "abil"),
        })
    return merchants


def _extract_demand(data: bytes, dmnd_dir: DirNode) -> dict[str, dict[str, dict[str, Any]]]:
    demand: dict[str, dict[str, dict[str, Any]]] = {}
    for btag, bfields in iter_table_records(data, dmnd_dir):
        building_demand: dict[str, dict[str, Any]] = {}
        for ctag, field in bfields.items():
            value = field.value
            if isinstance(value, tuple) and len(value) == 2:
                weight, amount = value
            else:
                weight, amount = None, value
            building_demand[ctag] = {"weight": weight, "amount": amount}
        demand[btag] = building_demand
    return demand


def _extract_movement_costs(data: bytes, path_dir: DirNode) -> dict[str, dict[str, Any]]:
    movement_costs: dict[str, dict[str, Any]] = {}
    for tag, fields in iter_table_records(data, path_dir):
        movement_costs[tag] = {net: fv(fields, net, 0) for net in _MOVEMENT_NETWORKS}
    return movement_costs


def _extract_regions(data: bytes, regi_dir: DirNode) -> list[dict[str, Any]]:
    regions = []
    for tag, fields in iter_table_records(data, regi_dir):
        regions.append({
            "id": tag,
            "name": fv(fields, "name", ""),
            "file": fv(fields, "file"),
            "music": fv(fields, "musi"),
            "x": fv(fields, "xcoo"),
            "y": fv(fields, "ycoo"),
        })
    return regions


def extract_episodes(data: bytes, root: DirNode) -> dict[str, dict[str, Any]]:
    """Returns `{episode_id: {...}, ...}` for every episode in `epis`."""
    epis_entry = find_child(root, "epis")
    if epis_entry is None or epis_entry.dir is None:
        raise ValueError("'epis' table not found in data.{}")

    episodes: dict[str, dict[str, Any]] = {}
    for ep_entry in epis_entry.dir.children:
        if ep_entry.kind != "dir" or ep_entry.dir is None:
            continue
        ep = ep_entry.tag
        ep_dir = ep_entry.dir

        ep_fields, _next_off = parse_record(data, ep_dir.offset)

        comm_entry = find_child(ep_dir, "comm")
        merc_entry = find_child(ep_dir, "merc")
        tech_entry = find_child(ep_dir, "tech")
        dmnd_entry = find_child(ep_dir, "dmnd")
        path_entry = find_child(ep_dir, "path")
        regi_entry = find_child(ep_dir, "regi")

        technologies = []
        if tech_entry is not None and tech_entry.dir is not None:
            technologies = [f"{ep}.{tag}" for tag, _fields in iter_table_records(data, tech_entry.dir)]

        episodes[ep] = {
            "name": fv(ep_fields, "titl", ""),
            "desc": fv(ep_fields, "desc", ""),
            "regions": _extract_regions(data, regi_entry.dir) if regi_entry and regi_entry.dir else [],
            "commodities": _extract_commodities(data, comm_entry.dir) if comm_entry and comm_entry.dir else {},
            "merchants": _extract_merchants(data, merc_entry.dir) if merc_entry and merc_entry.dir else [],
            "technologies": technologies,
            "demand": _extract_demand(data, dmnd_entry.dir) if dmnd_entry and dmnd_entry.dir else {},
            "movement_costs": _extract_movement_costs(data, path_entry.dir) if path_entry and path_entry.dir else {},
        }

    return episodes
