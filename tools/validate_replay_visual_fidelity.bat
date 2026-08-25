@rem
@rem File: tools/validate_replay_visual_fidelity.bat
@rem Purpose:
@rem   Runs the immutable 200-box prediction-visual oracle and its negative
@rem   controls.
@rem
@rem Summary:
@rem   One hidden Automation engine process generates prediction exactly once.
@rem   Every later check is a non-engine CPU or durable-artifact comparison.
@rem
@rem Glossary:
@rem   Generation: Single future-simulation build triggered by the interaction
@rem     script; offline checks inspect its report and artifact bytes.
@rem
@rem Invariants:
@rem   - A gate invocation starts exactly one SKULLBONEZ_CORE process.
@rem   - That process presents one reveal and exits without advancing the live
@rem     scene.
@rem   - The immutable approved manifest is the independent determinism oracle.
@rem   - Generated report, log, and artifact paths are cleared before launch so
@rem     an early process exit cannot reuse evidence from a previous invocation.
@rem
@rem Related:
@rem   - tools/check_replay_visual_fidelity.py
@rem   - SkullbonezData/interaction/prediction_ragdoll_wall_200_full_reveal.json
@rem
@echo off
setlocal
cd /d "%~dp0\.."

echo [replay-visual-fidelity] Proving the launcher contains one engine process...
python tools\check_replay_visual_fidelity.py --launcher-control
if errorlevel 1 exit /b %errorlevel%

echo [replay-visual-fidelity] Building Automation; comparing against the approved working base...
call tools\validate_build.bat Automation
if errorlevel 1 exit /b %errorlevel%

echo [replay-visual-fidelity] Running typed packet and real-float negative controls (no engine)...
Profile\SKULLBONEZ_TESTS.exe --test-case=Replay?visual?* --no-skip
if errorlevel 1 exit /b %errorlevel%

if not exist TestOutput\validation\replay_visual_fidelity mkdir TestOutput\validation\replay_visual_fidelity
set REPORT=TestOutput\validation\replay_visual_fidelity\full_reveal_probe_profile.json
set LOG=TestOutput\validation\replay_visual_fidelity\full_reveal_probe_profile.log
set ARTIFACT=TestOutput\validation\replay_visual_fidelity\full_reveal_probe_profile.skreplay
if exist %REPORT% del /q %REPORT%
if exist %LOG% del /q %LOG%
if exist %ARTIFACT% del /q %ARTIFACT%

echo [replay-visual-fidelity] Authoritative run: the only engine process and prediction generation...
REM The prediction horizon remains the approved 20 seconds. Recording keeps a
REM one-second guard band because 2400 intervals have 2401 inclusive endpoints;
REM without it the source packet is evicted as the final packet arrives.
Automation\SKULLBONEZ_CORE.exe --renderer dx12 --vsync on --shadows off --hide-top-text --automation-hidden-window --scene SkullbonezData\scenes\prediction_ragdoll_wall_200.scene.json --interaction-script SkullbonezData\interaction\prediction_ragdoll_wall_200_full_reveal.json --interaction-report %REPORT% --frames 6800 --replay on --replay-seconds 21 --fixed-step > %LOG% 2>&1
if errorlevel 1 (
    echo FAIL: authoritative 200-box run exited nonzero. See %LOG%
    exit /b 1
)

python tools\check_replay_visual_fidelity.py --report %REPORT%
if errorlevel 1 exit /b %errorlevel%

python tools\check_replay_visual_fidelity.py --report %REPORT% --negative-control
if errorlevel 1 exit /b %errorlevel%
python tools\check_replay_visual_fidelity.py --report %REPORT% --trajectory-count-control
if errorlevel 1 exit /b %errorlevel%
python tools\check_replay_visual_fidelity.py --report %REPORT% --incomplete-control
if errorlevel 1 exit /b %errorlevel%
python tools\check_replay_visual_fidelity.py --report %REPORT% --causal-activation-control
if errorlevel 1 exit /b %errorlevel%
python tools\check_replay_visual_fidelity.py --report %REPORT% --causal-topology-control
if errorlevel 1 exit /b %errorlevel%
python tools\check_replay_visual_fidelity.py --report %REPORT% --causal-segment-control
if errorlevel 1 exit /b %errorlevel%
python tools\check_replay_visual_fidelity.py --report %REPORT% --semantic-packet-control
if errorlevel 1 exit /b %errorlevel%
python tools\check_replay_visual_fidelity.py --report %REPORT% --artifact-byte-control
if errorlevel 1 exit /b %errorlevel%
python tools\check_replay_visual_fidelity.py --report %REPORT% --prediction-artifact-control
if errorlevel 1 exit /b %errorlevel%
python tools\check_replay_visual_fidelity.py --report %REPORT% --run-determinism-controls
if errorlevel 1 exit /b %errorlevel%

echo PASS: one-presentation 200-box visual fidelity, causal reveal proof, durable artifact, and negative controls.
exit /b 0
