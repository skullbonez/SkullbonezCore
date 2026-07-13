@echo off
REM File: tools/validate_replay_visual_fidelity.bat
REM Purpose: Run the immutable 200-box prediction-visual oracle and its controls.
REM Invariant: V4 runs two sequential hidden clean processes for determinism;
REM each process may generate exactly once and can never overlap the other. The
REM later hidden Debug process only loads/scrubs an already-saved artifact.
setlocal
cd /d "%~dp0\.."

echo [replay-visual-fidelity] Building Profile; comparing against the approved Profile working base...
call tools\validate_build.bat Profile
if errorlevel 1 exit /b %errorlevel%

if not exist TestOutput\validation\replay_visual_fidelity mkdir TestOutput\validation\replay_visual_fidelity
set REPORT_A=TestOutput\validation\replay_visual_fidelity\full_reveal_probe_profile_a.json
set REPORT_B=TestOutput\validation\replay_visual_fidelity\full_reveal_probe_profile_b.json
set LOG_A=TestOutput\validation\replay_visual_fidelity\full_reveal_probe_profile_a.log
set LOG_B=TestOutput\validation\replay_visual_fidelity\full_reveal_probe_profile_b.log
set ARTIFACT_A=TestOutput\validation\replay_visual_fidelity\full_reveal_probe_profile_a.skreplay
set LOAD_LOG=TestOutput\validation\replay_visual_fidelity\full_reveal_load_probe_debug.log

echo [replay-visual-fidelity] Clean run A: one hidden generation, all 2401 reveal ticks...
REM The prediction horizon remains the approved 20 seconds. Recording keeps a
REM one-second guard band because 2400 intervals have 2401 inclusive endpoints;
REM without it the source packet is evicted as the final packet arrives.
Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync on --shadows off --hide-top-text --automation-hidden-window --scene SkullbonezData\scenes\prediction_ragdoll_wall_200.scene.json --interaction-script SkullbonezData\interaction\prediction_ragdoll_wall_200_full_reveal.json --interaction-report %REPORT_A% --frames 6800 --replay on --replay-seconds 21 --fixed-step > %LOG_A% 2>&1
if errorlevel 1 (
    echo FAIL: 200-box clean run A exited nonzero. See %LOG_A%
    exit /b 1
)

python tools\check_replay_visual_fidelity.py --report %REPORT_A%
if errorlevel 1 exit /b %errorlevel%

echo [replay-visual-fidelity] Clean run B: fresh hidden process, one generation, no overlap with A...
Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync on --shadows off --hide-top-text --automation-hidden-window --scene SkullbonezData\scenes\prediction_ragdoll_wall_200.scene.json --interaction-script SkullbonezData\interaction\prediction_ragdoll_wall_200_full_reveal.json --interaction-report %REPORT_B% --frames 6800 --replay on --replay-seconds 21 --fixed-step > %LOG_B% 2>&1
if errorlevel 1 (
    echo FAIL: 200-box clean run B exited nonzero. See %LOG_B%
    exit /b 2
)

python tools\check_replay_visual_fidelity.py --report %REPORT_B%
if errorlevel 1 exit /b %errorlevel%
python tools\check_replay_visual_fidelity.py --report %REPORT_A% --compare-report %REPORT_B%
if errorlevel 1 exit /b %errorlevel%

echo [replay-visual-fidelity] Loading and scrubbing the saved presentation in one fresh hidden process...
call tools\validate_build.bat Debug
if errorlevel 1 exit /b %errorlevel%
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --automation-hidden-window --scene SkullbonezData\scenes\prediction_ragdoll_wall_200.scene.json --replay-load-probe %ARTIFACT_A% > %LOAD_LOG% 2>&1
if errorlevel 1 (
    echo FAIL: fresh-process replay load/scrub probe exited nonzero. See %LOAD_LOG%
    exit /b 3
)
findstr /c:"Load probe passed" %LOAD_LOG% >nul
if errorlevel 1 (
    echo FAIL: fresh-process replay load/scrub proof was not reported. See %LOAD_LOG%
    exit /b 4
)

python tools\check_replay_visual_fidelity.py --report %REPORT_A% --negative-control
if errorlevel 1 exit /b %errorlevel%
python tools\check_replay_visual_fidelity.py --report %REPORT_A% --incomplete-control
if errorlevel 1 exit /b %errorlevel%
python tools\check_replay_visual_fidelity.py --report %REPORT_A% --causal-activation-control
if errorlevel 1 exit /b %errorlevel%
python tools\check_replay_visual_fidelity.py --report %REPORT_A% --causal-topology-control
if errorlevel 1 exit /b %errorlevel%
python tools\check_replay_visual_fidelity.py --report %REPORT_A% --causal-segment-control
if errorlevel 1 exit /b %errorlevel%
python tools\check_replay_visual_fidelity.py --report %REPORT_A% --compare-report %REPORT_B% --run-determinism-controls
if errorlevel 1 exit /b %errorlevel%

echo PASS: two-process deterministic 200-box visual fidelity, causal live proof, v3 save/load/scrub, and false-pass controls.
exit /b 0
