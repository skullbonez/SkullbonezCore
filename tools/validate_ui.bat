@echo off
setlocal
REM The UI gate drives the current dock, drawer and floating windows through
REM Skarness. The retired floating-Tools screenshot rectangles cannot describe
REM this layout; native state assertions and captured pixels share its bounds.
set "REPO=%~dp0.."
pushd "%REPO%"
call "%~dp0validate_format.bat"
if errorlevel 1 exit /b 1
call "%~dp0find_python.bat"
if errorlevel 1 exit /b 99
call "%~dp0validate_build.bat" Profile
if errorlevel 1 exit /b 2
call "%~dp0validate_build.bat" Automation
if errorlevel 1 exit /b 2

REM Keep the low-level UI clipping, text and blur contracts as well as the
REM native interaction checks below. No screenshot baseline is regenerated.
call "%~dp0validate_tests.bat"
if errorlevel 1 exit /b 3
"%PYTHON_EXE%" "%~dp0validate_skarness_causal_playback.py" --session "%REPO%\TestOutput\skarness\ui-gate-causal-%RANDOM%"
if errorlevel 1 exit /b 3

for %%t in (scrubber_autohide header_autohide tools_diagnostics floating_diagnostics docked_navigation ui_themes ui_side_panels ui_panel_transitions unified_compact_tools_ui unified_editor_ui unified_options_keys_ui unified_physics_ui) do (
    call :native_case %%t
    if errorlevel 1 exit /b 4
)
call "%~dp0check_dx12_validation.bat"
if errorlevel 1 exit /b 5
call "%~dp0validate_ready_builds.bat"
if errorlevel 1 exit /b 6
echo PASS: current UI contracts, native controls, containment and captured appearance.
popd
exit /b 0

:native_case
echo Checking native %~1...
"%PYTHON_EXE%" "%~dp0validate_%~1.py" --session "%REPO%\TestOutput\skarness\ui-gate-%~1-%RANDOM%"
exit /b %ERRORLEVEL%
