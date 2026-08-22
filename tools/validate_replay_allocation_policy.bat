@rem
@rem File: tools/validate_replay_allocation_policy.bat
@rem Purpose:
@rem   Proves two live replay-prediction generations obey runtime allocation
@rem   phase and registered reserve-owner policy.
@rem
@rem Summary:
@rem   One hidden Automation process exercises enable, disable, and rebuild on
@rem   the tornado showcase, then strict allocation-guard output and interaction
@rem   completion are checked from bounded artifacts.
@rem
@rem Glossary:
@rem   Strict guard: Gameplay allocation mode whose nonzero violation count
@rem     makes the engine process fail.
@rem   Prediction generation: One private future-simulation build started by the
@rem     Predict control.
@rem
@rem Invariants:
@rem   - The script starts exactly one engine process and exactly two prediction
@rem     generations.
@rem   - Prior artifacts are removed before launch, so a missing report cannot
@rem     inherit success from an earlier run.
@rem   - Exit 0 requires both zero guard violations and a completed interaction
@rem     report; a clean early exit cannot create a false pass.
@rem   - Report proof is semantic JSON: at least 180 frames, a successful frame
@rem     180 path assertion, and exactly two completed prediction generations.
@rem
@rem Related:
@rem   - SkullbonezData/interaction/replay_allocation_policy_two_generation.json
@rem   - SkullbonezSource/Runtime/Replay/ReplayRetainedMemory.h
@rem
@echo off
setlocal
cd /d "%~dp0\.."
call tools\find_python.bat
if errorlevel 1 exit /b 1

echo [replay-allocation-policy] Building Automation...
call tools\validate_build.bat Automation
if errorlevel 1 exit /b 1

if not exist TestOutput\validation\replay_allocation_policy mkdir TestOutput\validation\replay_allocation_policy
set "REPORT=TestOutput\validation\replay_allocation_policy\two_generation_report.json"
set "LOG=TestOutput\validation\replay_allocation_policy\two_generation.log"
if exist "%REPORT%" del /q "%REPORT%"
if exist "%LOG%" del /q "%LOG%"

echo [replay-allocation-policy] Running strict two-generation prediction probe...
Automation\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\tornado_alley_showcase.scene.json --frames 220 --renderer dx12 --vsync off --cinematic off --shadows off --fixed-step --hide-top-text --automation-hidden-window --allocation-guard gameplay --dev-ui game --interaction-script SkullbonezData\interaction\replay_allocation_policy_two_generation.json --interaction-report "%REPORT%" --replay on --replay-seconds 2 > "%LOG%" 2>&1
if errorlevel 1 (
    echo FAIL: strict replay allocation process exited nonzero. See "%LOG%"
    exit /b 1
)

findstr /C:"gameplay_violations=0" "%LOG%" >nul
if errorlevel 1 (
    echo FAIL: zero gameplay-allocation evidence is missing. See "%LOG%"
    exit /b 1
)
findstr /C:"policy_violations=0" "%LOG%" >nul
if errorlevel 1 (
    echo FAIL: zero reserve-policy evidence is missing. See "%LOG%"
    exit /b 1
)
"%PYTHON_EXE%" -c "import json,sys; report=json.load(open(sys.argv[1], encoding='utf-8')); final=report.get('finalState', {}); assertions=report.get('assertions', []); visible=any(row.get('frame') == 180 and row.get('name') == 'predictionPathVisible' and row.get('passed') is True for row in assertions); ok=report.get('ok') is True and report.get('framesRun', 0) >= 180 and visible and final.get('predictionGenerationCount') == 2; sys.exit(0 if ok else 1)" "%REPORT%"
if errorlevel 1 (
    echo FAIL: interaction report did not prove the completed two-generation sequence. See "%REPORT%"
    exit /b 1
)

echo PASS: strict two-generation replay allocation policy is clean.
exit /b 0
