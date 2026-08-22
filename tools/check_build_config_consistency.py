#!/usr/bin/env python3
"""
File: check_build_config_consistency.py
Purpose:
  Inventory effective C++ build metadata in the five first-party Visual Studio
  projects and fail when shared sources diverge without current owner evidence.

Summary:
  Parses the repository's bounded vcxproj shape, resolves each declared
  configuration, joins exact current-setting rulings, and emits text or JSON
  evidence without invoking MSBuild or changing the worktree.


  MSBuild starts each ClCompile item with project defaults, then applies
  file-specific metadata. List metadata keeps the inherited value only when the
  item contains its matching %(...) token. This checker evaluates that bounded
  project shape for every declared configuration and compares a shared source
  only with the same configuration in its other owning projects.

Glossary:
  Effective metadata: Project ClCompile defaults after matching per-file
    overrides and list inheritance have been applied.
  Shared source: One source path compiled by at least two first-party projects.
  Dropped inheritance: A per-file list override that omits the matching
    %(...) token and therefore replaces, rather than extends, project defaults.
  Current-setting ruling: Owner judgement keyed by source path and metadata
    name, with a digest of every current cross-project variant for that pair.

Invariants:
  - The inventory covers exactly the five repository-root first-party projects.
  - Cross-project comparison pairs only equal Configuration|Platform names;
    Debug-versus-Release differences are normal build modes, not divergence.
  - A ruling digest changes when any project, configuration, or effective value
    in the ruled setting changes.
  - Dropped list inheritance always fails; a ruling cannot hide it.
  - Unsupported MSBuild conditions fail closed instead of silently guessing.

Related:
  - SKULLBONEZ_CORE.vcxproj
  - SKULLBONEZ_TESTS.vcxproj
  - tools/build_config_rulings.json
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import tempfile
import xml.etree.ElementTree as ET
from collections import defaultdict
from dataclasses import asdict, dataclass
from pathlib import Path


PROJECT_FILES = (
    "SKULLBONEZ_CORE.vcxproj",
    "SKULLBONEZ_TESTS.vcxproj",
    "SKULLBONEZ_MATHS.vcxproj",
    "SKULLBONEZ_PHYSICS.vcxproj",
    "SKULLBONEZ_UI.vcxproj",
)
SETTINGS = (
    "PreprocessorDefinitions",
    "ExceptionHandling",
    "LanguageStandard",
    "FloatingPointModel",
    "RuntimeLibrary",
    "ForcedIncludeFiles",
)
LIST_SETTINGS = {"PreprocessorDefinitions", "ForcedIncludeFiles"}
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


def _local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def _normalize_source(path: str) -> str:
    return path.replace("\\", "/").strip().lower()


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
    values = {setting: "" for setting in SETTINGS}
    declared: set[str] = set()
    for group in root:
        if _local_name(group.tag) != "ItemDefinitionGroup":
            continue
        if not condition_matches(group.get("Condition"), configuration, platform):
            continue
        compile_nodes = _children_named(group, "ClCompile")
        for compile_node in compile_nodes:
            for setting in SETTINGS:
                children = _children_named(compile_node, setting)
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
            for setting in SETTINGS:
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


def scan_repository(repo: Path) -> tuple[list[CompileRow], list[DroppedInheritance]]:
    rows: list[CompileRow] = []
    drops: list[DroppedInheritance] = []
    for project_name in PROJECT_FILES:
        project_rows, project_drops = scan_project(repo, project_name)
        rows.extend(project_rows)
        drops.extend(project_drops)
    rows.sort(key=lambda row: (row.file, row.configuration, row.project))
    drops.sort(
        key=lambda row: (row.file, row.setting, row.configuration, row.project)
    )
    return rows, drops


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


def load_rulings(path: Path) -> dict[tuple[str, str], Ruling]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if payload.get("schema_version") != 1:
        raise ValueError("build-config rulings schema_version must be 1")
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
        f"blocking diagnostics: {len(diagnostics) + len(drops)}",
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
    return "\n".join(lines) + "\n"


def run_scan(repo: Path, rulings_path: Path, output_format: str) -> int:
    rows, drops = scan_repository(repo)
    divergences = find_divergences(rows)
    fingerprints = divergence_fingerprints(divergences)
    rulings = load_rulings(rulings_path)
    diagnostics = ruling_diagnostics(fingerprints, rulings)
    if output_format == "json":
        sys.stdout.write(
            _json_output(rows, drops, divergences, fingerprints, diagnostics)
        )
    else:
        sys.stdout.write(
            _text_output(rows, drops, divergences, fingerprints, diagnostics)
        )
    return 1 if drops or diagnostics else 0


def _fixture_project(
    path: Path,
    project_definitions: str,
    release_definitions: str,
    item_body: str,
) -> None:
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
</Project>
""",
        encoding="utf-8",
    )


def self_test() -> int:
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
        for index, project_name in enumerate(PROJECT_FILES):
            debug_defs = "COMMON" if index != 1 else "DIVERGENT"
            release_defs = "COMMON"
            _fixture_project(
                repo / project_name,
                debug_defs,
                release_defs,
                include_body,
            )
        rows, drops = scan_repository(repo)
        if len(rows) != len(PROJECT_FILES) * 2 * 2:
            raise AssertionError("Include and Update fixture rows were not all scanned")
        if len(drops) != len(PROJECT_FILES) * 2:
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
        project_path = repo / PROJECT_FILES[0]
        project_text = project_path.read_text(encoding="utf-8")
        project_path.write_text(
            project_text.replace(
                "base.h;%(ForcedIncludeFiles)", "base.h"
            ),
            encoding="utf-8",
        )
        _, project_default_drops = scan_project(repo, PROJECT_FILES[0])
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
        _, empty_default_drops = scan_project(repo, PROJECT_FILES[0])
        if not any(
            row.file == "<project ClCompile defaults>"
            and row.setting == "ForcedIncludeFiles"
            and row.override == ""
            for row in empty_default_drops
        ):
            raise AssertionError("empty project-default inheritance drop did not fail")
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
