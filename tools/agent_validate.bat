@rem
@rem File: tools/agent_validate.bat
@rem Purpose:
@rem   Provides agents one explicit entry point for terminal plan validation.
@rem
@rem Mental model:
@rem   This is a stable alias, not another pipeline. Both this wrapper and
@rem   validate_full require --plan-completion so ordinary PR preparation cannot
@rem   accidentally launch the terminal gate.
@rem
@rem Glossary:
@rem   Plan-completion gate: Terminal repository proof run once after every task
@rem   in an implementation plan is complete and independently reviewed.
@rem
@rem Invariants:
@rem   - Reject calls that do not explicitly declare plan completion.
@rem   - Delegate exactly once and return validate_full's exit code unchanged.
@rem
@rem Related:
@rem   - AGENTS.md
@rem   - tools/validate_full.bat
@rem   - tools/validate_all_cpu_tests.bat
@rem
@rem
@echo off
REM ===============================================================
REM  agent_validate.bat - The terminal plan-completion gate for agents.
REM  Delegates exactly once to validate_full.bat, which owns mandatory CPU
REM  validation and the render+physics runtime lanes.
REM
REM  Usage: tools\agent_validate.bat --plan-completion
REM  Exit 0 = plan-completion validation passed.
REM  Non-zero = failure; see output for details.
REM ===============================================================

if /I not "%~1"=="--plan-completion" goto :usage_error
if not "%~2"=="" goto :usage_error

call "%~dp0validate_full.bat" --plan-completion
exit /b %errorlevel%

:usage_error
echo ERROR: agent_validate is reserved for completion of an entire plan.
echo Usage: tools\agent_validate.bat --plan-completion
echo For ordinary commit or PR validation, run the cumulative focused gates
echo mapped in AGENTS.md.
exit /b 64
