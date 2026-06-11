"""Locating and validating the user's Trade Empires installation directory."""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

# Files that must exist for a directory to be considered a valid Trade
# Empires installation. Paths are relative to the directory being checked.
_REQUIRED_FILES = (
    "Trade Empires.exe",
    Path("Data") / "data.{}",
    Path("Data") / "bldg.{}",
)


@dataclass
class GameDirectory:
    """A validated Trade Empires installation directory."""

    path: Path

    @property
    def data_dir(self) -> Path:
        return self.path / "Data"

    @property
    def maps_dir(self) -> Path:
        return self.path / "Maps"


def _looks_valid(path: Path) -> bool:
    return all((path / required).is_file() for required in _REQUIRED_FILES)


def find_game_directory(candidate: Path) -> GameDirectory | None:
    """Validates `candidate` as a Trade Empires installation directory.

    Accepts either the directory directly containing `Trade Empires.exe`
    and `Data/`, or its parent (so users can point at either the folder they
    installed the game into, or a folder that contains a `Trade Empires/`
    subfolder, e.g. an extracted distribution archive). Returns None if
    neither `candidate` nor `candidate / "Trade Empires"` is valid.
    """
    if _looks_valid(candidate):
        return GameDirectory(candidate)

    nested = candidate / "Trade Empires"
    if _looks_valid(nested):
        return GameDirectory(nested)

    return None
