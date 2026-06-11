"""JSON writing helper for table exporters.

Decoded record fields can contain tuples (e.g. recipe `(commodity, qty)`
pairs from `record.py`'s REFERENCE decoding); `json` only serializes lists,
so this recursively converts tuples to lists before dumping.
"""
from __future__ import annotations

import json
from pathlib import Path
from typing import Any


def _to_jsonable(value: Any) -> Any:
    if isinstance(value, tuple):
        return [_to_jsonable(v) for v in value]
    if isinstance(value, dict):
        return {k: _to_jsonable(v) for k, v in value.items()}
    if isinstance(value, list):
        return [_to_jsonable(v) for v in value]
    return value


def write_json(path: Path, data: Any) -> None:
    path.write_text(json.dumps(_to_jsonable(data), indent=2, sort_keys=True), encoding="utf-8")
