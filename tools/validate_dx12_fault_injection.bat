@rem
@rem File: tools/validate_dx12_fault_injection.bat
@rem Purpose:
@rem   Proves that the Debug DX12 backend fails closed before its first queue
@rem   submission and propagates that failure to a nonzero process exit.
@rem
@rem Summary:
@rem   The engine owns a Debug-only fault point immediately before
@rem   ExecuteCommandLists. This gate arms it for one bounded scene launch and
@rem   verifies process, diagnostic, submission-count, and InfoQueue evidence.
@rem
@rem Glossary:
@rem   Fail closed: Retain the first failure and issue no later GPU submission.
@rem   InfoQueue: DX12 debug-layer messages written to dx12_validation.txt.
@rem
@rem Invariants:
@rem   - The injected launch must exit nonzero without timing out.
@rem   - The probe report must show one injection and zero submissions.
@rem   - The bounded diagnostic and zero InfoQueue errors are required evidence.
@rem
@rem Related:
@rem   - tools/check_dx12_validation.bat
@rem
@echo off
setlocal enabledelayedexpansion

set "REPO=%~dp0.."
set "FAULT_TIMEOUT_SECONDS=45"
set "FAULT_REPORT=%REPO%\TestOutput\dx12_fault_injection.txt"
set "FAULT_STDOUT=%REPO%\Debug\dx12_fault_stdout.txt"
set "FAULT_STDERR=%REPO%\Debug\dx12_fault_stderr.txt"
pushd "%REPO%"

echo.
echo ========================================
echo   VALIDATE_DX12_FAULT_INJECTION
echo ========================================
echo.

echo [1/5] Building Debug x64...
call "%~dp0validate_build.bat" Debug
if errorlevel 1 (
    popd
    exit /b 1
)

echo [2/5] Cleaning old probe artifacts...
del /q "%FAULT_REPORT%" 2>nul
del /q "%FAULT_STDOUT%" 2>nul
del /q "%FAULT_STDERR%" 2>nul
del /q "%REPO%\dx12_validation.txt" 2>nul

echo [3/5] Injecting before the first DX12 submission...
set "SKULLBONEZ_DX12_FAULT=before-first-submit"
set "SKORE_FAULT_EXE=%REPO%\Debug\SKULLBONEZ_CORE.exe"
set "SKORE_FAULT_REPO=%REPO%"
set "SKORE_FAULT_STDOUT=%FAULT_STDOUT%"
set "SKORE_FAULT_STDERR=%FAULT_STDERR%"
set "SKORE_FAULT_TIMEOUT=%FAULT_TIMEOUT_SECONDS%"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; try { $psi=[System.Diagnostics.ProcessStartInfo]::new(); $psi.FileName=$env:SKORE_FAULT_EXE; $psi.Arguments='--renderer dx12 --vsync off --suite SkullbonezData/scenes/render_tests.suite.json'; $psi.WorkingDirectory=$env:SKORE_FAULT_REPO; $psi.UseShellExecute=$false; $psi.RedirectStandardOutput=$true; $psi.RedirectStandardError=$true; $p=[System.Diagnostics.Process]::new(); $p.StartInfo=$psi; [void]$p.Start(); $stdoutTask=$p.StandardOutput.ReadToEndAsync(); $stderrTask=$p.StandardError.ReadToEndAsync(); $waitMs=[Math]::Max(1,[int]$env:SKORE_FAULT_TIMEOUT)*1000; if(-not $p.WaitForExit($waitMs)){ Write-Host ('TIMEOUT: fault probe exceeded ' + $env:SKORE_FAULT_TIMEOUT + 's; killing PID ' + $p.Id); Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; $p.WaitForExit(5000) | Out-Null; exit 124 }; $p.WaitForExit(); [System.IO.File]::WriteAllText($env:SKORE_FAULT_STDOUT,$stdoutTask.GetAwaiter().GetResult()); [System.IO.File]::WriteAllText($env:SKORE_FAULT_STDERR,$stderrTask.GetAwaiter().GetResult()); exit $p.ExitCode } catch { Write-Host ('ERROR: fault probe launch failed: ' + $_.Exception.Message); exit 125 }"
set "FAULT_EXIT=%ERRORLEVEL%"
set "SKULLBONEZ_DX12_FAULT="
if "%FAULT_EXIT%"=="0" (
    echo FAIL: Injected DX12 launch returned success instead of propagating failure.
    popd
    exit /b 2
)
if "%FAULT_EXIT%"=="124" (
    echo FAIL: Injected DX12 launch timed out.
    popd
    exit /b 3
)
if "%FAULT_EXIT%"=="125" (
    echo FAIL: Injected DX12 launch wrapper failed.
    popd
    exit /b 4
)
echo PASS: Injected launch exited nonzero with code %FAULT_EXIT%.

echo [4/5] Checking retained failure evidence...
if not exist "%FAULT_REPORT%" (
    echo FAIL: Fault-injection report is missing: "%FAULT_REPORT%"
    popd
    exit /b 5
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$expected='point=before-first-submit injected=1 submissions=0 blocked_after_failure=0'; $actual=(Get-Content -LiteralPath $env:FAULT_REPORT -Raw).Trim(); if($actual -ne $expected){ exit 1 }"
if errorlevel 1 (
    echo FAIL: Fault report did not prove zero submissions.
    type "%FAULT_REPORT%"
    popd
    exit /b 6
)
findstr /C:"[dx12-fault] owner=Rendering/DX12FaultInjection" "%FAULT_STDERR%" >nul
if errorlevel 1 (
    echo FAIL: Bounded DX12 fault diagnostic is missing from stderr.
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "if(Test-Path -LiteralPath $env:SKORE_FAULT_STDERR){ Get-Content -LiteralPath $env:SKORE_FAULT_STDERR -Tail 40 }"
    popd
    exit /b 7
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$lines=@(Select-String -LiteralPath $env:FAULT_STDERR -SimpleMatch '[dx12-fault]'); $bytes=(Get-Item -LiteralPath $env:FAULT_STDERR).Length; if($lines.Count -ne 1 -or $bytes -gt 4096){ exit 1 }"
if errorlevel 1 (
    echo FAIL: Fault stderr was duplicated or exceeded the 4096-byte diagnostic bound.
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "if(Test-Path -LiteralPath $env:SKORE_FAULT_STDERR){ $item=Get-Item -LiteralPath $env:SKORE_FAULT_STDERR; Write-Host ('stderr_bytes=' + $item.Length); Get-Content -LiteralPath $env:SKORE_FAULT_STDERR -Tail 40 }"
    popd
    exit /b 8
)
echo PASS: First failure was retained before ExecuteCommandLists; submissions=0.

echo [5/5] Checking DX12 InfoQueue evidence...
call "%~dp0check_dx12_validation.bat"
if errorlevel 1 (
    popd
    exit /b 9
)

echo.
echo ========================================
echo   VALIDATE_DX12_FAULT_INJECTION: PASSED
echo ========================================
popd
exit /b 0
