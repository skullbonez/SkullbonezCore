@rem
@rem File: tools/update_baselines.bat
@rem Purpose:
@rem   Documents and runs the update_baselines.bat developer/validation helper script.
@rem
@rem Mental model:
@rem   Tools are command-line guardrails around builds, validation, screenshots,
@rem   diagnostics, and artifact handling. They make the safe path repeatable and
@rem   keep output bounded for humans and agents.
@rem
@rem Glossary:
@rem   JSON (JavaScript Object Notation): Structured text format used by
@rem   diagnostics, baselines, and tool reports.
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
REM Update TestOutput\baselines from current Profile artifacts.

if /I "%~1"=="--help" goto :help
if /I "%~1"=="/?" goto :help

set "REPO=%~dp0.."
call "%~dp0find_python.bat"
if errorlevel 1 exit /b 99

"%PYTHON_EXE%" "%~dp0update_baselines.py" --repo "%REPO%" %*
exit /b %ERRORLEVEL%

:help
echo Usage: tools\update_baselines.bat [--visuals] [--perf] [--require]
echo.
echo Updates TestOutput\baselines from current Profile artifacts.
echo With no flags, updates both visual PNG baselines and perf JSON baselines.
exit /b 0
