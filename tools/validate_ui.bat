@rem
@rem File: tools/validate_ui.bat
@rem Purpose:
@rem   Documents and runs the validate_ui.bat developer/validation helper script.
@rem
@rem Mental model:
@rem   Tools are command-line guardrails around builds, validation, screenshots,
@rem   diagnostics, and artifact handling. They make the safe path repeatable and
@rem   keep output bounded for humans and agents.
@rem
@rem Glossary:
@rem   DX12 (DirectX 12): Production renderer API used for explicit GPU resource,
@rem   descriptor, and command-list control.
@rem   Validation gate: Repository script that proves a class of changes before
@rem   commit or PR.
@rem
@rem Invariants:
@rem   - Tool output should be bounded and readable because agents and humans use
@rem   it for decisions.
@rem
@rem Related:
@rem   - AGENTS.md
@rem   - Agentic/Reference/comment-style-guide.md
@rem
@rem
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

echo [1/7] Checking formatting...
call "%~dp0validate_format.bat"
if errorlevel 1 exit /b 1
call "%~dp0find_python.bat"
if errorlevel 1 exit /b 1

echo [2/7] Building Profile x64...
call "%~dp0validate_build.bat" Profile
if errorlevel 1 exit /b 2

echo [3/7] Cleaning old UI artifacts...
del /q "%REPO%\Profile\ui_*.bmp" 2>nul
del /q "%REPO%\Profile\ui_*_perf.csv" 2>nul
del /q "%REPO%\Profile\ui_*_stdout.txt" 2>nul
del /q "%REPO%\Profile\ui_*_stderr.txt" 2>nul
del /q "%REPO%\dx12_validation.txt" 2>nul

echo [4/7] Running DX12 UI suite...
call :run_renderer dx12 "--renderer dx12"
if errorlevel 1 exit /b 3

echo [5/7] Checking logs and DX12 validation...
set "STDOUT_CLEAN=1"
for %%r in (dx12) do (
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
    exit /b 4
)
call "%~dp0check_dx12_validation.bat"
if errorlevel 1 exit /b 5

echo [6/7] Checking UI screenshots and blur metrics...
set "SKORE_REPO=%REPO%"
"%PYTHON_EXE%" "%~dp0check_ui_blur.py"
if errorlevel 1 exit /b 6

echo [7/7] Exporting shareable UI PNG artifact...
"%PYTHON_EXE%" "%~dp0export_screenshot_png.py" "%REPO%\Profile\ui_dx12_profiler_timeline.bmp" "%REPO%\Profile\ui_dx12_profiler_timeline.png" --max-width 1080
if errorlevel 1 exit /b 7
"%PYTHON_EXE%" "%~dp0export_screenshot_png.py" "%REPO%\Profile\ui_dx12_performance_histogram.bmp" "%REPO%\Profile\ui_dx12_performance_histogram.png" --max-width 1080
if errorlevel 1 exit /b 7

call "%~dp0validate_ready_builds.bat"
if errorlevel 1 (
    popd
    exit /b 8
)

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
for %%s in (blur_off blur_on blur_moved_off blur_moved_on profiler_default profiler_hierarchy profiler_timeline physics_toggles scene_options controls renderer_combo water_combo scene_complete small_scroll controls_clip_scroll controls_bottom controls_bottom_bg min_size min_size_bg minimized performance_histogram) do (
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
