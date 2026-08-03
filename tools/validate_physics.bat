@rem
@rem File: tools/validate_physics.bat
@rem Purpose:
@rem   Documents and runs the validate_physics.bat developer/validation helper script.
@rem
@rem Summary:
@rem   Tools are command-line guardrails around builds, validation, screenshots,
@rem   diagnostics, and artifact handling. They make the safe path repeatable and
@rem   keep output bounded for humans and agents.
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
@rem   - Tool output should be bounded and readable because agents and humans use
@rem   it for decisions.
@rem   - The engine lifecycle smoke must run before the scene regression so
@rem   owner-lifecycle failures are visible apart from scene loading or rendering.
@rem
@rem Related:
@rem   - AGENTS.md
@rem
@rem
@echo off
setlocal enabledelayedexpansion
REM ===============================================================
REM  validate_physics.bat - Core physics determinism regression test.
REM  Use for: normal physics, collision, solver, rigid body changes.
REM  Runtime: engine lifecycle smoke, one authored varied-scene launch, and baseline comparison.
REM ===============================================================

set "REPO=%~dp0.."
pushd "%REPO%"
call "%~dp0find_python.bat"
if errorlevel 1 exit /b 99
echo.
echo ========================================
echo   VALIDATE_PHYSICS - Determinism Check
echo ========================================
echo.

echo [1/5] Ensuring Debug x64 build...
if /I "%SKULLBONEZ_ASSUME_DEBUG_BUILT%"=="1" (
    echo PASS: Reusing prebuilt Debug x64.
) else (
    call "%~dp0validate_build.bat" Debug
    if errorlevel 1 exit /b 1
)

echo [2/5] Running PhysicsEngine lifecycle smoke...
"%REPO%\Debug\SKULLBONEZ_CORE.exe" --physics-standalone-smoke
if errorlevel 1 (
    echo FAIL: PhysicsEngine lifecycle smoke failed.
    exit /b 2
)

echo [3/5] Running core physics regression scene...
del /q "%REPO%\Debug\physics_regression_*.csv" 2>nul

echo   Running physics_bench_varied...
"%REPO%\Debug\SKULLBONEZ_CORE.exe" --renderer dx12 --vsync off --fixed-step --shadows off --scene SkullbonezData/scenes/physics_bench_varied.scene.json --physics-regression-log Debug/physics_regression_varied.csv
if errorlevel 1 (
    echo FAIL: physics_bench_varied crashed or errored.
    exit /b 2
)

echo [4/5] Comparing output against baselines...
set "SKORE_REPO=%REPO%"
"%PYTHON_EXE%" "%~dp0check_physics_regression.py"
if errorlevel 1 (
    echo FAIL: Physics regression detected. Output differs from baselines.
    echo       Baseline dir: TestOutput\baselines
    echo       Actual dir:   Debug
    exit /b 2
)

echo [5/5] Leaving Profile and Debug builds ready...
call "%~dp0validate_ready_builds.bat"
if errorlevel 1 (
    popd
    exit /b 4
)

echo.
echo ========================================
echo   VALIDATE_PHYSICS: ALL PASSED
echo ========================================
popd
exit /b 0
