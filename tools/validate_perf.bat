@rem
@rem File: tools/validate_perf.bat
@rem Purpose:
@rem   Documents and runs the validate_perf.bat developer/validation helper script.
@rem
@rem Mental model:
@rem   Tools are command-line guardrails around builds, validation, screenshots,
@rem   diagnostics, and artifact handling. They make the safe path repeatable and
@rem   keep output bounded for humans and agents.
@rem
@rem Glossary:
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
REM  validate_perf.bat - Performance regression detection.
REM  Use for: optimization work, hot-path changes, allocation changes.
REM  Runtime: about 1 minute.
REM  Exit 0 = build+run succeeded; perf regressions are shown for review.
REM ===============================================================

set "REPO=%~dp0.."
pushd "%REPO%"
call "%~dp0find_python.bat"
if errorlevel 1 exit /b 99
call "%~dp0find_git.bat"
if errorlevel 1 exit /b 99
echo.
echo ========================================
echo   VALIDATE_PERF - Performance Check
echo ========================================
echo.

echo [1/4] Building Profile x64...
call "%~dp0validate_build.bat" Profile
if errorlevel 1 exit /b 1

echo [2/4] Cleaning old perf artifacts...
del /q "%REPO%\Profile\perf_log.csv" 2>nul
del /q "%REPO%\Profile\*_perf_log.csv" 2>nul
del /q "%REPO%\Profile\*_perf.json" 2>nul

echo [3/4] Running tri-renderer perf tests...
echo.
echo Running gl perf test...
del /q "%REPO%\Profile\perf_log.csv" 2>nul
"%REPO%\Profile\SKULLBONEZ_CORE.exe" --vsync off --fixed-step --scene SkullbonezData/scenes/perf_test.scene
if errorlevel 1 (
    echo FAIL: perf_test scene crashed for gl.
    exit /b 2
)
if not exist "%REPO%\Profile\perf_log.csv" (
    echo FAIL: perf_log.csv not produced for gl.
    exit /b 2
)
move /Y "%REPO%\Profile\perf_log.csv" "%REPO%\Profile\gl_perf_log.csv" >nul
if errorlevel 1 (
    echo FAIL: Could not store gl perf log.
    exit /b 2
)

echo.
echo Running dx11 perf test...
del /q "%REPO%\Profile\perf_log.csv" 2>nul
"%REPO%\Profile\SKULLBONEZ_CORE.exe" --renderer dx11 --vsync off --fixed-step --scene SkullbonezData/scenes/perf_test.scene
if errorlevel 1 (
    echo FAIL: perf_test scene crashed for dx11.
    exit /b 3
)
if not exist "%REPO%\Profile\perf_log.csv" (
    echo FAIL: perf_log.csv not produced for dx11.
    exit /b 3
)
move /Y "%REPO%\Profile\perf_log.csv" "%REPO%\Profile\dx11_perf_log.csv" >nul
if errorlevel 1 (
    echo FAIL: Could not store dx11 perf log.
    exit /b 3
)

echo.
echo Running dx12 perf test...
del /q "%REPO%\Profile\perf_log.csv" 2>nul
"%REPO%\Profile\SKULLBONEZ_CORE.exe" --renderer dx12 --vsync off --fixed-step --scene SkullbonezData/scenes/perf_test.scene
if errorlevel 1 (
    echo FAIL: perf_test scene crashed for dx12.
    exit /b 4
)
if not exist "%REPO%\Profile\perf_log.csv" (
    echo FAIL: perf_log.csv not produced for dx12.
    exit /b 4
)
move /Y "%REPO%\Profile\perf_log.csv" "%REPO%\Profile\dx12_perf_log.csv" >nul
if errorlevel 1 (
    echo FAIL: Could not store dx12 perf log.
    exit /b 4
)

echo.
echo Running physics_bench physics perf test...
del /q "%REPO%\Profile\varied_physics_perf_log.csv" 2>nul
"%REPO%\Profile\SKULLBONEZ_CORE.exe" --vsync off --fixed-step --scene SkullbonezData/scenes/physics_bench_varied.scene
if errorlevel 1 (
    echo FAIL: physics_bench_varied scene crashed for physics_bench.
    exit /b 6
)
if not exist "%REPO%\Profile\varied_physics_perf_log.csv" (
    echo FAIL: varied_physics_perf_log.csv not produced for physics_bench.
    exit /b 6
)
move /Y "%REPO%\Profile\varied_physics_perf_log.csv" "%REPO%\Profile\physics_bench_perf_log.csv" >nul
if errorlevel 1 (
    echo FAIL: Could not store physics_bench perf log.
    exit /b 6
)

echo.
echo Running physics_bench_no_sleep physics perf test...
del /q "%REPO%\Profile\varied_physics_perf_log.csv" 2>nul
"%REPO%\Profile\SKULLBONEZ_CORE.exe" --vsync off --fixed-step --no-sleep --scene SkullbonezData/scenes/physics_bench_varied.scene
if errorlevel 1 (
    echo FAIL: physics_bench_varied scene crashed for physics_bench_no_sleep.
    exit /b 7
)
if not exist "%REPO%\Profile\varied_physics_perf_log.csv" (
    echo FAIL: varied_physics_perf_log.csv not produced for physics_bench_no_sleep.
    exit /b 7
)
move /Y "%REPO%\Profile\varied_physics_perf_log.csv" "%REPO%\Profile\physics_bench_no_sleep_perf_log.csv" >nul
if errorlevel 1 (
    echo FAIL: Could not store physics_bench_no_sleep perf log.
    exit /b 7
)

echo [4/4] Analyzing and comparing performance...
set "SKORE_REPO=%REPO%"
set "PYTHONUTF8=1"
set "PYTHONIOENCODING=utf-8"

for %%r in (gl dx11 dx12 physics_bench physics_bench_no_sleep) do (
    echo.
    echo Analyzing %%r performance...
    "%PYTHON_EXE%" "%REPO%\Agentic\Skills\skore-render-test\analyze_perf.py" --renderer %%r --csv "%REPO%\Profile\%%r_perf_log.csv" --out-dir "%REPO%\Profile"
    if errorlevel 1 (
        echo FAIL: %%r perf analysis script failed.
        exit /b 5
    )
)

set "REGRESSION_WARNINGS=0"
for %%r in (gl dx11 dx12 physics_bench physics_bench_no_sleep) do (
    if exist "%REPO%\TestOutput\baselines\%%r_perf.json" (
        echo.
        echo %%r performance comparison vs baseline:
        "%PYTHON_EXE%" "%REPO%\Agentic\Skills\skore-render-test\perf_compare.py" --current "%REPO%\Profile\%%r_perf.json" --previous "%REPO%\TestOutput\baselines\%%r_perf.json"
        if errorlevel 1 (
            echo.
            echo WARNING: %%r performance regression detected. Review output above.
            REM Perf regressions need human judgment, so the script still exits 0.
            set "REGRESSION_WARNINGS=1"
        )
    ) else (
        echo No baseline found at TestOutput\baselines\%%r_perf.json - skipping %%r comparison.
    )
)

call "%~dp0validate_ready_builds.bat"
if errorlevel 1 (
    popd
    exit /b 8
)

echo.
echo ========================================
echo   VALIDATE_PERF: COMPLETE
echo ========================================
if "%REGRESSION_WARNINGS%"=="1" (
    echo   Review performance warnings above.
    echo ========================================
)
popd
exit /b 0
