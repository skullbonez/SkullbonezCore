"""
File: tools/validate_native_diagnostics.py
Purpose:
  Runs the bounded native lifetime-safety lane: AddressSanitizer over the main
  CPU test project and MSVC static analysis over the engine maths library.

Summary:
  The normal lane imports temporary MSBuild settings from ignored TestOutput,
  so diagnostic binaries never replace the developer's Debug/Profile outputs.
  An explicit proof mode also creates a short-lived faulty project, verifies
  that AddressSanitizer identifies its heap use-after-free, and deletes it.

Glossary:
  ASan (AddressSanitizer): Compiler instrumentation that reports invalid memory
    accesses with allocation and free-site evidence.
  Static-analysis baseline: Exact warning exceptions that may pass the lane;
    each exception must identify its owner and removal condition.
  Proof fixture: Intentionally faulty source generated only for a detector
    self-test and never compiled by a normal repository build.

Invariants:
  - Normal builds cannot reach the proof fixture; it exists only in a temporary
    directory while --prove-asan-fixture is running.
  - Diagnostic artifacts remain under ignored TestOutput/validation and logs
    are bounded before they are written.
  - Suppressions match one exact path and warning code, carry complete ownership
    metadata, and fail when stale; blanket warning disables are not accepted.

Related:
  - tools/native_diagnostics_suppressions.json
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
from typing import Iterable


REPO = Path(__file__).resolve().parents[1]
ARTIFACT_ROOT = REPO / "TestOutput" / "validation" / "native_diagnostics"
SUPPRESSION_PATH = REPO / "tools" / "native_diagnostics_suppressions.json"
VSWHERE = Path(r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe")
MAX_LOG_CHARS = 240_000
LOG_HEAD_CHARS = 160_000
# Why: a failing lane must print the failure itself, not an arbitrary window of
# whatever ran last. Position-based excerpting cannot do that here. A passing run
# emits ~2 MB of [runtime-reserve] reservation diagnostics from the engine under
# test, so a head/tail slice of any affordable size lands in reservation noise
# whichever end it takes. Select failure blocks by content instead, and keep the
# positional budget below only as a fallback when no marker is recognized.
FAILURE_EXCERPT_CHARS = 24_000
FAILURE_EXCERPT_HEAD_CHARS = 16_000
FAILURE_CONTEXT_BEFORE = 8
FAILURE_CONTEXT_AFTER = 12
# doctest prints "<file>(<line>): ERROR:" (or FATAL ERROR) per failing assertion
# and a trailing "[doctest]" summary; ASan prints its own "==pid==ERROR:" banner.
# The lane fails on either, so both must survive into the printed excerpt.
FAILURE_MARKER_PATTERN = re.compile(
    r"\)\s*:\s*(?:FATAL )?ERROR:|==\d+==ERROR:|^\[doctest\]|^Assertion failed"
)
ASAN_FAILURE_SIGNATURES = (
    "ERROR: AddressSanitizer:",
    "SUMMARY: AddressSanitizer:",
    "AddressSanitizer:DEADLYSIGNAL",
)
WARNING_PATTERN = re.compile(
    r"^(?P<path>.+?)\((?P<line>\d+)(?:,\d+)?\):\s+warning\s+(?P<code>C\d+):\s*(?P<message>.*)$",
    re.MULTILINE | re.IGNORECASE,
)
GENERIC_WARNING_LINE_PATTERN = re.compile(
    r"^.*\bwarning(?:\s+[A-Z]+\d+)?\s*:.*$",
    re.IGNORECASE,
)


class NativeDiagnosticsError(RuntimeError):
    """Expected validation failure with a concise user-facing message."""


class ToolMissingError(NativeDiagnosticsError):
    """Required Visual Studio tooling is not installed on this machine."""


def bounded_text(text: str) -> str:
    if len(text) <= MAX_LOG_CHARS:
        return text
    omitted = len(text) - MAX_LOG_CHARS
    while True:
        marker = f"\n\n[validate_native_diagnostics omitted {omitted} characters]\n\n"
        tail_chars = MAX_LOG_CHARS - LOG_HEAD_CHARS - len(marker)
        actual_omitted = len(text) - LOG_HEAD_CHARS - tail_chars
        if actual_omitted == omitted:
            break
        omitted = actual_omitted
    # Invariant: the marker consumes part of the fixed log budget; otherwise
    # a file advertised as capped at MAX_LOG_CHARS would exceed that cap.
    return text[:LOG_HEAD_CHARS] + marker + text[-tail_chars:]


def positional_excerpt(text: str) -> str:
    # Fallback only. Used when no failure marker is recognized at all, so there
    # is nothing better than a head and a tail to show.
    if len(text) <= FAILURE_EXCERPT_CHARS:
        return text
    tail_chars = FAILURE_EXCERPT_CHARS - FAILURE_EXCERPT_HEAD_CHARS
    omitted = len(text) - FAILURE_EXCERPT_HEAD_CHARS - tail_chars
    marker = f"\n\n[validate_native_diagnostics omitted {omitted} characters]\n\n"
    return text[:FAILURE_EXCERPT_HEAD_CHARS] + marker + text[-tail_chars:]


def failure_excerpt(text: str) -> str:
    # Invariant: every recognized failure block and the trailing summary survive,
    # regardless of how much unrelated output the run produced around them.
    if len(text) <= FAILURE_EXCERPT_CHARS:
        return text
    lines = text.splitlines()
    marked = [index for index, line in enumerate(lines) if FAILURE_MARKER_PATTERN.search(line)]
    if not marked:
        return positional_excerpt(text)

    kept: set[int] = set()
    for index in marked:
        start = max(0, index - FAILURE_CONTEXT_BEFORE)
        end = min(len(lines), index + FAILURE_CONTEXT_AFTER + 1)
        kept.update(range(start, end))

    selected: list[str] = []
    previous: int | None = None
    for index in sorted(kept):
        if previous is not None and index != previous + 1:
            selected.append(f"[validate_native_diagnostics skipped {index - previous - 1} lines]")
        selected.append(lines[index])
        previous = index

    excerpt = "\n".join(selected)
    # Hazard: a run that fails thousands of assertions can still overflow the
    # budget. Bound the assembled excerpt rather than the raw output, so the
    # first failures and the summary are what survive the second trim.
    return excerpt if len(excerpt) <= FAILURE_EXCERPT_CHARS else positional_excerpt(excerpt)


def has_asan_failure(text: str) -> bool:
    # Invariant: a nonzero test exit is not sanitizer evidence. Only a runtime
    # signature may classify an ordinary doctest failure as an ASan finding.
    return any(signature in text for signature in ASAN_FAILURE_SIGNATURES)


def display_path(path: Path) -> str:
    try:
        return path.resolve().relative_to(REPO).as_posix()
    except ValueError:
        return str(path)


def run_logged(
    label: str,
    command: list[str],
    log_path: Path,
    *,
    cwd: Path = REPO,
    env: dict[str, str] | None = None,
    timeout_seconds: int = 600,
) -> tuple[int, str, float]:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    started = time.perf_counter()
    try:
        completed = subprocess.run(
            command,
            cwd=cwd,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout_seconds,
            check=False,
        )
        output = completed.stdout or ""
        return_code = completed.returncode
    except subprocess.TimeoutExpired as exc:
        partial = exc.stdout or ""
        if isinstance(partial, bytes):
            partial = partial.decode("utf-8", errors="replace")
        output = partial + f"\nTIMEOUT: {label} exceeded {timeout_seconds}s.\n"
        return_code = 124
    elapsed = time.perf_counter() - started
    bounded = bounded_text(output)
    log_path.write_text(bounded, encoding="utf-8")
    print(f"  {label}: exit {return_code}, {elapsed:.3f}s, log {display_path(log_path)}")
    # Invariant: only the persisted log is bounded. Detector signatures and
    # static-analysis warnings must be evaluated against the complete output or
    # a large build could hide a diagnostic in the omitted middle window.
    return return_code, output, elapsed


def find_msbuild() -> Path:
    if not VSWHERE.exists():
        raise ToolMissingError(f"vswhere not found: {VSWHERE}")
    completed = subprocess.run(
        [
            str(VSWHERE),
            "-latest",
            "-products",
            "*",
            "-requires",
            "Microsoft.Component.MSBuild",
            "-find",
            r"MSBuild\**\Bin\MSBuild.exe",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    candidates = [Path(line.strip()) for line in completed.stdout.splitlines() if line.strip()]
    if completed.returncode != 0 or not candidates or not candidates[0].exists():
        raise ToolMissingError("MSBuild with the Visual C++ workload was not found.")
    return candidates[0].resolve()


def installed_toolsets(msbuild: Path) -> set[str]:
    vc_targets_root = msbuild.parent.parent.parent / "Microsoft" / "VC"
    return {
        props.parent.name
        for props in vc_targets_root.glob("v*/Platforms/x64/PlatformToolsets/*/Toolset.props")
    }


def asan_runtime_environment(msbuild: Path) -> dict[str, str]:
    install_root = msbuild.parents[3]
    candidates = sorted(
        install_root.glob(
            "VC/Tools/MSVC/*/bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll"
        ),
        reverse=True,
    )
    if not candidates:
        raise ToolMissingError(
            "The MSVC AddressSanitizer x64 runtime is not installed. Add the "
            "C++ AddressSanitizer component in Visual Studio Installer."
        )
    environment = os.environ.copy()
    environment["PATH"] = str(candidates[0].parent) + os.pathsep + environment.get("PATH", "")
    environment["ASAN_OPTIONS"] = "halt_on_error=1:abort_on_error=1:allocator_may_return_null=0"
    return environment


def choose_toolset(msbuild: Path, project: Path) -> str:
    project_text = project.read_text(encoding="utf-8")
    requested = re.findall(r"<PlatformToolset>(v\d+)</PlatformToolset>", project_text)
    installed = installed_toolsets(msbuild)
    for candidate in requested:
        if candidate in installed:
            return candidate
    for fallback in ("v145", "v143"):
        if fallback in installed:
            print(f"  INFO: {project.name} toolset unavailable; using installed {fallback}.")
            return fallback
    raise ToolMissingError(
        "No supported x64 MSVC platform toolset was found (expected v145 or v143)."
    )


def msbuild_base(
    msbuild: Path,
    project: Path,
    toolset: str,
    target: str = "Rebuild",
    *,
    warnings_as_errors: bool = True,
) -> list[str]:
    command = [
        str(msbuild),
        str(project),
        f"/t:{target}",
        "/p:Configuration=Profile",
        "/p:Platform=x64",
        f"/p:PlatformToolset={toolset}",
        "/nologo",
        "/v:minimal",
    ]
    if warnings_as_errors:
        command.append("/warnaserror")
    return command


def write_asan_props(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        """<?xml version=\"1.0\" encoding=\"utf-8\"?>
<Project xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">
  <PropertyGroup>
    <OutDir>$(SkoreNativeDiagnosticsRoot)\\asan\\bin\\$(MSBuildProjectName)\\</OutDir>
    <IntDir>$(SkoreNativeDiagnosticsRoot)\\asan\\obj\\$(MSBuildProjectName)\\</IntDir>
    <LinkIncremental>false</LinkIncremental>
  </PropertyGroup>
  <ItemDefinitionGroup>
    <ClCompile>
      <EnableASAN>true</EnableASAN>
      <BasicRuntimeChecks>Default</BasicRuntimeChecks>
      <DebugInformationFormat>ProgramDatabase</DebugInformationFormat>
      <!-- Why: production Profile logging is a no-op. The test-only define is
           imported by the test project and every referenced library so the
           ASan lane exercises one ODR-consistent thread-safe EngineLog type. -->
      <PreprocessorDefinitions>SKULLBONEZ_TEST_ENGINE_LOG;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <AdditionalOptions>/fsanitize=address %(AdditionalOptions)</AdditionalOptions>
    </ClCompile>
    <Link>
      <LinkIncremental>false</LinkIncremental>
    </Link>
  </ItemDefinitionGroup>
</Project>
""",
        encoding="utf-8",
    )


def write_static_analysis_props(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        """<?xml version=\"1.0\" encoding=\"utf-8\"?>
<Project xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">
  <PropertyGroup>
    <OutDir>$(SkoreNativeDiagnosticsRoot)\\static_analysis\\bin\\$(MSBuildProjectName)\\</OutDir>
    <IntDir>$(SkoreNativeDiagnosticsRoot)\\static_analysis\\obj\\$(MSBuildProjectName)\\</IntDir>
  </PropertyGroup>
  <ItemDefinitionGroup>
    <ClCompile>
      <EnablePREfast>true</EnablePREfast>
      <TreatWarningAsError>false</TreatWarningAsError>
      <AdditionalOptions>/analyze %(AdditionalOptions)</AdditionalOptions>
    </ClCompile>
  </ItemDefinitionGroup>
</Project>
""",
        encoding="utf-8",
    )


def load_suppressions() -> list[dict[str, str]]:
    try:
        payload = json.loads(SUPPRESSION_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise NativeDiagnosticsError(f"Cannot read suppression baseline: {exc}") from exc
    if payload.get("schemaVersion") != 1 or not isinstance(payload.get("suppressions"), list):
        raise NativeDiagnosticsError("Suppression baseline must use schemaVersion 1 and a suppressions list.")

    required = ("path", "code", "owner", "reason", "deletionCondition", "reviewEvidence")
    validated: list[dict[str, str]] = []
    for index, raw in enumerate(payload["suppressions"]):
        if not isinstance(raw, dict) or any(not str(raw.get(field, "")).strip() for field in required):
            raise NativeDiagnosticsError(
                f"Suppression {index} must name path, code, owner, reason, deletionCondition, and reviewEvidence."
            )
        path = str(raw["path"]).replace("\\", "/")
        code = str(raw["code"]).upper()
        if any(token in path for token in ("*", "?", "[")) or not re.fullmatch(r"C\d+", code):
            raise NativeDiagnosticsError(
                f"Suppression {index} must use an exact path and one exact MSVC warning code."
            )
        entry = {field: str(raw[field]).strip() for field in required}
        entry["path"] = path
        entry["code"] = code
        validated.append(entry)
    return validated


def repo_relative_diagnostic_path(text: str) -> str:
    candidate = Path(text.strip())
    try:
        return candidate.resolve().relative_to(REPO).as_posix()
    except (OSError, ValueError):
        return text.strip().replace("\\", "/")


def parsed_warnings(output: str) -> list[dict[str, str]]:
    unique: dict[tuple[str, str, str, str], dict[str, str]] = {}
    for match in WARNING_PATTERN.finditer(output):
        warning = {
            "path": repo_relative_diagnostic_path(match.group("path")),
            "line": match.group("line"),
            "code": match.group("code").upper(),
            "message": match.group("message").strip(),
        }
        key = (warning["path"].casefold(), warning["line"], warning["code"], warning["message"])
        unique[key] = warning
    return list(unique.values())


def reject_unparsed_warning_lines(output: str) -> None:
    # Hazard: governed suppressions require an exact source path and C-code.
    # Any differently formatted compiler, linker, or MSBuild warning must fail
    # closed instead of disappearing outside the suppression matcher.
    unexpected = [
        line.strip()
        for line in output.splitlines()
        if GENERIC_WARNING_LINE_PATTERN.match(line) and not WARNING_PATTERN.match(line)
    ]
    if unexpected:
        details = "\n".join(f"    {line}" for line in unexpected[:20])
        raise NativeDiagnosticsError(
            f"Static analysis emitted {len(unexpected)} unclassified warning line(s):\n{details}"
        )


def run_self_tests() -> list[str]:
    failures: list[str] = []
    native_line = r"C:\repo\Math.cpp(12): warning C6001: sample warning"
    warnings = parsed_warnings(native_line)
    if len(warnings) != 1 or warnings[0]["code"] != "C6001" or warnings[0]["line"] != "12":
        failures.append("native warning parser did not retain path/line/code")
    try:
        reject_unparsed_warning_lines(native_line)
    except NativeDiagnosticsError:
        failures.append("classified native warning was rejected as unclassified")

    try:
        reject_unparsed_warning_lines("LINK : warning LNK4099: missing debug symbols")
        failures.append("unclassified linker warning did not fail closed")
    except NativeDiagnosticsError:
        pass

    oversized = "a" * (MAX_LOG_CHARS + 100)
    bounded = bounded_text(oversized)
    marker_match = re.search(r"omitted (\d+) characters", bounded)
    if (
        len(bounded) != MAX_LOG_CHARS
        or len(bounded) >= len(oversized)
        or marker_match is None
        or int(marker_match.group(1)) <= 100
    ):
        failures.append("bounded log did not retain the expected head/tail marker")

    short_failure = "FAILED: one case\n[doctest] Status: FAILURE!"
    if failure_excerpt(short_failure) != short_failure:
        failures.append("failure excerpt truncated output that fits the budget")

    if has_asan_failure(short_failure):
        failures.append("doctest failure was misclassified as an AddressSanitizer finding")
    if not has_asan_failure("==123==ERROR: AddressSanitizer: heap-use-after-free"):
        failures.append("AddressSanitizer finding signature was not recognized")

    # The real shape this lane produces: a failing assertion buried in megabytes
    # of engine reservation logging. Any position-based excerpt lands in the
    # noise, so this pins content selection rather than a head/tail budget.
    noise = "\n".join(
        f"[runtime-reserve] growth owner=Store.field{index} phase=scene_load status=granted"
        for index in range(40_000)
    )
    buried = "\n".join(
        [
            noise,
            "D:\\repo\\SkullbonezTests\\TestExample.cpp(42):",
            "TEST CASE:  Example case that fails",
            "",
            "D:\\repo\\SkullbonezTests\\TestExample.cpp(47): ERROR: CHECK( a == b ) is NOT correct!",
            "  values: CHECK( 1 == 2 )",
            noise,
            "[doctest] test cases: 527 | 526 passed | 1 failed | 0 skipped",
            "[doctest] Status: FAILURE!",
        ]
    )
    excerpt = failure_excerpt(buried)
    if len(excerpt) > FAILURE_EXCERPT_CHARS:
        failures.append("failure excerpt exceeded its budget")
    for required in (
        "TEST CASE:  Example case that fails",
        "ERROR: CHECK( a == b ) is NOT correct!",
        "values: CHECK( 1 == 2 )",
        "[doctest] Status: FAILURE!",
    ):
        if required not in excerpt:
            failures.append(f"failure excerpt dropped required content: {required}")
    # The fixture carries three marker lines (one ERROR, two summary), so the
    # retained noise cannot exceed three context windows however large the input
    # grows. This pins selection, not a hand-tuned line count.
    marker_count = 3
    if excerpt.count("[runtime-reserve]") > marker_count * (
        FAILURE_CONTEXT_BEFORE + FAILURE_CONTEXT_AFTER + 1
    ):
        failures.append("failure excerpt retained unbounded surrounding noise")

    # No recognizable marker at all still has to show something.
    unmarked = "z" * (FAILURE_EXCERPT_CHARS + 500)
    if "omitted" not in failure_excerpt(unmarked):
        failures.append("unmarked oversized output did not fall back to a bounded excerpt")
    return failures


def evaluate_warnings(warnings: Iterable[dict[str, str]], suppressions: list[dict[str, str]]) -> None:
    used: set[int] = set()
    unsuppressed: list[dict[str, str]] = []
    warning_list = list(warnings)
    for warning in warning_list:
        match_index = next(
            (
                index
                for index, suppression in enumerate(suppressions)
                if suppression["code"] == warning["code"]
                and suppression["path"].casefold() == warning["path"].casefold()
            ),
            None,
        )
        if match_index is None:
            unsuppressed.append(warning)
        else:
            used.add(match_index)
            owner = suppressions[match_index]["owner"]
            print(f"  SUPPRESSED: {warning['path']}:{warning['line']} {warning['code']} owner={owner}")

    stale = [suppressions[index] for index in range(len(suppressions)) if index not in used]
    if stale:
        details = ", ".join(f"{entry['path']} {entry['code']}" for entry in stale)
        raise NativeDiagnosticsError(f"Static-analysis suppression baseline contains stale entries: {details}")
    if unsuppressed:
        details = "\n".join(
            f"    {item['path']}:{item['line']} {item['code']}: {item['message']}"
            for item in unsuppressed[:20]
        )
        raise NativeDiagnosticsError(
            f"Static analysis reported {len(unsuppressed)} unsuppressed warning(s):\n{details}"
        )
    print(
        f"  Static-analysis baseline: {len(warning_list)} warning(s), "
        f"{len(suppressions)} governed suppression(s)."
    )


def run_asan_tests(msbuild: Path, *, report_durations: bool = False) -> float:
    project = REPO / "SKULLBONEZ_TESTS.vcxproj"
    toolset = choose_toolset(msbuild, project)
    props = ARTIFACT_ROOT / "asan" / "asan_settings.props"
    write_asan_props(props)
    command = msbuild_base(msbuild, project, toolset)
    command.extend(
        [
            f"/p:ForceImportBeforeCppTargets={props}",
            f"/p:SkoreNativeDiagnosticsRoot={ARTIFACT_ROOT}",
        ]
    )
    build_code, build_output, build_elapsed = run_logged(
        "ASan CPU-test build",
        command,
        ARTIFACT_ROOT / "asan" / "build.log",
    )
    if build_code != 0:
        print(build_output[-6000:])
        raise NativeDiagnosticsError("AddressSanitizer CPU-test build failed.")

    executable = ARTIFACT_ROOT / "asan" / "bin" / "SKULLBONEZ_TESTS" / "SKULLBONEZ_TESTS.exe"
    if not executable.exists():
        raise NativeDiagnosticsError(f"AddressSanitizer test executable was not produced: {executable}")
    asan_env = asan_runtime_environment(msbuild)
    # Hazard: --duration=true makes doctest print a header and timing block for
    # every test case, passing or not. At 527 cases that is roughly 180 KB of
    # pass-noise per run, which pushed the actual failure out of every bounded
    # excerpt and left four consecutive red scheduled runs undiagnosable. Keep it
    # opt-in: without it doctest reports only failing assertions and the summary.
    test_command = [str(executable)]
    if report_durations:
        test_command.append("--duration=true")
    run_code, run_output, run_elapsed = run_logged(
        "ASan CPU-test run",
        test_command,
        ARTIFACT_ROOT / "asan" / "run.log",
        env=asan_env,
        timeout_seconds=300,
    )
    asan_failure = has_asan_failure(run_output)
    if run_code != 0 or asan_failure:
        print(failure_excerpt(run_output))
        if asan_failure:
            raise NativeDiagnosticsError("AddressSanitizer detected a memory error in CPU tests.")
        raise NativeDiagnosticsError("ASan-instrumented CPU tests failed.")
    return build_elapsed + run_elapsed


def fixture_project_text(toolset: str) -> str:
    return f"""<?xml version=\"1.0\" encoding=\"utf-8\"?>
<Project DefaultTargets=\"Build\"
         ToolsVersion=\"Current\"
         xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">
  <ItemGroup Label=\"ProjectConfigurations\">
    <ProjectConfiguration Include=\"Profile|x64\">
      <Configuration>Profile</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label=\"Globals\">
    <ProjectGuid>{{C24C13C3-8D12-41D3-92EA-27B6381565DB}}</ProjectGuid>
    <Keyword>Win32Proj</Keyword>
  </PropertyGroup>
  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.Default.props\" />
  <PropertyGroup Label=\"Configuration\">
    <ConfigurationType>Application</ConfigurationType>
    <PlatformToolset>{toolset}</PlatformToolset>
    <CharacterSet>MultiByte</CharacterSet>
  </PropertyGroup>
  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.props\" />
  <PropertyGroup>
    <OutDir>$(ProjectDir)bin\\</OutDir>
    <IntDir>$(ProjectDir)obj\\</IntDir>
    <LinkIncremental>false</LinkIncremental>
  </PropertyGroup>
  <ItemDefinitionGroup>
    <ClCompile>
      <Optimization>Disabled</Optimization>
      <WarningLevel>Level4</WarningLevel>
      <TreatWarningAsError>true</TreatWarningAsError>
      <RuntimeLibrary>MultiThreaded</RuntimeLibrary>
      <DebugInformationFormat>ProgramDatabase</DebugInformationFormat>
      <LanguageStandard>stdcpp17</LanguageStandard>
      <EnableASAN>true</EnableASAN>
      <BasicRuntimeChecks>Default</BasicRuntimeChecks>
      <AdditionalOptions>/fsanitize=address %(AdditionalOptions)</AdditionalOptions>
    </ClCompile>
    <Link>
      <OutputFile>$(OutDir)asan_uaf_fixture.exe</OutputFile>
      <GenerateDebugInformation>true</GenerateDebugInformation>
      <SubSystem>Console</SubSystem>
      <LinkIncremental>false</LinkIncremental>
    </Link>
  </ItemDefinitionGroup>
  <ItemGroup>
    <ClCompile Include=\"asan_uaf_fixture.cpp\" />
  </ItemGroup>
  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\" />
</Project>
"""


def run_asan_proof_fixture(msbuild: Path) -> float:
    reference_project = REPO / "SKULLBONEZ_TESTS.vcxproj"
    toolset = choose_toolset(msbuild, reference_project)
    proof_root = ARTIFACT_ROOT / "asan_fixture"
    proof_root.mkdir(parents=True, exist_ok=True)
    started = time.perf_counter()
    # Hazard: the fixture deliberately dereferences freed memory. Keeping it in
    # a temporary directory makes accidental compilation from normal builds
    # structurally impossible.
    with tempfile.TemporaryDirectory(prefix="uaf_fixture_", dir=proof_root) as temporary:
        fixture_dir = Path(temporary)
        source = fixture_dir / "asan_uaf_fixture.cpp"
        project = fixture_dir / "asan_uaf_fixture.vcxproj"
        source.write_text(
            """#include <cstdint>

__declspec(noinline) int observe_freed_value(const int* value)
{
    return *value;
}

int main()
{
    int* value = new int(0x1234);
    delete value;
    volatile int observed = observe_freed_value(value);
    return observed == 0x1234 ? 0 : 0;
}
""",
            encoding="utf-8",
        )
        project.write_text(fixture_project_text(toolset), encoding="utf-8")
        build_code, build_output, _ = run_logged(
            "ASan injected-fixture build",
            msbuild_base(msbuild, project, toolset),
            proof_root / "build.log",
            cwd=fixture_dir,
        )
        if build_code != 0:
            print(build_output[-6000:])
            raise NativeDiagnosticsError("Injected AddressSanitizer fixture did not build.")

        executable = fixture_dir / "bin" / "asan_uaf_fixture.exe"
        asan_env = asan_runtime_environment(msbuild)
        run_code, run_output, _ = run_logged(
            "ASan injected-fixture run (failure expected)",
            [str(executable)],
            proof_root / "run.log",
            cwd=fixture_dir,
            env=asan_env,
            timeout_seconds=60,
        )
        signature = "heap-use-after-free"
        if run_code == 0 or signature not in run_output.lower():
            print(run_output[-6000:])
            raise NativeDiagnosticsError(
                "AddressSanitizer did not reject the injected fixture with a heap-use-after-free report."
            )
        print(f"  PASS: injected fixture exited {run_code} and reported {signature}.")
    return time.perf_counter() - started


def run_static_analysis(msbuild: Path) -> float:
    project = REPO / "SKULLBONEZ_MATHS.vcxproj"
    toolset = choose_toolset(msbuild, project)
    props = ARTIFACT_ROOT / "static_analysis" / "static_analysis_settings.props"
    write_static_analysis_props(props)
    # Why: static-analysis findings are governed below by exact source/code
    # rows. Leaving MSBuild's blanket warnings-as-errors switch enabled would
    # make an approved row impossible to use even after it matched.
    command = msbuild_base(msbuild, project, toolset, warnings_as_errors=False)
    command.extend(
        [
            f"/p:ForceImportBeforeCppTargets={props}",
            f"/p:SkoreNativeDiagnosticsRoot={ARTIFACT_ROOT}",
        ]
    )
    analysis_started = time.time()
    return_code, output, elapsed = run_logged(
        "MSVC static analysis (SKULLBONEZ_MATHS)",
        command,
        ARTIFACT_ROOT / "static_analysis" / "build.log",
    )
    reject_unparsed_warning_lines(output)
    warnings = parsed_warnings(output)
    evaluate_warnings(warnings, load_suppressions())
    if return_code != 0:
        print(output[-6000:])
        raise NativeDiagnosticsError("MSVC static-analysis build failed.")

    # MSVC writes analysis sidecars next to object files. Their presence proves
    # this was not merely a normal compile with an ignored property name.
    object_root = ARTIFACT_ROOT / "static_analysis" / "obj" / "SKULLBONEZ_MATHS"
    evidence = [
        path
        for path in object_root.rglob("*.nativecodeanalysis.xml")
        if path.stat().st_mtime >= analysis_started - 1.0
    ]
    if not evidence:
        raise NativeDiagnosticsError(
            "Static-analysis build produced no .nativecodeanalysis.xml evidence; /analyze may not have run."
        )
    print(f"  Static-analysis evidence: {len(evidence)} native analysis sidecar(s).")
    return elapsed


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run bounded MSVC native diagnostics.")
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="Run parser and bounded-log tests without invoking Visual Studio.",
    )
    parser.add_argument(
        "--lane",
        choices=("all", "asan", "static-analysis"),
        default="all",
        help="Diagnostic subset to run (default: all).",
    )
    parser.add_argument(
        "--prove-asan-fixture",
        action="store_true",
        help="Generate a temporary heap-use-after-free fixture and require ASan to catch it.",
    )
    parser.add_argument(
        "--test-durations",
        action="store_true",
        help="Report per-test-case timings. Off by default because the per-case "
        "output buries the failure a red run exists to show.",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.self_test:
        failures = run_self_tests()
        if failures:
            for failure in failures:
                print(f"SELF_TEST_FAIL: {failure}")
            return 1
        print("SELF_TEST_PASS: native diagnostics parser and log guards passed")
        return 0
    if args.prove_asan_fixture and args.lane == "static-analysis":
        print("FAIL: --prove-asan-fixture requires the asan or all lane.")
        return 2
    ARTIFACT_ROOT.mkdir(parents=True, exist_ok=True)
    total_started = time.perf_counter()
    print("NATIVE_DIAGNOSTICS - bounded MSVC lifetime and static-analysis lane")
    try:
        msbuild = find_msbuild()
        print(f"  MSBuild: {msbuild}")
        if args.prove_asan_fixture:
            proof_elapsed = run_asan_proof_fixture(msbuild)
            print(f"  ASan detector proof: {proof_elapsed:.3f}s")
        if args.lane in ("all", "asan"):
            asan_elapsed = run_asan_tests(msbuild, report_durations=args.test_durations)
            print(f"  Healthy ASan CPU lane: {asan_elapsed:.3f}s")
        if args.lane in ("all", "static-analysis"):
            static_elapsed = run_static_analysis(msbuild)
            print(f"  Static-analysis lane: {static_elapsed:.3f}s")
    except ToolMissingError as exc:
        print(f"TOOL MISSING: {exc}")
        return 99
    except NativeDiagnosticsError as exc:
        print(f"FAIL: {exc}")
        return 1
    finally:
        # The temporary fixture context removes the faulty source and executable.
        # Remove any empty temp parent left after a successful proof.
        fixture_root = ARTIFACT_ROOT / "asan_fixture"
        if fixture_root.exists() and not any(fixture_root.iterdir()):
            fixture_root.rmdir()
    print(f"PASS: native diagnostics completed in {time.perf_counter() - total_started:.3f}s")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
