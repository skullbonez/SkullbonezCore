@rem
@rem File: tools/validate_full.bat
@rem Purpose:
@rem   Runs the mandatory CPU preflight/tests before the default runtime gates.
@rem
@rem Mental model:
@rem   Full validation is the trustworthy PR fan-in: cheap failures surface
@rem   first, each CPU test target runs once, then automation, DX12, and
@rem   deterministic physics provide three runtime lanes. The automation lane
@rem   launches a negative Profile boundary plus one positive replay smoke.
@rem
@rem Glossary:
@rem   CPU preflight: Formatting, project metadata, staged-size, and Profile
@rem   build checks that do not launch a test or the engine.
@rem   Validation gate: Repository script that proves a class of changes before
@rem   commit or PR.
@rem
@rem Invariants:
@rem   - No runtime launch occurs until all mandatory CPU tests pass.
@rem   - validate_fast runs in preflight-only mode so validate_tests runs exactly
@rem   once through validate_all_cpu_tests.
@rem
@rem Related:
@rem   - AGENTS.md
@rem   - tools/validate_fast.bat
@rem   - tools/validate_all_cpu_tests.bat
@rem   - tools/validate_automation.bat
@rem
@rem
@echo off
setlocal
REM ===============================================================
REM  validate_full.bat - Default PR validation pipeline.
REM  Use for: broad changes, uncertain scope, normal pre-merge verification.
REM  Runtime: CPU tests followed by three runtime lanes and five engine processes.
REM ===============================================================

echo.
echo ============================================================
echo   VALIDATE_FULL - Default PR Validation
echo ============================================================
echo.

set "PREVIOUS_SKIP_READY_BUILDS=%SKULLBONEZ_SKIP_READY_BUILDS%"
set "PREVIOUS_ASSUME_PROFILE_BUILT=%SKULLBONEZ_ASSUME_PROFILE_BUILT%"
set "PREVIOUS_ASSUME_DEBUG_BUILT=%SKULLBONEZ_ASSUME_DEBUG_BUILT%"
set "SKULLBONEZ_SKIP_READY_BUILDS=1"

echo === Phase 0: Mandatory CPU Preflight ===
call "%~dp0validate_fast.bat" --preflight-only
set "PREFLIGHT_EXIT=%ERRORLEVEL%"
if not "%PREFLIGHT_EXIT%"=="0" (
    echo.
    echo VALIDATE_FULL: FAILED at mandatory CPU preflight.
    exit /b %PREFLIGHT_EXIT%
)
set "SKULLBONEZ_ASSUME_PROFILE_BUILT=1"

echo.
echo === Phase 1: Mandatory CPU Tests ===
call "%~dp0validate_all_cpu_tests.bat"
set "CPU_TEST_EXIT=%ERRORLEVEL%"
if not "%CPU_TEST_EXIT%"=="0" (
    echo.
    echo VALIDATE_FULL: FAILED at mandatory CPU tests.
    exit /b %CPU_TEST_EXIT%
)

echo.
echo === Phase 2: Automation Build Boundary and Replay Prediction Smoke ===
call "%~dp0validate_automation.bat"
if errorlevel 1 (
    echo.
    echo VALIDATE_FULL: FAILED at automation validation.
    exit /b 1
)

echo.
echo === Phase 3: Build Debug Configuration ===
call "%~dp0validate_build.bat" Debug
if errorlevel 1 (
    echo.
    echo VALIDATE_FULL: FAILED at Debug build.
    exit /b 1
)
set "SKULLBONEZ_ASSUME_DEBUG_BUILT=1"

echo.
echo === Phase 4: DX12 Renderer Validation ===
call "%~dp0validate_dx12_renderer.bat"
if errorlevel 1 (
    echo.
    echo VALIDATE_FULL: FAILED at DX12 renderer validation.
    exit /b 1
)

echo.
echo === Phase 5: Physics Validation ===
call "%~dp0validate_physics.bat"
if errorlevel 1 (
    echo.
    echo VALIDATE_FULL: FAILED at physics validation.
    exit /b 2
)

echo.
set "SKULLBONEZ_SKIP_READY_BUILDS=%PREVIOUS_SKIP_READY_BUILDS%"
set "SKULLBONEZ_ASSUME_PROFILE_BUILT=%PREVIOUS_ASSUME_PROFILE_BUILT%"
set "SKULLBONEZ_ASSUME_DEBUG_BUILT=%PREVIOUS_ASSUME_DEBUG_BUILT%"
echo [ready] Profile, Automation, and Debug were built before validation.

echo.
echo ============================================================
echo   VALIDATE_FULL: DEFAULT GATE PASSED
echo ============================================================
exit /b 0
