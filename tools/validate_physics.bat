@rem
@rem File: tools/validate_physics.bat
@rem Purpose:
@rem   Documents and runs the validate_physics.bat developer/validation helper script.
@rem
@rem Summary:
@rem   The owner-approved golden digest is checked before any build. The gate
@rem   then builds or reuses Debug, proves the PhysicsEngine lifecycle, runs the
@rem   authored deterministic scene, and performs a byte-exact comparison.
@rem
@rem Glossary:
@rem   SkullScope: Queryable physics diagnostics workflow backed by bounded trace
@rem   output and local queries.
@rem   Engine lifecycle smoke: Small executable path that proves the shipping
@rem   PhysicsEngine can be constructed and stepped without window/renderer setup.
@rem   Validation gate: Repository script that proves a class of changes before
@rem   commit or PR.
@rem
@rem Invariants:
@rem   - A modified golden fails before build or launch unless its exact bytes
@rem   have an owner approval record.
@rem   - The engine lifecycle smoke must run before the scene regression so
@rem   owner-lifecycle failures are visible apart from scene loading or rendering.
@rem   - Commit-gate and parent full-gate calls defer ready-build restoration;
@rem   their callers own any later build fan-in.
@rem
@rem Related:
@rem   - AGENTS.md
@rem   - tools/check_physics_baseline_guard.py
@rem   - tools/check_physics_regression.py
@rem
@rem
@echo off
setlocal enabledelayedexpansion
REM ===============================================================
REM  validate_physics.bat - Core physics determinism regression test.
REM  Use for: normal physics, collision, solver, rigid body changes.
REM  Runtime: engine lifecycle smoke, one authored varied-scene launch, and baseline comparison.
REM ===============================================================

set "COMMIT_GATE=0"
if /I "%~1"=="--commit-gate" set "COMMIT_GATE=1"
if not "%~1"=="" if not "%COMMIT_GATE%"=="1" (
    echo ERROR: Unknown argument "%~1".
    echo Usage: tools\validate_physics.bat [--commit-gate]
    exit /b 64
)
if not "%~2"=="" (
    echo ERROR: Too many arguments.
    echo Usage: tools\validate_physics.bat [--commit-gate]
    exit /b 64
)

set "REPO=%~dp0.."
pushd "%REPO%"
call "%~dp0find_python.bat"
if errorlevel 1 exit /b 99
echo.
echo ========================================
echo   VALIDATE_PHYSICS - Determinism Check
echo ========================================
echo.

echo [1/6] Verifying owner-approved golden digest...
"%PYTHON_EXE%" "%~dp0check_physics_baseline_guard.py" --repo "%REPO%"
if errorlevel 1 (
    echo FAIL: Physics golden is missing owner approval or was modified.
    popd
    exit /b 3
)

echo [2/6] Ensuring Debug x64 build...
if /I "%SKULLBONEZ_ASSUME_DEBUG_BUILT%"=="1" (
    echo PASS: Reusing prebuilt Debug x64.
) else (
    call "%~dp0validate_build.bat" Debug
    if errorlevel 1 exit /b 1
)

echo [3/6] Running PhysicsEngine lifecycle smoke...
"%REPO%\Debug\SKULLBONEZ_CORE.exe" --physics-standalone-smoke
if errorlevel 1 (
    echo FAIL: PhysicsEngine lifecycle smoke failed.
    exit /b 2
)

echo [4/6] Running core physics regression scene...
del /q "%REPO%\Debug\physics_regression_*.csv" 2>nul

echo   Running physics_bench_varied...
"%REPO%\Debug\SKULLBONEZ_CORE.exe" --renderer dx12 --vsync off --fixed-step --shadows off --scene SkullbonezData/scenes/physics_bench_varied.scene.json --physics-regression-log Debug/physics_regression_varied.csv
if errorlevel 1 (
    echo FAIL: physics_bench_varied crashed or errored.
    exit /b 2
)

echo [5/6] Comparing output against baselines...
set "SKORE_REPO=%REPO%"
"%PYTHON_EXE%" "%~dp0check_physics_regression.py"
if errorlevel 1 (
    echo FAIL: Physics regression detected. Output differs from baselines.
    echo       Baseline dir: TestOutput\baselines
    echo       Actual dir:   Debug
    exit /b 2
)

echo [6/6] Leaving Profile and Debug builds ready...
if "%COMMIT_GATE%"=="1" (
    echo       Deferred by commit gate; only the deterministic Debug proof is required.
    goto :ready_complete
)
if /I "%SKULLBONEZ_SKIP_READY_BUILDS%"=="1" (
    echo       Deferred to parent validation gate.
    goto :ready_complete
)
call "%~dp0validate_ready_builds.bat"
if errorlevel 1 (
    popd
    exit /b 4
)

:ready_complete

echo.
echo ========================================
echo   VALIDATE_PHYSICS: ALL PASSED
echo ========================================
popd
exit /b 0
