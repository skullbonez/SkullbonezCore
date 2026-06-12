@echo off
setlocal
REM ===============================================================
REM  validate_fast.bat - Quick sanity check: format + build.
REM  Use for: small code refactors and non-rendering code edits.
REM  Runtime: about 30 seconds.
REM ===============================================================

echo.
echo ========================================
echo   VALIDATE_FAST - Format + Build
echo ========================================
echo.

echo [1/2] Checking formatting...
call "%~dp0validate_format.bat"
if errorlevel 1 (
    echo.
    echo To auto-fix: tools\format_fix.bat
    exit /b 1
)

echo [2/2] Building Profile x64...
call "%~dp0validate_build.bat" Profile
if errorlevel 1 exit /b 2

call "%~dp0validate_ready_builds.bat"
if errorlevel 1 exit /b 3

echo.
echo ========================================
echo   VALIDATE_FAST: ALL PASSED
echo ========================================
exit /b 0
