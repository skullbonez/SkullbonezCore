@echo off
setlocal
REM ===============================================================
REM  validate_perf_demo.bat - Single-command management demo loop.
REM  Runs physics regression, then demo-scoped OpenGL perf capture.
REM ===============================================================

set "REPO=%~dp0..\..\.."
pushd "%REPO%"

echo.
echo ============================================================
echo   VALIDATE_PERF_DEMO - Physics + OpenGL Perf
echo ============================================================
echo.

echo === Step 1/2: Physics regression with broadphase visuals ===
call "%REPO%\Agentic\Plans\agent-loop\validate_physics_visual.bat"
if errorlevel 1 (
    echo.
    echo VALIDATE_PERF_DEMO: FAILED at physics regression.
    popd
    exit /b 1
)

echo.
echo === Step 2/2: OpenGL perf capture ===
call "%REPO%\Agentic\Plans\agent-loop\validate_perf_single.bat"
if errorlevel 1 (
    echo.
    echo VALIDATE_PERF_DEMO: FAILED at OpenGL perf capture.
    popd
    exit /b 2
)

echo.
echo ============================================================
echo   VALIDATE_PERF_DEMO: COMPLETE
echo ============================================================

popd
exit /b 0
