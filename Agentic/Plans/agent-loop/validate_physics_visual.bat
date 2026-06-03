@echo off
setlocal enabledelayedexpansion
REM ===============================================================
REM  validate_physics_visual.bat - Demo-loop regression test.
REM  Builds Debug and runs the physics regression with broadphase
REM  visuals enabled. This is demo-specific; perf capture stays clean.
REM ===============================================================

set "REPO=%~dp0..\..\.."
pushd "%REPO%"

call "%REPO%\tools\find_python.bat"
if errorlevel 1 exit /b 99

echo.
echo ========================================
echo   VALIDATE_PHYSICS_VISUAL - Demo Regression
echo ========================================
echo.

echo [1/3] Building Debug x64...
call "%REPO%\tools\validate_build.bat" Debug
if errorlevel 1 exit /b 1

echo [2/3] Running physics regression with broadphase visualizer...
set "PHYSICS_LOG=%REPO%\Debug\physics_regression_solver.csv"
del /q "%PHYSICS_LOG%" 2>nul
if exist "%PHYSICS_LOG%" (
    echo FAIL: Could not remove stale physics log:
    echo       %PHYSICS_LOG%
    exit /b 2
)

"%REPO%\Debug\SKULLBONEZ_CORE.exe" --vsync off --fixed-step --broadphase-visualizer --scene SkullbonezData/scenes/physics_regression_solver.scene
if errorlevel 1 (
    echo FAIL: physics_regression_solver crashed or errored.
    exit /b 2
)

echo [3/3] Comparing output against baselines...
set "SKORE_REPO=%REPO%"
"%PYTHON_EXE%" "%REPO%\tools\check_physics_regression.py"
if errorlevel 1 (
    echo FAIL: Physics regression detected. Output differs from baselines.
    echo       Baseline: TestOutput\baselines\physics_regression_solver.csv
    echo       Actual:   Debug\physics_regression_solver.csv
    exit /b 2
)

echo.
echo ========================================
echo   VALIDATE_PHYSICS_VISUAL: ALL PASSED
echo ========================================

popd
exit /b 0
