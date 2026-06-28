@rem
@rem File: tools/codex_usage_daily.bat
@rem Purpose:
@rem   Print local Codex token telemetry by day with a GPT-5.5-style cost estimate.
@rem
@rem Mental model:
@rem   Codex session files are diagnostic telemetry, not an authoritative billing
@rem   source. This tool keeps the raw daily view repeatable while the accounting
@rem   meaning is being investigated.
@rem
@rem Invariants:
@rem   - Output stays compact: Date, Input, Output, Cached, Cost.
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
        if ($line.IndexOf('"last_token_usage"', [System.StringComparison]::Ordinal) -lt 0) {
            continue
        }

        try {
            $event = $line | ConvertFrom-Json -ErrorAction Stop
        } catch {
            continue
        }

        $usage = $event.payload.info.last_token_usage
        if ($null -eq $usage) {
            continue
        }

        try {
            $date = [DateTimeOffset]::Parse($event.timestamp).ToLocalTime().ToString('yyyy-MM-dd')
        } catch {
            continue
        }

        if (-not $byDate.ContainsKey($date)) {
            $byDate[$date] = [pscustomobject]@{
                Date = $date
                Input = [int64]0
                Output = [int64]0
                Cached = [int64]0
                Cost = [double]0.0
            }
        }

        $inputTokens = [int64]$usage.input_tokens
        $cachedTokens = [int64]$usage.cached_input_tokens
        $outputTokens = [int64]$usage.output_tokens
        $uncachedTokens = [Math]::Max([int64]0, $inputTokens - $cachedTokens)
        $cost =
            ($uncachedTokens / 1000000.0 * $inputRatePerMillion) +
            ($cachedTokens / 1000000.0 * $cachedRatePerMillion) +
            ($outputTokens / 1000000.0 * $outputRatePerMillion)

        $row = $byDate[$date]
        $row.Input += $inputTokens
        $row.Output += $outputTokens
        $row.Cached += $cachedTokens
        $row.Cost += $cost
    }
    } finally {
        if ($null -ne $reader) {
            $reader.Dispose()
        } elseif ($null -ne $stream) {
            $stream.Dispose()
        }
    }
}

$rows = $byDate.Values | Sort-Object Date -Descending

"{0,-12} {1,16} {2,12} {3,16} {4,12}" -f 'Date', 'Input', 'Output', 'Cached', 'Cost'
"{0,-12} {1,16} {2,12} {3,16} {4,12}" -f '----', '-----', '------', '------', '----'
foreach ($row in $rows) {
    "{0,-12} {1,16:N0} {2,12:N0} {3,16:N0} {4,12}" -f `
        $row.Date,
        $row.Input,
        $row.Output,
        $row.Cached,
        ('$' + $row.Cost.ToString('N2'))
}
