#!/usr/bin/env python3
#
# File: tools/check_dependency_graph.py
# Purpose:
#   Enforce physical include direction and single-project source ownership.
#
# Summary:
#   Loads a data-only package graph, resolves tracked local include edges, and
#   checks Visual Studio project membership. The same evaluator runs embedded
#   positive/negative fixtures so new package rules require data, not code.
#
# Glossary:
#   Physical edge: Resolved quoted include from one tracked source file to
#     another path below SkullbonezSource.
#   Allow rule: Runtime-package row limiting only edges whose target is inside
#     the Runtime scope.
#   Deny rule: Source/target prefix pair that must never form an include edge.
#   Fixture: Synthetic edge or project membership proving a rule accepts and
#     rejects the intended cases.
#
# Invariants:
#   - Rules are qualitative package relationships, never frozen hit counts.
#   - Missing/unresolved external includes are ignored; resolved repository
#     edges and declared project items are authoritative.
#   - Every restrictive rule has both a passing and failing fixture.
#   - Adding package rows or fixtures requires no Python change.
#
# Related:
#   - tools/dependency_graph_rules.json
#   - AGENTS.md
#   - Agentic/Plans/TODO/ui-renderer-hard-boundary.md

from __future__ import annotations

import argparse
import json
import posixpath
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path


INCLUDE_PATTERN = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.MULTILINE)
SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".inl", ".hlsl"}
MSBUILD_NAMESPACE = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}


@dataclass(frozen=True)
class Finding:
    rule_id: str
    source: str
    target: str
    detail: str


def normalize(value: str) -> str:
    return posixpath.normpath(value.replace("\\", "/")).lstrip("./")


def matches_prefix(path: str, prefix: str) -> bool:
    path = normalize(path)
    prefix = normalize(prefix).rstrip("/")
    return path == prefix or path.startswith(prefix + "/")


def source_matches(rule: dict, source: str) -> bool:
    if source in [normalize(item) for item in rule.get("source_files", [])]:
        return True
    return any(matches_prefix(source, prefix) for prefix in rule.get("source_prefixes", []))


def edge_violates(rule: dict, source: str, target: str) -> bool:
    if not source_matches(rule, source):
        return False
    mode = rule["mode"]
    if mode == "deny":
        return any(matches_prefix(target, prefix) for prefix in rule["target_prefixes"])
    if mode == "allow":
        if not matches_prefix(target, rule["target_scope"]):
            return False
        return not any(matches_prefix(target, prefix) for prefix in rule["allowed_target_prefixes"])
    raise ValueError(f"unsupported rule mode: {mode}")


def evaluate_edge(rules: list[dict], source: str, target: str) -> list[Finding]:
    findings: list[Finding] = []
    for rule in rules:
        if edge_violates(rule, source, target):
            findings.append(
                Finding(
                    rule_id=rule["id"],
                    source=source,
                    target=target,
                    detail=f"{rule['mode']} package edge rejected",
                )
            )
    return findings


def tracked_source_files(repo: Path, source_root: str) -> list[str]:
    result = subprocess.run(
        ["git", "ls-files", source_root],
        cwd=repo,
        text=True,
        capture_output=True,
        check=True,
    )
    return [
        normalize(line)
        for line in result.stdout.splitlines()
        if line and Path(line).suffix.lower() in SOURCE_SUFFIXES
    ]


def resolve_include(repo: Path, source_root: str, source: str, include: str) -> str | None:
    source_path = Path(source)
    relative_candidate = repo / source_path.parent / include
    rooted_candidate = repo / source_root / include
    candidate = relative_candidate if relative_candidate.exists() else rooted_candidate
    try:
        relative = candidate.resolve().relative_to((repo / source_root).resolve())
    except ValueError:
        return None
    return normalize(relative.as_posix())


def scan_includes(repo: Path, source_root: str, rules: list[dict]) -> list[Finding]:
    findings: list[Finding] = []
    for tracked in tracked_source_files(repo, source_root):
        path = repo / tracked
        source = normalize(Path(tracked).relative_to(source_root).as_posix())
        text = path.read_text(encoding="utf-8-sig", errors="replace")
        for match in INCLUDE_PATTERN.finditer(text):
            target = resolve_include(repo, source_root, tracked, match.group(1))
            if target is None:
                continue
            findings.extend(evaluate_edge(rules, source, target))
    return findings


def project_items(repo: Path, project_name: str) -> set[str]:
    root = ET.parse(repo / project_name).getroot()
    items: set[str] = set()
    for item_type in ("ClCompile", "ClInclude"):
        for element in root.findall(f".//m:{item_type}", MSBUILD_NAMESPACE):
            include = element.get("Include")
            if include:
                items.add(normalize(include))
    return items


def scan_project_rules(repo: Path, rules: list[dict]) -> list[Finding]:
    project_names = sorted(
        {
            project
            for rule in rules
            for project in [rule["required_project"], *rule.get("forbidden_projects", [])]
        }
    )
    membership = {name: project_items(repo, name) for name in project_names}
    findings: list[Finding] = []
    for rule in rules:
        prefix = normalize(rule["path_prefix"])
        suffixes = tuple(rule["suffixes"])
        tracked = subprocess.run(
            ["git", "ls-files", prefix],
            cwd=repo,
            text=True,
            capture_output=True,
            check=True,
        )
        governed_paths = {
            normalize(path)
            for path in tracked.stdout.splitlines()
            if path and normalize(path).endswith(suffixes)
        }
        for path in sorted(governed_paths):
            required = rule["required_project"]
            if path not in membership[required]:
                findings.append(Finding(rule["id"], path, required, "missing required project ownership"))
            for forbidden in rule.get("forbidden_projects", []):
                if path in membership[forbidden]:
                    findings.append(Finding(rule["id"], path, forbidden, "forbidden duplicate project ownership"))
    return findings


def self_test(config: dict) -> list[str]:
    errors: list[str] = []
    for rule in config["include_rules"]:
        source = normalize(rule.get("source_files", [rule.get("source_prefixes", ["Core"])[0] + "/Fixture.cpp"])[0])
        positive = normalize(rule["positive_target"])
        if edge_violates(rule, source, positive):
            errors.append(f"{rule['id']}: positive fixture was rejected")
        negative = rule.get("negative_target")
        if negative is not None and not edge_violates(rule, source, normalize(negative)):
            errors.append(f"{rule['id']}: negative fixture was accepted")
    for rule in config["project_rules"]:
        required = rule["required_project"]
        if rule["positive_projects"] != [required]:
            errors.append(f"{rule['id']}: positive project fixture must contain only {required}")
        if required in rule["negative_projects"] or not any(
            project in rule.get("forbidden_projects", []) for project in rule["negative_projects"]
        ):
            errors.append(f"{rule['id']}: negative project fixture does not exercise a forbidden owner")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument(
        "--rules",
        type=Path,
        default=Path(__file__).with_name("dependency_graph_rules.json"),
    )
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    repo = args.repo.resolve()
    config = json.loads(args.rules.read_text(encoding="utf-8"))
    if config.get("version") != 1:
        print("ERROR: unsupported dependency graph rule version", file=sys.stderr)
        return 2

    fixture_errors = self_test(config)
    if fixture_errors:
        for error in fixture_errors:
            print(f"SELF_TEST_FAIL: {error}", file=sys.stderr)
        return 1
    if args.self_test:
        print(
            f"SELF_TEST_PASS: {len(config['include_rules'])} include-rule fixtures and "
            f"{len(config['project_rules'])} project-rule fixtures passed"
        )
        return 0

    findings = scan_includes(repo, config["source_root"], config["include_rules"])
    findings.extend(scan_project_rules(repo, config["project_rules"]))
    for finding in findings:
        print(
            f"ERROR[{finding.rule_id}]: {finding.source} -> {finding.target}: {finding.detail}",
            file=sys.stderr,
        )
    print(
        f"Dependency graph summary: include_rules={len(config['include_rules'])} "
        f"project_rules={len(config['project_rules'])} findings={len(findings)}"
    )
    if findings:
        return 1
    print("PASS: dependency graph and project ownership are valid.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
