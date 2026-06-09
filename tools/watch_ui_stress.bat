@echo off
setlocal enabledelayedexpansion
REM ===============================================================
REM  watch_ui_stress.bat - Repeated stress crash watcher.
REM  Defaults to a finite run so it does not look like a stuck build.
REM ===============================================================

set "SCRIPT_DIR=%~dp0"
set "REPO=%SCRIPT_DIR%.."
set "ITERATIONS=25"
set "SLEEP_SECONDS=1"
set "FOREVER=0"
set "TEST_KIND=ui"
set "VALIDATE_SCRIPT=validate_ui_stress.bat"
set "PASS_MARKER=VALIDATE_UI_STRESS: ALL PASSED"
set "WATCH_TITLE=UI STRESS WATCH"
set "OUT_PREFIX=ui_stress_watch"
set "LOG_STEM=validate_ui_stress"

:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="--help" goto usage
if /I "%~1"=="-h" goto usage
if /I "%~1"=="--demo" (
    set "TEST_KIND=demo"
    shift
    goto parse_args
)
if /I "%~1"=="--test" (
    if "%~2"=="" goto bad_args
    set "TEST_KIND=%~2"
    shift
    shift
    goto parse_args
)
if /I "%~1"=="--forever" (
    set "FOREVER=1"
    shift
    goto parse_args
)
if /I "%~1"=="-f" (
    set "FOREVER=1"
    shift
    goto parse_args
)
if /I "%~1"=="--iterations" (
    if "%~2"=="" goto bad_args
    set "ITERATIONS=%~2"
    shift
    shift
    goto parse_args
)
if /I "%~1"=="-n" (
    if "%~2"=="" goto bad_args
    set "ITERATIONS=%~2"
    shift
    shift
    goto parse_args
)
if /I "%~1"=="--sleep" (
    if "%~2"=="" goto bad_args
    set "SLEEP_SECONDS=%~2"
    shift
    shift
    goto parse_args
)
if /I "%~1"=="-s" (
    if "%~2"=="" goto bad_args
    set "SLEEP_SECONDS=%~2"
    shift
    shift
    goto parse_args
)
goto bad_args

:args_done
if /I "%TEST_KIND%"=="ui" (
    set "VALIDATE_SCRIPT=validate_ui_stress.bat"
    set "PASS_MARKER=VALIDATE_UI_STRESS: ALL PASSED"
    set "WATCH_TITLE=UI STRESS WATCH"
    set "OUT_PREFIX=ui_stress_watch"
    set "LOG_STEM=validate_ui_stress"
) else if /I "%TEST_KIND%"=="demo" (
    set "VALIDATE_SCRIPT=validate_demo_stress.bat"
    set "PASS_MARKER=VALIDATE_DEMO_STRESS: ALL PASSED"
    set "WATCH_TITLE=DEMO STRESS WATCH"
    set "OUT_PREFIX=demo_stress_watch"
    set "LOG_STEM=validate_demo_stress"
) else (
    echo ERROR: --test must be ui or demo.
    exit /b 64
)

if "%FOREVER%"=="0" (
    powershell -NoProfile -Command "$v=0; if ([int]::TryParse($env:ITERATIONS, [ref]$v) -and $v -gt 0) { exit 0 }; exit 1"
    if errorlevel 1 (
        echo ERROR: --iterations must be a positive integer.
        exit /b 64
    )
)
powershell -NoProfile -Command "$v=0; if ([int]::TryParse($env:SLEEP_SECONDS, [ref]$v) -and $v -ge 0) { exit 0 }; exit 1"
if errorlevel 1 (
    echo ERROR: --sleep must be a non-negative integer.
    exit /b 64
)

pushd "%REPO%"
for /f %%T in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set "STAMP=%%T"
set "OUT_DIR=%REPO%\TestOutput\%OUT_PREFIX%_%STAMP%"
set "SUMMARY=%OUT_DIR%\watch_summary.log"
mkdir "%OUT_DIR%" >nul 2>&1

echo.
echo ========================================
echo   %WATCH_TITLE%
echo ========================================
echo Output: %OUT_DIR%
echo Test: %TEST_KIND%
if "%FOREVER%"=="1" (
    echo Mode: forever, explicit --forever requested.
) else (
    echo Mode: %ITERATIONS% iterations.
)
echo.

set /a ITER=1

:loop
if "%FOREVER%"=="0" if !ITER! GTR %ITERATIONS% goto pass

set "PAD=00000!ITER!"
set "PAD=!PAD:~-5!"
set "LOG=%OUT_DIR%\!PAD!_%LOG_STEM%.log"
for /f "delims=" %%T in ('powershell -NoProfile -Command "Get-Date -Format o"') do set "NOW=%%T"
for /f %%S in ('powershell -NoProfile -Command "[DateTimeOffset]::Now.ToUnixTimeSeconds()"') do set "START_SECONDS=%%S"
echo !NOW! ITER !ITER! START log=!LOG!
>> "%SUMMARY%" echo !NOW! ITER !ITER! START log=!LOG!

call "%SCRIPT_DIR%%VALIDATE_SCRIPT%" > "!LOG!" 2>&1
set "EXITCODE=!ERRORLEVEL!"

findstr /C:"%PASS_MARKER%" "!LOG!" >nul 2>&1
if errorlevel 1 (
    set "PASSMARKER=False"
) else (
    set "PASSMARKER=True"
)
if /I "!PASSMARKER!"=="False" if "!EXITCODE!"=="0" set "EXITCODE=10"

for /f %%B in ('powershell -NoProfile -Command "(Get-Item -LiteralPath $env:LOG).Length"') do set "BYTES=%%B"
for /f %%S in ('powershell -NoProfile -Command "[DateTimeOffset]::Now.ToUnixTimeSeconds()"') do set "END_SECONDS=%%S"
set /a SECONDS=END_SECONDS-START_SECONDS
for /f "delims=" %%T in ('powershell -NoProfile -Command "Get-Date -Format o"') do set "NOW=%%T"

echo !NOW! ITER !ITER! END exit=!EXITCODE! passMarker=!PASSMARKER! seconds=!SECONDS! bytes=!BYTES!
>> "%SUMMARY%" echo !NOW! ITER !ITER! END exit=!EXITCODE! passMarker=!PASSMARKER! seconds=!SECONDS! bytes=!BYTES!

if not "!EXITCODE!"=="0" goto fail

if %SLEEP_SECONDS% GTR 0 timeout /t %SLEEP_SECONDS% /nobreak >nul
set /a ITER+=1
goto loop

:pass
echo.
echo ========================================
echo   %WATCH_TITLE%: ALL PASSED
echo ========================================
echo Summary: %SUMMARY%
popd
exit /b 0

:fail
echo.
echo ========================================
echo   %WATCH_TITLE%: FAILED
echo ========================================
echo Last log: !LOG!
echo Summary: %SUMMARY%
echo.
type "!LOG!"
popd
exit /b !EXITCODE!

:usage
echo Usage: tools\watch_ui_stress.bat [--test ui^|demo] [--iterations N] [--sleep N] [--forever]
echo.
echo Defaults to --test ui --iterations 25 --sleep 1.
echo Use --forever only for an intentional soak run.
exit /b 0

:bad_args
echo ERROR: Invalid arguments.
echo.
echo Usage: tools\watch_ui_stress.bat [--test ui^|demo] [--iterations N] [--sleep N] [--forever]
echo.
echo Defaults to --test ui --iterations 25 --sleep 1.
echo Use --forever only for an intentional soak run.
exit /b 64
