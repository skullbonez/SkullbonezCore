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
REM    tools\run_graphics_stress.bat [minutes|overnight] [seed] [actions] [scene_interval_frames] [memory_interval_frames]
REM
REM  minutes = 0 or overnight means run until stopped.
REM ===============================================================

set "REPO=%~dp0.."
set "DURATION_MINUTES=%~1"
set "SEED=%~2"
set "ACTIONS=%~3"
set "SCENE_INTERVAL=%~4"
set "MEMORY_INTERVAL=%~5"

if "%DURATION_MINUTES%"=="" set "DURATION_MINUTES=0"
if /I "%DURATION_MINUTES%"=="overnight" set "DURATION_MINUTES=0"
if "%SEED%"=="" set "SEED=3235774467"
if "%ACTIONS%"=="" set "ACTIONS=16"
if "%SCENE_INTERVAL%"=="" set "SCENE_INTERVAL=36"
if "%MEMORY_INTERVAL%"=="" set "MEMORY_INTERVAL=1800"

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
set "MEMORY_CSV=%OUT_DIR%\latest_memory.csv"
set "MEMORY_JSON=%OUT_DIR%\latest_memory.json"
del /q "%STDOUT%" "%STDERR%" "%EXIT_FILE%" "%MEMORY_CSV%" "%MEMORY_JSON%" 2>nul

set "EXE=%REPO%\Profile\SKULLBONEZ_CORE.exe"
set "RUN_ARGS=--renderer dx12 --vsync off --suite SkullbonezData/scenes/graphics_stress.suite.json --graphics-stress=on --graphics-stress-seed %SEED% --graphics-stress-actions %ACTIONS% --graphics-stress-scene-interval %SCENE_INTERVAL% --graphics-stress-memory-interval %MEMORY_INTERVAL% --memory-dump %MEMORY_JSON% --replay off"

echo [graphics-stress] exe: "%EXE%"
echo [graphics-stress] args: %RUN_ARGS%
echo [graphics-stress] stdout: "%STDOUT%"
echo [graphics-stress] stderr: "%STDERR%"
echo [graphics-stress] memory csv: "%MEMORY_CSV%"
echo [graphics-stress] shutdown memory json: "%MEMORY_JSON%"

set "SKORE_STRESS_EXE=%EXE%"
set "SKORE_STRESS_REPO=%REPO%"
set "SKORE_STRESS_ARGS=%RUN_ARGS%"
set "SKORE_STRESS_STDOUT=%STDOUT%"
set "SKORE_STRESS_STDERR=%STDERR%"
set "SKORE_STRESS_MINUTES=%DURATION_MINUTES%"
set "SKORE_STRESS_MEMORY_CSV=%MEMORY_CSV%"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_graphics_stress.ps1" -Exe "%SKORE_STRESS_EXE%" -Repo "%SKORE_STRESS_REPO%" -ArgumentLine "%SKORE_STRESS_ARGS%" -Stdout "%SKORE_STRESS_STDOUT%" -Stderr "%SKORE_STRESS_STDERR%" -MemoryCsv "%SKORE_STRESS_MEMORY_CSV%" -Minutes %SKORE_STRESS_MINUTES% -SampleSeconds 15
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
