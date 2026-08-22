@rem
@rem File: tools/validate_demo_stress.bat
@rem Purpose:
@rem   Documents and runs the validate_demo_stress.bat developer/validation helper script.
@rem
@rem Summary:
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
@rem
@rem
@echo off
setlocal enabledelayedexpansion
REM ===============================================================
REM  validate_demo_stress.bat - Generated demo interaction crash test.
REM  Runs the actual generated demo scene while hammering UI controls.
REM ===============================================================

set "REPO=%~dp0.."
pushd "%REPO%"
echo.
echo ========================================
echo   VALIDATE_DEMO_STRESS - Demo Crash Sweep
echo ========================================
echo.

echo [1/4] Checking formatting...
call "%~dp0validate_format.bat"
if errorlevel 1 exit /b 1

echo [2/4] Building Profile x64...
call "%~dp0validate_build.bat" Profile
if errorlevel 1 exit /b 2

echo [3/4] Running generated demo interaction stress...
del /q "%REPO%\Profile\demo_stress_stdout.txt" 2>nul
del /q "%REPO%\Profile\demo_stress_stderr.txt" 2>nul
del /q "%REPO%\dx12_validation.txt" 2>nul
"%REPO%\Profile\SKULLBONEZ_CORE.exe" --renderer dx12 --vsync off --seed 9001 --frames 360 --ui-stress --ui-stress-seed 1357911 --ui-stress-actions 8 >"%REPO%\Profile\demo_stress_stdout.txt" 2>"%REPO%\Profile\demo_stress_stderr.txt"
if errorlevel 1 (
    echo FAIL: Generated demo stress exited with error.
    type "%REPO%\Profile\demo_stress_stdout.txt"
    type "%REPO%\Profile\demo_stress_stderr.txt"
    exit /b 3
)

echo [4/4] Checking logs and DX12 validation...
set "STRESS_LOGS_CLEAN=1"
findstr /I /C:"error" /C:"warning" /C:"failed" "%REPO%\Profile\demo_stress_stdout.txt" >nul 2>&1
if not errorlevel 1 (
    echo FAIL: Unexpected error/warning in demo stress stdout:
    findstr /I /C:"error" /C:"warning" /C:"failed" "%REPO%\Profile\demo_stress_stdout.txt"
    set "STRESS_LOGS_CLEAN=0"
)
findstr /I /C:"error" /C:"warning" /C:"failed" "%REPO%\Profile\demo_stress_stderr.txt" >nul 2>&1
if not errorlevel 1 (
    echo FAIL: Unexpected error/warning in demo stress stderr:
    findstr /I /C:"error" /C:"warning" /C:"failed" "%REPO%\Profile\demo_stress_stderr.txt"
    set "STRESS_LOGS_CLEAN=0"
)
if "%STRESS_LOGS_CLEAN%"=="0" exit /b 4

call "%~dp0check_dx12_validation.bat"
if errorlevel 1 exit /b 5

call "%~dp0validate_ready_builds.bat"
if errorlevel 1 (
    popd
    exit /b 6
)

echo.
echo ========================================
echo   VALIDATE_DEMO_STRESS: ALL PASSED
echo ========================================
popd
exit /b 0
