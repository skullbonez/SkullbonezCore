@echo off
setlocal
REM Bake or verify deterministic DXIL assets using the pinned Windows SDK compiler.
call "%~dp0find_python.bat"
if errorlevel 1 exit /b %ERRORLEVEL%
"%PYTHON_EXE%" "%~dp0bake_shaders.py" %*
exit /b %ERRORLEVEL%
