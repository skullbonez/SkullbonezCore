@rem
@rem File: tools/validate_fast.bat
@rem Purpose:
@rem   Runs the inexpensive repository preflight and the primary doctest suite.
@rem
@rem Summary:
@rem   Fast validation checks source hygiene, project metadata, staged-file size,
@rem   and current Profile/Debug build evidence before running the main unit-test
@rem   executable. The broad gate reuses the preflight and lets the CPU umbrella
@rem   own all tests.
@rem
@rem Glossary:
@rem   Preflight-only: Internal broad-gate mode that runs checks/builds but
@rem   defers test execution to validate_all_cpu_tests.bat.
@rem   Validation gate: Repository script that proves a class of changes before
@rem   commit or PR.
@rem
@rem Invariants:
@rem   - Direct validate_fast calls still run SKULLBONEZ_TESTS.
@rem   - Preflight-only mode never runs a test executable, preventing broad-gate
@rem   duplication when the CPU umbrella follows it.
@rem   - A parent may skip ready builds only after proving Debug is current;
@rem   Profile is always built here before compiled-symbol reachability runs.
@rem
@rem Related:
@rem   - AGENTS.md
@rem   - tools/validate_all_cpu_tests.bat
@rem   - tools/validate_full.bat
@rem
@rem
@echo off
setlocal
REM ===============================================================
REM  validate_fast.bat - Quick sanity check: format + metadata + staged-size + build.
REM  Use for: small code refactors and non-rendering code edits.
REM  Runtime: about 30 seconds.
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

echo [1/9] Checking formatting...
call "%~dp0validate_format.bat"
if errorlevel 1 (
    echo.
    echo To auto-fix: tools\format_fix.bat
    exit /b 1
)

echo [2/9] Checking Visual Studio project filters...
call "%~dp0validate_project_filters.bat"
if errorlevel 1 exit /b 2

echo [3/9] Checking dependency graph...
call "%~dp0validate_dependency_graph.bat"
if errorlevel 1 exit /b 7

echo [4/9] Checking ownership rulings...
REM Why: the build-config, shape, signature, complexity, reachability, and
REM glossary inventories report current structure and fail on missing/stale
REM owner judgements. Their triggers start qualitative review; none is a
REM ceiling or count budget. Self-tests run first so a scanner regression is
REM distinguishable from a source finding.
python "%~dp0check_build_config_consistency.py" --self-test
if errorlevel 1 exit /b 8
python "%~dp0inventory_unreachable_symbols.py" --self-test
if errorlevel 1 exit /b 8
python "%~dp0inventory_authority_free_aggregates.py" --self-test
if errorlevel 1 exit /b 8
python "%~dp0inventory_extraction_scars.py" --self-test
if errorlevel 1 exit /b 8
python "%~dp0inventory_wide_signatures.py" --self-test
if errorlevel 1 exit /b 8
python "%~dp0inventory_function_complexity.py" --self-test
if errorlevel 1 exit /b 8
python "%~dp0inventory_glossary_terms.py" --self-test
if errorlevel 1 exit /b 8
python "%~dp0inventory_authority_free_aggregates.py" --repo "%~dp0.." --strict
if errorlevel 1 exit /b 8
python "%~dp0inventory_extraction_scars.py" --repo "%~dp0.."
if errorlevel 1 exit /b 8
python "%~dp0inventory_wide_signatures.py" --repo "%~dp0.." --threshold 12 --format json --strict >nul
if errorlevel 1 exit /b 8
python "%~dp0inventory_function_complexity.py" --repo "%~dp0.." --strict
if errorlevel 1 exit /b 8
python "%~dp0inventory_glossary_terms.py" --repo "%~dp0.." --strict --format json >nul
if errorlevel 1 exit /b 8
python "%~dp0check_build_config_consistency.py" --repo "%~dp0.." --format json >nul
if errorlevel 1 exit /b 8

echo [5/9] Checking staged file sizes...
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

echo [6/9] Building Profile x64...
call "%~dp0validate_build.bat" Profile
if errorlevel 1 exit /b 4

echo [7/9] Running unit tests...
if "%PREFLIGHT_ONLY%"=="1" goto :tests_deferred
call "%~dp0validate_tests.bat"
if errorlevel 1 exit /b 5
goto :tests_complete

:tests_deferred
echo       Deferred to validate_all_cpu_tests.bat; no test executable ran.

:tests_complete

echo [8/9] Checking ready builds...
if /I "%SKULLBONEZ_SKIP_READY_BUILDS%"=="1" if /I not "%SKULLBONEZ_ASSUME_DEBUG_BUILT%"=="1" (
    echo ERROR: Compiled-symbol reachability requires a current Debug build.
    echo        A parent that skips ready builds must build Debug first and set
    echo        SKULLBONEZ_ASSUME_DEBUG_BUILT=1.
    exit /b 6
)
call "%~dp0validate_ready_builds.bat"
if errorlevel 1 exit /b 6

echo [9/9] Checking compiled-symbol reachability...
REM Invariant: Profile and Debug objects must be current before this scan.
REM Decorated COFF identities distinguish overloads after both configurations'
REM preprocessors have run; exact rulings own every non-production-rooted row.
python "%~dp0inventory_unreachable_symbols.py" --repo "%~dp0.." --format json --strict >nul
if errorlevel 1 exit /b 8

echo.
echo ========================================
if "%PREFLIGHT_ONLY%"=="1" (
    echo   VALIDATE_FAST: PREFLIGHT PASSED
) else (
    echo   VALIDATE_FAST: ALL PASSED
)
echo ========================================
exit /b 0
