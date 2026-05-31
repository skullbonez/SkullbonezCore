@echo off
setlocal enabledelayedexpansion
REM ===============================================================
REM  validate_perf.bat - Performance regression detection.
REM  Use for: optimization work, hot-path changes, allocation changes.
REM  Runtime: about 1 minute.
REM  Exit 0 = build+run succeeded; perf regressions are shown for review.
REM ===============================================================

set "REPO=%~dp0.."
pushd "%REPO%"
call "%~dp0find_python.bat"
if errorlevel 1 exit /b 99
call "%~dp0find_git.bat"
if errorlevel 1 exit /b 99
echo.
echo ========================================
echo   VALIDATE_PERF - Performance Check
echo ========================================
echo.

echo [1/4] Building Profile x64...
call "%~dp0validate_build.bat" Profile
if errorlevel 1 exit /b 1

echo [2/4] Cleaning old perf artifacts...
del /q "%REPO%\Profile\perf_log.csv" 2>nul
del /q "%REPO%\Profile\*_perf_log.csv" 2>nul
del /q "%REPO%\Profile\*_perf.json" 2>nul

echo [3/4] Running tri-renderer perf tests...
call :RunPerf gl "" 2
if errorlevel 1 exit /b %ERRORLEVEL%
call :RunPerf dx11 "--renderer dx11" 3
if errorlevel 1 exit /b %ERRORLEVEL%
call :RunPerf dx12 "--renderer dx12" 4
if errorlevel 1 exit /b %ERRORLEVEL%

echo [4/4] Analyzing and comparing performance...
set "SKORE_REPO=%REPO%"
set "PYTHONUTF8=1"
set "PYTHONIOENCODING=utf-8"

for %%r in (gl dx11 dx12) do (
    echo.
    echo Analyzing %%r performance...
    "%PYTHON_EXE%" "%REPO%\Agentic\Skills\skore-render-test\analyze_perf.py" --renderer %%r --csv "%REPO%\Profile\%%r_perf_log.csv" --out-dir "%REPO%\Profile"
    if errorlevel 1 (
        echo FAIL: %%r perf analysis script failed.
        exit /b 5
    )
)

set "REGRESSION_WARNINGS=0"
for %%r in (gl dx11 dx12) do (
    if exist "%REPO%\TestOutput\baselines\%%r_perf.json" (
        echo.
        echo %%r performance comparison vs baseline:
        "%PYTHON_EXE%" "%REPO%\Agentic\Skills\skore-render-test\perf_compare.py" --current "%REPO%\Profile\%%r_perf.json" --previous "%REPO%\TestOutput\baselines\%%r_perf.json"
        if errorlevel 1 (
            echo.
            echo WARNING: %%r performance regression detected. Review output above.
            REM Perf regressions need human judgment, so the script still exits 0.
            set "REGRESSION_WARNINGS=1"
        )
    ) else (
        echo No baseline found at TestOutput\baselines\%%r_perf.json - skipping %%r comparison.
    )
)

echo.
echo ========================================
echo   VALIDATE_PERF: COMPLETE
echo ========================================
if "%REGRESSION_WARNINGS%"=="1" (
    echo   Review performance warnings above.
    echo ========================================
)
popd
exit /b 0

:RunPerf
set "RENDERER=%~1"
set "RENDERER_ARGS=%~2"
set "FAIL_CODE=%~3"
echo.
echo Running %RENDERER% perf test...
del /q "%REPO%\Profile\perf_log.csv" 2>nul
"%REPO%\Profile\SKULLBONEZ_CORE.exe" %RENDERER_ARGS% --vsync off --fixed-step --scene SkullbonezData/scenes/perf_test.scene
if errorlevel 1 (
    echo FAIL: perf_test scene crashed for %RENDERER%.
    exit /b %FAIL_CODE%
)
if not exist "%REPO%\Profile\perf_log.csv" (
    echo FAIL: perf_log.csv not produced for %RENDERER%.
    exit /b %FAIL_CODE%
)
move /Y "%REPO%\Profile\perf_log.csv" "%REPO%\Profile\%RENDERER%_perf_log.csv" >nul
if errorlevel 1 (
    echo FAIL: Could not store %RENDERER% perf log.
    exit /b %FAIL_CODE%
)
exit /b 0
