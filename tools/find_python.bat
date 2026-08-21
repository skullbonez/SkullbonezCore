@rem
@rem File: tools/find_python.bat
@rem Purpose:
@rem   Documents and runs the find_python.bat developer/validation helper script.
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
REM ===============================================================
REM  find_python.bat - Locates Python and sets PYTHON_EXE.
REM  Called by other validate_*.bat scripts. Do not run directly.
REM ===============================================================

if exist "%PYTHON_EXE%" exit /b 0

set "USER_PYTHON_312=%LOCALAPPDATA%\Programs\Python\Python312\python.exe"
if exist "%USER_PYTHON_312%" (
    set "PYTHON_EXE=%USER_PYTHON_312%"
    goto :found
)

set "USER_PYTHON_313=%LOCALAPPDATA%\Programs\Python\Python313\python.exe"
if exist "%USER_PYTHON_313%" (
    set "PYTHON_EXE=%USER_PYTHON_313%"
    goto :found
)

for %%i in (py.exe) do (
    if not "%%~$PATH:i"=="" (
        "%%~$PATH:i" --version >nul 2>&1
        if not errorlevel 1 (
            set "PYTHON_EXE=%%~$PATH:i"
            goto :found
        )
    )
)

for %%i in (python.exe) do (
    if not "%%~$PATH:i"=="" (
        "%%~$PATH:i" --version >nul 2>&1
        if not errorlevel 1 (
            set "PYTHON_EXE=%%~$PATH:i"
            goto :found
        )
    )
)

set "CODEX_PYTHON=%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
if exist "%CODEX_PYTHON%" (
    set "PYTHON_EXE=%CODEX_PYTHON%"
    goto :found
)

echo ERROR: Python not found. Install Python or run from Codex/Antigravity with bundled runtime available.
exit /b 99

:found
exit /b 0
