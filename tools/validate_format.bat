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
@rem
@rem
@echo off
setlocal enabledelayedexpansion
REM ===============================================================
REM  validate_format.bat - Check all C++ files are correctly formatted.
REM  Exit 0 = pass, Exit 1 = formatting violations found.
REM ===============================================================

set "REPO=%~dp0.."
call "%~dp0find_clang_format.bat"
if errorlevel 1 exit /b 99

call "%~dp0find_python.bat"
if errorlevel 1 exit /b 98

echo Checking C++ implementation formatting...

"%PYTHON_EXE%" "%~dp0separate_multiline_cpp_declarations.py" --self-test
if errorlevel 1 (
    echo FAIL: Source-layout regression fixture failed.
    exit /b 1
)

"%PYTHON_EXE%" "%~dp0check_related_paths.py" --self-test
if errorlevel 1 (
    echo FAIL: Related-path regression fixtures failed.
    exit /b 1
)

"%PYTHON_EXE%" "%~dp0check_related_paths.py" --repo "%REPO%"
if errorlevel 1 (
    echo FAIL: Source learning headers contain unresolved Related paths.
    exit /b 1
)

"%PYTHON_EXE%" "%~dp0separate_multiline_cpp_declarations.py" --repo "%REPO%" --check-pipeline --clang-format "%CLANG_FMT%"
if errorlevel 1 (
    echo FAIL: C++ implementation formatting, paragraph spacing, assignment heads, or compact calls need repair.
    echo       Run: tools\format_fix.bat
    exit /b 1
)

"%PYTHON_EXE%" "%~dp0align_header_inline_comments.py" --repo "%REPO%" --check-pipeline --clang-format "%CLANG_FMT%"
if errorlevel 1 (
    echo FAIL: Header formatting pipeline is not clean.
    echo       Run: tools\format_fix.bat
    exit /b 1
)

echo PASS: All source files correctly formatted.
exit /b 0
