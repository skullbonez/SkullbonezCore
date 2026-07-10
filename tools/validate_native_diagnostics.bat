@echo off
setlocal

REM Native diagnostic lane: MSVC AddressSanitizer plus bounded static analysis.
REM Use --prove-asan-fixture only for the explicit, self-cleaning UAF proof.
call "%~dp0find_python.bat"
if errorlevel 1 exit /b 99

"%PYTHON_EXE%" "%~dp0validate_native_diagnostics.py" %*
exit /b %errorlevel%
