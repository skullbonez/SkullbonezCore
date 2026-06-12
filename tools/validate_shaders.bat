@echo off
setlocal
REM ===============================================================
REM  validate_shaders.bat - Shader file and manifest contract check.
REM  Warnings indicate incomplete manifest coverage; hard errors fail.
REM ===============================================================

set "REPO=%~dp0.."
pushd "%REPO%"
echo.
echo ========================================
echo   VALIDATE_SHADERS - Shader Contracts
echo ========================================
echo.

call "%~dp0find_python.bat"
if errorlevel 1 exit /b 99

"%PYTHON_EXE%" "%~dp0validate_shaders.py" --repo "%REPO%" %*
if errorlevel 1 (
    echo FAIL: Shader contract validation failed.
    popd
    exit /b 1
)

call "%~dp0validate_ready_builds.bat"
if errorlevel 1 (
    popd
    exit /b 2
)

echo.
echo ========================================
echo   VALIDATE_SHADERS: ALL PASSED
echo ========================================
popd
exit /b 0
