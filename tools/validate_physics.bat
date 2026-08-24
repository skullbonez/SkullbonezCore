@rem
@rem File: tools/validate_physics.bat
@rem Purpose:
@rem   Documents and runs the validate_physics.bat developer/validation helper script.
@rem
@rem Summary:
@rem   The accepted golden digest is checked before any build. The gate
@rem   then builds or reuses Debug, proves the PhysicsEngine lifecycle, and runs
@rem   one authored scene in four clean processes for a byte-exact worker matrix.
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
@rem   have a content-bound transition receipt and immutable runtime archive.
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
REM  Runtime: engine lifecycle smoke, four authored varied-scene launches, and baseline comparison.
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

echo [1/7] Verifying accepted golden digest and retained transitions...
"%PYTHON_EXE%" "%~dp0check_physics_baseline_guard.py" --repo "%REPO%"
if errorlevel 1 (
    echo FAIL: Physics golden acceptance or retained transition integrity failed.
    popd
    exit /b 3
)

echo [2/7] Running physics comparator self-tests...
"%PYTHON_EXE%" "%~dp0check_physics_regression.py" --self-test
if errorlevel 1 (
    echo FAIL: Physics regression comparator self-tests failed.
    popd
    exit /b 2
)

echo [3/7] Ensuring Debug x64 build...
if /I "%SKULLBONEZ_ASSUME_DEBUG_BUILT%"=="1" (
    echo PASS: Reusing prebuilt Debug x64.
) else (
    call "%~dp0validate_build.bat" Debug
    if errorlevel 1 exit /b 1
)

echo [4/7] Running PhysicsEngine lifecycle smoke...
"%REPO%\Debug\SKULLBONEZ_CORE.exe" --physics-standalone-smoke
if errorlevel 1 (
    echo FAIL: PhysicsEngine lifecycle smoke failed.
    exit /b 2
)

echo [5/7] Running clean-process core physics worker matrix...
del /q "%REPO%\Debug\physics_regression_*.csv" 2>nul

echo   Running physics_bench_varied with workers=0 ^(primary^)...
"%REPO%\Debug\SKULLBONEZ_CORE.exe" --renderer dx12 --vsync off --fixed-step --shadows off --workers 0 --scene SkullbonezData/scenes/physics_bench_varied.scene.json --physics-regression-log Debug/physics_regression_varied.csv
if errorlevel 1 (
    echo FAIL: physics_bench_varied workers=0 primary crashed or errored.
    exit /b 2
)

echo   Running physics_bench_varied with workers=0 ^(repeat^)...
"%REPO%\Debug\SKULLBONEZ_CORE.exe" --renderer dx12 --vsync off --fixed-step --shadows off --workers 0 --scene SkullbonezData/scenes/physics_bench_varied.scene.json --physics-regression-log Debug/physics_regression_varied_workers_0_repeat.csv
if errorlevel 1 (
    echo FAIL: physics_bench_varied workers=0 repeat crashed or errored.
    exit /b 2
)

echo   Running physics_bench_varied with workers=1...
"%REPO%\Debug\SKULLBONEZ_CORE.exe" --renderer dx12 --vsync off --fixed-step --shadows off --workers 1 --scene SkullbonezData/scenes/physics_bench_varied.scene.json --physics-regression-log Debug/physics_regression_varied_workers_1.csv
if errorlevel 1 (
    echo FAIL: physics_bench_varied workers=1 crashed or errored.
    exit /b 2
)

echo   Running physics_bench_varied with workers=4...
"%REPO%\Debug\SKULLBONEZ_CORE.exe" --renderer dx12 --vsync off --fixed-step --shadows off --workers 4 --scene SkullbonezData/scenes/physics_bench_varied.scene.json --physics-regression-log Debug/physics_regression_varied_workers_4.csv
if errorlevel 1 (
    echo FAIL: physics_bench_varied workers=4 crashed or errored.
    exit /b 2
)

echo [6/7] Comparing clean-process worker matrix against itself and baseline...
set "SKORE_REPO=%REPO%"
"%PYTHON_EXE%" "%~dp0check_physics_regression.py" --worker-matrix
if errorlevel 1 (
    echo FAIL: Physics worker matrix differs between clean processes, worker counts, or baseline.
    echo       Baseline dir: TestOutput\baselines
    echo       Actual dir:   Debug
    exit /b 2
)

echo [7/7] Leaving Profile and Debug builds ready...
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
