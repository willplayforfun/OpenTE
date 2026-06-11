"""Shared helpers for table extractors.

Most tables in `Data/data.{}` are flat arrays of typed records (see
`documentation/01-container-format.md`). `iter_table_records` walks a
directory node's children and yields `(tag, fields)` for each real record,
skipping the "+1 spillover" entry (the table's own trailing directory header
misread as a child -- see `documentation/CLAUDE.md` container-format gotchas
#1/#2/#9).
"""
from __future__ import annotations

from typing import Any, Iterator

from ..containers.container import DirNode, read_record
from ..containers.record import Field, parse_record


def iter_table_records(data: bytes, node: DirNode) -> Iterator[tuple[str, dict[str, Field]]]:
    """Yields `(tag, fields)` for each real record directly under `node`.

    Skips children whose own on-disk tag (re-read via `read_record`) isn't
    one of `node`'s child tags -- this is the table's trailing directory
    spillover, not a real record.
    """
    child_tags = {child.tag for child in node.children}
    for child in sorted(node.children, key=lambda c: c.abs_off):
        if child.kind != "dir":
            continue
        _flag, tag, _rel, _count = read_record(data, child.abs_off)
        if tag not in child_tags:
            continue
        fields, _next_off = parse_record(data, child.abs_off)
        yield tag, fields


def fv(fields: dict[str, Field], name: str, default: Any = None) -> Any:
    """Returns `fields[name].value`, or `default` if `name` isn't present."""
    field = fields.get(name)
    return default if field is None else field.value
