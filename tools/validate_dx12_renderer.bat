@rem
@rem File: tools/validate_dx12_renderer.bat
@rem Purpose:
@rem   Runs the DX12-only renderer validation gate.
@rem
@rem Mental model:
@rem   Tools are command-line guardrails around builds, validation, screenshots,
@rem   diagnostics, and artifact handling. They make the safe path repeatable and
@rem   keep output bounded for humans and agents.
@rem
@rem Glossary:
@rem   DX12 (DirectX 12): Production renderer API used for explicit GPU resource,
@rem   descriptor, and command-list control.
@rem   Baseline: Committed reference artifact used to detect visual regression.
@rem   InfoQueue: DX12 debug-layer message queue checked for validation errors.
@rem   Validation gate: Repository script that proves a class of changes before
@rem   commit or PR.
@rem
@rem Invariants:
@rem   - DX12 screenshot and InfoQueue failures must fail without launching GL or
@rem   DX11.
@rem   - Renderer launches use PID-scoped timeout cleanup.
@rem
@rem Related:
@rem   - tools/check_dx12_baselines.py
@rem   - Agentic/Plans/dx12-only-renderer-retirement-plan.md
@rem   - Agentic/Reference/comment-style-guide.md
@rem
@echo off
setlocal enabledelayedexpansion
REM ===============================================================
REM  validate_dx12_renderer.bat - DX12-only visual validation.
REM  Use for: DX12 renderer, shader, texture, screenshot behavior.
REM ===============================================================

set "REPO=%~dp0.."
set "RENDER_TIMEOUT_SECONDS=120"
pushd "%REPO%"
echo.
echo ========================================
echo   VALIDATE_DX12_RENDERER
echo ========================================
echo.

echo [1/7] Checking formatting...
call "%~dp0validate_format.bat"
if errorlevel 1 (
    popd
    exit /b 1
)
call "%~dp0find_python.bat"
if errorlevel 1 (
    popd
    exit /b 1
)

echo [2/7] Ensuring Profile x64 build...
if /I "%SKULLBONEZ_ASSUME_PROFILE_BUILT%"=="1" (
    echo PASS: Reusing prebuilt Profile x64.
) else (
    if not exist "%REPO%\Profile" mkdir "%REPO%\Profile"
    set "BUILD_LOG=%REPO%\Profile\validate_dx12_renderer_build_profile.log"
    call "%~dp0validate_build.bat" Profile >"%BUILD_LOG%" 2>&1
    if errorlevel 1 (
        echo FAIL: Profile build failed. DX12 renderer suite was not started.
        echo Build log: "%BUILD_LOG%"
        powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Get-Content -LiteralPath $env:BUILD_LOG -Tail 120"
        popd
        exit /b 2
    )
    echo PASS: Profile build succeeded. Build log: "%BUILD_LOG%"
)

echo [3/7] Cleaning old DX12 artifacts...
del /q "%REPO%\Profile\screenshot.bmp" 2>nul
del /q "%REPO%\Profile\solver_smoke.bmp" 2>nul
del /q "%REPO%\Profile\dx12_screenshot.bmp" 2>nul
del /q "%REPO%\Profile\dx12_solver_smoke.bmp" 2>nul
del /q "%REPO%\Profile\dx12_stdout.txt" 2>nul
del /q "%REPO%\Profile\dx12_stderr.txt" 2>nul
del /q "%REPO%\dx12_validation.txt" 2>nul

echo [4/7] Running DX12 render suite...
call :run_renderer dx12 "--renderer dx12 --vsync off --suite SkullbonezData/scenes/render_tests.suite"
if errorlevel 1 (
    echo FAIL: DX12 suite exited with error.
    popd
    exit /b 3
)
if exist "%REPO%\Profile\screenshot.bmp" rename "%REPO%\Profile\screenshot.bmp" dx12_screenshot.bmp
if exist "%REPO%\Profile\solver_smoke.bmp" rename "%REPO%\Profile\solver_smoke.bmp" dx12_solver_smoke.bmp

echo [5/7] Checking expected DX12 screenshot artifacts...
set "MISSING=0"
for %%f in (dx12_screenshot.bmp dx12_solver_smoke.bmp) do (
    if not exist "%REPO%\Profile\%%f" (
        echo   MISSING: %%f
        set /a MISSING+=1
    )
)
if %MISSING% GTR 0 (
    echo FAIL: %MISSING% expected DX12 artifacts missing.
    popd
    exit /b 4
)

echo [6/7] Checking DX12 stdout/stderr and InfoQueue validation...
set "STDOUT_CLEAN=1"
findstr /I /C:"error" /C:"warning" /C:"failed" "%REPO%\Profile\dx12_stdout.txt" >nul 2>&1
if not errorlevel 1 (
    echo   FAIL [dx12]: Unexpected error/warning in stdout:
    findstr /I /C:"error" /C:"warning" /C:"failed" "%REPO%\Profile\dx12_stdout.txt"
    set "STDOUT_CLEAN=0"
)
findstr /I /C:"error" /C:"warning" /C:"failed" "%REPO%\Profile\dx12_stderr.txt" >nul 2>&1
if not errorlevel 1 (
    echo   FAIL [dx12]: Unexpected error/warning in stderr:
    findstr /I /C:"error" /C:"warning" /C:"failed" "%REPO%\Profile\dx12_stderr.txt"
    set "STDOUT_CLEAN=0"
)
if "%STDOUT_CLEAN%"=="0" (
    echo FAIL: DX12 produced error/warning output.
    popd
    exit /b 5
)

call "%~dp0check_dx12_validation.bat"
if errorlevel 1 (
    popd
    exit /b 6
)

echo [7/7] Comparing DX12 captures against committed baselines...
set "SKORE_REPO=%REPO%"
"%PYTHON_EXE%" "%~dp0check_dx12_baselines.py" --repo "%REPO%" --out-root "%REPO%\TestOutput\validation\dx12_renderer"
if errorlevel 1 (
    echo FAIL: DX12 baseline comparison failed.
    popd
    exit /b 7
)

call "%~dp0validate_ready_builds.bat"
if errorlevel 1 (
    popd
    exit /b 8
)

echo.
echo ========================================
echo   VALIDATE_DX12_RENDERER: ALL PASSED
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
