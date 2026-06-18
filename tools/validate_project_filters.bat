@rem
@rem File: tools/validate_project_filters.bat
@rem Purpose:
@rem   Validate Visual Studio project item filters and path casing.
@rem
@rem Mental model:
@rem   Tools are command-line guardrails around builds, validation, screenshots,
@rem   diagnostics, and artifact handling. They make the safe path repeatable and
@rem   keep output bounded for humans and agents.
@rem
@rem Glossary:
@rem   Filter: A Visual Studio virtual folder stored in .vcxproj.filters.
@rem   Validation gate: Repository script that proves a class of changes before
@rem   commit or PR.
@rem
@rem Invariants:
@rem   - Tool output should be bounded and readable because agents and humans use
@rem     it for decisions.
@rem
@rem Related:
@rem   - AGENTS.md
@rem   - tools/README.md
@rem
@echo off
setlocal
REM ===============================================================
REM  validate_project_filters.bat - Visual Studio filter drift check.
REM ===============================================================

set "REPO=%~dp0.."
pushd "%REPO%"
echo.
echo ========================================
echo   VALIDATE_PROJECT_FILTERS
echo ========================================
echo.

call "%~dp0find_python.bat"
if errorlevel 1 (
    popd
    exit /b 99
)

"%PYTHON_EXE%" "%~dp0validate_project_filters.py" --repo "%REPO%" %*
if errorlevel 1 (
    echo FAIL: Project filter validation failed.
    popd
    exit /b 1
)

echo.
echo ========================================
echo   VALIDATE_PROJECT_FILTERS: ALL PASSED
echo ========================================
popd
exit /b 0
