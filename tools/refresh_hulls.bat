@echo off
REM File: tools\refresh_hulls.bat
REM Purpose: Refresh every committed convex hull asset from source geometry.
setlocal

set "ROOT=%~dp0.."
call "%~dp0bake_hulls.bat" --write
if errorlevel 1 exit /b %errorlevel%

call "%~dp0bake_hulls.bat" --check
exit /b %errorlevel%
