@echo off
REM File: tools\bake_hulls.bat
REM Purpose: Bake or check serialized convex hull runtime data.
setlocal

set "ROOT=%~dp0.."
call "%~dp0find_python.bat"
if errorlevel 1 exit /b %errorlevel%

"%PYTHON_EXE%" "%~dp0bake_hulls.py" --repo "%ROOT%" %*
exit /b %errorlevel%
