# File: tools/run_graphics_stress.ps1
# Purpose:
#   Launch the DX12 graphics stress executable and sample process memory while it runs.
#
# Summary:
#   The engine emits semantic stress and renderer-memory records to stdout. This
#   wrapper records coarse OS process counters from outside the process so a
#   hard crash or forced stop still leaves a time-series artifact.
#
# Glossary:
#   Private bytes: Committed process memory not shareable with other processes.
#   Working set: Resident memory currently backed by physical RAM.
#   PID-scoped stop: Cleanup that targets only the process launched by this runner.
#
# Invariants:
#   - Cleanup targets the launched PID only.
#   - CSV rows are append-only after the header so partial runs remain readable.
#   - A zero-minute run has no timeout and samples until the process exits or the runner is interrupted.
#
# Related:
#   - tools/run_graphics_stress.bat
#   - SkullbonezSource/Runtime/RunStress.cpp

param(
    [Parameter(Mandatory = $true)]
    [string]$Exe,

    [Parameter(Mandatory = $true)]
    [string]$Repo,

    [Parameter(Mandatory = $true)]
    [string]$ArgumentLine,

    [Parameter(Mandatory = $true)]
    [string]$Stdout,

    [Parameter(Mandatory = $true)]
    [string]$Stderr,

    [Parameter(Mandatory = $true)]
    [string]$MemoryCsv,

    [int]$Minutes = 0,

    [int]$SampleSeconds = 15
)

$ErrorActionPreference = "Stop"
$SampleSeconds = [Math]::Max(1, $SampleSeconds)

function Split-ArgumentLine {
    param([string]$Text)

    if ([string]::IsNullOrWhiteSpace($Text)) {
        return @()
    }

    # The batch file passes simple switch/value pairs without embedded spaces.
    # Keep splitting explicit here so future quoted paths are a deliberate change.
    return $Text -split " "
}

function Write-MemorySample {
    param(
        [int]$TargetProcessId,
        [System.Diagnostics.Stopwatch]$Stopwatch,
        [string]$Path,
        [string]$ExitCode
    )

    $process = Get-Process -Id $TargetProcessId -ErrorAction SilentlyContinue
    if ($null -eq $process) {
        $elapsed = [Math]::Round($Stopwatch.Elapsed.TotalSeconds, 3)
        $row = '"{0}",{1},{2},,,,,,,"{3}"' -f `
            (Get-Date).ToUniversalTime().ToString("o"),
            $elapsed,
            $TargetProcessId,
            $ExitCode
        Add-Content -LiteralPath $Path -Value $row -Encoding ASCII
        return
    }

    $process.Refresh()
    $elapsedSeconds = [Math]::Round($Stopwatch.Elapsed.TotalSeconds, 3)
    $row = '"{0}",{1},{2},{3},{4},{5},{6},{7},{8},"{9}"' -f `
        (Get-Date).ToUniversalTime().ToString("o"),
        $elapsedSeconds,
        $process.Id,
        $process.WorkingSet64,
        $process.PrivateMemorySize64,
        $process.VirtualMemorySize64,
        $process.PagedMemorySize64,
        $process.HandleCount,
        $process.Threads.Count,
        $ExitCode
    Add-Content -LiteralPath $Path -Value $row -Encoding ASCII
}

$argv = Split-ArgumentLine -Text $ArgumentLine
$header = "utc_iso,elapsed_seconds,pid,working_set_bytes,private_bytes,virtual_bytes,paged_memory_bytes,handle_count,thread_count,exit_code"
Set-Content -LiteralPath $MemoryCsv -Value $header -Encoding ASCII

$process = $null
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$timedOut = $false
$scriptExitCode = 0

try {
    $process = Start-Process `
        -FilePath $Exe `
        -ArgumentList $argv `
        -WorkingDirectory $Repo `
        -RedirectStandardOutput $Stdout `
        -RedirectStandardError $Stderr `
        -PassThru

    Write-Host ("[graphics-stress] PID " + $process.Id + " started.")
    Write-Host ("[graphics-stress] memory csv: " + $MemoryCsv)
    if ($Minutes -le 0) {
        Write-Host "[graphics-stress] Running until stopped."
    }
    else {
        Write-Host ("[graphics-stress] Running for " + $Minutes + " minute(s).")
    }

    while (-not $process.HasExited) {
        Write-MemorySample -TargetProcessId $process.Id -Stopwatch $stopwatch -Path $MemoryCsv -ExitCode ""

        if ($Minutes -gt 0 -and $stopwatch.Elapsed.TotalMinutes -ge $Minutes) {
            $timedOut = $true
            Write-Host ("[graphics-stress] Timeout reached; closing PID " + $process.Id)
            $live = Get-Process -Id $process.Id -ErrorAction SilentlyContinue
            if ($null -ne $live) {
                [void]$live.CloseMainWindow()
                if (-not $live.WaitForExit(10000)) {
                    Write-Host ("[graphics-stress] Graceful close timed out; stopping PID " + $process.Id)
                    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
                    [void]$live.WaitForExit(5000)
                }
            }
            break
        }

        Start-Sleep -Seconds $SampleSeconds
        $process.Refresh()
    }

    $process.WaitForExit()
    $process.Refresh()
    $scriptExitCode = if ($timedOut) { 124 } else { $process.ExitCode }
    Write-MemorySample -TargetProcessId $process.Id -Stopwatch $stopwatch -Path $MemoryCsv -ExitCode $scriptExitCode
}
finally {
    if ($null -ne $process) {
        $live = Get-Process -Id $process.Id -ErrorAction SilentlyContinue
        if ($null -ne $live -and -not $live.HasExited) {
            Write-Host ("[graphics-stress] Runner exiting; stopping PID " + $process.Id)
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
    }
}

exit $scriptExitCode
