@echo off
setlocal
REM Update TestOutput\baselines from current Profile artifacts.

if /I "%~1"=="--help" goto :help
if /I "%~1"=="/?" goto :help

set "REPO=%~dp0.."
call "%~dp0find_python.bat"
if errorlevel 1 exit /b 99

"%PYTHON_EXE%" "%~dp0update_baselines.py" --repo "%REPO%" %*
exit /b %ERRORLEVEL%

:help
echo Usage: tools\update_baselines.bat [--visuals] [--perf] [--require]
echo.
echo Updates TestOutput\baselines from current Profile artifacts.
echo With no flags, updates both visual PNG baselines and perf JSON baselines.
exit /b 0
