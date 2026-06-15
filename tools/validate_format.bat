@rem
@rem File: tools/validate_format.bat
@rem Purpose:
@rem   Documents and runs the validate_format.bat developer/validation helper script.
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
setlocal enabledelayedexpansion
REM ===============================================================
REM  validate_format.bat - Check all C++ files are correctly formatted.
REM  Exit 0 = pass, Exit 1 = formatting violations found.
REM ===============================================================

set "REPO=%~dp0.."
set BAD_COUNT=0

call "%~dp0find_clang_format.bat"
if errorlevel 1 exit /b 99

echo Checking formatting...

for %%f in ("%REPO%\SkullbonezSource\*.cpp" "%REPO%\SkullbonezSource\*.h") do (
    "%CLANG_FMT%" --dry-run -Werror "%%f" >nul 2>&1
    if errorlevel 1 (
        echo   FAIL: %%~nxf
        set /a BAD_COUNT+=1
    )
)

if %BAD_COUNT% GTR 0 (
    echo FAIL: %BAD_COUNT% files need formatting.
    echo       Run: tools\format_fix.bat
    exit /b 1
)

echo PASS: All source files correctly formatted.
exit /b 0
