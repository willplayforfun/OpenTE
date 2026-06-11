"""Exports the `tran` table (transporter definitions) from `Data/data.{}`,
joined with each episode's `epis.<ep>.tran` per-episode stats.

See `OpenTE/spec/data-model.md` ("`tables/transporters.json`") and
`documentation/extracted/data_catalog_episode.md` ("Transporters (`tran`)")
for the schema this was ported from.
"""
from __future__ import annotations

from typing import Any

from ..containers.container import DirNode, find_child
from .common import fv, iter_table_records

_SPEED_NETWORKS = ("none", "road", "trai", "rail", "cana", "deep", "dese")


def extract_transporters(data: bytes, root: DirNode) -> dict[str, dict[str, Any]]:
    """Returns `{transporter_id: {...}, ...}` for every record in `tran`,
    with a `by_episode` map populated from `epis.<ep>.tran`."""
    tran_entry = find_child(root, "tran")
    if tran_entry is None or tran_entry.dir is None:
        raise ValueError("'tran' table not found in data.{}")

    transporters: dict[str, dict[str, Any]] = {}
    for tag, fields in iter_table_records(data, tran_entry.dir):
        transporters[tag] = {
            "name": fv(fields, "name", ""),
            "depot_class": fv(fields, "depo"),
            "is_boat": bool(fv(fields, "boat", 0)),
            "by_episode": {},
        }

    epis_entry = find_child(root, "epis")
    if epis_entry is None or epis_entry.dir is None:
        raise ValueError("'epis' table not found in data.{}")

    for ep_entry in epis_entry.dir.children:
        if ep_entry.kind != "dir" or ep_entry.dir is None:
            continue
        ep = ep_entry.tag

        ep_tran_entry = find_child(ep_entry.dir, "tran")
        if ep_tran_entry is None or ep_tran_entry.dir is None:
            continue

        for tag, fields in iter_table_records(data, ep_tran_entry.dir):
            if tag not in transporters:
                continue
            transporters[tag]["by_episode"][ep] = {
                "capacity": fv(fields, "capa", 0),
                "cost": fv(fields, "cost", 0),
                "path_type": fv(fields, "path", "none"),
                "tech": fv(fields, "tech"),
                "speed": {net: fv(fields, net, 0) for net in _SPEED_NETWORKS},
            }

    return transporters
