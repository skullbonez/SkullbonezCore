@rem
@rem File: tools/validate_all_cpu_tests.bat
@rem Purpose:
@rem   Runs every first-party CPU test and coverage gate concurrently.
@rem
@rem Summary:
@rem   This script is the CPU/coverage fan-in for PR validation. Each child owns
@rem   its build and executable; the parallel runner owns isolation, timings,
@rem   deterministic failure attribution, and combined evidence.
@rem
@rem Glossary:
@rem   CPU test gate: A build-and-test script that does not launch the engine or
@rem   require a graphics device.
@rem   Harness directory: Test-only replacement child-script directory used to
@rem   prove wrapper control flow without changing production tests.
@rem
@rem Invariants:
@rem   - Each child gate runs at most once in an isolated working directory.
@rem   - Every launched child finishes before the manifest-order failure is returned.
@rem   - A harness directory is honored only when its explicit opt-in is set.
@rem
@rem Related:
@rem   - tools/validate_full.bat
@rem
@echo off
setlocal

set "CPU_TEST_SCRIPT_DIR=%~dp0"
if not defined SKULLBONEZ_CPU_TEST_SCRIPT_DIR goto :script_dir_ready
if not "%SKULLBONEZ_CPU_TEST_HARNESS%"=="1" goto :invalid_harness_opt_in
set "CPU_TEST_SCRIPT_DIR=%SKULLBONEZ_CPU_TEST_SCRIPT_DIR%"

:script_dir_ready
for %%D in ("%CPU_TEST_SCRIPT_DIR%\.") do set "CPU_TEST_SCRIPT_DIR=%%~fD"

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

call "%~dp0find_python.bat"
if errorlevel 1 exit /b 99
"%PYTHON_EXE%" "%~dp0run_parallel_validation.py" --repo "%~dp0.." --manifest "%~dp0validation_parallel_cpu.json" --variable "CPU_TEST_SCRIPT_DIR=%CPU_TEST_SCRIPT_DIR%"
set "CHILD_EXIT=%ERRORLEVEL%"
if not "%CHILD_EXIT%"=="0" (
    echo.
    echo VALIDATE_ALL_CPU_TESTS: FAILED with exit code %CHILD_EXIT%.
    exit /b %CHILD_EXIT%
)
echo.
echo ============================================================
echo   VALIDATE_ALL_CPU_TESTS: ALL PASSED
echo ============================================================
exit /b 0

:invalid_harness_opt_in
echo ERROR: SKULLBONEZ_CPU_TEST_SCRIPT_DIR requires SKULLBONEZ_CPU_TEST_HARNESS=1.
exit /b 99
