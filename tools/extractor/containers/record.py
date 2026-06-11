"""Decoder for the typed-field "record" struct used inside `data.{}` tables.

A record (e.g. one `comm.<id>`, `bldg.<culture>.<id>`, etc.) reuses the same
16-byte directory-record shape as the container format (see `container.py`),
recursing one level deeper:

    struct RecordHeader {  // flag == 1
        uint32 flag;        // == 1
        char   tag[4];      // reversed ASCII, the record's own 4-letter ID
        int32  rel;         // unused
        int32  field_count; // number of FieldEntry structs that follow,
                             // PLUS ONE -- the last one is actually the start
                             // of the *next* record's header (see
                             // `iter_records`). Real field count is
                             // field_count - 1.
    }

    struct FieldEntry {  // flag != 1
        uint32 flag;  // field type code, see FieldFlag below
        char   name[4];  // reversed ASCII field name, e.g. "base", "name"
        int32  rel;   // meaning depends on flag (see FieldFlag)
        int32  size;  // byte width/length of the value
    }

This is a direct port of `documentation/scripts/te_record.py`; see
`documentation/01-container-format.md` ("Low-level record/field format")
for the full derivation and validation against ground-truth data.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import IntEnum
from typing import Any, Iterator

from .container import read_record

_RECORD = struct.Struct("<I4sii")


class FieldFlag(IntEnum):
    """Known FieldEntry.flag values."""

    INT32 = 0x41    # 'A': int32 scalar, value is `rel` itself
    FLOAT32 = 0x48  # 'H': float32 scalar, `rel`'s bits reinterpreted as float
    SMALL_INT = 0x58  # 'X': small int/bool scalar, value is `rel`
    STRING = 0x50   # 'P': NUL-terminated string at (header_offset + rel)
    REFERENCE = 0x40  # '@': 4-letter ID reference(s), see _decode_reference


def _read_cstring(data: bytes, offset: int) -> str:
    end = data.index(b"\x00", offset)
    return data[offset:end].decode("latin1", "replace")


def _as_4cc(value: int) -> str | int:
    """Decodes an int32 as a reversed-ASCII 4-letter tag if printable,
    otherwise returns the int unchanged."""
    raw = struct.pack("<i", value)[::-1]
    if all(0x20 <= b < 0x7F for b in raw):
        return raw.decode("ascii")
    return value


def _decode_reference(data: bytes, header_off: int, rel: int, size: int) -> Any:
    """Decodes a FieldFlag.REFERENCE field.

    size == 4: `rel` is itself a reversed-ASCII 4-letter tag (a single ID
    reference, e.g. `guar.bldg` -> the producing building's type).

    size >= 8 and size % 4 == 0: `rel` is a relative offset (from the record
    header) to `size / 4` back-to-back int32s, each independently re-decoded
    as a 4-letter tag if printable. Two common shapes:
      - size == 8: a single (a, b) pair, e.g. `guar.comm = (brsw, irsw)`
        ("either of these two weapons"), or a 1-input recipe
        `(commodity, quantity)`.
      - size == 16: two (a, b) pairs, e.g. a 2-input recipe
        `((copo, 2), (tino, 1))`.
    Other sizes (e.g. 12) are returned as a flat tuple.
    """
    if size == 4:
        return _as_4cc(rel)

    if size >= 8 and size % 4 == 0:
        roff = header_off + rel
        n = size // 4
        if 0 <= roff and roff + size <= len(data):
            values = struct.unpack_from("<" + "i" * n, data, roff)
            items = [_as_4cc(v) for v in values]
            if n % 2 == 0 and n > 2:
                return tuple(zip(items[0::2], items[1::2]))
            if n == 2:
                return (items[0], items[1])
            return tuple(items)

    return rel


@dataclass
class Field:
    """One decoded FieldEntry."""

    flag: int
    raw_rel: int
    size: int
    value: Any


def parse_record(data: bytes, header_off: int) -> tuple[dict[str, Field], int]:
    """Parses one record at `header_off` (must have flag == 1).

    Returns (fields, next_record_off). `fields` maps field name -> Field.
    `next_record_off` is the file offset of the next record's header in the
    same flat array (== header_off + 16 + 16 * real_field_count).
    """
    flag, _tag, _rel, field_count = read_record(data, header_off)
    if flag != 1:
        raise ValueError(f"expected record header (flag==1) at {header_off}, got flag={flag}")

    real_field_count = field_count - 1  # see module docstring
    fields: dict[str, Field] = {}
    for i in range(real_field_count):
        entry_off = header_off + 16 + i * 16
        entry_flag, name, rel, size = read_record(data, entry_off)

        value: Any
        if entry_flag == FieldFlag.INT32 and size == 4:
            value = rel
        elif entry_flag == FieldFlag.FLOAT32 and size == 4:
            value = struct.unpack("<f", struct.pack("<i", rel))[0]
        elif entry_flag == FieldFlag.SMALL_INT:
            value = rel
        elif entry_flag == FieldFlag.STRING:
            soff = header_off + rel
            value = _read_cstring(data, soff) if 0 <= soff < len(data) else None
        elif entry_flag == FieldFlag.REFERENCE:
            value = _decode_reference(data, header_off, rel, size)
        elif entry_flag == FieldFlag.INT32 and size > 4 and size % 4 == 0:
            roff = header_off + rel
            n = size // 4
            if 0 <= roff and roff + size <= len(data):
                value = struct.unpack_from("<" + "i" * n, data, roff)
            else:
                value = rel
        else:
            value = rel  # best-effort fallback for unrecognized flags

        fields[name] = Field(flag=entry_flag, raw_rel=rel, size=size, value=value)

    next_off = header_off + 16 + real_field_count * 16
    return fields, next_off


def iter_records(data: bytes, table_off: int, count: int) -> Iterator[tuple[str, int, dict[str, Field]]]:
    """Iterates `count` consecutive records starting at `table_off`.

    Yields (tag, header_offset, fields) for each record. `count` should be
    the number of children reported for the table's directory node (see
    `container.parse_tree`); the last "record" parsed by `parse_record`'s
    field-count handling is the table's own trailing directory header, which
    callers should ignore (it's only used to compute offsets correctly).
    """
    off = table_off
    for _ in range(count):
        fields, next_off = parse_record(data, off)
        _flag, tag, _rel, _count = read_record(data, off)
        yield tag, off, fields
        off = next_off
