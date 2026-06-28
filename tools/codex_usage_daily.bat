@rem
@rem File: tools/codex_usage_daily.bat
@rem Purpose:
@rem   Print local Codex token telemetry by day with a GPT-5.5-style cost estimate.
@rem
@rem Mental model:
@rem   Codex session files are diagnostic telemetry, not an authoritative billing
@rem   source. The script uses ripgrep when available to pull only token-count
@rem   rows, then parses the few numeric fields needed for the compact report.
@rem
@rem Invariants:
@rem   - Output stays compact: Date, Input, Output, Cached, Input-Fresh, Cost.
@rem   - The cost estimate uses GPT-5.5 default token rates:
@rem     uncached input $5.00/M, cached input $0.50/M, output $30.00/M.
@rem
@rem Related:
@rem   - %USERPROFILE%\.codex\sessions
@rem
@rem
@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -Command "$bat = Get-Content -Raw -LiteralPath '%~f0'; $ps = $bat -replace '(?s)^.*?# POWERSHELL\r?\n', ''; Invoke-Expression $ps"
exit /b %ERRORLEVEL%

# POWERSHELL
$ErrorActionPreference = 'Stop'

$sessionRoot = Join-Path $env:USERPROFILE '.codex\sessions'
if (-not (Test-Path -LiteralPath $sessionRoot)) {
    Write-Error "Codex session directory not found: $sessionRoot"
    exit 1
}

$inputRatePerMillion = 5.00
$cachedRatePerMillion = 0.50
$outputRatePerMillion = 30.00
$byDate = @{}

function Get-MatchValue {
    param(
        [string]$Line,
        [string]$Pattern
    )

    $match = [regex]::Match($Line, $Pattern)
    if (-not $match.Success) {
        return $null
    }
    return $match.Groups[1].Value
}

function Add-UsageLine {
    param([string]$Line)

    $timestamp = Get-MatchValue -Line $Line -Pattern '"timestamp"\s*:\s*"([^"]+)"'
    $usageStart = $Line.IndexOf('"last_token_usage"', [System.StringComparison]::Ordinal)
    if ($usageStart -lt 0) {
        return
    }

    $usageLine = $Line.Substring($usageStart)
    $inputText = Get-MatchValue -Line $usageLine -Pattern '"input_tokens"\s*:\s*(\d+)'
    $cachedText = Get-MatchValue -Line $usageLine -Pattern '"cached_input_tokens"\s*:\s*(\d+)'
    $outputText = Get-MatchValue -Line $usageLine -Pattern '"output_tokens"\s*:\s*(\d+)'

    if (-not $timestamp -or -not $inputText -or -not $cachedText -or -not $outputText) {
        return
    }

    try {
        $date = [DateTimeOffset]::Parse($timestamp).ToLocalTime().ToString('yyyy-MM-dd')
    } catch {
        return
    }

    if (-not $byDate.ContainsKey($date)) {
        $byDate[$date] = [pscustomobject]@{
            Date = $date
            Input = [int64]0
            Output = [int64]0
            Cached = [int64]0
            Uncached = [int64]0
            Cost = [double]0.0
        }
    }

    $inputTokens = [int64]$inputText
    $cachedTokens = [int64]$cachedText
    $outputTokens = [int64]$outputText
    $uncachedTokens = [Math]::Max([int64]0, $inputTokens - $cachedTokens)
    $cost =
        ($uncachedTokens / 1000000.0 * $inputRatePerMillion) +
        ($cachedTokens / 1000000.0 * $cachedRatePerMillion) +
        ($outputTokens / 1000000.0 * $outputRatePerMillion)

    $row = $byDate[$date]
    $row.Input += $inputTokens
    $row.Output += $outputTokens
    $row.Cached += $cachedTokens
    $row.Uncached += $uncachedTokens
    $row.Cost += $cost
}

function Add-UsageCsv {
    param([string]$Line)

    $parts = $Line.Split(',', 4)
    if ($parts.Count -ne 4) {
        return
    }

    try {
        $date = [DateTimeOffset]::Parse($parts[0]).ToLocalTime().ToString('yyyy-MM-dd')
        $inputTokens = [int64]$parts[1]
        $cachedTokens = [int64]$parts[2]
        $outputTokens = [int64]$parts[3]
    } catch {
        return
    }

    if (-not $byDate.ContainsKey($date)) {
        $byDate[$date] = [pscustomobject]@{
            Date = $date
            Input = [int64]0
            Output = [int64]0
            Cached = [int64]0
            Uncached = [int64]0
            Cost = [double]0.0
        }
    }

    $uncachedTokens = [Math]::Max([int64]0, $inputTokens - $cachedTokens)
    $cost =
        ($uncachedTokens / 1000000.0 * $inputRatePerMillion) +
        ($cachedTokens / 1000000.0 * $cachedRatePerMillion) +
        ($outputTokens / 1000000.0 * $outputRatePerMillion)

    $row = $byDate[$date]
    $row.Input += $inputTokens
    $row.Output += $outputTokens
    $row.Cached += $cachedTokens
    $row.Uncached += $uncachedTokens
    $row.Cost += $cost
}

$rg = Get-Command rg -ErrorAction SilentlyContinue
if ($null -ne $rg) {
    $pattern = '\x22timestamp\x22:\x22([^\x22]+)\x22.*\x22last_token_usage\x22:[{]\x22input_tokens\x22:([0-9]+),\x22cached_input_tokens\x22:([0-9]+),\x22output_tokens\x22:([0-9]+)'
    & $rg.Source --no-filename --only-matching --replace '$1,$2,$3,$4' --glob '*.jsonl' $pattern $sessionRoot 2>$null |
        ForEach-Object { Add-UsageCsv -Line $_ }
} else {
    Get-ChildItem -LiteralPath $sessionRoot -Recurse -File -Filter '*.jsonl' | ForEach-Object {
        try {
            $stream = [System.IO.File]::Open(
                $_.FullName,
                [System.IO.FileMode]::Open,
                [System.IO.FileAccess]::Read,
                [System.IO.FileShare]::ReadWrite)
            $reader = [System.IO.StreamReader]::new($stream)
        } catch {
            continue
        }

        try {
            while ($null -ne ($line = $reader.ReadLine())) {
                if ($line.IndexOf('"last_token_usage"', [System.StringComparison]::Ordinal) -ge 0) {
                    Add-UsageLine -Line $line
                }
            }
        } finally {
            if ($null -ne $reader) {
                $reader.Dispose()
            } elseif ($null -ne $stream) {
                $stream.Dispose()
            }
        }
    }
}

$rows = $byDate.Values | Sort-Object Date -Descending
$totalInput = [int64]0
$totalOutput = [int64]0
$totalCached = [int64]0
$totalUncached = [int64]0
$totalCost = [double]0.0

"{0,-12} {1,16} {2,12} {3,16} {4,16} {5,12}" -f 'Date', 'Input', 'Output', 'Cached', 'Input-Fresh', 'Cost'
"{0,-12} {1,16} {2,12} {3,16} {4,16} {5,12}" -f '----', '-----', '------', '------', '-----------', '----'
foreach ($row in $rows) {
    $totalInput += $row.Input
    $totalOutput += $row.Output
    $totalCached += $row.Cached
    $totalUncached += $row.Uncached
    $totalCost += $row.Cost

    "{0,-12} {1,16:N0} {2,12:N0} {3,16:N0} {4,16:N0} {5,12}" -f `
        $row.Date,
        $row.Input,
        $row.Output,
        $row.Cached,
        $row.Uncached,
        ('$' + $row.Cost.ToString('N2'))
}
"{0,-12} {1,16} {2,12} {3,16} {4,16} {5,12}" -f '----', '-----', '------', '------', '-----------', '----'
"{0,-12} {1,16:N0} {2,12:N0} {3,16:N0} {4,16:N0} {5,12}" -f `
    'TOTAL',
    $totalInput,
    $totalOutput,
    $totalCached,
    $totalUncached,
    ('$' + $totalCost.ToString('N2'))

