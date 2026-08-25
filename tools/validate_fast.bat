@rem
@rem File: tools/validate_fast.bat
@rem Purpose:
@rem   Runs the inexpensive repository preflight and the primary doctest suite.
@rem
@rem Summary:
@rem   Fast validation first proves that the deterministic physics golden still
@rem   has the exact accepted digest, then checks source hygiene, project
@rem   metadata, staged-file size, and current build evidence.
@rem
@rem Glossary:
@rem   Preflight-only: Internal broad-gate mode that runs checks/builds but
@rem   defers test execution to validate_all_cpu_tests.bat.
@rem   Validation gate: Repository script that proves a class of changes before
@rem   commit or PR.
@rem
@rem Invariants:
@rem   - Physics golden tampering fails before formatting, inventories, or builds.
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

echo [1/9] Checking accepted Physics golden and retained transitions...
python "%~dp0check_physics_baseline_guard.py" --repo "%~dp0.."
if errorlevel 1 exit /b 9

echo [2/9] Checking plain-language policy...
python "%~dp0check_plain_language.py" --repo "%~dp0.." --self-test
if errorlevel 1 exit /b 10
python "%~dp0check_plain_language.py" --repo "%~dp0.."
if errorlevel 1 exit /b 10

echo [3/9] Checking formatting...
call "%~dp0validate_format.bat"
if errorlevel 1 (
    echo.
    echo To auto-fix: tools\format_fix.bat
    exit /b 1
)

echo [4/9] Checking Visual Studio project filters...
call "%~dp0validate_project_filters.bat"
if errorlevel 1 exit /b 2

echo [5/9] Checking dependency graph...
call "%~dp0validate_dependency_graph.bat"
if errorlevel 1 exit /b 7

echo [6/9] Checking source design and retained policies...
REM Why: direct calls make each retained rule and failure visible without a
REM checker that exists only to run other checkers. Review triggers report
REM current structure; none is a ceiling or count allowance.
REM Self-tests and live scans are two explicit phases. Independent commands in
REM each phase run concurrently so deleting the old wrapper does not lengthen
REM the fast gate; a failed child prints its exact direct command for replay.
powershell -NoProfile -ExecutionPolicy Bypass -Command "$commands=@(@('%~dp0check_source_design.py','--repo','%~dp0..','--self-test'),@('%~dp0check_build_config_consistency.py','--self-test'),@('%~dp0check_determinism_math_policy.py','--self-test')); $processes=@(); foreach($command in $commands){$processes+=Start-Process -FilePath python -ArgumentList $command -PassThru -WindowStyle Hidden}; $failed=@(); for($index=0;$index -lt $processes.Count;++$index){$processes[$index].WaitForExit(); if($processes[$index].ExitCode -ne 0){$failed+=('python '+($commands[$index] -join ' '))}}; if($failed.Count){Write-Error ('FAILED ownership self-test(s): '+($failed -join '; ')); exit 8}"
if errorlevel 1 exit /b 8

powershell -NoProfile -ExecutionPolicy Bypass -Command "$commands=@(@('%~dp0check_source_design.py','--repo','%~dp0..'),@('%~dp0check_build_config_consistency.py','--repo','%~dp0..','--format','json'),@('%~dp0check_determinism_math_policy.py','--repo','%~dp0..','--format','json')); $processes=@(); foreach($command in $commands){$processes+=Start-Process -FilePath python -ArgumentList $command -PassThru -WindowStyle Hidden}; $failed=@(); for($index=0;$index -lt $processes.Count;++$index){$processes[$index].WaitForExit(); if($processes[$index].ExitCode -ne 0){$failed+=('python '+($commands[$index] -join ' '))}}; if($failed.Count){Write-Error ('FAILED ownership scan(s): '+($failed -join '; ')); exit 8}"
if errorlevel 1 exit /b 8

echo [7/9] Checking staged file sizes...
REM Why: the checker reads the git index, so keep it before the expensive build
REM steps and pass the repo root explicitly for callers outside the worktree.
REM Hosted CI supplies a base commit because its clean index contains no pending
REM commit; comparison mode then inspects exact HEAD blobs changed by the PR.
if defined SKORE_SIZE_DIFF_BASE (
    python "%~dp0check_staged_file_sizes.py" --repo "%~dp0.." --base-ref "%SKORE_SIZE_DIFF_BASE%"
) else (
    python "%~dp0check_staged_file_sizes.py" --repo "%~dp0.."
)
if errorlevel 1 exit /b 3

echo [8/9] Building Profile x64...
call "%~dp0validate_build.bat" Profile
if errorlevel 1 exit /b 4

echo [9/9] Running unit tests...
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
