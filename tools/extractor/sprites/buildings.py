"""Resolves a `bldg.{}` sprite path for a building defined in `data.{}`.

`bldg.{}` (the sprite container) has 18 top-level culture/era sets matching
`data.{}`'s `bldg` table (`chi1`, `chi2`, `eur1`-`eur5`, `ind1`-`ind3`,
`mon1`/`mon2`, `per1`-`per3`) plus a few extra sets (`cher`, `defa`, `flag`,
`regi`, `tbaz`).

For most culture sets (`eur1`, `per1`, `ind1`, ...), each building's sprites
live directly under `<culture>/<building_id>/`. For China (`chi1`/`chi2`),
production buildings instead live under `cher/<look>/` where `look` is the
building's `look` field from `data.{}` (e.g. `chi1.baza` has `look=cbaz`,
sprites live at `cher/cbaz/`) -- `chi1`/`chi2` themselves only contain
infrastructure (`dwel`, `dock`, `head`, ...) keyed by building id directly.

Each building sprite directory has one subdirectory per rotation (`rot0`-
`rot3`), and each rotation has a `terr` (ground-level) and/or `regi`
sub-directory containing a `pla0` leaf -- the static placed-building sprite.
This is a direct generalization of the path used by the toolchain spike
(`["cher", "cbaz", "rot0", "terr", "pla0"]`).
"""
from __future__ import annotations

from ..containers.container import ChildEntry, DirNode, find_child

_ROTATIONS = ("rot0", "rot1", "rot2", "rot3")
_LAYERS = ("terr", "regi")


def _find_pla0(building_dir: DirNode) -> list[str] | None:
    for rot in _ROTATIONS:
        rot_entry = find_child(building_dir, rot)
        if rot_entry is None or rot_entry.dir is None:
            continue
        for layer in _LAYERS:
            layer_entry = find_child(rot_entry.dir, layer)
            if layer_entry is None or layer_entry.dir is None:
                continue
            pla0 = find_child(layer_entry.dir, "pla0")
            if pla0 is not None and pla0.kind == "leaf":
                return [rot, layer, "pla0"]
    return None


def find_building_sprite_path(bldg_root: DirNode, culture: str, building_id: str,
                                look: str | None) -> list[str] | None:
    """Returns the path (list of tags, relative to `bldg_root`) to the
    building's static placed-building sprite leaf, or None if not found.

    Tries `<culture>/<building_id>/...` first, then falls back to
    `cher/<look>/...` (China's indirection via the `look` field).
    """
    culture_entry = find_child(bldg_root, culture)
    if culture_entry is not None and culture_entry.dir is not None:
        building_entry = find_child(culture_entry.dir, building_id)
        if building_entry is not None and building_entry.dir is not None:
            tail = _find_pla0(building_entry.dir)
            if tail is not None:
                return [culture, building_id] + tail

    if look:
        cher_entry = find_child(bldg_root, "cher")
        if cher_entry is not None and cher_entry.dir is not None:
            look_entry = find_child(cher_entry.dir, look)
            if look_entry is not None and look_entry.dir is not None:
                tail = _find_pla0(look_entry.dir)
                if tail is not None:
                    return ["cher", look] + tail

    return None
