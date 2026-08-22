@rem
@rem File: tools/validate_concepts.bat
@rem Purpose:
@rem   Documents and runs the validate_concepts.bat developer/validation helper script.
@rem
@rem Summary:
@rem   Tools are command-line guardrails around builds, validation, screenshots,
@rem   diagnostics, and artifact handling. They make the safe path repeatable and
@rem   keep output bounded for humans and agents.
@rem
@rem Glossary:
@rem   JSON (JavaScript Object Notation): Structured text format used by
@rem   diagnostics, baselines, and tool reports.
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
setlocal
REM ===============================================================
REM  validate_concepts.bat - Finite concept-scene validation tiers.
REM  Usage: tools\validate_concepts.bat [smoke|core|full] [dx12] [frames]
REM ===============================================================

if /I "%~1"=="--help" goto :help
if /I "%~1"=="/?" goto :help

set "REPO=%~dp0.."
set "TIER=smoke"
set "RENDERER=dx12"
set "FRAMES=2"
if not "%~1"=="" set "TIER=%~1"
if not "%~2"=="" set "RENDERER=%~2"
if not "%~3"=="" set "FRAMES=%~3"

pushd "%REPO%"
echo.
echo ========================================
echo   VALIDATE_CONCEPTS - %TIER% / %RENDERER%
echo ========================================
echo.

echo [1/3] Checking formatting...
call "%~dp0validate_format.bat"
if errorlevel 1 exit /b 1
call "%~dp0find_python.bat"
if errorlevel 1 exit /b 99

echo [2/3] Building Profile x64...
call "%~dp0validate_build.bat" Profile
if errorlevel 1 exit /b 2

echo [3/3] Running concept tier...
"%PYTHON_EXE%" "%~dp0validate_concepts.py" --repo "%REPO%" --tier "%TIER%" --renderer "%RENDERER%" --frames "%FRAMES%"
if errorlevel 1 (
    echo FAIL: Concept validation failed.
    popd
    exit /b 3
)

call "%~dp0validate_ready_builds.bat"
if errorlevel 1 (
    popd
    exit /b 4
)

echo.
echo ========================================
echo   VALIDATE_CONCEPTS: ALL PASSED
echo ========================================
popd
exit /b 0

:help
echo Usage: tools\validate_concepts.bat [smoke^|core^|full] [dx12] [frames]
echo.
echo Defaults to: smoke dx12 2
echo Writes logs and JSON under TestOutput\validation\concepts\^<run-id^>.
exit /b 0
