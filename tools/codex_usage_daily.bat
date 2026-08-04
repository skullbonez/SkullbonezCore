@rem
@rem File: tools/codex_usage_daily.bat
@rem Purpose:
@rem   Print local Codex token telemetry by day with a GPT-5.5-style cost estimate.
@rem
@rem Summary:
@rem   Codex records one JSON-lines event stream per session. This tool aggregates
@rem   token snapshots from those streams while treating damaged or live session
@rem   files as unavailable telemetry rather than a failure of the entire report.
@rem
@rem Invariants:
@rem   - Output stays compact: Date, Input, Output, Cached, Cost.
@rem   - The final Total row aggregates every reported daily row.
@rem   - Summary rankings use estimated cost; calendar weeks begin on Monday.
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
    $sessionFile = $_.FullName
    $stream = $null
    $reader = $null

    try {
        $stream = [System.IO.File]::Open(
            $sessionFile,
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read,
            [System.IO.FileShare]::ReadWrite)
        $reader = [System.IO.StreamReader]::new($stream)
    } catch {
        Write-Warning "Skipping unavailable Codex session file: $sessionFile ($($_.Exception.Message))"
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
    } catch [System.IO.IOException] {
        # Hazard: A session file can become unreadable after it was opened, such
        # as from storage corruption. Preserve the usable daily totals instead
        # of discarding them because one diagnostic stream cannot be read.
        Write-Warning "Skipping unreadable Codex session file: $sessionFile ($($_.Exception.Message))"
    } finally {
        if ($null -ne $reader) {
            $reader.Dispose()
        } elseif ($null -ne $stream) {
            $stream.Dispose()
        }
    }
}

$rows = $byDate.Values | Sort-Object Date -Descending
$byWeek = @{}
$total = [pscustomobject]@{
    Date = 'Total'
    Input = [int64]0
    Output = [int64]0
    Cached = [int64]0
    Cost = [double]0.0
}

"{0,-12} {1,16} {2,12} {3,16} {4,12}" -f 'Date', 'Input', 'Output', 'Cached', 'Cost'
"{0,-12} {1,16} {2,12} {3,16} {4,12}" -f '----', '-----', '------', '------', '----'
foreach ($row in $rows) {
    "{0,-12} {1,16:N0} {2,12:N0} {3,16:N0} {4,12}" -f `
        $row.Date,
        $row.Input,
        $row.Output,
        $row.Cached,
        ('$' + $row.Cost.ToString('N2'))

    $total.Input += $row.Input
    $total.Output += $row.Output
    $total.Cached += $row.Cached
    $total.Cost += $row.Cost

    $rowDate = [DateTime]::ParseExact(
        $row.Date,
        'yyyy-MM-dd',
        [System.Globalization.CultureInfo]::InvariantCulture)
    $daysSinceMonday = (([int]$rowDate.DayOfWeek + 6) % 7)
    $weekStartDate = $rowDate.AddDays(-$daysSinceMonday)
    $weekStart = $weekStartDate.ToString('yyyy-MM-dd')

    if (-not $byWeek.ContainsKey($weekStart)) {
        $byWeek[$weekStart] = [pscustomobject]@{
            Start = $weekStart
            End = $weekStartDate.AddDays(6).ToString('yyyy-MM-dd')
            Input = [int64]0
            Output = [int64]0
            Cached = [int64]0
            Cost = [double]0.0
        }
    }

    $week = $byWeek[$weekStart]
    $week.Input += $row.Input
    $week.Output += $row.Output
    $week.Cached += $row.Cached
    $week.Cost += $row.Cost
}

"{0,-12} {1,16} {2,12} {3,16} {4,12}" -f '', '', '', '', ''
"{0,-12} {1,16:N0} {2,12:N0} {3,16:N0} {4,12}" -f `
    $total.Date,
    $total.Input,
    $total.Output,
    $total.Cached,
    ('$' + $total.Cost.ToString('N2'))

$topDays = $rows | Sort-Object Cost -Descending | Select-Object -First 3
$largestWeek = $byWeek.Values | Sort-Object Cost -Descending | Select-Object -First 1

''
'Summary (ranked by estimated cost)'
'Top 3 days'
"{0,-12} {1,16} {2,12} {3,16} {4,12}" -f 'Date', 'Input', 'Output', 'Cached', 'Cost'
foreach ($row in $topDays) {
    "{0,-12} {1,16:N0} {2,12:N0} {3,16:N0} {4,12}" -f `
        $row.Date,
        $row.Input,
        $row.Output,
        $row.Cached,
        ('$' + $row.Cost.ToString('N2'))
}

'Largest calendar week (Monday-Sunday)'
"{0,-24} {1,16} {2,12} {3,16} {4,12}" -f 'Week', 'Input', 'Output', 'Cached', 'Cost'
"{0,-24} {1,16:N0} {2,12:N0} {3,16:N0} {4,12}" -f `
    ("$($largestWeek.Start) to $($largestWeek.End)"),
    $largestWeek.Input,
    $largestWeek.Output,
    $largestWeek.Cached,
    ('$' + $largestWeek.Cost.ToString('N2'))
