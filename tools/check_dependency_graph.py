#!/usr/bin/env python3
#
# File: tools/check_dependency_graph.py
# Purpose:
#   Enforce physical include direction and retired ownership vocabulary, report
#   current Runtime package topology, check single-project source ownership,
#   and verify the generated human proof's freshness.
#
# Summary:
#   Loads data-only package and content rules, resolves live repository include
#   edges once, and projects Runtime edges through a ranked, closed exact-file
#   policy. A strict Runtime check ignores migration debt and fails every cycle
#   or forbidden site; the ordinary repository gate accepts only fingerprinted
#   repair-plan rows so existing separation work cannot admit a new site.
#   Bounded content/project checks and the generated AGENTS proof use the same
#   rule data, while embedded fixtures exercise every production evaluator.
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
#   Fixture matrix: Synthetic include, content, project-file, and proof-drift
#     cases that exercise the same evaluators used by the repository scan.
#   Runtime package policy: Total owner rank plus a closed exact target-header
#     projection. Rank constrains direction but never grants an edge by itself.
#   Repair-plan ruling: Temporary source/line/target/spelling-sealed evidence
#     for a current forbidden site. Strict evaluation always ignores it.
#   Runtime package report: Current adjacency, strongly connected components,
#     cycle traces, forbidden sites, and reverse composition-root sites. Counts
#     are live evidence, never frozen allowances.
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
#   - Include scanning is deliberately textual: bounded fixtures pin its macro,
#     continuation, and local-first search-order limits.
#   - Strict Runtime evaluation rejects every multi-package cycle and every
#     forbidden exact edge regardless of current repair-plan rulings.
#   - Ordinary repository evaluation accepts only exact current repair rows;
#     new, changed, shifted, and deleted sites fail closed.
#   - Runtime graph counts are current measurements, never policy budgets.
#
# Related:
#   - tools/dependency_graph_rules.json
#   - AGENTS.md

from __future__ import annotations

import argparse
import hashlib
import json
import posixpath
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path


INCLUDE_PATTERN = re.compile(
    r'^[ \t]*#[ \t]*include[ \t]*(?P<operand>"(?P<quoted>[^"]+)"|<(?P<angled>[^>]+)>)',
    re.MULTILINE,
)
SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".inl", ".hlsl"}
MSBUILD_NAMESPACE = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}
PROOF_START_MARKER = "<!-- DEPENDENCY_PROOF_START -->"
PROOF_END_MARKER = "<!-- DEPENDENCY_PROOF_END -->"
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")


@dataclass(frozen=True)
class Finding:
    rule_id: str
    source: str
    target: str
    detail: str


@dataclass(frozen=True)
class ResolvedInclude:
    source: str
    target: str
    line: int
    spelling: str


@dataclass(frozen=True)
class RuntimePackageIncludeSite:
    source_package: str
    target_package: str
    source: str
    target: str
    line: int
    spelling: str


@dataclass(frozen=True)
class RuntimePackageEdge:
    source_package: str
    target_package: str
    include_site_count: int


@dataclass(frozen=True)
class RuntimePackageGraph:
    source_files: tuple[str, ...]
    packages: tuple[str, ...]
    edges: tuple[RuntimePackageEdge, ...]
    include_sites: tuple[RuntimePackageIncludeSite, ...]
    strongly_connected_components: tuple[tuple[str, ...], ...]
    cyclic_components: tuple[tuple[str, ...], ...]
    bidirectional_pairs: tuple[tuple[str, str], ...]
    reverse_composition_root_include_sites: tuple[RuntimePackageIncludeSite, ...]


@dataclass(frozen=True)
class RuntimePolicyViolation:
    source_package: str
    target_package: str
    source: str
    target: str
    line: int
    spelling: str
    reasons: tuple[str, ...]

    @property
    def key(self) -> tuple[str, int, str]:
        return self.source, self.line, self.target


@dataclass(frozen=True)
class RuntimeCycleTrace:
    packages: tuple[str, ...]
    include_sites: tuple[RuntimePackageIncludeSite, ...]


@dataclass(frozen=True)
class RuntimeRepairRuling:
    source: str
    line: int
    target: str
    fingerprint: str
    repair_phase: str

    @property
    def key(self) -> tuple[str, int, str]:
        return self.source, self.line, self.target


@dataclass(frozen=True)
class RuntimePolicyEvaluation:
    unregistered_packages: tuple[str, ...]
    violations: tuple[RuntimePolicyViolation, ...]
    cycle_traces: tuple[RuntimeCycleTrace, ...]

    @property
    def strict_ok(self) -> bool:
        return not self.unregistered_packages and not self.violations and not self.cycle_traces


@dataclass(frozen=True)
class RuntimeRepairEvaluation:
    current: tuple[RuntimeRepairRuling, ...]
    diagnostics: tuple[Finding, ...]


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


def canonical_json_digest(value: object) -> str:
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


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
    runtime_policy = config["runtime_package_policy"]

    broad_rows: list[list[str]] = []
    for rule in include_rules:
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

    runtime_rows = [
        [
            markdown_cell(str(package["rank"])),
            markdown_cell(normalize(package["package"])),
            markdown_values(package.get("allowed_exact_target_files", [])),
        ]
        for package in sorted(
            runtime_policy["packages"], key=lambda item: (int(item["rank"]), item["package"])
        )
    ]
    exact_edge_rows = [
        [
            markdown_cell(normalize(edge["source_file"])),
            markdown_cell(normalize(edge["target_file"])),
        ]
        for edge in sorted(
            runtime_policy.get("allowed_exact_edges", []),
            key=lambda item: (normalize(item["source_file"]), normalize(item["target_file"])),
        )
    ]
    repair_policy = runtime_policy["repair_plan_policy"]
    repair_digest = canonical_json_digest(repair_policy)

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
        "Broad prefixes match the named normalized path and every descendant.",
        "Runtime package rows are different: rank constrains direction, but a",
        "cross-package include is legal only when its exact target file (or mixed",
        "exact source/target pair) is listed. Self-package includes remain legal.",
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
            ["Rank", "Source package", "Allowed exact cross-package target files"],
            runtime_rows,
        ),
        "",
        f"Composition root: `{markdown_cell(normalize(runtime_policy['composition_root']))}`.",
        "Every cross-package target must have a lower rank; rank alone never grants permission.",
        "",
        "#### Runtime Mixed Exact Edges",
        "",
        *markdown_table(["Exact source file", "Exact target file"], exact_edge_rows),
        "",
        "#### Runtime Repair-Plan Debt",
        "",
        "Current pre-separation debt is sealed in rule data by source, line, resolved",
        "target, include spelling, and policy fingerprint. The ordinary repository",
        "gate accepts only an exact current seal and reports it as repair-plan debt;",
        "a new, changed, shifted, or deleted site fails. `--check-runtime-graph`",
        "ignores every repair row and fails every forbidden site and multi-package SCC.",
        f"Canonical repair-policy SHA-256: `{repair_digest}`.",
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
        "Bounded residual-parser fixtures prove that macro-expanded include operands",
        "and backslash-continued directives are not parsed. Quoted and angle-bracket",
        "operands are both recognized, but both use one local-first textual search",
        "order rather than the compiler's different quoted-versus-angle semantics.",
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

    current_document = prefix + rendered.encode("utf-8") + suffix
    drifted_config = json.loads(json.dumps(config))
    drifted_config["include_rules"][0].setdefault("target_prefixes", []).append(
        "Runtime/PlantedProofDrift"
    )
    drifted_rendered = render_dependency_proof(drifted_config)
    if drifted_rendered == rendered:
        errors.append("planted rule-data drift did not change the generated proof")
    elif proof_is_current(current_document, drifted_rendered):
        errors.append("generated proof freshness accepted a block from pre-drift rule data")

    repair_drifted_config = json.loads(json.dumps(config))
    repair_drifted_config["runtime_package_policy"]["repair_plan_policy"]["reason"] += (
        " planted drift"
    )
    if render_dependency_proof(repair_drifted_config) == rendered:
        errors.append("planted Runtime repair-policy drift did not change the proof digest")

    escaped_config = json.loads(json.dumps(config))
    escaped_config["include_rules"][0]["id"] = "pipe|tick`angle<value>&line\nbreak"
    escaped_rendered = render_dependency_proof(escaped_config)
    if "pipe&#124;tick&#96;angle&lt;value&gt;&amp;line&#10;break" not in escaped_rendered:
        errors.append("full proof render did not escape rule-controlled Markdown")
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


def scan_resolved_include_files(
    repo: Path,
    source_root: str,
    tracked_files: list[str],
) -> list[ResolvedInclude]:
    """Resolve textual include sites once for enforcement and reporting."""
    includes: list[ResolvedInclude] = []
    for tracked in tracked_files:
        path = repo / tracked
        source = normalize(Path(tracked).relative_to(source_root).as_posix())
        text = path.read_text(encoding="utf-8-sig", errors="replace")
        for match in INCLUDE_PATTERN.finditer(text):
            include = match.group("quoted") or match.group("angled")
            target = resolve_include(repo, source_root, tracked, include)
            if target is None:
                continue
            includes.append(
                ResolvedInclude(
                    source=source,
                    target=target,
                    line=text.count("\n", 0, match.start()) + 1,
                    spelling=match.group("operand"),
                )
            )
    return includes


def scan_include_files(
    repo: Path,
    source_root: str,
    rules: list[dict],
    tracked_files: list[str],
) -> list[Finding]:
    return evaluate_resolved_includes(
        rules, scan_resolved_include_files(repo, source_root, tracked_files)
    )


def evaluate_resolved_includes(
    rules: list[dict], resolved_includes: list[ResolvedInclude]
) -> list[Finding]:
    findings: list[Finding] = []
    for include in resolved_includes:
        findings.extend(evaluate_edge(rules, include.source, include.target))
    return findings


def scan_includes(repo: Path, source_root: str, rules: list[dict]) -> list[Finding]:
    return scan_include_files(repo, source_root, rules, repository_source_files(repo, source_root))


def runtime_package(path: str, scope: str) -> str | None:
    """Return a physical package or exact top-level file inside a configured scope."""
    normalized = normalize(path)
    normalized_scope = normalize(scope).rstrip("/")
    parts = normalized.split("/")
    scope_parts = normalized_scope.split("/")
    if len(parts) <= len(scope_parts) or parts[: len(scope_parts)] != scope_parts:
        return None
    if len(parts) == len(scope_parts) + 1:
        return normalized
    return "/".join(parts[: len(scope_parts) + 1])


def strongly_connected_components(
    packages: tuple[str, ...],
    adjacency: dict[str, set[str]],
) -> tuple[tuple[str, ...], ...]:
    """Return a deterministic Tarjan partition of the package graph."""
    next_index = 0
    indices: dict[str, int] = {}
    low_links: dict[str, int] = {}
    stack: list[str] = []
    on_stack: set[str] = set()
    components: list[tuple[str, ...]] = []

    def visit(package: str) -> None:
        nonlocal next_index
        indices[package] = next_index
        low_links[package] = next_index
        next_index += 1
        stack.append(package)
        on_stack.add(package)

        for target in sorted(adjacency.get(package, set())):
            if target not in indices:
                visit(target)
                low_links[package] = min(low_links[package], low_links[target])
            elif target in on_stack:
                low_links[package] = min(low_links[package], indices[target])

        if low_links[package] != indices[package]:
            return

        component: list[str] = []
        while True:
            member = stack.pop()
            on_stack.remove(member)
            component.append(member)
            if member == package:
                break
        components.append(tuple(sorted(component)))

    for package in packages:
        if package not in indices:
            visit(package)
    return tuple(sorted(components))


def build_runtime_package_graph(
    source_root: str,
    tracked_files: list[str],
    resolved_includes: list[ResolvedInclude],
    scope: str,
    composition_root: str,
) -> RuntimePackageGraph:
    """Project resolved file edges onto current physical Runtime owners."""
    runtime_source_files: list[str] = []
    packages: set[str] = set()
    for tracked in tracked_files:
        source = normalize(Path(tracked).relative_to(source_root).as_posix())
        package = runtime_package(source, scope)
        if package is None:
            continue
        runtime_source_files.append(source)
        packages.add(package)

    include_sites: list[RuntimePackageIncludeSite] = []
    edge_counts: dict[tuple[str, str], int] = {}
    for include in resolved_includes:
        source_package = runtime_package(include.source, scope)
        target_package = runtime_package(include.target, scope)
        if source_package is None or target_package is None or source_package == target_package:
            continue

        site = RuntimePackageIncludeSite(
            source_package=source_package,
            target_package=target_package,
            source=include.source,
            target=include.target,
            line=include.line,
            spelling=include.spelling,
        )
        include_sites.append(site)
        packages.update((source_package, target_package))
        key = (source_package, target_package)
        edge_counts[key] = edge_counts.get(key, 0) + 1

    ordered_packages = tuple(sorted(packages))
    edges = tuple(
        RuntimePackageEdge(source, target, count)
        for (source, target), count in sorted(edge_counts.items())
    )
    ordered_sites = tuple(
        sorted(
            include_sites,
            key=lambda site: (
                site.source_package,
                site.target_package,
                site.source,
                site.line,
                site.target,
            ),
        )
    )
    adjacency = {package: set() for package in ordered_packages}
    for edge in edges:
        adjacency[edge.source_package].add(edge.target_package)

    components = strongly_connected_components(ordered_packages, adjacency)
    cyclic_components = tuple(component for component in components if len(component) > 1)
    edge_pairs = set(edge_counts)
    bidirectional_pairs = tuple(
        sorted(
            (source, target)
            for source, target in edge_pairs
            if source < target and (target, source) in edge_pairs
        )
    )
    reverse_composition_root_sites = tuple(
        site
        for site in ordered_sites
        if site.source_package != composition_root and site.target_package == composition_root
    )

    # Invariant: these measurements describe the live tree. Enforcement remains
    # data-owned by dependency_graph_rules.json; no current count becomes a gate.
    return RuntimePackageGraph(
        source_files=tuple(sorted(runtime_source_files)),
        packages=ordered_packages,
        edges=edges,
        include_sites=ordered_sites,
        strongly_connected_components=components,
        cyclic_components=cyclic_components,
        bidirectional_pairs=bidirectional_pairs,
        reverse_composition_root_include_sites=reverse_composition_root_sites,
    )


def scan_runtime_package_graph(
    repo: Path, source_root: str, policy: dict
) -> RuntimePackageGraph:
    """Build the Runtime report from the same repository include resolution as enforcement."""
    tracked_files = repository_source_files(repo, source_root)
    resolved_includes = scan_resolved_include_files(repo, source_root, tracked_files)
    return build_runtime_package_graph(
        source_root,
        tracked_files,
        resolved_includes,
        policy["scope"],
        policy["composition_root"],
    )


def validate_runtime_policy(policy: dict) -> list[str]:
    """Validate the data-owned total rank and closed exact-file projection."""
    errors: list[str] = []
    scope = policy.get("scope")
    composition_root = policy.get("composition_root")
    packages = policy.get("packages")
    exact_edges = policy.get("allowed_exact_edges", [])
    repair_policy = policy.get("repair_plan_policy")
    if not isinstance(scope, str) or not scope.strip():
        return ["Runtime package policy scope must be a non-empty string"]
    if not isinstance(composition_root, str) or not composition_root.strip():
        errors.append("Runtime package policy composition_root must be a non-empty string")
    if not isinstance(packages, list) or not packages:
        errors.append("Runtime package policy packages must be a non-empty list")
        return errors

    package_rows: dict[str, dict] = {}
    ranks: dict[int, str] = {}
    for row in packages:
        if not isinstance(row, dict):
            errors.append("Runtime package row must be an object")
            continue
        package = row.get("package")
        rank = row.get("rank")
        targets = row.get("allowed_exact_target_files")
        if not isinstance(package, str) or not package.strip():
            errors.append("Runtime package row path must be a non-empty string")
            continue
        package = normalize(package)
        if runtime_package(package, scope) != package:
            errors.append(f"Runtime package row is not one physical package: {package}")
        if package in package_rows:
            errors.append(f"duplicate Runtime package row: {package}")
        package_rows[package] = row
        if not isinstance(rank, int) or isinstance(rank, bool) or rank < 0:
            errors.append(f"Runtime package {package} rank must be a non-negative integer")
        elif rank in ranks:
            errors.append(f"duplicate Runtime rank {rank}: {ranks[rank]} and {package}")
        else:
            ranks[rank] = package
        if not isinstance(targets, list):
            errors.append(f"Runtime package {package} exact targets must be a list")

    expected_ranks = set(range(len(package_rows)))
    if set(ranks) != expected_ranks:
        errors.append(
            "Runtime ranks must form one total contiguous order; "
            f"actual={sorted(ranks)} expected={sorted(expected_ranks)}"
        )
    normalized_root = normalize(composition_root) if isinstance(composition_root, str) else ""
    if normalized_root not in package_rows:
        errors.append(f"Runtime composition root is not registered: {normalized_root}")
    elif ranks and package_rows[normalized_root].get("rank") != max(ranks):
        errors.append("Runtime composition root must own the highest rank")

    for package, row in package_rows.items():
        targets = row.get("allowed_exact_target_files", [])
        normalized_targets = [normalize(target) for target in targets if isinstance(target, str)]
        if len(normalized_targets) != len(targets):
            errors.append(f"Runtime package {package} exact targets must all be strings")
        if len(set(normalized_targets)) != len(normalized_targets):
            errors.append(f"Runtime package {package} contains duplicate exact targets")
        for target in normalized_targets:
            target_package = runtime_package(target, scope)
            if target_package not in package_rows:
                errors.append(f"Runtime exact target is in an unregistered package: {target}")
                continue
            if target_package == package:
                errors.append(f"Runtime self-package target must not be listed explicitly: {target}")
                continue
            source_rank = row.get("rank")
            target_rank = package_rows[target_package].get("rank")
            if isinstance(source_rank, int) and isinstance(target_rank, int) and target_rank >= source_rank:
                errors.append(f"Runtime exact target is not rank-downward: {package} -> {target}")

    if not isinstance(exact_edges, list):
        errors.append("Runtime allowed_exact_edges must be a list")
        exact_edges = []
    exact_edge_keys: set[tuple[str, str]] = set()
    for row in exact_edges:
        if not isinstance(row, dict):
            errors.append("Runtime exact edge row must be an object")
            continue
        source = row.get("source_file")
        target = row.get("target_file")
        if not isinstance(source, str) or not isinstance(target, str):
            errors.append("Runtime exact edge source_file and target_file must be strings")
            continue
        source = normalize(source)
        target = normalize(target)
        key = (source, target)
        if key in exact_edge_keys:
            errors.append(f"duplicate Runtime exact edge: {source} -> {target}")
        exact_edge_keys.add(key)
        source_package = runtime_package(source, scope)
        target_package = runtime_package(target, scope)
        if source_package not in package_rows or target_package not in package_rows:
            errors.append(f"Runtime exact edge uses an unregistered package: {source} -> {target}")
            continue
        source_rank = package_rows[source_package].get("rank")
        target_rank = package_rows[target_package].get("rank")
        if source_package == target_package or (
            isinstance(source_rank, int)
            and isinstance(target_rank, int)
            and target_rank >= source_rank
        ):
            errors.append(f"Runtime exact edge is not cross-package rank-downward: {source} -> {target}")

    if not isinstance(repair_policy, dict):
        errors.append("Runtime repair_plan_policy must be an object")
        return errors
    for field in ("owner", "plan", "reason"):
        value = repair_policy.get(field)
        if not isinstance(value, str) or not value.strip():
            errors.append(f"Runtime repair_plan_policy {field} must be a non-empty string")
    rulings = repair_policy.get("rulings")
    if not isinstance(rulings, list):
        errors.append("Runtime repair_plan_policy rulings must be a list")
        return errors
    ruling_keys: set[tuple[str, int, str]] = set()
    for row in rulings:
        if not isinstance(row, dict):
            errors.append("Runtime repair-plan ruling must be an object")
            continue
        source = row.get("source")
        line = row.get("line")
        target = row.get("target")
        fingerprint = row.get("fingerprint")
        phase = row.get("repair_phase")
        if not isinstance(source, str) or not source.strip():
            errors.append("Runtime repair-plan source must be a non-empty string")
            continue
        if not isinstance(line, int) or isinstance(line, bool) or line <= 0:
            errors.append(f"Runtime repair-plan line must be positive for {source}")
            continue
        if not isinstance(target, str) or not target.strip():
            errors.append(f"Runtime repair-plan target must be non-empty for {source}:{line}")
            continue
        key = (normalize(source), line, normalize(target))
        if key in ruling_keys:
            errors.append(f"duplicate Runtime repair-plan ruling: {key}")
        ruling_keys.add(key)
        if not isinstance(fingerprint, str) or not SHA256_PATTERN.fullmatch(fingerprint):
            errors.append(f"Runtime repair-plan fingerprint must be lowercase SHA-256: {key}")
        if not isinstance(phase, str) or not phase.strip():
            errors.append(f"Runtime repair-plan phase must be non-empty: {key}")
    return errors


def runtime_policy_rows(policy: dict) -> dict[str, dict]:
    return {normalize(row["package"]): row for row in policy["packages"]}


def runtime_cycle_trace(
    component: tuple[str, ...], report: RuntimePackageGraph
) -> RuntimeCycleTrace:
    """Choose one deterministic concrete include walk that closes an SCC."""
    members = set(component)
    sites_by_edge: dict[tuple[str, str], list[RuntimePackageIncludeSite]] = {}
    for site in report.include_sites:
        if site.source_package in members and site.target_package in members:
            sites_by_edge.setdefault((site.source_package, site.target_package), []).append(site)
    adjacency = {
        package: sorted(
            target for source, target in sites_by_edge if source == package
        )
        for package in component
    }

    def find_from(start: str, current: str, path: list[str]) -> list[str] | None:
        for target in adjacency[current]:
            if target == start:
                return [*path, start]
            if target in path:
                continue
            found = find_from(start, target, [*path, target])
            if found is not None:
                return found
        return None

    package_walk: list[str] | None = None
    for start in component:
        package_walk = find_from(start, start, [start])
        if package_walk is not None:
            break
    if package_walk is None:
        raise ValueError(f"cyclic component has no concrete cycle trace: {component}")
    include_sites = tuple(
        sorted(
            sites_by_edge[(source, target)],
            key=lambda site: (site.source, site.line, site.target, site.spelling),
        )[0]
        for source, target in zip(package_walk, package_walk[1:])
    )
    return RuntimeCycleTrace(tuple(package_walk), include_sites)


def evaluate_runtime_policy(
    report: RuntimePackageGraph, policy: dict
) -> RuntimePolicyEvaluation:
    """Evaluate rank, exact-file closure, package registration, and SCCs."""
    packages = runtime_policy_rows(policy)
    allowed_exact_edges = {
        (normalize(row["source_file"]), normalize(row["target_file"]))
        for row in policy.get("allowed_exact_edges", [])
    }
    unregistered = tuple(sorted(set(report.packages) - set(packages)))
    violations: list[RuntimePolicyViolation] = []
    for site in report.include_sites:
        reasons: list[str] = []
        source_row = packages.get(site.source_package)
        target_row = packages.get(site.target_package)
        if source_row is None:
            reasons.append("source package is not registered")
        if target_row is None:
            reasons.append("target package is not registered")
        if source_row is not None and target_row is not None:
            if int(target_row["rank"]) >= int(source_row["rank"]):
                reasons.append("target rank is not lower than source rank")
            allowed_targets = {
                normalize(target)
                for target in source_row.get("allowed_exact_target_files", [])
            }
            if (
                site.target not in allowed_targets
                and (site.source, site.target) not in allowed_exact_edges
            ):
                reasons.append("edge is absent from the closed exact-file projection")
        if reasons:
            violations.append(
                RuntimePolicyViolation(
                    source_package=site.source_package,
                    target_package=site.target_package,
                    source=site.source,
                    target=site.target,
                    line=site.line,
                    spelling=site.spelling,
                    reasons=tuple(reasons),
                )
            )
    cycle_traces = tuple(
        runtime_cycle_trace(component, report) for component in report.cyclic_components
    )
    return RuntimePolicyEvaluation(
        unregistered_packages=unregistered,
        violations=tuple(violations),
        cycle_traces=cycle_traces,
    )


def runtime_violation_fingerprint(violation: RuntimePolicyViolation) -> str:
    return canonical_json_digest(
        {
            "source_package": violation.source_package,
            "target_package": violation.target_package,
            "source": violation.source,
            "line": violation.line,
            "target": violation.target,
            "spelling": violation.spelling,
            "reasons": list(violation.reasons),
        }
    )


def runtime_repair_rulings(policy: dict) -> dict[tuple[str, int, str], RuntimeRepairRuling]:
    rulings: dict[tuple[str, int, str], RuntimeRepairRuling] = {}
    for row in policy["repair_plan_policy"]["rulings"]:
        ruling = RuntimeRepairRuling(
            source=normalize(row["source"]),
            line=int(row["line"]),
            target=normalize(row["target"]),
            fingerprint=row["fingerprint"],
            repair_phase=row["repair_phase"].strip(),
        )
        rulings[ruling.key] = ruling
    return rulings


def reconcile_runtime_repairs(
    evaluation: RuntimePolicyEvaluation, policy: dict
) -> RuntimeRepairEvaluation:
    """Admit only exact current repair debt; stale and new sites fail closed."""
    rulings = runtime_repair_rulings(policy)
    current: list[RuntimeRepairRuling] = []
    diagnostics: list[Finding] = []
    violation_keys: set[tuple[str, int, str]] = set()
    for violation in evaluation.violations:
        violation_keys.add(violation.key)
        actual = runtime_violation_fingerprint(violation)
        ruling = rulings.get(violation.key)
        source_site = f"{violation.source}:{violation.line}"
        if ruling is None:
            diagnostics.append(
                Finding(
                    "runtime_package_policy",
                    source_site,
                    violation.target,
                    f"unruled forbidden Runtime edge fingerprint={actual}",
                )
            )
        elif ruling.fingerprint != actual:
            diagnostics.append(
                Finding(
                    "runtime_package_policy",
                    source_site,
                    violation.target,
                    "changed Runtime repair-plan fingerprint "
                    f"expected={ruling.fingerprint} actual={actual}",
                )
            )
        else:
            current.append(ruling)
    for key in sorted(set(rulings) - violation_keys):
        ruling = rulings[key]
        diagnostics.append(
            Finding(
                "runtime_package_policy",
                f"{ruling.source}:{ruling.line}",
                ruling.target,
                "stale Runtime repair-plan ruling",
            )
        )
    for package in evaluation.unregistered_packages:
        diagnostics.append(
            Finding(
                "runtime_package_policy",
                package,
                policy["scope"],
                "unregistered Runtime package",
            )
        )
    return RuntimeRepairEvaluation(tuple(current), tuple(diagnostics))


def runtime_site_document(site: RuntimePackageIncludeSite) -> dict[str, object]:
    return {
        "source_package": site.source_package,
        "target_package": site.target_package,
        "source": site.source,
        "line": site.line,
        "target": site.target,
        "include_spelling": site.spelling,
    }


def runtime_package_graph_document(
    report: RuntimePackageGraph,
    policy: dict,
    evaluation: RuntimePolicyEvaluation,
    repairs: RuntimeRepairEvaluation,
) -> dict[str, object]:
    """Serialize stable graph, strict-policy, and exact repair-debt evidence."""
    edges_by_source: dict[str, list[RuntimePackageEdge]] = {
        package: [] for package in report.packages
    }
    for edge in report.edges:
        edges_by_source[edge.source_package].append(edge)

    return {
        "runtime_source_file_count": len(report.source_files),
        "package_count": len(report.packages),
        "directed_cross_package_edge_count": len(report.edges),
        "directed_cross_package_include_site_count": len(report.include_sites),
        "packages": list(report.packages),
        "runtime_policy": {
            "scope": normalize(policy["scope"]),
            "composition_root": normalize(policy["composition_root"]),
            "ranks": [
                {
                    "package": normalize(row["package"]),
                    "rank": int(row["rank"]),
                }
                for row in sorted(
                    policy["packages"],
                    key=lambda row: (int(row["rank"]), normalize(row["package"])),
                )
            ],
        },
        "adjacency": [
            {
                "source_package": package,
                "targets": [
                    {
                        "target_package": edge.target_package,
                        "include_site_count": edge.include_site_count,
                    }
                    for edge in edges_by_source[package]
                ],
            }
            for package in report.packages
        ],
        "strongly_connected_components": [
            list(component) for component in report.strongly_connected_components
        ],
        "cyclic_components": [list(component) for component in report.cyclic_components],
        "cyclic_component_traces": [
            {
                "packages": list(trace.packages),
                "include_sites": [runtime_site_document(site) for site in trace.include_sites],
            }
            for trace in evaluation.cycle_traces
        ],
        "bidirectional_pairs": [
            {"left_package": left, "right_package": right}
            for left, right in report.bidirectional_pairs
        ],
        "non_composition_root_to_composition_root_include_site_count": len(
            report.reverse_composition_root_include_sites
        ),
        "non_composition_root_to_composition_root_include_sites": [
            runtime_site_document(site)
            for site in report.reverse_composition_root_include_sites
        ],
        "unregistered_packages": list(evaluation.unregistered_packages),
        "forbidden_include_site_count": len(evaluation.violations),
        "forbidden_include_sites": [
            {
                "source_package": violation.source_package,
                "target_package": violation.target_package,
                "source": violation.source,
                "line": violation.line,
                "target": violation.target,
                "include_spelling": violation.spelling,
                "reasons": list(violation.reasons),
                "fingerprint": runtime_violation_fingerprint(violation),
            }
            for violation in evaluation.violations
        ],
        "current_repair_plan_debt": [
            {
                "source": ruling.source,
                "line": ruling.line,
                "target": ruling.target,
                "fingerprint": ruling.fingerprint,
                "repair_phase": ruling.repair_phase,
            }
            for ruling in repairs.current
        ],
        "repair_ruling_diagnostics": [
            {
                "rule": finding.rule_id,
                "source": finding.source,
                "target": finding.target,
                "detail": finding.detail,
            }
            for finding in repairs.diagnostics
        ],
        "strict_policy_ok": evaluation.strict_ok,
        "default_repository_policy_ok": not repairs.diagnostics,
    }


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


def include_parser_limit_self_test(config: dict) -> list[str]:
    """Pin the documented boundary of the textual include parser and resolver."""
    errors: list[str] = []
    source_root = normalize(config["source_root"])
    rule = {
        "id": "residual_include_parser_fixture",
        "source_prefixes": ["FixtureOwner"],
        "mode": "deny",
        "target_prefixes": ["FixtureOwner/Shared"],
    }
    fixture_cases = [
        (
            "Macro.cpp",
            '#define DEPENDENCY_FIXTURE_HEADER "Shared/Macro.h"\n'
            "#include DEPENDENCY_FIXTURE_HEADER\n",
            "Shared/Macro.h",
            False,
        ),
        (
            "Continuation.cpp",
            '#include \\\n    "Shared/Continuation.h"\n',
            "Shared/Continuation.h",
            False,
        ),
        ("Quoted.cpp", '#include "Shared/Quoted.h"\n', "Shared/Quoted.h", True),
        ("Angle.cpp", "#include <Shared/Angle.h>\n", "Shared/Angle.h", True),
    ]

    with tempfile.TemporaryDirectory(prefix="skore_dependency_parser_fixture_") as fixture_dir:
        repo = Path(fixture_dir)
        for filename, fixture_text, include, should_parse in fixture_cases:
            source = f"FixtureOwner/{filename}"
            tracked_source = normalize(f"{source_root}/{source}")
            source_path = repo / tracked_source
            source_path.parent.mkdir(parents=True, exist_ok=True)
            source_path.write_text(fixture_text, encoding="utf-8")

            local_target = normalize(f"FixtureOwner/{include}")
            local_path = repo / source_root / local_target
            local_path.parent.mkdir(parents=True, exist_ok=True)
            local_path.touch()
            rooted_path = repo / source_root / include
            rooted_path.parent.mkdir(parents=True, exist_ok=True)
            rooted_path.touch()

            findings = scan_include_files(repo, source_root, [rule], [tracked_source])
            expected = (
                [Finding(rule["id"], source, local_target, "deny package edge rejected")]
                if should_parse
                else []
            )
            if findings != expected:
                errors.append(
                    f"residual include-parser fixture {filename} returned {findings!r}; "
                    f"expected {expected!r}"
                )
    return errors


def runtime_package_graph_fixture(
    source_root: str,
    scope: str,
    composition_root: str,
    sources: dict[str, str],
) -> RuntimePackageGraph:
    """Run one synthetic Runtime tree through the production parser and resolver."""
    with tempfile.TemporaryDirectory(prefix="skore_runtime_graph_fixture_") as fixture_dir:
        repo = Path(fixture_dir)
        tracked_files: list[str] = []
        for source, text in sorted(sources.items()):
            tracked = normalize(f"{source_root}/{source}")
            tracked_files.append(tracked)
            path = repo / tracked
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")

        resolved_includes = scan_resolved_include_files(repo, source_root, tracked_files)
        return build_runtime_package_graph(
            source_root,
            tracked_files,
            resolved_includes,
            scope,
            composition_root,
        )


def runtime_package_graph_self_test(config: dict) -> list[str]:
    """Exercise DAG, cycle, reverse-App, and exact top-level file projections."""
    errors: list[str] = []
    source_root = normalize(config["source_root"])
    policy = config["runtime_package_policy"]
    scope = normalize(policy["scope"])
    composition_root = normalize(policy["composition_root"])
    cases = [
        {
            "name": "legal DAG",
            "sources": {
                "Runtime/App/Main.cpp": '#include "../Feature/Feature.h"\n',
                "Runtime/Feature/Feature.h": '#include "../Leaf/Leaf.h"\n',
                "Runtime/Leaf/Leaf.h": "",
            },
            "edges": {
                ("Runtime/App", "Runtime/Feature"): 1,
                ("Runtime/Feature", "Runtime/Leaf"): 1,
            },
            "cyclic_components": set(),
            "bidirectional_pairs": set(),
            "reverse_sites": set(),
            "required_packages": set(),
        },
        {
            "name": "two-node cycle",
            "sources": {
                "Runtime/Alpha/Alpha.h": '#include "../Beta/Beta.h"\n',
                "Runtime/Beta/Beta.h": '#include "../Alpha/Alpha.h"\n',
            },
            "edges": {
                ("Runtime/Alpha", "Runtime/Beta"): 1,
                ("Runtime/Beta", "Runtime/Alpha"): 1,
            },
            "cyclic_components": {("Runtime/Alpha", "Runtime/Beta")},
            "bidirectional_pairs": {("Runtime/Alpha", "Runtime/Beta")},
            "reverse_sites": set(),
            "required_packages": set(),
        },
        {
            "name": "long cycle",
            "sources": {
                "Runtime/Alpha/Alpha.h": '#include "../Beta/Beta.h"\n',
                "Runtime/Beta/Beta.h": '#include "../Gamma/Gamma.h"\n',
                "Runtime/Gamma/Gamma.h": '#include "../Alpha/Alpha.h"\n',
            },
            "edges": {
                ("Runtime/Alpha", "Runtime/Beta"): 1,
                ("Runtime/Beta", "Runtime/Gamma"): 1,
                ("Runtime/Gamma", "Runtime/Alpha"): 1,
            },
            "cyclic_components": {
                ("Runtime/Alpha", "Runtime/Beta", "Runtime/Gamma")
            },
            "bidirectional_pairs": set(),
            "reverse_sites": set(),
            "required_packages": set(),
        },
        {
            "name": "reverse App edge",
            "sources": {
                "Runtime/App/Run.h": "",
                "Runtime/Feature/Feature.cpp": '// fixture prelude\n#include "../App/Run.h"\n',
            },
            "edges": {("Runtime/Feature", "Runtime/App"): 1},
            "cyclic_components": set(),
            "bidirectional_pairs": set(),
            "reverse_sites": {
                ("Runtime/Feature/Feature.cpp", 2, "Runtime/App/Run.h")
            },
            "required_packages": set(),
        },
        {
            "name": "top-level Runtime exact file",
            "sources": {
                "Runtime/App/Main.cpp": '#include "../RuntimeFrameViews.h"\n',
                "Runtime/Feature/Value.h": "",
                "Runtime/RuntimeFrameViews.h": '#include "Feature/Value.h"\n',
            },
            "edges": {
                ("Runtime/App", "Runtime/RuntimeFrameViews.h"): 1,
                ("Runtime/RuntimeFrameViews.h", "Runtime/Feature"): 1,
            },
            "cyclic_components": set(),
            "bidirectional_pairs": set(),
            "reverse_sites": set(),
            "required_packages": {"Runtime/RuntimeFrameViews.h"},
        },
    ]

    for case in cases:
        report = runtime_package_graph_fixture(
            source_root, scope, composition_root, case["sources"]
        )
        actual_edges = {
            (edge.source_package, edge.target_package): edge.include_site_count
            for edge in report.edges
        }
        if actual_edges != case["edges"]:
            errors.append(
                f"Runtime package graph {case['name']} returned edges {actual_edges!r}; "
                f"expected {case['edges']!r}"
            )

        actual_cyclic_components = set(report.cyclic_components)
        if actual_cyclic_components != case["cyclic_components"]:
            errors.append(
                f"Runtime package graph {case['name']} returned cyclic components "
                f"{actual_cyclic_components!r}; expected {case['cyclic_components']!r}"
            )

        actual_bidirectional_pairs = set(report.bidirectional_pairs)
        if actual_bidirectional_pairs != case["bidirectional_pairs"]:
            errors.append(
                f"Runtime package graph {case['name']} returned bidirectional pairs "
                f"{actual_bidirectional_pairs!r}; expected {case['bidirectional_pairs']!r}"
            )

        actual_reverse_sites = {
            (site.source, site.line, site.target)
            for site in report.reverse_composition_root_include_sites
        }
        if actual_reverse_sites != case["reverse_sites"]:
            errors.append(
                f"Runtime package graph {case['name']} returned reverse App sites "
                f"{actual_reverse_sites!r}; expected {case['reverse_sites']!r}"
            )

        missing_packages = case["required_packages"] - set(report.packages)
        if missing_packages:
            errors.append(
                f"Runtime package graph {case['name']} omitted packages {missing_packages!r}"
            )

        component_members = sorted(
            member
            for component in report.strongly_connected_components
            for member in component
        )
        if component_members != list(report.packages):
            errors.append(
                f"Runtime package graph {case['name']} did not partition every package exactly once"
            )

    return errors


def runtime_package_policy_self_test(config: dict) -> list[str]:
    """Exercise strict rank/exact/SCC behavior and sealed repair reconciliation."""
    errors = validate_runtime_policy(config["runtime_package_policy"])
    source_root = normalize(config["source_root"])
    scope = normalize(config["runtime_package_policy"]["scope"])
    synthetic = {
        "scope": scope,
        "composition_root": f"{scope}/App",
        "packages": [
            {
                "package": f"{scope}/Leaf",
                "rank": 0,
                "allowed_exact_target_files": [],
            },
            {
                "package": f"{scope}/Feature",
                "rank": 1,
                "allowed_exact_target_files": [f"{scope}/Leaf/Leaf.h"],
            },
            {
                "package": f"{scope}/App",
                "rank": 2,
                "allowed_exact_target_files": [f"{scope}/Feature/Feature.h"],
            },
        ],
        "allowed_exact_edges": [
            {
                "source_file": f"{scope}/Feature/Special.cpp",
                "target_file": f"{scope}/Leaf/Special.h",
            }
        ],
        "repair_plan_policy": {
            "owner": "self-test Runtime architecture",
            "plan": "fixture",
            "reason": "exercise exact repair reconciliation",
            "rulings": [],
        },
    }
    synthetic_errors = validate_runtime_policy(synthetic)
    if synthetic_errors:
        errors.extend(f"synthetic Runtime policy: {error}" for error in synthetic_errors)
        return errors

    def evaluate(sources: dict[str, str]) -> tuple[
        RuntimePackageGraph, RuntimePolicyEvaluation
    ]:
        report = runtime_package_graph_fixture(
            source_root, scope, synthetic["composition_root"], sources
        )
        return report, evaluate_runtime_policy(report, synthetic)

    legal_sources = {
        f"{scope}/App/Main.cpp": '#include "../Feature/Feature.h"\n',
        f"{scope}/Feature/Feature.h": '#include "../Leaf/Leaf.h"\n',
        f"{scope}/Leaf/Leaf.h": "",
    }
    _, legal = evaluate(legal_sources)
    if not legal.strict_ok:
        errors.append(f"strict Runtime policy rejected legal exact downward DAG: {legal!r}")

    unlisted_sources = {
        f"{scope}/Feature/Feature.cpp": '#include "../Leaf/Other.h"\n',
        f"{scope}/Leaf/Other.h": "",
    }
    unlisted_report, unlisted = evaluate(unlisted_sources)
    if len(unlisted.violations) != 1 or unlisted.strict_ok:
        errors.append("strict Runtime policy did not reject rank-downward unlisted target")
    elif unlisted.violations[0].reasons != (
        "edge is absent from the closed exact-file projection",
    ):
        errors.append(f"rank-downward rejection reason drifted: {unlisted.violations[0]!r}")

    reverse_sources = {
        f"{scope}/App/Run.h": "",
        f"{scope}/Leaf/Leaf.cpp": '#include "../App/Run.h"\n',
    }
    reverse_report, reverse = evaluate(reverse_sources)
    if len(reverse.violations) != 1 or reverse.strict_ok:
        errors.append("strict Runtime policy did not reject reverse composition-root edge")
    elif "target rank is not lower than source rank" not in reverse.violations[0].reasons:
        errors.append("reverse composition-root fixture did not fail rank direction")

    exact_pair_sources = {
        f"{scope}/Feature/Special.cpp": '#include "../Leaf/Special.h"\n',
        f"{scope}/Feature/Other.cpp": '#include "../Leaf/Special.h"\n',
        f"{scope}/Leaf/Special.h": "",
    }
    _, exact_pair = evaluate(exact_pair_sources)
    if len(exact_pair.violations) != 1:
        errors.append("mixed exact source/target fixture did not reject only the sibling source")
    elif exact_pair.violations[0].source != f"{scope}/Feature/Other.cpp":
        errors.append(f"mixed exact source/target fixture rejected wrong site: {exact_pair!r}")

    cycle_cases = [
        {
            f"{scope}/Feature/Feature.h": '#include "../Leaf/Leaf.h"\n',
            f"{scope}/Leaf/Leaf.h": '#include "../Feature/Feature.h"\n',
        },
        {
            f"{scope}/App/Run.h": '#include "../Feature/Feature.h"\n',
            f"{scope}/Feature/Feature.h": '#include "../Leaf/Leaf.h"\n',
            f"{scope}/Leaf/Leaf.h": '#include "../App/Run.h"\n',
        },
    ]
    for expected_length, sources in zip((3, 4), cycle_cases):
        _, cycle = evaluate(sources)
        if len(cycle.cycle_traces) != 1 or cycle.strict_ok:
            errors.append("strict Runtime policy did not reject every synthetic SCC")
            continue
        trace = cycle.cycle_traces[0]
        if len(trace.packages) != expected_length or len(trace.include_sites) != expected_length - 1:
            errors.append(f"Runtime cycle trace did not close every concrete edge: {trace!r}")

    if unlisted.violations:
        violation = unlisted.violations[0]
        sealed_policy = json.loads(json.dumps(synthetic))
        sealed_policy["repair_plan_policy"]["rulings"] = [
            {
                "source": violation.source,
                "line": violation.line,
                "target": violation.target,
                "fingerprint": runtime_violation_fingerprint(violation),
                "repair_phase": "fixture",
            }
        ]
        repairs = reconcile_runtime_repairs(unlisted, sealed_policy)
        if len(repairs.current) != 1 or repairs.diagnostics:
            errors.append("exact current Runtime repair-plan ruling did not pass default policy")

        sibling_report = runtime_package_graph_fixture(
            source_root,
            scope,
            synthetic["composition_root"],
            {
                **unlisted_sources,
                f"{scope}/Feature/Sibling.cpp": '#include "../Leaf/Other.h"\n',
            },
        )
        sibling = evaluate_runtime_policy(sibling_report, sealed_policy)
        sibling_repairs = reconcile_runtime_repairs(sibling, sealed_policy)
        if len(sibling_repairs.current) != 1 or not any(
            "unruled forbidden Runtime edge" in finding.detail
            for finding in sibling_repairs.diagnostics
        ):
            errors.append("new Runtime site to a repair-ruled target did not fail closed")

        for spelling_name, directive in (
            ("separator", '#include "..\\\\Leaf\\\\Other.h"\n'),
            ("delimiter", "#include <../Leaf/Other.h>\n"),
        ):
            spelling_report = runtime_package_graph_fixture(
                source_root,
                scope,
                synthetic["composition_root"],
                {
                    f"{scope}/Feature/Feature.cpp": directive,
                    f"{scope}/Leaf/Other.h": "",
                },
            )
            spelling = evaluate_runtime_policy(spelling_report, sealed_policy)
            spelling_repairs = reconcile_runtime_repairs(spelling, sealed_policy)
            if len(spelling.violations) != 1 or not any(
                "changed Runtime repair-plan fingerprint" in finding.detail
                for finding in spelling_repairs.diagnostics
            ):
                errors.append(
                    f"{spelling_name}-only Runtime debt drift did not fail closed"
                )

        shifted_report = runtime_package_graph_fixture(
            source_root,
            scope,
            synthetic["composition_root"],
            {
                f"{scope}/Feature/Feature.cpp": '\n#include "../Leaf/Other.h"\n',
                f"{scope}/Leaf/Other.h": "",
            },
        )
        shifted = evaluate_runtime_policy(shifted_report, sealed_policy)
        shifted_repairs = reconcile_runtime_repairs(shifted, sealed_policy)
        shifted_details = [finding.detail for finding in shifted_repairs.diagnostics]
        if not any("unruled forbidden Runtime edge" in detail for detail in shifted_details) or not any(
            "stale Runtime repair-plan ruling" in detail for detail in shifted_details
        ):
            errors.append("shifted Runtime debt site did not produce unruled plus stale diagnostics")

        deleted = reconcile_runtime_repairs(legal, sealed_policy)
        if not any("stale Runtime repair-plan ruling" in finding.detail for finding in deleted.diagnostics):
            errors.append("deleted Runtime debt site did not make its ruling stale")

        document = runtime_package_graph_document(
            unlisted_report, sealed_policy, unlisted, repairs
        )
        if document["forbidden_include_site_count"] != 1:
            errors.append("Runtime policy document serialized the wrong forbidden-site count")
        if not document["strict_policy_ok"] is False or not document[
            "default_repository_policy_ok"
        ]:
            errors.append("Runtime policy document strict/default status is incorrect")
        if document != runtime_package_graph_document(
            unlisted_report, sealed_policy, unlisted, repairs
        ):
            errors.append("Runtime policy document changed across unchanged serialization")
    return errors


def project_rule_fixture_cases(
    rule: dict, fixture_path: str
) -> list[tuple[str, set[str], list[Finding]]]:
    """Build independent exact-result cases from one project-ownership rule."""
    required = rule["required_project"]
    cases = [
        ("required only", {required}, []),
        (
            "missing required",
            set(),
            [Finding(rule["id"], fixture_path, required, "missing required project ownership")],
        ),
    ]
    for forbidden in rule.get("forbidden_projects", []):
        cases.append(
            (
                f"required plus {forbidden}",
                {required, forbidden},
                [
                    Finding(
                        rule["id"],
                        fixture_path,
                        forbidden,
                        "forbidden duplicate project ownership",
                    )
                ],
            )
        )
    return cases


def write_project_fixture(path: Path, items: list[str]) -> None:
    """Write the minimal namespaced project XML consumed by project_items."""
    namespace = MSBUILD_NAMESPACE["m"]
    ET.register_namespace("", namespace)
    root = ET.Element(f"{{{namespace}}}Project")
    item_group = ET.SubElement(root, f"{{{namespace}}}ItemGroup")
    for item in items:
        item_type = "ClCompile" if Path(item).suffix.lower() == ".cpp" else "ClInclude"
        ET.SubElement(
            item_group,
            f"{{{namespace}}}{item_type}",
            {"Include": item.replace("/", "\\")},
        )
    ET.ElementTree(root).write(path, encoding="utf-8", xml_declaration=True)


def project_rule_self_test(rule: dict) -> list[str]:
    """Exercise exact direct and all-suffix repository-discovery ownership cases."""
    errors: list[str] = []
    fixture_path = normalize(f"{rule['path_prefix']}Fixture{rule['suffixes'][0]}")
    fixture_paths = [
        normalize(f"{rule['path_prefix']}Fixture{suffix}")
        for suffix in rule["suffixes"]
    ]
    project_names = {
        rule["required_project"],
        *rule.get("forbidden_projects", []),
    }
    cases = project_rule_fixture_cases(rule, fixture_path)

    for name, member_projects, expected in cases:
        membership = {
            project: ({fixture_path} if project in member_projects else set())
            for project in project_names
        }
        actual = evaluate_project_rule(rule, {fixture_path}, membership)
        if actual != expected:
            errors.append(f"{rule['id']}: direct project fixture '{name}' returned {actual!r}")

    with tempfile.TemporaryDirectory(prefix="skore_dependency_project_fixture_") as fixture_dir:
        repo = Path(fixture_dir)
        subprocess.run(
            ["git", "init", "--quiet"],
            cwd=repo,
            text=True,
            capture_output=True,
            check=True,
        )

        for tracked_fixture_path in fixture_paths:
            fixture_source = repo / tracked_fixture_path
            fixture_source.parent.mkdir(parents=True, exist_ok=True)
            fixture_source.write_text("// tracked ownership fixture\n", encoding="utf-8")
        untracked_path = normalize(
            f"{rule['path_prefix']}UntrackedFixture{rule['suffixes'][0]}"
        )
        untracked_source = repo / untracked_path
        untracked_source.write_text("// intentionally untracked\n", encoding="utf-8")
        ignored_suffix_path = normalize(f"{rule['path_prefix']}TrackedButIgnored.txt")
        ignored_suffix_source = repo / ignored_suffix_path
        ignored_suffix_source.write_text("not source-bearing\n", encoding="utf-8")
        subprocess.run(
            ["git", "add", "--", *fixture_paths, ignored_suffix_path],
            cwd=repo,
            text=True,
            capture_output=True,
            check=True,
        )

        noise_project = next(iter(rule.get("forbidden_projects", [])), rule["required_project"])
        for name, member_projects, expected in cases:
            for project in sorted(project_names):
                items = list(fixture_paths) if project in member_projects else []
                if project == noise_project:
                    items.extend([untracked_path, ignored_suffix_path])
                write_project_fixture(repo / project, items)

            actual = scan_project_rules(repo, [rule])
            end_to_end_expected = [
                Finding(finding.rule_id, path, finding.target, finding.detail)
                for path in sorted(fixture_paths)
                for finding in expected
            ]
            if actual != end_to_end_expected:
                errors.append(
                    f"{rule['id']}: end-to-end XML/path fixture '{name}' returned {actual!r}"
                )
    return errors


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
    errors.extend(include_parser_limit_self_test(config))
    errors.extend(runtime_package_graph_self_test(config))
    errors.extend(runtime_package_policy_self_test(config))
    for rule in config["include_rules"]:
        sources = include_fixture_sources(rule)
        positive_targets = [normalize(rule["positive_target"])]
        positive_targets.extend(
            normalize(target) for target in rule.get("allowed_target_files", [])
        )
        positive_targets = list(dict.fromkeys(positive_targets))
        negative_entries = include_fixture_negative_entries(rule)
        if rule["mode"] == "allow":
            future_target = normalize(
                f"{rule['target_scope']}/UnregisteredPackage/Fixture.h"
            )
            negative_entries.append((future_target, future_target, True))
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
        errors.extend(project_rule_self_test(rule))
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
    action.add_argument(
        "--report-runtime-graph",
        action="store_true",
        help=(
            "Emit deterministic Runtime graph, strict findings, and repair debt as JSON "
            "without changing the exit status."
        ),
    )
    action.add_argument(
        "--check-runtime-graph",
        action="store_true",
        help=(
            "Emit the Runtime graph as JSON and fail every forbidden exact edge, "
            "unregistered package, or multi-package SCC; repair rulings are ignored."
        ),
    )
    args = parser.parse_args()

    repo = args.repo.resolve()
    config = json.loads(args.rules.read_text(encoding="utf-8"))
    if config.get("version") != 2:
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
            len(include_fixture_sources(rule))
            * (
                len(include_fixture_negative_targets(rule))
                + (1 if rule["mode"] == "allow" else 0)
            )
            for rule in config["include_rules"]
        )
        content_fixture_count = sum(
            len(rule.get("negative_fixtures", []))
            for rule in config.get("content_rules", [])
        )
        project_fixture_count = sum(
            2 * (2 + len(rule.get("forbidden_projects", [])))
            for rule in config["project_rules"]
        )
        print(
            f"SELF_TEST_PASS: {len(config['include_rules'])} include rules with "
            f"{negative_fixture_count} negative edge fixtures and "
            f"{len(config.get('content_rules', []))} content rules with "
            f"{content_fixture_count} negative content fixtures and "
            f"{project_fixture_count} exact project-rule cases plus "
            "5 Runtime graph cases, strict rank/exact/SCC and sealed-repair cases, "
            "textual parser-limit cases, and generated-proof fixtures passed"
        )
        return 0
    if args.render_proof:
        print(rendered_proof)
        return 0
    if args.report_runtime_graph or args.check_runtime_graph:
        policy = config["runtime_package_policy"]
        report = scan_runtime_package_graph(repo, config["source_root"], policy)
        evaluation = evaluate_runtime_policy(report, policy)
        repairs = reconcile_runtime_repairs(evaluation, policy)
        print(
            json.dumps(
                runtime_package_graph_document(report, policy, evaluation, repairs),
                indent=2,
            )
        )
        return 0 if args.report_runtime_graph or evaluation.strict_ok else 1

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

    tracked_files = repository_source_files(repo, config["source_root"])
    resolved_includes = scan_resolved_include_files(
        repo, config["source_root"], tracked_files
    )
    findings = evaluate_resolved_includes(config["include_rules"], resolved_includes)
    runtime_policy = config["runtime_package_policy"]
    runtime_report = build_runtime_package_graph(
        config["source_root"],
        tracked_files,
        resolved_includes,
        runtime_policy["scope"],
        runtime_policy["composition_root"],
    )
    runtime_evaluation = evaluate_runtime_policy(runtime_report, runtime_policy)
    runtime_repairs = reconcile_runtime_repairs(runtime_evaluation, runtime_policy)
    findings.extend(runtime_repairs.diagnostics)
    findings.extend(
        scan_content_files(
            repo,
            config["source_root"],
            config.get("content_rules", []),
            tracked_files,
        )
    )
    findings.extend(scan_project_rules(repo, config["project_rules"]))
    for finding in findings:
        print(
            f"ERROR[{finding.rule_id}]: {finding.source} -> {finding.target}: {finding.detail}",
            file=sys.stderr,
        )
    print(
        f"Dependency graph summary: include_rules={len(config['include_rules'])} "
        f"content_rules={len(config.get('content_rules', []))} "
        f"project_rules={len(config['project_rules'])} "
        f"runtime_repair_plan_debt={len(runtime_repairs.current)} "
        f"findings={len(findings)}"
    )
    if findings:
        return 1
    print("PASS: dependency graph and project ownership are valid.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
