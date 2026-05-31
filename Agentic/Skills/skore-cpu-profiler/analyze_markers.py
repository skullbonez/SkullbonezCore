#!/usr/bin/env python3
"""Summarize profiler CSV markers under a parent marker path."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path


def percentile(values: list[float], pct: float) -> float:
    if not values:
        return 0.0
    values = sorted(values)
    pos = (len(values) - 1) * pct / 100.0
    low = int(pos)
    high = min(low + 1, len(values) - 1)
    return values[low] + (values[high] - values[low]) * (pos - low)


def read_rows(path: Path) -> tuple[list[str], list[dict[str, str]]]:
    with path.open(newline="") as handle:
        rows: list[dict[str, str]] = []
        fieldnames: list[str] | None = None
        for raw in handle:
            if raw.startswith("#") or not raw.strip():
                continue
            if raw.startswith("pass,frame,"):
                fieldnames = next(csv.reader([raw]))
                continue
            if fieldnames:
                values = next(csv.reader([raw]))
                rows.append(dict(zip(fieldnames, values)))
    return fieldnames or [], rows


def marker_stats(fieldnames: list[str], rows: list[dict[str, str]], area: str) -> dict[str, dict[str, float]]:
    second_pass = [row for row in rows if row.get("pass") == "2"] or rows
    stats: dict[str, dict[str, float]] = {}
    for name in fieldnames[2:]:
        if name != area and not name.startswith(area + "/"):
            continue
        values = []
        for row in second_pass:
            try:
                values.append(float(row[name]))
            except (KeyError, ValueError):
                pass
        if values:
            stats[name] = {
                "avg": round(sum(values) / len(values), 4),
                "p50": round(percentile(values, 50), 4),
                "p99": round(percentile(values, 99), 4),
                "count": len(values),
            }
    return stats


def print_table(area: str, stats: dict[str, dict[str, float]]) -> None:
    parent_avg = stats.get(area, {}).get("avg", 0.0)
    children = {
        name: stat
        for name, stat in stats.items()
        if name.startswith(area + "/") and name.count("/") == area.count("/") + 1
    }
    ranked = sorted(children.items(), key=lambda item: item[1]["avg"], reverse=True)

    print(f"Parent: {area} avg={parent_avg:.4f}ms")
    print()
    print(f"  {'Sub-marker':<56} {'avg ms':>8} {'p50 ms':>8} {'p99 ms':>8} {'% parent':>9}")
    print(f"  {'-' * 56} {'-' * 8} {'-' * 8} {'-' * 8} {'-' * 9}")
    for name, stat in ranked:
        share = stat["avg"] / parent_avg * 100.0 if parent_avg else 0.0
        label = name if len(name) <= 56 else "..." + name[-53:]
        print(f"  {label:<56} {stat['avg']:>8.4f} {stat['p50']:>8.4f} {stat['p99']:>8.4f} {share:>8.1f}%")

    if ranked:
        name, stat = ranked[0]
        share = stat["avg"] / parent_avg * 100.0 if parent_avg else 0.0
        print()
        print(f"Hotspot: {name} avg={stat['avg']:.4f}ms p99={stat['p99']:.4f}ms share={share:.1f}%")


def update_session(path: Path, slot: str, csv_path: Path, area: str, stats: dict[str, dict[str, float]]) -> None:
    if path.exists():
        data = json.loads(path.read_text())
    else:
        data = {}
    data["area_path"] = area
    data[f"{slot}_csv"] = str(csv_path)
    data[f"{slot}_stats"] = stats
    path.write_text(json.dumps(data, indent=2) + "\n")
    print(f"updated {path}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv", type=Path, required=True)
    parser.add_argument("--area", required=True, help="parent marker path, e.g. Frame/Render/Balls")
    parser.add_argument("--session", type=Path, help="optional session_markers.json to update")
    parser.add_argument("--slot", choices=["before", "after"], default="before")
    args = parser.parse_args()

    fieldnames, rows = read_rows(args.csv)
    stats = marker_stats(fieldnames, rows, args.area)
    if not stats:
        print(f"no markers found under {args.area}")
        return 1

    print_table(args.area, stats)
    if args.session:
        update_session(args.session, args.slot, args.csv, args.area, stats)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
