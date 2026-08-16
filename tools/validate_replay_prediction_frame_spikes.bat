@rem
@rem File: tools/validate_replay_prediction_frame_spikes.bat
@rem Purpose:
@rem   Runs the diagnostic 120-second replay-prediction restart workload.
@rem
@rem Summary:
@rem   Uses the Automation build already produced by validate_full, generates a
@rem   perf-enabled copy of the 200-brick scene, waits for four closely spaced
@rem   prediction generations, and reports the largest profiler frames.
@rem
@rem Invariants:
@rem   - Prediction completion assertions precede every later replay interaction.
@rem   - The source scene remains unchanged; only TestOutput receives generated data.
@rem   - Frame times are diagnostic findings and never fixed-threshold failures.
@rem   - validate_full is the only validation pipeline that invokes this script,
@rem     and it deliberately treats every exit code as informational.
@rem   - The engine process has a 105-second watchdog so setup and analysis keep
@rem     the complete informational diagnostic inside a two-minute budget.
@rem
@rem Related:
@rem   - tools/analyze_replay_prediction_spikes.py
@rem   - SkullbonezData/interaction/replay_prediction_120s_frame_spike.json
@rem   - SkullbonezData/scenes/prediction_ragdoll_wall_200.scene.json
@rem
@echo off
setlocal EnableExtensions

set "REPO=%~dp0.."
pushd "%REPO%" >nul
call "%~dp0find_python.bat"
if errorlevel 1 goto fail

echo [replay-prediction-spikes] Running focused diagnostic-tool tests...
"%PYTHON_EXE%" -m unittest tools.test_analyze_replay_prediction_spikes -v
if errorlevel 1 goto fail

set "OUTPUT_DIR=TestOutput\diagnostics\replay_prediction_frame_spikes"
set "GENERATED_SCENE=%OUTPUT_DIR%\prediction_ragdoll_wall_200_perf.scene.json"
set "PERF_CSV=%OUTPUT_DIR%\perf.csv"
set "INTERACTION_REPORT=%OUTPUT_DIR%\interaction_report.json"
set "SPIKE_REPORT=%OUTPUT_DIR%\spikes.json"
set "RUN_LOG=%OUTPUT_DIR%\run.log"
set "INTERACTION_SCRIPT=SkullbonezData\interaction\replay_prediction_120s_frame_spike.json"

if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"
del /q "%GENERATED_SCENE%" "%PERF_CSV%" "%INTERACTION_REPORT%" "%SPIKE_REPORT%" "%RUN_LOG%" 2>nul

"%PYTHON_EXE%" tools\analyze_replay_prediction_spikes.py validate-script --script "%INTERACTION_SCRIPT%"
if errorlevel 1 goto fail

"%PYTHON_EXE%" tools\analyze_replay_prediction_spikes.py prepare-scene --source SkullbonezData\scenes\prediction_ragdoll_wall_200.scene.json --output "%GENERATED_SCENE%" --perf-log "TestOutput/diagnostics/replay_prediction_frame_spikes/perf.csv"
if errorlevel 1 goto fail

echo [replay-prediction-spikes] Running four completed 120-second prediction generations with a 105-second watchdog...
"%PYTHON_EXE%" tools\analyze_replay_prediction_spikes.py run-workload ^
    --executable Automation\SKULLBONEZ_CORE.exe ^
    --scene "%GENERATED_SCENE%" ^
    --script "%INTERACTION_SCRIPT%" ^
    --report "%INTERACTION_REPORT%" ^
    --log "%RUN_LOG%" ^
    --frames 3800 ^
    --timeout-seconds 105
if errorlevel 1 (
    echo FAIL: replay-prediction spike workload failed or exceeded its watchdog. See "%RUN_LOG%".
    goto fail
)

if not exist "%PERF_CSV%" (
    echo FAIL: replay-prediction spike workload did not produce "%PERF_CSV%".
    goto fail
)

if not exist "%INTERACTION_REPORT%" (
    echo FAIL: replay-prediction spike workload did not produce "%INTERACTION_REPORT%".
    goto fail
)

"%PYTHON_EXE%" -c "import json,sys; report=json.load(open(sys.argv[1], encoding='utf-8')); count=report.get('finalState', {}).get('predictionGenerationCount', 0); sys.exit(0 if report.get('ok') is True and count >= 4 else 1)" "%INTERACTION_REPORT%"
if errorlevel 1 (
    echo FAIL: interaction report did not prove four successful prediction generations. See "%INTERACTION_REPORT%".
    goto fail
)

"%PYTHON_EXE%" tools\analyze_replay_prediction_spikes.py analyze --csv "%PERF_CSV%" --interaction-report "%INTERACTION_REPORT%" --output "%SPIKE_REPORT%" --top-count 20 --correlation-radius 4
if errorlevel 1 goto fail

echo INFO: diagnostic artifacts were produced without imposing a frame-time threshold.
echo   %PERF_CSV%
echo   %INTERACTION_REPORT%
echo   %SPIKE_REPORT%
popd >nul
exit /b 0

:fail
set "EXIT_CODE=%errorlevel%"
if "%EXIT_CODE%"=="0" set "EXIT_CODE=1"
popd >nul
exit /b %EXIT_CODE%
