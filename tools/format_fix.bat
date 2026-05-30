@echo off
setlocal
REM ===============================================================
REM  format_fix.bat - Auto-format all C++ source files in-place.
REM ===============================================================

set "REPO=%~dp0.."
set "CLANG_FMT=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\Llvm\x64\bin\clang-format.exe"

if not exist "%CLANG_FMT%" (
    echo ERROR: clang-format not found. Install VS2022 with C++ LLVM tools.
    exit /b 99
)

REM Run the parameter collapse script first (matches pipeline Step 1).
py "%REPO%\Copilot\Skills\collapse_params.py"
if errorlevel 1 exit /b 99

set COUNT=0
for %%f in ("%REPO%\SkullbonezSource\*.cpp" "%REPO%\SkullbonezSource\*.h") do (
    "%CLANG_FMT%" -i "%%f"
    set /a COUNT+=1
)

echo Formatted %COUNT% files.
exit /b 0
