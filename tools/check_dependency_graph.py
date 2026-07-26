#!/usr/bin/env python3
#
# File: tools/check_dependency_graph.py
# Purpose:
#   Enforce physical include direction, retired ownership vocabulary, and
#   single-project source ownership.
#
# Summary:
#   Loads data-only package and content rules, resolves live repository include
#   edges, scans bounded source scopes for explicitly retired concept names,
#   and checks Visual Studio project membership. The same evaluators run
#   embedded positive/negative fixtures so new ownership rules require data,
#   not checker branches.
#
# Glossary:
#   Physical edge: Resolved quoted or angle-bracket include from one tracked or
#     untracked live source file to another path below SkullbonezSource.
#   Allow rule: Runtime-package row limiting only edges whose target is inside
#     the Runtime scope.
#   Deny rule: Source/target prefix pair that must never form an include edge.
#   Content rule: Bounded deletion check for named retired vocabulary. It is
#     not a general word census or frozen occurrence budget.
#   Fixture matrix: Synthetic source/target edges proving every governed
#     package accepts one allowed edge and rejects every named forbidden edge,
#     plus source snippets proving every retired literal is detected.
#
# Invariants:
#   - Rules are qualitative package relationships, never frozen hit counts.
#   - Content rules name deleted concepts exactly; broad architectural language
#     remains a review proof instead of becoming a spelling budget.
#   - Missing/unresolved external includes are ignored; resolved repository
#     edges and declared project items are authoritative.
#   - Every restrictive rule has both a passing and failing fixture.
#   - Adding package rows or fixtures requires no Python change.
#
# Related:
#   - tools/dependency_graph_rules.json
#   - AGENTS.md
#   - Agentic/Reports/2026-07-25/ui-renderer-hard-boundary-closure.md

from __future__ import annotations

import argparse
import json
import posixpath
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path


INCLUDE_PATTERN = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]', re.MULTILINE)
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
        allowed_file = target in [normalize(item) for item in rule.get("allowed_target_files", [])]
        allowed_prefix = any(matches_prefix(target, prefix) for prefix in rule["allowed_target_prefixes"])
        return not allowed_file and not allowed_prefix
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


def repository_source_files(repo: Path, source_root: str) -> list[str]:
    result = subprocess.run(
        [
            "git",
            "ls-files",
            "-z",
            "--cached",
            "--others",
            "--exclude-standard",
            "--",
            source_root,
        ],
        cwd=repo,
        text=True,
        capture_output=True,
        check=True,
    )
    return [
        normalize(path)
        for path in result.stdout.split("\0")
        if path
        and Path(path).suffix.lower() in SOURCE_SUFFIXES
        and (repo / path).is_file()
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


def scan_include_files(
    repo: Path,
    source_root: str,
    rules: list[dict],
    tracked_files: list[str],
) -> list[Finding]:
    findings: list[Finding] = []
    for tracked in tracked_files:
        path = repo / tracked
        source = normalize(Path(tracked).relative_to(source_root).as_posix())
        text = path.read_text(encoding="utf-8-sig", errors="replace")
        for match in INCLUDE_PATTERN.finditer(text):
            target = resolve_include(repo, source_root, tracked, match.group(1))
            if target is None:
                continue
            findings.extend(evaluate_edge(rules, source, target))
    return findings


def scan_includes(repo: Path, source_root: str, rules: list[dict]) -> list[Finding]:
    return scan_include_files(repo, source_root, rules, repository_source_files(repo, source_root))


def project_items(repo: Path, project_name: str) -> set[str]:
    root = ET.parse(repo / project_name).getroot()
    items: set[str] = set()
    for item_type in ("ClCompile", "ClInclude"):
        for element in root.findall(f".//m:{item_type}", MSBUILD_NAMESPACE):
            include = element.get("Include")
            if include:
                items.add(normalize(include))
    return items


def evaluate_project_rule(
    rule: dict,
    governed_paths: set[str],
    membership: dict[str, set[str]],
) -> list[Finding]:
    findings: list[Finding] = []
    for path in sorted(governed_paths):
        required = rule["required_project"]
        if path not in membership[required]:
            findings.append(Finding(rule["id"], path, required, "missing required project ownership"))
        for forbidden in rule.get("forbidden_projects", []):
            if path in membership[forbidden]:
                findings.append(Finding(rule["id"], path, forbidden, "forbidden duplicate project ownership"))
    return findings


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
        findings.extend(evaluate_project_rule(rule, governed_paths, membership))
    return findings


def include_fixture_sources(rule: dict) -> list[str]:
    source_files = rule.get("source_files", [])
    if source_files:
        return [normalize(source) for source in source_files]
    return [normalize(f"{prefix}/Fixture.cpp") for prefix in rule.get("source_prefixes", ["Core"])]


def include_fixture_negative_targets(rule: dict) -> list[str]:
    targets = [*rule.get("negative_targets", [])]
    if rule.get("negative_target") is not None:
        targets.append(rule["negative_target"])
    targets.extend(fixture["target"] for fixture in rule.get("negative_include_fixtures", []))
    return list(dict.fromkeys(normalize(target) for target in targets))


def include_fixture_negative_entries(rule: dict) -> list[tuple[str, str, bool]]:
    entries = {
        target: (target, target, True)
        for target in include_fixture_negative_targets(rule)
    }
    for fixture in rule.get("negative_include_fixtures", []):
        target = normalize(fixture["target"])
        entries[target] = (target, fixture["include"], False)
    return list(entries.values())


def evaluate_content_rule(rule: dict, source: str, text: str) -> list[Finding]:
    if not source_matches(rule, source):
        return []

    findings: list[Finding] = []
    for literal in rule["forbidden_literals"]:
        offset = text.find(literal)
        while offset != -1:
            line = text.count("\n", 0, offset) + 1
            findings.append(
                Finding(
                    rule_id=rule["id"],
                    source=source,
                    target=literal,
                    detail=f'retired vocabulary "{literal}" found at line {line}',
                )
            )
            offset = text.find(literal, offset + len(literal))
    return findings


def scan_content_files(
    repo: Path,
    source_root: str,
    rules: list[dict],
    tracked_files: list[str],
) -> list[Finding]:
    findings: list[Finding] = []
    for tracked in tracked_files:
        path = repo / tracked
        source = normalize(Path(tracked).relative_to(source_root).as_posix())
        text = path.read_text(encoding="utf-8-sig", errors="replace")
        for rule in rules:
            findings.extend(evaluate_content_rule(rule, source, text))
    return findings


def scan_content(repo: Path, source_root: str, rules: list[dict]) -> list[Finding]:
    return scan_content_files(repo, source_root, rules, repository_source_files(repo, source_root))


def self_test(config: dict) -> list[str]:
    errors: list[str] = []
    for rule in config["include_rules"]:
        sources = include_fixture_sources(rule)
        positive = normalize(rule["positive_target"])
        negative_entries = include_fixture_negative_entries(rule)
        negatives = [target for target, _, _ in negative_entries]
        for source in sources:
            if edge_violates(rule, source, positive):
                errors.append(f"{rule['id']}: positive fixture was rejected for {source}")
            for negative in negatives:
                if not edge_violates(rule, source, negative):
                    errors.append(f"{rule['id']}: negative fixture was accepted for {source} -> {negative}")
        if not negatives:
            errors.append(f"{rule['id']}: restrictive rule has no negative fixture")
            continue

        # Exercise the real parser and resolver for the complete fixture
        # matrix. Forbidden edges deliberately use angle brackets so
        # repository-local angle includes cannot bypass a quoted-include rule.
        with tempfile.TemporaryDirectory(prefix="skore_dependency_fixture_") as fixture_dir:
            repo = Path(fixture_dir)
            source_root = normalize(config["source_root"])
            positive_path = repo / source_root / positive
            positive_path.parent.mkdir(parents=True, exist_ok=True)
            positive_path.touch()
            for negative in negatives:
                negative_path = repo / source_root / negative
                negative_path.parent.mkdir(parents=True, exist_ok=True)
                negative_path.touch()

            tracked_sources: list[str] = []
            for source in sources:
                tracked_source = normalize(f"{source_root}/{source}")
                tracked_sources.append(tracked_source)
                source_path = repo / tracked_source
                source_path.parent.mkdir(parents=True, exist_ok=True)
                include_lines = [f'#include "{positive}"']
                for _, include, use_angles in negative_entries:
                    include_lines.append(f"#include <{include}>" if use_angles else f'#include "{include}"')
                source_path.write_text("\n".join(include_lines) + "\n", encoding="utf-8")

            fixture_findings = scan_include_files(repo, source_root, [rule], tracked_sources)
            expected_edges = {(source, negative) for source in sources for negative in negatives}
            actual_edges = {(finding.source, finding.target) for finding in fixture_findings}
            if len(fixture_findings) != len(expected_edges) or actual_edges != expected_edges:
                errors.append(f"{rule['id']}: end-to-end include fixture did not reject the full edge matrix")

    for rule in config.get("content_rules", []):
        literals = set(rule.get("forbidden_literals", []))
        if not literals:
            errors.append(f"{rule['id']}: restrictive content rule has no forbidden literal")
            continue

        positive = rule["positive_fixture"]
        positive_source = normalize(positive["source"])
        if not source_matches(rule, positive_source):
            errors.append(f"{rule['id']}: positive content fixture is outside the governed scope")
        elif evaluate_content_rule(rule, positive_source, positive["text"]):
            errors.append(f"{rule['id']}: positive content fixture was rejected")

        negative_fixtures = rule.get("negative_fixtures", [])
        covered_literals = {fixture["literal"] for fixture in negative_fixtures}
        if covered_literals != literals:
            errors.append(f"{rule['id']}: negative content fixtures do not cover every forbidden literal")
            continue

        with tempfile.TemporaryDirectory(prefix="skore_dependency_content_fixture_") as fixture_dir:
            repo = Path(fixture_dir)
            source_root = normalize(config["source_root"])
            tracked_sources: list[str] = []
            expected_findings: set[tuple[str, str]] = set()

            fixture_rows = [positive, *negative_fixtures]
            for fixture in fixture_rows:
                source = normalize(fixture["source"])
                tracked_source = normalize(f"{source_root}/{source}")
                tracked_sources.append(tracked_source)
                source_path = repo / tracked_source
                source_path.parent.mkdir(parents=True, exist_ok=True)
                source_path.write_text(fixture["text"] + "\n", encoding="utf-8")
                if fixture is not positive:
                    expected_findings.add((source, fixture["literal"]))

            fixture_findings = scan_content_files(repo, source_root, [rule], tracked_sources)
            actual_findings = {(finding.source, finding.target) for finding in fixture_findings}
            if len(fixture_findings) != len(expected_findings) or actual_findings != expected_findings:
                errors.append(f"{rule['id']}: end-to-end content fixture did not reject the retired vocabulary")

    for rule in config["project_rules"]:
        required = rule["required_project"]
        if rule["positive_projects"] != [required]:
            errors.append(f"{rule['id']}: positive project fixture must contain only {required}")
        if required in rule["negative_projects"] or not any(
            project in rule.get("forbidden_projects", []) for project in rule["negative_projects"]
        ):
            errors.append(f"{rule['id']}: negative project fixture does not exercise a forbidden owner")
            continue
        fixture_path = normalize(f"{rule['path_prefix']}Fixture{rule['suffixes'][0]}")
        project_names = {required, *rule.get("forbidden_projects", [])}
        positive_membership = {
            project: ({fixture_path} if project in rule["positive_projects"] else set())
            for project in project_names
        }
        if evaluate_project_rule(rule, {fixture_path}, positive_membership):
            errors.append(f"{rule['id']}: positive project fixture was rejected")
        negative_membership = {
            project: ({fixture_path} if project in rule["negative_projects"] else set())
            for project in project_names
        }
        if not evaluate_project_rule(rule, {fixture_path}, negative_membership):
            errors.append(f"{rule['id']}: negative project fixture was accepted")
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
        negative_fixture_count = sum(
            len(include_fixture_sources(rule)) * len(include_fixture_negative_targets(rule))
            for rule in config["include_rules"]
        )
        content_fixture_count = sum(
            len(rule.get("negative_fixtures", []))
            for rule in config.get("content_rules", [])
        )
        print(
            f"SELF_TEST_PASS: {len(config['include_rules'])} include rules with "
            f"{negative_fixture_count} negative edge fixtures and "
            f"{len(config.get('content_rules', []))} content rules with "
            f"{content_fixture_count} negative content fixtures and "
            f"{len(config['project_rules'])} project-rule fixtures passed"
        )
        return 0

    findings = scan_includes(repo, config["source_root"], config["include_rules"])
    findings.extend(scan_content(repo, config["source_root"], config.get("content_rules", [])))
    findings.extend(scan_project_rules(repo, config["project_rules"]))
    for finding in findings:
        print(
            f"ERROR[{finding.rule_id}]: {finding.source} -> {finding.target}: {finding.detail}",
            file=sys.stderr,
        )
    print(
        f"Dependency graph summary: include_rules={len(config['include_rules'])} "
        f"content_rules={len(config.get('content_rules', []))} "
        f"project_rules={len(config['project_rules'])} findings={len(findings)}"
    )
    if findings:
        return 1
    print("PASS: dependency graph and project ownership are valid.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
