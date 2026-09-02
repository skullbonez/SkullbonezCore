@echo off
setlocal
python "%~dp0skarness.py" %*
exit /b %errorlevel%
