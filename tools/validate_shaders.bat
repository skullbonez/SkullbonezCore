@rem
@rem File: tools/validate_shaders.bat
@rem Purpose:
@rem   Documents and runs the validate_shaders.bat developer/validation helper script.
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
setlocal
REM ===============================================================
REM  validate_shaders.bat - Shader file and manifest contract check.
REM  Warnings indicate incomplete manifest coverage; hard errors fail.
REM ===============================================================

set "REPO=%~dp0.."
pushd "%REPO%"
echo.
echo ========================================
echo   VALIDATE_SHADERS - Shader Contracts
echo ========================================
echo.

call "%~dp0find_python.bat"
if errorlevel 1 exit /b 99

"%PYTHON_EXE%" "%~dp0validate_shaders.py" --repo "%REPO%" %*
if errorlevel 1 (
    echo FAIL: Shader contract validation failed.
    popd
    exit /b 1
)

call "%~dp0validate_ready_builds.bat"
if errorlevel 1 (
    popd
    exit /b 2
)

echo.
echo ========================================
echo   VALIDATE_SHADERS: ALL PASSED
echo ========================================
popd
exit /b 0
