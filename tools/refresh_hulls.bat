@echo off
REM File: tools\refresh_hulls.bat
REM Purpose: Refresh every committed convex hull asset from source geometry.
REM
REM Mental model:
REM   This is the intentional write path for hull metadata refreshes. It runs the
REM   bake tool in write mode, then immediately checks the result.
REM
REM Glossary:
REM   Hull refresh: Rewrite of committed .hull runtime metadata from source
REM   vertex/face data.
REM
REM Invariants:
REM   - A refresh must be followed by a check so stale or nondeterministic hull
REM   output is caught before commit.
REM   - Physics validation is still required for PR-bound hull behavior changes.
REM
REM Related:
REM   - tools/bake_hulls.bat
REM   - AGENTS.md
setlocal

set "ROOT=%~dp0.."
call "%~dp0bake_hulls.bat" --write
if errorlevel 1 exit /b %errorlevel%

call "%~dp0bake_hulls.bat" --check
exit /b %errorlevel%
