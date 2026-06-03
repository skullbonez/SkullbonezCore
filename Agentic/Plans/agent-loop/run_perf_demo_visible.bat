@echo off
setlocal
REM ===============================================================
REM  run_perf_demo_visible.bat - Launch the demo loop in a visible cmd.
REM  The cmd window closes automatically when the run completes.
REM  Pass --wait when another process should wait for the window to finish.
REM ===============================================================

set "REPO=%~dp0..\..\.."

echo Launching visible SkullbonezCore perf demo window...

if /I "%~1"=="--wait" (
    start /wait "SkullbonezCore Perf Demo" /D "%REPO%" "%ComSpec%" /c "Agentic\Plans\agent-loop\validate_perf_demo.bat"
) else (
    start "SkullbonezCore Perf Demo" /D "%REPO%" "%ComSpec%" /c "Agentic\Plans\agent-loop\validate_perf_demo.bat"
)
exit /b %ERRORLEVEL%
