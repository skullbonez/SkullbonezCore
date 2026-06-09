@echo off
setlocal enabledelayedexpansion
REM ===============================================================
REM  validate_scene_loads.bat - Boot-only sweep for every .scene.
REM  Builds Profile, enumerates all SkullbonezData\scenes\*.scene
REM  files, then loads each scene with a timeout so setup failures
REM  are caught without waiting for scenarios to complete.
REM ===============================================================

set "REPO=%~dp0.."
pushd "%REPO%"
call "%~dp0find_python.bat"
if errorlevel 1 exit /b 99

echo [1/2] Building Profile x64...
call "%~dp0validate_build.bat" Profile
if errorlevel 1 exit /b 1

echo [2/2] Running load-only scene sweep...
"%PYTHON_EXE%" "%~dp0validate_scene_loads.py" %*
if errorlevel 1 exit /b 2
popd
exit /b 0
