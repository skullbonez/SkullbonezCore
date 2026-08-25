@rem
@rem File: tools/validate_format.bat
@rem Purpose:
@rem   Documents and runs the validate_format.bat developer/validation helper script.
@rem
@rem Summary:
@rem   Runs the pinned clang-format binary directly over changed first-party C++ source.
@rem   Repository prose links remain useful review aids but do not determine
@rem   whether mechanical source layout passes.
@rem
@rem Glossary:
@rem   Validation gate: Repository script that proves a class of changes before
@rem   commit or PR.
@rem
@rem Invariants:
@rem   - clang-format is the sole mechanical layout authority.
@rem   - Every changed C++, header, and inline file under SkullbonezSource is checked.
@rem   - Untouched legacy layout does not force a repository-wide source rewrite.
@rem
@rem Related:
@rem   - AGENTS.md
@rem
@rem
@echo off
setlocal enabledelayedexpansion
REM ===============================================================
REM  validate_format.bat - Check changed C++ files with clang-format.
REM  Exit 0 = pass, Exit 1 = formatting violations found.
REM ===============================================================

set "REPO=%~dp0.."
call "%~dp0find_clang_format.bat"
if errorlevel 1 exit /b 99

echo Checking changed C++ source with clang-format...

set "FORMAT_FAILED=0"
set "SOURCE_COUNT=0"
set "FORMAT_BASE=%SKORE_SIZE_DIFF_BASE%"
if not defined FORMAT_BASE for /f "usebackq tokens=*" %%b in (`git -C "%REPO%" merge-base HEAD origin/main 2^>nul`) do set "FORMAT_BASE=%%b"
if not defined FORMAT_BASE for /f "usebackq tokens=*" %%b in (`git -C "%REPO%" rev-parse HEAD^^ 2^>nul`) do set "FORMAT_BASE=%%b"

if defined FORMAT_BASE call :check_range "%FORMAT_BASE%...HEAD"
call :check_range "HEAD"

if "!FORMAT_FAILED!"=="1" (
    echo FAIL: clang-format reported source layout differences.
    echo       Run: tools\format_fix.bat
    exit /b 1
)

echo PASS: clang-format accepted !SOURCE_COUNT! source files.
exit /b 0

:check_range
for /f "usebackq delims=" %%f in (`git -C "%REPO%" diff --name-only --diff-filter=ACMR %~1 -- SkullbonezSource`) do call :check_file "%%f"
exit /b 0

:check_file
if /I not "%~x1"==".cpp" if /I not "%~x1"==".h" if /I not "%~x1"==".hpp" if /I not "%~x1"==".inl" exit /b 0
if not exist "%REPO%\%~1" exit /b 0
"%CLANG_FMT%" --dry-run --Werror "%REPO%\%~1"
if errorlevel 1 set "FORMAT_FAILED=1"
set /a SOURCE_COUNT+=1
exit /b 0
