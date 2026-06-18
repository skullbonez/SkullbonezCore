@rem
@rem File: tools/validate_full.bat
@rem Purpose:
@rem   Documents and runs the validate_full.bat developer/validation helper script.
@rem
@rem Mental model:
@rem   Tools are command-line guardrails around builds, validation, screenshots,
@rem   diagnostics, and artifact handling. They make the safe path repeatable and
@rem   keep output bounded for humans and agents.
@rem
@rem Glossary:
@rem   Validation gate: Repository script that proves a class of changes before
@rem   commit or PR.
@rem
@rem Invariants:
@rem   - Tool output should be bounded and readable because agents and humans use
@rem   it for decisions.
@rem
@rem Related:
@rem   - AGENTS.md
@rem   - Agentic/Reference/comment-style-guide.md
@rem
@rem
@echo off
setlocal
REM ===============================================================
REM  validate_full.bat - Complete validation pipeline.
REM  Use for: broad changes, uncertain scope, pre-merge verification.
REM  Runtime: about 3 minutes.
REM ===============================================================

echo.
echo ============================================================
echo   VALIDATE_FULL - Complete Validation Pipeline
echo ============================================================
echo.

set "PREVIOUS_SKIP_READY_BUILDS=%SKULLBONEZ_SKIP_READY_BUILDS%"
set "PREVIOUS_ASSUME_PROFILE_BUILT=%SKULLBONEZ_ASSUME_PROFILE_BUILT%"
set "PREVIOUS_ASSUME_DEBUG_BUILT=%SKULLBONEZ_ASSUME_DEBUG_BUILT%"
set "SKULLBONEZ_SKIP_READY_BUILDS=1"

echo === Phase 0: Project Metadata Validation ===
call "%~dp0validate_project_filters.bat"
if errorlevel 1 (
    echo.
    echo VALIDATE_FULL: FAILED at project filter validation.
    exit /b 1
)

echo === Phase 1: Build Required Configurations ===
call "%~dp0validate_build.bat" Profile
if errorlevel 1 (
    echo.
    echo VALIDATE_FULL: FAILED at Profile build.
    exit /b 1
)
call "%~dp0validate_build.bat" Debug
if errorlevel 1 (
    echo.
    echo VALIDATE_FULL: FAILED at Debug build.
    exit /b 1
)
set "SKULLBONEZ_ASSUME_PROFILE_BUILT=1"
set "SKULLBONEZ_ASSUME_DEBUG_BUILT=1"

echo === Phase 2: DX12 Renderer Validation ===
call "%~dp0validate_dx12_renderer.bat"
if errorlevel 1 (
    echo.
    echo VALIDATE_FULL: FAILED at DX12 renderer validation.
    exit /b 1
)

echo.
echo === Phase 3: Physics Validation ===
call "%~dp0validate_physics.bat"
if errorlevel 1 (
    echo.
    echo VALIDATE_FULL: FAILED at physics validation.
    exit /b 2
)

echo.
echo === Phase 4: Performance Validation ===
call "%~dp0validate_perf.bat"
if errorlevel 1 (
    echo.
    echo VALIDATE_FULL: FAILED at performance validation.
    exit /b 3
)

set "SKULLBONEZ_SKIP_READY_BUILDS=%PREVIOUS_SKIP_READY_BUILDS%"
set "SKULLBONEZ_ASSUME_PROFILE_BUILT=%PREVIOUS_ASSUME_PROFILE_BUILT%"
set "SKULLBONEZ_ASSUME_DEBUG_BUILT=%PREVIOUS_ASSUME_DEBUG_BUILT%"
echo [ready] Profile and Debug were built before validation.

echo.
echo ============================================================
echo   VALIDATE_FULL: ALL PHASES PASSED
echo ============================================================
exit /b 0
