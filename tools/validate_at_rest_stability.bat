@echo off
setlocal
python "%~dp0check_at_rest_stability_analyzer.py"
exit /b %ERRORLEVEL%
