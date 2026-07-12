@echo off
setlocal
REM Bake or verify deterministic DXIL assets using the pinned Windows SDK compiler.
py -3 "%~dp0bake_shaders.py" %*
exit /b %ERRORLEVEL%
