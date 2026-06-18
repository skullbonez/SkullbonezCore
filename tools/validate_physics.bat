@rem
@rem File: tools/validate_physics.bat
@rem Purpose:
@rem   Documents and runs the validate_physics.bat developer/validation helper script.
@rem
@rem Mental model:
@rem   Tools are command-line guardrails around builds, validation, screenshots,
@rem   diagnostics, and artifact handling. They make the safe path repeatable and
@rem   keep output bounded for humans and agents.
@rem
@rem Glossary:
@rem   SkullScope: Queryable physics diagnostics workflow backed by bounded trace
@rem   output and local queries.
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
REM  validate_physics.bat - Physics determinism regression test.
REM  Use for: physics, collision, solver, rigid body changes.
REM  Runtime: about 45 seconds.
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

echo [1/4] Ensuring Debug x64 build...
if /I "%SKULLBONEZ_ASSUME_DEBUG_BUILT%"=="1" (
    echo PASS: Reusing prebuilt Debug x64.
) else (
    call "%~dp0validate_build.bat" Debug
    if errorlevel 1 exit /b 1
)

echo [2/4] Running physics regression scenes...
del /q "%REPO%\Debug\physics_regression_*.csv" 2>nul
del /q "%REPO%\Debug\bullet_sweep_*.csv" 2>nul
del /q "%REPO%\Debug\shooting_reaction_*.csv" 2>nul
del /q "%REPO%\Debug\physics_known_*.csv" 2>nul

echo   Running physics_regression_solver...
"%REPO%\Debug\SKULLBONEZ_CORE.exe" --renderer dx12 --vsync off --fixed-step --shadows off --scene SkullbonezData/scenes/physics_regression_solver.scene.json --physics-regression-log Debug/physics_regression_solver.csv
if errorlevel 1 (
    echo FAIL: physics_regression_solver crashed or errored.
    exit /b 2
)

echo   Running bullet_sweep_wall...
"%REPO%\Debug\SKULLBONEZ_CORE.exe" --renderer dx12 --vsync off --fixed-step --shadows off --scene SkullbonezData/scenes/bullet_sweep_wall.scene.json --physics-collision-time-log Debug/bullet_sweep_wall.csv
if errorlevel 1 (
    echo FAIL: bullet_sweep_wall crashed or errored.
    exit /b 2
)

echo   Running bullet_sweep_object...
"%REPO%\Debug\SKULLBONEZ_CORE.exe" --renderer dx12 --vsync off --fixed-step --shadows off --scene SkullbonezData/scenes/bullet_sweep_object.scene.json --physics-collision-time-log Debug/bullet_sweep_object.csv
if errorlevel 1 (
    echo FAIL: bullet_sweep_object crashed or errored.
    exit /b 2
)

echo   Running bullet_sweep_terrain...
"%REPO%\Debug\SKULLBONEZ_CORE.exe" --renderer dx12 --vsync off --fixed-step --shadows off --scene SkullbonezData/scenes/bullet_sweep_terrain.scene.json --physics-collision-time-log Debug/bullet_sweep_terrain.csv
if errorlevel 1 (
    echo FAIL: bullet_sweep_terrain crashed or errored.
    exit /b 2
)

echo   Running shooting_reaction_volley...
"%REPO%\Debug\SKULLBONEZ_CORE.exe" --renderer dx12 --vsync off --fixed-step --shadows off --scene SkullbonezData/scenes/shooting_reaction_volley.scene.json --physics-regression-log Debug/shooting_reaction_volley.csv
if errorlevel 1 (
    echo FAIL: shooting_reaction_volley crashed or errored.
    exit /b 2
)

echo   Running physics_known_stacking...
"%REPO%\Debug\SKULLBONEZ_CORE.exe" --renderer dx12 --vsync off --fixed-step --shadows off --scene SkullbonezData/scenes/stacking.scene.json --physics-regression-log Debug/physics_known_stacking.csv
if errorlevel 1 (
    echo FAIL: physics_known_stacking crashed or errored.
    exit /b 2
)

echo   Running physics_known_at_rest...
"%REPO%\Debug\SKULLBONEZ_CORE.exe" --renderer dx12 --vsync off --fixed-step --shadows off --scene SkullbonezData/scenes/at_rest.scene.json --physics-regression-log Debug/physics_known_at_rest.csv
if errorlevel 1 (
    echo FAIL: physics_known_at_rest crashed or errored.
    exit /b 2
)

echo   Running physics_known_terrain_contact...
"%REPO%\Debug\SKULLBONEZ_CORE.exe" --renderer dx12 --vsync off --fixed-step --shadows off --scene SkullbonezData/scenes/terrain_contact_probe_debug.scene.json --physics-regression-log Debug/physics_known_terrain_contact.csv
if errorlevel 1 (
    echo FAIL: physics_known_terrain_contact crashed or errored.
    exit /b 2
)

echo [3/4] Comparing output against baselines...
set "SKORE_REPO=%REPO%"
"%PYTHON_EXE%" "%~dp0check_physics_regression.py"
if errorlevel 1 (
    echo FAIL: Physics regression detected. Output differs from baselines.
    echo       Baseline dir: TestOutput\baselines
    echo       Actual dir:   Debug
    exit /b 2
)

echo   Checking known physics issue signatures...
"%PYTHON_EXE%" "%~dp0check_physics_known_issue_regression.py"
if errorlevel 1 (
    echo FAIL: Known physics issue signature changed.
    echo       Baseline: TestOutput\baselines\physics_known_issue_signatures.json
    echo       Actual dir: Debug
    exit /b 2
)

echo   Checking shooting target reactions...
"%PYTHON_EXE%" "%~dp0check_shooting_reaction.py" "%REPO%\Debug\shooting_reaction_volley.csv"
if errorlevel 1 (
    echo FAIL: Shooting reaction regression detected.
    exit /b 2
)

echo [4/4] Checking SkullScope query baseline...
"%PYTHON_EXE%" "%~dp0check_physics_query_regression.py"
if errorlevel 1 (
    echo FAIL: SkullScope query regression detected.
    echo       Baseline: TestOutput\baselines\physics_query_varied.json
    echo       Trace:    Debug\physics_query_varied.physicsdiag.ndjson
    exit /b 3
)

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
