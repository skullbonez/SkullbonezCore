@rem
@rem File: Agentic/Skills/orchestrator/scripts/work_ledger.bat
@rem Purpose:
@rem   Provide the batch entrypoint for the orchestrator's live work ledger.
@rem
@rem Summary:
@rem   Every call delegates to the deterministic PowerShell ledger owner, then
@rem   returns its exit code so a missing token snapshot or malformed transition
@rem   stops orchestration instead of silently weakening the evidence.
@rem
@rem Related:
@rem   - Agentic/Skills/orchestrator/scripts/work_ledger.ps1
@rem   - Agentic/Skills/orchestrator/SKILL.md
@rem
@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0work_ledger.ps1" %*
exit /b %ERRORLEVEL%
