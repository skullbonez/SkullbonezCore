@rem
@rem File: tools/validate_deep.bat
@rem Purpose:
@rem   Runs the opt-in expensive validation path.
@rem
@rem Mental model:
@rem   Normal PR validation should stay cheap and launch the executable twice.
@rem   This script is for deliberate broad sweeps where the extra runtime
@rem   launches are worth the cost.
@rem
@echo off
setlocal
REM ===============================================================
REM  validate_deep.bat - Opt-in broad validation pipeline.
REM  Use for: pre-release checks, broad physics/render risk, or requested deep
REM           validation. Do not use as the default PR gate.
REM ===============================================================

echo.
echo ============================================================
echo   VALIDATE_DEEP - Opt-in Broad Validation
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
    echo VALIDATE_DEEP: FAILED at project filter validation.
    exit /b 1
)

echo === Phase 1: Build Required Configurations ===
call "%~dp0validate_build.bat" Profile
if errorlevel 1 (
    echo.
    echo VALIDATE_DEEP: FAILED at Profile build.
    exit /b 1
)
call "%~dp0validate_build.bat" Debug
if errorlevel 1 (
    echo.
    echo VALIDATE_DEEP: FAILED at Debug build.
    exit /b 1
)
set "SKULLBONEZ_ASSUME_PROFILE_BUILT=1"
set "SKULLBONEZ_ASSUME_DEBUG_BUILT=1"

echo === Phase 2: DX12 Renderer Validation ===
call "%~dp0validate_dx12_renderer.bat"
if errorlevel 1 (
    echo.
    echo VALIDATE_DEEP: FAILED at DX12 renderer validation.
    exit /b 1
)

echo.
echo === Phase 3: Deep Physics Validation ===
call "%~dp0validate_physics_deep.bat"
if errorlevel 1 (
    echo.
    echo VALIDATE_DEEP: FAILED at deep physics validation.
    exit /b 2
)

echo.
echo === Phase 4: Performance Validation ===
call "%~dp0validate_perf.bat"
if errorlevel 1 (
    echo.
    echo VALIDATE_DEEP: FAILED at performance validation.
    exit /b 3
)

set "SKULLBONEZ_SKIP_READY_BUILDS=%PREVIOUS_SKIP_READY_BUILDS%"
set "SKULLBONEZ_ASSUME_PROFILE_BUILT=%PREVIOUS_ASSUME_PROFILE_BUILT%"
set "SKULLBONEZ_ASSUME_DEBUG_BUILT=%PREVIOUS_ASSUME_DEBUG_BUILT%"
echo [ready] Profile and Debug were built before validation.

echo.
echo ============================================================
echo   VALIDATE_DEEP: ALL PHASES PASSED
echo ============================================================
exit /b 0
