#!/usr/bin/env python3
#
# File: plan_subsystem_hunt.py
# Purpose:
#   Build deterministic subsystem waves for a read-only parallel bug hunt.
#
# Summary:
#   Treats the canonical master bug report as the live subsystem taxonomy,
#   validates its identity columns, removes coordinator-declared leases, and
#   chunks every remaining subsystem into waves sized to the actual worker
#   capacity. The output is scheduling evidence; it never edits the report.
#
# Glossary:
#   Hunt wave: Set of distinct subsystem lanes that may occupy worker slots at
#     the same time against one frozen commit.
#   Lease: Exact canonical subsystem value unavailable to this campaign because
#     an implementation lane already owns it.
#
# Invariants:
#   - Every non-leased subsystem appears exactly once in the manifest.
#   - Worker capacity limits a wave, never the total campaign coverage.
#   - The CSV remains the sole taxonomy owner; no subsystem list is copied here.
#
# Related:
#   - Agentic/Bugs/master_bug_report.csv
#   - Agentic/Skills/parallel-bug-finder/SKILL.md
#   - Agentic/Skills/parallel-bug-finder/references/finding-contract.md

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import subprocess
import sys
import tempfile
from collections import Counter, defaultdict
from pathlib import Path
from typing import Iterable


REQUIRED_COLUMNS = (
    "finding_id",
    "subsystem",
    "severity",
    "title",
    "locations",
    "trigger",
    "impact",
    "confidence",
    "fixed",
)
VALID_SEVERITIES = {"High", "Medium", "Low"}
VALID_CONFIDENCE = {"High", "Medium-High", "Medium"}
VALID_FIXED_VALUES = {"Yes", "No"}
COMMIT_PATTERN = re.compile(r"[0-9a-f]{40}")
FINDING_ID_PATTERN = re.compile(r"[A-Z][A-Z0-9_]*-[0-9]{3,}")


class ContractError(ValueError):
    """Report a deterministic input-contract failure."""


def read_report(report_path: Path) -> list[dict[str, str]]:
    with report_path.open("r", encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        if tuple(reader.fieldnames or ()) != REQUIRED_COLUMNS:
            raise ContractError(
                f"report columns must be exactly {REQUIRED_COLUMNS}; got {reader.fieldnames}"
            )
        rows = list(reader)

    if not rows:
        raise ContractError("canonical report contains no rows and therefore no taxonomy")

    seen_ids: set[str] = set()
    for line_number, row in enumerate(rows, start=2):
        if None in row:
            raise ContractError(f"line {line_number}: row has undeclared extra CSV cells")
        for column in REQUIRED_COLUMNS:
            value = row[column]
            if value is None or not value.strip():
                raise ContractError(f"line {line_number}: {column} is empty")
            if value != value.strip():
                raise ContractError(
                    f"line {line_number}: {column} has leading or trailing whitespace"
                )
        finding_id = row["finding_id"].strip()
        subsystem = row["subsystem"].strip()
        if not FINDING_ID_PATTERN.fullmatch(finding_id):
            raise ContractError(f"line {line_number}: invalid finding_id {finding_id!r}")
        if finding_id in seen_ids:
            raise ContractError(f"line {line_number}: duplicate finding_id {finding_id}")
        if not subsystem:
            raise ContractError(f"line {line_number}: subsystem is empty")
        if row["severity"] not in VALID_SEVERITIES:
            raise ContractError(
                f"line {line_number}: unsupported severity {row['severity']!r}"
            )
        if row["confidence"] not in VALID_CONFIDENCE:
            raise ContractError(
                f"line {line_number}: unsupported confidence {row['confidence']!r}"
            )
        if row["fixed"] not in VALID_FIXED_VALUES:
            raise ContractError(f"line {line_number}: fixed must be Yes or No")
        seen_ids.add(finding_id)
    return rows


def resolve_base_commit(repo: Path, supplied: str | None) -> str:
    if supplied is not None:
        base_commit = supplied.strip().lower()
    else:
        result = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=repo,
            check=True,
            capture_output=True,
            text=True,
        )
        base_commit = result.stdout.strip().lower()
    if not COMMIT_PATTERN.fullmatch(base_commit):
        raise ContractError("base commit must be a full 40-character lowercase SHA-1")
    subprocess.run(
        ["git", "cat-file", "-e", f"{base_commit}^{{commit}}"],
        cwd=repo,
        check=True,
        capture_output=True,
        text=True,
    )
    return base_commit


def report_digest_at_base(repo: Path, report_path: Path, base_commit: str) -> str:
    try:
        report_label = report_path.resolve().relative_to(repo.resolve()).as_posix()
    except ValueError as error:
        raise ContractError("canonical report must be inside the repository") from error
    base_blob_result = subprocess.run(
        ["git", "rev-parse", f"{base_commit}:{report_label}"],
        cwd=repo,
        check=True,
        capture_output=True,
        text=True,
    )
    base_blob = base_blob_result.stdout.strip()
    working_blob_result = subprocess.run(
        ["git", "hash-object", f"--path={report_label}", str(report_path)],
        cwd=repo,
        check=True,
        capture_output=True,
        text=True,
    )
    if working_blob_result.stdout.strip() != base_blob:
        raise ContractError("canonical report working copy differs from frozen base")
    blob_result = subprocess.run(
        ["git", "cat-file", "blob", base_blob],
        cwd=repo,
        check=True,
        capture_output=True,
    )
    base_bytes = blob_result.stdout
    return hashlib.sha256(base_bytes).hexdigest()


def subsystem_priority(rows: Iterable[dict[str, str]]) -> list[str]:
    unresolved: dict[str, Counter[str]] = defaultdict(Counter)
    all_subsystems: set[str] = set()
    for row in rows:
        subsystem = row["subsystem"]
        all_subsystems.add(subsystem)
        if row["fixed"] == "No":
            unresolved[subsystem][row["severity"]] += 1

    # Why: high-risk owners go first, while the name tie-break keeps manifests
    # byte-stable when report row order changes.
    return sorted(
        all_subsystems,
        key=lambda name: (
            -unresolved[name]["High"],
            -unresolved[name]["Medium"],
            -unresolved[name]["Low"],
            name.casefold(),
        ),
    )


def build_manifest(
    rows: list[dict[str, str]],
    base_commit: str,
    worker_slots: int,
    leased_subsystems: Iterable[str],
    report_label: str,
    report_sha256: str,
) -> dict[str, object]:
    if worker_slots < 1:
        raise ContractError("worker-slots must be at least 1")

    ordered_subsystems = subsystem_priority(rows)
    taxonomy = set(ordered_subsystems)
    leases = sorted(set(leased_subsystems), key=str.casefold)
    unknown_leases = sorted(set(leases) - taxonomy, key=str.casefold)
    if unknown_leases:
        raise ContractError(
            "leased subsystem values are not in the canonical taxonomy: "
            + ", ".join(unknown_leases)
        )

    rows_by_subsystem: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        rows_by_subsystem[row["subsystem"]].append(row)

    available = [name for name in ordered_subsystems if name not in leases]
    waves: list[dict[str, object]] = []
    for offset in range(0, len(available), worker_slots):
        lanes = []
        for subsystem in available[offset : offset + worker_slots]:
            subsystem_rows = rows_by_subsystem[subsystem]
            unresolved = [row for row in subsystem_rows if row["fixed"] == "No"]
            severity_counts = Counter(row["severity"] for row in unresolved)
            lanes.append(
                {
                    "subsystem": subsystem,
                    "known_finding_ids": sorted(
                        row["finding_id"] for row in subsystem_rows
                    ),
                    "unresolved_count": len(unresolved),
                    "unresolved_by_severity": {
                        severity: severity_counts[severity]
                        for severity in ("High", "Medium", "Low")
                    },
                }
            )
        waves.append({"wave": len(waves) + 1, "lanes": lanes})

    return {
        "schema_version": 1,
        "base_commit": base_commit,
        "report": report_label,
        "report_sha256": report_sha256,
        "worker_slots": worker_slots,
        "taxonomy": sorted(taxonomy, key=str.casefold),
        "leased_subsystems": leases,
        "scheduled_subsystem_count": len(available),
        "waves": waves,
    }


def run_self_test() -> None:
    rows = [
        {
            "finding_id": "CORE-001",
            "subsystem": "Core",
            "severity": "High",
            "title": "Core defect",
            "locations": "Core.cpp:1",
            "trigger": "Call it.",
            "impact": "Failure.",
            "confidence": "High",
            "fixed": "No",
        },
        {
            "finding_id": "UI-001",
            "subsystem": "UI Library",
            "severity": "Medium",
            "title": "UI defect",
            "locations": "UI.cpp:1",
            "trigger": "Open it.",
            "impact": "Wrong output.",
            "confidence": "High",
            "fixed": "Yes",
        },
        {
            "finding_id": "PHYS-001",
            "subsystem": "Physics",
            "severity": "Low",
            "title": "Physics defect",
            "locations": "Physics.cpp:1",
            "trigger": "Step it.",
            "impact": "Drift.",
            "confidence": "Medium",
            "fixed": "No",
        },
    ]

    with tempfile.TemporaryDirectory() as temporary_directory:
        report_path = Path(temporary_directory) / "report.csv"
        with report_path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=REQUIRED_COLUMNS)
            writer.writeheader()
            writer.writerows(rows)
        loaded = read_report(report_path)
        malformed_path = Path(temporary_directory) / "malformed.csv"
        with malformed_path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(REQUIRED_COLUMNS)
            writer.writerow([*rows[0].values(), "undeclared"])
        try:
            read_report(malformed_path)
        except ContractError:
            pass
        else:
            raise AssertionError("surplus CSV cells must fail closed")

    manifest = build_manifest(
        loaded,
        "a" * 40,
        worker_slots=1,
        leased_subsystems=["Physics"],
        report_label="report.csv",
        report_sha256="c" * 64,
    )
    assert manifest["scheduled_subsystem_count"] == 2
    assert manifest["leased_subsystems"] == ["Physics"]
    waves = manifest["waves"]
    assert isinstance(waves, list) and len(waves) == 2
    assert waves[0]["lanes"][0]["subsystem"] == "Core"

    try:
        build_manifest(
            loaded,
            "a" * 40,
            1,
            ["Unknown"],
            "report.csv",
            "c" * 64,
        )
    except ContractError:
        pass
    else:
        raise AssertionError("unknown leases must fail closed")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path("."))
    parser.add_argument(
        "--report",
        type=Path,
        default=Path("Agentic/Bugs/master_bug_report.csv"),
    )
    parser.add_argument("--worker-slots", type=int, default=1)
    parser.add_argument("--base-commit")
    parser.add_argument("--leased-subsystem", action="append", default=[])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        if args.self_test:
            run_self_test()
            print("parallel bug-finder wave planner self-test: PASS")
            return 0

        repo = args.repo.resolve()
        report_path = args.report
        if not report_path.is_absolute():
            report_path = repo / report_path
        rows = read_report(report_path)
        base_commit = resolve_base_commit(repo, args.base_commit)
        try:
            report_label = report_path.relative_to(repo).as_posix()
        except ValueError:
            report_label = str(report_path)
        report_sha256 = report_digest_at_base(repo, report_path, base_commit)
        manifest = build_manifest(
            rows,
            base_commit,
            args.worker_slots,
            args.leased_subsystem,
            report_label,
            report_sha256,
        )
        rendered = json.dumps(manifest, indent=2, sort_keys=True) + "\n"
        if args.output is not None:
            output_path = args.output.resolve()
            if output_path == report_path.resolve():
                raise ContractError("manifest output must not overwrite the canonical report")
            if not output_path.parent.is_dir():
                raise ContractError("manifest output parent directory does not exist")
            output_path.write_text(rendered, encoding="utf-8")
        else:
            print(rendered, end="")
        return 0
    except (ContractError, OSError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
