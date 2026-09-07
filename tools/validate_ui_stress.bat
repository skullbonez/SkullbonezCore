@rem Runs the native GameUI stress scene and checks renderer diagnostics.
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

echo [1/4] Checking formatting...
call "%~dp0validate_format.bat"
if errorlevel 1 exit /b 1

echo [2/4] Building Profile x64...
call "%~dp0validate_build.bat" Profile
if errorlevel 1 exit /b 2

echo [3/4] Running deterministic Game UI stress scene...
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

echo [4/4] Checking logs and DX12 validation...
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
