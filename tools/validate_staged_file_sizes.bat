@echo off
setlocal

if not defined PYTHON_EXE call "%~dp0find_python.bat"
if errorlevel 1 exit /b 99

if defined SKORE_SIZE_DIFF_BASE (
    "%PYTHON_EXE%" "%~dp0check_staged_file_sizes.py" --repo "%~dp0.." --base-ref "%SKORE_SIZE_DIFF_BASE%"
) else (
    "%PYTHON_EXE%" "%~dp0check_staged_file_sizes.py" --repo "%~dp0.."
)
exit /b %errorlevel%
