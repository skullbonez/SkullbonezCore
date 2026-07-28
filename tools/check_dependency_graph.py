#!/usr/bin/env python3
#
# File: tools/check_dependency_graph.py
# Purpose:
#   Enforce physical include direction, retired ownership vocabulary,
#   single-project source ownership, and the generated human proof's freshness.
#
# Summary:
#   Loads data-only package and content rules, resolves live repository include
#   edges, scans bounded source scopes for explicitly retired concept names,
#   checks Visual Studio project membership, and renders the same rules into a
#   marked AGENTS.md block. The same evaluators run embedded positive/negative
#   fixtures so new ownership rules require data, not checker branches.
#
# Glossary:
#   Physical edge: Resolved quoted or angle-bracket include from one tracked or
#     untracked live source file to another path below SkullbonezSource.
#   Allow rule: Runtime-package row limiting only edges whose target is inside
#     the Runtime scope.
#   Deny rule: Source/target prefix pair that must never form an include edge.
#   Content rule: Bounded deletion check for named retired vocabulary. It is
#     not a general word census or frozen occurrence budget.
#   Generated proof: Deterministic Markdown projection whose prefix and exact
#     file columns preserve the checker's path-matching semantics.
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
#   - Generated-proof writes replace one ordered marker pair and preserve every
#     byte outside it; malformed marker topology fails closed.
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
PROOF_START_MARKER = "<!-- DEPENDENCY_PROOF_START -->"
PROOF_END_MARKER = "<!-- DEPENDENCY_PROOF_END -->"


@dataclass(frozen=True)
class Finding:
    rule_id: str
    source: str
    target: str
    detail: str


class ProofBlockError(ValueError):
    pass


def markdown_cell(value: str) -> str:
    """Escape rule-controlled text for one deterministic Markdown table cell."""
    text = str(value).replace("\r\n", "\n").replace("\r", "\n")
    text = text.replace("&", "&amp;")
    text = text.replace("|", "&#124;")
    text = text.replace("`", "&#96;")
    text = text.replace("<", "&lt;").replace(">", "&gt;")
    return text.replace("\n", "&#10;")


def markdown_values(values: list[str], *, normalize_paths: bool = True) -> str:
    canonical = [normalize(value) if normalize_paths else str(value) for value in values]
    if not canonical:
        return "(none)"
    return markdown_cell(", ".join(sorted(canonical)))


def source_projection(rule: dict) -> tuple[str, str]:
    prefixes = rule.get("source_prefixes", [])
    files = rule.get("source_files", [])
    parts: list[str] = []
    kinds: list[str] = []
    if prefixes:
        parts.append(markdown_values(prefixes))
        kinds.append("prefix")
    if files:
        parts.append(markdown_values(files))
        kinds.append("exact file")
    return "; ".join(parts) or "(none)", " + ".join(kinds) or "(none)"


def markdown_table(headers: list[str], rows: list[list[str]]) -> list[str]:
    lines = [
        "| " + " | ".join(headers) + " |",
        "|" + "|".join("---" for _ in headers) + "|",
    ]
    lines.extend("| " + " | ".join(row) + " |" for row in rows)
    return lines


def render_dependency_proof(config: dict) -> str:
    """Project rule data to the only mechanically maintained AGENTS.md block."""
    include_rules = sorted(config["include_rules"], key=lambda rule: str(rule["id"]))
    runtime_rules = [
        rule
        for rule in include_rules
        if any(matches_prefix(prefix, "Runtime") for prefix in rule.get("source_prefixes", []))
        or any(matches_prefix(path, "Runtime") for path in rule.get("source_files", []))
    ]
    broad_rules = [rule for rule in include_rules if rule not in runtime_rules]

    broad_rows: list[list[str]] = []
    for rule in broad_rules:
        sources, source_kind = source_projection(rule)
        broad_rows.append(
            [
                markdown_cell(rule["id"]),
                sources,
                source_kind,
                markdown_cell(rule["mode"]),
                markdown_values([rule["target_scope"]] if rule.get("target_scope") else []),
                markdown_values(rule.get("target_prefixes", [])),
                markdown_values(rule.get("allowed_target_prefixes", [])),
                markdown_values(rule.get("allowed_target_files", [])),
            ]
        )

    runtime_rows: list[list[str]] = []
    for rule in runtime_rules:
        sources, source_kind = source_projection(rule)
        policy = (
            f"closed allow inside {markdown_cell(rule['target_scope'])}"
            if rule["mode"] == "allow"
            else "deny matching target prefixes"
        )
        runtime_rows.append(
            [
                sources,
                source_kind,
                policy,
                markdown_values(rule.get("allowed_target_prefixes", [])),
                markdown_values(rule.get("allowed_target_files", [])),
                markdown_values(rule.get("target_prefixes", [])),
            ]
        )

    content_rows: list[list[str]] = []
    for rule in sorted(config.get("content_rules", []), key=lambda item: str(item["id"])):
        sources, source_kind = source_projection(rule)
        content_rows.append(
            [
                markdown_cell(rule["id"]),
                sources,
                source_kind,
                markdown_values(rule.get("forbidden_literals", []), normalize_paths=False),
            ]
        )

    project_rows: list[list[str]] = []
    for rule in sorted(config.get("project_rules", []), key=lambda item: str(item["id"])):
        project_rows.append(
            [
                markdown_cell(rule["id"]),
                markdown_cell(normalize(rule["path_prefix"])),
                markdown_values(rule.get("suffixes", []), normalize_paths=False),
                markdown_cell(rule["required_project"]),
                markdown_values(rule.get("forbidden_projects", []), normalize_paths=False),
            ]
        )

    lines = [
        PROOF_START_MARKER,
        "<!-- Generated by tools/check_dependency_graph.py --write-proof AGENTS.md. Do not edit this block. -->",
        "",
        "### Generated Dependency Proof",
        "",
        "This is a deterministic projection of `tools/dependency_graph_rules.json`.",
        "A prefix matches the named normalized path and every descendant; an exact",
        "file matches only that normalized file. An allow row is closed-world only",
        "inside its target scope. Applicable broad deny rows still govern other",
        "engine-layer targets.",
        "",
        "#### Broad And Boundary Include Rules",
        "",
        *markdown_table(
            [
                "Rule",
                "Source",
                "Source kind",
                "Mode",
                "Target scope",
                "Denied prefixes",
                "Allowed prefixes",
                "Allowed exact files",
            ],
            broad_rows,
        ),
        "",
        "#### Runtime Package Rules",
        "",
        *markdown_table(
            [
                "Source",
                "Source kind",
                "Policy",
                "Allowed target prefixes",
                "Allowed exact target files",
                "Denied target prefixes",
            ],
            runtime_rows,
        ),
        "",
        "#### Content Rules",
        "",
        *markdown_table(
            ["Rule", "Source", "Source kind", "Forbidden exact literals"],
            content_rows,
        ),
        "",
        "#### Project Ownership Rules",
        "",
        *markdown_table(
            ["Rule", "Path prefix", "Suffixes", "Required project", "Forbidden projects"],
            project_rows,
        ),
        "",
        "#### Executable Proof",
        "",
        "The checker, not a second regular-expression parser, evaluates resolved",
        "repository edges and verifies this block before repository validation:",
        "",
        "```powershell",
        "python tools/check_dependency_graph.py --check-proof AGENTS.md",
        "python tools/check_dependency_graph.py --repo .",
        "```",
        "",
        "Residual scanner limits: macro-expanded include operands and",
        "backslash-continued include directives are not parsed. Quoted and",
        "angle-bracket operands are both recognized, but the textual resolver uses",
        "one local-first search order rather than reproducing the compiler's",
        "different quoted-versus-angle search semantics.",
        PROOF_END_MARKER,
    ]
    return "\n".join(lines)


def proof_block_bounds(document: bytes) -> tuple[int, int]:
    start_marker = PROOF_START_MARKER.encode("ascii")
    end_marker = PROOF_END_MARKER.encode("ascii")
    start_count = document.count(start_marker)
    end_count = document.count(end_marker)
    if start_count != 1 or end_count != 1:
        raise ProofBlockError(
            f"expected exactly one proof marker pair; starts={start_count} ends={end_count}"
        )
    start = document.find(start_marker)
    end = document.find(end_marker)
    if end < start:
        raise ProofBlockError("proof markers are reversed")
    return start, end + len(end_marker)


def replace_proof_block(document: bytes, rendered: str) -> bytes:
    start, end = proof_block_bounds(document)
    return document[:start] + rendered.encode("utf-8") + document[end:]


def proof_is_current(document: bytes, rendered: str) -> bool:
    start, end = proof_block_bounds(document)
    return document[start:end] == rendered.encode("utf-8")


def proof_self_test(config: dict) -> list[str]:
    errors: list[str] = []
    rendered = render_dependency_proof(config)
    if rendered != render_dependency_proof(config):
        errors.append("generated proof changed across two unchanged renders")
    if rendered.count(PROOF_START_MARKER) != 1 or rendered.count(PROOF_END_MARKER) != 1:
        errors.append("generated proof does not contain exactly one ordered marker pair")

    escaped = markdown_cell("pipe|tick`angle<value>&line\r\nbreak")
    if escaped != "pipe&#124;tick&#96;angle&lt;value&gt;&amp;line&#10;break":
        errors.append("Markdown escaping did not canonicalize rule-controlled text")

    malformed_documents = {
        "missing": b"no generated proof",
        "duplicate start": (
            PROOF_START_MARKER + PROOF_START_MARKER + PROOF_END_MARKER
        ).encode("ascii"),
        "duplicate end": (
            PROOF_START_MARKER + PROOF_END_MARKER + PROOF_END_MARKER
        ).encode("ascii"),
        "reversed": (PROOF_END_MARKER + PROOF_START_MARKER).encode("ascii"),
    }
    for name, document in malformed_documents.items():
        try:
            proof_block_bounds(document)
        except ProofBlockError:
            pass
        else:
            errors.append(f"generated proof accepted {name} markers")

    prefix = b"\x00outside-before\r\n"
    suffix = b"\r\noutside-after\xff"
    stale = (
        prefix
        + PROOF_START_MARKER.encode("ascii")
        + b"\nstale\n"
        + PROOF_END_MARKER.encode("ascii")
        + suffix
    )
    rewritten = replace_proof_block(stale, rendered)
    if not rewritten.startswith(prefix) or not rewritten.endswith(suffix):
        errors.append("generated proof write changed bytes outside the marker pair")
    elif not proof_is_current(rewritten, rendered):
        errors.append("generated proof write did not produce a current block")
    if proof_is_current(stale, rendered):
        errors.append("generated proof freshness check accepted stale content")
    return errors


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
    errors = proof_self_test(config)
    for rule in config["include_rules"]:
        sources = include_fixture_sources(rule)
        positive_targets = [normalize(rule["positive_target"])]
        positive_targets.extend(
            normalize(target) for target in rule.get("allowed_target_files", [])
        )
        positive_targets = list(dict.fromkeys(positive_targets))
        negative_entries = include_fixture_negative_entries(rule)
        negatives = [target for target, _, _ in negative_entries]
        for source in sources:
            for positive in positive_targets:
                if edge_violates(rule, source, positive):
                    errors.append(
                        f"{rule['id']}: positive fixture was rejected for {source} -> {positive}"
                    )
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
            for positive in positive_targets:
                positive_path = repo / source_root / positive
                positive_path.parent.mkdir(parents=True, exist_ok=True)
                positive_path.touch()
            for target, include, _ in negative_entries:
                # Relative-include fixtures need a physical local candidate.
                # Rooted spellings intentionally exercise the resolver's
                # non-existent source-root fallback, including the file-shaped
                # RuntimeFrameViews.h/Child.h pseudo-descendant.
                if not include.startswith("."):
                    continue
                negative_path = repo / source_root / target
                negative_path.parent.mkdir(parents=True, exist_ok=True)
                negative_path.touch()

            tracked_sources: list[str] = []
            for source in sources:
                tracked_source = normalize(f"{source_root}/{source}")
                tracked_sources.append(tracked_source)
                source_path = repo / tracked_source
                source_path.parent.mkdir(parents=True, exist_ok=True)
                include_lines = [
                    f'#include "{positive}"'
                    for positive in positive_targets
                ]
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
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--self-test", action="store_true")
    action.add_argument("--render-proof", action="store_true")
    action.add_argument("--check-proof", type=Path, metavar="MARKDOWN")
    action.add_argument("--write-proof", type=Path, metavar="MARKDOWN")
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
    rendered_proof = render_dependency_proof(config)
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
            f"{len(config['project_rules'])} project-rule fixtures plus "
            "generated-proof fixtures passed"
        )
        return 0
    if args.render_proof:
        print(rendered_proof)
        return 0

    proof_path = args.check_proof or args.write_proof
    if proof_path is not None:
        proof_path = proof_path if proof_path.is_absolute() else repo / proof_path
    elif not args.self_test:
        proof_path = repo / "AGENTS.md"

    if proof_path is not None:
        try:
            proof_document = proof_path.read_bytes()
            current = proof_is_current(proof_document, rendered_proof)
        except (OSError, ProofBlockError) as error:
            print(f"PROOF_CHECK_FAIL: {proof_path}: {error}", file=sys.stderr)
            return 1

        if args.write_proof:
            rewritten = replace_proof_block(proof_document, rendered_proof)
            if rewritten != proof_document:
                proof_path.write_bytes(rewritten)
                print(f"PROOF_WRITE_PASS: updated {proof_path}")
            else:
                print(f"PROOF_WRITE_PASS: already current {proof_path}")
            return 0

        if not current:
            print(
                f"PROOF_CHECK_FAIL: {proof_path}: generated dependency proof is stale; "
                f"run --write-proof {proof_path}",
                file=sys.stderr,
            )
            return 1
        print(f"PROOF_CHECK_PASS: generated dependency proof is current in {proof_path}")
        if args.check_proof:
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
