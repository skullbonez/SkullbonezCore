@rem
@rem File: tools/validate_scene_loads.bat
@rem Purpose:
@rem   Documents and runs the validate_scene_loads.bat developer/validation helper script.
@rem
@rem Mental model:
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
REM  validate_scene_loads.bat - Boot-only sweep for every .scene.json.
REM  Builds Profile, enumerates all SkullbonezData\scenes\*.scene.json
REM  files, then loads each scene with a timeout so setup failures
REM  are caught without waiting for scenarios to complete.
REM ===============================================================

set "REPO=%~dp0.."
pushd "%REPO%"
call "%~dp0find_python.bat"
if errorlevel 1 exit /b 99

echo [1/2] Building Profile x64...
call "%~dp0validate_build.bat" Profile
if errorlevel 1 exit /b 1

echo [2/2] Running load-only scene sweep...
"%PYTHON_EXE%" "%~dp0validate_scene_loads.py" %*
if errorlevel 1 exit /b 2
call "%~dp0validate_ready_builds.bat"
if errorlevel 1 (
    popd
    exit /b 3
)
popd
exit /b 0
