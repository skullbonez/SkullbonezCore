#!/usr/bin/env python3
"""
File: cpp_source_scan.py
Purpose:
  Share the tracked-source enumeration and comment/literal masking used by the
  governance inventories.

Summary:
  Two shape inventories need the same two primitives: the list of tracked
  SkullbonezSource translation units, and a masked copy of a file where
  comments, string/char literals, raw strings, and preprocessor lines are blanked
  while byte offsets and line breaks survive. Masking is imported from
  `inventory_wide_signatures` rather than copied, so the subtle scanner has one
  implementation.

Glossary:
  Masked text: Source with non-code regions replaced by spaces at identical
    offsets, so a line/column computed on the mask is valid for the original.
  Tracked source: Files `git ls-files` reports under a configured root, filtered
    to C++ suffixes. Untracked scratch files are deliberately invisible.

Invariants:
  - Masking has exactly one implementation in the repository; this module
    re-exports it and must never fork a second copy.
  - Enumeration is git-driven, so an ignored-but-present directory cannot
    silently widen or narrow a scan.
  - This module is read-only and performs no repository mutation.

Related:
  - tools/inventory_authority_free_aggregates.py
  - tools/inventory_extraction_scars.py
  - tools/inventory_wide_signatures.py
  - Agentic/Reports/2026-07-27/governance-shape-to-judgment-conversion-closure.md
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

# Why: mask_cpp is a delicate scanner that already survives the wide-signature
# gate. Importing it keeps one implementation instead of a second copy that
# could drift on raw strings or line-continued preprocessor directives.
from inventory_wide_signatures import mask_cpp  # noqa: E402

__all__ = ["mask_cpp", "SOURCE_SUFFIXES", "tracked_files", "line_of_offset"]

SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".inl"}


def tracked_files(repo: Path, roots: list[str]) -> list[Path]:
    """Return tracked C++ files under the given repository-relative roots."""
    result = subprocess.run(
        ["git", "ls-files", "-z", "--", *roots],
        cwd=repo,
        check=True,
        capture_output=True,
    )
    names = result.stdout.decode("utf-8", errors="strict").split("\0")
    return [repo / name for name in names if name and Path(name).suffix.lower() in SOURCE_SUFFIXES]


def line_of_offset(text: str, offset: int) -> int:
    """1-indexed line number for a byte offset into text."""
    return text.count("\n", 0, offset) + 1
