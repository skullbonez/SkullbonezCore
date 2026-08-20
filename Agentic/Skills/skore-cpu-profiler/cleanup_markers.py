#!/usr/bin/env python3
"""
File: Agentic/Skills/skore-cpu-profiler/cleanup_markers.py
Purpose:
  Removes profiler instrumentation blocks named by a session manifest.

Summary:
  Cleanup is manifest-driven rather than repository-wide: each recorded file
  and escaped marker id selects its sentinel-delimited source ranges. Missing
  files or markers fail the run, and the session is deleted only after complete
  success.

Glossary:
  Sentinel block: Source range delimited by matching SKORE-PROFILER-BEGIN and
    SKORE-PROFILER-END comments carrying the same marker id.

Invariants:
  - The mutation set is bounded to file/id pairs in `added_markers`.
  - Marker ids are regex-escaped before matching source text.
  - `--delete-session` cannot discard recovery metadata after a partial cleanup.

Related:
  - Agentic/Skills/skore-cpu-profiler/skill.md
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


def remove_marker(content: str, marker_id: str) -> tuple[str, bool]:
    # Hazard: DOTALL intentionally consumes everything between one matching
    # sentinel pair. Escaping the manifest id prevents it from widening that
    # source range through regex metacharacters.
    pattern = re.compile(
        rf"[ \t]*// \[SKORE-PROFILER-BEGIN:{re.escape(marker_id)}\][^\r\n]*(?:\r?\n)"
        rf".*?"
        rf"[ \t]*// \[SKORE-PROFILER-END:{re.escape(marker_id)}\][^\r\n]*(?:\r?\n)?",
        re.DOTALL,
    )
    updated, count = pattern.subn("", content)
    return updated, count > 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Remove profiler sentinel blocks recorded in a session manifest.")
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument(
        "--session",
        type=Path,
        default=Path(__file__).resolve().parent / "session_markers.json",
    )
    parser.add_argument("--delete-session", action="store_true")
    args = parser.parse_args()

    if not args.session.exists():
        print(f"session not found: {args.session}")
        return 1

    session = json.loads(args.session.read_text())
    markers = session.get("added_markers", [])
    by_file: dict[str, list[str]] = {}
    for marker in markers:
        by_file.setdefault(marker["file"], []).append(marker["id"])

    status = 0
    for rel_file, ids in by_file.items():
        path = args.repo.joinpath(*rel_file.replace("\\", "/").split("/"))
        if not path.exists():
            print(f"missing {rel_file}")
            status = 1
            continue
        content = path.read_text()
        changed = False
        for marker_id in ids:
            content, removed = remove_marker(content, marker_id)
            if removed:
                print(f"removed {marker_id} from {rel_file}")
                changed = True
            else:
                print(f"not found {marker_id} in {rel_file}")
                status = 1
        if changed:
            path.write_text(content)

    if args.delete_session and status == 0:
        args.session.unlink()
        print(f"deleted {args.session}")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
