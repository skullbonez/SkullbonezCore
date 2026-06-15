@rem
@rem File: tools/loc_count.bat
@rem Purpose:
@rem   Documents and runs the loc_count.bat developer/validation helper script.
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
setlocal
REM ===============================================================
REM  loc_count.bat - Count first-party source logical lines of code.
REM ===============================================================

set "REPO=%~dp0.."

call "%~dp0find_python.bat"
if errorlevel 1 exit /b %errorlevel%

"%PYTHON_EXE%" "%REPO%\Agentic\Skills\loc_count.py"
exit /b %errorlevel%
