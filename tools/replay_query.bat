@echo off
REM File: tools\replay_query.bat
REM Purpose: Query chunked replay v2 artifacts through the repo Python helper.
REM
REM Mental model:
REM   This wrapper resolves the repo-local Python runtime and delegates bounded
REM   replay artifact queries to replay_query.py.
REM
REM Glossary:
REM   Replay v2 artifact: Chunked binary presentation .skreplay file.
REM   Bounded query: Small structured read of selected replay data.
REM
REM Invariants:
REM   - The wrapper does not parse replay bytes itself.
REM   - replay_query.py exit code is propagated unchanged.
REM
REM Related:
REM   - tools/replay_query.py
REM   - AGENTS.md
setlocal

set "SCRIPT_DIR=%~dp0"
call "%SCRIPT_DIR%find_python.bat"
if errorlevel 1 exit /b 1

"%PYTHON_EXE%" "%SCRIPT_DIR%replay_query.py" %*
exit /b %ERRORLEVEL%
