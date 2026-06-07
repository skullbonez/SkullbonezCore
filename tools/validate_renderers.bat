@echo off
setlocal enabledelayedexpansion
REM ===============================================================
REM  validate_renderers.bat - Tri-renderer visual validation.
REM  Use for: shader changes, render backend changes, texture changes.
REM  Runtime: about 60 seconds.
REM ===============================================================

set "REPO=%~dp0.."
pushd "%REPO%"
echo.
echo ========================================
echo   VALIDATE_RENDERERS - Tri-Renderer Suite
echo ========================================
echo.

echo [1/7] Checking formatting...
call "%~dp0validate_format.bat"
if errorlevel 1 exit /b 1
call "%~dp0find_python.bat"
if errorlevel 1 exit /b 1

echo [2/7] Building Profile x64...
call "%~dp0validate_build.bat" Profile
if errorlevel 1 exit /b 2

echo [3/7] Cleaning old artifacts...
del /q "%REPO%\Profile\*screenshot.bmp" 2>nul
del /q "%REPO%\Profile\*solver_smoke.bmp" 2>nul
del /q "%REPO%\Profile\perf_log.csv" 2>nul
del /q "%REPO%\Profile\*_perf_log.csv" 2>nul
del /q "%REPO%\Profile\*_stdout.txt" 2>nul
del /q "%REPO%\Profile\*_stderr.txt" 2>nul
del /q "%REPO%\dx12_validation.txt" 2>nul

echo [4/7] Running GL suite...
"%REPO%\Profile\SKULLBONEZ_CORE.exe" --vsync off --suite SkullbonezData/scenes/render_tests.suite >"%REPO%\Profile\gl_stdout.txt" 2>"%REPO%\Profile\gl_stderr.txt"
if errorlevel 1 (
    echo FAIL: GL suite exited with error.
    exit /b 3
)
if exist "%REPO%\Profile\screenshot.bmp" rename "%REPO%\Profile\screenshot.bmp" gl_screenshot.bmp
if exist "%REPO%\Profile\solver_smoke.bmp" rename "%REPO%\Profile\solver_smoke.bmp" gl_solver_smoke.bmp

echo [5/7] Running DX11 suite...
"%REPO%\Profile\SKULLBONEZ_CORE.exe" --renderer dx11 --vsync off --suite SkullbonezData/scenes/render_tests.suite >"%REPO%\Profile\dx11_stdout.txt" 2>"%REPO%\Profile\dx11_stderr.txt"
if errorlevel 1 (
    echo FAIL: DX11 suite exited with error.
    exit /b 4
)
if exist "%REPO%\Profile\screenshot.bmp" rename "%REPO%\Profile\screenshot.bmp" dx11_screenshot.bmp
if exist "%REPO%\Profile\solver_smoke.bmp" rename "%REPO%\Profile\solver_smoke.bmp" dx11_solver_smoke.bmp

echo [6/7] Running DX12 suite...
"%REPO%\Profile\SKULLBONEZ_CORE.exe" --renderer dx12 --vsync off --suite SkullbonezData/scenes/render_tests.suite >"%REPO%\Profile\dx12_stdout.txt" 2>"%REPO%\Profile\dx12_stderr.txt"
if errorlevel 1 (
    echo FAIL: DX12 suite exited with error.
    exit /b 5
)
if exist "%REPO%\Profile\screenshot.bmp" rename "%REPO%\Profile\screenshot.bmp" dx12_screenshot.bmp
if exist "%REPO%\Profile\solver_smoke.bmp" rename "%REPO%\Profile\solver_smoke.bmp" dx12_solver_smoke.bmp

set MISSING=0
for %%f in (gl_screenshot.bmp gl_solver_smoke.bmp dx11_screenshot.bmp dx11_solver_smoke.bmp dx12_screenshot.bmp dx12_solver_smoke.bmp) do (
    if not exist "%REPO%\Profile\%%f" (
        echo   MISSING: %%f
        set /a MISSING+=1
    )
)
if %MISSING% GTR 0 (
    echo FAIL: %MISSING% expected artifacts missing.
    exit /b 6
)

echo [7/7] Checking stdout/stderr for errors...
set "STDOUT_CLEAN=1"
for %%r in (gl dx11 dx12) do (
    findstr /I /C:"error" /C:"warning" /C:"failed" "%REPO%\Profile\%%r_stdout.txt" >nul 2>&1
    if not errorlevel 1 (
        echo   FAIL [%%r]: Unexpected error/warning in stdout:
        findstr /I /C:"error" /C:"warning" /C:"failed" "%REPO%\Profile\%%r_stdout.txt"
        set "STDOUT_CLEAN=0"
    )
    findstr /I /C:"error" /C:"warning" /C:"failed" "%REPO%\Profile\%%r_stderr.txt" >nul 2>&1
    if not errorlevel 1 (
        echo   FAIL [%%r]: Unexpected error/warning in stderr:
        findstr /I /C:"error" /C:"warning" /C:"failed" "%REPO%\Profile\%%r_stderr.txt"
        set "STDOUT_CLEAN=0"
    )
)
if "%STDOUT_CLEAN%"=="0" (
    echo FAIL: One or more renderers produced error/warning output.
    exit /b 7
)

call "%~dp0check_dx12_validation.bat"
if errorlevel 1 exit /b 8

echo.
echo Checking cross-renderer parity...
set "SKORE_REPO=%REPO%"
"%PYTHON_EXE%" "%~dp0check_parity.py"
if errorlevel 1 (
    echo FAIL: Cross-renderer parity check failed.
    exit /b 9
)

echo.
echo ========================================
echo   VALIDATE_RENDERERS: ALL PASSED
echo ========================================
popd
exit /b 0
