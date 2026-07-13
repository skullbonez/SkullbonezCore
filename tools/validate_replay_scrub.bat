@rem
@rem File: tools/validate_replay_scrub.bat
@rem Purpose:
@rem   Runs the focused replay scrub, retained-restore, and prediction
@rem   determinism regressions.
@rem
@rem Mental model:
@rem   Tools are command-line guardrails around builds, validation, screenshots,
@rem   diagnostics, and artifact handling. This one proves the replay scrubber
@rem   can select an older presentation sample, retained restore can hash-
@rem   verify an older solver sample through bounded SkullScope queries, and
@rem   identical prediction interaction runs produce the same sampled trajectory
@rem   fingerprint, a 120-frame submitted-geometry hash window, and no replay
@rem   reserve growth during that steady window.
@rem
@rem Glossary:
@rem   SkullScope: Queryable physics diagnostics workflow backed by bounded
@rem   trace output and local queries.
@rem   Validation gate: Repository script that proves a class of changes before
@rem   commit or PR.
@rem   Steady window: Consecutive rendered frames where submitted replay geometry
@rem   and the replay reserve-growth counter remain unchanged.
@rem   Replay reserve growth: Approved runtime capacity increase for replay-owned
@rem   buffers, counted by RuntimeReserveAllocator.
@rem
@rem Invariants:
@rem   - The Debug executable is rebuilt before replay scrub probes run.
@rem   - Validation passes only after check_replay_scrub_regression.py verifies
@rem   the generated SkullScope traces.
@rem   - Prediction determinism passes only when two identical interaction
@rem   launches produce the same sampled trajectory fingerprint and the submitted
@rem   draw bytes stay stable for the no-growth steady-state window.
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
echo   VALIDATE_REPLAY_SCRUB - replay probes
echo ========================================
echo.

echo [1/3] Building Debug x64...
call "%~dp0validate_build.bat" Debug
if errorlevel 1 (
    popd
    exit /b 1
)

echo [2/3] Checking replay scrub and restore SkullScope probes...
set "SKORE_REPO=%REPO%"
"%PYTHON_EXE%" "%~dp0check_replay_scrub_regression.py"
if errorlevel 1 (
    echo FAIL: replay scrub/restore SkullScope regression detected.
    echo       Scrub trace:   Debug\replay_scrub.physicsdiag.ndjson
    echo       Restore trace: Debug\replay_restore.physicsdiag.ndjson
    popd
    exit /b 2
)

echo [3/3] Checking replay prediction trajectory determinism and submitted-geometry stability...
"%PYTHON_EXE%" "%~dp0check_replay_prediction_determinism.py"
if errorlevel 1 (
    echo FAIL: replay prediction determinism/stability probe detected drift.
    echo       Reports: TestOutput\validation\replay_prediction_determinism
    popd
    exit /b 4
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
