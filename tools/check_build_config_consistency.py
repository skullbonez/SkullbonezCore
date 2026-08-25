#!/usr/bin/env python3
"""
Purpose:
  Inventory effective C++ metadata and first-party Visual Studio project
  topology, then fail configuration drift, project cycles, and production
  sources without exactly one production owner.

Invariants:
  - Every tracked or unignored vcxproj is registered exactly once in rule data.
  - Effective-metadata scanning covers exactly rows with metadata_inventory=true.
  - Cross-project comparison pairs only equal Configuration|Platform names;
    Debug-versus-Release differences are normal build modes, not divergence.
  - A ruling digest changes when any project, configuration, or effective value
    in the ruled setting changes.
  - Dropped list inheritance always fails; a ruling cannot hide it.
  - Every configured project reference participates in one acyclic graph.
  - Every tracked source in the configured production scope has exactly one
    production-role project owner; test compilation remains separate evidence.
  - Unsupported MSBuild conditions fail closed instead of silently guessing.
"""

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
from collections import defaultdict
from dataclasses import asdict, dataclass
from pathlib import Path


SETTINGS = (
    "PreprocessorDefinitions",
    "ExceptionHandling",
    "LanguageStandard",
    "FloatingPointModel",
    "RuntimeLibrary",
    "ForcedIncludeFiles",
    "FunctionLevelLinking",
)
EFFECTIVE_SETTINGS = SETTINGS + ("AdditionalIncludeDirectories",)
LIST_SETTINGS = {"PreprocessorDefinitions", "ForcedIncludeFiles", "AdditionalIncludeDirectories"}
DEFAULT_RULINGS = Path("tools/build_config_rulings.json")
SHA256_RE = re.compile(r"[0-9a-f]{64}")
COMPARISON_RE = re.compile(
    r"^\s*(['\"])(.*?)\1\s*(==|!=)\s*(['\"])(.*?)\4\s*$",
    re.IGNORECASE,
)


@dataclass(frozen=True)
class CompileRow:
    file: str
    project: str
    configuration: str
    settings: dict[str, str]


@dataclass(frozen=True)
class DroppedInheritance:
    file: str
    project: str
    configuration: str
    setting: str
    override: str
    inherited: str


@dataclass(frozen=True)
class Divergence:
    file: str
    configuration: str
    setting: str
    variants: tuple[tuple[str, str], ...]


@dataclass(frozen=True)
class Ruling:
    file: str
    setting: str
    fingerprint: str
    owner: str
    reason: str
    evidence: str

    @property
    def key(self) -> tuple[str, str]:
        return self.file, self.setting


@dataclass(frozen=True)
class ProjectTopology:
    projects: tuple[str, ...]
    metadata_projects: tuple[str, ...]
    production_projects: tuple[str, ...]
    edges: tuple[tuple[str, str], ...]
    cyclic_components: tuple[tuple[str, ...], ...]
    cycle_traces: tuple[tuple[str, ...], ...]
    tracked_production_sources: tuple[str, ...]
    production_source_owners: tuple[tuple[str, tuple[str, ...]], ...]
    diagnostics: tuple[str, ...]


def _local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def _normalize_source(path: str) -> str:
    normalized = posixpath.normpath(path.replace("\\", "/").strip())
    return normalized.removeprefix("./").casefold()


def _condition_value(text: str, configuration: str, platform: str) -> str:
    return (
        text.replace("$(Configuration)", configuration)
        .replace("$(Platform)", platform)
        .strip()
    )


def _evaluate_comparison(text: str) -> bool:
    match = COMPARISON_RE.fullmatch(text)
    if not match:
        raise ValueError(f"unsupported MSBuild condition comparison: {text}")
    left = match.group(2).casefold()
    operator = match.group(3)
    right = match.group(5).casefold()
    return left == right if operator == "==" else left != right


def condition_matches(condition: str | None, configuration: str, platform: str) -> bool:
    if condition is None or not condition.strip():
        return True
    expanded = _condition_value(condition, configuration, platform)
    or_terms = re.split(r"\s+[Oo][Rr]\s+", expanded)
    results: list[bool] = []
    for or_term in or_terms:
        and_terms = re.split(r"\s+[Aa][Nn][Dd]\s+", or_term)
        results.append(all(_evaluate_comparison(term) for term in and_terms))
    return any(results)


def _children_named(node: ET.Element, name: str) -> list[ET.Element]:
    return [child for child in node if _local_name(child.tag) == name]


def _configurations(root: ET.Element) -> list[tuple[str, str]]:
    configurations: list[tuple[str, str]] = []
    for node in root.iter():
        if _local_name(node.tag) != "ProjectConfiguration":
            continue
        include = node.get("Include", "")
        if "|" not in include:
            raise ValueError(f"invalid ProjectConfiguration Include={include!r}")
        configuration, platform = include.split("|", 1)
        configurations.append((configuration, platform))
    if not configurations:
        raise ValueError("project declares no ProjectConfiguration rows")
    return configurations


def _project_defaults(
    root: ET.Element,
    configuration: str,
    platform: str,
) -> tuple[dict[str, str], set[str]]:
    values = {setting: "" for setting in EFFECTIVE_SETTINGS}
    declared: set[str] = set()
    for group in root:
        if _local_name(group.tag) != "ItemDefinitionGroup":
            continue
        if not condition_matches(group.get("Condition"), configuration, platform):
            continue
        compile_nodes = _children_named(group, "ClCompile")
        for compile_node in compile_nodes:
            for setting in EFFECTIVE_SETTINGS:
                children = [
                    child
                    for child in _children_named(compile_node, setting)
                    if condition_matches(child.get("Condition"), configuration, platform)
                ]
                if children:
                    declared.add(setting)
                    values[setting] = (children[-1].text or "").strip()
    return values, declared


def _compile_items(root: ET.Element) -> tuple[dict[str, list[ET.Element]], dict[str, list[ET.Element]]]:
    includes: dict[str, list[ET.Element]] = defaultdict(list)
    updates: dict[str, list[ET.Element]] = defaultdict(list)
    for group in root:
        if _local_name(group.tag) != "ItemGroup":
            continue
        for node in group:
            if _local_name(node.tag) != "ClCompile":
                continue
            include = node.get("Include")
            update = node.get("Update")
            if bool(include) == bool(update):
                raise ValueError("ClCompile must have exactly one Include or Update attribute")
            if include:
                includes[_normalize_source(include)].append(node)
            else:
                assert update is not None
                updates[_normalize_source(update)].append(node)
    return includes, updates


def _metadata_values(
    nodes: list[ET.Element],
    name: str,
    configuration: str,
    platform: str,
) -> list[str]:
    values: list[str] = []
    for node in nodes:
        for child in _children_named(node, name):
            if condition_matches(child.get("Condition"), configuration, platform):
                values.append((child.text or "").strip())
    return values


def _effective_list_value(base: str, override: str, setting: str) -> str:
    token = f"%({setting})"
    if token in override:
        return override.replace(token, base)
    return override


def _normalized_setting_value(setting: str, value: str) -> str:
    if setting not in LIST_SETTINGS:
        return value.strip()
    parts = [part.strip() for part in value.split(";") if part.strip()]
    if setting == "ForcedIncludeFiles":
        parts = [part.replace("\\", "/").replace("$(ProjectDir)", "$(ProjectDir)/") for part in parts]
    return ";".join(parts)


def scan_project(
    repo: Path,
    project_name: str,
) -> tuple[list[CompileRow], list[DroppedInheritance]]:
    project_path = repo / project_name
    root = ET.parse(project_path).getroot()
    includes, updates = _compile_items(root)
    rows: list[CompileRow] = []
    drops: list[DroppedInheritance] = []
    for configuration, platform in _configurations(root):
        defaults, declared_defaults = _project_defaults(
            root, configuration, platform
        )
        for setting in LIST_SETTINGS:
            value = defaults[setting]
            token = f"%({setting})"
            if setting in declared_defaults and token not in value:
                drops.append(
                    DroppedInheritance(
                        file="<project ClCompile defaults>",
                        project=project_name,
                        configuration=f"{configuration}|{platform}",
                        setting=setting,
                        override=_normalized_setting_value(setting, value),
                        inherited=token,
                    )
                )
        for file_name, include_nodes in includes.items():
            active_includes = [
                node
                for node in include_nodes
                if condition_matches(node.get("Condition"), configuration, platform)
            ]
            if not active_includes:
                continue
            active_updates = [
                node
                for node in updates.get(file_name, [])
                if condition_matches(node.get("Condition"), configuration, platform)
            ]
            nodes = active_includes + active_updates
            excluded_values = _metadata_values(
                nodes, "ExcludedFromBuild", configuration, platform
            )
            if excluded_values and excluded_values[-1].casefold() == "true":
                continue
            effective: dict[str, str] = {}
            for setting in EFFECTIVE_SETTINGS:
                value = defaults[setting]
                overrides = _metadata_values(nodes, setting, configuration, platform)
                for override in overrides:
                    if setting in LIST_SETTINGS:
                        token = f"%({setting})"
                        if value and token not in override:
                            drops.append(
                                DroppedInheritance(
                                    file=file_name,
                                    project=project_name,
                                    configuration=f"{configuration}|{platform}",
                                    setting=setting,
                                    override=_normalized_setting_value(setting, override),
                                    inherited=_normalized_setting_value(setting, value),
                                )
                            )
                        value = _effective_list_value(value, override, setting)
                    else:
                        value = override
                effective[setting] = _normalized_setting_value(setting, value)
            rows.append(
                CompileRow(
                    file=file_name,
                    project=project_name,
                    configuration=f"{configuration}|{platform}",
                    settings=effective,
                )
            )
    return rows, drops


def scan_repository(
    repo: Path, project_files: tuple[str, ...]
) -> tuple[list[CompileRow], list[DroppedInheritance]]:
    rows: list[CompileRow] = []
    drops: list[DroppedInheritance] = []
    for project_name in project_files:
        project_rows, project_drops = scan_project(repo, project_name)
        rows.extend(project_rows)
        drops.extend(project_drops)
    rows.sort(key=lambda row: (row.file, row.configuration, row.project))
    drops.sort(
        key=lambda row: (row.file, row.setting, row.configuration, row.project)
    )
    return rows, drops


def declared_compile_items(repo: Path, project_name: str) -> set[str]:
    root = ET.parse(repo / project_name).getroot()
    includes, _ = _compile_items(root)
    project_directory = posixpath.dirname(_normalize_source(project_name))
    return {
        _normalize_source(posixpath.join(project_directory, include))
        for include in includes
    }


def repository_project_files(repo: Path) -> tuple[str, ...]:
    """Return every tracked or unignored Visual Studio C++ project path."""
    result = subprocess.run(
        [
            "git",
            "ls-files",
            "-z",
            "--cached",
            "--others",
            "--exclude-standard",
            "--",
            "*.vcxproj",
        ],
        cwd=repo,
        text=True,
        capture_output=True,
        check=True,
    )
    return tuple(
        sorted(_normalize_source(path) for path in result.stdout.split("\0") if path)
    )


def project_reference_edges(
    repo: Path, project_files: tuple[str, ...]
) -> tuple[set[tuple[str, str]], list[str]]:
    canonical = {_normalize_source(project): project for project in project_files}
    edges: set[tuple[str, str]] = set()
    diagnostics: list[str] = []
    for project in project_files:
        if not (repo / project).is_file():
            continue
        root = ET.parse(repo / project).getroot()
        for node in root.iter():
            if _local_name(node.tag) != "ProjectReference":
                continue
            include = node.get("Include")
            if not include:
                diagnostics.append(f"PROJECT-REFERENCE-MISSING-PATH {project}")
                continue
            relative = posixpath.join(posixpath.dirname(project), include)
            target = canonical.get(_normalize_source(relative))
            if target is None:
                diagnostics.append(
                    f"UNREGISTERED-PROJECT-REFERENCE {project} -> {include}"
                )
                continue
            edges.add((project, target))
    return edges, diagnostics


def strongly_connected_projects(
    projects: tuple[str, ...], edges: set[tuple[str, str]]
) -> tuple[tuple[str, ...], ...]:
    adjacency = {project: set() for project in projects}
    for source, target in edges:
        adjacency[source].add(target)
    next_index = 0
    indices: dict[str, int] = {}
    low_links: dict[str, int] = {}
    stack: list[str] = []
    on_stack: set[str] = set()
    components: list[tuple[str, ...]] = []

    def visit(project: str) -> None:
        nonlocal next_index
        indices[project] = next_index
        low_links[project] = next_index
        next_index += 1
        stack.append(project)
        on_stack.add(project)
        for target in sorted(adjacency[project]):
            if target not in indices:
                visit(target)
                low_links[project] = min(low_links[project], low_links[target])
            elif target in on_stack:
                low_links[project] = min(low_links[project], indices[target])
        if low_links[project] != indices[project]:
            return
        component: list[str] = []
        while True:
            member = stack.pop()
            on_stack.remove(member)
            component.append(member)
            if member == project:
                break
        components.append(tuple(sorted(component)))

    for project in projects:
        if project not in indices:
            visit(project)
    return tuple(sorted(components))


def project_cycle_trace(
    component: tuple[str, ...], edges: set[tuple[str, str]]
) -> tuple[str, ...]:
    if len(component) == 1:
        return component + component
    members = set(component)
    adjacency = {
        project: sorted(
            target for source, target in edges if source == project and target in members
        )
        for project in component
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

    for start in component:
        trace = find_from(start, start, [start])
        if trace is not None:
            return tuple(trace)
    raise ValueError(f"project SCC has no concrete cycle trace: {component}")


def tracked_production_sources(repo: Path, policy: dict) -> tuple[str, ...]:
    roots = [root.replace("\\", "/") for root in policy["production_source_roots"]]
    suffixes = tuple(suffix.casefold() for suffix in policy["production_source_suffixes"])
    result = subprocess.run(
        ["git", "ls-files", "-z", "--cached", "--", *roots],
        cwd=repo,
        text=True,
        capture_output=True,
        check=True,
    )
    return tuple(
        sorted(
            _normalize_source(path)
            for path in result.stdout.split("\0")
            if path and Path(path).suffix.casefold() in suffixes
        )
    )


def scan_project_topology(repo: Path, policy: dict) -> ProjectTopology:
    """Derive the first-party project DAG and exact production source owners."""
    projects = tuple(row["path"] for row in policy["projects"])
    metadata_projects = tuple(
        row["path"] for row in policy["projects"] if row["metadata_inventory"]
    )
    production_projects = tuple(
        row["path"] for row in policy["projects"] if row["role"] == "production"
    )
    registered = {_normalize_source(project): project for project in projects}
    discovered = set(repository_project_files(repo))
    diagnostics = [
        f"UNREGISTERED-PROJECT {project}"
        for project in sorted(discovered - set(registered))
    ]
    diagnostics.extend(
        f"STALE-PROJECT-RULE {registered[project]}"
        for project in sorted(set(registered) - discovered)
    )
    edges, reference_diagnostics = project_reference_edges(repo, projects)
    diagnostics.extend(reference_diagnostics)
    components = strongly_connected_projects(projects, edges)
    cyclic_components = tuple(
        component
        for component in components
        if len(component) > 1 or (component[0], component[0]) in edges
    )
    cycle_traces = tuple(
        project_cycle_trace(component, edges) for component in cyclic_components
    )
    diagnostics.extend(
        f"PROJECT-CYCLE {' -> '.join(trace)}" for trace in cycle_traces
    )

    membership = {
        project: (
            declared_compile_items(repo, project)
            if (repo / project).is_file()
            else set()
        )
        for project in production_projects
    }
    tracked_sources = tracked_production_sources(repo, policy)
    source_owners: list[tuple[str, tuple[str, ...]]] = []
    for source in tracked_sources:
        owners = tuple(
            project for project in production_projects if source in membership[project]
        )
        source_owners.append((source, owners))
        if not owners:
            diagnostics.append(f"MISSING-PRODUCTION-OWNER {source}")
        elif len(owners) > 1:
            diagnostics.append(
                f"DUPLICATE-PRODUCTION-OWNER {source}: {', '.join(owners)}"
            )
    return ProjectTopology(
        projects=projects,
        metadata_projects=metadata_projects,
        production_projects=production_projects,
        edges=tuple(sorted(edges)),
        cyclic_components=cyclic_components,
        cycle_traces=cycle_traces,
        tracked_production_sources=tracked_sources,
        production_source_owners=tuple(source_owners),
        diagnostics=tuple(sorted(diagnostics)),
    )


def find_divergences(rows: list[CompileRow]) -> list[Divergence]:
    grouped: dict[tuple[str, str], list[CompileRow]] = defaultdict(list)
    projects_by_file: dict[str, set[str]] = defaultdict(set)
    for row in rows:
        grouped[(row.file, row.configuration)].append(row)
        projects_by_file[row.file].add(row.project)
    divergences: list[Divergence] = []
    for (file_name, configuration), group in grouped.items():
        if len(projects_by_file[file_name]) < 2 or len(group) < 2:
            continue
        for setting in SETTINGS:
            variants = tuple(
                sorted((row.project, row.settings[setting]) for row in group)
            )
            if len({value for _, value in variants}) > 1:
                divergences.append(
                    Divergence(
                        file=file_name,
                        configuration=configuration,
                        setting=setting,
                        variants=variants,
                    )
                )
    divergences.sort(key=lambda row: (row.file, row.setting, row.configuration))
    return divergences


def divergence_fingerprints(
    divergences: list[Divergence],
) -> dict[tuple[str, str], str]:
    grouped: dict[tuple[str, str], list[dict[str, object]]] = defaultdict(list)
    for row in divergences:
        grouped[(row.file, row.setting)].append(asdict(row))
    fingerprints: dict[tuple[str, str], str] = {}
    for key, payload in grouped.items():
        encoded = json.dumps(payload, sort_keys=True, separators=(",", ":")).encode(
            "utf-8"
        )
        fingerprints[key] = hashlib.sha256(encoded).hexdigest()
    return fingerprints


def load_rule_data(path: Path) -> dict:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if payload.get("schema_version") != 2:
        raise ValueError("build-config rulings schema_version must be 2")
    project_policy = payload.get("project_policy")
    if not isinstance(project_policy, dict):
        raise ValueError("build-config rulings must contain project_policy")
    roots = project_policy.get("production_source_roots")
    suffixes = project_policy.get("production_source_suffixes")
    projects = project_policy.get("projects")
    if not isinstance(roots, list) or not roots or not all(
        isinstance(root, str) and root.strip() for root in roots
    ):
        raise ValueError("project_policy production_source_roots must be non-empty strings")
    if not isinstance(suffixes, list) or not suffixes or not all(
        isinstance(suffix, str) and suffix.startswith(".") for suffix in suffixes
    ):
        raise ValueError("project_policy production_source_suffixes must be dotted strings")
    if not isinstance(projects, list) or not projects:
        raise ValueError("project_policy projects must be a non-empty list")
    paths: set[str] = set()
    production_count = 0
    for row in projects:
        if not isinstance(row, dict):
            raise ValueError("project_policy project rows must be objects")
        project = row.get("path")
        role = row.get("role")
        metadata_inventory = row.get("metadata_inventory")
        if not isinstance(project, str) or not project.strip():
            raise ValueError("project_policy project path must be non-empty")
        key = _normalize_source(project)
        if key in paths:
            raise ValueError(f"duplicate project_policy project path: {project}")
        paths.add(key)
        if role not in {"production", "test"}:
            raise ValueError(f"project_policy role must be production or test: {project}")
        if not isinstance(metadata_inventory, bool):
            raise ValueError(
                f"project_policy metadata_inventory must be boolean: {project}"
            )
        if role == "production" and not metadata_inventory:
            raise ValueError(
                f"production project must participate in metadata inventory: {project}"
            )
        production_count += role == "production"
    if production_count == 0:
        raise ValueError("project_policy must name at least one production project")
    return payload


def load_rulings(payload: dict) -> dict[tuple[str, str], Ruling]:
    raw_rulings = payload.get("rulings")
    if not isinstance(raw_rulings, dict):
        raise ValueError("build-config rulings must contain a rulings object")
    owner = payload.get("owner")
    evidence = payload.get("evidence")
    reasons = payload.get("reasons")
    if not isinstance(owner, str) or not owner.strip():
        raise ValueError("build-config rulings owner must be a non-empty string")
    if not isinstance(evidence, str) or not evidence.strip():
        raise ValueError("build-config rulings evidence must be a non-empty string")
    if not isinstance(reasons, dict):
        raise ValueError("build-config rulings reasons must be an object")
    rulings: dict[tuple[str, str], Ruling] = {}
    for setting, reason in reasons.items():
        if setting not in SETTINGS:
            raise ValueError(f"reason setting {setting!r} is not inventoried")
        if not isinstance(reason, str) or not reason.strip():
            raise ValueError(f"reason for {setting} must be a non-empty string")
    for file_name, setting_map in raw_rulings.items():
        if not isinstance(file_name, str) or not file_name.strip():
            raise ValueError("ruling file keys must be non-empty strings")
        if not isinstance(setting_map, dict) or not setting_map:
            raise ValueError(f"rulings[{file_name!r}] must be a non-empty object")
        for setting, fingerprint in setting_map.items():
            if setting not in SETTINGS:
                raise ValueError(f"rulings[{file_name!r}] setting is not inventoried")
            if setting not in reasons:
                raise ValueError(f"rulings[{file_name!r}] has no reason for {setting}")
            if not isinstance(fingerprint, str) or not SHA256_RE.fullmatch(fingerprint):
                raise ValueError(
                    f"rulings[{file_name!r}].{setting} must be 64 lowercase hexadecimal digits"
                )
            ruling = Ruling(
                file=file_name.strip(),
                setting=setting,
                fingerprint=fingerprint,
                owner=owner.strip(),
                reason=reasons[setting].strip(),
                evidence=evidence.strip(),
            )
            if ruling.key in rulings:
                raise ValueError(f"duplicate ruling for {ruling.file}: {ruling.setting}")
            rulings[ruling.key] = ruling
    return rulings


def ruling_diagnostics(
    fingerprints: dict[tuple[str, str], str],
    rulings: dict[tuple[str, str], Ruling],
) -> list[str]:
    diagnostics: list[str] = []
    for key, fingerprint in sorted(fingerprints.items()):
        ruling = rulings.get(key)
        if ruling is None:
            diagnostics.append(f"UNRULED {key[0]}: {key[1]} fingerprint={fingerprint}")
        elif ruling.fingerprint != fingerprint:
            diagnostics.append(
                f"CHANGED-SETTING {key[0]}: {key[1]} "
                f"expected={ruling.fingerprint} actual={fingerprint}"
            )
    for file_name, setting in sorted(set(rulings) - set(fingerprints)):
        diagnostics.append(f"STALE-RULING {file_name}: {setting}")
    return diagnostics


def _json_output(
    rows: list[CompileRow],
    drops: list[DroppedInheritance],
    divergences: list[Divergence],
    fingerprints: dict[tuple[str, str], str],
    diagnostics: list[str],
    topology: ProjectTopology,
) -> str:
    return (
        json.dumps(
            {
                "compile_rows": [asdict(row) for row in rows],
                "dropped_inheritance": [asdict(row) for row in drops],
                "divergences": [asdict(row) for row in divergences],
                "fingerprints": [
                    {"file": key[0], "setting": key[1], "fingerprint": value}
                    for key, value in sorted(fingerprints.items())
                ],
                "diagnostics": diagnostics,
                "project_topology": asdict(topology),
            },
            indent=2,
        )
        + "\n"
    )


def _text_output(
    rows: list[CompileRow],
    drops: list[DroppedInheritance],
    divergences: list[Divergence],
    fingerprints: dict[tuple[str, str], str],
    diagnostics: list[str],
    topology: ProjectTopology,
) -> str:
    shared_files = {
        row.file
        for row in rows
        if sum(other.project != row.project and other.file == row.file for other in rows)
    }
    lines = [
        "Build configuration consistency inventory",
        f"compile rows: {len(rows)}",
        f"source files: {len({row.file for row in rows})}",
        f"shared source files: {len(shared_files)}",
        f"divergent file/setting pairs: {len(fingerprints)}",
        f"dropped inheritance rows: {len(drops)}",
        f"first-party topology projects: {len(topology.projects)}",
        f"effective-metadata projects: {len(topology.metadata_projects)}",
        f"production projects: {len(topology.production_projects)}",
        f"project-reference edges: {len(topology.edges)}",
        f"cyclic project components: {len(topology.cyclic_components)}",
        f"tracked production sources: {len(topology.tracked_production_sources)}",
        f"project-topology diagnostics: {len(topology.diagnostics)}",
        f"blocking diagnostics: {len(diagnostics) + len(drops) + len(topology.diagnostics)}",
    ]
    if drops:
        lines.append("Dropped inheritance:")
        lines.extend(
            f"  {row.file} {row.project} {row.configuration} {row.setting}"
            for row in drops
        )
    if divergences:
        lines.append("Divergences:")
        for row in divergences:
            variants = ", ".join(f"{project}={value}" for project, value in row.variants)
            lines.append(
                f"  {row.file} {row.configuration} {row.setting}: {variants}"
            )
    if diagnostics:
        lines.append("Ruling diagnostics:")
        lines.extend(f"  {diagnostic}" for diagnostic in diagnostics)
    if topology.edges:
        lines.append("Project-reference DAG:")
        lines.extend(f"  {source} -> {target}" for source, target in topology.edges)
    if topology.diagnostics:
        lines.append("Project topology diagnostics:")
        lines.extend(f"  {diagnostic}" for diagnostic in topology.diagnostics)
    return "\n".join(lines) + "\n"


def run_scan(repo: Path, rulings_path: Path, output_format: str) -> int:
    payload = load_rule_data(rulings_path)
    project_policy = payload["project_policy"]
    project_files = tuple(
        row["path"]
        for row in project_policy["projects"]
        if row["metadata_inventory"]
    )
    rows, drops = scan_repository(repo, project_files)
    divergences = find_divergences(rows)
    fingerprints = divergence_fingerprints(divergences)
    rulings = load_rulings(payload)
    diagnostics = ruling_diagnostics(fingerprints, rulings)
    topology = scan_project_topology(repo, project_policy)
    if output_format == "json":
        sys.stdout.write(
            _json_output(rows, drops, divergences, fingerprints, diagnostics, topology)
        )
    else:
        sys.stdout.write(
            _text_output(rows, drops, divergences, fingerprints, diagnostics, topology)
        )
    return 1 if drops or diagnostics or topology.diagnostics else 0


def _fixture_project(
    path: Path,
    project_definitions: str,
    release_definitions: str,
    item_body: str,
    references: tuple[str, ...] = (),
) -> None:
    reference_body = ""
    if references:
        reference_rows = "".join(
            f'<ProjectReference Include="{reference}" />' for reference in references
        )
        reference_body = f"<ItemGroup>{reference_rows}</ItemGroup>"
    path.write_text(
        f"""<?xml version="1.0" encoding="utf-8"?>
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|x64"><Configuration>Debug</Configuration><Platform>x64</Platform></ProjectConfiguration>
    <ProjectConfiguration Include="Release|x64"><Configuration>Release</Configuration><Platform>x64</Platform></ProjectConfiguration>
  </ItemGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">
    <ClCompile>
      <PreprocessorDefinitions>{project_definitions};%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <ExceptionHandling>false</ExceptionHandling><LanguageStandard>stdcpp20</LanguageStandard>
      <FloatingPointModel>Precise</FloatingPointModel><RuntimeLibrary>MultiThreadedDebug</RuntimeLibrary>
      <ForcedIncludeFiles>base.h;%(ForcedIncludeFiles)</ForcedIncludeFiles>
    </ClCompile>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
    <ClCompile>
      <PreprocessorDefinitions>{release_definitions};%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <ExceptionHandling>false</ExceptionHandling><LanguageStandard>stdcpp20</LanguageStandard>
      <FloatingPointModel>Precise</FloatingPointModel><RuntimeLibrary>MultiThreaded</RuntimeLibrary>
      <ForcedIncludeFiles>base.h;%(ForcedIncludeFiles)</ForcedIncludeFiles>
    </ClCompile>
  </ItemDefinitionGroup>
  {item_body}
  {reference_body}
</Project>
""",
        encoding="utf-8",
    )


def self_test() -> int:
    project_files = (
        "FixtureCore.vcxproj",
        "FixtureTests.vcxproj",
        "FixtureMaths.vcxproj",
        "FixturePhysics.vcxproj",
        "FixtureUI.vcxproj",
    )
    with tempfile.TemporaryDirectory() as temporary:
        repo = Path(temporary)
        include_body = """
<ItemGroup>
  <ClCompile Include="Source\\shared.cpp">
    <ForcedIncludeFiles>item.h;%(ForcedIncludeFiles)</ForcedIncludeFiles>
  </ClCompile>
  <ClCompile Include="Source\\drop.cpp">
    <ForcedIncludeFiles>item.h</ForcedIncludeFiles>
  </ClCompile>
</ItemGroup>
<ItemGroup>
  <ClCompile Update="Source\\shared.cpp">
    <PreprocessorDefinitions Condition="'$(Configuration)'=='Release'">UPDATED;%(PreprocessorDefinitions)</PreprocessorDefinitions>
  </ClCompile>
</ItemGroup>
"""
        for index, project_name in enumerate(project_files):
            debug_defs = "COMMON" if index != 1 else "DIVERGENT"
            release_defs = "COMMON"
            _fixture_project(
                repo / project_name,
                debug_defs,
                release_defs,
                include_body,
            )
        rows, drops = scan_repository(repo, project_files)
        if len(rows) != len(project_files) * 2 * 2:
            raise AssertionError("Include and Update fixture rows were not all scanned")
        if len(drops) != len(project_files) * 2:
            raise AssertionError("missing-inheritance fixture was not detected per configuration")
        shared_release = [
            row
            for row in rows
            if row.file == "source/shared.cpp"
            and row.configuration == "Release|x64"
        ]
        if not all(
            row.settings["PreprocessorDefinitions"].startswith("UPDATED;COMMON")
            for row in shared_release
        ):
            raise AssertionError("Update metadata or %(...) inheritance was not applied")
        divergences = find_divergences(rows)
        fingerprints = divergence_fingerprints(divergences)
        key = ("source/shared.cpp", "PreprocessorDefinitions")
        if key not in fingerprints:
            raise AssertionError("planted per-configuration divergence was not reported")
        if not ruling_diagnostics(fingerprints, {}):
            raise AssertionError("unruled planted divergence did not fail")
        exact_rulings = {
            current_key: Ruling(
                file=current_key[0],
                setting=current_key[1],
                fingerprint=fingerprint,
                owner="self-test",
                reason="fixture",
                evidence="fixture",
            )
            for current_key, fingerprint in fingerprints.items()
        }
        if ruling_diagnostics(fingerprints, exact_rulings):
            raise AssertionError("exact current-setting ruling did not pass")
        stale_key = ("source/deleted.cpp", "ExceptionHandling")
        stale = Ruling(
            file=stale_key[0],
            setting=stale_key[1],
            fingerprint="0" * 64,
            owner="self-test",
            reason="fixture",
            evidence="fixture",
        )
        stale_diagnostics = ruling_diagnostics(
            fingerprints, {**exact_rulings, stale_key: stale}
        )
        if not any(item.startswith("STALE-RULING") for item in stale_diagnostics):
            raise AssertionError("stale ruling fixture did not fail")
        project_path = repo / project_files[0]
        project_text = project_path.read_text(encoding="utf-8")
        project_path.write_text(
            project_text.replace(
                "base.h;%(ForcedIncludeFiles)", "base.h"
            ),
            encoding="utf-8",
        )
        _, project_default_drops = scan_project(repo, project_files[0])
        if not any(
            row.file == "<project ClCompile defaults>"
            and row.setting == "ForcedIncludeFiles"
            for row in project_default_drops
        ):
            raise AssertionError("project-default inheritance drop did not fail")
        project_path.write_text(
            project_path.read_text(encoding="utf-8").replace(
                "<ForcedIncludeFiles>base.h</ForcedIncludeFiles>",
                "<ForcedIncludeFiles />",
            ),
            encoding="utf-8",
        )
        _, empty_default_drops = scan_project(repo, project_files[0])
        if not any(
            row.file == "<project ClCompile defaults>"
            and row.setting == "ForcedIncludeFiles"
            and row.override == ""
            for row in empty_default_drops
        ):
            raise AssertionError("empty project-default inheritance drop did not fail")

    with tempfile.TemporaryDirectory() as topology_temporary:
        repo = Path(topology_temporary)
        topology_projects = (
            "Core.vcxproj",
            "Physics.vcxproj",
            "Maths.vcxproj",
            "Tests.vcxproj",
        )
        topology_policy = {
            "production_source_roots": ["Source"],
            "production_source_suffixes": [".cpp"],
            "projects": [
                {
                    "path": "Core.vcxproj",
                    "role": "production",
                    "metadata_inventory": True,
                },
                {
                    "path": "Physics.vcxproj",
                    "role": "production",
                    "metadata_inventory": True,
                },
                {
                    "path": "Maths.vcxproj",
                    "role": "production",
                    "metadata_inventory": True,
                },
                {
                    "path": "Tests.vcxproj",
                    "role": "test",
                    "metadata_inventory": True,
                },
            ],
        }

        def item_body(*sources: str) -> str:
            items = "".join(
                f'<ClCompile Include="{source.replace("/", chr(92))}" />'
                for source in sources
            )
            return f"<ItemGroup>{items}</ItemGroup>"

        sources = ("Source/app.cpp", "Source/physics.cpp", "Source/maths.cpp")
        for source in sources:
            path = repo / source
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("// project topology fixture\n", encoding="utf-8")
        subprocess.run(
            ["git", "init", "--quiet"], cwd=repo, capture_output=True, check=True
        )
        subprocess.run(
            ["git", "add", "--", *sources], cwd=repo, capture_output=True, check=True
        )

        _fixture_project(
            repo / topology_projects[0],
            "COMMON",
            "COMMON",
            item_body("Source/app.cpp"),
            ("Physics.vcxproj",),
        )
        _fixture_project(
            repo / topology_projects[1],
            "COMMON",
            "COMMON",
            item_body("Source/physics.cpp"),
            ("Maths.vcxproj",),
        )
        _fixture_project(
            repo / topology_projects[2],
            "COMMON",
            "COMMON",
            item_body("Source/maths.cpp"),
        )
        _fixture_project(
            repo / topology_projects[3],
            "COMMON",
            "COMMON",
            item_body("Source/app.cpp"),
            ("Core.vcxproj",),
        )
        legal_topology = scan_project_topology(repo, topology_policy)
        if legal_topology.diagnostics:
            raise AssertionError(
                f"legal project DAG and test reuse were rejected: {legal_topology.diagnostics!r}"
            )

        _fixture_project(
            repo / "Unlisted.vcxproj",
            "COMMON",
            "COMMON",
            item_body(),
        )
        unlisted_project = scan_project_topology(repo, topology_policy)
        if not any(
            item == "UNREGISTERED-PROJECT unlisted.vcxproj"
            for item in unlisted_project.diagnostics
        ):
            raise AssertionError("unlisted physical project did not fail closed")
        (repo / "Unlisted.vcxproj").unlink()

        stale_policy = json.loads(json.dumps(topology_policy))
        stale_policy["projects"].append(
            {
                "path": "Deleted.vcxproj",
                "role": "test",
                "metadata_inventory": False,
            }
        )
        stale_project = scan_project_topology(repo, stale_policy)
        if not any(
            item == "STALE-PROJECT-RULE Deleted.vcxproj"
            for item in stale_project.diagnostics
        ):
            raise AssertionError("stale project-policy row did not fail closed")

        _fixture_project(
            repo / topology_projects[2],
            "COMMON",
            "COMMON",
            item_body("Source/maths.cpp"),
            ("Physics.vcxproj",),
        )
        two_node = scan_project_topology(repo, topology_policy)
        if not any(item.startswith("PROJECT-CYCLE") for item in two_node.diagnostics):
            raise AssertionError("two-node project cycle did not fail")

        _fixture_project(
            repo / topology_projects[2],
            "COMMON",
            "COMMON",
            item_body("Source/maths.cpp"),
            ("Core.vcxproj",),
        )
        long_cycle = scan_project_topology(repo, topology_policy)
        if not any(item.startswith("PROJECT-CYCLE") for item in long_cycle.diagnostics):
            raise AssertionError("long project cycle did not fail")

        _fixture_project(
            repo / topology_projects[2],
            "COMMON",
            "COMMON",
            item_body("Source/maths.cpp"),
        )
        _fixture_project(
            repo / topology_projects[1],
            "COMMON",
            "COMMON",
            item_body("Source/physics.cpp", "Source/app.cpp"),
            ("Maths.vcxproj",),
        )
        duplicate_owner = scan_project_topology(repo, topology_policy)
        if not any(
            item.startswith("DUPLICATE-PRODUCTION-OWNER Source/app.cpp".casefold())
            for item in (diagnostic.casefold() for diagnostic in duplicate_owner.diagnostics)
        ):
            raise AssertionError("duplicate production source owner did not fail")

        missing_path = repo / "Source/missing.cpp"
        missing_path.write_text("// missing owner fixture\n", encoding="utf-8")
        subprocess.run(
            ["git", "add", "--", "Source/missing.cpp"],
            cwd=repo,
            capture_output=True,
            check=True,
        )
        missing_owner = scan_project_topology(repo, topology_policy)
        if not any(
            item.startswith("MISSING-PRODUCTION-OWNER source/missing.cpp")
            for item in missing_owner.diagnostics
        ):
            raise AssertionError("missing production source owner did not fail")

        _fixture_project(
            repo / topology_projects[3],
            "COMMON",
            "COMMON",
            item_body("Source/app.cpp"),
            ("Unknown.vcxproj",),
        )
        unknown_reference = scan_project_topology(repo, topology_policy)
        if not any(
            item.startswith("UNREGISTERED-PROJECT-REFERENCE")
            for item in unknown_reference.diagnostics
        ):
            raise AssertionError("unregistered first-party project reference did not fail")
    print("check_build_config_consistency self-test: PASS")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Inventory first-party Visual Studio C++ build metadata."
    )
    parser.add_argument("--repo", type=Path)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--rulings", type=Path, default=DEFAULT_RULINGS)
    parser.add_argument("--format", choices=("text", "json"), default="text")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if args.repo is None:
        parser.error("--repo is required unless --self-test is used")
    repo = args.repo.resolve()
    rulings_path = args.rulings
    if not rulings_path.is_absolute():
        rulings_path = repo / rulings_path
    return run_scan(repo, rulings_path, args.format)


if __name__ == "__main__":
    raise SystemExit(main())
