@rem
@rem File: tools/validate_full.bat
@rem Purpose:
@rem   Runs the terminal full-plan closure gate and collects the replay-prediction
@rem   frame-spike diagnostic.
@rem
@rem Summary:
@rem   Full validation is reserved for completion of an entire implementation
@rem   plan. An explicit --plan-completion token prevents routine commits, pull
@rem   requests, and convenience selectors from invoking this expensive fan-in.
@rem   Cheap failures surface first. After the Debug build, deterministic
@rem   physics runs immediately so behavior drift cannot hide behind the CPU,
@rem   automation, or DX12 lanes. Automation and Debug then join the Profile
@rem   build performed by validate_fast to make reachability evidence current.
@rem   Each CPU test target runs once before automation and DX12. Automation
@rem   launches a negative Profile boundary plus one positive replay smoke.
@rem   After every gate passes, the informational replay spike workload records
@rem   findings without changing validation success.
@rem
@rem Glossary:
@rem   CPU preflight: Formatting, project metadata, staged-size, Profile build,
@rem   and Automation/Debug/Profile reachability checks that do not launch a
@rem   test or engine.
@rem   Plan-completion gate: Terminal repository proof run once after every task
@rem   in an implementation plan is complete and independently reviewed.
@rem
@rem Invariants:
@rem   - Invocation fails unless the caller explicitly declares plan completion.
@rem   - Physics is the first runtime launch and follows only its required Debug build.
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
