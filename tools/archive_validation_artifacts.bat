@rem
@rem File: tools/archive_validation_artifacts.bat
@rem Purpose:
@rem   Documents and runs the archive_validation_artifacts.bat developer/validation helper script.
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
@rem
@rem
@echo off
setlocal
REM Archive current Profile validation artifacts into TestOutput\NNN_commit.

if /I "%~1"=="--help" goto :help
if /I "%~1"=="/?" goto :help

set "REPO=%~dp0.."
call "%~dp0find_python.bat"
if errorlevel 1 exit /b 99
call "%~dp0find_git.bat"
if errorlevel 1 exit /b 99

"%PYTHON_EXE%" "%~dp0archive_validation_artifacts.py" --repo "%REPO%" %*
exit /b %ERRORLEVEL%

:help
echo Usage: tools\archive_validation_artifacts.bat [--visuals] [--perf] [--require] [--commit HASH]
echo.
echo Creates or reuses TestOutput\NNN_commit and archives current Profile artifacts.
exit /b 0
