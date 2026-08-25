@rem
@rem File: tools/format_fix.bat
@rem Purpose:
@rem   Documents and runs the format_fix.bat developer/validation helper script.
@rem
@rem Summary:
@rem   Applies the pinned clang-format binary directly to changed first-party C++ source.
@rem
@rem Glossary:
@rem   Validation gate: Repository script that proves a class of changes before
@rem   commit or PR.
@rem
@rem Invariants:
@rem   - clang-format is the sole mechanical layout authority.
@rem   - Only changed first-party source is rewritten; untouched legacy layout
@rem     does not create a repository-wide formatting diff.
@rem
@rem Related:
@rem   - AGENTS.md
@rem
@rem
@echo off
setlocal enabledelayedexpansion
REM ===============================================================
REM  format_fix.bat - Auto-format changed C++ source files in-place.
REM ===============================================================

set "REPO=%~dp0.."

call "%~dp0find_clang_format.bat"
if errorlevel 1 exit /b 99

set "COUNT=0"
set "FORMAT_BASE=%SKORE_SIZE_DIFF_BASE%"
if not defined FORMAT_BASE for /f "usebackq tokens=*" %%b in (`git -C "%REPO%" merge-base HEAD origin/main 2^>nul`) do set "FORMAT_BASE=%%b"
if not defined FORMAT_BASE for /f "usebackq tokens=*" %%b in (`git -C "%REPO%" rev-parse HEAD^^ 2^>nul`) do set "FORMAT_BASE=%%b"

if defined FORMAT_BASE call :format_range "%FORMAT_BASE%...HEAD"
if errorlevel 1 exit /b 1
call :format_range "HEAD"
if errorlevel 1 exit /b 1

echo Formatted !COUNT! changed C++ source files with clang-format.
exit /b 0

:format_range
for /f "usebackq delims=" %%f in (`git -C "%REPO%" diff --name-only --diff-filter=ACMR %~1 -- SkullbonezSource`) do (
    call :format_file "%%f"
    if errorlevel 1 exit /b 1
)
exit /b 0

:format_file
if /I not "%~x1"==".cpp" if /I not "%~x1"==".h" if /I not "%~x1"==".hpp" if /I not "%~x1"==".inl" exit /b 0
if not exist "%REPO%\%~1" exit /b 0
"%CLANG_FMT%" -i "%REPO%\%~1"
if errorlevel 1 exit /b 1
set /a COUNT+=1
exit /b 0
