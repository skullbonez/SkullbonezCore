@echo off
setlocal
REM SkullScope launcher for Windows shells.
REM Avoids relying on .py file associations, which often open an app picker.

call "%~dp0find_python.bat"
if errorlevel 1 exit /b 99

"%PYTHON_EXE%" "%~dp0physics_query.py" %*
exit /b %errorlevel%
