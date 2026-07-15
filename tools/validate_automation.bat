@rem
@rem File: tools\validate_automation.bat
@rem Purpose:
@rem   Proves that scripted replay/prediction diagnostics are absent from the
@rem   ordinary Profile game and remain operational in the Automation build.
@rem
@rem Mental model:
@rem   Automation is a Profile-equivalent executable with one extra compile-time
@rem   diagnostics surface. This gate tests both sides of that boundary, then
@rem   runs the short replay prediction interaction used by every broad commit.
@rem
@rem Glossary:
@rem   Automation build: Dedicated executable containing interaction scripts and
@rem     rich replay evidence views that normal game configurations omit.
@rem   Negative boundary: Expected Profile rejection proving the diagnostics
@rem     cannot silently return to the ordinary frame loop.
@rem
@rem Invariants:
@rem   - Profile must reject --interaction-script with a nonzero exit.
@rem   - Automation must produce a successful replay prediction report.
@rem   - This pre-commit smoke supplements, rather than replaces, the immutable
@rem     200-box replay visual-fidelity oracle required for replay-facing edits.
@rem
@rem Related:
@rem   - tools\validate_full.bat
@rem   - tools\validate_replay_visual_fidelity.bat
@rem   - SkullbonezSource\Runtime\InteractionAutomationController.cpp
@echo off
setlocal EnableExtensions

set "REPO=%~dp0.."
pushd "%REPO%" >nul
call "%~dp0find_python.bat"
if errorlevel 1 goto fail

if /I "%SKULLBONEZ_ASSUME_PROFILE_BUILT%"=="1" (
    echo [automation] Reusing prebuilt Profile x64 for the negative boundary.
) else (
    call "%~dp0validate_build.bat" Profile
    if errorlevel 1 goto fail
)

call "%~dp0validate_build.bat" Automation
if errorlevel 1 goto fail

if not exist TestOutput\validation\automation mkdir TestOutput\validation\automation
set "NEGATIVE_LOG=TestOutput\validation\automation\profile_interaction_rejection.log"
set "POSITIVE_LOG=TestOutput\validation\automation\replay_prediction_precommit.log"
set "REPORT=TestOutput\validation\automation\replay_prediction_precommit.json"
del /q "%NEGATIVE_LOG%" "%POSITIVE_LOG%" "%REPORT%" 2>nul

echo [automation] Proving Profile rejects diagnostic interaction scripts...
Profile\SKULLBONEZ_CORE.exe --automation-hidden-window --frames 1 --interaction-script SkullbonezData\interaction\replay_prediction_click.json > "%NEGATIVE_LOG%" 2>&1
if not errorlevel 1 (
    echo FAIL: Profile accepted --interaction-script; diagnostics leaked into the ordinary game.
    goto fail
)

echo [automation] Running replay/prediction pre-commit smoke in Automation...
Automation\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --hide-top-text --automation-hidden-window --scene SkullbonezData\scenes\interaction_replay_prediction_harness.scene.json --interaction-script SkullbonezData\interaction\replay_prediction_click.json --interaction-report "%REPORT%" --frames 150 --replay on --replay-seconds 2 --fixed-step > "%POSITIVE_LOG%" 2>&1
if errorlevel 1 (
    echo FAIL: Automation replay/prediction launch failed. See %POSITIVE_LOG%.
    goto fail
)
if not exist "%REPORT%" (
    echo FAIL: Automation did not produce %REPORT%.
    goto fail
)
"%PYTHON_EXE%" -c "import json,sys; report=json.load(open(sys.argv[1], encoding='utf-8')); sys.exit(0 if report.get('ok') is True else 1)" "%REPORT%"
if errorlevel 1 (
    echo FAIL: Automation replay/prediction report did not contain ok=true.
    goto fail
)

echo PASS: diagnostics excluded from Profile; Automation replay/prediction smoke passed.
popd >nul
exit /b 0

:fail
echo VALIDATE_AUTOMATION: FAILED
popd >nul
exit /b 1
