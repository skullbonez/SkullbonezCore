@rem
@rem File: tools/validate_replay_v2_artifact.bat
@rem Purpose:
@rem   Runs the focused replay v2 save/load/restore/query artifact validation.
@rem
@rem Mental model:
@rem   This gate proves the executable can write a real v2 presentation
@rem   .skreplay file, reload presentation data, restore a saved solver
@rem   checkpoint, report an expected saved-file restore failure, and expose
@rem   bounded query output plus a SkullScope-compatible slice.
@rem
@rem Glossary:
@rem   Replay v2 artifact: Chunked binary presentation .skreplay file.
@rem   SkullScope slice: Bounded NDJSON exported from a replay window for
@rem   physics_query import.
@rem
@rem Related:
@rem   - tools/replay_query.py
@rem   - AGENTS.md
@rem
@echo off
setlocal enabledelayedexpansion

set "REPO=%~dp0.."
pushd "%REPO%"
call "%~dp0find_python.bat"
if errorlevel 1 exit /b 99

echo.
echo ========================================
echo   VALIDATE_REPLAY_V2_ARTIFACT
echo ========================================
echo.

echo [1/2] Building Debug x64...
call "%~dp0validate_build.bat" Debug
if errorlevel 1 (
    popd
    exit /b 1
)

echo [2/2] Checking replay v2 save/query artifact...
set "SKORE_REPO=%REPO%"
"%PYTHON_EXE%" "%~dp0check_replay_v2_artifact.py"
if errorlevel 1 (
    echo FAIL: replay v2 artifact validation failed.
    echo       Artifact: TestOutput\validation\replay_v2\replay_save_probe.skreplay
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
echo   VALIDATE_REPLAY_V2_ARTIFACT: ALL PASSED
echo ========================================
popd
exit /b 0
