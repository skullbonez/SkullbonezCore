@echo off
REM ===============================================================
REM  find_git.bat - Locates Git and makes it available on PATH.
REM  Called by validation scripts that invoke Python helpers using git.
REM ===============================================================

for %%i in (git.exe) do (
    if not "%%~$PATH:i"=="" (
        "%%~$PATH:i" --version >nul 2>&1
        if not errorlevel 1 goto :found
    )
)

if exist "%ProgramFiles%\Git\cmd\git.exe" (
    set "PATH=%ProgramFiles%\Git\cmd;%PATH%"
    goto :found
)

if exist "%LOCALAPPDATA%\Programs\Git\cmd\git.exe" (
    set "PATH=%LOCALAPPDATA%\Programs\Git\cmd;%PATH%"
    goto :found
)

echo ERROR: Git not found. Install Git for Windows.
exit /b 99

:found
exit /b 0
