@rem
@rem File: tools/validate_renderers.bat
@rem Purpose:
@rem   Documents and runs the validate_renderers.bat developer/validation helper script.
@rem
@rem Mental model:
@rem   Tools are command-line guardrails around builds, validation, screenshots,
@rem   diagnostics, and artifact handling. They make the safe path repeatable and
@rem   keep output bounded for humans and agents.
@rem
@rem Glossary:
@rem   DX12 (DirectX 12): Production renderer API used for explicit GPU resource,
@rem   descriptor, and command-list control.
@rem   DX11 (DirectX 11): Legacy parity renderer used to compare output while the
@rem   engine migrates to DX12.
@rem   GL (OpenGL): Legacy parity renderer path.
@rem   Validation gate: Repository script that proves a class of changes before
@rem   commit or PR.
@rem
@rem Invariants:
@rem   - Tool output should be bounded and readable because agents and humans use
@rem   it for decisions.
@rem
@rem Related:
@rem   - AGENTS.md
@rem   - Agentic/Reference/comment-style-guide.md
@rem
@rem
@echo off
setlocal enabledelayedexpansion
REM ===============================================================
REM  validate_renderers.bat - Tri-renderer visual validation.
REM  Use for: shader changes, render backend changes, texture changes.
REM  Runtime: about 60 seconds.
REM ===============================================================

set "REPO=%~dp0.."
set "RENDER_TIMEOUT_SECONDS=120"
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
if not exist "%REPO%\Profile" mkdir "%REPO%\Profile"
set "BUILD_LOG=%REPO%\Profile\validate_renderers_build_profile.log"
call "%~dp0validate_build.bat" Profile >"%BUILD_LOG%" 2>&1
if errorlevel 1 (
    echo FAIL: Profile build failed. Renderer suites were not started.
    echo Build log: "%BUILD_LOG%"
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Get-Content -LiteralPath $env:BUILD_LOG -Tail 120"
    popd
    exit /b 2
)
echo PASS: Profile build succeeded. Build log: "%BUILD_LOG%"

echo [3/7] Cleaning old artifacts...
del /q "%REPO%\Profile\*screenshot.bmp" 2>nul
del /q "%REPO%\Profile\*solver_smoke.bmp" 2>nul
del /q "%REPO%\Profile\perf_log.csv" 2>nul
del /q "%REPO%\Profile\*_perf_log.csv" 2>nul
del /q "%REPO%\Profile\*_stdout.txt" 2>nul
del /q "%REPO%\Profile\*_stderr.txt" 2>nul
del /q "%REPO%\dx12_validation.txt" 2>nul

echo [4/7] Running GL suite...
call :run_renderer gl "--vsync off --suite SkullbonezData/scenes/render_tests.suite"
if errorlevel 1 (
    echo FAIL: GL suite exited with error.
    popd
    exit /b 3
)
if exist "%REPO%\Profile\screenshot.bmp" rename "%REPO%\Profile\screenshot.bmp" gl_screenshot.bmp
if exist "%REPO%\Profile\solver_smoke.bmp" rename "%REPO%\Profile\solver_smoke.bmp" gl_solver_smoke.bmp

echo [5/7] Running DX11 suite...
call :run_renderer dx11 "--renderer dx11 --vsync off --suite SkullbonezData/scenes/render_tests.suite"
if errorlevel 1 (
    echo FAIL: DX11 suite exited with error.
    popd
    exit /b 4
)
if exist "%REPO%\Profile\screenshot.bmp" rename "%REPO%\Profile\screenshot.bmp" dx11_screenshot.bmp
if exist "%REPO%\Profile\solver_smoke.bmp" rename "%REPO%\Profile\solver_smoke.bmp" dx11_solver_smoke.bmp

echo [6/7] Running DX12 suite...
call :run_renderer dx12 "--renderer dx12 --vsync off --suite SkullbonezData/scenes/render_tests.suite"
if errorlevel 1 (
    echo FAIL: DX12 suite exited with error.
    popd
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

echo [7/8] Checking stdout/stderr and DX12 validation...
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
echo [8/8] Checking cross-renderer parity and writing comparison artifacts...
set "SKORE_REPO=%REPO%"
"%PYTHON_EXE%" "%~dp0check_parity.py" --repo "%REPO%" --out-root "%REPO%\TestOutput\validation\renderers"
if errorlevel 1 (
    echo FAIL: Cross-renderer parity check failed.
    exit /b 9
)

call "%~dp0validate_ready_builds.bat"
if errorlevel 1 (
    popd
    exit /b 10
)

echo.
echo ========================================
echo   VALIDATE_RENDERERS: ALL PASSED
echo ========================================
popd
exit /b 0

:run_renderer
setlocal
set "RUN_RENDERER=%~1"
set "RUN_ARGS=%~2"
set "RUN_STDOUT=%REPO%\Profile\%RUN_RENDERER%_stdout.txt"
set "RUN_STDERR=%REPO%\Profile\%RUN_RENDERER%_stderr.txt"
set "SKORE_RENDER_EXE=%REPO%\Profile\SKULLBONEZ_CORE.exe"
set "SKORE_RENDER_REPO=%REPO%"
set "SKORE_RENDER_ARGS=%RUN_ARGS%"
set "SKORE_RENDER_STDOUT=%RUN_STDOUT%"
set "SKORE_RENDER_STDERR=%RUN_STDERR%"
set "SKORE_RENDER_TIMEOUT=%RENDER_TIMEOUT_SECONDS%"
echo Running %RUN_RENDERER% suite with %RENDER_TIMEOUT_SECONDS%s timeout...
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; $exe=$env:SKORE_RENDER_EXE; $repo=$env:SKORE_RENDER_REPO; $stdout=$env:SKORE_RENDER_STDOUT; $stderr=$env:SKORE_RENDER_STDERR; $timeout=[int]$env:SKORE_RENDER_TIMEOUT; $argText=$env:SKORE_RENDER_ARGS; $argv=@(); if(-not [string]::IsNullOrWhiteSpace($argText)){ $argv=$argText -split ' ' }; try { $p=Start-Process -FilePath $exe -ArgumentList $argv -WorkingDirectory $repo -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru; $waitMs=[Math]::Max(1, $timeout) * 1000; if(-not $p.WaitForExit($waitMs)){ Write-Host ('TIMEOUT: renderer process exceeded ' + $timeout + 's; killing PID ' + $p.Id); $live=Get-Process -Id $p.Id -ErrorAction SilentlyContinue; if($null -ne $live){ Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; $p.WaitForExit(5000) | Out-Null }; exit 124 }; $p.Refresh(); exit $p.ExitCode } catch { Write-Host ('ERROR: renderer launch wrapper failed: ' + $_.Exception.Message); exit 125 }"
set "RUN_EXIT=%ERRORLEVEL%"
if not "%RUN_EXIT%"=="0" (
    echo Renderer %RUN_RENDERER% failed with exit code %RUN_EXIT%.
    echo stdout: "%RUN_STDOUT%"
    echo stderr: "%RUN_STDERR%"
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "if(Test-Path -LiteralPath $env:SKORE_RENDER_STDOUT){ Write-Host '--- stdout tail ---'; Get-Content -LiteralPath $env:SKORE_RENDER_STDOUT -Tail 80 }; if(Test-Path -LiteralPath $env:SKORE_RENDER_STDERR){ Write-Host '--- stderr tail ---'; Get-Content -LiteralPath $env:SKORE_RENDER_STDERR -Tail 80 }"
    endlocal
    exit /b %RUN_EXIT%
)
endlocal
exit /b 0
