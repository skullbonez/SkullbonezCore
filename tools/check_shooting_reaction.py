#
# File: tools/check_shooting_reaction.py
# Purpose:
#   Documents and runs the check_shooting_reaction.py developer/validation helper script.
#
# Mental model:
#   Tools are command-line guardrails around builds, validation, screenshots,
#   diagnostics, and artifact handling. They make the safe path repeatable and
#   keep output bounded for humans and agents.
#
# Glossary:
#   CSV (Comma-Separated Values): Text table format used for byte-exact physics
#   regression output.
#   Validation gate: Repository script that proves a class of changes before
#   commit or PR.
#
# Invariants:
#   - Tool output should be bounded and readable because agents and humans use
#   it for decisions.
#
# Related:
#   - AGENTS.md
#   - Agentic/Reference/comment-style-guide.md
#
#
"""
Semantic regression check for shooting_reaction_volley.scene.

The byte-exact physics CSV catches deterministic drift. This checker catches the
behavioral contract for the runtime projectile work: every named target in the
volley must react to being shot, not merely register a swept hit.
"""

import csv
import math
import os
import sys


EXPECTED_TARGETS = [
    "target_ball_00",
    "target_ball_01",
    "target_ball_02",
    "target_ball_03",
    "target_ball_04",
    "target_box_05",
    "target_box_06",
    "target_box_07",
    "target_box_08",
    "target_box_09",
]

MIN_DISPLACEMENT = 0.05
MIN_MAX_SPEED = 0.10


def read_rows(path):
    with open(path, newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def vec(row, prefix):
    return (
        float(row[f"{prefix}X"]),
        float(row[f"{prefix}Y"]),
        float(row[f"{prefix}Z"]),
    )


def distance(a, b):
    dx = a[0] - b[0]
    dy = a[1] - b[1]
    dz = a[2] - b[2]
    return math.sqrt(dx * dx + dy * dy + dz * dz)


def main():
    repo = os.environ.get("SKORE_REPO", os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(repo, "Debug", "shooting_reaction_volley.csv")
    if not os.path.exists(path):
        print(f"  FAIL: shooting reaction CSV not found: {path}")
        return 1

    rows = read_rows(path)
    if not rows:
        print(f"  FAIL: shooting reaction CSV is empty: {path}")
        return 1

    failed = False
    for target in EXPECTED_TARGETS:
        target_rows = [row for row in rows if row["name"] == target]
        if not target_rows:
            print(f"  FAIL: {target} missing from shooting reaction CSV")
            failed = True
            continue

        first = target_rows[0]
        last = target_rows[-1]
        displacement = distance(vec(last, "pos"), vec(first, "pos"))
        max_speed = max(float(row["speed"]) for row in target_rows)
        if displacement < MIN_DISPLACEMENT or max_speed < MIN_MAX_SPEED:
            print(f"  FAIL: {target} did not react enough (displacement={displacement:.4f}, maxSpeed={max_speed:.4f})")
            failed = True
        else:
            print(f"  PASS: {target} reacted (displacement={displacement:.4f}, maxSpeed={max_speed:.4f})")

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
