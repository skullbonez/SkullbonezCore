@echo off
setlocal

set "REPO=%~dp0.."
set "OUTPUT=%REPO%\TestOutput\validation\persistent_island_sleep"
set "EXE=%REPO%\Debug\SKULLBONEZ_CORE.exe"
pushd "%REPO%"

call "%~dp0find_python.bat"
if errorlevel 1 goto :tool_fail
call "%~dp0find_msbuild.bat"
if errorlevel 1 goto :tool_fail

if not exist "%OUTPUT%" mkdir "%OUTPUT%"

echo [1/3] Building Debug x64 runtime...
"%MSBUILD_EXE%" "%REPO%\SKULLBONEZ_CORE.vcxproj" /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal /warnaserror
if errorlevel 1 goto :fail

echo [2/3] Proving analyzer negative controls...
"%PYTHON_EXE%" tools\check_persistent_island_sleep_regression.py --self-test
if errorlevel 1 goto :fail

echo [3/3] Running corner, wall, and at-rest acceptance twice for workers 0, 1, and 4...
for %%W in (0 1 4) do (
    call :run_pair corner SkullbonezData\scenes\sleep_test_corner.scene.json 3600 %%W
    if errorlevel 1 goto :fail
    call :run_pair wall200 SkullbonezData\scenes\prediction_ragdoll_wall_200.scene.json 2000 %%W
    if errorlevel 1 goto :fail
    call :run_pair at_rest SkullbonezData\scenes\at_rest.scene.json 7200 %%W
    if errorlevel 1 goto :fail
)

for %%C in (corner wall200 at_rest) do (
    fc /b "%OUTPUT%\%%C_workers_0_a.csv" "%OUTPUT%\%%C_workers_1_a.csv" >nul
    if errorlevel 1 (
        echo FAIL: %%C workers=0 and workers=1 outputs differ.
        goto :fail
    )
    fc /b "%OUTPUT%\%%C_workers_0_a.csv" "%OUTPUT%\%%C_workers_4_a.csv" >nul
    if errorlevel 1 (
        echo FAIL: %%C workers=0 and workers=4 outputs differ.
        goto :fail
    )
)

echo PASS: persistent-island sleep outputs are complete, accepted, and byte-identical.
popd
endlocal & exit /b 0

:fail
popd
endlocal & exit /b 1

:tool_fail
popd
endlocal & exit /b 99

:run_pair
set "CASE=%~1"
set "SCENE=%~2"
set "FRAMES=%~3"
set "WORKERS=%~4"

call :run_once %CASE% %SCENE% %FRAMES% %WORKERS% a
if errorlevel 1 exit /b 1
call :run_once %CASE% %SCENE% %FRAMES% %WORKERS% b
if errorlevel 1 exit /b 1

fc /b "%OUTPUT%\%CASE%_workers_%WORKERS%_a.csv" "%OUTPUT%\%CASE%_workers_%WORKERS%_b.csv" >nul
if errorlevel 1 (
    echo FAIL: %CASE% workers=%WORKERS% clean-process outputs differ.
    exit /b 1
)
exit /b 0

:run_once
set "CASE=%~1"
set "SCENE=%~2"
set "FRAMES=%~3"
set "WORKERS=%~4"
set "REPEAT=%~5"
set "CSV=%OUTPUT%\%CASE%_workers_%WORKERS%_%REPEAT%.csv"
set "LOG=%OUTPUT%\%CASE%_workers_%WORKERS%_%REPEAT%.log"
set "JSON=%OUTPUT%\%CASE%_workers_%WORKERS%_%REPEAT%.json"

if exist "%CSV%" del /q "%CSV%"
if exist "%LOG%" del /q "%LOG%"
if exist "%JSON%" del /q "%JSON%"

echo   %CASE% workers=%WORKERS% repeat=%REPEAT%
"%EXE%" --renderer dx12 --vsync off --fixed-step --shadows off --automation-hidden-window --workers %WORKERS% --frames %FRAMES% --scene %SCENE% --physics-regression-log "%CSV%" > "%LOG%" 2>&1
if errorlevel 1 (
    echo FAIL: runtime launch failed. See %LOG%
    exit /b 1
)
if not exist "%CSV%" (
    echo FAIL: runtime did not freshly create %CSV%
    exit /b 1
)

"%PYTHON_EXE%" tools\check_persistent_island_sleep_regression.py --workload %CASE% --input "%CSV%" --json-out "%JSON%"
if errorlevel 1 exit /b 1
exit /b 0
