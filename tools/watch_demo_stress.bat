@echo off
call "%~dp0watch_ui_stress.bat" --test demo %*
exit /b %ERRORLEVEL%
