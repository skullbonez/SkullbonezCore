@echo off
setlocal
pushd "%~dp0.."
Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData/scenes/split_future.scene.json --interactive on --replay on
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
