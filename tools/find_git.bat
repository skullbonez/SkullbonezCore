@rem
@rem File: tools/find_git.bat
@rem Purpose:
@rem   Documents and runs the find_git.bat developer/validation helper script.
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
REM ===============================================================
REM  find_git.bat - Locates Git and makes it available on PATH.
REM  Called by validation scripts that invoke Python helpers using git.
REM ===============================================================

for %%i in (git.exe) do (
    if not "%%~$PATH:i"=="" (
        "%%~$PATH:i" --version >nul 2>&1
        if not errorlevel 1 goto :found
    )
)

if exist "%ProgramFiles%\Git\cmd\git.exe" (
    set "PATH=%ProgramFiles%\Git\cmd;%PATH%"
    goto :found
)

if exist "%LOCALAPPDATA%\Programs\Git\cmd\git.exe" (
    set "PATH=%LOCALAPPDATA%\Programs\Git\cmd;%PATH%"
    goto :found
)

echo ERROR: Git not found. Install Git for Windows.
exit /b 99

:found
exit /b 0
