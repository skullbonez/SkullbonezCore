@echo off
setlocal
REM ===============================================================
REM  check_dx12_validation.bat - Verify DX12 InfoQueue clean.
REM  Exit 0 = no validation errors, Exit 1 = errors present or file missing.
REM ===============================================================

set "REPO=%~dp0.."
set "VAL_FILE=%REPO%\dx12_validation.txt"

if not exist "%VAL_FILE%" (
    echo FAIL: dx12_validation.txt not found.
    echo       DX12 suite may not have run or crashed before writing validation output.
    exit /b 1
)

REM Read the last line; it should be "0" (error count).
for /f "usebackq delims=" %%a in ("%VAL_FILE%") do set "LAST_LINE=%%a"

if "%LAST_LINE%"=="0" (
    echo PASS: DX12 InfoQueue reported 0 validation errors.
    exit /b 0
)

echo FAIL: DX12 InfoQueue reported %LAST_LINE% validation errors:
type "%VAL_FILE%"
exit /b 1
