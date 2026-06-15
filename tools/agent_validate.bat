@rem
@rem File: tools/agent_validate.bat
@rem Purpose:
@rem   Documents and runs the agent_validate.bat developer/validation helper script.
@rem
@rem Mental model:
@rem   Tools are command-line guardrails around builds, validation, screenshots,
@rem   diagnostics, and artifact handling. They make the safe path repeatable and
@rem   keep output bounded for humans and agents.
@rem
@rem Glossary:
@rem   Validation gate: Repository script that proves a class of changes before
@rem   commit or PR.
@rem
@rem Invariants:
@rem   - Tool output should be bounded and readable because agents and humans use
@rem   it for decisions.
@rem
@rem Related:
@rem   - AGENTS.md
@rem   - Agentic/Reference/comment-style-guide.md
@rem
@rem
@echo off
REM ===============================================================
REM  agent_validate.bat - The one command an agent can run.
REM  Delegates to validate_full.bat.
REM
REM  Usage: tools\agent_validate.bat
REM  Exit 0 = all validation passed.
REM  Non-zero = failure; see output for details.
REM ===============================================================

call "%~dp0validate_full.bat"
exit /b %errorlevel%
