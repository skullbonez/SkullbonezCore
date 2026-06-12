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
set "SKULLBONEZ_SKIP_READY_BUILDS=1"

echo === Phase 1: Renderer Validation ===
call "%~dp0validate_renderers.bat"
if errorlevel 1 (
    echo.
    echo VALIDATE_FULL: FAILED at renderer validation.
    exit /b 1
)

echo.
echo === Phase 2: Physics Validation ===
call "%~dp0validate_physics.bat"
if errorlevel 1 (
    echo.
    echo VALIDATE_FULL: FAILED at physics validation.
    exit /b 2
)

echo.
echo === Phase 3: Performance Validation ===
call "%~dp0validate_perf.bat"
if errorlevel 1 (
    echo.
    echo VALIDATE_FULL: FAILED at performance validation.
    exit /b 3
)

set "SKULLBONEZ_SKIP_READY_BUILDS=%PREVIOUS_SKIP_READY_BUILDS%"
call "%~dp0validate_ready_builds.bat"
if errorlevel 1 exit /b 4

echo.
echo ============================================================
echo   VALIDATE_FULL: ALL PHASES PASSED
echo ============================================================
exit /b 0
