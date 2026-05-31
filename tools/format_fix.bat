@echo off
setlocal
REM ===============================================================
REM  format_fix.bat - Auto-format all C++ source files in-place.
REM ===============================================================

set "REPO=%~dp0.."

call "%~dp0find_clang_format.bat"
if errorlevel 1 exit /b 99
call "%~dp0find_python.bat"
if errorlevel 1 exit /b 99

REM Run the parameter collapse script first (matches pipeline Step 1).
"%PYTHON_EXE%" "%REPO%\Agentic\Skills\collapse_params.py"
if errorlevel 1 exit /b 99

set COUNT=0
for %%f in ("%REPO%\SkullbonezSource\*.cpp" "%REPO%\SkullbonezSource\*.h") do (
    "%CLANG_FMT%" -i "%%f"
    set /a COUNT+=1
)

echo Formatted %COUNT% files.
exit /b 0
