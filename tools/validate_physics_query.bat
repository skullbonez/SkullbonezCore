@echo off
setlocal enabledelayedexpansion
REM ===============================================================
REM  validate_physics_query.bat - SkullScope query regression test.
REM  Builds Debug, generates a queryable diagnostics trace, and
REM  compares normalized query outputs against the committed baseline.
REM ===============================================================

set "REPO=%~dp0.."
pushd "%REPO%"
call "%~dp0find_python.bat"
if errorlevel 1 exit /b 99

echo.
echo ========================================
echo   VALIDATE_PHYSICS_QUERY - SkullScope
echo ========================================
echo.

echo [1/2] Building Debug x64...
call "%~dp0validate_build.bat" Debug
if errorlevel 1 exit /b 1

echo [2/2] Checking SkullScope query baseline...
set "SKORE_REPO=%REPO%"
"%PYTHON_EXE%" "%~dp0check_physics_query_regression.py"
if errorlevel 1 (
    echo FAIL: SkullScope query regression detected.
    echo       Baseline: TestOutput\baselines\physics_query_varied.json
    echo       Trace:    Debug\physics_query_varied.physicsdiag.ndjson
    exit /b 2
)

call "%~dp0validate_ready_builds.bat"
if errorlevel 1 (
    popd
    exit /b 3
)

echo.
echo ========================================
echo   VALIDATE_PHYSICS_QUERY: ALL PASSED
echo ========================================
popd
exit /b 0
