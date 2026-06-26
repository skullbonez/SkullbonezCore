@rem
@rem File: tools/validate_runtime_boundaries.bat
@rem Purpose:
@rem   Validate runtime ownership guardrails after the Run decomposition plan
@rem   and physics GameModelCollection compatibility ratchets.
@rem
@rem Mental model:
@rem   This is a lightweight architecture check. It keeps Run.h as the runtime
@rem   composition root, catches extracted subsystem state creeping back in, and
@rem   blocks new physics dependencies on the legacy GameModelCollection owner.
@rem
@rem Related:
@rem   - Agentic/Plans/runtime-run-decomposition-plan.md
@rem   - Agentic/Plans/engine-architecture-next-steps-plan.md
@rem   - tools/check_runtime_boundaries.py
@rem
@echo off
setlocal
REM ===============================================================
REM  validate_runtime_boundaries.bat - Runtime boundary drift check.
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
