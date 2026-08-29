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
from dataclasses import dataclass
import functools
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
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
class ContextAnalysisResult:
    work_item: SourceDesignWorkItem
    diagnostics: tuple[str, ...]
    tidy_seconds: float
    query_seconds: float


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
    repo: Path, source: Path, query: Path, arguments: tuple[str, ...] | None = None
) -> dict[str, list[str]]:
    findings: dict[str, list[str]] = {}
    for label, matcher in QUERY_COMMANDS.items():
        result = run(
            [str(query), "-c", matcher, str(source), "--", *(arguments or tuple(compile_arguments(repo)))],
            repo,
        )
        output = result.stdout + result.stderr
        if result.returncode != 0:
            raise RuntimeError(f"clang-query failed for {source}:\n{output}")

        if label == "parameter struct unpack":
            locations = collections.Counter(QUERY_ROOT_RE.findall(output))
            grouped = [f"{path}:{line}" for (path, line), count in locations.items() if count >= UNPACK_THRESHOLD]
            if grouped:
                findings[label] = grouped
            continue

        counts = [int(match.group(1)) for match in MATCH_COUNT_RE.finditer(output)]
        if any(counts):
            findings[label] = [line for line in output.splitlines() if 'note: "root" binds here' in line]
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
) -> dict[str, list[str]]:
    started = time.perf_counter()
    measurements.query_process_count += len(QUERY_COMMANDS)
    measurements.peak_workers = max(measurements.peak_workers, 1)
    try:
        return clang_query_findings(repo, source, query, arguments)
    finally:
        measurements.query_seconds += time.perf_counter() - started


def inspect_context(
    repo: Path,
    source: Path,
    tidy: Path,
    query: Path,
    context: CompileContext,
    measurements: SourceDesignMeasurements,
) -> ContextAnalysisResult:
    prefix = f"{context.project} {context.configuration}"
    tidy_started = time.perf_counter()
    tidy_output = measured_tidy_findings(repo, source, tidy, context.arguments, measurements)
    tidy_seconds = time.perf_counter() - tidy_started
    diagnostics = [
        f"{prefix}: {line}" for line in tidy_output.splitlines() if "readability-function-size" in line
    ]

    query_started = time.perf_counter()
    query_findings = measured_query_findings(repo, source, query, context.arguments, measurements)
    query_seconds = time.perf_counter() - query_started
    for label, locations in query_findings.items():
        location_text = ", ".join(locations) if locations else str(source)
        diagnostics.append(f"{prefix}: {label}: {location_text}")

    return ContextAnalysisResult(
        SourceDesignWorkItem(source, context.project, context.configuration, context.arguments),
        tuple(diagnostics),
        tidy_seconds,
        query_seconds,
    )


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
        diagnostics.extend(inspect_context(repo, source, tidy, query, context, active_measurements).diagnostics)
    return diagnostics


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
        query_process_count=20,
        peak_workers=1,
    ).summary()
    if "\n" in summary or not all(
        token in summary
        for token in (
            "mode=fixture",
            "sources=2",
            "contexts=5",
            "tidy_processes=5",
            "query_processes=20",
            "findings=0",
            "infrastructure_errors=0",
        )
    ):
        raise AssertionError(f"source-design summary is incomplete or unbounded: {summary!r}")


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
        "locals.cpp": "int Leftovers(const int& input){const int& alias=input;int m_local=alias;return m_local;}\n",
        "clean.cpp": "struct Values{int a;int b;}; int Read(const Values& v){return v.a+v.b;} int Sum(int a,int b){return a+b;}\n",
        "large.cpp": "int Large(int value) {\n" + "value += 1;\n" * 401 + "return value;\n}\n",
    }
    measurements.source_count = len(fixtures)
    measurements.context_count = len(fixtures)
    context_started = time.perf_counter()
    prove_work_item_enumeration(repo)
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
        local_findings = measured_query_findings(
            repo, paths["locals.cpp"], query, tuple(compile_arguments(repo)), measurements
        )
        if "member-prefixed local" not in local_findings or "pure parameter alias" not in local_findings:
            raise AssertionError("local-code negative controls were not rejected")
        fixture_context = [CompileContext("fixture", "default", tuple(compile_arguments(repo)))]
        if inspect_source(repo, paths["clean.cpp"], tidy, query, fixture_context, measurements):
            raise AssertionError("clean compiler fixture produced a finding")

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
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo = args.repo.resolve()
    measurements = SourceDesignMeasurements("self-test" if args.self_test else "live")
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
            source_contexts = [(source, compile_contexts(repo, source, rows)) for source in sources]
            measurements.source_count = len(sources)
            measurements.context_count = sum(len(contexts) for _, contexts in source_contexts)
            measurements.context_discovery_seconds += time.perf_counter() - context_started

            dead_code_started = time.perf_counter()
            diagnostics = project_dead_code_findings(repo)
            measurements.dead_code_seconds += time.perf_counter() - dead_code_started
            for source, contexts in source_contexts:
                for finding in inspect_source(repo, source, tidy, query, contexts, measurements):
                    diagnostics.append(f"{source.relative_to(repo).as_posix()}: {finding}")
            measurements.finding_count = len(diagnostics)
            if diagnostics:
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
