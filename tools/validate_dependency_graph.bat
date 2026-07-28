@echo off
REM File: tools/validate_dependency_graph.bat
REM Purpose: Run data-driven direction, proof-freshness, and ownership checks.
REM Invariant: The checker runs its fixtures before the proof and repository scan.
setlocal
cd /d "%~dp0\.."
python tools\check_dependency_graph.py --repo .
if errorlevel 1 exit /b %errorlevel%
exit /b 0
