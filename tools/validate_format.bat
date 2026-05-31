@echo off
setlocal enabledelayedexpansion
REM ===============================================================
REM  validate_format.bat - Check all C++ files are correctly formatted.
REM  Exit 0 = pass, Exit 1 = formatting violations found.
REM ===============================================================

set "REPO=%~dp0.."
set BAD_COUNT=0

call "%~dp0find_clang_format.bat"
if errorlevel 1 exit /b 99

echo Checking formatting...

for %%f in ("%REPO%\SkullbonezSource\*.cpp" "%REPO%\SkullbonezSource\*.h") do (
    "%CLANG_FMT%" --dry-run -Werror "%%f" >nul 2>&1
    if errorlevel 1 (
        echo   FAIL: %%~nxf
        set /a BAD_COUNT+=1
    )
)

if %BAD_COUNT% GTR 0 (
    echo FAIL: %BAD_COUNT% files need formatting.
    echo       Run: tools\format_fix.bat
    exit /b 1
)

echo PASS: All source files correctly formatted.
exit /b 0
