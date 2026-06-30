@rem
@rem File: tools/run_graphics_stress.bat
@rem Purpose:
@rem   Launch the persistent DX12 graphics stress run.
@rem
@rem Mental model:
@rem   Tools are command-line guardrails around builds, validation, screenshots,
@rem   diagnostics, and artifact handling. This runner keeps the graphics fuzzer
@rem   launch repeatable so crash seeds and logs can be reused later.
@rem
@rem Glossary:
@rem   Graphics stress: Deterministic runtime fuzzer that changes scenes,
@rem     cinematic settings, sky/fog/ray controls, water, debug overlays, and
@rem     generated object counts while DX12 is rendering.
@rem   Seed: 32-bit value that drives the deterministic stress random stream.
@rem   Validation gate: Repository script that proves a class of changes before
@rem     commit or PR.
@rem
@rem Invariants:
@rem   - Process cleanup uses the launched PID only.
@rem   - stdout and stderr are mirrored to stable files under TestOutput.
@rem
@rem Related:
@rem   - SkullbonezData/scenes/graphics_stress.suite.json
@rem   - SkullbonezSource/Runtime/RunStress.cpp
@rem   - Agentic/Reference/comment-style-guide.md
@rem
@echo off
setlocal enabledelayedexpansion
REM ===============================================================
REM  run_graphics_stress.bat - Launch the DX12 graphics stress fuzzer.
REM  Usage:
REM    tools\run_graphics_stress.bat [minutes|overnight] [seed] [actions] [scene_interval_frames]
REM
REM  minutes = 0 or overnight means run until stopped.
REM ===============================================================

set "REPO=%~dp0.."
set "DURATION_MINUTES=%~1"
set "SEED=%~2"
set "ACTIONS=%~3"
set "SCENE_INTERVAL=%~4"

if "%DURATION_MINUTES%"=="" set "DURATION_MINUTES=0"
if /I "%DURATION_MINUTES%"=="overnight" set "DURATION_MINUTES=0"
if "%SEED%"=="" set "SEED=3235774467"
if "%ACTIONS%"=="" set "ACTIONS=16"
if "%SCENE_INTERVAL%"=="" set "SCENE_INTERVAL=36"

pushd "%REPO%"
if not exist "%REPO%\Profile\SKULLBONEZ_CORE.exe" (
    echo [graphics-stress] Profile binary missing; building Profile first.
    call "%~dp0validate_build.bat" Profile
    if errorlevel 1 (
        popd
        exit /b 1
    )
)

set "OUT_DIR=%REPO%\TestOutput\graphics_stress"
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"
set "STDOUT=%OUT_DIR%\latest_stdout.txt"
set "STDERR=%OUT_DIR%\latest_stderr.txt"
set "EXIT_FILE=%OUT_DIR%\latest_exit.txt"
del /q "%STDOUT%" "%STDERR%" "%EXIT_FILE%" 2>nul

set "EXE=%REPO%\Profile\SKULLBONEZ_CORE.exe"
set "RUN_ARGS=--renderer dx12 --vsync off --suite SkullbonezData/scenes/graphics_stress.suite.json --graphics-stress=on --graphics-stress-seed %SEED% --graphics-stress-actions %ACTIONS% --graphics-stress-scene-interval %SCENE_INTERVAL% --replay off"

echo [graphics-stress] exe: "%EXE%"
echo [graphics-stress] args: %RUN_ARGS%
echo [graphics-stress] stdout: "%STDOUT%"
echo [graphics-stress] stderr: "%STDERR%"

if "%DURATION_MINUTES%"=="0" (
    echo [graphics-stress] Running until stopped.
    "%EXE%" %RUN_ARGS% >"%STDOUT%" 2>"%STDERR%"
    set "RUN_EXIT=!ERRORLEVEL!"
    >"%EXIT_FILE%" echo !RUN_EXIT!
    popd
    exit /b !RUN_EXIT!
)

set "SKORE_STRESS_EXE=%EXE%"
set "SKORE_STRESS_REPO=%REPO%"
set "SKORE_STRESS_ARGS=%RUN_ARGS%"
set "SKORE_STRESS_STDOUT=%STDOUT%"
set "SKORE_STRESS_STDERR=%STDERR%"
set "SKORE_STRESS_MINUTES=%DURATION_MINUTES%"

powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; $exe=$env:SKORE_STRESS_EXE; $repo=$env:SKORE_STRESS_REPO; $stdout=$env:SKORE_STRESS_STDOUT; $stderr=$env:SKORE_STRESS_STDERR; $minutes=[int]$env:SKORE_STRESS_MINUTES; $argv=$env:SKORE_STRESS_ARGS -split ' '; $p=Start-Process -FilePath $exe -ArgumentList $argv -WorkingDirectory $repo -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru; Write-Host ('[graphics-stress] PID ' + $p.Id + ' running for ' + $minutes + ' minute(s).'); if(-not $p.WaitForExit($minutes * 60000)){ Write-Host ('[graphics-stress] Timeout reached; stopping PID ' + $p.Id); $live=Get-Process -Id $p.Id -ErrorAction SilentlyContinue; if($null -ne $live){ Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; $p.WaitForExit(5000) | Out-Null }; exit 124 }; $p.Refresh(); exit $p.ExitCode"
set "RUN_EXIT=%ERRORLEVEL%"
if not "%RUN_EXIT%"=="0" if not "%RUN_EXIT%"=="124" (
    echo [graphics-stress] Process failed with exit code %RUN_EXIT%.
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "if(Test-Path -LiteralPath $env:SKORE_STRESS_STDOUT){ Write-Host '--- stdout tail ---'; Get-Content -LiteralPath $env:SKORE_STRESS_STDOUT -Tail 80 }; if(Test-Path -LiteralPath $env:SKORE_STRESS_STDERR){ Write-Host '--- stderr tail ---'; Get-Content -LiteralPath $env:SKORE_STRESS_STDERR -Tail 80 }"
)
if "%RUN_EXIT%"=="124" (
    echo [graphics-stress] Timed run completed and was stopped by PID timeout.
    set "RUN_EXIT=0"
)
>"%EXIT_FILE%" echo %RUN_EXIT%
popd
exit /b %RUN_EXIT%
