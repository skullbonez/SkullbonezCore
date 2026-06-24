@rem
@rem File: tools/validate_runtime_boundaries.bat
@rem Purpose:
@rem   Validate runtime ownership guardrails after the Run decomposition plan.
@rem
@rem Mental model:
@rem   This is a lightweight architecture check. It keeps Run.h as the runtime
@rem   composition root and catches extracted subsystem state creeping back in.
@rem
@rem Related:
@rem   - Agentic/Plans/runtime-run-decomposition-plan.md
@rem   - tools/check_runtime_boundaries.py
@rem
@echo off
setlocal
REM ===============================================================
REM  validate_runtime_boundaries.bat - Run boundary drift check.
REM ===============================================================

set "REPO=%~dp0.."
pushd "%REPO%"
echo.
echo ========================================
echo   VALIDATE_RUNTIME_BOUNDARIES
echo ========================================
echo.

call "%~dp0find_python.bat"
if errorlevel 1 (
    popd
    exit /b 99
)

"%PYTHON_EXE%" "%~dp0check_runtime_boundaries.py" --repo "%REPO%" %*
if errorlevel 1 (
    echo FAIL: Runtime boundary validation failed.
    popd
    exit /b 1
)

echo.
echo ========================================
echo   VALIDATE_RUNTIME_BOUNDARIES: ALL PASSED
echo ========================================
popd
exit /b 0
