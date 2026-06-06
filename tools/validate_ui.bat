@echo off
setlocal enabledelayedexpansion
REM ===============================================================
REM  validate_ui.bat - Optional in-game UI visual suite.
REM  Use for: UI window, blur, screenshot automation, and UI controls.
REM ===============================================================

set "REPO=%~dp0.."
pushd "%REPO%"
echo.
echo ========================================
echo   VALIDATE_UI - Optional UI Suite
echo ========================================
echo.

echo [1/9] Checking formatting...
call "%~dp0validate_format.bat"
if errorlevel 1 exit /b 1
call "%~dp0find_python.bat"
if errorlevel 1 exit /b 1

echo [2/9] Building Profile x64...
call "%~dp0validate_build.bat" Profile
if errorlevel 1 exit /b 2

echo [3/9] Cleaning old UI artifacts...
del /q "%REPO%\Profile\ui_*.bmp" 2>nul
del /q "%REPO%\Profile\ui_*_perf.csv" 2>nul
del /q "%REPO%\Profile\ui_*_stdout.txt" 2>nul
del /q "%REPO%\Profile\ui_*_stderr.txt" 2>nul
del /q "%REPO%\dx12_validation.txt" 2>nul

echo [4/9] Running GL UI suite...
call :run_renderer gl ""
if errorlevel 1 exit /b 3

echo [5/9] Running DX11 UI suite...
call :run_renderer dx11 "--renderer dx11"
if errorlevel 1 exit /b 4

echo [6/9] Running DX12 UI suite...
call :run_renderer dx12 "--renderer dx12"
if errorlevel 1 exit /b 5

echo [7/9] Checking logs and DX12 validation...
set "STDOUT_CLEAN=1"
for %%r in (gl dx11 dx12) do (
    findstr /I /C:"error" /C:"warning" /C:"failed" "%REPO%\Profile\ui_%%r_stdout.txt" >nul 2>&1
    if not errorlevel 1 (
        echo   FAIL [%%r]: Unexpected error/warning in stdout:
        findstr /I /C:"error" /C:"warning" /C:"failed" "%REPO%\Profile\ui_%%r_stdout.txt"
        set "STDOUT_CLEAN=0"
    )
    findstr /I /C:"error" /C:"warning" /C:"failed" "%REPO%\Profile\ui_%%r_stderr.txt" >nul 2>&1
    if not errorlevel 1 (
        echo   FAIL [%%r]: Unexpected error/warning in stderr:
        findstr /I /C:"error" /C:"warning" /C:"failed" "%REPO%\Profile\ui_%%r_stderr.txt"
        set "STDOUT_CLEAN=0"
    )
)
if "%STDOUT_CLEAN%"=="0" (
    echo FAIL: One or more UI suite runs produced error/warning output.
    exit /b 6
)
call "%~dp0check_dx12_validation.bat"
if errorlevel 1 exit /b 7

echo [8/9] Checking UI screenshots and blur metrics...
set "SKORE_REPO=%REPO%"
"%PYTHON_EXE%" "%~dp0check_ui_blur.py"
if errorlevel 1 exit /b 8

echo [9/9] Exporting shareable UI PNG artifact...
"%PYTHON_EXE%" "%~dp0export_screenshot_png.py" "%REPO%\Profile\ui_gl_profiler_timeline.bmp" "%REPO%\Profile\ui_gl_profiler_timeline.png" --max-width 1080
if errorlevel 1 exit /b 9

echo.
echo ========================================
echo   VALIDATE_UI: ALL PASSED
echo ========================================
popd
exit /b 0

:run_renderer
set "RENDERER=%~1"
set "ARGS=%~2"
"%REPO%\Profile\SKULLBONEZ_CORE.exe" %ARGS% --vsync off --suite SkullbonezData/scenes/ui_tests.suite >"%REPO%\Profile\ui_%RENDERER%_stdout.txt" 2>"%REPO%\Profile\ui_%RENDERER%_stderr.txt"
if errorlevel 1 (
    echo FAIL: %RENDERER% UI suite exited with error.
    exit /b 1
)
for %%s in (blur_off blur_on profiler_hierarchy profiler_timeline renderer_combo small_scroll minimized) do (
    if not exist "%REPO%\Profile\ui_%%s.bmp" (
        echo FAIL: %RENDERER% did not produce ui_%%s.bmp.
        exit /b 1
    )
    move /Y "%REPO%\Profile\ui_%%s.bmp" "%REPO%\Profile\ui_%RENDERER%_%%s.bmp" >nul
)
if not exist "%REPO%\Profile\ui_profiler_timeline_perf.csv" (
    echo FAIL: %RENDERER% did not produce ui_profiler_timeline_perf.csv.
    exit /b 1
)
move /Y "%REPO%\Profile\ui_profiler_timeline_perf.csv" "%REPO%\Profile\ui_%RENDERER%_profiler_timeline_perf.csv" >nul
exit /b 0
