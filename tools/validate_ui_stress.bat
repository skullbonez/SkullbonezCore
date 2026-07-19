@rem
@rem File: tools/validate_ui_stress.bat
@rem Purpose:
@rem   Documents and runs the validate_ui_stress.bat developer/validation helper script.
@rem
@rem Mental model:
@rem   Tools are command-line guardrails around builds, validation, screenshots,
@rem   diagnostics, and artifact handling. They make the safe path repeatable and
@rem   keep output bounded for humans and agents.
@rem
@rem Glossary:
@rem   Validation gate: Repository script that proves a class of changes before
@rem   commit or PR.
@rem   Exclusive surface matrix: Sequential Legacy and ImGui runs plus atomic
@rem   hot swaps that prove only one development UI owns focus at a time.
@rem
@rem Invariants:
@rem   - Tool output should be bounded and readable because agents and humans use
@rem   it for decisions.
@rem   - Legacy and ImGui stress execute sequentially; they are never active in
@rem   parallel processes during this gate.
@rem
@rem Related:
@rem   - AGENTS.md
@rem   - Agentic/Reference/comment-style-guide.md
@rem
@rem
@echo off
setlocal enabledelayedexpansion
REM ===============================================================
REM  validate_ui_stress.bat - Deterministic UI-only stress crash test.
REM  Use for: UI controls, tabs, combo state, and DX12 UI state over a backdrop.
REM ===============================================================

set "REPO=%~dp0.."
pushd "%REPO%"
echo.
echo ========================================
echo   VALIDATE_UI_STRESS - UI-Only Crash Sweep
echo ========================================
echo.

echo [1/6] Checking formatting...
call "%~dp0validate_format.bat"
if errorlevel 1 exit /b 1

echo [2/6] Building Profile x64...
call "%~dp0validate_build.bat" Profile
if errorlevel 1 exit /b 2

echo [3/6] Running deterministic Legacy UI stress scene...
del /q "%REPO%\Profile\ui_stress_stdout.txt" 2>nul
del /q "%REPO%\Profile\ui_stress_stderr.txt" 2>nul
del /q "%REPO%\dx12_validation.txt" 2>nul
"%REPO%\Profile\SKULLBONEZ_CORE.exe" --renderer dx12 --vsync off --scene SkullbonezData/scenes/ui_stress.scene.json >"%REPO%\Profile\ui_stress_stdout.txt" 2>"%REPO%\Profile\ui_stress_stderr.txt"
if errorlevel 1 (
    echo FAIL: UI stress scene exited with error.
    type "%REPO%\Profile\ui_stress_stdout.txt"
    type "%REPO%\Profile\ui_stress_stderr.txt"
    exit /b 3
)

call "%~dp0check_dx12_validation.bat"
if errorlevel 1 exit /b 4

echo [4/6] Building Automation x64...
call "%~dp0validate_build.bat" Automation
if errorlevel 1 exit /b 5

echo [5/6] Running deterministic ImGui editor stress matrix...
if not exist "%REPO%\TestOutput\interaction" mkdir "%REPO%\TestOutput\interaction"
del /q "%REPO%\Automation\imgui_ui_stress_stdout.txt" 2>nul
del /q "%REPO%\Automation\imgui_ui_stress_stderr.txt" 2>nul
del /q "%REPO%\TestOutput\interaction\imgui_editor_stress_report.json" 2>nul
del /q "%REPO%\dx12_validation.txt" 2>nul
"%REPO%\Automation\SKULLBONEZ_CORE.exe" --renderer dx12 --vsync off --fixed-step --dev-ui imgui --replay on --replay-seconds 2 --scene SkullbonezData/scenes/ui_stress.scene.json --interaction-script SkullbonezData/interaction/imgui_editor_stress.json --interaction-report TestOutput/interaction/imgui_editor_stress_report.json --frames 180 >"%REPO%\Automation\imgui_ui_stress_stdout.txt" 2>"%REPO%\Automation\imgui_ui_stress_stderr.txt"
if errorlevel 1 (
    echo FAIL: ImGui editor stress matrix exited with error.
    type "%REPO%\Automation\imgui_ui_stress_stdout.txt"
    type "%REPO%\Automation\imgui_ui_stress_stderr.txt"
    exit /b 6
)
python -c "import json,sys; p=r'TestOutput/interaction/imgui_editor_stress_report.json'; d=json.load(open(p,encoding='utf-8')); sys.exit(0 if d.get('ok') is True else 1)"
if errorlevel 1 (
    echo FAIL: ImGui editor stress report is missing or not ok=true.
    type "%REPO%\TestOutput\interaction\imgui_editor_stress_report.json"
    exit /b 7
)

echo [6/6] Checking logs and DX12 validation...
set "STRESS_LOGS_CLEAN=1"
findstr /I /C:"error" /C:"warning" /C:"failed" "%REPO%\Profile\ui_stress_stdout.txt" >nul 2>&1
if not errorlevel 1 (
    echo FAIL: Unexpected error/warning in stress stdout:
    findstr /I /C:"error" /C:"warning" /C:"failed" "%REPO%\Profile\ui_stress_stdout.txt"
    set "STRESS_LOGS_CLEAN=0"
)
findstr /I /C:"error" /C:"warning" /C:"failed" "%REPO%\Profile\ui_stress_stderr.txt" >nul 2>&1
if not errorlevel 1 (
    echo FAIL: Unexpected error/warning in stress stderr:
    findstr /I /C:"error" /C:"warning" /C:"failed" "%REPO%\Profile\ui_stress_stderr.txt"
    set "STRESS_LOGS_CLEAN=0"
)
if "%STRESS_LOGS_CLEAN%"=="0" exit /b 4

findstr /I /C:"error" /C:"warning" /C:"failed" "%REPO%\Automation\imgui_ui_stress_stdout.txt" >nul 2>&1
if not errorlevel 1 (
    echo FAIL: Unexpected error/warning in ImGui stress stdout:
    findstr /I /C:"error" /C:"warning" /C:"failed" "%REPO%\Automation\imgui_ui_stress_stdout.txt"
    set "STRESS_LOGS_CLEAN=0"
)
findstr /I /C:"error" /C:"warning" /C:"failed" "%REPO%\Automation\imgui_ui_stress_stderr.txt" >nul 2>&1
if not errorlevel 1 (
    echo FAIL: Unexpected error/warning in ImGui stress stderr:
    findstr /I /C:"error" /C:"warning" /C:"failed" "%REPO%\Automation\imgui_ui_stress_stderr.txt"
    set "STRESS_LOGS_CLEAN=0"
)
if "%STRESS_LOGS_CLEAN%"=="0" exit /b 8

call "%~dp0check_dx12_validation.bat"
if errorlevel 1 exit /b 9

call "%~dp0validate_ready_builds.bat"
if errorlevel 1 (
    popd
    exit /b 10
)

echo.
echo ========================================
echo   VALIDATE_UI_STRESS: ALL PASSED
echo ========================================
popd
exit /b 0
