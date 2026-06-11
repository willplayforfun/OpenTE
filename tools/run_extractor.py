"""PyInstaller entry point for the standalone extractor executable.

`extractor/main.py` uses package-relative imports (it's normally run as
`python -m extractor.main`), which PyInstaller's script-analysis doesn't
follow as cleanly as a flat top-level script. This thin wrapper gives
PyInstaller a single entry point while keeping `extractor/` a normal,
testable Python package.
"""
import sys

from extractor.main import main

if __name__ == "__main__":
    sys.exit(main())
