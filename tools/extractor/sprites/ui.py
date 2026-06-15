"""Extracts all UI sprite leaves from `a_ui,6.{}` and `d_ui,5.{}`, plus the
main-menu `main/` group from `a_ui,5.{}`.

`a_ui,6.{}` (57 MB, RGB565) holds the main game UI: 181 `bg6a` leaves
organised under 26 dialog-group top-level children (`adva`, `comm`, `cons`,
`curs`, `depo`, ...).  `a_ui,5.{}` contains the same dialog-group sprites in
RGB555, plus an additional `main/` group (main-menu background, button
highlights, logos) that is exclusive to `a_ui,5.{}` and not present in
`a_ui,6.{}`.

`d_ui,5.{}` (1.1 MB, ARGB4444) holds debug/dev UI sprites with per-pixel
alpha: 379 leaves including ability icons at `d_ui/abil/<name>` and several
other panel groups.  `d_ui,6.{}` is byte-identical -- we skip it.

All containers use the universal `bg6a` sprite format, already handled by
`sprites.sprite.decode_sprite`. This module recursively walks every directory
in each container, attempts to decode each leaf as a `bg6a` sprite, and
writes the decoded RGBA8888 pixels as a PNG.

Output layout (relative to `game_data/`)::

    sprites/ui/a_ui/<group>/.../<leaf>.png
    sprites/ui/d_ui/<group>/.../<leaf>.png
    sprites/ui/main/<leaf>.png              ← main-menu group from a_ui,5.{}

Sprite IDs follow the dotted container-path convention::

    ui.a_ui.<group>...<leaf>
    ui.d_ui.<group>...<leaf>
    ui.main.<leaf>
"""
from __future__ import annotations

import re
from pathlib import Path

from ..containers.container import ChildEntry, DirNode
from ..manifest import SpriteEntry
from .sprite import decode_sprite, write_png_rgba

# Characters that cannot appear safely in cross-platform filenames.
_UNSAFE_CHARS = re.compile(r"[^\w\-]")


def _safe_tag(tag: str) -> str:
    """Returns `tag` with filesystem-unsafe characters replaced by underscores."""
    return _UNSAFE_CHARS.sub("_", tag) or "_"


def _collect_leaves(node: DirNode, path_parts: list[str]) -> list[tuple[list[str], ChildEntry]]:
    """Recursively collects every leaf entry under `node`.

    Returns a list of (path_parts, leaf) pairs where path_parts is the full
    tag path from the container root down to and including the leaf's own tag.
    """
    result: list[tuple[list[str], ChildEntry]] = []
    for child in node.children:
        child_path = path_parts + [child.tag]
        if child.kind == "leaf":
            result.append((child_path, child))
        elif child.kind == "dir" and child.dir is not None:
            result.extend(_collect_leaves(child.dir, child_path))
    return result


def extract_ui_sprites(
    container_name: str,
    data: bytes,
    root: DirNode,
    output_dir: Path,
) -> list[SpriteEntry]:
    """Extracts all `bg6a` sprite leaves from a UI container.

    `container_name` is the second component of each sprite ID and the first
    subdirectory level under ``sprites/ui/`` (e.g. ``"a_ui"`` or ``"d_ui"``).

    Every leaf in the tree is attempted; leaves that do not decode as a `bg6a`
    sprite (wrong magic, inconsistent size) are silently skipped.

    Returns a list of `SpriteEntry` records for the manifest.
    """
    sprites_dir = output_dir / "sprites" / "ui" / container_name
    sprites_dir.mkdir(parents=True, exist_ok=True)

    leaves = _collect_leaves(root, [])
    entries: list[SpriteEntry] = []
    skipped = 0

    total = len(leaves)
    for i, (path_parts, leaf) in enumerate(leaves, 1):
        sprite_label = ".".join(path_parts)
        print(f"  [{i}/{total}] {container_name}: {sprite_label:<60}", end="\r", flush=True)
        sprite = decode_sprite(data, leaf.abs_off, leaf.size)
        if sprite is None:
            skipped += 1
            continue

        # Build a safe filesystem path while preserving the tag hierarchy.
        safe_parts = [_safe_tag(p) for p in path_parts]
        file_parts = ["sprites", "ui", container_name] + safe_parts
        file_parts[-1] += ".png"
        rel_path = Path(*file_parts)

        abs_path = output_dir / rel_path
        abs_path.parent.mkdir(parents=True, exist_ok=True)
        write_png_rgba(abs_path, sprite.width, sprite.height, sprite.rgba)

        entries.append(SpriteEntry(
            id="ui." + container_name + "." + ".".join(path_parts),
            file=str(rel_path).replace("\\", "/"),
            width=sprite.width,
            height=sprite.height,
            anchor_x=sprite.anchor_x,
            anchor_y=sprite.anchor_y,
        ))

    print(f"  {container_name}: {len(entries)} sprites written" +
          (f" ({skipped} skipped)" if skipped else "") + " " * 40)

    return entries


def extract_main_menu_sprites(
    data: bytes,
    root: DirNode,
    output_dir: Path,
) -> list[SpriteEntry]:
    """Extracts the `main/` group from `a_ui,5.{}` (main-menu sprites).

    Outputs to ``sprites/ui/main/`` with sprite IDs like ``ui.main.<tag>``.
    The `main/` group is exclusive to `a_ui,5.{}` and contains: lbak (full-
    screen background), stup/stdn (button highlight sprites), dark/drkr
    (overlay tints), brik (brick texture), eido/frog/mile (logos), line
    (divider), and the tiny `comb` entry (skipped if < 32 bytes).
    """
    main_node: DirNode | None = None
    for child in root.children:
        if child.tag == "main" and child.kind == "dir" and child.dir is not None:
            main_node = child.dir
            break
    if main_node is None:
        print("  warning: 'main' group not found in a_ui,5.{}")
        return []

    sprites_dir = output_dir / "sprites" / "ui" / "main"
    sprites_dir.mkdir(parents=True, exist_ok=True)

    leaves = _collect_leaves(main_node, [])
    entries: list[SpriteEntry] = []
    skipped = 0

    for path_parts, leaf in leaves:
        sprite = decode_sprite(data, leaf.abs_off, leaf.size)
        if sprite is None:
            skipped += 1
            continue

        safe_parts = [_safe_tag(p) for p in path_parts]
        rel_path = Path("sprites", "ui", "main",
                        *safe_parts[:-1], safe_parts[-1] + ".png")
        abs_path = output_dir / rel_path
        abs_path.parent.mkdir(parents=True, exist_ok=True)
        write_png_rgba(abs_path, sprite.width, sprite.height, sprite.rgba)

        entries.append(SpriteEntry(
            id="ui.main." + ".".join(path_parts),
            file=str(rel_path).replace("\\", "/"),
            width=sprite.width,
            height=sprite.height,
            anchor_x=sprite.anchor_x,
            anchor_y=sprite.anchor_y,
        ))

    print(f"  main menu: {len(entries)} sprites written" +
          (f" ({skipped} skipped)" if skipped else ""))
    return entries
