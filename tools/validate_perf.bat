@rem
@rem File: tools/validate_perf.bat
@rem Purpose:
@rem   Runs the bounded performance regression gate and compares current runtime
@rem   artifacts against committed baselines. The selected-ball path scenario
@rem   also guards against full compact-history rebuilds on live ring eviction.
@rem
@rem Mental model:
@rem   Tools are command-line guardrails around builds, validation, screenshots,
@rem   diagnostics, and artifact handling. They make the safe path repeatable and
@rem   keep output bounded for humans and agents.
@rem
@rem Glossary:
@rem   Validation gate: Repository script that proves a class of changes before
@rem   commit or PR.
@rem   Headless perf args: Launch flags used to remove unrelated interactive or
@rem     audio-side work from perf comparisons.
@rem   Structural perf proof: Counter-based assertion that rejects an expensive
@rem     algorithmic path without relying on machine-specific frame timings.
@rem   Scale matrix: Measurement-only 200/520/1,000/2,000-body artifacts used
@rem     to ratify and track the physics fixed-step budget. These rows are
@rem     reported but do not compare against a committed baseline before the
@rem     SIMD cutover ceremony.
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
REM Keep contact audio out of perf scenes; audio has a separate smoke path, and
REM decoded samples/XAudio startup would pollute render/physics measurements.
set "PERF_HEADLESS_ARGS=--no-contact-audio"

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

echo [1/5] Ensuring Profile x64 build...
if /I "%SKULLBONEZ_ASSUME_PROFILE_BUILT%"=="1" (
    echo PASS: Reusing prebuilt Profile x64.
) else (
    call "%~dp0validate_build.bat" Profile
    if errorlevel 1 exit /b 1
)

echo [2/5] Checking runtime allocation policy...
"%PYTHON_EXE%" "%REPO%\tools\check_allocation_policy.py" --repo "%REPO%"
if errorlevel 1 exit /b 9

echo [3/5] Cleaning old perf artifacts...
del /q "%REPO%\Profile\perf_log.csv" 2>nul
del /q "%REPO%\Profile\*_perf_log.csv" 2>nul
del /q "%REPO%\Profile\*_perf.json" 2>nul

echo [4/5] Running allocation guard and DX12 perf tests...
echo.
echo Running allocation guard perf_1000 smoke...
set "ALLOC_GUARD_LOG=%REPO%\TestOutput\validation\agent_logs\allocation_guard_perf_1000.log"
if not exist "%REPO%\TestOutput\validation\agent_logs" mkdir "%REPO%\TestOutput\validation\agent_logs"
"%REPO%\Profile\SKULLBONEZ_CORE.exe" --renderer dx12 --vsync off --fixed-step %PERF_HEADLESS_ARGS% --allocation-guard gameplay --frames 180 --scene SkullbonezData/scenes/perf_1000.scene.json > "%ALLOC_GUARD_LOG%" 2>&1
set "ALLOC_GUARD_EXIT=!errorlevel!"
type "%ALLOC_GUARD_LOG%"
if not "!ALLOC_GUARD_EXIT!"=="0" (
    echo FAIL: allocation guard perf_1000 smoke failed.
    exit /b 9
)
findstr /C:"[allocation-guard] mode=gameplay" "%ALLOC_GUARD_LOG%" >nul
if errorlevel 1 (
    echo FAIL: allocation guard summary marker missing.
    exit /b 9
)
findstr /C:"[allocation-guard] PASS:" "%ALLOC_GUARD_LOG%" >nul
if errorlevel 1 (
    echo FAIL: allocation guard clean PASS marker missing.
    exit /b 9
)
findstr /C:"[allocation-guard] WARNING" "%ALLOC_GUARD_LOG%" >nul
if errorlevel 1 (
    echo PASS: allocation guard evidence is clean for perf_1000.
) else (
    echo WARN: allocation guard evidence is warning-bearing; steady gameplay allocations remain to be converted.
)
echo.
echo Running selected-ball live-path structural perf regression...
call "%~dp0validate_build.bat" Automation
if errorlevel 1 exit /b 1
set "SELECTED_PATH_REPORT=%REPO%\TestOutput\interaction\selected_ball_path_perf_report.json"
if not exist "%REPO%\TestOutput\interaction" mkdir "%REPO%\TestOutput\interaction"
del /q "%SELECTED_PATH_REPORT%" 2>nul
"%REPO%\Automation\SKULLBONEZ_CORE.exe" --renderer dx12 --vsync off --fixed-step %PERF_HEADLESS_ARGS% --replay on --replay-seconds 1 --frames 330 --scene SkullbonezData/scenes/interaction_replay_prediction_harness.scene.json --interaction-script SkullbonezData/interaction/selected_ball_path_perf.json --interaction-report "%SELECTED_PATH_REPORT%"
if errorlevel 1 (
    echo FAIL: selected-ball live-path structural perf regression failed.
    exit /b 9
)
if not exist "%SELECTED_PATH_REPORT%" (
    echo FAIL: selected-ball live-path report was not produced.
    exit /b 9
)
echo PASS: selected-ball path used one initial build, incremental ring trims, and a continuously published prefix.
echo.
echo Running dx12 perf test...
del /q "%REPO%\Profile\perf_log.csv" 2>nul
"%REPO%\Profile\SKULLBONEZ_CORE.exe" --renderer dx12 --vsync off --fixed-step %PERF_HEADLESS_ARGS% --scene SkullbonezData/scenes/perf_test.scene.json
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
"%REPO%\Profile\SKULLBONEZ_CORE.exe" --vsync off --fixed-step %PERF_HEADLESS_ARGS% --scene SkullbonezData/scenes/physics_bench_varied.scene.json
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
echo Running measurement-only physics scale matrix...
for %%n in (200 520 1000 2000) do (
    del /q "%REPO%\Profile\physics_scale_%%n_perf_log.csv" 2>nul
    "%REPO%\Profile\SKULLBONEZ_CORE.exe" --vsync off --fixed-step --shadows off %PERF_HEADLESS_ARGS% --scene SkullbonezData/scenes/physics_scale_%%n.scene.json
    if errorlevel 1 (
        echo FAIL: physics_scale_%%n scene crashed or errored.
        exit /b 6
    )
    if not exist "%REPO%\Profile\physics_scale_%%n_perf_log.csv" (
        echo FAIL: physics_scale_%%n_perf_log.csv was not produced.
        exit /b 6
    )
)

echo [5/5] Analyzing and comparing performance...
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

for %%n in (200 520 1000 2000) do (
    echo.
    echo Analyzing measurement-only physics_scale_%%n performance...
    "%PYTHON_EXE%" "%REPO%\Agentic\Skills\skore-render-test\analyze_perf.py" --renderer physics_scale_%%n --csv "%REPO%\Profile\physics_scale_%%n_perf_log.csv" --out-dir "%REPO%\Profile"
    if errorlevel 1 (
        echo FAIL: physics_scale_%%n perf analysis failed.
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
