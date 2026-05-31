@echo off
setlocal
REM Archive current Profile validation artifacts into TestOutput\NNN_commit.

if /I "%~1"=="--help" goto :help
if /I "%~1"=="/?" goto :help

set "REPO=%~dp0.."
call "%~dp0find_python.bat"
if errorlevel 1 exit /b 99
call "%~dp0find_git.bat"
if errorlevel 1 exit /b 99

"%PYTHON_EXE%" "%~dp0archive_validation_artifacts.py" --repo "%REPO%" %*
exit /b %ERRORLEVEL%

:help
echo Usage: tools\archive_validation_artifacts.bat [--visuals] [--perf] [--require] [--commit HASH]
echo.
echo Creates or reuses TestOutput\NNN_commit and archives current Profile artifacts.
exit /b 0
