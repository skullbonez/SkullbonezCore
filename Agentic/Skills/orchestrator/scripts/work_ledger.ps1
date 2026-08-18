<#
File: Agentic/Skills/orchestrator/scripts/work_ledger.ps1
Purpose:
  Persist exact orchestrator timing, token, review, validation, and commit
  evidence beside the authoritative MASTER-PLAN.

Summary:
  A goal owns ordered task records, and each task owns a contiguous sequence of
  steps. Every command captures the parent Codex session counter and atomically
  rewrites the Markdown ledger. Rubber-duck steps may add a second session
  counter, keeping reviewer usage distinct while preserving an exact combined
  total for the step, task, and goal.

Glossary:
  Token snapshot: Monotonic cumulative counters from the latest token_count
    event in one Codex JSON-lines session stream.
  Transition: One ledger call that closes the active step and opens its
    successor at the same timestamp and parent-session token snapshot.

Invariants:
  - One unfinished goal, task, and step may exist in a ledger at a time.
  - Parent-session step boundaries are contiguous, so their deltas neither gap
    nor overlap; reviewer-session deltas are added exactly once.
  - The Markdown and its embedded state are replaced together under a named
    mutex, so a reader never observes a partially written ledger.
  - Completed task commits resolve to full hashes and are verified as pushed
    to the configured upstream unless self-test explicitly disables that check.

Related:
  - Agentic/Skills/orchestrator/scripts/work_ledger.bat is the public entrypoint.
  - Agentic/Skills/orchestrator/SKILL.md owns the call sequence.
  - tools/codex_usage_daily.bat documents the Codex session event source.
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('start-goal', 'start-task', 'attach-worker', 'transition', 'finish-task', 'finish-goal', 'show', 'self-test')]
    [string]$Action = 'show',

    [string]$MasterPlanPath = 'Agentic/Plans/MASTER-PLAN.md',
    [string]$LedgerPath,
    [string]$Goal = 'MASTER-PLAN',
    [string]$Task,
    [string]$Title,
    [string]$Step,
    [string]$Kind,
    [string]$Label,
    [string]$Outcome,
    [int]$Findings = -1,
    [string]$Commit,
    [string]$MainThreadId,
    [string]$MainSessionFile,
    [string]$WorkerThreadId,
    [string]$WorkerSessionFile,
    [switch]$WorkerBaselineZero,
    [string]$SessionRoot,
    [string]$RepositoryRoot,
    [string]$At,
    [switch]$SkipPushVerification
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:StateMarkerPattern = '(?s)<!-- WORK_LEDGER_STATE:([A-Za-z0-9+/=]+) -->'
$script:AllowedKinds = @('implementation', 'rubber-duck', 'finding-fix', 'validation', 'commit-push', 'other')

function Get-EventTime {
    if ([string]::IsNullOrWhiteSpace($At)) {
        return [DateTimeOffset]::Now
    }

    return [DateTimeOffset]::Parse(
        $At,
        [Globalization.CultureInfo]::InvariantCulture,
        [Globalization.DateTimeStyles]::RoundtripKind)
}

function Format-Timestamp {
    param([Parameter(Mandatory)][DateTimeOffset]$Value)

    return $Value.ToString('yyyy-MM-ddTHH:mm:ss.fffzzz', [Globalization.CultureInfo]::InvariantCulture)
}

function Format-Elapsed {
    param([Parameter(Mandatory)][TimeSpan]$Value)

    $totalHours = [Math]::Floor($Value.TotalHours)
    return '{0:00}:{1:00}:{2:00}' -f $totalHours, $Value.Minutes, $Value.Seconds
}

function Escape-MarkdownCell {
    param($Value)

    if ($null -eq $Value) {
        return 'n/a'
    }

    $text = [string]$Value
    if ([string]::IsNullOrWhiteSpace($text)) {
        return 'n/a'
    }

    return ($text -replace '\|', '\|' -replace '[\r\n]+', ' ').Trim()
}

function New-TokenSource {
    param(
        [string]$ThreadId,
        [string]$SessionFile,
        [string]$Root
    )

    if (-not [string]::IsNullOrWhiteSpace($SessionFile)) {
        return [pscustomobject][ordered]@{
            ThreadId = $ThreadId
            SessionFile = [IO.Path]::GetFullPath($SessionFile)
            SessionRoot = $null
        }
    }

    if ([string]::IsNullOrWhiteSpace($ThreadId)) {
        throw 'A token source requires CODEX_THREAD_ID, -MainThreadId, -WorkerThreadId, or an explicit session file.'
    }

    if ([string]::IsNullOrWhiteSpace($Root)) {
        $Root = Join-Path $env:USERPROFILE '.codex\sessions'
    }

    return [pscustomobject][ordered]@{
        ThreadId = $ThreadId
        SessionFile = $null
        SessionRoot = [IO.Path]::GetFullPath($Root)
    }
}

function Resolve-SessionFile {
    param([Parameter(Mandatory)]$Source)

    if (-not [string]::IsNullOrWhiteSpace([string]$Source.SessionFile)) {
        $path = [string]$Source.SessionFile
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Codex session file not found: $path"
        }
        return $path
    }

    $root = [string]$Source.SessionRoot
    $threadId = [string]$Source.ThreadId
    if (-not (Test-Path -LiteralPath $root -PathType Container)) {
        throw "Codex session directory not found: $root"
    }

    $matches = @(Get-ChildItem -LiteralPath $root -Recurse -File -Filter "*$threadId.jsonl")
    if ($matches.Count -ne 1) {
        throw "Expected one Codex session stream for thread '$threadId' under '$root'; found $($matches.Count)."
    }
    return $matches[0].FullName
}

function Read-TokenSnapshot {
    param(
        [Parameter(Mandatory)]$Source,
        [switch]$Zero
    )

    if ($Zero) {
        return [pscustomobject][ordered]@{
            Input = [int64]0
            CachedInput = [int64]0
            Output = [int64]0
            Total = [int64]0
        }
    }

    $path = Resolve-SessionFile -Source $Source
    $stream = $null
    $reader = $null
    $latest = $null

    try {
        $stream = [IO.File]::Open(
            $path,
            [IO.FileMode]::Open,
            [IO.FileAccess]::Read,
            [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
        $reader = [IO.StreamReader]::new($stream)

        while ($null -ne ($line = $reader.ReadLine())) {
            if ($line.IndexOf('"total_token_usage"', [StringComparison]::Ordinal) -lt 0) {
                continue
            }

            try {
                $event = $line | ConvertFrom-Json -ErrorAction Stop
            } catch {
                continue
            }

            $usage = $event.payload.info.total_token_usage
            if ($null -ne $usage) {
                $latest = $usage
            }
        }
    } finally {
        if ($null -ne $reader) {
            $reader.Dispose()
        } elseif ($null -ne $stream) {
            $stream.Dispose()
        }
    }

    if ($null -eq $latest) {
        throw "No cumulative token snapshot is available in Codex session stream: $path"
    }

    $snapshot = [pscustomobject][ordered]@{
        Input = [int64]$latest.input_tokens
        CachedInput = [int64]$latest.cached_input_tokens
        Output = [int64]$latest.output_tokens
        Total = [int64]$latest.total_tokens
    }
    if ($snapshot.Input -lt 0 -or $snapshot.CachedInput -lt 0 -or $snapshot.Output -lt 0 -or
        $snapshot.Total -lt 0 -or $snapshot.CachedInput -gt $snapshot.Input -or
        $snapshot.Total -ne ($snapshot.Input + $snapshot.Output)) {
        throw "Invalid cumulative token snapshot in Codex session stream: $path"
    }
    return $snapshot
}

function Get-TokenDelta {
    param(
        [Parameter(Mandatory)]$Start,
        [Parameter(Mandatory)]$End
    )

    $delta = [pscustomobject][ordered]@{
        Input = [int64]$End.Input - [int64]$Start.Input
        CachedInput = [int64]$End.CachedInput - [int64]$Start.CachedInput
        Output = [int64]$End.Output - [int64]$Start.Output
        Total = [int64]$End.Total - [int64]$Start.Total
    }
    if ($delta.Input -lt 0 -or $delta.CachedInput -lt 0 -or $delta.Output -lt 0 -or
        $delta.Total -lt 0 -or $delta.CachedInput -gt $delta.Input -or
        $delta.Total -ne ($delta.Input + $delta.Output)) {
        throw 'Codex token counters moved backwards or became internally inconsistent across a ledger boundary.'
    }
    return $delta
}

function Add-TokenDeltas {
    param([Parameter(Mandatory)][AllowEmptyCollection()][object[]]$Deltas)

    $sum = [pscustomobject][ordered]@{
        Input = [int64]0
        CachedInput = [int64]0
        Output = [int64]0
        Total = [int64]0
    }
    foreach ($delta in $Deltas) {
        if ($null -eq $delta) {
            continue
        }
        $sum.Input += [int64]$delta.Input
        $sum.CachedInput += [int64]$delta.CachedInput
        $sum.Output += [int64]$delta.Output
        $sum.Total += [int64]$delta.Total
    }
    return $sum
}

function Get-SourceLabel {
    param($Source)

    if ($null -eq $Source) {
        return 'none'
    }
    if (-not [string]::IsNullOrWhiteSpace([string]$Source.ThreadId)) {
        return [string]$Source.ThreadId
    }
    return Split-Path -Leaf ([string]$Source.SessionFile)
}

function Read-LedgerState {
    param([Parameter(Mandatory)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $null
    }

    $content = [IO.File]::ReadAllText($Path)
    $match = [regex]::Match($content, $script:StateMarkerPattern)
    if (-not $match.Success) {
        throw "Ledger state marker is missing or damaged: $Path"
    }

    $json = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($match.Groups[1].Value))
    $state = $json | ConvertFrom-Json -ErrorAction Stop
    if ([int]$state.Version -ne 1) {
        throw "Unsupported work-ledger state version '$($state.Version)' in: $Path"
    }
    return $state
}

function Get-ActiveRun {
    param([Parameter(Mandatory)]$State)

    $active = @($State.Runs | Where-Object { [string]::IsNullOrWhiteSpace([string]$_.FinishedAt) })
    if ($active.Count -ne 1) {
        throw "Expected one active ledger goal; found $($active.Count)."
    }
    return $active[0]
}

function Get-ActiveTask {
    param([Parameter(Mandatory)]$Run)

    $active = @($Run.Tasks | Where-Object { [string]::IsNullOrWhiteSpace([string]$_.FinishedAt) })
    if ($active.Count -ne 1) {
        throw "Expected one active ledger task; found $($active.Count)."
    }
    return $active[0]
}

function Get-ActiveStep {
    param([Parameter(Mandatory)]$TaskRecord)

    $active = @($TaskRecord.Steps | Where-Object { [string]::IsNullOrWhiteSpace([string]$_.FinishedAt) })
    if ($active.Count -ne 1) {
        throw "Expected one active ledger step; found $($active.Count)."
    }
    return $active[0]
}

function Assert-StepFields {
    if ([string]::IsNullOrWhiteSpace($Step) -or [string]::IsNullOrWhiteSpace($Kind) -or
        [string]::IsNullOrWhiteSpace($Label)) {
        throw '-Step, -Kind, and -Label are required when opening a step.'
    }
    if ($Kind -notin $script:AllowedKinds) {
        throw "Unsupported step kind '$Kind'. Expected one of: $($script:AllowedKinds -join ', ')."
    }
}

function New-StepRecord {
    param(
        [Parameter(Mandatory)][DateTimeOffset]$Now,
        [Parameter(Mandatory)]$MainStart
    )

    Assert-StepFields
    $workerSource = $null
    $workerStart = $null
    if (-not [string]::IsNullOrWhiteSpace($WorkerThreadId) -or
        -not [string]::IsNullOrWhiteSpace($WorkerSessionFile)) {
        $workerSource = New-TokenSource -ThreadId $WorkerThreadId -SessionFile $WorkerSessionFile -Root $SessionRoot
        $workerStart = Read-TokenSnapshot -Source $workerSource -Zero:$WorkerBaselineZero
    } elseif ($WorkerBaselineZero) {
        throw '-WorkerBaselineZero requires -WorkerThreadId or -WorkerSessionFile.'
    }

    return [pscustomobject][ordered]@{
        Id = $Step
        Kind = $Kind
        Label = $Label
        StartedAt = Format-Timestamp -Value $Now
        FinishedAt = $null
        MainStart = $MainStart
        MainEnd = $null
        WorkerSource = $workerSource
        WorkerStart = $workerStart
        WorkerEnd = $null
        Findings = $null
        Outcome = $null
    }
}

function Complete-ActiveStep {
    param(
        [Parameter(Mandatory)]$Run,
        [Parameter(Mandatory)]$TaskRecord,
        [Parameter(Mandatory)][DateTimeOffset]$Now
    )

    $stepRecord = Get-ActiveStep -TaskRecord $TaskRecord
    $mainEnd = Read-TokenSnapshot -Source $Run.MainSource
    $workerEnd = $null
    if ($null -ne $stepRecord.WorkerSource) {
        $workerEnd = Read-TokenSnapshot -Source $stepRecord.WorkerSource
    }

    # Invariant: validate monotonicity before mutating the in-memory record so a
    # failed token read leaves the on-disk ledger at its previous good boundary.
    $null = Get-TokenDelta -Start $stepRecord.MainStart -End $mainEnd
    if ($null -ne $workerEnd) {
        $null = Get-TokenDelta -Start $stepRecord.WorkerStart -End $workerEnd
    }
    if ($stepRecord.Kind -eq 'rubber-duck' -and $Findings -lt 0) {
        throw 'Closing a rubber-duck step requires -Findings with the exact finding count.'
    }

    $stepRecord.FinishedAt = Format-Timestamp -Value $Now
    $stepRecord.MainEnd = $mainEnd
    $stepRecord.WorkerEnd = $workerEnd
    $stepRecord.Findings = if ($Findings -ge 0) { $Findings } else { $null }
    $stepRecord.Outcome = $Outcome
    return $mainEnd
}

function Resolve-CommitRecord {
    param([Parameter(Mandatory)][string]$CommitValue)

    $repo = $RepositoryRoot
    if ([string]::IsNullOrWhiteSpace($repo)) {
        $planDirectory = Split-Path -Parent ([IO.Path]::GetFullPath($MasterPlanPath))
        $repo = (& git -C $planDirectory rev-parse --show-toplevel 2>$null)
        if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($repo)) {
            throw 'Could not resolve the Git repository containing MASTER-PLAN.'
        }
    }
    $repo = [IO.Path]::GetFullPath($repo.Trim())

    $fullHash = (& git -C $repo rev-parse --verify "$CommitValue`^{commit}" 2>$null)
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($fullHash)) {
        throw "Commit does not resolve to a local commit: $CommitValue"
    }
    $fullHash = $fullHash.Trim()

    if (-not $SkipPushVerification) {
        $upstream = (& git -C $repo rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>$null)
        if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($upstream)) {
            throw 'The current branch has no upstream; push the task commit before closing its ledger row.'
        }
        & git -C $repo merge-base --is-ancestor $fullHash $upstream.Trim()
        if ($LASTEXITCODE -ne 0) {
            throw "Commit $fullHash is not present in upstream '$($upstream.Trim())'; push it before closing the task."
        }
    }

    $subject = (& git -C $repo show -s --format=%s $fullHash)
    if ($LASTEXITCODE -ne 0) {
        throw "Could not read subject for commit: $fullHash"
    }
    return [pscustomobject][ordered]@{
        Hash = $fullHash
        Subject = $subject.Trim()
    }
}

function Get-StepDeltas {
    param([Parameter(Mandatory)]$StepRecord)

    if ([string]::IsNullOrWhiteSpace([string]$StepRecord.FinishedAt)) {
        return $null
    }
    $main = Get-TokenDelta -Start $StepRecord.MainStart -End $StepRecord.MainEnd
    $worker = $null
    if ($null -ne $StepRecord.WorkerEnd) {
        $worker = Get-TokenDelta -Start $StepRecord.WorkerStart -End $StepRecord.WorkerEnd
    }
    $combinedParts = @($main)
    if ($null -ne $worker) {
        $combinedParts += $worker
    }
    $combined = Add-TokenDeltas -Deltas $combinedParts
    return [pscustomobject][ordered]@{
        Main = $main
        Worker = $worker
        Combined = $combined
    }
}

function Get-TaskTotals {
    param([Parameter(Mandatory)]$TaskRecord)

    $stepDeltas = @($TaskRecord.Steps | ForEach-Object { Get-StepDeltas -StepRecord $_ } | Where-Object { $null -ne $_ })
    return Add-TokenDeltas -Deltas @($stepDeltas | ForEach-Object { $_.Combined })
}

function Get-RunTotals {
    param([Parameter(Mandatory)]$Run)

    $taskTotals = @($Run.Tasks | ForEach-Object { Get-TaskTotals -TaskRecord $_ })
    return Add-TokenDeltas -Deltas $taskTotals
}

function Get-RunMainTotal {
    param([Parameter(Mandatory)]$Run)

    if ($null -eq $Run.EndMain) {
        return $null
    }
    return Get-TokenDelta -Start $Run.StartMain -End $Run.EndMain
}

function Get-RunWorkerTotal {
    param([Parameter(Mandatory)]$Run)

    $workerDeltas = @()
    foreach ($taskRecord in $Run.Tasks) {
        foreach ($stepRecord in $taskRecord.Steps) {
            $deltas = Get-StepDeltas -StepRecord $stepRecord
            if ($null -ne $deltas -and $null -ne $deltas.Worker) {
                $workerDeltas += $deltas.Worker
            }
        }
    }
    return Add-TokenDeltas -Deltas $workerDeltas
}

function Get-StepElapsed {
    param([Parameter(Mandatory)]$StepRecord)

    if ([string]::IsNullOrWhiteSpace([string]$StepRecord.FinishedAt)) {
        return $null
    }
    return [DateTimeOffset]::Parse($StepRecord.FinishedAt) - [DateTimeOffset]::Parse($StepRecord.StartedAt)
}

function Get-TaskValidationElapsed {
    param([Parameter(Mandatory)]$TaskRecord)

    $total = [TimeSpan]::Zero
    foreach ($stepRecord in $TaskRecord.Steps) {
        if ($stepRecord.Kind -eq 'validation') {
            $elapsed = Get-StepElapsed -StepRecord $stepRecord
            if ($null -ne $elapsed) {
                $total += $elapsed
            }
        }
    }
    return $total
}

function Render-Ledger {
    param(
        [Parameter(Mandatory)]$State,
        [Parameter(Mandatory)][DateTimeOffset]$UpdatedAt
    )

    $builder = [Text.StringBuilder]::new()
    $null = $builder.AppendLine('# Work Ledger')
    $null = $builder.AppendLine()
    $null = $builder.AppendLine('Live orchestrator evidence. Every batch boundary atomically refreshes this file; an unfinished row is the current step.')
    $null = $builder.AppendLine()
    $null = $builder.AppendLine("- Master plan: ``$($State.MasterPlan)``")
    $null = $builder.AppendLine("- Last updated: ``$(Format-Timestamp -Value $UpdatedAt)``")
    $null = $builder.AppendLine('- Token semantics: input includes the cached-input subset; cached input is reported separately, not added again.')
    $null = $builder.AppendLine()
    $null = $builder.AppendLine('## Goal Runs')
    $null = $builder.AppendLine()
    $null = $builder.AppendLine('| Run | Goal | Started | Finished | Elapsed | Main input | Main output | Main cached input | Reviewer input | Reviewer output | Reviewer cached input | Combined input | Combined output | Combined cached input | Latest commit | Outcome |')
    $null = $builder.AppendLine('|-----|------|---------|----------|---------|------------|-------------|-------------------|----------------|-----------------|-----------------------|----------------|-----------------|-----------------------|---------------|---------|')

    foreach ($run in $State.Runs) {
        $finished = if ([string]::IsNullOrWhiteSpace([string]$run.FinishedAt)) { 'in progress' } else { $run.FinishedAt }
        $elapsed = if ([string]::IsNullOrWhiteSpace([string]$run.FinishedAt)) {
            'in progress'
        } else {
            Format-Elapsed -Value ([DateTimeOffset]::Parse($run.FinishedAt) - [DateTimeOffset]::Parse($run.StartedAt))
        }
        $mainTotal = Get-RunMainTotal -Run $run
        $workerTotal = Get-RunWorkerTotal -Run $run
        $combinedTotal = if ($null -eq $mainTotal) { $null } else { Add-TokenDeltas -Deltas @($mainTotal, $workerTotal) }
        $latestCommit = if ($run.Commits.Count -gt 0) { $run.Commits[-1].Hash } else { 'n/a' }
        $mainInput = if ($null -eq $mainTotal) { $null } else { $mainTotal.Input }
        $mainOutput = if ($null -eq $mainTotal) { $null } else { $mainTotal.Output }
        $mainCached = if ($null -eq $mainTotal) { $null } else { $mainTotal.CachedInput }
        $reviewerInput = if ($null -eq $mainTotal) { $null } else { $workerTotal.Input }
        $reviewerOutput = if ($null -eq $mainTotal) { $null } else { $workerTotal.Output }
        $reviewerCached = if ($null -eq $mainTotal) { $null } else { $workerTotal.CachedInput }
        $combinedInput = if ($null -eq $combinedTotal) { $null } else { $combinedTotal.Input }
        $combinedOutput = if ($null -eq $combinedTotal) { $null } else { $combinedTotal.Output }
        $combinedCached = if ($null -eq $combinedTotal) { $null } else { $combinedTotal.CachedInput }
        $null = $builder.AppendLine("| $(Escape-MarkdownCell $run.Id) | $(Escape-MarkdownCell $run.Goal) | ``$($run.StartedAt)`` | $(Escape-MarkdownCell $finished) | $(Escape-MarkdownCell $elapsed) | $(Escape-MarkdownCell $mainInput) | $(Escape-MarkdownCell $mainOutput) | $(Escape-MarkdownCell $mainCached) | $(Escape-MarkdownCell $reviewerInput) | $(Escape-MarkdownCell $reviewerOutput) | $(Escape-MarkdownCell $reviewerCached) | $(Escape-MarkdownCell $combinedInput) | $(Escape-MarkdownCell $combinedOutput) | $(Escape-MarkdownCell $combinedCached) | $(Escape-MarkdownCell $latestCommit) | $(Escape-MarkdownCell $run.Outcome) |")
    }

    foreach ($run in $State.Runs) {
        $null = $builder.AppendLine()
        $null = $builder.AppendLine("## Run $(Escape-MarkdownCell $run.Id): $(Escape-MarkdownCell $run.Goal)")
        $null = $builder.AppendLine()
        $null = $builder.AppendLine("Parent token session: ``$(Get-SourceLabel -Source $run.MainSource)``")
        $null = $builder.AppendLine()
        $null = $builder.AppendLine('### Task Summary')
        $null = $builder.AppendLine()
        $null = $builder.AppendLine('| Task | Started | Finished | Elapsed | Input tokens | Output tokens | Cached input tokens | Duck passes | Fix cycles | Findings | Validation | Commit | Outcome |')
        $null = $builder.AppendLine('|------|---------|----------|---------|--------------|---------------|---------------------|-------------|------------|----------|------------|--------|---------|')

        foreach ($taskRecord in $run.Tasks) {
            $finished = if ([string]::IsNullOrWhiteSpace([string]$taskRecord.FinishedAt)) { 'in progress' } else { $taskRecord.FinishedAt }
            $elapsed = if ([string]::IsNullOrWhiteSpace([string]$taskRecord.FinishedAt)) {
                'in progress'
            } else {
                Format-Elapsed -Value ([DateTimeOffset]::Parse($taskRecord.FinishedAt) - [DateTimeOffset]::Parse($taskRecord.StartedAt))
            }
            $totals = Get-TaskTotals -TaskRecord $taskRecord
            $duckSteps = @($taskRecord.Steps | Where-Object { $_.Kind -eq 'rubber-duck' -and -not [string]::IsNullOrWhiteSpace([string]$_.FinishedAt) })
            $fixSteps = @($taskRecord.Steps | Where-Object { $_.Kind -eq 'finding-fix' -and -not [string]::IsNullOrWhiteSpace([string]$_.FinishedAt) })
            $findingTotal = [int64]0
            foreach ($duckStep in $duckSteps) {
                $findingTotal += [int64]$duckStep.Findings
            }
            $validation = Get-TaskValidationElapsed -TaskRecord $taskRecord
            $commitHash = if ($taskRecord.Commits.Count -gt 0) { $taskRecord.Commits[-1].Hash } else { 'n/a' }
            $taskDisplay = Escape-MarkdownCell -Value ($taskRecord.Id + ' - ' + $taskRecord.Title)
            $null = $builder.AppendLine("| $taskDisplay | ``$($taskRecord.StartedAt)`` | $(Escape-MarkdownCell $finished) | $(Escape-MarkdownCell $elapsed) | $($totals.Input) | $($totals.Output) | $($totals.CachedInput) | $($duckSteps.Count) | $($fixSteps.Count) | $findingTotal | $(Format-Elapsed -Value $validation) | $(Escape-MarkdownCell $commitHash) | $(Escape-MarkdownCell $taskRecord.Outcome) |")
        }

        foreach ($taskRecord in $run.Tasks) {
            $null = $builder.AppendLine()
            $null = $builder.AppendLine("### Task $(Escape-MarkdownCell $taskRecord.Id): $(Escape-MarkdownCell $taskRecord.Title)")
            $null = $builder.AppendLine()
            $null = $builder.AppendLine('| Step | Kind | Token sessions | Started | Finished | Elapsed | Main input | Main output | Main cached input | Reviewer input | Reviewer output | Reviewer cached input | Combined input | Combined output | Combined cached input | Findings | Outcome |')
            $null = $builder.AppendLine('|------|------|----------------|---------|----------|---------|------------|-------------|-------------------|----------------|-----------------|-----------------------|----------------|-----------------|-----------------------|----------|---------|')

            foreach ($stepRecord in $taskRecord.Steps) {
                $deltas = Get-StepDeltas -StepRecord $stepRecord
                $finished = if ([string]::IsNullOrWhiteSpace([string]$stepRecord.FinishedAt)) { 'in progress' } else { $stepRecord.FinishedAt }
                $elapsed = Get-StepElapsed -StepRecord $stepRecord
                $elapsedText = if ($null -eq $elapsed) { 'in progress' } else { Format-Elapsed -Value $elapsed }
                $sessions = Get-SourceLabel -Source $run.MainSource
                if ($null -ne $stepRecord.WorkerSource) {
                    $sessions += " + $(Get-SourceLabel -Source $stepRecord.WorkerSource)"
                }
                $mainInput = if ($null -eq $deltas) { $null } else { $deltas.Main.Input }
                $mainOutput = if ($null -eq $deltas) { $null } else { $deltas.Main.Output }
                $mainCached = if ($null -eq $deltas) { $null } else { $deltas.Main.CachedInput }
                $workerInput = if ($null -eq $deltas -or $null -eq $deltas.Worker) { $null } else { $deltas.Worker.Input }
                $workerOutput = if ($null -eq $deltas -or $null -eq $deltas.Worker) { $null } else { $deltas.Worker.Output }
                $workerCached = if ($null -eq $deltas -or $null -eq $deltas.Worker) { $null } else { $deltas.Worker.CachedInput }
                $combinedInput = if ($null -eq $deltas) { $null } else { $deltas.Combined.Input }
                $combinedOutput = if ($null -eq $deltas) { $null } else { $deltas.Combined.Output }
                $combinedCached = if ($null -eq $deltas) { $null } else { $deltas.Combined.CachedInput }
                $stepDisplay = Escape-MarkdownCell -Value ($stepRecord.Id + ' - ' + $stepRecord.Label)
                $null = $builder.AppendLine("| $stepDisplay | $(Escape-MarkdownCell $stepRecord.Kind) | $(Escape-MarkdownCell $sessions) | ``$($stepRecord.StartedAt)`` | $(Escape-MarkdownCell $finished) | $(Escape-MarkdownCell $elapsedText) | $(Escape-MarkdownCell $mainInput) | $(Escape-MarkdownCell $mainOutput) | $(Escape-MarkdownCell $mainCached) | $(Escape-MarkdownCell $workerInput) | $(Escape-MarkdownCell $workerOutput) | $(Escape-MarkdownCell $workerCached) | $(Escape-MarkdownCell $combinedInput) | $(Escape-MarkdownCell $combinedOutput) | $(Escape-MarkdownCell $combinedCached) | $(Escape-MarkdownCell $stepRecord.Findings) | $(Escape-MarkdownCell $stepRecord.Outcome) |")
            }

            if ($taskRecord.Commits.Count -gt 0) {
                $null = $builder.AppendLine()
                $null = $builder.AppendLine('#### Commits')
                $null = $builder.AppendLine()
                $null = $builder.AppendLine('| Hash | Subject |')
                $null = $builder.AppendLine('|------|---------|')
                foreach ($commitRecord in $taskRecord.Commits) {
                    $null = $builder.AppendLine("| ``$($commitRecord.Hash)`` | $(Escape-MarkdownCell $commitRecord.Subject) |")
                }
            }
        }
    }

    $json = $State | ConvertTo-Json -Depth 20 -Compress
    $encoded = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($json))
    $null = $builder.AppendLine()
    $null = $builder.AppendLine("<!-- WORK_LEDGER_STATE:$encoded -->")
    return $builder.ToString()
}

function Write-Ledger {
    param(
        [Parameter(Mandatory)]$State,
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][DateTimeOffset]$UpdatedAt
    )

    $directory = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $directory -PathType Container)) {
        throw "Ledger directory does not exist: $directory"
    }

    $content = Render-Ledger -State $State -UpdatedAt $UpdatedAt
    $tempPath = Join-Path $directory ('.' + [IO.Path]::GetFileName($Path) + '.' + [Guid]::NewGuid().ToString('N') + '.tmp')
    try {
        [IO.File]::WriteAllText($tempPath, $content, [Text.UTF8Encoding]::new($false))
        Move-Item -LiteralPath $tempPath -Destination $Path -Force
    } finally {
        if (Test-Path -LiteralPath $tempPath -PathType Leaf) {
            Remove-Item -LiteralPath $tempPath -Force
        }
    }
}

function Invoke-LedgerAction {
    $masterPath = [IO.Path]::GetFullPath($MasterPlanPath)
    if (-not (Test-Path -LiteralPath $masterPath -PathType Leaf)) {
        throw "MASTER-PLAN file not found: $masterPath"
    }
    $resolvedLedgerPath = if ([string]::IsNullOrWhiteSpace($LedgerPath)) {
        Join-Path (Split-Path -Parent $masterPath) 'WORK_LEDGER.md'
    } else {
        [IO.Path]::GetFullPath($LedgerPath)
    }

    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        $mutexBytes = $sha256.ComputeHash([Text.Encoding]::UTF8.GetBytes($resolvedLedgerPath.ToLowerInvariant()))
    } finally {
        $sha256.Dispose()
    }
    $mutexName = 'SkullbonezWorkLedger_' + (($mutexBytes | ForEach-Object { $_.ToString('x2') }) -join '')
    $mutex = [Threading.Mutex]::new($false, $mutexName)
    $locked = $false
    try {
        $locked = $mutex.WaitOne([TimeSpan]::FromSeconds(30))
        if (-not $locked) {
            throw "Timed out waiting for the live work-ledger writer: $resolvedLedgerPath"
        }

        $now = Get-EventTime
        $state = Read-LedgerState -Path $resolvedLedgerPath

        switch ($Action) {
            'start-goal' {
                if ($null -eq $state) {
                    $state = [pscustomobject][ordered]@{
                        Version = 1
                        MasterPlan = $masterPath
                        Runs = @()
                    }
                }
                $unfinished = @($state.Runs | Where-Object { [string]::IsNullOrWhiteSpace([string]$_.FinishedAt) })
                if ($unfinished.Count -ne 0) {
                    throw 'Cannot start a goal while another ledger goal is unfinished.'
                }

                $threadId = if ([string]::IsNullOrWhiteSpace($MainThreadId)) { $env:CODEX_THREAD_ID } else { $MainThreadId }
                $mainSource = New-TokenSource -ThreadId $threadId -SessionFile $MainSessionFile -Root $SessionRoot
                $mainStart = Read-TokenSnapshot -Source $mainSource
                $runId = $now.ToString('yyyyMMdd-HHmmss', [Globalization.CultureInfo]::InvariantCulture)
                $run = [pscustomobject][ordered]@{
                    Id = $runId
                    Goal = $Goal
                    StartedAt = Format-Timestamp -Value $now
                    FinishedAt = $null
                    MainSource = $mainSource
                    StartMain = $mainStart
                    EndMain = $null
                    Tasks = @()
                    Commits = @()
                    Outcome = $null
                }
                $state.Runs = @($state.Runs) + $run
            }
            'start-task' {
                if ($null -eq $state) {
                    throw 'Start the ledger goal before starting a task.'
                }
                if ([string]::IsNullOrWhiteSpace($Task) -or [string]::IsNullOrWhiteSpace($Title)) {
                    throw '-Task and -Title are required for start-task.'
                }
                Assert-StepFields
                $run = Get-ActiveRun -State $state
                $unfinished = @($run.Tasks | Where-Object { [string]::IsNullOrWhiteSpace([string]$_.FinishedAt) })
                if ($unfinished.Count -ne 0) {
                    throw 'Cannot start a task while another ledger task is unfinished.'
                }
                if (@($run.Tasks | Where-Object { $_.Id -eq $Task }).Count -ne 0) {
                    throw "Task id already exists in this goal run: $Task"
                }

                $mainStart = Read-TokenSnapshot -Source $run.MainSource
                $taskRecord = [pscustomobject][ordered]@{
                    Id = $Task
                    Title = $Title
                    StartedAt = Format-Timestamp -Value $now
                    FinishedAt = $null
                    StartMain = $mainStart
                    EndMain = $null
                    Steps = @()
                    Commits = @()
                    Outcome = $null
                }
                $taskRecord.Steps = @(New-StepRecord -Now $now -MainStart $mainStart)
                $run.Tasks = @($run.Tasks) + $taskRecord
            }
            'attach-worker' {
                if ($null -eq $state) {
                    throw 'No live ledger exists to attach a worker session.'
                }
                if ([string]::IsNullOrWhiteSpace($WorkerThreadId) -and
                    [string]::IsNullOrWhiteSpace($WorkerSessionFile)) {
                    throw 'attach-worker requires -WorkerThreadId or -WorkerSessionFile.'
                }
                $run = Get-ActiveRun -State $state
                $taskRecord = Get-ActiveTask -Run $run
                $stepRecord = Get-ActiveStep -TaskRecord $taskRecord
                if (-not [string]::IsNullOrWhiteSpace($Task) -and $Task -ne $taskRecord.Id) {
                    throw "Active task is '$($taskRecord.Id)', not '$Task'."
                }
                if ($null -ne $stepRecord.WorkerSource) {
                    throw "Active step '$($stepRecord.Id)' already has a worker token source."
                }

                $workerSource = New-TokenSource -ThreadId $WorkerThreadId -SessionFile $WorkerSessionFile -Root $SessionRoot
                $workerStart = Read-TokenSnapshot -Source $workerSource -Zero:$WorkerBaselineZero
                $stepRecord.WorkerSource = $workerSource
                $stepRecord.WorkerStart = $workerStart
            }
            'transition' {
                if ($null -eq $state) {
                    throw 'No live ledger exists to transition.'
                }
                Assert-StepFields
                $run = Get-ActiveRun -State $state
                $taskRecord = Get-ActiveTask -Run $run
                if (-not [string]::IsNullOrWhiteSpace($Task) -and $Task -ne $taskRecord.Id) {
                    throw "Active task is '$($taskRecord.Id)', not '$Task'."
                }
                if (@($taskRecord.Steps | Where-Object { $_.Id -eq $Step }).Count -ne 0) {
                    throw "Step id already exists in task '$($taskRecord.Id)': $Step"
                }

                $mainBoundary = Complete-ActiveStep -Run $run -TaskRecord $taskRecord -Now $now
                $taskRecord.Steps = @($taskRecord.Steps) + (New-StepRecord -Now $now -MainStart $mainBoundary)
            }
            'finish-task' {
                if ($null -eq $state) {
                    throw 'No live ledger exists to finish.'
                }
                if ([string]::IsNullOrWhiteSpace($Commit)) {
                    throw '-Commit is required for finish-task and must name the pushed task commit.'
                }
                $run = Get-ActiveRun -State $state
                $taskRecord = Get-ActiveTask -Run $run
                if (-not [string]::IsNullOrWhiteSpace($Task) -and $Task -ne $taskRecord.Id) {
                    throw "Active task is '$($taskRecord.Id)', not '$Task'."
                }

                $mainEnd = Complete-ActiveStep -Run $run -TaskRecord $taskRecord -Now $now
                $commitRecord = Resolve-CommitRecord -CommitValue $Commit
                $taskRecord.FinishedAt = Format-Timestamp -Value $now
                $taskRecord.EndMain = $mainEnd
                $taskRecord.Commits = @($taskRecord.Commits) + $commitRecord
                $taskRecord.Outcome = $Outcome
                $run.Commits = @($run.Commits) + $commitRecord
            }
            'finish-goal' {
                if ($null -eq $state) {
                    throw 'No live ledger exists to finish.'
                }
                $run = Get-ActiveRun -State $state
                $unfinishedTasks = @($run.Tasks | Where-Object { [string]::IsNullOrWhiteSpace([string]$_.FinishedAt) })
                if ($unfinishedTasks.Count -ne 0) {
                    throw 'Finish the active task before finishing the ledger goal.'
                }
                if ($run.Tasks.Count -eq 0) {
                    throw 'A goal cannot finish without at least one recorded task.'
                }

                $run.FinishedAt = Format-Timestamp -Value $now
                $run.EndMain = Read-TokenSnapshot -Source $run.MainSource
                $null = Get-TokenDelta -Start $run.StartMain -End $run.EndMain
                $run.Outcome = $Outcome
            }
            'show' {
                if ($null -eq $state) {
                    throw "No work ledger exists yet: $resolvedLedgerPath"
                }
            }
        }

        if ($Action -ne 'show') {
            Write-Ledger -State $state -Path $resolvedLedgerPath -UpdatedAt $now
        }
        Write-Output $resolvedLedgerPath
    } finally {
        if ($locked) {
            $mutex.ReleaseMutex()
        }
        $mutex.Dispose()
    }
}

function Write-FakeTokenEvent {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][int64]$InputTokens,
        [Parameter(Mandatory)][int64]$CachedTokens,
        [Parameter(Mandatory)][int64]$OutputTokens
    )

    $usage = [ordered]@{
        input_tokens = $InputTokens
        cached_input_tokens = $CachedTokens
        output_tokens = $OutputTokens
        total_tokens = $InputTokens + $OutputTokens
    }
    $event = [ordered]@{
        timestamp = [DateTimeOffset]::UtcNow.ToString('o')
        type = 'event_msg'
        payload = [ordered]@{
            type = 'token_count'
            info = [ordered]@{
                total_token_usage = $usage
                last_token_usage = $usage
            }
        }
    }
    [IO.File]::AppendAllText(
        $Path,
        (($event | ConvertTo-Json -Depth 8 -Compress) + [Environment]::NewLine),
        [Text.UTF8Encoding]::new($false))
}

function Invoke-SelfTest {
    $testRoot = Join-Path ([IO.Path]::GetTempPath()) ('skullbonez-work-ledger-' + [Guid]::NewGuid().ToString('N'))
    $null = New-Item -ItemType Directory -Path $testRoot
    try {
        $plan = Join-Path $testRoot 'MASTER-PLAN.md'
        $ledger = Join-Path $testRoot 'WORK_LEDGER.md'
        $mainSession = Join-Path $testRoot 'main.jsonl'
        $duckSession = Join-Path $testRoot 'duck.jsonl'
        [IO.File]::WriteAllText($plan, '# Test master plan', [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText($mainSession, '', [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText($duckSession, '', [Text.UTF8Encoding]::new($false))

        $repo = (& git -C $PSScriptRoot rev-parse --show-toplevel 2>$null)
        if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($repo)) {
            throw 'Self-test must run from the tracked SkullbonezCore skill directory.'
        }
        $head = (& git -C $repo.Trim() rev-parse HEAD).Trim()
        $powershell = Join-Path $PSHOME 'powershell.exe'

        Write-FakeTokenEvent -Path $mainSession -InputTokens 100 -CachedTokens 50 -OutputTokens 20
        $null = & $powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath start-goal -MasterPlanPath $plan -LedgerPath $ledger -MainSessionFile $mainSession -At '2026-08-18T09:00:00+10:00'
        if ($LASTEXITCODE -ne 0) { throw 'start-goal self-test failed.' }

        Write-FakeTokenEvent -Path $mainSession -InputTokens 150 -CachedTokens 70 -OutputTokens 30
        $null = & $powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath start-task -MasterPlanPath $plan -LedgerPath $ledger -Task 'PLAN-T1' -Title 'Test task' -Step implementation -Kind implementation -Label 'Implementation' -At '2026-08-18T09:01:00+10:00'
        if ($LASTEXITCODE -ne 0) { throw 'start-task self-test failed.' }

        $ledgerBeforeFailure = [Convert]::ToBase64String([IO.File]::ReadAllBytes($ledger))
        Write-FakeTokenEvent -Path $mainSession -InputTokens 140 -CachedTokens 65 -OutputTokens 25
        $savedErrorPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        $null = & $powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath transition -MasterPlanPath $plan -LedgerPath $ledger -Task 'PLAN-T1' -Outcome 'must fail' -Step invalid -Kind other -Label 'Invalid' -At '2026-08-18T09:02:00+10:00' 2>&1
        $negativeExitCode = $LASTEXITCODE
        $ErrorActionPreference = $savedErrorPreference
        if ($negativeExitCode -eq 0) { throw 'Backward-token negative control unexpectedly succeeded.' }
        $ledgerAfterFailure = [Convert]::ToBase64String([IO.File]::ReadAllBytes($ledger))
        if ($ledgerAfterFailure -ne $ledgerBeforeFailure) {
            throw 'A rejected token transition mutated the live ledger.'
        }

        Write-FakeTokenEvent -Path $mainSession -InputTokens 250 -CachedTokens 120 -OutputTokens 50
        $null = & $powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath transition -MasterPlanPath $plan -LedgerPath $ledger -Task 'PLAN-T1' -Outcome 'ready for review' -Step 'rubber-duck-01' -Kind 'rubber-duck' -Label 'Rubber duck pass 1' -At '2026-08-18T09:11:00+10:00'
        if ($LASTEXITCODE -ne 0) { throw 'implementation-to-duck transition self-test failed.' }
        $null = & $powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath attach-worker -MasterPlanPath $plan -LedgerPath $ledger -Task 'PLAN-T1' -WorkerSessionFile $duckSession -WorkerBaselineZero -At '2026-08-18T09:11:01+10:00'
        if ($LASTEXITCODE -ne 0) { throw 'reviewer attachment self-test failed.' }

        Write-FakeTokenEvent -Path $mainSession -InputTokens 270 -CachedTokens 130 -OutputTokens 55
        Write-FakeTokenEvent -Path $duckSession -InputTokens 100 -CachedTokens 60 -OutputTokens 20
        $ledgerBeforeFailure = [Convert]::ToBase64String([IO.File]::ReadAllBytes($ledger))
        $savedErrorPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        $null = & $powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath transition -MasterPlanPath $plan -LedgerPath $ledger -Task 'PLAN-T1' -Outcome 'missing count' -Step invalid -Kind other -Label 'Invalid' -At '2026-08-18T09:15:00+10:00' 2>&1
        $negativeExitCode = $LASTEXITCODE
        $ErrorActionPreference = $savedErrorPreference
        if ($negativeExitCode -eq 0) { throw 'Missing-findings negative control unexpectedly succeeded.' }
        $ledgerAfterFailure = [Convert]::ToBase64String([IO.File]::ReadAllBytes($ledger))
        if ($ledgerAfterFailure -ne $ledgerBeforeFailure) {
            throw 'A rejected rubber-duck transition mutated the live ledger.'
        }

        $null = & $powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath transition -MasterPlanPath $plan -LedgerPath $ledger -Task 'PLAN-T1' -Outcome 'two blocking findings' -Findings 2 -Step 'finding-fix-01' -Kind 'finding-fix' -Label 'Finding fixes 1' -At '2026-08-18T09:16:00+10:00'
        if ($LASTEXITCODE -ne 0) { throw 'duck-to-fix transition self-test failed.' }

        Write-FakeTokenEvent -Path $mainSession -InputTokens 320 -CachedTokens 150 -OutputTokens 70
        $null = & $powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath transition -MasterPlanPath $plan -LedgerPath $ledger -Task 'PLAN-T1' -Outcome 'findings fixed' -Step validation -Kind validation -Label 'Final validation' -At '2026-08-18T09:21:00+10:00'
        if ($LASTEXITCODE -ne 0) { throw 'fix-to-validation transition self-test failed.' }

        Write-FakeTokenEvent -Path $mainSession -InputTokens 400 -CachedTokens 180 -OutputTokens 90
        $null = & $powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath transition -MasterPlanPath $plan -LedgerPath $ledger -Task 'PLAN-T1' -Outcome 'passed' -Step 'commit-push' -Kind 'commit-push' -Label 'Commit and push' -At '2026-08-18T09:31:00+10:00'
        if ($LASTEXITCODE -ne 0) { throw 'validation-to-commit transition self-test failed.' }

        Write-FakeTokenEvent -Path $mainSession -InputTokens 420 -CachedTokens 190 -OutputTokens 95
        $null = & $powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath finish-task -MasterPlanPath $plan -LedgerPath $ledger -Task 'PLAN-T1' -Outcome 'pushed' -Commit $head -RepositoryRoot $repo.Trim() -SkipPushVerification -At '2026-08-18T09:33:00+10:00'
        if ($LASTEXITCODE -ne 0) { throw 'finish-task self-test failed.' }

        Write-FakeTokenEvent -Path $mainSession -InputTokens 430 -CachedTokens 195 -OutputTokens 100
        $null = & $powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath finish-goal -MasterPlanPath $plan -LedgerPath $ledger -Outcome 'complete' -At '2026-08-18T09:34:00+10:00'
        if ($LASTEXITCODE -ne 0) { throw 'finish-goal self-test failed.' }

        $content = [IO.File]::ReadAllText($ledger)
        foreach ($expected in @(
            'PLAN-T1 - Test task',
            'rubber-duck-01 - Rubber duck pass 1',
            'Token semantics: input includes the cached-input subset',
            '| Input tokens | Output tokens | Cached input tokens |',
            '| 370 | 85 | 180 | 1 | 1 | 2 | 00:10:00 |',
            '| 20 | 5 | 10 | 100 | 20 | 60 | 120 | 25 | 70 | 2 | two blocking findings |',
            $head,
            '<!-- WORK_LEDGER_STATE:'
        )) {
            if ($content.IndexOf($expected, [StringComparison]::Ordinal) -lt 0) {
                throw "Rendered-ledger self-test did not find: $expected"
            }
        }
        foreach ($retiredHeader in @('Main tokens', 'Reviewer tokens', 'Total tokens', '| Total |')) {
            if ($content.IndexOf($retiredHeader, [StringComparison]::Ordinal) -ge 0) {
                throw "Rendered-ledger self-test retained ambiguous token header: $retiredHeader"
            }
        }

        Write-Output 'PASS: live work-ledger transitions, token deltas, reviewer accounting, findings, validation timing, and commit attribution.'
    } finally {
        if (Test-Path -LiteralPath $testRoot -PathType Container) {
            Remove-Item -LiteralPath $testRoot -Recurse -Force
        }
    }
}

if ($Action -eq 'self-test') {
    Invoke-SelfTest
    exit 0
}

Invoke-LedgerAction
