@rem
@rem File: tools/capture_ui_screenshot.bat
@rem Purpose:
@rem   Documents and runs the capture_ui_screenshot.bat developer/validation helper script.
@rem
@rem Mental model:
@rem   Tools are command-line guardrails around builds, validation, screenshots,
@rem   diagnostics, and artifact handling. They make the safe path repeatable and
@rem   keep output bounded for humans and agents.
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
@rem   - Agentic/Reference/comment-style-guide.md
@rem
@rem
@echo off
setlocal enabledelayedexpansion
REM ===============================================================
REM  capture_ui_screenshot.bat - Capture the in-game UI as a PNG.
REM  Usage: tools\capture_ui_screenshot.bat [dx12] [output.png] [max_width]
REM ===============================================================

set "REPO=%~dp0.."
pushd "%REPO%"

set "RENDERER=%~1"
if "%RENDERER%"=="" set "RENDERER=dx12"
set "OUTPUT=%~2"
if "%OUTPUT%"=="" set "OUTPUT=%REPO%\Profile\codex_ui_capture.png"
set "MAX_WIDTH=%~3"
if "%MAX_WIDTH%"=="" set "MAX_WIDTH=1080"

set "RENDER_ARGS="
if /I "%RENDERER%"=="dx12" set "RENDER_ARGS=--renderer dx12"
if /I not "%RENDERER%"=="dx12" (
    echo ERROR: Unknown renderer "%RENDERER%". DX12 is the only runtime renderer.
    popd
    exit /b 1
)

if not exist "%REPO%\Profile\SKULLBONEZ_CORE.exe" (
    echo Profile executable is missing; building Profile x64...
    call "%~dp0validate_build.bat" Profile
    if errorlevel 1 (
        popd
        exit /b 2
    )
)

call "%~dp0find_python.bat"
if errorlevel 1 (
    popd
    exit /b 99
)

set "BMP=%REPO%\Profile\ui_profiler_timeline.bmp"
set "STDOUT=%REPO%\Profile\codex_ui_capture_stdout.txt"
set "STDERR=%REPO%\Profile\codex_ui_capture_stderr.txt"
del /q "%BMP%" "%OUTPUT%" "%STDOUT%" "%STDERR%" 2>nul

echo Capturing %RENDERER% UI scene...
"%REPO%\Profile\SKULLBONEZ_CORE.exe" %RENDER_ARGS% --vsync off --scene SkullbonezData/scenes/ui_profiler_timeline.scene.json >"%STDOUT%" 2>"%STDERR%"
if errorlevel 1 (
    echo ERROR: UI capture run failed. See:
    echo   %STDOUT%
    echo   %STDERR%
    popd
    exit /b 3
)

if not exist "%BMP%" (
    echo ERROR: Expected screenshot was not produced: %BMP%
    popd
    exit /b 4
)

"%PYTHON_EXE%" "%~dp0export_screenshot_png.py" "%BMP%" "%OUTPUT%" --max-width %MAX_WIDTH%
if errorlevel 1 (
    popd
    exit /b 5
)

echo UI_CAPTURE_PNG=%OUTPUT%
popd
exit /b 0
