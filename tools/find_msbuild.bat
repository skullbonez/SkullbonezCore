@echo off
REM ===============================================================
REM  find_msbuild.bat - Locates MSBuild via vswhere and sets MSBUILD_EXE.
REM  Called by other validate_*.bat scripts. Do not run directly.
REM ===============================================================

for /f "usebackq tokens=*" %%i in (`"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
    set "MSBUILD_EXE=%%i"
    goto :found
)

echo ERROR: MSBuild not found. Install Visual Studio with C++ workload.
exit /b 99

:found
exit /b 0
