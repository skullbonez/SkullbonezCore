@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0"
if "%~1"=="" (
    call :print_help
    exit /b 1
)

set "FAILED=0"

for %%A in (%*) do (
    set "ARG=%%~A"
    set "KNOWN=1"
    if /I "!ARG!"=="fast" (
        call "%ROOT%validate_fast.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="renderers" (
        call "%ROOT%validate_renderers.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="concepts" (
        call "%ROOT%validate_concepts.bat" smoke
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="concept-smoke" (
        call "%ROOT%validate_concepts.bat" smoke
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="concept-core" (
        call "%ROOT%validate_concepts.bat" core
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="concept-full" (
        call "%ROOT%validate_concepts.bat" full
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="shaders" (
        call "%ROOT%validate_shaders.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="ui" (
        call "%ROOT%validate_ui.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="ui-stress" (
        call "%ROOT%validate_ui_stress.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="demo-stress" (
        call "%ROOT%validate_demo_stress.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="physics" (
        call "%ROOT%validate_physics.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="physics-query" (
        call "%ROOT%validate_physics_query.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="perf" (
        call "%ROOT%validate_perf.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="full" (
        call "%ROOT%validate_full.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="agent" (
        call "%ROOT%agent_validate.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="format" (
        call "%ROOT%validate_format.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="build-debug" (
        call "%ROOT%validate_build.bat" Debug
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="build-profile" (
        call "%ROOT%validate_build.bat" Profile
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="build-release" (
        call "%ROOT%validate_build.bat" Release
        if errorlevel 1 set "FAILED=1"
    ) else (
        set "KNOWN=0"
        echo Unknown validation target: !ARG!
        call :print_help
        set "FAILED=1"
    )
)

if "!FAILED!"=="1" (
    echo.
    echo VALIDATE_SELECT: one or more validations failed.
    exit /b 1
)

echo.
echo VALIDATE_SELECT: all requested validations passed.
exit /b 0

:print_help
echo.
echo Validate one or more targets from this workspace:
echo   tools\validate_select.bat fast
echo   tools\validate_select.bat renderers
echo   tools\validate_select.bat concepts
echo   tools\validate_select.bat concept-smoke
echo   tools\validate_select.bat concept-core
echo   tools\validate_select.bat concept-full
echo   tools\validate_select.bat shaders
echo   tools\validate_select.bat ui
echo   tools\validate_select.bat ui-stress
echo   tools\validate_select.bat demo-stress
echo   tools\validate_select.bat physics
echo   tools\validate_select.bat physics-query
echo   tools\validate_select.bat perf
echo   tools\validate_select.bat full
echo.
echo   tools\validate_select.bat format
echo   tools\validate_select.bat build-debug
echo   tools\validate_select.bat build-profile
echo   tools\validate_select.bat build-release
echo.
echo You can pass several targets in one command:
echo   tools\validate_select.bat format renderers physics
exit /b 0
