@echo off
setlocal enabledelayedexpansion
REM ===============================================================
REM  validate_perf_single.bat - Demo-loop OpenGL perf capture.
REM  Scope: management demo only, not the general repo validation suite.
REM ===============================================================

set "REPO=%~dp0..\..\.."
pushd "%REPO%"

call "%REPO%\tools\find_python.bat"
if errorlevel 1 exit /b 99
call "%REPO%\tools\find_git.bat"
if errorlevel 1 exit /b 99

echo.
echo ========================================
echo   VALIDATE_PERF_SINGLE - Demo OpenGL
echo ========================================
echo.

echo [1/4] Building Profile x64...
call "%REPO%\tools\validate_build.bat" Profile
if errorlevel 1 exit /b 1

echo [2/4] Cleaning old OpenGL perf artifacts...
del /q "%REPO%\Profile\perf_log.csv" 2>nul
del /q "%REPO%\Profile\gl_perf_log.csv" 2>nul
del /q "%REPO%\Profile\gl_perf.json" 2>nul

echo [3/4] Running OpenGL perf scene...
"%REPO%\Profile\SKULLBONEZ_CORE.exe" --renderer gl --vsync off --fixed-step --scene SkullbonezData/scenes/perf_test.scene
if errorlevel 1 (
    echo FAIL: OpenGL perf_test scene crashed.
    exit /b 2
)
if not exist "%REPO%\Profile\perf_log.csv" (
    echo FAIL: perf_log.csv not produced for OpenGL.
    exit /b 3
)
move /Y "%REPO%\Profile\perf_log.csv" "%REPO%\Profile\gl_perf_log.csv" >nul
if errorlevel 1 (
    echo FAIL: Could not store OpenGL perf log.
    exit /b 4
)

echo [4/4] Analyzing OpenGL performance...
set "SKORE_REPO=%REPO%"
set "PYTHONUTF8=1"
set "PYTHONIOENCODING=utf-8"
"%PYTHON_EXE%" "%REPO%\Agentic\Skills\skore-render-test\analyze_perf.py" --renderer gl --csv "%REPO%\Profile\gl_perf_log.csv" --out-dir "%REPO%\Profile"
if errorlevel 1 (
    echo FAIL: OpenGL perf analysis script failed.
    exit /b 5
)

if exist "%REPO%\TestOutput\baselines\gl_perf.json" (
    echo.
    echo OpenGL performance comparison vs baseline:
    "%PYTHON_EXE%" "%REPO%\Agentic\Skills\skore-render-test\perf_compare.py" --current "%REPO%\Profile\gl_perf.json" --previous "%REPO%\TestOutput\baselines\gl_perf.json"
    if errorlevel 1 (
        echo.
        echo WARNING: OpenGL performance regression detected. Review output above.
        REM Perf regressions need human judgment, so the script still exits 0.
    )
) else (
    echo No baseline found at TestOutput\baselines\gl_perf.json - skipping OpenGL comparison.
)

echo.
echo Artifacts:
echo   Profile\gl_perf_log.csv
echo   Profile\gl_perf.json
echo.
echo ========================================
echo   VALIDATE_PERF_SINGLE: COMPLETE
echo ========================================

popd
exit /b 0
