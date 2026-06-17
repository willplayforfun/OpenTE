"""Extracts the UI / localization string table from ``text.{}``.

``text.{}`` is a standard ``.{}`` container whose directory tree mirrors the
game's dotted string keys: e.g. the leaf reached by ``cons`` -> ``labl`` ->
``dema`` holds the string ``"DEMAND BUILDINGS"`` and is keyed as
``"cons.labl.dema"``.  The original game looks these up through its text-scope
helper (``0x576170`` -> ``0x402c00`` etc.); OpenTE loads the whole table and
looks strings up by the same dotted key.

Output: a flat ``{ "<dotted.key>": "<string>" }`` map, written by the caller to
``game_data/tables/strings.json`` and loaded by ``DataRegistry`` as the
``strings`` table.

Placeholders
------------
Many strings carry positional placeholder tokens like ``[1 cost]`` /
``[1 building]`` / ``[2 price]`` (e.g. ``cons.labl.conf`` =
``"CONFIRM     ([1 cost] coins)"``).  These are substituted at *runtime* by the
consumer with the appropriate value -- this module emits the templates verbatim.

Encoding
--------
The strings are Windows-1252 (cp1252): notably ``0x80`` is the Euro sign.  We
decode as cp1252 so those bytes round-trip into proper Unicode, then the JSON
writer emits UTF-8.
"""
from __future__ import annotations

from ..containers.container import ChildEntry, DirNode


def _decode_leaf(text_data: bytes, entry: ChildEntry) -> str:
    blob = text_data[entry.abs_off : entry.abs_off + entry.size]
    # Strings are NUL-terminated; the leaf may carry trailing slack after it.
    nul = blob.find(b"\x00")
    if nul >= 0:
        blob = blob[:nul]
    return blob.decode("cp1252", "replace")


def extract_strings(text_data: bytes, text_root: DirNode) -> dict[str, str]:
    """Walks ``text.{}``'s directory tree into a flat dotted-key -> string map.

    ``text_root`` is the tree returned by ``parse_tree(text_data, footer)``.
    Top-level scopes (``cons``, ``adva``, ``tuto``, ...) become the first key
    segment, so a leaf is keyed exactly as the game references it (no ``text.``
    prefix), e.g. ``"cons.labl.titl"``.
    """
    strings: dict[str, str] = {}

    def walk(node: DirNode, prefix: str) -> None:
        for child in node.children:
            key = f"{prefix}.{child.tag}" if prefix else child.tag
            if child.kind == "dir" and child.dir is not None:
                walk(child.dir, key)
            else:
                strings[key] = _decode_leaf(text_data, child)

    walk(text_root, "")
    return strings
