@echo off
setlocal
REM ===============================================================
REM  loc_count.bat - Count first-party source logical lines of code.
REM ===============================================================

set "REPO=%~dp0.."

call "%~dp0find_python.bat"
if errorlevel 1 exit /b %errorlevel%

"%PYTHON_EXE%" "%REPO%\Agentic\Skills\loc_count.py"
exit /b %errorlevel%
