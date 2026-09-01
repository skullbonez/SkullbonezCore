@rem
@rem File: tools/validate_fast.bat
@rem Purpose:
@rem   Runs the inexpensive repository preflight and the primary doctest suite.
@rem
@rem Summary:
@rem   Fast validation runs independent repository checks concurrently, then
@rem   builds Profile and optionally executes the primary doctest suite.
@rem
@rem Glossary:
@rem   Preflight-only: Internal broad-gate mode that runs checks/builds but
@rem   defers test execution to validate_all_cpu_tests.bat.
@rem   Validation gate: Repository script that proves a class of changes before
@rem   commit or PR.
@rem
@rem Invariants:
@rem   - Every repository check finishes and retains an isolated log before the
@rem     Profile build starts.
@rem   - Direct validate_fast calls still run SKULLBONEZ_TESTS.
@rem   - Preflight-only mode never runs a test executable, preventing broad-gate
@rem   duplication when the CPU umbrella follows it.
@rem   - Profile is the sole build produced by this fast gate.
@rem
@rem Related:
@rem   - AGENTS.md
@rem   - tools/check_physics_baseline_guard.py
@rem   - tools/validate_all_cpu_tests.bat
@rem   - tools/validate_full.bat
@rem
@rem
@echo off
setlocal
REM ===============================================================
REM  validate_fast.bat - Quick sanity check: format + metadata + staged-size + build.
REM  Use for: small code refactors and non-rendering code edits.
REM  Runtime: about 3 minutes (preflight) / 4 minutes (with unit tests).
REM ===============================================================

set "PREFLIGHT_ONLY=0"
if /I "%~1"=="--preflight-only" set "PREFLIGHT_ONLY=1"
if not "%~1"=="" if not "%PREFLIGHT_ONLY%"=="1" (
    echo ERROR: Unknown argument "%~1".
    echo Usage: tools\validate_fast.bat [--preflight-only]
    exit /b 64
)

echo.
echo ========================================
echo   VALIDATE_FAST - Format + Metadata + Dependencies + Ownership + Size + Build
echo ========================================
echo.

echo [1/3] Running independent repository checks in parallel...
call "%~dp0find_python.bat"
if errorlevel 1 exit /b 99
"%PYTHON_EXE%" "%~dp0run_parallel_validation.py" --repo "%~dp0.." --manifest "%~dp0validation_parallel_fast.json"
if errorlevel 1 (
    echo.
    echo To auto-fix formatting failures: tools\format_fix.bat
    exit /b 1
)

echo [2/3] Building Profile x64...
call "%~dp0validate_build.bat" Profile
if errorlevel 1 exit /b 4

echo [3/3] Running unit tests...
if "%PREFLIGHT_ONLY%"=="1" goto :tests_deferred
call "%~dp0validate_tests.bat"
if errorlevel 1 exit /b 5
goto :tests_complete

:tests_deferred
echo       Deferred to validate_all_cpu_tests.bat; no test executable ran.

:tests_complete

echo.
echo ========================================
if "%PREFLIGHT_ONLY%"=="1" (
    echo   VALIDATE_FAST: PREFLIGHT PASSED
) else (
    echo   VALIDATE_FAST: ALL PASSED
)
echo ========================================
exit /b 0
