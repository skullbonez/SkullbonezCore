#!/usr/bin/env python3
#
# File: consolidate_findings.py
# Purpose:
#   Validate subsystem bug-hunt packets and propose canonical CSV rows.
#
# Summary:
#   Fails closed on stale bases, unknown owners, incomplete evidence, duplicate
#   lane packets, and conflicting root-cause keys. It derives stable next IDs
#   from the live report and separates exact or suspected duplicates for human
#   review. The canonical report is never written by this script.
#
# Glossary:
#   Finding packet: Structured, read-only evidence returned by one subsystem
#     worker.
#   Suspected duplicate: Candidate with enough title and source overlap to need
#     coordinator adjudication before it may receive a new public ID.
#
# Invariants:
#   - Workers cannot assign public IDs or fixed dispositions.
#   - Every accepted candidate has one primary canonical subsystem.
#   - Output is a proposal; only the coordinator may patch the canonical CSV.
#
# Related:
#   - Agentic/Bugs/master_bug_report.csv
#   - Agentic/Skills/parallel-bug-finder/SKILL.md
#   - Agentic/Skills/parallel-bug-finder/references/finding-contract.md

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path
from typing import Any, Iterable

from plan_subsystem_hunt import (
    COMMIT_PATTERN,
    ContractError,
    REQUIRED_COLUMNS,
    read_report,
    report_digest_at_base,
    resolve_base_commit,
)


PACKET_KEYS = {
    "schema_version",
    "base_commit",
    "agent_id",
    "worktree",
    "subsystem",
    "secondary_subsystems",
    "coverage",
    "findings",
}
COVERAGE_KEYS = {
    "files_reviewed",
    "ownership_evidence",
    "entry_points",
    "tests_reviewed",
    "commands",
}
FINDING_KEYS = {
    "root_cause_key",
    "severity",
    "title",
    "locations",
    "trigger",
    "impact",
    "confidence",
    "evidence",
    "reproduction",
}
VALID_SEVERITIES = {"High", "Medium", "Low"}
VALID_CONFIDENCE = {"High", "Medium-High", "Medium"}
ROOT_CAUSE_PATTERN = re.compile(r"[a-z0-9]+(?:-[a-z0-9]+)*")
PUBLIC_ID_PATTERN = re.compile(r"(?P<prefix>[A-Z][A-Z0-9_]*)-(?P<number>[0-9]{3,})")
WORD_PATTERN = re.compile(r"[a-z0-9]+")
SOURCE_PATH_PATTERN = re.compile(
    r"(?P<path>[A-Za-z0-9_.\-/]+\.(?:cpp|c|h|hpp|inl|hlsl|py|ps1|bat|json|csv))"
)
LOCATION_TOKEN_PATTERN = re.compile(
    r"(?P<path>[^:;\r\n]+):(?P<start>[0-9]+)(?:-(?P<end>[0-9]+))?"
)
STOP_WORDS = {
    "a",
    "an",
    "and",
    "can",
    "does",
    "for",
    "from",
    "in",
    "is",
    "its",
    "of",
    "on",
    "or",
    "the",
    "to",
    "when",
    "with",
}
MANIFEST_KEYS = {
    "schema_version",
    "base_commit",
    "report",
    "report_sha256",
    "worker_slots",
    "taxonomy",
    "leased_subsystems",
    "scheduled_subsystem_count",
    "waves",
}


def require_exact_keys(value: dict[str, Any], expected: set[str], label: str) -> None:
    actual = set(value)
    if actual != expected:
        missing = sorted(expected - actual)
        extra = sorted(actual - expected)
        raise ContractError(f"{label} keys differ; missing={missing}, extra={extra}")


def require_text(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ContractError(f"{label} must be a non-empty string")
    return value.strip()


def require_text_list(
    value: Any,
    label: str,
    *,
    allow_empty: bool,
) -> list[str]:
    if not isinstance(value, list):
        raise ContractError(f"{label} must be a list")
    cleaned = [require_text(item, f"{label} item") for item in value]
    if not allow_empty and not cleaned:
        raise ContractError(f"{label} must not be empty")
    if len(cleaned) != len(set(cleaned)):
        raise ContractError(f"{label} contains duplicate values")
    return cleaned


def validate_relative_path(value: str, label: str) -> None:
    normalized = value.replace("\\", "/")
    path = Path(normalized)
    if path.is_absolute() or ".." in path.parts:
        raise ContractError(f"{label} must be a repository-relative path")
    if "\\" in value:
        raise ContractError(f"{label} must use forward slashes")


def parse_locations(value: str, label: str) -> list[tuple[str, int, int]]:
    """Parse the complete semicolon-delimited location grammar."""
    parsed: list[tuple[str, int, int]] = []
    for index, raw_token in enumerate(value.split(";")):
        token = raw_token.strip()
        if not token:
            raise ContractError(f"{label}: location token {index} is empty")
        match = LOCATION_TOKEN_PATTERN.fullmatch(token)
        if match is None:
            raise ContractError(
                f"{label}: location token {index} must be path:start[-end]"
            )
        file_path = match.group("path")
        if file_path != file_path.strip():
            raise ContractError(f"{label}: location path has surrounding whitespace")
        validate_relative_path(file_path, f"{label} location[{index}]")
        start = int(match.group("start"))
        end = int(match.group("end") or start)
        if start < 1 or end < start:
            raise ContractError(f"{label}: invalid location range {token!r}")
        parsed.append((file_path, start, end))
    return parsed


def validate_packet(
    raw_packet: Any,
    packet_path: Path,
    base_commit: str,
    taxonomy: set[str],
) -> dict[str, Any]:
    if not isinstance(raw_packet, dict):
        raise ContractError(f"{packet_path}: packet must be a JSON object")
    require_exact_keys(raw_packet, PACKET_KEYS, str(packet_path))

    if raw_packet["schema_version"] != 1:
        raise ContractError(f"{packet_path}: schema_version must be 1")
    packet_base = require_text(raw_packet["base_commit"], "base_commit").lower()
    if not COMMIT_PATTERN.fullmatch(packet_base) or packet_base != base_commit:
        raise ContractError(f"{packet_path}: packet base does not match frozen base")

    agent_id = require_text(raw_packet["agent_id"], "agent_id")
    worktree = require_text(raw_packet["worktree"], "worktree")
    if not Path(worktree).is_absolute():
        raise ContractError(f"{packet_path}: worktree must be absolute")

    subsystem = require_text(raw_packet["subsystem"], "subsystem")
    if subsystem not in taxonomy:
        raise ContractError(f"{packet_path}: unknown subsystem {subsystem!r}")
    secondary = require_text_list(
        raw_packet["secondary_subsystems"],
        "secondary_subsystems",
        allow_empty=True,
    )
    if subsystem in secondary:
        raise ContractError(f"{packet_path}: primary subsystem repeated as secondary")
    unknown_secondary = sorted(set(secondary) - taxonomy, key=str.casefold)
    if unknown_secondary:
        raise ContractError(
            f"{packet_path}: unknown secondary subsystems {unknown_secondary}"
        )

    coverage = raw_packet["coverage"]
    if not isinstance(coverage, dict):
        raise ContractError(f"{packet_path}: coverage must be an object")
    require_exact_keys(coverage, COVERAGE_KEYS, f"{packet_path} coverage")
    files_reviewed = require_text_list(
        coverage["files_reviewed"],
        "coverage.files_reviewed",
        allow_empty=False,
    )
    for index, file_path in enumerate(files_reviewed):
        validate_relative_path(file_path, f"coverage.files_reviewed[{index}]")
    ownership_raw = coverage["ownership_evidence"]
    if not isinstance(ownership_raw, dict):
        raise ContractError(f"{packet_path}: ownership_evidence must be an object")
    if set(ownership_raw) != set(files_reviewed):
        raise ContractError(
            f"{packet_path}: ownership_evidence keys must exactly match files_reviewed"
        )
    ownership_evidence = {
        file_path: require_text(
            ownership_raw[file_path],
            f"ownership_evidence[{file_path!r}]",
        )
        for file_path in files_reviewed
    }
    entry_points = require_text_list(
        coverage["entry_points"],
        "coverage.entry_points",
        allow_empty=False,
    )
    tests_reviewed = require_text_list(
        coverage["tests_reviewed"],
        "coverage.tests_reviewed",
        allow_empty=True,
    )
    commands = require_text_list(
        coverage["commands"],
        "coverage.commands",
        allow_empty=False,
    )

    findings_raw = raw_packet["findings"]
    if not isinstance(findings_raw, list):
        raise ContractError(f"{packet_path}: findings must be a list")
    findings: list[dict[str, Any]] = []
    for index, raw_finding in enumerate(findings_raw):
        label = f"{packet_path} finding[{index}]"
        if not isinstance(raw_finding, dict):
            raise ContractError(f"{label} must be an object")
        require_exact_keys(raw_finding, FINDING_KEYS, label)
        root_cause_key = require_text(raw_finding["root_cause_key"], "root_cause_key")
        if not ROOT_CAUSE_PATTERN.fullmatch(root_cause_key):
            raise ContractError(f"{label}: root_cause_key must be lowercase kebab-case")
        severity = require_text(raw_finding["severity"], "severity")
        confidence = require_text(raw_finding["confidence"], "confidence")
        if severity not in VALID_SEVERITIES:
            raise ContractError(f"{label}: unsupported severity {severity!r}")
        if confidence not in VALID_CONFIDENCE:
            raise ContractError(f"{label}: unsupported confidence {confidence!r}")
        locations = require_text(raw_finding["locations"], "locations")
        parsed_locations = parse_locations(locations, label)
        location_paths = {file_path for file_path, _, _ in parsed_locations}
        if not location_paths <= set(files_reviewed):
            raise ContractError(
                f"{label}: every location file must appear in coverage.files_reviewed"
            )
        evidence = require_text_list(
            raw_finding["evidence"],
            "evidence",
            allow_empty=False,
        )
        findings.append(
            {
                "root_cause_key": root_cause_key,
                "severity": severity,
                "title": require_text(raw_finding["title"], "title"),
                "locations": locations,
                "trigger": require_text(raw_finding["trigger"], "trigger"),
                "impact": require_text(raw_finding["impact"], "impact"),
                "confidence": confidence,
                "evidence": evidence,
                "reproduction": require_text(
                    raw_finding["reproduction"], "reproduction"
                ),
            }
        )

    return {
        "packet_path": str(packet_path),
        "base_commit": packet_base,
        "agent_id": agent_id,
        "worktree": worktree,
        "subsystem": subsystem,
        "secondary_subsystems": secondary,
        "coverage": {
            "files_reviewed": files_reviewed,
            "ownership_evidence": ownership_evidence,
            "entry_points": entry_points,
            "tests_reviewed": tests_reviewed,
            "commands": commands,
        },
        "findings": findings,
    }


def normalized_absolute_path(value: str | Path) -> str:
    return os.path.normcase(os.path.abspath(os.fspath(value)))


def parse_worktree_porcelain(text: str) -> dict[str, dict[str, str]]:
    worktrees: dict[str, dict[str, str]] = {}
    current: dict[str, str] = {}
    for line in [*text.splitlines(), ""]:
        if line:
            key, _, value = line.partition(" ")
            current[key] = value
            continue
        if not current:
            continue
        path = current.get("worktree")
        head = current.get("HEAD")
        if not path or not head:
            raise ContractError("git worktree list returned an incomplete entry")
        normalized = normalized_absolute_path(path)
        if normalized in worktrees:
            raise ContractError(f"git listed duplicate worktree path {path!r}")
        worktrees[normalized] = dict(current)
        current = {}
    return worktrees


def path_is_within(child: Path, parent: Path) -> bool:
    child_value = normalized_absolute_path(child)
    parent_value = normalized_absolute_path(parent)
    try:
        return os.path.commonpath((child_value, parent_value)) == parent_value
    except ValueError:
        return False


def verify_packet_worktrees(
    repo: Path,
    packets: list[dict[str, Any]],
    base_commit: str,
) -> list[dict[str, str]]:
    agent_ids = [packet["agent_id"] for packet in packets]
    if len(agent_ids) != len(set(agent_ids)):
        raise ContractError("a consolidation wave contains duplicate agent_id values")

    worker_paths = [normalized_absolute_path(packet["worktree"]) for packet in packets]
    if len(worker_paths) != len(set(worker_paths)):
        raise ContractError("a consolidation wave reuses a worker worktree")
    coordinator_path = normalized_absolute_path(repo)
    if coordinator_path in worker_paths:
        raise ContractError("a worker packet points at the coordinator worktree")

    result = subprocess.run(
        ["git", "worktree", "list", "--porcelain"],
        cwd=repo,
        check=True,
        capture_output=True,
        text=True,
    )
    registered = parse_worktree_porcelain(result.stdout)
    evidence: list[dict[str, str]] = []
    for packet, worker_path in zip(packets, worker_paths):
        if worker_path not in registered:
            raise ContractError(
                f"{packet['packet_path']}: unregistered worktree {packet['worktree']!r}"
            )
        entry = registered[worker_path]
        if entry["HEAD"].lower() != base_commit:
            raise ContractError(
                f"{packet['packet_path']}: worktree HEAD differs from frozen base"
            )
        if "branch" not in entry:
            raise ContractError(f"{packet['packet_path']}: worker worktree is detached")
        if path_is_within(Path(packet["packet_path"]).resolve(), Path(packet["worktree"])):
            raise ContractError(
                f"{packet['packet_path']}: preserve packet outside its worker worktree"
            )
        status = subprocess.run(
            ["git", "-C", packet["worktree"], "status", "--porcelain", "--untracked-files=all"],
            check=True,
            capture_output=True,
            text=True,
        )
        if status.stdout:
            raise ContractError(f"{packet['packet_path']}: worker worktree is not clean")
        evidence.append(
            {
                "subsystem": packet["subsystem"],
                "agent_id": packet["agent_id"],
                "worktree": packet["worktree"],
                "head": entry["HEAD"].lower(),
                "branch": entry["branch"],
                "packet_sha256": hashlib.sha256(
                    Path(packet["packet_path"]).read_bytes()
                ).hexdigest(),
            }
        )
    return sorted(evidence, key=lambda item: item["subsystem"].casefold())


def validate_wave_attestations(
    attestation_paths: list[Path],
    packets: list[dict[str, Any]],
    raw_manifest: dict[str, Any],
    manifest_path: Path,
    manifest_sha256: str,
    base_commit: str,
    rows: list[dict[str, str]],
    report_sha256: str,
) -> None:
    packet_by_subsystem = {packet["subsystem"]: packet for packet in packets}
    seen_waves: set[int] = set()
    seen_subsystems: set[str] = set()
    for attestation_path in attestation_paths:
        raw_output = load_packet(attestation_path)
        if not isinstance(raw_output, dict) or not isinstance(
            raw_output.get("wave_attestation"), dict
        ):
            raise ContractError(f"{attestation_path}: missing wave_attestation")
        attestation = raw_output["wave_attestation"]
        require_exact_keys(
            attestation,
            {
                "schema_version",
                "base_commit",
                "report_sha256",
                "manifest_sha256",
                "wave",
                "packet_records",
            },
            f"{attestation_path} wave_attestation",
        )
        if attestation["schema_version"] != 1:
            raise ContractError(f"{attestation_path}: attestation schema must be 1")
        if attestation["base_commit"] != base_commit:
            raise ContractError(f"{attestation_path}: attestation base changed")
        if attestation["report_sha256"] != report_sha256:
            raise ContractError(f"{attestation_path}: attestation report changed")
        if attestation["manifest_sha256"] != manifest_sha256:
            raise ContractError(f"{attestation_path}: attestation manifest changed")
        wave_number = attestation["wave"]
        if not isinstance(wave_number, int) or wave_number < 1 or wave_number in seen_waves:
            raise ContractError(f"{attestation_path}: invalid or duplicate wave number")
        expected_subsystems = validate_manifest(
            raw_manifest,
            manifest_path,
            base_commit,
            rows,
            report_sha256,
            wave_number,
        )
        records = attestation["packet_records"]
        if not isinstance(records, list):
            raise ContractError(f"{attestation_path}: packet_records must be a list")
        record_subsystems: set[str] = set()
        for record in records:
            if not isinstance(record, dict):
                raise ContractError(f"{attestation_path}: packet record must be an object")
            require_exact_keys(
                record,
                {
                    "subsystem",
                    "agent_id",
                    "worktree",
                    "head",
                    "branch",
                    "packet_sha256",
                },
                f"{attestation_path} packet record",
            )
            subsystem = require_text(record["subsystem"], "attested subsystem")
            if subsystem in record_subsystems or subsystem in seen_subsystems:
                raise ContractError(f"{attestation_path}: subsystem attested more than once")
            if subsystem not in packet_by_subsystem:
                raise ContractError(f"{attestation_path}: attested packet is missing")
            packet = packet_by_subsystem[subsystem]
            branch = require_text(record["branch"], "attested branch")
            if not branch.startswith("refs/heads/"):
                raise ContractError(f"{attestation_path}: attested branch is not local")
            expected_record = {
                "subsystem": subsystem,
                "agent_id": packet["agent_id"],
                "worktree": packet["worktree"],
                "head": base_commit,
                "branch": branch,
                "packet_sha256": hashlib.sha256(
                    Path(packet["packet_path"]).read_bytes()
                ).hexdigest(),
            }
            if record != expected_record:
                raise ContractError(f"{attestation_path}: packet attestation changed")
            record_subsystems.add(subsystem)
        if record_subsystems != expected_subsystems:
            raise ContractError(f"{attestation_path}: attestation does not cover its wave")
        seen_waves.add(wave_number)
        seen_subsystems.update(record_subsystems)

    expected_waves = {wave["wave"] for wave in raw_manifest["waves"]}
    if seen_waves != expected_waves:
        raise ContractError("all-waves consolidation requires one attestation per wave")


def verify_coverage_files(
    repo: Path,
    packets: list[dict[str, Any]],
    base_commit: str,
) -> None:
    line_counts: dict[str, int] = {}
    for packet in packets:
        for file_path in packet["coverage"]["files_reviewed"]:
            result = subprocess.run(
                ["git", "cat-file", "-t", f"{base_commit}:{file_path}"],
                cwd=repo,
                capture_output=True,
                text=True,
            )
            if result.returncode != 0 or result.stdout.strip() != "blob":
                raise ContractError(
                    f"{packet['packet_path']}: reviewed path is not a file at frozen base: "
                    f"{file_path}"
                )
            content = subprocess.run(
                ["git", "show", f"{base_commit}:{file_path}"],
                cwd=repo,
                check=True,
                capture_output=True,
                text=True,
                errors="replace",
            )
            line_counts[file_path] = len(content.stdout.splitlines())
        for finding in packet["findings"]:
            for file_path, start, end in parse_locations(
                finding["locations"],
                f"{packet['packet_path']} finding locations",
            ):
                if start < 1 or end < start or end > line_counts[file_path]:
                    raise ContractError(
                        f"{packet['packet_path']}: location range is outside frozen file: "
                        f"{file_path}:{start}-{end}"
                    )


def validate_manifest(
    raw_manifest: Any,
    manifest_path: Path,
    base_commit: str,
    rows: list[dict[str, str]],
    report_sha256: str,
    wave_number: int | None,
) -> set[str]:
    if not isinstance(raw_manifest, dict):
        raise ContractError(f"{manifest_path}: manifest must be a JSON object")
    require_exact_keys(raw_manifest, MANIFEST_KEYS, str(manifest_path))
    if raw_manifest["schema_version"] != 1:
        raise ContractError(f"{manifest_path}: schema_version must be 1")
    if raw_manifest["base_commit"] != base_commit:
        raise ContractError(f"{manifest_path}: manifest base differs from frozen base")
    require_text(raw_manifest["report"], "manifest.report")
    if raw_manifest["report_sha256"] != report_sha256:
        raise ContractError(f"{manifest_path}: canonical report digest changed")
    if not isinstance(raw_manifest["worker_slots"], int) or raw_manifest["worker_slots"] < 1:
        raise ContractError(f"{manifest_path}: worker_slots must be a positive integer")

    taxonomy = {row["subsystem"] for row in rows}
    rows_by_subsystem: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        rows_by_subsystem[row["subsystem"]].append(row)
    manifest_taxonomy = require_text_list(
        raw_manifest["taxonomy"],
        "manifest.taxonomy",
        allow_empty=False,
    )
    if set(manifest_taxonomy) != taxonomy:
        raise ContractError(f"{manifest_path}: manifest taxonomy differs from live report")
    if manifest_taxonomy != sorted(taxonomy, key=str.casefold):
        raise ContractError(f"{manifest_path}: manifest taxonomy is not deterministically sorted")
    leases = require_text_list(
        raw_manifest["leased_subsystems"],
        "manifest.leased_subsystems",
        allow_empty=True,
    )
    if not set(leases) <= taxonomy:
        raise ContractError(f"{manifest_path}: manifest contains unknown leases")
    if leases != sorted(leases, key=str.casefold):
        raise ContractError(f"{manifest_path}: manifest leases are not deterministically sorted")
    expected_scheduled_count = len(taxonomy - set(leases))
    if raw_manifest["scheduled_subsystem_count"] != expected_scheduled_count:
        raise ContractError(f"{manifest_path}: scheduled subsystem count is inconsistent")

    waves = raw_manifest["waves"]
    if not isinstance(waves, list):
        raise ContractError(f"{manifest_path}: waves must be a list")
    all_scheduled: list[str] = []
    selected: set[str] | None = None
    expected_wave_number = 1
    for raw_wave in waves:
        if not isinstance(raw_wave, dict):
            raise ContractError(f"{manifest_path}: every wave must be an object")
        require_exact_keys(raw_wave, {"wave", "lanes"}, "manifest wave")
        if raw_wave["wave"] != expected_wave_number:
            raise ContractError(f"{manifest_path}: waves must be consecutively numbered")
        expected_wave_number += 1
        lanes = raw_wave["lanes"]
        if not isinstance(lanes, list) or not lanes:
            raise ContractError(f"{manifest_path}: each wave must contain lanes")
        if len(lanes) > raw_manifest["worker_slots"]:
            raise ContractError(f"{manifest_path}: wave exceeds worker capacity")
        lane_subsystems: list[str] = []
        for lane in lanes:
            if not isinstance(lane, dict):
                raise ContractError(f"{manifest_path}: lane must be an object")
            require_exact_keys(
                lane,
                {
                    "subsystem",
                    "known_finding_ids",
                    "unresolved_count",
                    "unresolved_by_severity",
                },
                "manifest lane",
            )
            subsystem = require_text(lane["subsystem"], "manifest lane subsystem")
            if subsystem not in taxonomy or subsystem in leases:
                raise ContractError(f"{manifest_path}: invalid scheduled subsystem {subsystem!r}")
            expected_rows = rows_by_subsystem[subsystem]
            known_ids = require_text_list(
                lane["known_finding_ids"],
                "manifest lane known_finding_ids",
                allow_empty=False,
            )
            if known_ids != sorted(row["finding_id"] for row in expected_rows):
                raise ContractError(f"{manifest_path}: lane finding IDs differ from report")
            expected_unresolved = [row for row in expected_rows if row["fixed"] == "No"]
            if lane["unresolved_count"] != len(expected_unresolved):
                raise ContractError(f"{manifest_path}: unresolved count differs from report")
            severity_counts = lane["unresolved_by_severity"]
            if not isinstance(severity_counts, dict):
                raise ContractError(f"{manifest_path}: severity counts must be an object")
            require_exact_keys(
                severity_counts,
                {"High", "Medium", "Low"},
                "manifest severity counts",
            )
            for severity in ("High", "Medium", "Low"):
                expected_count = sum(
                    row["severity"] == severity for row in expected_unresolved
                )
                if severity_counts[severity] != expected_count:
                    raise ContractError(
                        f"{manifest_path}: {subsystem} {severity} count differs from report"
                    )
            lane_subsystems.append(subsystem)
        if len(lane_subsystems) != len(set(lane_subsystems)):
            raise ContractError(f"{manifest_path}: one wave repeats a subsystem")
        all_scheduled.extend(lane_subsystems)
        if raw_wave["wave"] == wave_number:
            selected = set(lane_subsystems)

    if len(all_scheduled) != len(set(all_scheduled)):
        raise ContractError(f"{manifest_path}: subsystem appears in multiple waves")
    if set(all_scheduled) != taxonomy - set(leases):
        raise ContractError(f"{manifest_path}: waves do not cover every non-leased subsystem")
    if wave_number is None:
        return set(all_scheduled)
    if selected is None:
        raise ContractError(f"{manifest_path}: requested wave {wave_number} does not exist")
    return selected


def normalized_text(value: str) -> str:
    return " ".join(WORD_PATTERN.findall(value.casefold()))


def title_tokens(value: str) -> set[str]:
    return {
        token
        for token in WORD_PATTERN.findall(value.casefold())
        if token not in STOP_WORDS
    }


def source_paths(value: str) -> set[str]:
    try:
        return {
            file_path.casefold()
            for file_path, _, _ in parse_locations(value, "locations")
        }
    except ContractError:
        # Existing report rows predate the packet grammar. Retain bounded legacy
        # extraction for duplicate hints without weakening new-packet parsing.
        pass
    return {
        match.group("path").replace("\\", "/").casefold()
        for match in SOURCE_PATH_PATTERN.finditer(value)
    }


def similarity(left: set[str], right: set[str]) -> float:
    if not left or not right:
        return 0.0
    return len(left & right) / len(left | right)


def exact_fingerprint(title: str, locations: str, trigger: str) -> str:
    return "|".join(
        (normalized_text(title), normalized_text(locations), normalized_text(trigger))
    )


def derive_prefixes(rows: Iterable[dict[str, str]]) -> dict[str, tuple[str, int]]:
    prefixes: dict[str, set[str]] = defaultdict(set)
    prefix_owners: dict[str, set[str]] = defaultdict(set)
    maximums: dict[str, int] = defaultdict(int)
    for row in rows:
        match = PUBLIC_ID_PATTERN.fullmatch(row["finding_id"])
        if match is None:
            raise ContractError(f"cannot derive prefix from {row['finding_id']!r}")
        subsystem = row["subsystem"]
        prefix = match.group("prefix")
        number = int(match.group("number"))
        prefixes[subsystem].add(prefix)
        prefix_owners[prefix].add(subsystem)
        maximums[prefix] = max(maximums[prefix], number)

    ambiguous_owners = {
        prefix: sorted(owners, key=str.casefold)
        for prefix, owners in prefix_owners.items()
        if len(owners) != 1
    }
    if ambiguous_owners:
        raise ContractError(f"public ID prefixes have multiple subsystem owners: {ambiguous_owners}")

    resolved: dict[str, tuple[str, int]] = {}
    for subsystem, values in prefixes.items():
        if len(values) != 1:
            raise ContractError(
                f"subsystem {subsystem!r} has ambiguous ID prefixes {sorted(values)}"
            )
        prefix = next(iter(values))
        resolved[subsystem] = (prefix, maximums[prefix])
    return resolved


def suspected_matches(
    finding: dict[str, Any],
    rows: Iterable[dict[str, str]],
) -> list[str]:
    finding_title = normalized_text(finding["title"])
    finding_tokens = title_tokens(finding["title"])
    finding_paths = source_paths(finding["locations"])
    matches: list[str] = []
    for row in rows:
        same_title = finding_title == normalized_text(row["title"])
        overlapping_paths = bool(finding_paths & source_paths(row["locations"]))
        similar_title = similarity(finding_tokens, title_tokens(row["title"])) >= 0.60
        if same_title or (overlapping_paths and similar_title):
            matches.append(row["finding_id"])
    return matches


def findings_need_duplicate_review(left: dict[str, Any], right: dict[str, Any]) -> bool:
    if exact_fingerprint(left["title"], left["locations"], left["trigger"]) == exact_fingerprint(
        right["title"], right["locations"], right["trigger"]
    ):
        return True
    same_title = normalized_text(left["title"]) == normalized_text(right["title"])
    overlapping_paths = bool(source_paths(left["locations"]) & source_paths(right["locations"]))
    similar_title = similarity(title_tokens(left["title"]), title_tokens(right["title"])) >= 0.60
    return same_title or (overlapping_paths and similar_title)


def load_dispositions(path: Path, base_commit: str) -> dict[str, dict[str, str]]:
    with path.open("r", encoding="utf-8") as stream:
        raw = json.load(stream)
    if not isinstance(raw, dict):
        raise ContractError(f"{path}: dispositions must be a JSON object")
    require_exact_keys(raw, {"schema_version", "base_commit", "decisions"}, str(path))
    if raw["schema_version"] != 1 or raw["base_commit"] != base_commit:
        raise ContractError(f"{path}: disposition schema/base does not match")
    if not isinstance(raw["decisions"], dict):
        raise ContractError(f"{path}: decisions must be an object")

    decisions: dict[str, dict[str, str]] = {}
    for root_cause_key, raw_decision in raw["decisions"].items():
        if not ROOT_CAUSE_PATTERN.fullmatch(root_cause_key):
            raise ContractError(f"{path}: invalid disposition key {root_cause_key!r}")
        if not isinstance(raw_decision, dict):
            raise ContractError(f"{path}: decision for {root_cause_key!r} must be an object")
        decision = require_text(raw_decision.get("decision"), "disposition decision")
        reason = require_text(raw_decision.get("reason"), "disposition reason")
        if decision == "new":
            require_exact_keys(raw_decision, {"decision", "reason"}, root_cause_key)
            decisions[root_cause_key] = {"decision": decision, "reason": reason}
        elif decision == "duplicate":
            require_exact_keys(
                raw_decision,
                {"decision", "duplicate_of", "reason"},
                root_cause_key,
            )
            duplicate_of = require_text(raw_decision["duplicate_of"], "duplicate_of")
            if duplicate_of == root_cause_key:
                raise ContractError(f"{path}: a finding cannot duplicate itself")
            decisions[root_cause_key] = {
                "decision": decision,
                "duplicate_of": duplicate_of,
                "reason": reason,
            }
        else:
            raise ContractError(f"{path}: decision must be new or duplicate")
    return decisions


def consolidate(
    rows: list[dict[str, str]],
    packets: list[dict[str, Any]],
    base_commit: str,
    expected_subsystems: set[str] | None = None,
    dispositions: dict[str, dict[str, str]] | None = None,
) -> dict[str, Any]:
    packets = sorted(
        packets,
        key=lambda packet: (
            packet["subsystem"].casefold(),
            packet["agent_id"],
            packet["packet_path"],
        ),
    )
    packet_subsystems = [packet["subsystem"] for packet in packets]
    if len(packet_subsystems) != len(set(packet_subsystems)):
        raise ContractError("a consolidation wave contains duplicate subsystem packets")
    if expected_subsystems is not None and set(packet_subsystems) != expected_subsystems:
        missing = sorted(expected_subsystems - set(packet_subsystems), key=str.casefold)
        unexpected = sorted(set(packet_subsystems) - expected_subsystems, key=str.casefold)
        raise ContractError(
            f"packet set does not exactly cover manifest wave; missing={missing}, "
            f"unexpected={unexpected}"
        )

    seen_root_causes: dict[str, str] = {}
    candidates: list[dict[str, Any]] = []
    for packet in packets:
        for finding in packet["findings"]:
            root_cause_key = finding["root_cause_key"]
            if root_cause_key in seen_root_causes:
                raise ContractError(
                    f"duplicate root_cause_key {root_cause_key!r} in "
                    f"{seen_root_causes[root_cause_key]} and {packet['packet_path']}"
                )
            seen_root_causes[root_cause_key] = packet["packet_path"]
            candidates.append({"packet": packet, "finding": finding})
    candidates.sort(
        key=lambda candidate: (
            candidate["packet"]["subsystem"].casefold(),
            candidate["finding"]["root_cause_key"],
        )
    )

    prefixes = derive_prefixes(rows)
    exact_existing = {
        exact_fingerprint(row["title"], row["locations"], row["trigger"]): row
        for row in rows
    }
    exact_duplicates: list[dict[str, Any]] = []
    review_required: list[dict[str, Any]] = []
    adjudicated_duplicates: list[dict[str, Any]] = []
    assignable: list[dict[str, Any]] = []

    review_reasons: dict[str, dict[str, Any]] = {}
    for index, candidate in enumerate(candidates):
        for other in candidates[index + 1 :]:
            if not findings_need_duplicate_review(
                candidate["finding"], other["finding"]
            ):
                continue
            left_key = candidate["finding"]["root_cause_key"]
            right_key = other["finding"]["root_cause_key"]
            review_reasons.setdefault(left_key, {}).setdefault(
                "possible_candidate_root_cause_keys", []
            ).append(right_key)
            review_reasons.setdefault(right_key, {}).setdefault(
                "possible_candidate_root_cause_keys", []
            ).append(left_key)

    for candidate in candidates:
        finding = candidate["finding"]
        fingerprint = exact_fingerprint(
            finding["title"], finding["locations"], finding["trigger"]
        )
        if fingerprint in exact_existing:
            exact_duplicates.append(
                {
                    "root_cause_key": finding["root_cause_key"],
                    "existing_finding_id": exact_existing[fingerprint]["finding_id"],
                    "packet": candidate["packet"]["packet_path"],
                }
            )
            continue
        possible_matches = suspected_matches(finding, rows)
        if possible_matches:
            review_reasons.setdefault(finding["root_cause_key"], {})[
                "possible_existing_finding_ids"
            ] = possible_matches

    exact_duplicate_keys = {item["root_cause_key"] for item in exact_duplicates}
    for root_cause_key in exact_duplicate_keys:
        review_reasons.pop(root_cause_key, None)

    decisions = dispositions or {}
    unexpected_decisions = sorted(set(decisions) - set(review_reasons))
    if unexpected_decisions:
        raise ContractError(
            f"dispositions name findings that do not require review: {unexpected_decisions}"
        )
    existing_ids = {row["finding_id"] for row in rows}
    candidate_keys = {candidate["finding"]["root_cause_key"] for candidate in candidates}
    for source_key, decision in decisions.items():
        if decision["decision"] != "duplicate":
            continue
        target = decision["duplicate_of"]
        visited = {source_key}
        while target in candidate_keys:
            if target in visited:
                raise ContractError(f"duplicate dispositions contain a cycle at {target!r}")
            visited.add(target)
            if target in exact_duplicate_keys:
                raise ContractError(
                    f"{source_key!r} must point to the exact existing ID, not "
                    f"exact-duplicate candidate {target!r}"
                )
            target_decision = decisions.get(target)
            if target in review_reasons and target_decision is None:
                raise ContractError(
                    f"{source_key!r} points to unresolved review candidate {target!r}"
                )
            if target_decision is None or target_decision["decision"] == "new":
                break
            target = target_decision["duplicate_of"]
        if target not in candidate_keys and target not in existing_ids:
            raise ContractError(
                f"disposition for {source_key!r} has unknown duplicate target {target!r}"
            )

    for candidate in candidates:
        finding = candidate["finding"]
        root_cause_key = finding["root_cause_key"]
        if root_cause_key in exact_duplicate_keys:
            continue
        if root_cause_key not in review_reasons:
            assignable.append(candidate)
            continue
        decision = decisions.get(root_cause_key)
        if decision is None:
            review_required.append(
                {
                    "root_cause_key": root_cause_key,
                    "packet": candidate["packet"]["packet_path"],
                    "finding": finding,
                    **review_reasons[root_cause_key],
                }
            )
            continue
        if decision["decision"] == "new":
            candidate["adjudication"] = decision["reason"]
            assignable.append(candidate)
            continue
        duplicate_of = decision["duplicate_of"]
        adjudicated_duplicates.append(
            {
                "root_cause_key": root_cause_key,
                "duplicate_of": duplicate_of,
                "reason": decision["reason"],
                "packet": candidate["packet"]["packet_path"],
            }
        )

    assignable.sort(
        key=lambda candidate: (
            candidate["packet"]["subsystem"].casefold(),
            candidate["finding"]["root_cause_key"],
            candidate["finding"]["title"].casefold(),
        )
    )
    next_numbers = {subsystem: maximum for subsystem, (_, maximum) in prefixes.items()}
    used_ids = {row["finding_id"] for row in rows}
    proposed_rows: list[dict[str, str]] = []
    candidate_metadata: list[dict[str, Any]] = []
    for candidate in assignable:
        packet = candidate["packet"]
        finding = candidate["finding"]
        subsystem = packet["subsystem"]
        if subsystem not in prefixes:
            raise ContractError(f"subsystem {subsystem!r} has no existing ID prefix")
        prefix, _ = prefixes[subsystem]
        while True:
            next_numbers[subsystem] += 1
            finding_id = f"{prefix}-{next_numbers[subsystem]:03d}"
            if finding_id not in used_ids:
                break
        used_ids.add(finding_id)
        row = {
            "finding_id": finding_id,
            "subsystem": subsystem,
            "severity": finding["severity"],
            "title": finding["title"],
            "locations": finding["locations"],
            "trigger": finding["trigger"],
            "impact": finding["impact"],
            "confidence": finding["confidence"],
            "fixed": "No",
        }
        proposed_rows.append({column: row[column] for column in REQUIRED_COLUMNS})
        candidate_metadata.append(
            {
                "finding_id": finding_id,
                "root_cause_key": finding["root_cause_key"],
                "packet": packet["packet_path"],
                "agent_id": packet["agent_id"],
                "secondary_subsystems": packet["secondary_subsystems"],
                "evidence": finding["evidence"],
                "reproduction": finding["reproduction"],
                "adjudication": candidate.get("adjudication"),
            }
        )

    return {
        "schema_version": 1,
        "base_commit": base_commit,
        "packet_count": len(packets),
        "covered_subsystems": sorted(packet_subsystems, key=str.casefold),
        "proposed_rows": proposed_rows,
        "candidate_metadata": candidate_metadata,
        "exact_duplicates": exact_duplicates,
        "adjudicated_duplicates": adjudicated_duplicates,
        "review_required": review_required,
    }


def load_packet(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def run_self_test() -> None:
    rows = [
        {
            "finding_id": "CORE-001",
            "subsystem": "Core",
            "severity": "High",
            "title": "Existing allocator defect",
            "locations": "SkullbonezSource/Core/Allocator.cpp:10",
            "trigger": "Allocate once.",
            "impact": "Crash.",
            "confidence": "High",
            "fixed": "No",
        },
        {
            "finding_id": "UI-001",
            "subsystem": "UI Library",
            "severity": "Low",
            "title": "Existing UI defect",
            "locations": "SkullbonezSource/UI/Panel.cpp:20",
            "trigger": "Open once.",
            "impact": "Wrong text.",
            "confidence": "Medium",
            "fixed": "Yes",
        },
    ]
    base_commit = "b" * 40
    with tempfile.TemporaryDirectory() as temporary_directory:
        packet_path = Path(temporary_directory) / "packet.json"
        raw_packet = {
            "schema_version": 1,
            "base_commit": base_commit,
            "agent_id": "worker-core",
            "worktree": str(Path(temporary_directory).resolve()),
            "subsystem": "Core",
            "secondary_subsystems": [],
            "coverage": {
                "files_reviewed": ["SkullbonezSource/Core/Queue.cpp"],
                "ownership_evidence": {
                    "SkullbonezSource/Core/Queue.cpp": "Core path and queue owner."
                },
                "entry_points": ["Core::Queue::Pop"],
                "tests_reviewed": ["TestQueue.cpp"],
                "commands": ["rg -n Queue SkullbonezSource/Core"],
            },
            "findings": [
                {
                    "root_cause_key": "queue-empty-read",
                    "severity": "Medium",
                    "title": "Queue reads an empty slot",
                    "locations": "SkullbonezSource/Core/Queue.cpp:42-48",
                    "trigger": "Pop an empty queue.",
                    "impact": "Returns stale memory.",
                    "confidence": "High",
                    "evidence": ["The empty branch reads before checking size."],
                    "reproduction": "Add a focused empty-pop test.",
                }
            ],
        }
        packet_path.write_text(json.dumps(raw_packet), encoding="utf-8")
        validated = validate_packet(
            load_packet(packet_path),
            packet_path,
            base_commit,
            {"Core", "UI Library"},
        )
        raw_manifest = {
            "schema_version": 1,
            "base_commit": base_commit,
            "report": "report.csv",
            "report_sha256": "d" * 64,
            "worker_slots": 1,
            "taxonomy": ["Core", "UI Library"],
            "leased_subsystems": ["UI Library"],
            "scheduled_subsystem_count": 1,
            "waves": [
                {
                    "wave": 1,
                    "lanes": [
                        {
                            "subsystem": "Core",
                            "known_finding_ids": ["CORE-001"],
                            "unresolved_count": 1,
                            "unresolved_by_severity": {
                                "High": 1,
                                "Medium": 0,
                                "Low": 0,
                            },
                        }
                    ],
                }
            ],
        }
        expected = validate_manifest(
            raw_manifest,
            Path(temporary_directory).resolve() / "manifest.json",
            base_commit,
            rows,
            "d" * 64,
            1,
        )
        result = consolidate(rows, [validated], base_commit, expected)

    assert result["packet_count"] == 1
    assert result["covered_subsystems"] == ["Core"]
    assert result["proposed_rows"][0]["finding_id"] == "CORE-002"
    assert not result["exact_duplicates"]
    assert not result["review_required"]

    exact_packet = json.loads(json.dumps(raw_packet))
    exact_packet["findings"][0].update(
        {
            "root_cause_key": "existing-allocator-defect",
            "title": rows[0]["title"],
            "locations": rows[0]["locations"],
            "trigger": rows[0]["trigger"],
        }
    )
    exact_packet["coverage"]["files_reviewed"].append(
        "SkullbonezSource/Core/Allocator.cpp"
    )
    exact_packet["coverage"]["ownership_evidence"][
        "SkullbonezSource/Core/Allocator.cpp"
    ] = "The existing Core allocator row owns this comparison."
    exact_validated = validate_packet(
        exact_packet,
        Path(temporary_directory).resolve() / "exact.json",
        base_commit,
        {"Core", "UI Library"},
    )
    exact_result = consolidate(rows, [exact_validated], base_commit)
    assert exact_result["exact_duplicates"][0]["existing_finding_id"] == "CORE-001"
    assert not exact_result["proposed_rows"]

    suspected_packet = json.loads(json.dumps(raw_packet))
    suspected_packet["findings"][0].update(
        {
            "root_cause_key": "allocator-defect-can-crash",
            "title": "Existing allocator defect can crash",
            "locations": "SkullbonezSource/Core/Allocator.cpp:50",
        }
    )
    suspected_packet["coverage"]["files_reviewed"].append(
        "SkullbonezSource/Core/Allocator.cpp"
    )
    suspected_packet["coverage"]["ownership_evidence"][
        "SkullbonezSource/Core/Allocator.cpp"
    ] = "The existing Core allocator row owns this comparison."
    suspected_validated = validate_packet(
        suspected_packet,
        Path(temporary_directory).resolve() / "suspected.json",
        base_commit,
        {"Core", "UI Library"},
    )
    suspected_result = consolidate(rows, [suspected_validated], base_commit)
    assert suspected_result["review_required"][0][
        "possible_existing_finding_ids"
    ] == ["CORE-001"]
    assert not suspected_result["proposed_rows"]

    ui_packet = json.loads(json.dumps(raw_packet))
    ui_packet.update(
        {
            "agent_id": "worker-ui",
            "worktree": str((Path(temporary_directory) / "ui-worktree").resolve()),
            "subsystem": "UI Library",
            "secondary_subsystems": ["Core"],
        }
    )
    ui_packet["coverage"] = {
        "files_reviewed": [
            "SkullbonezSource/UI/QueuePanel.cpp",
            "SkullbonezSource/Core/Queue.cpp",
        ],
        "ownership_evidence": {
            "SkullbonezSource/UI/QueuePanel.cpp": "UI Library product path.",
            "SkullbonezSource/Core/Queue.cpp": "Secondary Core queue owner.",
        },
        "entry_points": ["UI::QueuePanel::Draw"],
        "tests_reviewed": ["UiBoundaryUnitTests"],
        "commands": ["rg -n QueuePanel SkullbonezSource/UI"],
    }
    ui_packet["findings"][0]["root_cause_key"] = "ui-queue-empty-read"
    ui_validated = validate_packet(
        ui_packet,
        Path(temporary_directory).resolve() / "ui.json",
        base_commit,
        {"Core", "UI Library"},
    )
    candidate_duplicate_result = consolidate(
        rows,
        [validated, ui_validated],
        base_commit,
        {"Core", "UI Library"},
    )
    review_keys = {
        item["root_cause_key"] for item in candidate_duplicate_result["review_required"]
    }
    assert review_keys == {"queue-empty-read", "ui-queue-empty-read"}
    assert not candidate_duplicate_result["proposed_rows"]

    adjudicated_result = consolidate(
        rows,
        [validated, ui_validated],
        base_commit,
        {"Core", "UI Library"},
        {
            "queue-empty-read": {
                "decision": "new",
                "reason": "Core owns the queue invariant.",
            },
            "ui-queue-empty-read": {
                "decision": "duplicate",
                "duplicate_of": "queue-empty-read",
                "reason": "The UI lane observed the same Core root cause.",
            },
        },
    )
    assert [row["finding_id"] for row in adjudicated_result["proposed_rows"]] == [
        "CORE-002"
    ]
    assert adjudicated_result["adjudicated_duplicates"][0][
        "root_cause_key"
    ] == "ui-queue-empty-read"

    campaign_manifest = json.loads(json.dumps(raw_manifest))
    campaign_manifest["leased_subsystems"] = []
    campaign_manifest["scheduled_subsystem_count"] = 2
    campaign_manifest["waves"].append(
        {
            "wave": 2,
            "lanes": [
                {
                    "subsystem": "UI Library",
                    "known_finding_ids": ["UI-001"],
                    "unresolved_count": 0,
                    "unresolved_by_severity": {
                        "High": 0,
                        "Medium": 0,
                        "Low": 0,
                    },
                }
            ],
        }
    )
    with tempfile.TemporaryDirectory() as campaign_directory:
        campaign_root = Path(campaign_directory)
        manifest_path = campaign_root / "manifest.json"
        manifest_path.write_text(
            json.dumps(campaign_manifest, sort_keys=True),
            encoding="utf-8",
        )
        manifest_sha256 = hashlib.sha256(manifest_path.read_bytes()).hexdigest()
        core_packet_path = campaign_root / "core.json"
        ui_packet_path = campaign_root / "ui.json"
        core_packet_path.write_text(json.dumps(raw_packet), encoding="utf-8")
        ui_packet_path.write_text(json.dumps(ui_packet), encoding="utf-8")
        campaign_core = validate_packet(
            load_packet(core_packet_path),
            core_packet_path,
            base_commit,
            {"Core", "UI Library"},
        )
        campaign_ui = validate_packet(
            load_packet(ui_packet_path),
            ui_packet_path,
            base_commit,
            {"Core", "UI Library"},
        )
        attestation_paths: list[Path] = []
        for wave_number, packet in ((1, campaign_core), (2, campaign_ui)):
            packet_path = Path(packet["packet_path"])
            attestation_path = campaign_root / f"wave-{wave_number}.json"
            attestation_path.write_text(
                json.dumps(
                    {
                        "wave_attestation": {
                            "schema_version": 1,
                            "base_commit": base_commit,
                            "report_sha256": "d" * 64,
                            "manifest_sha256": manifest_sha256,
                            "wave": wave_number,
                            "packet_records": [
                                {
                                    "subsystem": packet["subsystem"],
                                    "agent_id": packet["agent_id"],
                                    "worktree": packet["worktree"],
                                    "head": base_commit,
                                    "branch": f"refs/heads/test-wave-{wave_number}",
                                    "packet_sha256": hashlib.sha256(
                                        packet_path.read_bytes()
                                    ).hexdigest(),
                                }
                            ],
                        }
                    }
                ),
                encoding="utf-8",
            )
            attestation_paths.append(attestation_path)
        validate_wave_attestations(
            attestation_paths,
            [campaign_core, campaign_ui],
            campaign_manifest,
            manifest_path,
            manifest_sha256,
            base_commit,
            rows,
            "d" * 64,
        )
        campaign_result = consolidate(
            rows,
            [campaign_core, campaign_ui],
            base_commit,
            {"Core", "UI Library"},
        )
        assert {
            item["root_cause_key"] for item in campaign_result["review_required"]
        } == {"queue-empty-read", "ui-queue-empty-read"}
        reversed_result = consolidate(
            rows,
            [campaign_ui, campaign_core],
            base_commit,
            {"Core", "UI Library"},
        )
        assert json.dumps(campaign_result, sort_keys=True) == json.dumps(
            reversed_result,
            sort_keys=True,
        )

    try:
        derive_prefixes(
            [
                {**rows[0], "finding_id": "SHARED-001", "subsystem": "Core"},
                {
                    **rows[1],
                    "finding_id": "SHARED-002",
                    "subsystem": "UI Library",
                },
            ]
        )
    except ContractError:
        pass
    else:
        raise AssertionError("public ID prefixes must have one global owner")

    try:
        consolidate(rows, [], base_commit, {"Core"})
    except ContractError:
        pass
    else:
        raise AssertionError("missing manifest lane packets must fail closed")

    duplicate_worktree_packet = json.loads(json.dumps(validated))
    duplicate_worktree_packet["agent_id"] = "worker-ui"
    try:
        verify_packet_worktrees(
            Path("."),
            [validated, duplicate_worktree_packet],
            base_commit,
        )
    except ContractError:
        pass
    else:
        raise AssertionError("worker worktrees must be unique")

    try:
        consolidate(rows, [validated, validated], base_commit)
    except ContractError:
        pass
    else:
        raise AssertionError("duplicate subsystem packets must fail closed")

    invalid = dict(raw_packet)
    invalid["subsystem"] = "Unknown"
    try:
        validate_packet(invalid, Path("invalid.json"), base_commit, {"Core"})
    except ContractError:
        pass
    else:
        raise AssertionError("unknown packet subsystems must fail closed")

    invalid_location = json.loads(json.dumps(raw_packet))
    invalid_location["findings"][0]["locations"] = "No/Such/File.cpp:999"
    try:
        validate_packet(
            invalid_location,
            Path("invalid-location.json"),
            base_commit,
            {"Core", "UI Library"},
        )
    except ContractError:
        pass
    else:
        raise AssertionError("finding locations must be covered reviewed files")

    for malformed_locations in (
        "SkullbonezSource/Core/Allocator.cpp:10;999999",
        "SkullbonezSource/Core/Allocator.cpp:10; No/Such/File.md:999999",
        "SkullbonezSource/Core/Allocator.cpp:10 trailing text",
        "SkullbonezSource/Core/Allocator.cpp:10;",
    ):
        malformed = json.loads(json.dumps(raw_packet))
        malformed["findings"][0]["locations"] = malformed_locations
        try:
            validate_packet(
                malformed,
                Path("malformed-location.json"),
                base_commit,
                {"Core", "UI Library"},
            )
        except ContractError:
            pass
        else:
            raise AssertionError(
                f"partial or malformed location text must fail: {malformed_locations}"
            )


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path("."))
    parser.add_argument(
        "--report",
        type=Path,
        default=Path("Agentic/Bugs/master_bug_report.csv"),
    )
    parser.add_argument("--base-commit")
    parser.add_argument("--manifest", type=Path)
    scope = parser.add_mutually_exclusive_group()
    scope.add_argument("--wave", type=int)
    scope.add_argument("--all-waves", action="store_true")
    parser.add_argument("--packet", type=Path, action="append", default=[])
    parser.add_argument("--attestation", type=Path, action="append", default=[])
    parser.add_argument("--dispositions", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        if args.self_test:
            run_self_test()
            print("parallel bug-finder packet consolidator self-test: PASS")
            return 0
        if not args.packet:
            raise ContractError("at least one --packet is required")
        if args.manifest is None:
            raise ContractError("--manifest is required")
        if not args.all_waves and (args.wave is None or args.wave < 1):
            raise ContractError("select a positive --wave or --all-waves")
        if args.all_waves and not args.attestation:
            raise ContractError("--all-waves requires every wave --attestation")
        if not args.all_waves and args.attestation:
            raise ContractError("--attestation is valid only with --all-waves")
        if not args.all_waves and args.dispositions is not None:
            raise ContractError("duplicate dispositions are campaign-wide; use --all-waves")

        repo = args.repo.resolve()
        report_path = args.report
        if not report_path.is_absolute():
            report_path = repo / report_path
        rows = read_report(report_path)
        taxonomy = {row["subsystem"] for row in rows}
        base_commit = resolve_base_commit(repo, args.base_commit)
        report_sha256 = report_digest_at_base(repo, report_path, base_commit)
        manifest_path = args.manifest.resolve()
        raw_manifest = load_packet(manifest_path)
        manifest_sha256 = hashlib.sha256(manifest_path.read_bytes()).hexdigest()
        expected_subsystems = validate_manifest(
            raw_manifest,
            manifest_path,
            base_commit,
            rows,
            report_sha256,
            None if args.all_waves else args.wave,
        )
        packets = [
            validate_packet(
                load_packet(packet_path.resolve()),
                packet_path.resolve(),
                base_commit,
                taxonomy,
            )
            for packet_path in args.packet
        ]
        verify_coverage_files(repo, packets, base_commit)
        worktree_evidence: list[dict[str, str]] | None = None
        if args.all_waves:
            validate_wave_attestations(
                [path.resolve() for path in args.attestation],
                packets,
                raw_manifest,
                manifest_path,
                manifest_sha256,
                base_commit,
                rows,
                report_sha256,
            )
        else:
            worktree_evidence = verify_packet_worktrees(repo, packets, base_commit)
        dispositions = (
            load_dispositions(args.dispositions.resolve(), base_commit)
            if args.dispositions is not None
            else None
        )
        output = consolidate(
            rows,
            packets,
            base_commit,
            expected_subsystems,
            dispositions,
        )
        output["scope"] = "all-waves" if args.all_waves else f"wave-{args.wave}"
        output["authoritative_for_report"] = bool(
            args.all_waves and not output["review_required"]
        )
        if worktree_evidence is not None:
            output["wave_attestation"] = {
                "schema_version": 1,
                "base_commit": base_commit,
                "report_sha256": report_sha256,
                "manifest_sha256": manifest_sha256,
                "wave": args.wave,
                "packet_records": worktree_evidence,
            }
        rendered = json.dumps(output, indent=2, sort_keys=True) + "\n"
        if args.output is not None:
            output_path = args.output.resolve()
            if output_path == report_path.resolve():
                raise ContractError("candidate output must not overwrite the canonical report")
            if not output_path.parent.is_dir():
                raise ContractError("candidate output parent directory does not exist")
            output_path.write_text(rendered, encoding="utf-8")
        else:
            print(rendered, end="")
        return 0
    except (
        ContractError,
        OSError,
        json.JSONDecodeError,
        subprocess.CalledProcessError,
    ) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
