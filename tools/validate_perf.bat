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
REM  Exit 0 = build+run succeeded and perf budgets/comparisons passed.
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

echo [1/4] Ensuring Profile x64 build...
if /I "%SKULLBONEZ_ASSUME_PROFILE_BUILT%"=="1" (
    echo PASS: Reusing prebuilt Profile x64.
) else (
    call "%~dp0validate_build.bat" Profile
    if errorlevel 1 exit /b 1
)

echo [2/4] Cleaning old perf artifacts...
del /q "%REPO%\Profile\perf_log.csv" 2>nul
del /q "%REPO%\Profile\*_perf_log.csv" 2>nul
del /q "%REPO%\Profile\*_perf.json" 2>nul

echo [3/4] Running DX12 perf tests...
echo.
echo Running dx12 perf test...
del /q "%REPO%\Profile\perf_log.csv" 2>nul
"%REPO%\Profile\SKULLBONEZ_CORE.exe" --renderer dx12 --vsync off --fixed-step --scene SkullbonezData/scenes/perf_test.scene.json
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
"%REPO%\Profile\SKULLBONEZ_CORE.exe" --vsync off --fixed-step --scene SkullbonezData/scenes/physics_bench_varied.scene.json
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

echo [4/4] Analyzing and comparing performance...
set "SKORE_REPO=%REPO%"
set "PYTHONUTF8=1"
set "PYTHONIOENCODING=utf-8"

for %%r in (dx12 physics_bench) do (
    echo.
    echo Analyzing %%r performance...
    "%PYTHON_EXE%" "%REPO%\Agentic\Skills\skore-render-test\analyze_perf.py" --renderer %%r --csv "%REPO%\Profile\%%r_perf_log.csv" --out-dir "%REPO%\Profile"
    if errorlevel 1 (
        echo FAIL: %%r perf analysis script failed.
        exit /b 5
    )
)

set "PERF_FAILURES=0"
for %%r in (dx12 physics_bench) do (
    echo.
    echo Checking %%r absolute performance budgets...
    "%PYTHON_EXE%" "%REPO%\tools\check_perf_budgets.py" --artifact "%REPO%\Profile\%%r_perf.json"
    if errorlevel 1 (
        echo FAIL: %%r absolute performance budget exceeded.
        set "PERF_FAILURES=1"
    )

    if exist "%REPO%\TestOutput\baselines\%%r_perf.json" (
        echo.
        echo %%r performance comparison vs baseline:
        "%PYTHON_EXE%" "%REPO%\Agentic\Skills\skore-render-test\perf_compare.py" --current "%REPO%\Profile\%%r_perf.json" --previous "%REPO%\TestOutput\baselines\%%r_perf.json"
        if errorlevel 1 (
            echo.
            echo FAIL: %%r performance regression detected. Review output above.
            set "PERF_FAILURES=1"
        )
    ) else (
        echo No baseline found at TestOutput\baselines\%%r_perf.json - skipping %%r comparison.
        echo Absolute performance budgets still apply for %%r.
    )
)

if "%PERF_FAILURES%"=="1" (
    echo.
    echo ========================================
    echo   VALIDATE_PERF: FAILED
    echo ========================================
    echo   Performance regressions are commit blockers.
    echo   Update baselines only when the slower numbers are intentional and reviewed.
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
echo   VALIDATE_PERF: COMPLETE
echo ========================================
popd
exit /b 0
