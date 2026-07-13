@rem
@rem File: tools/validate_replay_scrub.bat
@rem Purpose:
@rem   Preserves the historical replay-scrub entry point while delegating to
@rem   the authoritative 200-box replay visual-fidelity gate.
@rem
@rem Mental model:
@rem   Tools are command-line guardrails around builds, validation, screenshots,
@rem   diagnostics, and artifact handling. This file is a compatibility entry
@rem   point, not a second oracle: the complete visual, causal, deterministic,
@rem   artifact-load, and scrub contract belongs to
@rem   validate_replay_visual_fidelity.bat.
@rem
@rem Glossary:
@rem   Delegating alias: An established command that invokes one newer owner
@rem   and returns that owner's exit status unchanged.
@rem   Propagation probe: A no-engine synthetic child failure used only to prove
@rem   that this wrapper cannot turn a nested failure into a pass.
@rem   Validation gate: Repository script that proves a class of changes before
@rem   commit or PR.
@rem   Visual-fidelity gate: The immutable, frame-exact 200-box replay oracle.
@rem
@rem Invariants:
@rem   - There is exactly one replay presentation oracle.
@rem   - The delegated command's nonzero exit code is returned unchanged.
@rem   - This wrapper never launches an additional replay process of its own.
@rem   - --prove-failure-propagation returns 37 and never calls the real gate.
@rem
@rem Related:
@rem   - AGENTS.md
@rem   - Agentic/Reference/comment-style-guide.md
@rem   - tools/validate_replay_visual_fidelity.bat
@rem
@echo off
setlocal

echo.
echo ========================================
echo   VALIDATE_REPLAY_SCRUB - authoritative alias
echo ========================================
echo.

if /I "%~1"=="--prove-failure-propagation" (
    cmd /d /c exit 37
) else (
    call "%~dp0validate_replay_visual_fidelity.bat"
)
set "DELEGATE_EXIT=%ERRORLEVEL%"
if not "%DELEGATE_EXIT%"=="0" (
    echo FAIL: delegated replay gate failed with exit code %DELEGATE_EXIT%.
    exit /b %DELEGATE_EXIT%
)

echo.
echo ========================================
echo   VALIDATE_REPLAY_SCRUB: ALL PASSED
echo ========================================
exit /b 0
