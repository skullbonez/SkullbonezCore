@echo off
setlocal enabledelayedexpansion
REM ===============================================================
REM  validate_physics.bat - Physics determinism regression test.
REM  Use for: physics, collision, solver, rigid body changes.
REM  Runtime: about 45 seconds.
REM ===============================================================

set "REPO=%~dp0.."
pushd "%REPO%"
echo.
echo ========================================
echo   VALIDATE_PHYSICS - Determinism Check
echo ========================================
echo.

echo [1/3] Building Debug x64...
call "%~dp0validate_build.bat" Debug
if errorlevel 1 exit /b 1

echo [2/3] Running physics regression scenes...
del /q "%REPO%\Debug\physics_regression_*.csv" 2>nul

echo   Running physics_regression_solver...
"%REPO%\Debug\SKULLBONEZ_CORE.exe" --vsync off --fixed-step --scene SkullbonezData/scenes/physics_regression_solver.scene
if errorlevel 1 (
    echo FAIL: physics_regression_solver crashed or errored.
    exit /b 2
)

echo [3/3] Comparing output against baselines...
set "SKORE_REPO=%REPO%"
py "%~dp0check_physics_regression.py"
if errorlevel 1 (
    echo FAIL: Physics regression detected. Output differs from baselines.
    echo       Baseline: TestOutput\baselines\physics_regression_solver.csv
    echo       Actual:   Debug\physics_regression_solver.csv
    exit /b 2
)

echo.
echo ========================================
echo   VALIDATE_PHYSICS: ALL PASSED
echo ========================================
popd
exit /b 0
