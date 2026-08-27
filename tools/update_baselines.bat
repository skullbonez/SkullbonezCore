@rem Update visual/perf baselines or run the guarded core Physics transition.
@echo off
setlocal
REM Update TestOutput\baselines from current validation artifacts.

if /I "%~1"=="--help" goto :help
if /I "%~1"=="/?" goto :help

set "REPO=%~dp0.."
call "%~dp0find_python.bat"
if errorlevel 1 exit /b 99

"%PYTHON_EXE%" "%~dp0update_baselines.py" --repo "%REPO%" %*
exit /b %ERRORLEVEL%

:help
echo Usage: tools\update_baselines.bat [--visuals] [--perf] [--physics] [--require] [--self-test]
echo.
echo Updates TestOutput\baselines from current validation artifacts.
echo With no flags, updates both visual PNG baselines and perf JSON baselines.
echo --physics archives old/new Debug producers, writes the Physics transition,
echo updates the core golden, stages the complete evidence, and checks the index.
exit /b 0
