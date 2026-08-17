@rem
@rem File: tools/validate_ui.bat
@rem Purpose:
@rem   Runs the optional DX12 in-game UI visual and interaction gate.
@rem
@rem Summary:
@rem   This gate owns the Profile UI build, Automation interaction build,
@rem   deterministic screenshot suite, causal-window pointer probe, screenshot
@rem   analysis, and shareable exports. Each phase leaves bounded artifacts for
@rem   human or CI review.
@rem
@rem Glossary:
@rem   Validation gate: Repository script that proves a class of changes before
@rem   commit or PR.
@rem
@rem Invariants:
@rem   - Tool output should be bounded and readable because agents and humans use
@rem   it for decisions.
@rem
@rem Related:
@rem   - AGENTS.md
@rem   - tools/check_causal_tree_interaction.py
@rem
@rem
@echo off
setlocal enabledelayedexpansion

set "REPO=%~dp0.."
pushd "%REPO%"
echo.
echo ========================================
echo   VALIDATE_UI - Optional UI Suite
echo ========================================
echo.

echo [1/8] Checking formatting...
call "%~dp0validate_format.bat"
if errorlevel 1 exit /b 1
call "%~dp0find_python.bat"
if errorlevel 1 exit /b 1

echo [2/8] Building Profile and Automation x64...
call "%~dp0validate_build.bat" Profile
if errorlevel 1 exit /b 2
call "%~dp0validate_build.bat" Automation
if errorlevel 1 exit /b 2

echo [3/8] Cleaning old UI artifacts...
del /q "%REPO%\Profile\ui_*.bmp" 2>nul
del /q "%REPO%\Profile\ui_*_perf.csv" 2>nul
del /q "%REPO%\Profile\ui_*_stdout.txt" 2>nul
del /q "%REPO%\Profile\ui_*_stderr.txt" 2>nul
del /q "%REPO%\dx12_validation.txt" 2>nul

echo [4/8] Running DX12 UI suite...
"%REPO%\Profile\SKULLBONEZ_CORE.exe" --renderer dx12 --vsync off --automation-hidden-window --suite SkullbonezData/scenes/ui_tests.suite.json >"%REPO%\Profile\ui_dx12_stdout.txt" 2>"%REPO%\Profile\ui_dx12_stderr.txt"
if errorlevel 1 (
    echo FAIL: dx12 UI suite exited with error.
    exit /b 3
)
for %%s in (blur_off blur_on blur_moved_off blur_moved_on profiler_default profiler_hierarchy profiler_timeline physics_toggles scene_options controls renderer_combo water_combo scene_complete small_scroll controls_clip_scroll controls_bottom controls_bottom_bg min_size min_size_bg minimized performance_histogram) do (
    if not exist "%REPO%\Profile\ui_%%s.bmp" (
        echo FAIL: dx12 did not produce ui_%%s.bmp.
        exit /b 3
    )
    move /Y "%REPO%\Profile\ui_%%s.bmp" "%REPO%\Profile\ui_dx12_%%s.bmp" >nul
)
if not exist "%REPO%\Profile\ui_profiler_timeline_perf.csv" (
    echo FAIL: dx12 did not produce ui_profiler_timeline_perf.csv.
    exit /b 3
)
move /Y "%REPO%\Profile\ui_profiler_timeline_perf.csv" "%REPO%\Profile\ui_dx12_profiler_timeline_perf.csv" >nul

echo [5/8] Checking logs and DX12 validation...
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

echo [6/8] Checking UI screenshots and blur metrics...
set "SKORE_REPO=%REPO%"
"%PYTHON_EXE%" "%~dp0check_ui_blur.py"
if errorlevel 1 exit /b 6

echo [7/8] Checking causal hierarchy and repeated physical row selection...
"%PYTHON_EXE%" "%~dp0check_causal_tree_interaction.py" --self-test
if errorlevel 1 exit /b 7
"%PYTHON_EXE%" "%~dp0check_causal_tree_interaction.py" --repo "%REPO%" --executable "%REPO%\Automation\SKULLBONEZ_CORE.exe"
if errorlevel 1 exit /b 7

echo [8/8] Exporting shareable UI PNG artifact...
"%PYTHON_EXE%" "%~dp0export_screenshot_png.py" "%REPO%\Profile\ui_dx12_profiler_timeline.bmp" "%REPO%\Profile\ui_dx12_profiler_timeline.png" --max-width 1080
if errorlevel 1 exit /b 8
"%PYTHON_EXE%" "%~dp0export_screenshot_png.py" "%REPO%\Profile\ui_dx12_performance_histogram.bmp" "%REPO%\Profile\ui_dx12_performance_histogram.png" --max-width 1080
if errorlevel 1 exit /b 8

call "%~dp0validate_ready_builds.bat"
if errorlevel 1 (
    popd
    exit /b 9
)

echo.
echo ========================================
echo   VALIDATE_UI: ALL PASSED
echo ========================================
popd
exit /b 0
