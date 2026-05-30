@echo off
setlocal enabledelayedexpansion
REM ===============================================================
REM  validate_format.bat - Check all C++ files are correctly formatted.
REM  Exit 0 = pass, Exit 1 = formatting violations found.
REM ===============================================================

set "REPO=%~dp0.."
set "CLANG_FMT=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\Llvm\x64\bin\clang-format.exe"
set BAD_COUNT=0

if not exist "%CLANG_FMT%" (
    echo ERROR: clang-format not found at expected path.
    echo        Expected: %CLANG_FMT%
    echo        Install VS2022 with C++ LLVM tools.
    exit /b 99
)

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
