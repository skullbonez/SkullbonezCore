@rem
@rem File: tools\validate_automation.bat
@rem Purpose:
@rem   Proves that scripted replay/prediction diagnostics are absent from the
@rem   ordinary Profile game and remain operational in the Automation build.
@rem
@rem Summary:
@rem   Automation is a Profile-equivalent executable with one extra compile-time
@rem   diagnostics surface. This gate tests both sides of that boundary, then
@rem   runs the native UI interaction smoke, then uses
@rem   Skarness to prove the causal prediction path through production state.
@rem
@rem Glossary:
@rem   Automation build: Dedicated executable containing interaction scripts and
@rem     rich replay evidence views that normal game configurations omit.
@rem   Negative boundary: Expected Profile rejection proving the diagnostics
@rem     cannot silently return to the ordinary frame loop.
@rem
@rem Invariants:
@rem   - Profile must reject --interaction-script with a nonzero exit.
@rem   - Automation must preserve native UI input, client resize, and replay controls.
@rem   - Prediction passes only when one selected identity reaches full causal
@rem     trajectories, retained wireframe poses, production visual buffers, the
@rem     DX12 submission, and a connected viewport raster feature.
@rem   - Negative and positive processes use separate working files and PSO
@rem     caches, so they may overlap without changing either launch contract.
@rem   - This pre-commit smoke supplements, rather than replaces, the immutable
@rem     200-box replay visual-fidelity oracle required for replay-facing edits.
@rem
@rem Related:
@rem   - tools\validate_full.bat
@rem   - tools\validate_replay_visual_fidelity.bat
@rem   - SkullbonezSource\Runtime\Automation\InteractionAutomationController.cpp
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

if /I "%SKULLBONEZ_ASSUME_AUTOMATION_BUILT%"=="1" (
    echo [automation] Reusing prebuilt Automation x64.
) else (
    call "%~dp0validate_build.bat" Automation
    if errorlevel 1 goto fail
)

if not exist TestOutput\validation\automation mkdir TestOutput\validation\automation
set "REPORT=%REPO%\TestOutput\validation\automation\replay_prediction_precommit.json"
del /q "%REPORT%" 2>nul

echo [automation] Running isolated Profile rejection and Automation smoke processes in parallel...
"%PYTHON_EXE%" "%~dp0run_parallel_validation.py" --repo "%REPO%" --manifest "%~dp0validation_parallel_automation.json" --variable "AUTOMATION_REPORT=%REPORT%" --variable "PYTHON_EXE=%PYTHON_EXE%"
if errorlevel 1 (
    echo FAIL: Profile rejection or Automation native UI smoke failed.
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

echo [automation] Running the complete Skarness regression suite...
call "%~dp0validate_skarness.bat"
if errorlevel 1 (
    echo FAIL: Skarness regression suite failed.
    goto fail
)

echo PASS: diagnostics excluded from Profile; Automation native UI and Skarness regressions passed.
popd >nul
exit /b 0

:fail
echo VALIDATE_AUTOMATION: FAILED
popd >nul
exit /b 1
