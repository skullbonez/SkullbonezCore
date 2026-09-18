@rem Purpose:
@rem   Runs the complete Automation-build Skarness protocol, command, state,
@rem   query, and production future-render regression suite.
@echo off
setlocal EnableExtensions

set "REPO=%~dp0.."
pushd "%REPO%" >nul
if not defined PYTHON_EXE (
    call "%~dp0find_python.bat"
    if errorlevel 1 goto fail
)

echo [skarness] Transport and build boundaries...
"%PYTHON_EXE%" "%~dp0validate_skarness_transport.py" --output-root TestOutput\validation\skarness\transport
if errorlevel 1 goto fail

echo [skarness] Exact pause, step, timeout, and reconnect control...
"%PYTHON_EXE%" "%~dp0validate_skarness_run_control.py" --session TestOutput\validation\skarness\run-control
if errorlevel 1 goto fail

echo [skarness] Catto normal playback, domino impulse, reset, and scene switching...
"%PYTHON_EXE%" "%~dp0validate_catto_playback.py" --session "%REPO%\TestOutput\skarness\catto-playback-%RANDOM%"
if errorlevel 1 goto fail

echo [skarness] Player-control capability coverage...
"%PYTHON_EXE%" "%~dp0validate_skarness_command_coverage.py" --output-root TestOutput\validation\skarness\command-coverage
if errorlevel 1 goto fail

echo [skarness] Lazy velocity comparison, frozen Original, both choices, and cleanup...
"%PYTHON_EXE%" "%~dp0validate_velocity_divergence.py" --session TestOutput\skarness\validation\velocity-divergence
if errorlevel 1 goto fail

echo [skarness] Original and Modified prediction speed and quality...
"%PYTHON_EXE%" "%~dp0validate_velocity_prediction_speed.py" --session TestOutput\skarness\validation\velocity-speed
if errorlevel 1 goto fail

echo [skarness] Editor and Solver Lab fixed camera views...
"%PYTHON_EXE%" "%~dp0validate_editor_views.py" --session TestOutput\skarness\validation\editor-views
if errorlevel 1 exit /b 1
"%PYTHON_EXE%" "%~dp0validate_four_views.py" --session TestOutput\skarness\validation\four-views
if errorlevel 1 goto fail
"%PYTHON_EXE%" "%~dp0validate_scene_reset.py" --session "%REPO%\TestOutput\skarness\scene-reset-%RANDOM%"
if errorlevel 1 exit /b 1

echo [skarness] Snapshot, delta, eviction, reset, and Physics correlation state...
"%PYTHON_EXE%" "%~dp0validate_skarness_state_stream.py" --session TestOutput\validation\skarness\state-stream
if errorlevel 1 goto fail

echo [skarness] Incremental query negative controls and live joins...
"%PYTHON_EXE%" "%~dp0validate_skarness_queries.py" --self-test
if errorlevel 1 goto fail
"%PYTHON_EXE%" "%~dp0validate_skarness_queries.py" --session TestOutput\validation\skarness\queries
if errorlevel 1 goto fail

echo [skarness] Production future-render negative controls and live proof...
"%PYTHON_EXE%" "%~dp0validate_skarness_future_render.py" --self-test
if errorlevel 1 goto fail
"%PYTHON_EXE%" "%~dp0validate_skarness_future_render.py" --session TestOutput\validation\skarness\future-render
if errorlevel 1 goto fail

echo [skarness] Held causal playback and camera adjustments...
"%PYTHON_EXE%" "%~dp0validate_skarness_causal_playback.py" --session TestOutput\validation\skarness\causal-playback
if errorlevel 1 goto fail

echo [skarness] Catto causal ancestry, late detail playback, and equal-time camera transitions...
"%PYTHON_EXE%" "%~dp0validate_catto_causal_playback.py" --session "%REPO%\TestOutput\skarness\catto-causal-%RANDOM%"
if errorlevel 1 goto fail

echo [skarness] Cause category buttons, search, and retained selection...
"%PYTHON_EXE%" "%~dp0validate_cause_filters.py" --session "%REPO%\TestOutput\skarness\cause-filters-%RANDOM%"
if errorlevel 1 goto fail

echo [skarness] P shortcut pause, predict, clear and resume...
"%PYTHON_EXE%" "%~dp0validate_skarness_prediction_shortcut.py" --session TestOutput\validation\skarness\prediction-shortcut
if errorlevel 1 goto fail

echo [skarness] Dense horizon continuation, trimming, and target selection...
"%PYTHON_EXE%" "%~dp0validate_prediction_horizon.py" --session TestOutput/skarness/validation/prediction-horizon
if errorlevel 1 goto fail

echo [skarness] Long-horizon demand allocation and release...
"%PYTHON_EXE%" "%~dp0validate_prediction_memory.py" --session TestOutput/skarness/validation/prediction-memory
if errorlevel 1 goto fail

echo [skarness] Space 200 continuous horizon drag and curve quality...
"%PYTHON_EXE%" "%~dp0validate_space_prediction_horizon.py" --session TestOutput/skarness/validation/space-prediction-horizon
if errorlevel 1 goto fail

echo [skarness] Native capture release and window controls...
"%PYTHON_EXE%" "%~dp0validate_native_window.py" --session "%REPO%\TestOutput\skarness\native-window-%RANDOM%"
if errorlevel 1 goto fail

echo [skarness] Persistent multi-scene prediction matrix...
"%PYTHON_EXE%" "%~dp0validate_skarness_prediction_matrix.py" --self-test
if errorlevel 1 goto fail
"%PYTHON_EXE%" "%~dp0validate_skarness_prediction_matrix.py" --session TestOutput\validation\skarness\prediction-matrix
if errorlevel 1 goto fail

echo [skarness] Grass identity, recovery, recorded time, quality, and scene policy...
"%PYTHON_EXE%" "%~dp0validate_interactive_grass.py" --session "%REPO%\TestOutput\skarness\grass-validation-%RANDOM%"
if errorlevel 1 goto fail

echo [skarness] Grass swept shapes, terrain exclusions, overlap, capacity, and Physics isolation...
"%PYTHON_EXE%" "%~dp0validate_interactive_grass_edges.py" --session "%REPO%\TestOutput\skarness\grass-edges-%RANDOM%"
if errorlevel 1 goto fail

echo [skarness] Physics exact tick, settings persistence and overlay controls...
"%PYTHON_EXE%" "%~dp0validate_physics_window_actions.py" --session "%REPO%\TestOutput\skarness\physics-window-%RANDOM%"
if errorlevel 1 goto fail

echo [skarness] Physics compact layouts, four views, retained scroll and historical isolation...
"%PYTHON_EXE%" "%~dp0validate_physics_window_layouts.py" --session "%REPO%\TestOutput\skarness\physics-layouts-%RANDOM%"
if errorlevel 1 goto fail

echo [skarness] Physics worker edit, saved policy continuation and allocation guard...
"%PYTHON_EXE%" "%~dp0validate_physics_window_policy.py" --session "%REPO%\TestOutput\skarness\physics-policy-%RANDOM%"
if errorlevel 1 goto fail

echo PASS: Complete Skarness Automation regression suite passed.
popd >nul
exit /b 0

:fail
echo VALIDATE_SKARNESS: FAILED
popd >nul
exit /b 1
