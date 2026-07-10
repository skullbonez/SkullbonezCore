@echo off
REM File: tools\validate_interaction_clicks.bat
REM Purpose:
REM   Validate scripted runtime interaction clicks for inspect gizmo, Attach
REM   target selection, manipulator pickup, and replay prediction workflows.
REM
REM Mental model:
REM   This is a focused UI/runtime interaction gate. It builds Profile, runs
REM   deterministic interaction scripts, and writes reports/screenshots for the
REM   four covered click paths.
REM
REM Glossary:
REM   Interaction script: JSON input sequence consumed by the runtime.
REM   Interaction report: JSON artifact describing what the scripted click did.
REM
REM Invariants:
REM   - The script fails on any build or executable error.
REM   - Reports are written under TestOutput\interaction for review.
REM
REM Related:
REM   - AGENTS.md
REM   - SkullbonezData\interaction
setlocal

set ROOT=%~dp0..
pushd "%ROOT%" >nul

call tools\validate_build.bat Profile
if errorlevel 1 goto fail

if not exist TestOutput\interaction mkdir TestOutput\interaction

Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\interaction_inspect_gizmo_harness.scene.json --interaction-script SkullbonezData\interaction\inspect_gizmo_click.json --interaction-report TestOutput\interaction\inspect_gizmo_click_report.json --frames 90 --vsync off
if errorlevel 1 goto fail

Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\interaction_inspect_gizmo_harness.scene.json --interaction-script SkullbonezData\interaction\attach_target_click.json --interaction-report TestOutput\interaction\attach_target_click_report.json --frames 90 --vsync off
if errorlevel 1 goto fail

Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\interaction_replay_prediction_harness.scene.json --interaction-script SkullbonezData\interaction\manipulator_pickup_click.json --interaction-report TestOutput\interaction\manipulator_pickup_click_report.json --frames 90 --fixed-step --vsync off
if errorlevel 1 goto fail

Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\interaction_replay_prediction_harness.scene.json --interaction-script SkullbonezData\interaction\replay_prediction_click.json --interaction-report TestOutput\interaction\replay_prediction_click_report.json --frames 150 --replay on --replay-seconds 2 --fixed-step --vsync off
if errorlevel 1 goto fail

echo [interaction] Reports:
echo   TestOutput\interaction\inspect_gizmo_click_report.json
echo   TestOutput\interaction\attach_target_click_report.json
echo   TestOutput\interaction\manipulator_pickup_click_report.json
echo   TestOutput\interaction\replay_prediction_click_report.json
echo [interaction] Screenshots:
echo   TestOutput\interaction\inspect_gizmo_after_click.bmp
echo   TestOutput\interaction\attach_target_after_click.bmp
echo   TestOutput\interaction\manipulator_pickup_after_click.bmp
echo   TestOutput\interaction\replay_prediction_after_click.bmp
popd >nul
exit /b 0

:fail
echo [interaction] FAILED
popd >nul
exit /b 1
