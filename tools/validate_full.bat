@rem
@rem File: tools/validate_full.bat
@rem Purpose:
@rem   Runs the mandatory CPU preflight/tests and default runtime gates, then
@rem   collects the full-only replay-prediction frame-spike diagnostic.
@rem
@rem Summary:
@rem   Full validation is the trustworthy PR fan-in: cheap failures surface
@rem   first, after a required Debug build makes two-configuration reachability
@rem   evidence current. Each CPU test target then runs once before automation,
@rem   DX12, and deterministic physics provide three runtime lanes. The
@rem   automation lane launches a negative Profile boundary plus one positive
@rem   replay smoke. After every gate passes, the informational replay spike
@rem   workload records findings without changing validation success.
@rem
@rem Glossary:
@rem   CPU preflight: Formatting, project metadata, staged-size, Profile build,
@rem   and Debug/Profile reachability checks that do not launch a test or engine.
@rem   Validation gate: Repository script that proves a class of changes before
@rem   commit or PR.
@rem
@rem Invariants:
@rem   - No runtime launch occurs until all mandatory CPU tests pass.
@rem   - validate_fast runs in preflight-only mode so validate_tests runs exactly
@rem   once through validate_all_cpu_tests.
@rem   - Replay-prediction spike collection is full-only and non-blocking; a
@rem   diagnostic failure is reported but never changes this script's exit code.
@rem
@rem Related:
@rem   - AGENTS.md
@rem   - tools/validate_fast.bat
@rem   - tools/validate_all_cpu_tests.bat
@rem   - tools/validate_automation.bat
@rem   - tools/validate_replay_prediction_frame_spikes.bat
@rem
@rem
@echo off
setlocal

echo.
echo ============================================================
echo   VALIDATE_FULL - Default PR Validation
echo ============================================================
echo.

set "PREVIOUS_SKIP_READY_BUILDS=%SKULLBONEZ_SKIP_READY_BUILDS%"
set "PREVIOUS_ASSUME_PROFILE_BUILT=%SKULLBONEZ_ASSUME_PROFILE_BUILT%"
set "PREVIOUS_ASSUME_DEBUG_BUILT=%SKULLBONEZ_ASSUME_DEBUG_BUILT%"
set "SKULLBONEZ_SKIP_READY_BUILDS=1"

echo === Phase 0: Build Debug Configuration ===
call "%~dp0validate_build.bat" Debug
if errorlevel 1 (
    echo.
    echo VALIDATE_FULL: FAILED at Debug build.
    exit /b 1
)
set "SKULLBONEZ_ASSUME_DEBUG_BUILT=1"

echo.
echo === Phase 1: Mandatory CPU Preflight ===
call "%~dp0validate_fast.bat" --preflight-only
set "PREFLIGHT_EXIT=%ERRORLEVEL%"
if not "%PREFLIGHT_EXIT%"=="0" (
    echo.
    echo VALIDATE_FULL: FAILED at mandatory CPU preflight.
    exit /b %PREFLIGHT_EXIT%
)
set "SKULLBONEZ_ASSUME_PROFILE_BUILT=1"
set "SKULLBONEZ_DEPENDENCY_GRAPH_ALREADY_VALIDATED=1"

echo.
echo === Phase 2: Mandatory CPU Tests ===
call "%~dp0validate_all_cpu_tests.bat"
set "CPU_TEST_EXIT=%ERRORLEVEL%"
if not "%CPU_TEST_EXIT%"=="0" (
    echo.
    echo VALIDATE_FULL: FAILED at mandatory CPU tests.
    exit /b %CPU_TEST_EXIT%
)

echo.
echo === Phase 3: Automation Build Boundary and Replay Prediction Smoke ===
call "%~dp0validate_automation.bat"
if errorlevel 1 (
    echo.
    echo VALIDATE_FULL: FAILED at automation validation.
    exit /b 1
)

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
echo === Phase 6: Informational Replay Prediction Frame-Spike Diagnostic ===
call "%~dp0validate_replay_prediction_frame_spikes.bat"
set "REPLAY_SPIKE_DIAGNOSTIC_EXIT=%ERRORLEVEL%"
if not "%REPLAY_SPIKE_DIAGNOSTIC_EXIT%"=="0" (
    echo.
    echo WARNING: replay-prediction frame-spike diagnostic did not produce findings.
    echo WARNING: informational diagnostic exit %REPLAY_SPIKE_DIAGNOSTIC_EXIT% does not fail validation.
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
