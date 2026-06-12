@echo off
setlocal
REM ===============================================================
REM  validate_ready_builds.bat - Leave IDE-ready binaries after validation.
REM  Builds Profile and Debug so developers can launch without waiting for
REM  Visual Studio to compile after a successful validation run.
REM ===============================================================

if /I "%SKULLBONEZ_SKIP_READY_BUILDS%"=="1" exit /b 0

echo.
echo [ready] Building Profile x64 for launch/F5...
call "%~dp0validate_build.bat" Profile
if errorlevel 1 exit /b 1

echo.
echo [ready] Building Debug x64 for launch/F5...
call "%~dp0validate_build.bat" Debug
if errorlevel 1 exit /b 2

echo PASS: Profile and Debug binaries are ready.
exit /b 0
