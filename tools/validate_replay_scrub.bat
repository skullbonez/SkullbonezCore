@rem
@rem File: tools/validate_replay_scrub.bat
@rem Purpose:
@rem   Runs the focused replay scrub SkullScope regression.
@rem
@rem Mental model:
@rem   Tools are command-line guardrails around builds, validation, screenshots,
@rem   diagnostics, and artifact handling. This one proves the replay scrubber
@rem   can select an older presentation sample and expose that proof through
@rem   bounded SkullScope queries.
@rem
@rem Glossary:
@rem   SkullScope: Queryable physics diagnostics workflow backed by bounded
@rem   trace output and local queries.
@rem   Validation gate: Repository script that proves a class of changes before
@rem   commit or PR.
@rem
@rem Related:
@rem   - AGENTS.md
@rem   - Agentic/Reference/comment-style-guide.md
@rem
@echo off
setlocal enabledelayedexpansion

set "REPO=%~dp0.."
pushd "%REPO%"
call "%~dp0find_python.bat"
if errorlevel 1 exit /b 99

echo.
echo ========================================
echo   VALIDATE_REPLAY_SCRUB - SkullScope
echo ========================================
echo.

echo [1/2] Building Debug x64...
call "%~dp0validate_build.bat" Debug
if errorlevel 1 (
    popd
    exit /b 1
)

echo [2/2] Checking replay scrub SkullScope probe...
set "SKORE_REPO=%REPO%"
"%PYTHON_EXE%" "%~dp0check_replay_scrub_regression.py"
if errorlevel 1 (
    echo FAIL: replay scrub SkullScope regression detected.
    echo       Trace: Debug\replay_scrub.physicsdiag.ndjson
    popd
    exit /b 2
)

call "%~dp0validate_ready_builds.bat"
if errorlevel 1 (
    popd
    exit /b 3
)

echo.
echo ========================================
echo   VALIDATE_REPLAY_SCRUB: ALL PASSED
echo ========================================
popd
exit /b 0
