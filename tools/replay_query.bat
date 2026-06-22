@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
call "%SCRIPT_DIR%find_python.bat"
if errorlevel 1 exit /b 1

"%PYTHON_EXE%" "%SCRIPT_DIR%replay_query.py" %*
exit /b %ERRORLEVEL%
