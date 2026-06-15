@rem
@rem File: tools/check_dx12_validation.bat
@rem Purpose:
@rem   Documents and runs the check_dx12_validation.bat developer/validation helper script.
@rem
@rem Mental model:
@rem   Tools are command-line guardrails around builds, validation, screenshots,
@rem   diagnostics, and artifact handling. They make the safe path repeatable and
@rem   keep output bounded for humans and agents.
@rem
@rem Glossary:
@rem   DX12 (DirectX 12): Production renderer API used for explicit GPU resource,
@rem   descriptor, and command-list control.
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
REM  check_dx12_validation.bat - Verify DX12 InfoQueue clean.
REM  Exit 0 = no validation errors, Exit 1 = errors present or file missing.
REM ===============================================================

set "REPO=%~dp0.."
set "VAL_FILE=%REPO%\dx12_validation.txt"

if not exist "%VAL_FILE%" (
    echo DX12 validation status: missing
    echo DX12 validation errors: unavailable
    echo FAIL: dx12_validation.txt not found.
    echo       DX12 suite may not have run or crashed before writing validation output.
    exit /b 1
)

REM Read the last line; it should be "0" (error count).
set "LAST_LINE="
for /f "usebackq delims=" %%a in ("%VAL_FILE%") do set "LAST_LINE=%%a"

if not defined LAST_LINE (
    echo DX12 validation status: unreadable
    echo DX12 validation errors: unavailable
    echo FAIL: dx12_validation.txt is empty.
    exit /b 1
)

if "%LAST_LINE%"=="0" (
    echo DX12 validation status: available
    echo DX12 validation errors: 0
    echo PASS: DX12 InfoQueue reported 0 validation errors.
    exit /b 0
)

echo DX12 validation status: available
echo DX12 validation errors: %LAST_LINE%
echo FAIL: DX12 InfoQueue reported %LAST_LINE% validation errors:
type "%VAL_FILE%"
exit /b 1
