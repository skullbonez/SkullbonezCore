@echo off
REM File: tools\bake_hulls.bat
REM Purpose: Bake or check serialized convex hull runtime data.
REM
REM Summary:
REM   This wrapper resolves the repo-local Python runtime and delegates all hull
REM   parsing and serialization to bake_hulls.py.
REM
REM Glossary:
REM   Hull bake: Conversion from source hull vertices/faces to runtime topology,
REM   mass, inertia, and metadata fields.
REM
REM Invariants:
REM   - The batch wrapper does not edit hull files itself.
REM   - The Python script exit code is propagated unchanged.
REM
REM Related:
REM   - tools/bake_hulls.py
REM   - AGENTS.md
setlocal

set "ROOT=%~dp0.."
call "%~dp0find_python.bat"
if errorlevel 1 exit /b %errorlevel%

"%PYTHON_EXE%" "%~dp0bake_hulls.py" --repo "%ROOT%" %*
exit /b %errorlevel%
