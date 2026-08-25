@rem Purpose:
@rem   Runs the terminal full-plan closure gate and collects the replay-prediction
@rem   frame-spike diagnostic.

@rem Invariants:
@rem   - Invocation fails unless the caller explicitly declares plan completion.
@rem   - Physics is the first runtime launch and follows only its required Debug build.
@rem   - validate_fast runs in preflight-only mode so validate_tests runs exactly
@rem   once through validate_all_cpu_tests.
@rem   - Replay-prediction spike collection is full-only and non-blocking; a
@rem   diagnostic failure is reported but never changes this script's exit code.

@echo off
setlocal

if /I not "%~1"=="--plan-completion" goto :usage_error
if not "%~2"=="" goto :usage_error

echo.
echo ============================================================
echo   VALIDATE_FULL - Full Plan Completion Gate
echo ============================================================
echo.

set "PREVIOUS_SKIP_READY_BUILDS=%SKULLBONEZ_SKIP_READY_BUILDS%"
set "PREVIOUS_ASSUME_PROFILE_BUILT=%SKULLBONEZ_ASSUME_PROFILE_BUILT%"
set "PREVIOUS_ASSUME_DEBUG_BUILT=%SKULLBONEZ_ASSUME_DEBUG_BUILT%"
set "SKULLBONEZ_SKIP_READY_BUILDS=1"

echo === Phase 0A: Build Debug Configuration ===
call "%~dp0validate_build.bat" Debug
if errorlevel 1 (
    echo.
    echo VALIDATE_FULL: FAILED at Debug build.
    exit /b 1
)
set "SKULLBONEZ_ASSUME_DEBUG_BUILT=1"

echo.
echo === Phase 0B: Physics Validation - First Runtime Oracle ===
call "%~dp0validate_physics.bat"
if errorlevel 1 (
    echo.
    echo VALIDATE_FULL: FAILED at physics validation.
    exit /b 2
)

echo.
echo === Phase 0C: Build Automation Configuration for Reachability ===
call "%~dp0validate_build.bat" Automation
if errorlevel 1 (
    echo.
    echo VALIDATE_FULL: FAILED at Automation reachability build.
    exit /b 1
)

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
echo === Phase 5: Informational Replay Prediction Frame-Spike Diagnostic ===
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
echo   VALIDATE_FULL: PLAN COMPLETION GATE PASSED
echo ============================================================
exit /b 0

:usage_error
echo ERROR: validate_full is reserved for completion of an entire plan.
echo Usage: tools\validate_full.bat --plan-completion
echo For ordinary commit or PR validation, run the cumulative focused gates
echo mapped in AGENTS.md.
exit /b 64
