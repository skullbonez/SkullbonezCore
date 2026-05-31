#!/usr/bin/env python3
"""Remove SKORE profiler sentinel blocks recorded in session_markers.json."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


def remove_marker(content: str, marker_id: str) -> tuple[str, bool]:
    pattern = re.compile(
        rf"[ \t]*// \[SKORE-PROFILER-BEGIN:{re.escape(marker_id)}\][^\r\n]*(?:\r?\n)"
        rf".*?"
        rf"[ \t]*// \[SKORE-PROFILER-END:{re.escape(marker_id)}\][^\r\n]*(?:\r?\n)?",
        re.DOTALL,
    )
    updated, count = pattern.subn("", content)
    return updated, count > 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
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
