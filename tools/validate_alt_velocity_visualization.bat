@echo off
REM File: tools\validate_alt_velocity_visualization.bat
REM Purpose:
REM   Exercise the real ALT-VEL button and held gizmo drag in the terrainless
REM   N-body scene, proving prediction stays visible and consumes live samples.
REM
REM Invariants:
REM   - The Automation build drives the normal UI and pointer-routing path.
REM   - Every held-drag sample retains a visible prediction path.
REM   - At least one in-flight build is superseded before pointer release.
REM   - A 120-second Amortized replacement publishes changed velocity evidence
REM     while pointer capture is still held.
REM   - The newest sampled velocity owns the complete prediction after release.
setlocal

set "ROOT=%~dp0.."
pushd "%ROOT%" >nul

call tools\validate_build.bat Automation
if errorlevel 1 goto fail

if not exist TestOutput\interaction mkdir TestOutput\interaction

Automation\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\nbody_chaos_playground.scene.json --interaction-script SkullbonezData\interaction\nbody_velocity_drag_instant.json --interaction-report TestOutput\interaction\nbody_velocity_drag_instant_report.json --frames 120 --replay on --replay-seconds 4 --fixed-step --vsync off
if errorlevel 1 goto fail

Automation\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\nbody_chaos_playground.scene.json --interaction-script SkullbonezData\interaction\nbody_velocity_drag_amortized.json --interaction-report TestOutput\interaction\nbody_velocity_drag_amortized_report.json --frames 180 --replay on --replay-seconds 4 --fixed-step --vsync off
if errorlevel 1 goto fail

echo [alt-velocity] PASS
echo [alt-velocity] Reports: TestOutput\interaction\nbody_velocity_drag_instant_report.json and nbody_velocity_drag_amortized_report.json
echo [alt-velocity] Screenshot: TestOutput\interaction\nbody_velocity_drag_instant.bmp
popd >nul
exit /b 0

:fail
echo [alt-velocity] FAILED
popd >nul
exit /b 1
