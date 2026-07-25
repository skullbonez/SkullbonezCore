@echo off
REM File: tools/validate_dependency_graph.bat
REM Purpose: Run data-driven package-direction and project-ownership checks.
REM Invariant: Self-tests run first so a malformed rule cannot silently pass the repository.
setlocal
cd /d "%~dp0\.."
python tools\check_dependency_graph.py --self-test
if errorlevel 1 exit /b %errorlevel%
python tools\check_dependency_graph.py --repo .
if errorlevel 1 exit /b %errorlevel%
exit /b 0
