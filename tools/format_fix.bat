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
if errorlevel 1 exit /b 98

set COUNT=0
for /r "%REPO%\SkullbonezSource" %%f in (*.cpp *.h) do (
    "%CLANG_FMT%" -i "%%f"
    set /a COUNT+=1
)

"%PYTHON_EXE%" "%~dp0separate_multiline_cpp_declarations.py" --self-test
if errorlevel 1 exit /b 1

"%PYTHON_EXE%" "%~dp0separate_multiline_cpp_declarations.py" --repo "%REPO%" --write
if errorlevel 1 exit /b 1

"%PYTHON_EXE%" "%~dp0align_header_inline_comments.py" --repo "%REPO%" --write
if errorlevel 1 exit /b 1

echo Formatted %COUNT% C++ files, kept assignment heads and compact calls together, separated multiline statements/control blocks, and aligned header inline comments.
exit /b 0
