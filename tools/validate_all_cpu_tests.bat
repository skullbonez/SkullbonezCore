@rem
@rem File: tools/validate_all_cpu_tests.bat
@rem Purpose:
@rem   Runs every first-party CPU test and coverage gate through one fail-fast entry point.
@rem
@rem Mental model:
@rem   This script is the CPU/coverage fan-in for PR validation. Each child still
@rem   owns its build and executable, while this wrapper owns ordering, failure
@rem   attribution, and the combined result shown to a caller.
@rem
@rem Glossary:
@rem   CPU test gate: A build-and-test script that does not launch the engine or
@rem   require a graphics device.
@rem   Fail-fast: Stop at the first failed child so later output cannot obscure
@rem   the owning failure.
@rem   Harness directory: Test-only replacement child-script directory used to
@rem   prove wrapper control flow without changing production tests.
@rem
@rem Invariants:
@rem   - Each child gate runs at most once and in the order printed below.
@rem   - The first child failure code is returned unchanged to the caller.
@rem   - A harness directory is honored only when its explicit opt-in is set.
@rem
@rem Related:
@rem   - tools/validate_full.bat
@rem   - Agentic/Plans/TODO/validation-gate-integrity.md
@rem   - Agentic/Reports/validation_gate_inventory_20260710.md
@rem
@echo off
setlocal

set "CPU_TEST_SCRIPT_DIR=%~dp0"
if not defined SKULLBONEZ_CPU_TEST_SCRIPT_DIR goto :script_dir_ready
if not "%SKULLBONEZ_CPU_TEST_HARNESS%"=="1" goto :invalid_harness_opt_in
set "CPU_TEST_SCRIPT_DIR=%SKULLBONEZ_CPU_TEST_SCRIPT_DIR%"

:script_dir_ready
for %%D in ("%CPU_TEST_SCRIPT_DIR%\.") do set "CPU_TEST_SCRIPT_DIR=%%~fD\"

set "STATUS_TESTS=NOT RUN"
set "STATUS_COVERAGE=NOT RUN"
set "STATUS_INTERACTION=NOT RUN"
set "STATUS_SCENE_PARSER=NOT RUN"
set "STATUS_DX12_ARCH=NOT RUN"

echo.
echo ============================================================
echo   VALIDATE_ALL_CPU_TESTS - Mandatory CPU Test Umbrella
echo ============================================================
echo.

if "%SKULLBONEZ_DEPENDENCY_GRAPH_ALREADY_VALIDATED%"=="1" (
    echo Dependency graph already passed in the owning preflight.
) else (
    echo Running dependency graph preflight...
    call "%~dp0validate_dependency_graph.bat"
    if errorlevel 1 exit /b %errorlevel%
)

echo [1/5] Running validate_tests.bat...
if not exist "%CPU_TEST_SCRIPT_DIR%validate_tests.bat" (
    set "CHILD_EXIT=99"
    goto :on_tests_missing
)
call "%CPU_TEST_SCRIPT_DIR%validate_tests.bat"
set "CHILD_EXIT=%ERRORLEVEL%"
if not "%CHILD_EXIT%"=="0" goto :on_tests_failure
set "STATUS_TESTS=PASS"

echo.
echo [2/5] Running validate_coverage.bat...
if not exist "%CPU_TEST_SCRIPT_DIR%validate_coverage.bat" (
    set "CHILD_EXIT=99"
    goto :on_coverage_missing
)
call "%CPU_TEST_SCRIPT_DIR%validate_coverage.bat"
set "CHILD_EXIT=%ERRORLEVEL%"
if not "%CHILD_EXIT%"=="0" goto :on_coverage_failure
set "STATUS_COVERAGE=PASS"

echo.
echo [3/5] Running validate_runtime_interaction_policy.bat...
if not exist "%CPU_TEST_SCRIPT_DIR%validate_runtime_interaction_policy.bat" (
    set "CHILD_EXIT=99"
    goto :on_interaction_missing
)
call "%CPU_TEST_SCRIPT_DIR%validate_runtime_interaction_policy.bat"
set "CHILD_EXIT=%ERRORLEVEL%"
if not "%CHILD_EXIT%"=="0" goto :on_interaction_failure
set "STATUS_INTERACTION=PASS"

echo.
echo [4/5] Running validate_scene_parser_tests.bat...
if not exist "%CPU_TEST_SCRIPT_DIR%validate_scene_parser_tests.bat" (
    set "CHILD_EXIT=99"
    goto :on_scene_parser_missing
)
call "%CPU_TEST_SCRIPT_DIR%validate_scene_parser_tests.bat"
set "CHILD_EXIT=%ERRORLEVEL%"
if not "%CHILD_EXIT%"=="0" goto :on_scene_parser_failure
set "STATUS_SCENE_PARSER=PASS"

echo.
echo [5/5] Running validate_dx12_arch_tests.bat...
if not exist "%CPU_TEST_SCRIPT_DIR%validate_dx12_arch_tests.bat" (
    set "CHILD_EXIT=99"
    goto :on_dx12_arch_missing
)
call "%CPU_TEST_SCRIPT_DIR%validate_dx12_arch_tests.bat"
set "CHILD_EXIT=%ERRORLEVEL%"
if not "%CHILD_EXIT%"=="0" goto :on_dx12_arch_failure
set "STATUS_DX12_ARCH=PASS"

echo.
echo ---------------- CPU test summary ----------------
echo   validate_tests.bat                       %STATUS_TESTS%
echo   validate_coverage.bat                    %STATUS_COVERAGE%
echo   validate_runtime_interaction_policy.bat  %STATUS_INTERACTION%
echo   validate_scene_parser_tests.bat          %STATUS_SCENE_PARSER%
echo   validate_dx12_arch_tests.bat              %STATUS_DX12_ARCH%
echo --------------------------------------------------
echo.
echo ============================================================
echo   VALIDATE_ALL_CPU_TESTS: ALL PASSED
echo ============================================================
exit /b 0

:invalid_harness_opt_in
echo ERROR: SKULLBONEZ_CPU_TEST_SCRIPT_DIR requires SKULLBONEZ_CPU_TEST_HARNESS=1.
exit /b 99

:on_tests_missing
set "STATUS_TESTS=MISSING - exit 99"
set "FAILED_TARGET=validate_tests.bat"
goto :emit_failure

:on_tests_failure
set "STATUS_TESTS=FAIL - exit %CHILD_EXIT%"
set "FAILED_TARGET=validate_tests.bat"
goto :emit_failure

:on_coverage_missing
set "STATUS_COVERAGE=MISSING - exit 99"
set "FAILED_TARGET=validate_coverage.bat"
goto :emit_failure

:on_coverage_failure
set "STATUS_COVERAGE=FAIL - exit %CHILD_EXIT%"
set "FAILED_TARGET=validate_coverage.bat"
goto :emit_failure

:on_interaction_missing
set "STATUS_INTERACTION=MISSING - exit 99"
set "FAILED_TARGET=validate_runtime_interaction_policy.bat"
goto :emit_failure

:on_interaction_failure
set "STATUS_INTERACTION=FAIL - exit %CHILD_EXIT%"
set "FAILED_TARGET=validate_runtime_interaction_policy.bat"
goto :emit_failure

:on_scene_parser_missing
set "STATUS_SCENE_PARSER=MISSING - exit 99"
set "FAILED_TARGET=validate_scene_parser_tests.bat"
goto :emit_failure

:on_scene_parser_failure
set "STATUS_SCENE_PARSER=FAIL - exit %CHILD_EXIT%"
set "FAILED_TARGET=validate_scene_parser_tests.bat"
goto :emit_failure

:on_dx12_arch_missing
set "STATUS_DX12_ARCH=MISSING - exit 99"
set "FAILED_TARGET=validate_dx12_arch_tests.bat"
goto :emit_failure

:on_dx12_arch_failure
set "STATUS_DX12_ARCH=FAIL - exit %CHILD_EXIT%"
set "FAILED_TARGET=validate_dx12_arch_tests.bat"

:emit_failure
echo.
echo ---------------- CPU test summary ----------------
echo   validate_tests.bat                       %STATUS_TESTS%
echo   validate_coverage.bat                    %STATUS_COVERAGE%
echo   validate_runtime_interaction_policy.bat  %STATUS_INTERACTION%
echo   validate_scene_parser_tests.bat          %STATUS_SCENE_PARSER%
echo   validate_dx12_arch_tests.bat              %STATUS_DX12_ARCH%
echo --------------------------------------------------
echo.
echo VALIDATE_ALL_CPU_TESTS: FAILED at %FAILED_TARGET% with exit code %CHILD_EXIT%.
exit /b %CHILD_EXIT%
