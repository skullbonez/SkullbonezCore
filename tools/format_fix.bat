@rem
@rem File: tools/format_fix.bat
@rem Purpose:
@rem   Documents and runs the format_fix.bat developer/validation helper script.
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
REM  format_fix.bat - Auto-format all C++ source files in-place.
REM ===============================================================

set "REPO=%~dp0.."

call "%~dp0find_clang_format.bat"
if errorlevel 1 exit /b 99
call "%~dp0find_python.bat"
if errorlevel 1 exit /b 99

REM Run the parameter collapse script first (matches pipeline Step 1).
"%PYTHON_EXE%" "%REPO%\Agentic\Skills\collapse_params.py"
if errorlevel 1 exit /b 99

set COUNT=0
for %%f in ("%REPO%\SkullbonezSource\*.cpp" "%REPO%\SkullbonezSource\*.h") do (
    "%CLANG_FMT%" -i "%%f"
    set /a COUNT+=1
)

echo Formatted %COUNT% files.
exit /b 0
