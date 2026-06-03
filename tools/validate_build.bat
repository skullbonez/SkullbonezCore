@echo off
setlocal enabledelayedexpansion
REM ===============================================================
REM  validate_build.bat - Build SkullbonezCore solution.
REM  Usage: validate_build.bat [Configuration]
REM    Configuration = Debug | Release | Profile (default: Profile)
REM  Exit 0 = build succeeded, Exit 1 = build failed.
REM ===============================================================

set "REPO=%~dp0.."
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Profile"

call "%~dp0find_msbuild.bat"
if errorlevel 1 exit /b 99

set "TOOLSET_ARG="
set "FALLBACK_TOOLSET=v143"

if not "%SKULLBONEZ_PLATFORM_TOOLSET%"=="" (
    call :find_toolset "%SKULLBONEZ_PLATFORM_TOOLSET%"
    if errorlevel 1 (
        echo ERROR: Requested platform toolset %SKULLBONEZ_PLATFORM_TOOLSET% not found.
        exit /b 99
    )
    set "TOOLSET_ARG=/p:PlatformToolset=%SKULLBONEZ_PLATFORM_TOOLSET%"
) else (
    findstr /C:"<PlatformToolset>v145</PlatformToolset>" "%REPO%\SKULLBONEZ_CORE.vcxproj" >nul
    if not errorlevel 1 (
        call :find_toolset v145
        if errorlevel 1 (
            call :find_toolset %FALLBACK_TOOLSET%
            if errorlevel 1 (
                echo ERROR: Platform toolset v145 not found, and fallback %FALLBACK_TOOLSET% is not installed.
                exit /b 99
            )
            set "TOOLSET_ARG=/p:PlatformToolset=%FALLBACK_TOOLSET%"
            echo INFO: Platform toolset v145 not found; falling back to VS2022 toolset %FALLBACK_TOOLSET%.
        )
    )
)

echo Building %CONFIG%^|x64 %TOOLSET_ARG%...
"%MSBUILD_EXE%" "%REPO%\SKULLBONEZ_CORE.sln" /p:Configuration=%CONFIG% /p:Platform=x64 %TOOLSET_ARG% /nologo /v:minimal /warnaserror
if errorlevel 1 (
    echo FAIL: Build %CONFIG% failed.
    exit /b 1
)

echo PASS: Build %CONFIG%^|x64 succeeded.
exit /b 0

:find_toolset
set "CHECK_TOOLSET=%~1"
for %%B in ("%MSBUILD_EXE%") do set "MSBUILD_BIN=%%~dpB"
set "VC_TARGETS_ROOT=%MSBUILD_BIN%..\..\Microsoft\VC"
for /d %%D in ("%VC_TARGETS_ROOT%\v*") do (
    if exist "%%~fD\Platforms\x64\PlatformToolsets\%CHECK_TOOLSET%\Toolset.props" exit /b 0
)
exit /b 1
