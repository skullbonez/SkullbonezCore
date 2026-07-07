@rem
@rem File: tools/validate_fast.bat
@rem Purpose:
@rem   Documents and runs the validate_fast.bat developer/validation helper script.
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
@rem   - Agentic/Reference/comment-style-guide.md
@rem
@rem
@echo off
setlocal
REM ===============================================================
REM  validate_fast.bat - Quick sanity check: format + metadata + staged-size + build.
REM  Use for: small code refactors and non-rendering code edits.
REM  Runtime: about 30 seconds.
REM ===============================================================

echo.
echo ========================================
echo   VALIDATE_FAST - Format + Metadata + Size + Build
echo ========================================
echo.

echo [1/6] Checking formatting...
call "%~dp0validate_format.bat"
if errorlevel 1 (
    echo.
    echo To auto-fix: tools\format_fix.bat
    exit /b 1
)

echo [2/6] Checking Visual Studio project filters...
call "%~dp0validate_project_filters.bat"
if errorlevel 1 exit /b 2

echo [3/6] Checking staged file sizes...
REM Why: the checker reads the git index, so keep it before the expensive build
REM steps and pass the repo root explicitly for callers outside the worktree.
python "%~dp0check_staged_file_sizes.py" --repo "%~dp0.."
if errorlevel 1 exit /b 3

echo [4/6] Checking runtime boundaries...
call "%~dp0validate_runtime_boundaries.bat"
if errorlevel 1 exit /b 4

echo [5/6] Building Profile x64...
call "%~dp0validate_build.bat" Profile
if errorlevel 1 exit /b 5

echo [6/6] Running unit tests...
call "%~dp0validate_tests.bat"
if errorlevel 1 exit /b 6

call "%~dp0validate_ready_builds.bat"
if errorlevel 1 exit /b 7

echo.
echo ========================================
echo   VALIDATE_FAST: ALL PASSED
echo ========================================
exit /b 0
