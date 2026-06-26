@echo off
setlocal

set ROOT=%~dp0..
pushd "%ROOT%" >nul

call tools\validate_build.bat Profile
if errorlevel 1 goto fail

if not exist TestOutput\interaction mkdir TestOutput\interaction

Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\interaction_inspect_gizmo_harness.scene.json --interaction-script SkullbonezData\interaction\inspect_gizmo_click.json --interaction-report TestOutput\interaction\inspect_gizmo_click_report.json --frames 90 --vsync off
if errorlevel 1 goto fail

Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\interaction_replay_prediction_harness.scene.json --interaction-script SkullbonezData\interaction\replay_prediction_click.json --interaction-report TestOutput\interaction\replay_prediction_click_report.json --frames 150 --replay on --replay-seconds 2 --fixed-step --vsync off
if errorlevel 1 goto fail

echo [interaction] Reports:
echo   TestOutput\interaction\inspect_gizmo_click_report.json
echo   TestOutput\interaction\replay_prediction_click_report.json
echo [interaction] Screenshots:
echo   TestOutput\interaction\inspect_gizmo_after_click.bmp
echo   TestOutput\interaction\replay_prediction_after_click.bmp
popd >nul
exit /b 0

:fail
echo [interaction] FAILED
popd >nul
exit /b 1
