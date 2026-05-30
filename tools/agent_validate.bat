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
