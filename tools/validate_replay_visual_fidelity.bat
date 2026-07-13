@echo off
setlocal
cd /d "%~dp0\.."

echo [replay-visual-fidelity] Building Profile; comparing against the approved Profile working base...
call tools\validate_build.bat Profile
if errorlevel 1 exit /b %errorlevel%

if not exist TestOutput\validation\replay_visual_fidelity mkdir TestOutput\validation\replay_visual_fidelity
set REPORT=TestOutput\validation\replay_visual_fidelity\full_reveal_probe_profile.json
set LOG=TestOutput\validation\replay_visual_fidelity\full_reveal_probe_profile.log

echo [replay-visual-fidelity] Running all 2401 reveal ticks; the full 200-box wall must topple...
Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync on --shadows off --hide-top-text --scene SkullbonezData\scenes\prediction_ragdoll_wall_200.scene.json --interaction-script SkullbonezData\interaction\prediction_ragdoll_wall_200_full_reveal.json --interaction-report %REPORT% --frames 4250 --replay on --replay-seconds 20 --fixed-step > %LOG% 2>&1
if errorlevel 1 (
    echo FAIL: 200-box replay visual probe exited nonzero. See %LOG%
    exit /b 1
)

python tools\check_replay_visual_fidelity.py --report %REPORT%
if errorlevel 1 exit /b %errorlevel%
python tools\check_replay_visual_fidelity.py --report %REPORT% --negative-control
if errorlevel 1 exit /b %errorlevel%
python tools\check_replay_visual_fidelity.py --report %REPORT% --incomplete-control
if errorlevel 1 exit /b %errorlevel%

echo PASS: frame-exact 200-box replay visual fidelity and negative control.
exit /b 0
