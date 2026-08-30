param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("SelfTest", "Live")]
    [string]$Mode,

    [Parameter(Mandatory = $true)]
    [string]$Repo
)

$ErrorActionPreference = "Stop"
$repoPath = (Resolve-Path -LiteralPath $Repo).Path
$toolsPath = Join-Path $repoPath "tools"
$modeArguments = if ($Mode -eq "SelfTest") { @("--self-test") } else { @() }
$commands = @(
    [pscustomobject]@{
        Name = "source design"
        Arguments = @((Join-Path $toolsPath "check_source_design.py"), "--repo", $repoPath) + $modeArguments
    }
    [pscustomobject]@{
        Name = "build configuration consistency"
        Arguments = @((Join-Path $toolsPath "check_build_config_consistency.py")) +
            $(if ($Mode -eq "SelfTest") { @("--self-test") } else { @("--repo", $repoPath, "--format", "json") })
    }
    [pscustomobject]@{
        Name = "deterministic math policy"
        Arguments = @((Join-Path $toolsPath "check_determinism_math_policy.py")) +
            $(if ($Mode -eq "SelfTest") { @("--self-test") } else { @("--repo", $repoPath, "--format", "json") })
    }
)

function Write-SourceDesignStepSummary {
    param([string]$SummaryLine)

    if ($Mode -ne "Live" -or [string]::IsNullOrWhiteSpace($env:GITHUB_STEP_SUMMARY)) {
        return
    }

    $values = @{}
    foreach ($match in [regex]::Matches($SummaryLine, "(?<name>[a-z_]+)=(?<value>[^ ]+)")) {
        $values[$match.Groups["name"].Value] = $match.Groups["value"].Value
    }
    $required = @(
        "sources", "contexts", "tidy_processes", "query_processes", "jobs",
        "peak_workers", "context_seconds", "tidy_seconds", "query_seconds",
        "dead_code_seconds", "total_seconds", "findings", "infrastructure_errors"
    )
    $missing = @($required | Where-Object { -not $values.ContainsKey($_) })
    if ($missing.Count -ne 0) {
        throw "source-design summary is missing fields: $($missing -join ', ')"
    }

    @(
        "### Source-design validation"
        ""
        "| Metric | Value |"
        "|---|---:|"
        "| Sources | $($values.sources) |"
        "| Compile contexts | $($values.contexts) |"
        "| Tidy processes | $($values.tidy_processes) |"
        "| Query processes | $($values.query_processes) |"
        "| Configured workers | $($values.jobs) |"
        "| Observed peak workers | $($values.peak_workers) |"
        "| Context discovery (seconds) | $($values.context_seconds) |"
        "| Tidy aggregate (seconds) | $($values.tidy_seconds) |"
        "| Query aggregate (seconds) | $($values.query_seconds) |"
        "| Dead-code proof (seconds) | $($values.dead_code_seconds) |"
        "| Source-design phase (seconds) | $($values.total_seconds) |"
        "| Findings | $($values.findings) |"
        "| Infrastructure errors | $($values.infrastructure_errors) |"
        ""
    ) | Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY
}

$systemTemp = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
$outputRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $systemTemp ("skore-retained-policy-" + [guid]::NewGuid()))
)
if (-not $outputRoot.StartsWith($systemTemp, [System.StringComparison]::OrdinalIgnoreCase) -or
    (Split-Path -Leaf $outputRoot) -notlike "skore-retained-policy-*") {
    throw "refusing unsafe retained-policy output directory: $outputRoot"
}
New-Item -ItemType Directory -Path $outputRoot | Out-Null

$records = @()
try {
    for ($index = 0; $index -lt $commands.Count; ++$index) {
        $stdoutPath = Join-Path $outputRoot "$index.stdout.txt"
        $stderrPath = Join-Path $outputRoot "$index.stderr.txt"
        $process = Start-Process -FilePath "python" `
            -ArgumentList $commands[$index].Arguments `
            -RedirectStandardOutput $stdoutPath `
            -RedirectStandardError $stderrPath `
            -PassThru `
            -WindowStyle Hidden
        # Hazard: Windows PowerShell can lose ExitCode when a fast redirected
        # child exits before its process handle is materialized.
        $null = $process.Handle
        $records += [pscustomobject]@{
            Command = $commands[$index]
            Process = $process
            StdoutPath = $stdoutPath
            StderrPath = $stderrPath
        }
    }

    $failures = @()
    $sourceSummary = ""
    foreach ($record in $records) {
        $process = $record.Process
        $process.WaitForExit()
        $process.Refresh()
        $exitCode = $process.ExitCode
        $stdout = @(Get-Content -LiteralPath $record.StdoutPath -ErrorAction SilentlyContinue)
        $stderr = @(Get-Content -LiteralPath $record.StderrPath -ErrorAction SilentlyContinue)
        if ($record.Command.Name -eq "source design") {
            $sourceSummary = $stdout | Where-Object { $_ -match "^source_design_summary " } | Select-Object -Last 1
            foreach ($line in $stdout) {
                Write-Output $line
            }
            foreach ($line in $stderr) {
                [Console]::Error.WriteLine($line)
            }
        }
        elseif ($exitCode -ne 0) {
            foreach ($line in $stdout) {
                Write-Output $line
            }
            foreach ($line in $stderr) {
                [Console]::Error.WriteLine($line)
            }
        }

        if ($exitCode -ne 0) {
            $commandText = "python " + ($record.Command.Arguments -join " ")
            $failures += "$($record.Command.Name): $commandText (exit $exitCode)"
        }
    }

    if ([string]::IsNullOrWhiteSpace($sourceSummary)) {
        $failures += "source design: missing bounded summary line"
    }
    else {
        Write-SourceDesignStepSummary -SummaryLine $sourceSummary
        if ($Mode -eq "Live") {
            Write-Output "source_design_serial_diagnostic=python tools\check_source_design.py --repo . --jobs 1"
        }
    }

    if ($failures.Count -ne 0) {
        [Console]::Error.WriteLine("FAILED retained policy group: " + ($failures -join "; "))
        exit 8
    }
}
finally {
    # Lifetime: every redirected child handle has completed before this exact
    # per-run directory is removed, including when summary formatting fails.
    foreach ($record in $records) {
        if (-not $record.Process.HasExited) {
            $record.Process.WaitForExit()
        }
    }
    Remove-Item -LiteralPath $outputRoot -Recurse -Force -ErrorAction SilentlyContinue
}

exit 0
