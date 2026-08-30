"""Compiler-backed checks for changed C++ function shape and local code issues.

Clang-Tidy owns parameter-count and function-size decisions. Clang Query owns
three syntax-tree matches that are awkward to express as compiler warnings:
member-prefixed locals, pure aliases of parameters, and parameter structs that
are immediately copied into four or more locals. The gate checks changed
translation units, so existing reviewed code needs no permission ledger while a
new or edited violation fails locally and in hosted validation.
"""

from __future__ import annotations

import argparse
import collections
import concurrent.futures
from dataclasses import dataclass
import functools
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from typing import Callable
import xml.etree.ElementTree as ET

from check_build_config_consistency import CompileRow, condition_matches, scan_repository


SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".inl"}
FIRST_PARTY_PROJECTS = (
    "SKULLBONEZ_CORE.vcxproj",
    "SKULLBONEZ_MATHS.vcxproj",
    "SKULLBONEZ_PHYSICS.vcxproj",
    "SKULLBONEZ_RENDERING.vcxproj",
    "SKULLBONEZ_TESTS.vcxproj",
    "SKULLBONEZ_UI.vcxproj",
)
APPLICATION_PROJECTS = {"SKULLBONEZ_CORE.vcxproj", "SKULLBONEZ_TESTS.vcxproj"}
UNPACK_THRESHOLD = 4
MAX_SOURCE_DESIGN_JOBS = 4
MATCH_COUNT_RE = re.compile(r"(?m)^(\d+) match(?:es)?\.$")
QUERY_ROOT_RE = re.compile(r'(?m)^(.+?):(\d+):\d+: note: "root" binds here$')

TIDY_CONFIG = (
    "{CheckOptions: {"
    "readability-function-size.LineThreshold: 400, "
    "readability-function-size.StatementThreshold: 4294967295, "
    "readability-function-size.BranchThreshold: 4294967295, "
    "readability-function-size.ParameterThreshold: 11, "
    "readability-function-size.NestingThreshold: 5, "
    "readability-function-size.VariableThreshold: 4294967295}}"
)

QUERY_COMMANDS = {
    "wide declaration": (
        "match functionDecl(hasParameter(11, parmVarDecl()), "
        "isExpansionInMainFile()).bind('wide_declaration')"
    ),
    "member-prefixed local": (
        "match varDecl(hasLocalStorage(), matchesName('(^|::)m_'), "
        "isExpansionInMainFile()).bind('member_prefixed_local')"
    ),
    "pure parameter alias": (
        "match varDecl(hasLocalStorage(), hasType(referenceType()), "
        "hasInitializer(ignoringParenImpCasts(declRefExpr(to(parmVarDecl())))), "
        "unless(hasAncestor(cxxForRangeStmt())), isExpansionInMainFile()).bind('parameter_alias')"
    ),
    "parameter struct unpack": (
        "match functionDecl(isDefinition(), isExpansionInMainFile(), "
        "forEachDescendant(varDecl(hasLocalStorage(), "
        "hasInitializer(ignoringParenImpCasts(memberExpr(hasObjectExpression("
        "ignoringParenImpCasts(declRefExpr(to(parmVarDecl())))))))).bind('unpacked_local')))"
    ),
}


@dataclass(frozen=True)
class CompileContext:
    project: str
    configuration: str
    arguments: tuple[str, ...]


WorkItemIdentity = tuple[str, str, str, tuple[str, ...]]


@dataclass(frozen=True)
class SourceDesignWorkItem:
    source: Path
    project: str
    configuration: str
    arguments: tuple[str, ...]

    def identity(self, repo: Path) -> WorkItemIdentity:
        relative = self.source.resolve().relative_to(repo.resolve()).as_posix()
        return relative.casefold(), self.project, self.configuration, self.arguments


@dataclass(frozen=True)
class ContextDiagnostic:
    rule: str
    location: str
    text: str


@dataclass(frozen=True)
class ContextAnalysisResult:
    work_item: SourceDesignWorkItem
    diagnostics: tuple[ContextDiagnostic, ...] = ()
    tidy_seconds: float = 0.0
    query_seconds: float = 0.0
    tidy_process_count: int = 0
    query_process_count: int = 0
    infrastructure_kind: str | None = None
    infrastructure_error: str | None = None


@dataclass(frozen=True)
class WorkExecutionBatch:
    results: tuple[ContextAnalysisResult, ...]
    peak_workers: int
    peak_in_flight: int
    admitted_count: int


@dataclass
class SourceDesignMeasurements:
    mode: str
    source_count: int = 0
    context_count: int = 0
    tidy_process_count: int = 0
    query_process_count: int = 0
    configured_workers: int = 1
    peak_workers: int = 0
    context_discovery_seconds: float = 0.0
    tidy_seconds: float = 0.0
    query_seconds: float = 0.0
    dead_code_seconds: float = 0.0
    total_seconds: float = 0.0
    finding_count: int = 0
    infrastructure_error_count: int = 0

    def summary(self) -> str:
        return (
            f"source_design_summary mode={self.mode} sources={self.source_count} "
            f"contexts={self.context_count} tidy_processes={self.tidy_process_count} "
            f"query_processes={self.query_process_count} jobs={self.configured_workers} "
            f"peak_workers={self.peak_workers} context_seconds={self.context_discovery_seconds:.3f} "
            f"tidy_seconds={self.tidy_seconds:.3f} query_seconds={self.query_seconds:.3f} "
            f"dead_code_seconds={self.dead_code_seconds:.3f} total_seconds={self.total_seconds:.3f} "
            f"findings={self.finding_count} infrastructure_errors={self.infrastructure_error_count}"
        )


def parse_job_count(value: str) -> int:
    if value.casefold() == "auto":
        return min(max(os.cpu_count() or 1, 1), MAX_SOURCE_DESIGN_JOBS)
    try:
        jobs = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("--jobs must be 'auto' or an integer") from error
    if not 1 <= jobs <= MAX_SOURCE_DESIGN_JOBS:
        raise argparse.ArgumentTypeError(f"--jobs must be between 1 and {MAX_SOURCE_DESIGN_JOBS}")
    return jobs


def llvm_tool(name: str) -> Path:
    """Find the Visual Studio or standalone LLVM binary without pinning a VS release."""
    found = shutil.which(name)
    if found:
        return Path(found)

    program_files = Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
    candidates = sorted(
        program_files.glob(f"Microsoft Visual Studio/*/*/VC/Tools/Llvm/x64/bin/{name}.exe"),
        reverse=True,
    )
    candidates.extend(program_files.glob(f"LLVM/bin/{name}.exe"))
    if candidates:
        return candidates[0]
    raise FileNotFoundError(f"required LLVM tool is unavailable: {name}")


def msvc_tool(name: str) -> Path:
    """Find the native x64 MSVC compiler or linker used by project builds."""
    program_files = Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
    candidates = sorted(
        program_files.glob(f"Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/bin/Hostx64/x64/{name}.exe"),
        reverse=True,
    )
    if candidates:
        return candidates[0]

    found = shutil.which(name)
    if found:
        candidate = Path(found).resolve()
        normalized = candidate.as_posix().casefold()
        if "/vc/tools/msvc/" in normalized and "/bin/hostx64/x64/" in normalized:
            return candidate
    raise FileNotFoundError(f"required MSVC tool is unavailable: {name}")


def compile_arguments(repo: Path) -> list[str]:
    return [
        "-x",
        "c++",
        "-std=c++20",
        "-fms-extensions",
        "-I",
        str(repo / "SkullbonezSource"),
        "-I",
        str(repo / "ThirdPtySource"),
    ]


def _xml_local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def _expand_known_msbuild_properties(value: str, properties: dict[str, str]) -> str:
    expanded = value
    for _ in range(len(properties) + 1):
        previous = expanded
        for name, replacement in properties.items():
            expanded = expanded.replace(f"$({name})", replacement)
        if expanded == previous:
            break
    return expanded


@functools.lru_cache(maxsize=None)
def _repo_local_import_arguments(repo: Path, project_name: str, configuration_name: str) -> tuple[str, ...]:
    """Read literal compile settings from active repo-local .props imports."""
    configuration, platform = configuration_name.split("|", 1)
    project_path = repo / project_name
    project_root = ET.parse(project_path).getroot()
    arguments: list[str] = []

    for import_node in project_root:
        if _xml_local_name(import_node.tag) != "Import" or not condition_matches(
            import_node.get("Condition"), configuration, platform
        ):
            continue
        import_value = import_node.get("Project", "").strip()
        if not import_value or "$(" in import_value:
            continue
        props_path = (project_path.parent / import_value.replace("\\", os.sep)).resolve()
        if not props_path.is_file() or props_path.suffix.casefold() != ".props":
            continue

        props_root = ET.parse(props_path).getroot()
        properties = {
            "MSBuildThisFileDirectory": str(props_path.parent) + os.sep,
            "ProjectDir": str(project_path.parent) + os.sep,
        }
        for group in props_root:
            if _xml_local_name(group.tag) != "PropertyGroup" or group.get("Condition"):
                continue
            for property_node in group:
                if property_node.get("Condition"):
                    continue
                value = _expand_known_msbuild_properties(property_node.text or "", properties)
                if "$(" not in value:
                    properties[_xml_local_name(property_node.tag)] = value

        for group in props_root:
            if _xml_local_name(group.tag) != "ItemDefinitionGroup" or not condition_matches(
                group.get("Condition"), configuration, platform
            ):
                continue
            for compile_node in group:
                if _xml_local_name(compile_node.tag) != "ClCompile":
                    continue
                for setting in compile_node:
                    value = _expand_known_msbuild_properties(setting.text or "", properties)
                    if _xml_local_name(setting.tag) == "PreprocessorDefinitions":
                        for definition in value.split(";"):
                            definition = definition.strip()
                            if definition and "$(" not in definition and not definition.startswith("%("):
                                arguments.append(f"-D{definition}")
                    elif _xml_local_name(setting.tag) == "AdditionalIncludeDirectories":
                        for include_directory in value.split(";"):
                            include_directory = include_directory.strip()
                            if not include_directory or "$(" in include_directory or include_directory.startswith("%("):
                                continue
                            path = Path(include_directory.replace("\\", os.sep))
                            if not path.is_absolute():
                                path = props_path.parent / path
                            arguments.extend(("-I", str(path.resolve())))
    return tuple(arguments)


def _row_arguments(repo: Path, row: CompileRow) -> tuple[str, ...]:
    arguments = compile_arguments(repo)
    language = row.settings["LanguageStandard"].casefold()
    standard = {
        "stdcpp17": "c++17",
        "stdcpp20": "c++20",
        "stdcpplatest": "c++2c",
    }.get(language)
    if standard:
        arguments[arguments.index("-std=c++20")] = f"-std={standard}"

    for definition in row.settings["PreprocessorDefinitions"].split(";"):
        definition = definition.strip()
        if definition and not definition.startswith("%(") and "$(" not in definition:
            arguments.append(f"-D{definition}")
    for forced_include in row.settings["ForcedIncludeFiles"].split(";"):
        forced_include = forced_include.strip()
        if not forced_include or forced_include.startswith("%("):
            continue
        expanded = forced_include.replace("$(ProjectDir)/", "").replace("$(ProjectDir)", "")
        path = Path(expanded.replace("\\", "/"))
        if not path.is_absolute():
            path = repo / path
        arguments.extend(("-include", str(path)))
    for include_directory in row.settings["AdditionalIncludeDirectories"].split(";"):
        include_directory = include_directory.strip()
        if not include_directory or include_directory.startswith("%("):
            continue
        expanded = include_directory.replace("$(ProjectDir)/", "").replace("$(ProjectDir)", "")
        path = Path(expanded.replace("\\", "/"))
        if not path.is_absolute():
            path = repo / path
        arguments.extend(("-I", str(path)))
    if row.settings["ExceptionHandling"].casefold() == "false":
        arguments.append("-fno-exceptions")
    arguments.extend(_repo_local_import_arguments(repo.resolve(), row.project, row.configuration))
    return tuple(arguments)


def compile_contexts(repo: Path, source: Path, rows: list[CompileRow]) -> list[CompileContext]:
    """Use exact source settings or every distinct first-party header context."""
    relative = source.resolve().relative_to(repo.resolve()).as_posix().casefold()
    source_text = source.read_text(encoding="utf-8", errors="replace") if source.is_file() else ""
    development_only_header = source.suffix.lower() in {".h", ".hpp", ".inl"} and (
        "DevelopmentToolsCapability.h" in source_text or "SKULLBONEZ_DEVELOPMENT_TOOLS" in source_text
    )
    exact = [row for row in rows if row.file == relative]
    sibling_rows: list[CompileRow] = []
    if not exact and source.suffix.lower() in {".h", ".hpp", ".inl"}:
        sibling_names = {
            source.with_suffix(suffix).resolve().relative_to(repo.resolve()).as_posix().casefold()
            for suffix in (".cpp", ".c")
        }
        sibling_rows = [row for row in rows if row.file in sibling_names]
    first_party_rows = [
        row
        for row in rows
        if row.project in FIRST_PARTY_PROJECTS
        and (
            row.file.startswith("skullbonezsource/")
            or row.file.startswith("skullboneztests/")
        )
    ]
    # Headers can be compiled by projects other than the physical source-root
    # owner. Use each project's most common effective context per configuration;
    # this preserves project macros and forced includes without treating
    # unrelated per-file metadata as a header requirement. An exact sibling is
    # retained in addition to those project contexts.
    grouped_rows: dict[tuple[str, str], dict[tuple[str, ...], list[CompileRow]]] = collections.defaultdict(
        lambda: collections.defaultdict(list)
    )
    for row in first_party_rows:
        grouped_rows[(row.project, row.configuration)][_row_arguments(repo, row)].append(row)
    representative_rows = [
        min(argument_rows, key=lambda row: row.file)
        for argument_groups in grouped_rows.values()
        for argument_rows in [
            max(argument_groups.values(), key=lambda group: (len(group), tuple(sorted(row.file for row in group))))
        ]
    ]
    candidates = exact or (sibling_rows + representative_rows)
    contexts: dict[tuple[str, str, tuple[str, ...]], CompileContext] = {}
    for row in candidates:
        configuration = row.configuration.split("|", 1)[0]
        if configuration not in {"Debug", "Profile", "Automation"}:
            continue
        arguments = _row_arguments(repo, row)
        if development_only_header and "-DSKULLBONEZ_DEVELOPMENT_TOOLS" not in arguments:
            continue
        key = row.project, configuration, arguments
        contexts[key] = CompileContext(row.project, row.configuration, arguments)
    if not contexts:
        return [CompileContext("fixture", "default", tuple(compile_arguments(repo)))]
    return sorted(contexts.values(), key=lambda context: (context.configuration, context.project))


def enumerate_work_items(
    repo: Path,
    sources: list[Path],
    rows: list[CompileRow],
    context_loader=compile_contexts,
) -> list[SourceDesignWorkItem]:
    """Return immutable source/context identities without launching LLVM."""
    unique: dict[WorkItemIdentity, SourceDesignWorkItem] = {}
    for source in sources:
        resolved_source = source.resolve()
        for context in context_loader(repo, resolved_source, rows):
            work_item = SourceDesignWorkItem(
                resolved_source,
                context.project,
                context.configuration,
                context.arguments,
            )
            unique[work_item.identity(repo)] = work_item

    # Invariant: scheduling may change later, but admission identity and its
    # canonical order do not depend on caller order or duplicated contexts.
    return [unique[identity] for identity in sorted(unique)]


def run(command: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=cwd, text=True, capture_output=True, encoding="utf-8", errors="replace")


def clang_tidy_findings(repo: Path, source: Path, tidy: Path, arguments: tuple[str, ...] | None = None) -> str:
    result = run(
        [
            str(tidy),
            str(source),
            "--checks=-*,readability-function-size",
            f"--config={TIDY_CONFIG}",
            "--exclude-header-filter=.*ThirdPtySource.*",
            "--warnings-as-errors=readability-function-size",
            "--",
            *(arguments or tuple(compile_arguments(repo))),
        ],
        repo,
    )
    output = result.stdout + result.stderr
    if result.returncode not in (0, 1):
        raise RuntimeError(f"clang-tidy failed for {source}:\n{output}")
    if result.returncode == 1 and "readability-function-size" not in output:
        raise RuntimeError(f"clang-tidy could not compile {source}:\n{output}")
    return output if result.returncode == 1 else ""


def clang_query_findings(
    repo: Path,
    source: Path,
    query: Path,
    arguments: tuple[str, ...] | None = None,
    commands: dict[str, str] | None = None,
) -> dict[str, list[str]]:
    active_commands = QUERY_COMMANDS if commands is None else commands
    if tuple(active_commands) != tuple(QUERY_COMMANDS):
        missing = sorted(set(QUERY_COMMANDS) - set(active_commands))
        extra = sorted(set(active_commands) - set(QUERY_COMMANDS))
        raise RuntimeError(f"clang-query command inventory is incomplete: missing={missing} extra={extra}")

    command = [str(query)]
    for matcher in active_commands.values():
        command.extend(("-c", matcher))
    command.extend((str(source), "--", *(arguments or tuple(compile_arguments(repo)))))
    result = run(command, repo)
    output = result.stdout + result.stderr
    if result.returncode != 0:
        raise RuntimeError(f"clang-query failed for {source}:\n{output}")

    return parse_batched_query_output(source, output, tuple(active_commands))


def parse_batched_query_output(
    source: Path,
    output: str,
    expected_labels: tuple[str, ...] = tuple(QUERY_COMMANDS),
) -> dict[str, list[str]]:
    count_matches = list(MATCH_COUNT_RE.finditer(output))
    if len(count_matches) != len(expected_labels):
        raise RuntimeError(
            f"clang-query completed {len(count_matches)}/{len(expected_labels)} rules for {source}: "
            f"expected={list(expected_labels)}"
        )

    findings: dict[str, list[str]] = {}
    section_start = 0
    for label, count_match in zip(expected_labels, count_matches, strict=True):
        section = output[section_start : count_match.end()]
        section_start = count_match.end()
        count = int(count_match.group(1))
        root_lines = [line for line in section.splitlines() if 'note: "root" binds here' in line]
        if len(root_lines) != count:
            raise RuntimeError(
                f"clang-query reported {count} matches but {len(root_lines)} bound locations "
                f"for {label!r} in {source}"
            )

        if label == "parameter struct unpack":
            locations = collections.Counter(QUERY_ROOT_RE.findall(section))
            grouped = [f"{path}:{line}" for (path, line), count in locations.items() if count >= UNPACK_THRESHOLD]
            if grouped:
                findings[label] = grouped
            continue

        if count:
            findings[label] = root_lines
    return findings


def measured_tidy_findings(
    repo: Path,
    source: Path,
    tidy: Path,
    arguments: tuple[str, ...],
    measurements: SourceDesignMeasurements,
) -> str:
    started = time.perf_counter()
    measurements.tidy_process_count += 1
    measurements.peak_workers = max(measurements.peak_workers, 1)
    try:
        return clang_tidy_findings(repo, source, tidy, arguments)
    finally:
        measurements.tidy_seconds += time.perf_counter() - started


def measured_query_findings(
    repo: Path,
    source: Path,
    query: Path,
    arguments: tuple[str, ...],
    measurements: SourceDesignMeasurements,
    commands: dict[str, str] | None = None,
) -> dict[str, list[str]]:
    started = time.perf_counter()
    measurements.query_process_count += 1
    measurements.peak_workers = max(measurements.peak_workers, 1)
    try:
        return clang_query_findings(repo, source, query, arguments, commands)
    finally:
        measurements.query_seconds += time.perf_counter() - started


def analyze_work_item(
    repo: Path,
    tidy: Path,
    query: Path,
    work_item: SourceDesignWorkItem,
) -> ContextAnalysisResult:
    """Run one context without printing or mutating coordinator-owned state."""
    tidy_started = time.perf_counter()
    try:
        tidy_output = clang_tidy_findings(repo, work_item.source, tidy, work_item.arguments)
    except (OSError, RuntimeError) as error:
        return ContextAnalysisResult(
            work_item,
            tidy_seconds=time.perf_counter() - tidy_started,
            tidy_process_count=1,
            infrastructure_kind="tidy",
            infrastructure_error=str(error),
        )
    tidy_seconds = time.perf_counter() - tidy_started
    diagnostics = []
    for line in tidy_output.splitlines():
        if "readability-function-size" not in line:
            continue
        location = re.split(r": (?:warning|error):", line, maxsplit=1)[0]
        diagnostics.append(ContextDiagnostic("readability-function-size", location, line))

    query_started = time.perf_counter()
    try:
        query_findings = clang_query_findings(repo, work_item.source, query, work_item.arguments)
    except (OSError, RuntimeError) as error:
        return ContextAnalysisResult(
            work_item,
            tuple(diagnostics),
            tidy_seconds,
            time.perf_counter() - query_started,
            1,
            1,
            "query",
            str(error),
        )
    query_seconds = time.perf_counter() - query_started
    for label, locations in query_findings.items():
        location_text = ", ".join(locations) if locations else str(work_item.source)
        diagnostics.append(
            ContextDiagnostic(
                label,
                locations[0] if locations else str(work_item.source),
                f"{label}: {location_text}",
            )
        )

    return ContextAnalysisResult(work_item, tuple(diagnostics), tidy_seconds, query_seconds, 1, 1)


def _accumulate_context_measurements(
    measurements: SourceDesignMeasurements,
    result: ContextAnalysisResult,
) -> None:
    measurements.tidy_process_count += result.tidy_process_count
    measurements.query_process_count += result.query_process_count
    measurements.tidy_seconds += result.tidy_seconds
    measurements.query_seconds += result.query_seconds


def inspect_context(
    repo: Path,
    source: Path,
    tidy: Path,
    query: Path,
    context: CompileContext,
    measurements: SourceDesignMeasurements,
) -> ContextAnalysisResult:
    work_item = SourceDesignWorkItem(source, context.project, context.configuration, context.arguments)
    result = analyze_work_item(repo, tidy, query, work_item)
    _accumulate_context_measurements(measurements, result)
    measurements.peak_workers = max(measurements.peak_workers, 1)
    if result.infrastructure_error:
        raise RuntimeError(
            f"{result.infrastructure_kind} infrastructure failure for {source} "
            f"[{context.project} {context.configuration}]: {result.infrastructure_error}"
        )
    return result


def inspect_source(
    repo: Path,
    source: Path,
    tidy: Path,
    query: Path,
    contexts: list[CompileContext],
    measurements: SourceDesignMeasurements | None = None,
) -> list[str]:
    active_measurements = measurements or SourceDesignMeasurements("focused")
    diagnostics: list[str] = []
    for context in contexts:
        result = inspect_context(repo, source, tidy, query, context, active_measurements)
        diagnostics.extend(
            f"{context.project} {context.configuration}: {diagnostic.text}"
            for diagnostic in result.diagnostics
        )
    return diagnostics


def execute_work_items(
    repo: Path,
    work_items: list[SourceDesignWorkItem],
    jobs: int,
    worker: Callable[[SourceDesignWorkItem], ContextAnalysisResult],
) -> WorkExecutionBatch:
    """Run deterministic context batches and stop after a batch reports infrastructure failure."""
    if not 1 <= jobs <= MAX_SOURCE_DESIGN_JOBS:
        raise ValueError(f"jobs must be between 1 and {MAX_SOURCE_DESIGN_JOBS}")
    if not work_items:
        return WorkExecutionBatch((), 0, 0, 0)

    active_workers = 0
    peak_workers = 0
    peak_in_flight = 0
    admitted_count = 0
    state_lock = threading.Lock()

    def invoke(work_item: SourceDesignWorkItem) -> ContextAnalysisResult:
        nonlocal active_workers, peak_workers
        with state_lock:
            active_workers += 1
            peak_workers = max(peak_workers, active_workers)
        try:
            return worker(work_item)
        finally:
            with state_lock:
                active_workers -= 1

    results: list[ContextAnalysisResult] = []
    next_index = 0
    first_failure = ""
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs, thread_name_prefix="source-design") as executor:
        while not first_failure and next_index < len(work_items):
            batch_items = work_items[next_index : next_index + jobs]
            next_index += len(batch_items)
            active = {executor.submit(invoke, work_item): work_item for work_item in batch_items}
            admitted_count += len(active)
            peak_in_flight = max(peak_in_flight, len(active))

            # Hazard: replenishing a partially completed batch lets scheduling
            # decide which later contexts run before an active failure arrives.
            # The coordinator admits the next prefix only after this whole
            # prefix completes, so failures always leave the same unexamined tail.
            concurrent.futures.wait(active)
            for future in sorted(active, key=lambda item: active[item].identity(repo)):
                work_item = active[future]
                try:
                    result = future.result()
                    if result.work_item != work_item:
                        raise RuntimeError("worker returned a result for a different work item")
                except ChildProcessError as error:
                    result = ContextAnalysisResult(
                        work_item,
                        infrastructure_kind="child",
                        infrastructure_error=f"{type(error).__name__}: {error}",
                    )
                except Exception as error:
                    result = ContextAnalysisResult(
                        work_item,
                        infrastructure_kind="worker",
                        infrastructure_error=f"{type(error).__name__}: {error}",
                    )
                results.append(result)
                if result.infrastructure_error and not first_failure:
                    relative = work_item.source.resolve().relative_to(repo.resolve()).as_posix()
                    first_failure = f"{relative} [{work_item.project} {work_item.configuration}]"

    # Hazard: silently dropping the tail would turn an incomplete scan into a
    # plausible partial result. Name every context that was never admitted.
    if first_failure:
        for work_item in work_items[next_index:]:
            results.append(
                ContextAnalysisResult(
                    work_item,
                    infrastructure_kind="not-admitted",
                    infrastructure_error=f"not admitted after infrastructure failure in {first_failure}",
                )
            )

    ordered = tuple(sorted(results, key=lambda result: result.work_item.identity(repo)))
    return WorkExecutionBatch(ordered, peak_workers, peak_in_flight, admitted_count)


def render_context_diagnostics(repo: Path, results: tuple[ContextAnalysisResult, ...]) -> list[str]:
    sortable = []
    for result in results:
        relative = result.work_item.source.resolve().relative_to(repo.resolve()).as_posix()
        for diagnostic in result.diagnostics:
            key = (
                relative.casefold(),
                result.work_item.project,
                result.work_item.configuration,
                diagnostic.rule,
                diagnostic.location.casefold(),
                diagnostic.text,
            )
            text = (
                f"{relative}: {result.work_item.project} "
                f"{result.work_item.configuration}: {diagnostic.text}"
            )
            sortable.append((key, text))
    return [text for _, text in sorted(sortable)]


def render_infrastructure_errors(repo: Path, results: tuple[ContextAnalysisResult, ...]) -> list[str]:
    errors = []
    for result in results:
        if not result.infrastructure_error:
            continue
        relative = result.work_item.source.resolve().relative_to(repo.resolve()).as_posix()
        errors.append(
            f"{relative}: {result.work_item.project} {result.work_item.configuration}: "
            f"{result.infrastructure_kind}: {result.infrastructure_error}"
        )
    return errors


def classify_results(results: tuple[ContextAnalysisResult, ...]) -> int:
    if any(result.infrastructure_error for result in results):
        return 2
    if any(result.diagnostics for result in results):
        return 1
    return 0


def git_output(repo: Path, arguments: list[str]) -> list[str]:
    result = run(["git", "-C", str(repo), *arguments], repo)
    if result.returncode != 0:
        return []
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def changed_sources(repo: Path) -> list[Path]:
    base = os.environ.get("SKORE_SIZE_DIFF_BASE", "").strip()
    if not base:
        merge_base = git_output(repo, ["merge-base", "HEAD", "origin/main"])
        base = merge_base[0] if merge_base else ""
    if not base:
        parent = git_output(repo, ["rev-parse", "HEAD^"])
        base = parent[0] if parent else ""

    names: set[str] = set()
    if base:
        names.update(git_output(repo, ["diff", "--name-only", "--diff-filter=ACMR", f"{base}...HEAD", "--", "SkullbonezSource"]))
    names.update(git_output(repo, ["diff", "--name-only", "--diff-filter=ACMR", "HEAD", "--", "SkullbonezSource"]))
    return sorted(
        (repo / name for name in names if Path(name).suffix.lower() in SOURCE_SUFFIXES and (repo / name).is_file()),
        key=lambda path: path.as_posix().lower(),
    )


def project_dead_code_findings(repo: Path) -> list[str]:
    """Evaluate effective compile and link dead-code settings for every optimized row."""
    findings: set[str] = set()
    rows, _ = scan_repository(repo, FIRST_PARTY_PROJECTS)
    for row in rows:
        if row.configuration.startswith("Debug|"):
            continue
        if row.settings["FunctionLevelLinking"].casefold() != "true":
            findings.add(
                f"{row.project}: {row.configuration} {row.file} does not effectively enable function-level linking"
            )

    for project_name in APPLICATION_PROJECTS:
        root = ET.parse(repo / project_name).getroot()
        configurations = []
        for node in root.iter():
            if node.tag.rsplit("}", 1)[-1] == "ProjectConfiguration" and "|" in node.get("Include", ""):
                configurations.append(tuple(node.get("Include", "").split("|", 1)))
        for configuration, platform in configurations:
            if configuration == "Debug":
                continue
            effective = ""
            for group in root:
                if group.tag.rsplit("}", 1)[-1] != "ItemDefinitionGroup":
                    continue
                if not condition_matches(group.get("Condition"), configuration, platform):
                    continue
                for owner in group:
                    if owner.tag.rsplit("}", 1)[-1] != "Link":
                        continue
                    for setting in owner:
                        if setting.tag.rsplit("}", 1)[-1] == "OptimizeReferences" and condition_matches(
                            setting.get("Condition"), configuration, platform
                        ):
                            effective = (setting.text or "").strip()
            if effective.casefold() != "true":
                findings.add(f"{project_name}: {configuration}|{platform} does not effectively enable /OPT:REF")
    return sorted(findings)


def prove_dead_code_elimination(compiler: Path, linker: Path) -> None:
    """Compile one unused external function and prove /OPT:REF removes its section."""
    with tempfile.TemporaryDirectory(prefix="skore-dead-code-") as temporary:
        root = Path(temporary)
        source = root / "dead_code.cpp"
        obj = root / "dead_code.obj"
        executable = root / "dead_code.exe"
        map_file = root / "dead_code.map"
        source.write_text(
            'extern "C" __declspec(dllexport,noinline) int Used(){return 7;}\n'
            'extern "C" __declspec(noinline) int Unused(){return 9;}\n'
            'extern "C" int main(){return Used();}\n',
            encoding="utf-8",
            newline="\n",
        )
        compile_result = run(
            [str(compiler), "/nologo", "/O2", "/Gy", "/c", str(source), f"/Fo{obj}"],
            root,
        )
        if compile_result.returncode != 0:
            raise AssertionError(f"dead-code fixture compilation failed:\n{compile_result.stdout}{compile_result.stderr}")
        link_result = run(
            [
                str(linker),
                str(obj),
                "/entry:main",
                "/subsystem:console",
                "/nodefaultlib",
                "/opt:ref",
                f"/map:{map_file}",
                f"/out:{executable}",
            ],
            root,
        )
        if link_result.returncode != 0:
            raise AssertionError(f"dead-code fixture link failed:\n{link_result.stdout}{link_result.stderr}")
        mapping = map_file.read_text(encoding="utf-8", errors="replace")
        if "Unused" in mapping or "Used" not in mapping:
            relevant = "\n".join(line for line in mapping.splitlines() if "Used" in line or "Unused" in line)
            raise AssertionError(f"linker negative control did not remove only the unreferenced function:\n{relevant}")


def prove_project_dead_code_evaluation(repo: Path) -> None:
    """Prove late and per-file MSBuild overrides defeat the effective-setting check."""
    namespace = "http://schemas.microsoft.com/developer/msbuild/2003"
    ET.register_namespace("", namespace)
    with tempfile.TemporaryDirectory(prefix="skore-project-dead-code-") as temporary:
        root = Path(temporary)
        for project_name in FIRST_PARTY_PROJECTS:
            shutil.copy2(repo / project_name, root / project_name)

        physics_tree = ET.parse(root / "SKULLBONEZ_PHYSICS.vcxproj")
        physics_root = physics_tree.getroot()
        compile_item = next(
            node
            for node in physics_root.iter()
            if node.tag.rsplit("}", 1)[-1] == "ClCompile" and node.get("Include")
        )
        override = ET.SubElement(compile_item, f"{{{namespace}}}FunctionLevelLinking")
        override.set("Condition", "'$(Configuration)|$(Platform)'=='Profile|x64'")
        override.text = "false"
        physics_tree.write(root / "SKULLBONEZ_PHYSICS.vcxproj", encoding="utf-8", xml_declaration=True)

        core_tree = ET.parse(root / "SKULLBONEZ_CORE.vcxproj")
        core_root = core_tree.getroot()
        group = ET.SubElement(core_root, f"{{{namespace}}}ItemDefinitionGroup")
        group.set("Condition", "'$(Configuration)|$(Platform)'=='Profile|x64'")
        link = ET.SubElement(group, f"{{{namespace}}}Link")
        ET.SubElement(link, f"{{{namespace}}}OptimizeReferences").text = "false"
        core_tree.write(root / "SKULLBONEZ_CORE.vcxproj", encoding="utf-8", xml_declaration=True)

        findings = project_dead_code_findings(root)
        if not any("SKULLBONEZ_PHYSICS.vcxproj: Profile|x64" in finding for finding in findings):
            raise AssertionError("per-file /Gy override negative control was not rejected")
        if "SKULLBONEZ_CORE.vcxproj: Profile|x64 does not effectively enable /OPT:REF" not in findings:
            raise AssertionError("late /OPT:REF override negative control was not rejected")


def prove_compile_contexts(repo: Path) -> None:
    """Pin source-root ownership and the production include path that exposed context drift."""
    rows, _ = scan_repository(repo, FIRST_PARTY_PROJECTS)
    texture = repo / "SkullbonezSource/Assets/TextureCollection.cpp"
    contexts = compile_contexts(repo, texture, rows)
    configurations = {context.configuration for context in contexts}
    if configurations != {"Automation|x64", "Debug|x64", "Profile|x64"}:
        raise AssertionError(f"Assets source contexts are incomplete: {sorted(configurations)}")
    expected_include = str(repo / "ThirdPtySource/stb")
    if not all(expected_include in context.arguments for context in contexts):
        raise AssertionError("Assets source contexts omit the effective stb include directory")
    imgui = repo / "SkullbonezSource/Rendering/DX12/Dx12ImGuiRendererOwner.cpp"
    imgui_contexts = compile_contexts(repo, imgui, rows)
    imgui_header_contexts = compile_contexts(repo, imgui.with_suffix(".h"), rows)
    expected_imgui_include = str(repo / "ThirdPtySource/imgui")
    if not all(
        "-DSKULLBONEZ_DEVELOPMENT_TOOLS" in context.arguments and expected_imgui_include in context.arguments
        for context in imgui_contexts + imgui_header_contexts
    ):
        raise AssertionError("development source contexts omit imported ImGui compile settings")
    for root_name in ("Assets", "Gameplay"):
        header = repo / f"SkullbonezSource/{root_name}/ContextProbe.h"
        header_contexts = compile_contexts(repo, header, rows)
        if any(context.project == "fixture" for context in header_contexts):
            raise AssertionError(f"{root_name} headers fell back to the synthetic fixture context")
    reserve_header = repo / "SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h"
    reserve_contexts = compile_contexts(repo, reserve_header, rows)
    if any(
        "DevelopmentToolsCapability.h" in argument
        for context in reserve_contexts
        for argument in context.arguments
    ):
        raise AssertionError("header context leaked unrelated third-party per-file metadata")

    sibling_row = next(
        row
        for row in rows
        if row.project == "SKULLBONEZ_CORE.vcxproj" and row.configuration == "Profile|x64"
    )
    consumer_row = next(
        row
        for row in rows
        if row.project == "SKULLBONEZ_TESTS.vcxproj" and row.configuration == "Profile|x64"
    )
    probe_header = repo / "SkullbonezSource/Core/HeaderContextProbe.h"
    probe_rows = [
        CompileRow(
            file="skullbonezsource/core/headercontextprobe.cpp",
            project=sibling_row.project,
            configuration=sibling_row.configuration,
            settings={
                **sibling_row.settings,
                "PreprocessorDefinitions": sibling_row.settings["PreprocessorDefinitions"] + ";SIBLING_CONTEXT=1",
            },
        ),
        CompileRow(
            file="skullbonezsource/core/headercontextconsumer.cpp",
            project=consumer_row.project,
            configuration=consumer_row.configuration,
            settings={
                **consumer_row.settings,
                "PreprocessorDefinitions": consumer_row.settings["PreprocessorDefinitions"] + ";CONSUMER_CONTEXT=1",
            },
        ),
    ]
    probe_contexts = compile_contexts(repo, probe_header, probe_rows)
    probe_definitions = {
        argument
        for context in probe_contexts
        for argument in context.arguments
        if argument in {"-DSIBLING_CONTEXT=1", "-DCONSUMER_CONTEXT=1"}
    }
    if probe_definitions != {"-DSIBLING_CONTEXT=1", "-DCONSUMER_CONTEXT=1"}:
        raise AssertionError(
            "header contexts dropped a non-sibling first-party consumer: "
            f"actual={sorted(probe_definitions)}"
        )
    if {context.project for context in probe_contexts} != {
        "SKULLBONEZ_CORE.vcxproj",
        "SKULLBONEZ_TESTS.vcxproj",
    }:
        raise AssertionError("header contexts did not preserve distinct first-party consumer projects")


def prove_work_item_enumeration(repo: Path) -> None:
    """Pin source/context identity, ordering, and deduplication without LLVM."""
    header = repo / "SkullbonezSource/Core/SourceDesignWorkItemProbe.h"
    source = repo / "SkullbonezSource/Core/SourceDesignWorkItemProbe.cpp"
    common_arguments = tuple(compile_arguments(repo))
    contexts = {
        header.name: [
            CompileContext("SKULLBONEZ_TESTS.vcxproj", "Profile|x64", common_arguments + ("-DHEADER_TEST",)),
            CompileContext("SKULLBONEZ_CORE.vcxproj", "Debug|x64", common_arguments + ("-DHEADER_CORE",)),
            CompileContext("SKULLBONEZ_TESTS.vcxproj", "Profile|x64", common_arguments + ("-DHEADER_TEST",)),
        ],
        source.name: [
            CompileContext("SKULLBONEZ_CORE.vcxproj", "Profile|x64", common_arguments + ("-DSOURCE_B",)),
            CompileContext("SKULLBONEZ_CORE.vcxproj", "Automation|x64", common_arguments + ("-DSOURCE_AUTO",)),
            CompileContext("SKULLBONEZ_CORE.vcxproj", "Profile|x64", common_arguments + ("-DSOURCE_A",)),
            CompileContext("SKULLBONEZ_CORE.vcxproj", "Profile|x64", common_arguments + ("-DSOURCE_A",)),
        ],
    }

    def fixture_contexts(_repo: Path, fixture_source: Path, _rows: list[CompileRow]) -> list[CompileContext]:
        return contexts[fixture_source.name]

    work_items = enumerate_work_items(repo, [source, header, source], [], fixture_contexts)
    expected = sorted(
        {
            (
                "skullbonezsource/core/sourcedesignworkitemprobe.h",
                "SKULLBONEZ_TESTS.vcxproj",
                "Profile|x64",
                common_arguments + ("-DHEADER_TEST",),
            ),
            (
                "skullbonezsource/core/sourcedesignworkitemprobe.h",
                "SKULLBONEZ_CORE.vcxproj",
                "Debug|x64",
                common_arguments + ("-DHEADER_CORE",),
            ),
            (
                "skullbonezsource/core/sourcedesignworkitemprobe.cpp",
                "SKULLBONEZ_CORE.vcxproj",
                "Automation|x64",
                common_arguments + ("-DSOURCE_AUTO",),
            ),
            (
                "skullbonezsource/core/sourcedesignworkitemprobe.cpp",
                "SKULLBONEZ_CORE.vcxproj",
                "Profile|x64",
                common_arguments + ("-DSOURCE_A",),
            ),
            (
                "skullbonezsource/core/sourcedesignworkitemprobe.cpp",
                "SKULLBONEZ_CORE.vcxproj",
                "Profile|x64",
                common_arguments + ("-DSOURCE_B",),
            ),
        }
    )
    actual = [work_item.identity(repo) for work_item in work_items]
    if actual != expected:
        raise AssertionError(f"work-item identity ordering or deduplication changed: actual={actual}")
    try:
        work_items[0].project = "mutated"  # type: ignore[misc]
    except AttributeError:
        pass
    else:
        raise AssertionError("work-item identity is mutable")

    summary = SourceDesignMeasurements(
        "fixture",
        source_count=2,
        context_count=5,
        tidy_process_count=5,
        query_process_count=5,
        peak_workers=1,
    ).summary()
    if "\n" in summary or not all(
        token in summary
        for token in (
            "mode=fixture",
            "sources=2",
            "contexts=5",
            "tidy_processes=5",
            "query_processes=5",
            "findings=0",
            "infrastructure_errors=0",
        )
    ):
        raise AssertionError(f"source-design summary is incomplete or unbounded: {summary!r}")


def prove_bounded_concurrency(repo: Path) -> None:
    """Exercise bounded admission, overlap, ordering, and failure classes without LLVM."""
    automatic_jobs = parse_job_count("auto")
    if not 1 <= automatic_jobs <= MAX_SOURCE_DESIGN_JOBS:
        raise AssertionError(f"automatic job selection escaped its cap: {automatic_jobs}")
    for invalid in ("0", "-1", "nonnumeric", str(MAX_SOURCE_DESIGN_JOBS + 1)):
        try:
            parse_job_count(invalid)
        except argparse.ArgumentTypeError:
            pass
        else:
            raise AssertionError(f"invalid job value was accepted: {invalid}")

    arguments = tuple(compile_arguments(repo))
    work_items = [
        SourceDesignWorkItem(
            repo / f"SkullbonezSource/Core/ConcurrencyProbe{index}.cpp",
            "SKULLBONEZ_CORE.vcxproj",
            "Profile|x64",
            arguments + (f"-DPROBE={index}",),
        )
        for index in range(6)
    ]

    def clean_worker(work_item: SourceDesignWorkItem) -> ContextAnalysisResult:
        return ContextAnalysisResult(work_item, tidy_process_count=1, query_process_count=1)

    clean_outputs = []
    for jobs in (1, 2, automatic_jobs):
        batch = execute_work_items(repo, work_items, jobs, clean_worker)
        clean_outputs.append((render_context_diagnostics(repo, batch.results), classify_results(batch.results)))
    if any(output != ([], 0) for output in clean_outputs):
        raise AssertionError(f"clean concurrency control changed result classification: {clean_outputs}")

    def policy_worker(reverse: bool) -> Callable[[SourceDesignWorkItem], ContextAnalysisResult]:
        def inspect(work_item: SourceDesignWorkItem) -> ContextAnalysisResult:
            index = int(work_item.source.stem.removeprefix("ConcurrencyProbe"))
            time.sleep((5 - index if reverse else index) * 0.002)
            diagnostics = ()
            if index in {1, 4}:
                location = f"{work_item.source}:1"
                diagnostics = (ContextDiagnostic("fixture policy", location, f"fixture policy: {location}"),)
            return ContextAnalysisResult(
                work_item,
                diagnostics,
                tidy_process_count=1,
                query_process_count=1,
            )

        return inspect

    planted_outputs = []
    for jobs, reverse in ((1, False), (2, True), (automatic_jobs, False)):
        batch = execute_work_items(repo, work_items, jobs, policy_worker(reverse))
        planted_outputs.append((render_context_diagnostics(repo, batch.results), classify_results(batch.results)))
    if planted_outputs[0] != planted_outputs[1] or planted_outputs[0] != planted_outputs[2]:
        raise AssertionError("policy diagnostics changed with worker count or completion order")
    if planted_outputs[0][1] != 1:
        raise AssertionError("planted policy finding did not retain policy exit classification")

    active_children = 0
    peak_children = 0
    child_lock = threading.Lock()

    def delayed_worker(work_item: SourceDesignWorkItem) -> ContextAnalysisResult:
        nonlocal active_children, peak_children
        with child_lock:
            active_children += 1
            peak_children = max(peak_children, active_children)
        try:
            time.sleep(0.025)
            return clean_worker(work_item)
        finally:
            with child_lock:
                active_children -= 1

    overlap = execute_work_items(repo, work_items, 2, delayed_worker)
    if peak_children != 2 or overlap.peak_workers != 2 or overlap.peak_in_flight != 2:
        raise AssertionError(
            "bounded overlap control did not observe exactly two active children: "
            f"child_peak={peak_children} worker_peak={overlap.peak_workers} "
            f"in_flight_peak={overlap.peak_in_flight}"
        )

    def admission_failure(
        failure_delay: float, success_delay: float, started: set[int], started_lock: threading.Lock
    ) -> Callable[[SourceDesignWorkItem], ContextAnalysisResult]:
        def inspect(work_item: SourceDesignWorkItem) -> ContextAnalysisResult:
            index = int(work_item.source.stem.removeprefix("ConcurrencyProbe"))
            with started_lock:
                started.add(index)
            if index == 0:
                time.sleep(failure_delay)
                return ContextAnalysisResult(
                    work_item,
                    infrastructure_kind="tidy",
                    infrastructure_error="planted parse failure",
                )
            time.sleep(success_delay)
            return clean_worker(work_item)

        return inspect

    admission_runs = []
    for failure_delay, success_delay in ((0.005, 0.050), (0.050, 0.005)):
        started: set[int] = set()
        started_lock = threading.Lock()
        stopped = execute_work_items(
            repo,
            work_items,
            2,
            admission_failure(failure_delay, success_delay, started, started_lock),
        )
        skipped_identities = tuple(
            result.work_item.identity(repo)
            for result in stopped.results
            if result.infrastructure_kind == "not-admitted"
        )
        process_counts = (
            sum(result.tidy_process_count for result in stopped.results),
            sum(result.query_process_count for result in stopped.results),
        )
        admission_runs.append(
            (
                render_infrastructure_errors(repo, stopped.results),
                classify_results(stopped.results),
                stopped.admitted_count,
                tuple(sorted(started)),
                skipped_identities,
                process_counts,
            )
        )
    if admission_runs[0] != admission_runs[1]:
        raise AssertionError(f"infrastructure admission changed with completion order: {admission_runs}")
    errors, classification, admitted, started, skipped, process_counts = admission_runs[0]
    if classification != 2 or admitted != 2 or started != (0, 1) or len(skipped) != 4 or process_counts != (1, 1):
        raise AssertionError(
            "infrastructure failure did not stop deterministic prefix admission: "
            f"errors={len(errors)} admitted={admitted} started={started} "
            f"skipped={len(skipped)} processes={process_counts}"
        )

    failure_kinds = ("tidy", "query", "child", "worker")

    def failure_worker(reverse: bool) -> Callable[[SourceDesignWorkItem], ContextAnalysisResult]:
        def inspect(work_item: SourceDesignWorkItem) -> ContextAnalysisResult:
            index = int(work_item.source.stem.removeprefix("ConcurrencyProbe"))
            time.sleep((3 - index if reverse else index) * 0.003)
            if index == 2:
                raise ChildProcessError("planted child crash")
            if index == 3:
                raise RuntimeError("planted worker exception")
            return ContextAnalysisResult(
                work_item,
                infrastructure_kind=failure_kinds[index],
                infrastructure_error=f"planted {failure_kinds[index]} failure",
            )

        return inspect

    failure_items = work_items[:4]
    forward = execute_work_items(repo, failure_items, 4, failure_worker(False))
    reverse = execute_work_items(repo, failure_items, 4, failure_worker(True))
    forward_errors = render_infrastructure_errors(repo, forward.results)
    reverse_errors = render_infrastructure_errors(repo, reverse.results)
    if forward_errors != reverse_errors or classify_results(forward.results) != 2:
        raise AssertionError("infrastructure diagnostics changed with completion order")
    observed_kinds = {result.infrastructure_kind for result in forward.results}
    if observed_kinds != set(failure_kinds):
        raise AssertionError(f"infrastructure classifications are incomplete: {sorted(observed_kinds)}")


def self_test(
    repo: Path,
    tidy: Path,
    query: Path,
    clang_cl: Path,
    linker: Path,
    measurements: SourceDesignMeasurements,
) -> None:
    fixtures = {
        "wide.cpp": (
            "struct BodyStore{}; struct ColliderStore{}; struct Settings{};\n"
            "struct PhysicsBroadphaseStage { static int Run(\n"
            "    BodyStore& bodyStore,\n"
            "    ColliderStore& colliderStore,\n"
            "    const Settings& settings,\n"
            "    int pairs,\n"
            "    int ranges,\n"
            "    int activeSlots,\n"
            "    int diagnostics,\n"
            "    float dt,\n"
            "    float minimumContactOffset,\n"
            "    float contactOffsetFraction,\n"
            "    int& clampedOffsetCount,\n"
            "    int& stackFallbackCount) { return 0; } };\n"
        ),
        "nested.cpp": "int Helper(int v){if(v){if(v){if(v){if(v){if(v){if(v){return v;}}}}}}return 0;} int Root(int v){return Helper(v);}\n",
        "struct.cpp": "struct Values{int a;int b;int c;int d;}; int Read(Values v){const int a=v.a;const int b=v.b;const int c=v.c;const int d=v.d;return a+b+c+d;}\n",
        "member.cpp": "int MemberPrefix(){int m_local=1;return m_local;}\n",
        "alias.cpp": "int Alias(const int& input){const int& alias=input;return alias;}\n",
        "multi.cpp": "int Leftovers(const int& input){const int& alias=input;int m_local=alias;return m_local;}\n",
        "clean.cpp": "struct Values{int a;int b;}; int Read(const Values& v){return v.a+v.b;} int Sum(int a,int b){return a+b;}\n",
        "large.cpp": "int Large(int value) {\n" + "value += 1;\n" * 401 + "return value;\n}\n",
    }
    measurements.source_count = len(fixtures)
    measurements.context_count = len(fixtures)
    context_started = time.perf_counter()
    prove_work_item_enumeration(repo)
    prove_bounded_concurrency(repo)
    measurements.context_discovery_seconds += time.perf_counter() - context_started
    with tempfile.TemporaryDirectory(prefix="skore-source-design-") as temporary:
        root = Path(temporary)
        paths: dict[str, Path] = {}
        for name, text in fixtures.items():
            path = root / name
            path.write_text(text, encoding="utf-8", newline="\n")
            paths[name] = path

        wide = measured_tidy_findings(repo, paths["wide.cpp"], tidy, tuple(compile_arguments(repo)), measurements)
        if "12 parameters" not in wide:
            raise AssertionError("12-parameter negative control was not rejected")
        if "wide declaration" not in measured_query_findings(
            repo, paths["wide.cpp"], query, tuple(compile_arguments(repo)), measurements
        ):
            raise AssertionError("12-parameter syntax-tree negative control was not rejected")
        nested = measured_tidy_findings(
            repo, paths["nested.cpp"], tidy, tuple(compile_arguments(repo)), measurements
        )
        if "nesting level" not in nested:
            raise AssertionError("nested once-called helper negative control was not rejected")
        large = measured_tidy_findings(repo, paths["large.cpp"], tidy, tuple(compile_arguments(repo)), measurements)
        if "lines including whitespace and comments (threshold 400)" not in large:
            raise AssertionError("400-line function negative control was not rejected")

        struct_findings = measured_query_findings(
            repo, paths["struct.cpp"], query, tuple(compile_arguments(repo)), measurements
        )
        if "parameter struct unpack" not in struct_findings:
            raise AssertionError("parameter-struct entry-unpack negative control was not rejected")
        member_findings = measured_query_findings(
            repo, paths["member.cpp"], query, tuple(compile_arguments(repo)), measurements
        )
        if set(member_findings) != {"member-prefixed local"}:
            raise AssertionError(f"member-local batching lost rule attribution: {member_findings}")
        alias_findings = measured_query_findings(
            repo, paths["alias.cpp"], query, tuple(compile_arguments(repo)), measurements
        )
        if set(alias_findings) != {"pure parameter alias"}:
            raise AssertionError(f"parameter-alias batching lost rule attribution: {alias_findings}")
        multi_findings = measured_query_findings(
            repo, paths["multi.cpp"], query, tuple(compile_arguments(repo)), measurements
        )
        if set(multi_findings) != {"member-prefixed local", "pure parameter alias"}:
            raise AssertionError(f"multi-rule batching lost rule attribution: {multi_findings}")
        fixture_context = [CompileContext("fixture", "default", tuple(compile_arguments(repo)))]
        if inspect_source(repo, paths["clean.cpp"], tidy, query, fixture_context, measurements):
            raise AssertionError("clean compiler fixture produced a finding")

        incomplete_commands = dict(list(QUERY_COMMANDS.items())[:-1])
        try:
            clang_query_findings(
                repo,
                paths["clean.cpp"],
                query,
                tuple(compile_arguments(repo)),
                incomplete_commands,
            )
        except RuntimeError as error:
            if "command inventory is incomplete" not in str(error):
                raise AssertionError(f"missing-rule control reported the wrong error: {error}") from error
        else:
            raise AssertionError("missing Query rule was accepted")

        partial_output = "\n".join("0 matches." for _ in range(len(QUERY_COMMANDS) - 1))
        try:
            parse_batched_query_output(paths["clean.cpp"], partial_output)
        except RuntimeError as error:
            if "3/4 rules" not in str(error):
                raise AssertionError(f"partial-command control reported the wrong error: {error}") from error
        else:
            raise AssertionError("partial Query execution reported a clean result")

        truncated_locations = (
            f'{paths["clean.cpp"]}:1:1: note: "root" binds here\n'
            "2 matches.\n0 matches.\n0 matches.\n0 matches."
        )
        try:
            parse_batched_query_output(paths["clean.cpp"], truncated_locations)
        except RuntimeError as error:
            if "2 matches but 1 bound locations" not in str(error):
                raise AssertionError(f"truncated-location control reported the wrong error: {error}") from error
        else:
            raise AssertionError("truncated Query locations reported a complete result")

        malformed_commands = dict(QUERY_COMMANDS)
        malformed_commands["wide declaration"] = "match functionDecl("
        try:
            measured_query_findings(
                repo,
                paths["clean.cpp"],
                query,
                tuple(compile_arguments(repo)),
                measurements,
                malformed_commands,
            )
        except RuntimeError as error:
            if "clang-query failed" not in str(error):
                raise AssertionError(f"malformed-command control reported the wrong error: {error}") from error
        else:
            raise AssertionError("malformed Query command reported a clean result")

    dead_code_started = time.perf_counter()
    prove_dead_code_elimination(clang_cl, linker)
    if project_dead_code_findings(repo):
        raise AssertionError("repository project dead-code settings do not pass their positive control")
    prove_project_dead_code_evaluation(repo)
    measurements.dead_code_seconds += time.perf_counter() - dead_code_started
    context_started = time.perf_counter()
    prove_compile_contexts(repo)
    measurements.context_discovery_seconds += time.perf_counter() - context_started
    print("PASS: compiler-backed source-design and linker negative controls")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--files", nargs="*", type=Path)
    parser.add_argument(
        "--jobs",
        type=parse_job_count,
        default="auto",
        help=f"whole-context workers: auto or 1-{MAX_SOURCE_DESIGN_JOBS} (default: auto)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo = args.repo.resolve()
    measurements = SourceDesignMeasurements(
        "self-test" if args.self_test else "live",
        configured_workers=args.jobs,
    )
    total_started = time.perf_counter()
    exit_code = 2
    try:
        tidy = llvm_tool("clang-tidy")
        query = llvm_tool("clang-query")
        clang_cl = msvc_tool("cl")
        linker = msvc_tool("link")
        if args.self_test:
            self_test(repo, tidy, query, clang_cl, linker, measurements)
            exit_code = 0
        else:
            context_started = time.perf_counter()
            sources = (
                [path if path.is_absolute() else repo / path for path in args.files]
                if args.files
                else changed_sources(repo)
            )
            rows, _ = scan_repository(repo, FIRST_PARTY_PROJECTS)
            work_items = enumerate_work_items(repo, sources, rows)
            measurements.source_count = len(sources)
            measurements.context_count = len(work_items)
            measurements.context_discovery_seconds += time.perf_counter() - context_started

            dead_code_started = time.perf_counter()
            diagnostics = project_dead_code_findings(repo)
            measurements.dead_code_seconds += time.perf_counter() - dead_code_started
            worker = functools.partial(analyze_work_item, repo, tidy, query)
            batch = execute_work_items(repo, work_items, args.jobs, worker)
            measurements.peak_workers = batch.peak_workers
            for result in batch.results:
                _accumulate_context_measurements(measurements, result)
            diagnostics.extend(render_context_diagnostics(repo, batch.results))
            infrastructure_errors = render_infrastructure_errors(repo, batch.results)
            measurements.finding_count = len(diagnostics)
            measurements.infrastructure_error_count = len(infrastructure_errors)
            if infrastructure_errors:
                print("ERROR: source-design infrastructure failures:", file=sys.stderr)
                for diagnostic in infrastructure_errors:
                    print(f"  {diagnostic}", file=sys.stderr)
                if diagnostics:
                    print("POLICY FINDINGS OBSERVED BEFORE INFRASTRUCTURE FAILURE:", file=sys.stderr)
                    for diagnostic in diagnostics:
                        print(f"  {diagnostic}", file=sys.stderr)
                exit_code = 2
            elif diagnostics:
                print("FAIL: changed C++ source has design findings:", file=sys.stderr)
                for diagnostic in diagnostics:
                    print(f"  {diagnostic}", file=sys.stderr)
                exit_code = 1
            else:
                print(f"PASS: compiler-backed source-design check files={len(sources)}")
                exit_code = 0

    except (FileNotFoundError, RuntimeError, AssertionError) as error:
        measurements.infrastructure_error_count += 1
        print(f"ERROR: {error}", file=sys.stderr)
        exit_code = 2
    finally:
        measurements.total_seconds = time.perf_counter() - total_started
        print(measurements.summary())
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
