@echo off
REM File: tools/validate_replay_visual_fidelity.bat
REM Purpose: Run the immutable 200-box prediction-visual oracle and its controls.
REM Invariant: exactly one hidden engine process may generate prediction. The
REM later hidden Debug process only loads/scrubs the already-saved artifact.
setlocal
cd /d "%~dp0\.."

echo [replay-visual-fidelity] Building Profile; comparing against the approved Profile working base...
call tools\validate_build.bat Profile
if errorlevel 1 exit /b %errorlevel%

if not exist TestOutput\validation\replay_visual_fidelity mkdir TestOutput\validation\replay_visual_fidelity
set REPORT=TestOutput\validation\replay_visual_fidelity\full_reveal_probe_profile.json
set LOG=TestOutput\validation\replay_visual_fidelity\full_reveal_probe_profile.log
set ARTIFACT=TestOutput\validation\replay_visual_fidelity\full_reveal_probe_profile.skreplay
set LOAD_LOG=TestOutput\validation\replay_visual_fidelity\full_reveal_load_probe_debug.log

echo [replay-visual-fidelity] Running all 2401 reveal ticks; the full 200-box wall must topple...
REM The prediction horizon remains the approved 20 seconds. Recording keeps a
REM one-second guard band because 2400 intervals have 2401 inclusive endpoints;
REM without it the source packet is evicted as the final packet arrives.
Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync on --shadows off --hide-top-text --automation-hidden-window --scene SkullbonezData\scenes\prediction_ragdoll_wall_200.scene.json --interaction-script SkullbonezData\interaction\prediction_ragdoll_wall_200_full_reveal.json --interaction-report %REPORT% --frames 6800 --replay on --replay-seconds 21 --fixed-step > %LOG% 2>&1
if errorlevel 1 (
    echo FAIL: 200-box replay visual probe exited nonzero. See %LOG%
    exit /b 1
)

python tools\check_replay_visual_fidelity.py --report %REPORT%
if errorlevel 1 exit /b %errorlevel%

echo [replay-visual-fidelity] Loading and scrubbing the saved presentation in one fresh hidden process...
call tools\validate_build.bat Debug
if errorlevel 1 exit /b %errorlevel%
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --automation-hidden-window --scene SkullbonezData\scenes\prediction_ragdoll_wall_200.scene.json --replay-load-probe %ARTIFACT% > %LOAD_LOG% 2>&1
if errorlevel 1 (
    echo FAIL: fresh-process replay load/scrub probe exited nonzero. See %LOAD_LOG%
    exit /b 2
)
findstr /c:"Load probe passed" %LOAD_LOG% >nul
if errorlevel 1 (
    echo FAIL: fresh-process replay load/scrub proof was not reported. See %LOAD_LOG%
    exit /b 3
)

python tools\check_replay_visual_fidelity.py --report %REPORT% --negative-control
if errorlevel 1 exit /b %errorlevel%
python tools\check_replay_visual_fidelity.py --report %REPORT% --incomplete-control
if errorlevel 1 exit /b %errorlevel%
python tools\check_replay_visual_fidelity.py --report %REPORT% --causal-activation-control
if errorlevel 1 exit /b %errorlevel%
python tools\check_replay_visual_fidelity.py --report %REPORT% --causal-topology-control
if errorlevel 1 exit /b %errorlevel%
python tools\check_replay_visual_fidelity.py --report %REPORT% --causal-segment-control
if errorlevel 1 exit /b %errorlevel%

echo PASS: frame-exact 200-box replay visual fidelity, causal live proof, v3 save/load/scrub, and negative controls.
exit /b 0
