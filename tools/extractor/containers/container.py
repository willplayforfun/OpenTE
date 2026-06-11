"""Generic reader for the Trade Empires ".{}" container format.

Every persistent data file in Trade Empires (the economy database, maps,
save games, sprites, fonts...) uses the same recursive tagged-directory
container format. This module provides the minimal primitives needed to
open one of these files and walk its directory tree; see
`documentation/01-container-format.md` in the reverse-engineering repo for
the full format writeup this is ported from.

File layout::

    Header (16 bytes):
        magic:      8 bytes  -> 75 b1 c0 ba 00 00 01 00 (constant)
        footer_off: uint32 LE -> absolute offset of the root directory record
        file_size:  uint32 LE -> total file size

    Directory record (16 bytes), repeated:
        flag:          uint32 LE (1 for directory/group nodes)
        tag:           4 bytes ASCII, stored REVERSED (e.g. b'atad' -> "data")
        rel:           int32 LE, signed relative offset
        count_or_size: uint32 LE
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field
from pathlib import Path

_RECORD = struct.Struct("<I4sii")


@dataclass
class DirNode:
    """One directory record and its children, recursively parsed."""

    offset: int
    flag: int
    tag: str
    count: int
    children: list["ChildEntry"] = field(default_factory=list)


@dataclass
class ChildEntry:
    """One entry in a DirNode's children list.

    `kind` is "dir" (in which case `dir` is populated) or "leaf" (in which
    case [abs_off, abs_off + size) is an opaque blob to be interpreted by the
    caller, e.g. as a sprite or a record table).
    """

    tag: str
    abs_off: int
    size: int
    flag: int
    kind: str
    dir: DirNode | None = None


def read_record(data: bytes, offset: int) -> tuple[int, str, int, int]:
    """Reads the 16-byte (flag, tag, rel, count_or_size) record at `offset`.

    `tag` is decoded from its on-disk reversed-ASCII form into normal
    reading order (e.g. on-disk b'atad' -> "data").
    """
    flag, tag, rel, val = _RECORD.unpack_from(data, offset)
    return flag, tag[::-1].decode("latin1", "replace"), rel, val


def parse_tree(data: bytes, offset: int, max_depth: int = 12) -> DirNode:
    """Recursively parses the directory tree rooted at `offset`.

    The root directory record (at file offset == footer_off, see `load`)
    is special: its `count` field is `len(children) + 1` -- the "+1"
    accounts for the root record itself being included in its own footer
    block. This off-by-one does NOT apply to non-root directory records.
    """
    return _parse_tree(data, offset, max_depth, depth=0, is_root=True)


def _parse_tree(data: bytes, offset: int, max_depth: int, depth: int, is_root: bool) -> DirNode:
    flag, tag, _rel, count = read_record(data, offset)
    node = DirNode(offset=offset, flag=flag, tag=tag, count=count)
    if depth >= max_depth:
        return node

    n_children = count - 1 if is_root else count
    for i in range(n_children):
        entry_off = offset + 16 + i * 16
        entry_flag, entry_tag, entry_rel, entry_size = read_record(data, entry_off)
        abs_off = offset + entry_rel

        is_dir = False
        if 0 <= abs_off <= len(data) - 16:
            child_flag, child_tag, _r, child_count = read_record(data, abs_off)
            if child_flag == 1 and child_tag == entry_tag and child_count * 16 == entry_size:
                is_dir = True

        if is_dir:
            child_dir = _parse_tree(data, abs_off, max_depth, depth + 1, is_root=False)
            node.children.append(
                ChildEntry(tag=entry_tag, abs_off=abs_off, size=entry_size,
                           flag=entry_flag, kind="dir", dir=child_dir)
            )
        else:
            node.children.append(
                ChildEntry(tag=entry_tag, abs_off=abs_off, size=entry_size,
                           flag=entry_flag, kind="leaf")
            )

    return node


def load(path: Path) -> tuple[bytes, int]:
    """Reads `path` and returns (file_bytes, footer_offset)."""
    data = path.read_bytes()
    _magic, footer_off, size = struct.unpack_from("<8sII", data, 0)
    if size != len(data):
        raise ValueError(f"{path}: header size {size} != actual file size {len(data)}")
    return data, footer_off


def find_child(node: DirNode, tag: str) -> ChildEntry | None:
    """Returns the first direct child of `node` with the given tag, or None."""
    for child in node.children:
        if child.tag == tag:
            return child
    return None
