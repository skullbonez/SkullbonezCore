@rem
@rem File: tools/agent_validate.bat
@rem Purpose:
@rem   Provides agents one default entry point for broad PR validation.
@rem
@rem Mental model:
@rem   This is a stable alias, not another pipeline. validate_full owns the CPU
@rem   umbrella and runtime ordering so this wrapper cannot duplicate a test.
@rem
@rem Glossary:
@rem   Validation gate: Repository script that proves a class of changes before
@rem   commit or PR.
@rem
@rem Invariants:
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
REM  agent_validate.bat - The default PR gate for agents.
REM  Delegates exactly once to validate_full.bat, which owns mandatory CPU
REM  validation and the render+physics runtime lanes.
REM
REM  Usage: tools\agent_validate.bat
REM  Exit 0 = default validation passed.
REM  Non-zero = failure; see output for details.
REM ===============================================================

call "%~dp0validate_full.bat"
exit /b %errorlevel%
