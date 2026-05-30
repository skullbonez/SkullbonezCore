@echo off
setlocal enabledelayedexpansion
REM ===============================================================
REM  validate_build.bat - Build SkullbonezCore solution.
REM  Usage: validate_build.bat [Configuration]
REM    Configuration = Debug | Release | Profile (default: Profile)
REM  Exit 0 = build succeeded, Exit 1 = build failed.
REM ===============================================================

set "REPO=%~dp0.."
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Profile"

call "%~dp0find_msbuild.bat"
if errorlevel 1 exit /b 99

echo Building %CONFIG%^|x64...
"%MSBUILD_EXE%" "%REPO%\SKULLBONEZ_CORE.sln" /p:Configuration=%CONFIG% /p:Platform=x64 /nologo /v:minimal /warnaserror
if errorlevel 1 (
    echo FAIL: Build %CONFIG% failed.
    exit /b 1
)

echo PASS: Build %CONFIG%^|x64 succeeded.
exit /b 0
