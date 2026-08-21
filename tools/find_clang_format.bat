@rem
@rem File: tools/find_clang_format.bat
@rem Purpose:
@rem   Documents and runs the find_clang_format.bat developer/validation helper script.
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
REM  find_clang_format.bat - Locates clang-format and sets CLANG_FMT.
REM  Called by other validate_*.bat scripts. Do not run directly.
REM ===============================================================

if exist "%CLANG_FMT%" exit /b 0

for /f "usebackq tokens=*" %%i in (`"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -find VC\Tools\Llvm\x64\bin\clang-format.exe`) do (
    set "CLANG_FMT=%%i"
    goto :found
)

for /f "usebackq tokens=*" %%i in (`"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -find VC\Tools\Llvm\bin\clang-format.exe`) do (
    set "CLANG_FMT=%%i"
    goto :found
)

for %%i in (clang-format.exe) do (
    if not "%%~$PATH:i"=="" (
        set "CLANG_FMT=%%~$PATH:i"
        goto :found
    )
)

echo ERROR: clang-format not found. Install Visual Studio with C++ LLVM tools.
exit /b 99

:found
exit /b 0
