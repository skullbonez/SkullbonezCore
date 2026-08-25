<#
Purpose:
  Persist exact orchestrator timing, token, review, validation, and commit
  evidence beside the authoritative MASTER-PLAN.

Invariants:
  - One unfinished goal may contain multiple unfinished tasks, with exactly one
    unfinished step per task and no primary session shared by live tasks.
  - Per-task primary-session step boundaries are contiguous, so their deltas
    neither gap nor overlap; reviewer-session deltas are added exactly once.
  - The CSV and its final embedded-state row are replaced together under a
    named mutex, so a reader never observes a partially written ledger.
  - Cached input is a subset of input. Cost charges uncached input at the input
    rate and cached input at the cached rate, never both rates for one token.
  - Completed task commits resolve to full hashes and are verified as pushed
    to the configured upstream unless self-test explicitly disables that check.
  - Every accepted plan or bug commit carries ordered rationale, ownership,
    implementation, validation, artifact, and review evidence in its body.
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('start-goal', 'start-task', 'attach-worker', 'transition', 'finish-task', 'stop-task', 'finish-goal', 'show', 'verify-commit-message', 'self-test')]
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
    [string]$MessageFile,
    [string]$MainThreadId,
    [string]$MainSessionFile,
    [switch]$MainBaselineZero,
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

$script:LegacyStateMarkerPattern = '(?s)<!-- WORK_LEDGER_STATE:([A-Za-z0-9+/=]+) -->'
$script:AllowedKinds = @('implementation', 'rubber-duck', 'finding-fix', 'validation', 'commit-push', 'other')
$script:PricingSource = 'https://platform.openai.com/docs/pricing'
$script:PricingTier = 'standard'
$script:PricingContext = 'short'
$script:CsvColumns = @(
    'record_type', 'run_id', 'goal', 'task_id', 'task_title', 'step_id',
    'step_kind', 'step_label', 'status', 'started_at', 'finished_at',
    'elapsed_seconds', 'elapsed_hms', 'main_session', 'main_model',
    'main_input_tokens', 'main_output_tokens', 'main_cached_input_tokens',
    'main_uncached_input_tokens', 'main_input_cost_usd',
    'main_cached_input_cost_usd', 'main_output_cost_usd',
    'main_api_cost_usd', 'reviewer_session', 'reviewer_model',
    'reviewer_input_tokens', 'reviewer_output_tokens',
    'reviewer_cached_input_tokens', 'reviewer_uncached_input_tokens',
    'reviewer_input_cost_usd', 'reviewer_cached_input_cost_usd',
    'reviewer_output_cost_usd', 'reviewer_api_cost_usd',
    'combined_input_tokens', 'combined_output_tokens',
    'combined_cached_input_tokens', 'combined_uncached_input_tokens',
    'combined_api_cost_usd', 'main_input_rate_usd_per_million',
    'main_cached_input_rate_usd_per_million', 'main_output_rate_usd_per_million',
    'reviewer_input_rate_usd_per_million',
    'reviewer_cached_input_rate_usd_per_million',
    'reviewer_output_rate_usd_per_million',
    'pricing_tier', 'pricing_context', 'pricing_source', 'duck_passes',
    'fix_cycles', 'findings', 'validation_seconds', 'validation_hms',
    'commit_hash', 'commit_subject', 'outcome',
    'portfolio_completed_tasks', 'portfolio_total_tasks',
    'portfolio_percent_complete', 'plan_completed_tasks', 'plan_total_tasks',
    'plan_percent_complete', 'updated_at', 'state_base64')

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

function Format-Decimal {
    param(
        [Parameter(Mandatory)][decimal]$Value,
        [int]$Places = 6
    )

    return $Value.ToString(('0.' + ('0' * $Places)), [Globalization.CultureInfo]::InvariantCulture)
}

function New-CsvRow {
    param([hashtable]$Values = @{})

    $row = [ordered]@{}
    foreach ($column in $script:CsvColumns) {
        $row[$column] = if ($Values.ContainsKey($column)) { $Values[$column] } else { $null }
    }
    return [pscustomobject]$row
}

function Get-ApiPricing {
    param([Parameter(Mandatory)][string]$Model)

    # Source: official OpenAI API pricing, Standard / Short context, fetched
    # 2026-08-18. Unknown models fail closed so a model change cannot silently
    # apply stale or unrelated rates.
    $rates = switch ($Model) {
        'gpt-5.6-sol' { @(5.0, 0.5, 30.0); break }
        'gpt-5.6-terra' { @(2.0, 0.2, 12.0); break }
        'gpt-5.6-luna' { @(0.2, 0.02, 1.2); break }
        'gpt-5.5' { @(5.0, 0.5, 30.0); break }
        'gpt-5.4' { @(2.5, 0.25, 15.0); break }
        'gpt-5.4-mini' { @(0.75, 0.075, 4.5); break }
        'gpt-5.4-nano' { @(0.2, 0.02, 1.25); break }
        'gpt-5.2' { @(1.75, 0.175, 14.0); break }
        'gpt-5.1' { @(1.25, 0.125, 10.0); break }
        'gpt-5' { @(1.25, 0.125, 10.0); break }
        'gemini-3.7-flash' { @(0.10, 0.025, 0.40); break }
        'gemini-2.5-pro' { @(1.25, 0.3125, 5.00); break }
        'gemini-2.5-flash' { @(0.075, 0.01875, 0.30); break }
        'gemini-1.5-pro' { @(1.25, 0.3125, 5.00); break }
        'gemini-1.5-flash' { @(0.075, 0.01875, 0.30); break }
        default { throw "No verified standard short-context API pricing is configured for model '$Model'." }
    }
    return [pscustomobject][ordered]@{
        Model = $Model
        Input = [decimal]$rates[0]
        CachedInput = [decimal]$rates[1]
        Output = [decimal]$rates[2]
    }
}

function Get-UsageCost {
    param(
        [Parameter(Mandatory)]$Delta,
        [Parameter(Mandatory)][string]$Model
    )

    $pricing = Get-ApiPricing -Model $Model
    $uncached = [int64]$Delta.Input - [int64]$Delta.CachedInput
    $million = [decimal]1000000
    $inputCost = ([decimal]$uncached * $pricing.Input) / $million
    $cachedCost = ([decimal]$Delta.CachedInput * $pricing.CachedInput) / $million
    $outputCost = ([decimal]$Delta.Output * $pricing.Output) / $million
    return [pscustomobject][ordered]@{
        Model = $Model
        UncachedInput = $uncached
        InputRate = $pricing.Input
        CachedInputRate = $pricing.CachedInput
        OutputRate = $pricing.Output
        InputCost = $inputCost
        CachedInputCost = $cachedCost
        OutputCost = $outputCost
        TotalCost = $inputCost + $cachedCost + $outputCost
    }
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
        if (-not [string]::IsNullOrWhiteSpace($env:ANTIGRAVITY_CONVERSATION_ID)) {
            $ThreadId = $env:ANTIGRAVITY_CONVERSATION_ID
        }
    }

    if ([string]::IsNullOrWhiteSpace($ThreadId)) {
        throw 'A token source requires CODEX_THREAD_ID, ANTIGRAVITY_CONVERSATION_ID, -MainThreadId, -WorkerThreadId, or an explicit session file.'
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
            throw "Session file not found: $path"
        }
        return $path
    }

    $root = [string]$Source.SessionRoot
    $threadId = [string]$Source.ThreadId

    # Check Antigravity brain transcript path first if present
    $agyTranscript = Join-Path $env:USERPROFILE ".gemini\antigravity\brain\$threadId\.system_generated\logs\transcript.jsonl"
    if (Test-Path -LiteralPath $agyTranscript -PathType Leaf) {
        return $agyTranscript
    }

    if (-not (Test-Path -LiteralPath $root -PathType Container)) {
        throw "Session directory not found: $root (and Antigravity transcript not found at $agyTranscript)"
    }

    $matches = @(Get-ChildItem -LiteralPath $root -Recurse -File -Filter "*$threadId.jsonl")
    if ($matches.Count -ne 1) {
        throw "Expected one session stream for thread '$threadId' under '$root'; found $($matches.Count)."
    }
    return $matches[0].FullName
}

function Read-TokenSnapshot {
    param(
        [Parameter(Mandatory)]$Source,
        [switch]$Zero,
        [switch]$EmptyAsZero
    )

    $path = Resolve-SessionFile -Source $Source
    $stream = $null
    $reader = $null
    $latest = $null
    $latestModel = $null

    try {
        $stream = [IO.File]::Open(
            $path,
            [IO.FileMode]::Open,
            [IO.FileAccess]::Read,
            [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
        $reader = [IO.StreamReader]::new($stream)

        while ($null -ne ($line = $reader.ReadLine())) {
            if ($line.IndexOf('"turn_context"', [StringComparison]::Ordinal) -ge 0 -and
                $line.IndexOf('"model"', [StringComparison]::Ordinal) -ge 0) {
                try {
                    $contextEvent = $line | ConvertFrom-Json -ErrorAction Stop
                    if ($contextEvent.type -eq 'turn_context' -and
                        -not [string]::IsNullOrWhiteSpace([string]$contextEvent.payload.model)) {
                        $latestModel = [string]$contextEvent.payload.model
                    }
                } catch {
                    # A concurrently appended partial JSON line is ignored; the
                    # next complete turn_context event remains authoritative.
                }
            }
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

    if ([string]::IsNullOrWhiteSpace($latestModel)) {
        throw "No model identity is available in Codex session stream: $path"
    }
    $null = Get-ApiPricing -Model $latestModel

    if ($Zero -or ($EmptyAsZero -and $null -eq $latest)) {
        return [pscustomobject][ordered]@{
            Input = [int64]0
            CachedInput = [int64]0
            Output = [int64]0
            Total = [int64]0
            Model = $latestModel
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
        Model = $latestModel
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
    $startModel = if ($null -ne $Start.PSObject.Properties['Model']) { [string]$Start.Model } else { $null }
    $endModel = if ($null -ne $End.PSObject.Properties['Model']) { [string]$End.Model } else { $null }
    if (-not [string]::IsNullOrWhiteSpace($startModel) -and
        -not [string]::IsNullOrWhiteSpace($endModel) -and $startModel -ne $endModel) {
        throw "Codex model changed from '$startModel' to '$endModel' inside one ledger step; split the step at the model boundary."
    }
    $delta | Add-Member -NotePropertyName Model -NotePropertyValue $(
        if (-not [string]::IsNullOrWhiteSpace($endModel)) { $endModel } else { $startModel })
    return $delta
}

function Add-TokenDeltas {
    param([Parameter(Mandatory)][AllowEmptyCollection()][object[]]$Deltas)

    $sum = [pscustomobject][ordered]@{
        Input = [int64]0
        CachedInput = [int64]0
        Output = [int64]0
        Total = [int64]0
        Cost = [decimal]0
    }
    foreach ($delta in $Deltas) {
        if ($null -eq $delta) {
            continue
        }
        $sum.Input += [int64]$delta.Input
        $sum.CachedInput += [int64]$delta.CachedInput
        $sum.Output += [int64]$delta.Output
        $sum.Total += [int64]$delta.Total
        if ($null -ne $delta.PSObject.Properties['Cost']) {
            $sum.Cost += [decimal]$delta.Cost
        }
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

function Get-DeltaModel {
    param(
        [Parameter(Mandatory)]$Delta,
        [Parameter(Mandatory)]$Source
    )

    if ($null -ne $Delta.PSObject.Properties['Model'] -and
        -not [string]::IsNullOrWhiteSpace([string]$Delta.Model)) {
        return [string]$Delta.Model
    }
    return [string](Read-TokenSnapshot -Source $Source).Model
}

function Read-LedgerState {
    param([Parameter(Mandatory)][string]$Path)

    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        $stateRows = @(Import-Csv -LiteralPath $Path | Where-Object { $_.record_type -eq 'state' })
        if ($stateRows.Count -ne 1 -or [string]::IsNullOrWhiteSpace($stateRows[0].state_base64)) {
            throw "CSV ledger state row is missing or damaged: $Path"
        }
        $json = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($stateRows[0].state_base64))
    } else {
        $legacyPath = [IO.Path]::ChangeExtension($Path, '.md')
        if (-not (Test-Path -LiteralPath $legacyPath -PathType Leaf)) {
            return $null
        }
        $content = [IO.File]::ReadAllText($legacyPath)
        $match = [regex]::Match($content, $script:LegacyStateMarkerPattern)
        if (-not $match.Success) {
            throw "Legacy ledger state marker is missing or damaged: $legacyPath"
        }
        $json = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($match.Groups[1].Value))
    }
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
    param(
        [Parameter(Mandatory)]$Run,
        [string]$TaskId
    )

    $active = @($Run.Tasks | Where-Object { [string]::IsNullOrWhiteSpace([string]$_.FinishedAt) })
    if (-not [string]::IsNullOrWhiteSpace($TaskId)) {
        $matching = @($active | Where-Object { $_.Id -eq $TaskId })
        if ($matching.Count -ne 1) {
            throw "Expected one active ledger task named '$TaskId'; found $($matching.Count)."
        }
        return $matching[0]
    }
    if ($active.Count -ne 1) {
        throw "Expected one active ledger task or an explicit -Task; found $($active.Count)."
    }
    return $active[0]
}

function Get-TaskMainSource {
    param(
        [Parameter(Mandatory)]$Run,
        [Parameter(Mandatory)]$TaskRecord
    )

    # Compatibility: ledgers written before concurrent task support inherit
    # the goal's primary session exactly as they did when created.
    if ($null -ne $TaskRecord.PSObject.Properties['MainSource'] -and
        $null -ne $TaskRecord.MainSource) {
        return $TaskRecord.MainSource
    }
    return $Run.MainSource
}

function Get-SourceKey {
    param([Parameter(Mandatory)]$Source)

    if (-not [string]::IsNullOrWhiteSpace([string]$Source.SessionFile)) {
        return 'file:' + [IO.Path]::GetFullPath([string]$Source.SessionFile).ToLowerInvariant()
    }
    return 'thread:' + ([string]$Source.ThreadId).ToLowerInvariant()
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
    $taskMainSource = Get-TaskMainSource -Run $Run -TaskRecord $TaskRecord
    $mainEnd = Read-TokenSnapshot -Source $taskMainSource
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

    $message = (& git -C $repo show -s --format=%B $fullHash)
    if ($LASTEXITCODE -ne 0) {
        throw "Could not read commit message for commit: $fullHash"
    }
    $messageText = ($message -join [Environment]::NewLine)
    Assert-OrchestratorCommitMessageContent -Content $messageText -Context "commit $fullHash"
    $subject = ($messageText.Replace("`r`n", "`n") -split "`n")[0]
    return [pscustomobject][ordered]@{
        Hash = $fullHash
        Subject = $subject.Trim()
    }
}

function Assert-OrchestratorCommitMessageContent {
    param(
        [Parameter(Mandatory)][string]$Content,
        [Parameter(Mandatory)][string]$Context
    )

    $content = $Content.Replace("`r`n", "`n")
    $lines = @($content -split "`n")
    if ($lines.Count -lt 3 -or [string]::IsNullOrWhiteSpace($lines[0])) {
        throw "$Context requires a subject and a substantive body."
    }
    if (-not [string]::IsNullOrWhiteSpace($lines[1])) {
        throw "$Context subject must be followed by a blank line."
    }
    $planSubject = $lines[0] -match '^[A-Z0-9_]+, TASK [0-9]+/[0-9]+ (?:\u2014|-)[ ]+\S.+$'
    $bugSubject = $lines[0] -match '^BUG [A-Z][A-Z0-9_-]*-[0-9]+ (?:\u2014|-)[ ]+\S.+$'
    if (-not $planSubject -and -not $bugSubject) {
        throw "$Context subject must use the plan-task or parallel bug-finding convention."
    }

    $body = ($lines[2..($lines.Count - 1)] -join "`n").Trim()
    if ($body.Length -lt 320) {
        throw "$Context body is too short to preserve plan evidence: length=$($body.Length) minimum=320"
    }
    $sections = @('Why', 'Ownership', 'What', 'Validation', 'Baselines/Artifacts', 'Review')
    $sectionValues = @{}
    $previousIndex = -1
    foreach ($section in $sections) {
        $match = [regex]::Match($body, "(?m)^$([regex]::Escape($section)):\s+(.+?)\s*$")
        if (-not $match.Success) {
            throw "$Context requires a non-empty '${section}:' section."
        }
        if ($match.Index -le $previousIndex) {
            throw "$Context sections must appear in the required order."
        }
        $value = $match.Groups[1].Value.Trim()
        if ($value.Length -lt 32 -or $value -match '^(?i:n/?a|none|unknown|tbd|todo|x)[.!]?$') {
            throw "$Context '${section}:' section is not substantive enough: length=$($value.Length) minimum=32"
        }
        $sectionValues[$section] = $value
        $previousIndex = $match.Index
    }
    if ($sectionValues['Ownership'] -notmatch '(?i)\b(owner|ownership|authority)\b') {
        throw "$Context Ownership section must identify the affected owner or state that authority did not move."
    }
    if ($sectionValues['Validation'] -notmatch '(?i)\b(pass(?:ed)?|fail(?:ed)?|deferred|not applicable|exit(?:ed)?(?: code)? [0-9]+)\b') {
        throw "$Context Validation section must record an exact result or an explicit deferral/not-applicable ruling."
    }
    if ($sectionValues['Baselines/Artifacts'] -notmatch '(?i)\b(baseline|golden|artifact|binary|dll|testoutput)\b') {
        throw "$Context Baselines/Artifacts section must disposition baselines and generated artifacts explicitly."
    }
    if ($sectionValues['Review'] -notmatch '(?i)\b(clean|finding|not required|deferred)\b') {
        throw "$Context Review section must record the independent verdict, findings, or why review was not required."
    }
}

function Assert-OrchestratorCommitMessage {
    param([Parameter(Mandatory)][string]$Path)

    $resolved = [IO.Path]::GetFullPath($Path)
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw "Commit message file not found: $resolved"
    }
    Assert-OrchestratorCommitMessageContent -Content ([IO.File]::ReadAllText($resolved)) -Context "commit message file '$resolved'"
}

function Get-StepDeltas {
    param(
        [Parameter(Mandatory)]$StepRecord,
        [Parameter(Mandatory)]$Run,
        [Parameter(Mandatory)]$TaskRecord,
        [switch]$IncludeLive
    )

    $taskMainSource = Get-TaskMainSource -Run $Run -TaskRecord $TaskRecord
    $mainEnd = $StepRecord.MainEnd
    $workerEnd = $StepRecord.WorkerEnd
    if ([string]::IsNullOrWhiteSpace([string]$StepRecord.FinishedAt)) {
        if (-not $IncludeLive) {
            return $null
        }
        $mainEnd = Read-TokenSnapshot -Source $taskMainSource
        if ($null -ne $StepRecord.WorkerSource) {
            $workerEnd = Read-TokenSnapshot -Source $StepRecord.WorkerSource -EmptyAsZero:$([int64]$StepRecord.WorkerStart.Total -eq 0)
        }
    }
    $main = Get-TokenDelta -Start $StepRecord.MainStart -End $mainEnd
    $mainModel = Get-DeltaModel -Delta $main -Source $taskMainSource
    $mainCost = Get-UsageCost -Delta $main -Model $mainModel
    $main | Add-Member -NotePropertyName Cost -NotePropertyValue $mainCost.TotalCost
    $worker = $null
    $workerCost = $null
    if ($null -ne $workerEnd) {
        $worker = Get-TokenDelta -Start $StepRecord.WorkerStart -End $workerEnd
        $workerModel = Get-DeltaModel -Delta $worker -Source $StepRecord.WorkerSource
        $workerCost = Get-UsageCost -Delta $worker -Model $workerModel
        $worker | Add-Member -NotePropertyName Cost -NotePropertyValue $workerCost.TotalCost
    }
    $combinedParts = @($main)
    if ($null -ne $worker) {
        $combinedParts += $worker
    }
    $combined = Add-TokenDeltas -Deltas $combinedParts
    return [pscustomobject][ordered]@{
        Main = $main
        MainCost = $mainCost
        Worker = $worker
        WorkerCost = $workerCost
        Combined = $combined
    }
}

function Get-TaskTotals {
    param(
        [Parameter(Mandatory)]$TaskRecord,
        [Parameter(Mandatory)]$Run,
        [switch]$IncludeLive
    )

    $stepDeltas = @($TaskRecord.Steps | ForEach-Object {
        Get-StepDeltas -StepRecord $_ -Run $Run -TaskRecord $TaskRecord -IncludeLive:$IncludeLive
    } | Where-Object { $null -ne $_ })
    return Add-TokenDeltas -Deltas @($stepDeltas | ForEach-Object { $_.Combined })
}

function Get-RunTotals {
    param(
        [Parameter(Mandatory)]$Run,
        [switch]$IncludeLive
    )

    $taskTotals = @($Run.Tasks | ForEach-Object {
        Get-TaskTotals -TaskRecord $_ -Run $Run -IncludeLive:$IncludeLive
    })
    return Add-TokenDeltas -Deltas $taskTotals
}

function Get-RunMainTotal {
    param(
        [Parameter(Mandatory)]$Run,
        [switch]$IncludeLive
    )

    $end = $Run.EndMain
    if ($null -eq $end -and $IncludeLive) {
        $end = Read-TokenSnapshot -Source $Run.MainSource
    }
    if ($null -eq $end) {
        return $null
    }
    $delta = Get-TokenDelta -Start $Run.StartMain -End $end
    $model = Get-DeltaModel -Delta $delta -Source $Run.MainSource
    $cost = Get-UsageCost -Delta $delta -Model $model
    $delta | Add-Member -NotePropertyName Cost -NotePropertyValue $cost.TotalCost
    return [pscustomobject][ordered]@{ Delta = $delta; Cost = $cost }
}

# Invariant: the goal's primary session is counted once by Get-RunMainTotal.
# Primary sessions owned by concurrent tasks therefore join the worker total,
# alongside any secondary reviewer attached to a task step.
function Get-RunWorkerTotal {
    param(
        [Parameter(Mandatory)]$Run,
        [switch]$IncludeLive
    )

    $workerDeltas = @()
    foreach ($taskRecord in $Run.Tasks) {
        $taskMainSource = Get-TaskMainSource -Run $Run -TaskRecord $taskRecord
        $taskIsWorkerOwned = (Get-SourceKey -Source $taskMainSource) -ne
            (Get-SourceKey -Source $Run.MainSource)
        foreach ($stepRecord in $taskRecord.Steps) {
            $deltas = Get-StepDeltas -StepRecord $stepRecord -Run $Run -TaskRecord $taskRecord -IncludeLive:$IncludeLive
            if ($null -ne $deltas -and $taskIsWorkerOwned) {
                $workerDeltas += $deltas.Main
            }
            if ($null -ne $deltas -and $null -ne $deltas.Worker) {
                $workerDeltas += $deltas.Worker
            }
        }
    }
    return Add-TokenDeltas -Deltas $workerDeltas
}

function Get-StepElapsed {
    param(
        [Parameter(Mandatory)]$StepRecord,
        [DateTimeOffset]$AsOf
    )

    if ([string]::IsNullOrWhiteSpace([string]$StepRecord.FinishedAt)) {
        if ($PSBoundParameters.ContainsKey('AsOf')) {
            return $AsOf - [DateTimeOffset]::Parse($StepRecord.StartedAt)
        }
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

function Get-MasterPlanProgress {
    param([Parameter(Mandatory)][string]$Path)

    $content = [IO.File]::ReadAllText($Path)
    $portfolioMatch = [regex]::Match($content, '(?m)^Status:.*?(\d+)\s*/\s*(\d+)\s+tasks complete')
    if (-not $portfolioMatch.Success) {
        throw "Could not read portfolio task progress from MASTER-PLAN: $Path"
    }
    $plans = @{}
    foreach ($match in [regex]::Matches(
        $content,
        '(?m)^\|\s*[^|]+\|\s*`(?<code>[A-Z0-9_]+)`\s*\|\s*(?<total>\d+)\s*\|\s*(?<complete>\d+)\s*\|')) {
        $plans[$match.Groups['code'].Value] = [pscustomobject]@{
            Complete = [int]$match.Groups['complete'].Value
            Total = [int]$match.Groups['total'].Value
        }
    }
    return [pscustomobject]@{
        Complete = [int]$portfolioMatch.Groups[1].Value
        Total = [int]$portfolioMatch.Groups[2].Value
        Plans = $plans
    }
}

function Get-TaskPlanCode {
    param([string]$TaskId)

    $match = [regex]::Match($TaskId, '^(?<code>.+)-T\d+$')
    return $(if ($match.Success) { $match.Groups['code'].Value } else { $null })
}

function Get-Percent {
    param([int]$Complete, [int]$Total)

    if ($Total -le 0) { return $null }
    return ([decimal]$Complete * [decimal]100 / [decimal]$Total).ToString(
        '0.00', [Globalization.CultureInfo]::InvariantCulture)
}

function Get-TaskUsage {
    param(
        [Parameter(Mandatory)]$TaskRecord,
        [Parameter(Mandatory)]$Run,
        [switch]$IncludeLive
    )

    $main = @()
    $worker = @()
    $mainModels = @()
    $workerModels = @()
    foreach ($stepRecord in $TaskRecord.Steps) {
        $deltas = Get-StepDeltas -StepRecord $stepRecord -Run $Run -TaskRecord $TaskRecord -IncludeLive:$IncludeLive
        if ($null -eq $deltas) { continue }
        $main += $deltas.Main
        $mainModels += $deltas.MainCost.Model
        if ($null -ne $deltas.Worker) {
            $worker += $deltas.Worker
            $workerModels += $deltas.WorkerCost.Model
        }
    }
    $mainTotal = Add-TokenDeltas -Deltas $main
    $workerTotal = Add-TokenDeltas -Deltas $worker
    $distinctMainModels = @($mainModels | Select-Object -Unique)
    $distinctWorkerModels = @($workerModels | Select-Object -Unique)
    return [pscustomobject]@{
        Main = $mainTotal
        MainCost = if ($distinctMainModels.Count -eq 1) {
            Get-UsageCost -Delta $mainTotal -Model $distinctMainModels[0]
        } else { $null }
        Worker = $workerTotal
        WorkerCost = if ($distinctWorkerModels.Count -eq 1) {
            Get-UsageCost -Delta $workerTotal -Model $distinctWorkerModels[0]
        } else { $null }
        MainModel = if ($distinctMainModels.Count -eq 1) { $distinctMainModels[0] } elseif ($distinctMainModels.Count -gt 1) { 'mixed' } else { $null }
        WorkerModel = if ($distinctWorkerModels.Count -eq 1) { $distinctWorkerModels[0] } elseif ($distinctWorkerModels.Count -gt 1) { 'mixed' } else { $null }
    }
}

function Set-UsageValues {
    param(
        [Parameter(Mandatory)][hashtable]$Values,
        [Parameter(Mandatory)][ValidateSet('main', 'reviewer')][string]$Prefix,
        $Delta,
        $Cost
    )

    if ($null -eq $Delta) { return }
    $Values["${Prefix}_input_tokens"] = [int64]$Delta.Input
    $Values["${Prefix}_output_tokens"] = [int64]$Delta.Output
    $Values["${Prefix}_cached_input_tokens"] = [int64]$Delta.CachedInput
    $Values["${Prefix}_uncached_input_tokens"] = [int64]$Delta.Input - [int64]$Delta.CachedInput
    $Values["${Prefix}_api_cost_usd"] = Format-Decimal -Value ([decimal]$Delta.Cost)
    if ($null -ne $Cost) {
        $Values["${Prefix}_model"] = $Cost.Model
        $Values["${Prefix}_input_cost_usd"] = Format-Decimal -Value $Cost.InputCost
        $Values["${Prefix}_cached_input_cost_usd"] = Format-Decimal -Value $Cost.CachedInputCost
        $Values["${Prefix}_output_cost_usd"] = Format-Decimal -Value $Cost.OutputCost
        $Values["${Prefix}_input_rate_usd_per_million"] = Format-Decimal -Value $Cost.InputRate -Places 3
        $Values["${Prefix}_cached_input_rate_usd_per_million"] = Format-Decimal -Value $Cost.CachedInputRate -Places 3
        $Values["${Prefix}_output_rate_usd_per_million"] = Format-Decimal -Value $Cost.OutputRate -Places 3
    }
}

function Set-CombinedUsageValues {
    param(
        [Parameter(Mandatory)][hashtable]$Values,
        [Parameter(Mandatory)]$Main,
        $Worker
    )

    $parts = @($Main)
    if ($null -ne $Worker) { $parts += $Worker }
    $combined = Add-TokenDeltas -Deltas $parts
    $Values['combined_input_tokens'] = $combined.Input
    $Values['combined_output_tokens'] = $combined.Output
    $Values['combined_cached_input_tokens'] = $combined.CachedInput
    $Values['combined_uncached_input_tokens'] = $combined.Input - $combined.CachedInput
    $Values['combined_api_cost_usd'] = Format-Decimal -Value $combined.Cost
}

function Render-Ledger {
    param(
        [Parameter(Mandatory)]$State,
        [Parameter(Mandatory)][DateTimeOffset]$UpdatedAt
    )

    $rows = [Collections.Generic.List[object]]::new()
    $updatedText = Format-Timestamp -Value $UpdatedAt
    $progress = Get-MasterPlanProgress -Path $State.MasterPlan
    $portfolioPercent = Get-Percent -Complete $progress.Complete -Total $progress.Total

    foreach ($run in $State.Runs) {
        $runEnd = if ([string]::IsNullOrWhiteSpace([string]$run.FinishedAt)) { $UpdatedAt } else { [DateTimeOffset]::Parse($run.FinishedAt) }
        $runElapsed = $runEnd - [DateTimeOffset]::Parse($run.StartedAt)
        $runMain = Get-RunMainTotal -Run $run -IncludeLive
        $runWorker = Get-RunWorkerTotal -Run $run -IncludeLive
        $runValues = @{
            record_type = 'goal'; run_id = $run.Id; goal = $run.Goal
            status = $(if ($null -eq $run.EndMain) { 'in_progress' } else { 'complete' })
            started_at = $run.StartedAt; finished_at = $run.FinishedAt
            elapsed_seconds = [int64][Math]::Floor($runElapsed.TotalSeconds)
            elapsed_hms = Format-Elapsed -Value $runElapsed
            main_session = Get-SourceLabel -Source $run.MainSource
            pricing_tier = $script:PricingTier; pricing_context = $script:PricingContext
            pricing_source = $script:PricingSource
            portfolio_completed_tasks = $progress.Complete
            portfolio_total_tasks = $progress.Total
            portfolio_percent_complete = $portfolioPercent
            commit_hash = $(if ($run.Commits.Count -gt 0) { $run.Commits[-1].Hash } else { $null })
            commit_subject = $(if ($run.Commits.Count -gt 0) { $run.Commits[-1].Subject } else { $null })
            outcome = $run.Outcome; updated_at = $updatedText
        }
        if ($null -ne $runMain) {
            Set-UsageValues -Values $runValues -Prefix main -Delta $runMain.Delta -Cost $runMain.Cost
            Set-UsageValues -Values $runValues -Prefix reviewer -Delta $runWorker -Cost $null
            Set-CombinedUsageValues -Values $runValues -Main $runMain.Delta -Worker $runWorker
        }
        $rows.Add((New-CsvRow -Values $runValues))

        foreach ($taskRecord in $run.Tasks) {
            $taskMainSource = Get-TaskMainSource -Run $run -TaskRecord $taskRecord
            $taskEnd = if ([string]::IsNullOrWhiteSpace([string]$taskRecord.FinishedAt)) { $UpdatedAt } else { [DateTimeOffset]::Parse($taskRecord.FinishedAt) }
            $taskElapsed = $taskEnd - [DateTimeOffset]::Parse($taskRecord.StartedAt)
            $usage = Get-TaskUsage -TaskRecord $taskRecord -Run $run -IncludeLive
            $duckSteps = @($taskRecord.Steps | Where-Object { $_.Kind -eq 'rubber-duck' -and -not [string]::IsNullOrWhiteSpace([string]$_.FinishedAt) })
            $fixSteps = @($taskRecord.Steps | Where-Object { $_.Kind -eq 'finding-fix' -and -not [string]::IsNullOrWhiteSpace([string]$_.FinishedAt) })
            $findingTotal = [int64]0
            foreach ($duckStep in $duckSteps) { $findingTotal += [int64]$duckStep.Findings }
            $validation = Get-TaskValidationElapsed -TaskRecord $taskRecord
            $planCode = Get-TaskPlanCode -TaskId $taskRecord.Id
            $planProgress = if ($null -ne $planCode -and $progress.Plans.ContainsKey($planCode)) { $progress.Plans[$planCode] } else { $null }
            $taskValues = @{
                record_type = 'task'; run_id = $run.Id; goal = $run.Goal
                task_id = $taskRecord.Id; task_title = $taskRecord.Title
                status = $(if ([string]::IsNullOrWhiteSpace([string]$taskRecord.FinishedAt)) { 'in_progress' } else { 'complete' })
                started_at = $taskRecord.StartedAt; finished_at = $taskRecord.FinishedAt
                elapsed_seconds = [int64][Math]::Floor($taskElapsed.TotalSeconds)
                elapsed_hms = Format-Elapsed -Value $taskElapsed
                main_session = Get-SourceLabel -Source $taskMainSource
                main_model = $usage.MainModel; reviewer_model = $usage.WorkerModel
                pricing_tier = $script:PricingTier; pricing_context = $script:PricingContext
                pricing_source = $script:PricingSource; duck_passes = $duckSteps.Count
                fix_cycles = $fixSteps.Count; findings = $findingTotal
                validation_seconds = [int64][Math]::Floor($validation.TotalSeconds)
                validation_hms = Format-Elapsed -Value $validation
                commit_hash = $(if ($taskRecord.Commits.Count -gt 0) { $taskRecord.Commits[-1].Hash } else { $null })
                commit_subject = $(if ($taskRecord.Commits.Count -gt 0) { $taskRecord.Commits[-1].Subject } else { $null })
                outcome = $taskRecord.Outcome
                portfolio_completed_tasks = $progress.Complete
                portfolio_total_tasks = $progress.Total
                portfolio_percent_complete = $portfolioPercent
                plan_completed_tasks = $(if ($null -ne $planProgress) { $planProgress.Complete } else { $null })
                plan_total_tasks = $(if ($null -ne $planProgress) { $planProgress.Total } else { $null })
                plan_percent_complete = $(if ($null -ne $planProgress) { Get-Percent -Complete $planProgress.Complete -Total $planProgress.Total } else { $null })
                updated_at = $updatedText
            }
            Set-UsageValues -Values $taskValues -Prefix main -Delta $usage.Main -Cost $usage.MainCost
            Set-UsageValues -Values $taskValues -Prefix reviewer -Delta $usage.Worker -Cost $usage.WorkerCost
            Set-CombinedUsageValues -Values $taskValues -Main $usage.Main -Worker $usage.Worker
            $rows.Add((New-CsvRow -Values $taskValues))

            foreach ($stepRecord in $taskRecord.Steps) {
                $stepDeltas = Get-StepDeltas -StepRecord $stepRecord -Run $run -TaskRecord $taskRecord -IncludeLive
                $stepElapsed = Get-StepElapsed -StepRecord $stepRecord -AsOf $UpdatedAt
                $stepValues = @{
                    record_type = 'step'; run_id = $run.Id; goal = $run.Goal
                    task_id = $taskRecord.Id; task_title = $taskRecord.Title
                    step_id = $stepRecord.Id; step_kind = $stepRecord.Kind
                    step_label = $stepRecord.Label
                    status = $(if ([string]::IsNullOrWhiteSpace([string]$stepRecord.FinishedAt)) { 'in_progress' } else { 'complete' })
                    started_at = $stepRecord.StartedAt; finished_at = $stepRecord.FinishedAt
                    elapsed_seconds = [int64][Math]::Floor($stepElapsed.TotalSeconds)
                    elapsed_hms = Format-Elapsed -Value $stepElapsed
                    main_session = Get-SourceLabel -Source $taskMainSource
                    reviewer_session = $(if ($null -ne $stepRecord.WorkerSource) { Get-SourceLabel -Source $stepRecord.WorkerSource } else { $null })
                    pricing_tier = $script:PricingTier; pricing_context = $script:PricingContext
                    pricing_source = $script:PricingSource; findings = $stepRecord.Findings
                    outcome = $stepRecord.Outcome
                    portfolio_completed_tasks = $progress.Complete
                    portfolio_total_tasks = $progress.Total
                    portfolio_percent_complete = $portfolioPercent
                    plan_completed_tasks = $(if ($null -ne $planProgress) { $planProgress.Complete } else { $null })
                    plan_total_tasks = $(if ($null -ne $planProgress) { $planProgress.Total } else { $null })
                    plan_percent_complete = $(if ($null -ne $planProgress) { Get-Percent -Complete $planProgress.Complete -Total $planProgress.Total } else { $null })
                    updated_at = $updatedText
                }
                Set-UsageValues -Values $stepValues -Prefix main -Delta $stepDeltas.Main -Cost $stepDeltas.MainCost
                Set-UsageValues -Values $stepValues -Prefix reviewer -Delta $stepDeltas.Worker -Cost $stepDeltas.WorkerCost
                Set-CombinedUsageValues -Values $stepValues -Main $stepDeltas.Main -Worker $stepDeltas.Worker
                $rows.Add((New-CsvRow -Values $stepValues))
            }

            foreach ($commitRecord in $taskRecord.Commits) {
                $rows.Add((New-CsvRow -Values @{
                    record_type = 'commit'; run_id = $run.Id; goal = $run.Goal
                    task_id = $taskRecord.Id; task_title = $taskRecord.Title
                    status = 'complete'; commit_hash = $commitRecord.Hash
                    commit_subject = $commitRecord.Subject
                    portfolio_completed_tasks = $progress.Complete
                    portfolio_total_tasks = $progress.Total
                    portfolio_percent_complete = $portfolioPercent
                    updated_at = $updatedText
                }))
            }
        }
    }

    $json = $State | ConvertTo-Json -Depth 20 -Compress
    $encoded = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($json))
    $rows.Add((New-CsvRow -Values @{
        record_type = 'state'; status = 'internal'; updated_at = $updatedText
        state_base64 = $encoded
    }))
    return (($rows | ConvertTo-Csv -NoTypeInformation) -join [Environment]::NewLine) + [Environment]::NewLine
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
        Join-Path (Split-Path -Parent $masterPath) 'WORK_LEDGER.csv'
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
                if (@($run.Tasks | Where-Object { $_.Id -eq $Task }).Count -ne 0) {
                    throw "Task id already exists in this goal run: $Task"
                }

                $hasExplicitMain = -not [string]::IsNullOrWhiteSpace($MainThreadId) -or
                    -not [string]::IsNullOrWhiteSpace($MainSessionFile)
                if ($MainBaselineZero -and -not $hasExplicitMain) {
                    throw '-MainBaselineZero requires -MainThreadId or -MainSessionFile.'
                }
                $taskMainSource = if ($hasExplicitMain) {
                    New-TokenSource -ThreadId $MainThreadId -SessionFile $MainSessionFile -Root $SessionRoot
                } else {
                    $run.MainSource
                }
                $taskSourceKey = Get-SourceKey -Source $taskMainSource
                # Hazard: assigning one cumulative token stream to two live
                # tasks would count the same agent usage twice.
                foreach ($activeTask in @($run.Tasks | Where-Object { [string]::IsNullOrWhiteSpace([string]$_.FinishedAt) })) {
                    $activeSource = Get-TaskMainSource -Run $run -TaskRecord $activeTask
                    if ((Get-SourceKey -Source $activeSource) -eq $taskSourceKey) {
                        throw "Primary session '$((Get-SourceLabel -Source $taskMainSource))' already owns active task '$($activeTask.Id)'."
                    }
                    foreach ($activeStep in @($activeTask.Steps | Where-Object { [string]::IsNullOrWhiteSpace([string]$_.FinishedAt) })) {
                        if ($null -ne $activeStep.WorkerSource -and
                            (Get-SourceKey -Source $activeStep.WorkerSource) -eq $taskSourceKey) {
                            throw "Session '$((Get-SourceLabel -Source $taskMainSource))' is already attached to active task '$($activeTask.Id)'."
                        }
                    }
                }

                $mainStart = Read-TokenSnapshot -Source $taskMainSource -Zero:$MainBaselineZero
                $taskRecord = [pscustomobject][ordered]@{
                    Id = $Task
                    Title = $Title
                    StartedAt = Format-Timestamp -Value $now
                    FinishedAt = $null
                    MainSource = $taskMainSource
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
                $taskRecord = Get-ActiveTask -Run $run -TaskId $Task
                $stepRecord = Get-ActiveStep -TaskRecord $taskRecord
                if ($null -ne $stepRecord.WorkerSource) {
                    throw "Active step '$($stepRecord.Id)' already has a worker token source."
                }

                $workerSource = New-TokenSource -ThreadId $WorkerThreadId -SessionFile $WorkerSessionFile -Root $SessionRoot
                $workerSourceKey = Get-SourceKey -Source $workerSource
                foreach ($activeTask in @($run.Tasks | Where-Object { [string]::IsNullOrWhiteSpace([string]$_.FinishedAt) })) {
                    $activeMainSource = Get-TaskMainSource -Run $run -TaskRecord $activeTask
                    if ((Get-SourceKey -Source $activeMainSource) -eq $workerSourceKey) {
                        throw "Worker session '$((Get-SourceLabel -Source $workerSource))' already owns active task '$($activeTask.Id)'."
                    }
                    foreach ($activeStep in @($activeTask.Steps | Where-Object { [string]::IsNullOrWhiteSpace([string]$_.FinishedAt) })) {
                        if ($activeStep -ne $stepRecord -and $null -ne $activeStep.WorkerSource -and
                            (Get-SourceKey -Source $activeStep.WorkerSource) -eq $workerSourceKey) {
                            throw "Worker session '$((Get-SourceLabel -Source $workerSource))' is already attached to active task '$($activeTask.Id)'."
                        }
                    }
                }
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
                $taskRecord = Get-ActiveTask -Run $run -TaskId $Task
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
                $taskRecord = Get-ActiveTask -Run $run -TaskId $Task

                $mainEnd = Complete-ActiveStep -Run $run -TaskRecord $taskRecord -Now $now
                $commitRecord = Resolve-CommitRecord -CommitValue $Commit
                $taskRecord.FinishedAt = Format-Timestamp -Value $now
                $taskRecord.EndMain = $mainEnd
                $taskRecord.Commits = @($taskRecord.Commits) + $commitRecord
                $taskRecord.Outcome = $Outcome
                $run.Commits = @($run.Commits) + $commitRecord
            }
            'stop-task' {
                if ($null -eq $state) {
                    throw 'No live ledger exists to stop.'
                }
                if ([string]::IsNullOrWhiteSpace($Outcome)) {
                    throw '-Outcome is required for stop-task.'
                }
                $run = Get-ActiveRun -State $state
                $taskRecord = Get-ActiveTask -Run $run -TaskId $Task
                $mainEnd = Complete-ActiveStep -Run $run -TaskRecord $taskRecord -Now $now
                $taskRecord.FinishedAt = Format-Timestamp -Value $now
                $taskRecord.EndMain = $mainEnd
                $taskRecord.Outcome = $Outcome
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

        # `show` is also a write: it refreshes the unfinished row's live elapsed
        # time, token counters, progress, and cost without closing the step.
        Write-Ledger -State $state -Path $resolvedLedgerPath -UpdatedAt $now
        $legacyPath = [IO.Path]::ChangeExtension($resolvedLedgerPath, '.md')
        if (Test-Path -LiteralPath $legacyPath -PathType Leaf) {
            Remove-Item -LiteralPath $legacyPath -Force
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

function Write-FakeModelEvent {
    param(
        [Parameter(Mandatory)][string]$Path,
        [string]$Model = 'gpt-5.6-sol'
    )

    $event = [ordered]@{
        timestamp = [DateTimeOffset]::UtcNow.ToString('o')
        type = 'turn_context'
        payload = [ordered]@{ model = $Model }
    }
    [IO.File]::AppendAllText(
        $Path,
        (($event | ConvertTo-Json -Depth 4 -Compress) + [Environment]::NewLine),
        [Text.UTF8Encoding]::new($false))
}

function Invoke-SelfTest {
    $testRoot = Join-Path ([IO.Path]::GetTempPath()) ('skullbonez-work-ledger-' + [Guid]::NewGuid().ToString('N'))
    $null = New-Item -ItemType Directory -Path $testRoot
    try {
        $plan = Join-Path $testRoot 'MASTER-PLAN.md'
        $ledger = Join-Path $testRoot 'WORK_LEDGER.csv'
        $mainSession = Join-Path $testRoot 'main.jsonl'
        $planWorkerSession = Join-Path $testRoot 'plan-worker.jsonl'
        $duckSession = Join-Path $testRoot 'duck.jsonl'
        $testPlan = @'
# Test master plan
Status: One active plan; 2/5 tasks complete

| Plan | Commit code | Tasks | Verified | Path |
|---|---|---:|---:|---|
| Test plan | `PLAN` | 3 | 1 | `TODO/test.md` |
'@
        [IO.File]::WriteAllText($plan, $testPlan, [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText($mainSession, '', [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText($planWorkerSession, '', [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText($duckSession, '', [Text.UTF8Encoding]::new($false))

        $validMessage = Join-Path $testRoot 'valid-commit-message.txt'
        $validBugMessage = Join-Path $testRoot 'valid-bug-commit-message.txt'
        $invalidMessage = Join-Path $testRoot 'invalid-commit-message.txt'
        $paddedMessage = Join-Path $testRoot 'padded-commit-message.txt'
        $validMessageText = @'
PLAN, TASK 2/3 - VERIFY COMMIT EVIDENCE

Why: Close the accepted plan slice while preserving the decision and its motivation in Git history.
Ownership: The plan owner retains the changed policy; no unrelated subsystem authority moved.
What: Record the implementation, focused tests, and integration metadata for the completed slice.
Validation: tools\validate_fast.bat passed with all required configurations and focused tests green.
Baselines/Artifacts: No baseline changed and no generated binary, vendor DLL, or TestOutput file is committed.
Review: Independent rubber-duck review returned CLEAN with zero findings after the final source change.
'@
        [IO.File]::WriteAllText($validMessage, $validMessageText, [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText($validBugMessage, $validMessageText.Replace('PLAN, TASK 2/3', 'BUG UI-001'), [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText($invalidMessage, "PLAN, TASK 2/3 - EMPTY BODY`n", [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText(
            $paddedMessage,
            "PLAN, TASK 2/3 - PAD PLACEHOLDERS`n`nWhy: x`nOwnership: x`nWhat: x`nValidation: x`nBaselines/Artifacts: x`nReview: x$('z' * 400)`n",
            [Text.UTF8Encoding]::new($false))
        Assert-OrchestratorCommitMessage -Path $validMessage
        Assert-OrchestratorCommitMessage -Path $validBugMessage
        $invalidRejected = $false
        try {
            Assert-OrchestratorCommitMessage -Path $invalidMessage
        } catch {
            $invalidRejected = $true
        }
        if (-not $invalidRejected) {
            throw 'Empty commit-body negative control unexpectedly succeeded.'
        }

        $paddedRejected = $false
        try {
            Assert-OrchestratorCommitMessage -Path $paddedMessage
        } catch {
            $paddedRejected = $true
        }
        if (-not $paddedRejected) {
            throw 'Padded placeholder commit-body negative control unexpectedly succeeded.'
        }

        $repo = Join-Path $testRoot 'repo'
        $null = New-Item -ItemType Directory -Path $repo
        & git -C $repo init --quiet
        if ($LASTEXITCODE -ne 0) { throw 'Could not initialize self-test Git repository.' }
        & git -C $repo config user.name 'Skullbonez Work Ledger Self Test'
        & git -C $repo config user.email 'work-ledger-self-test@example.invalid'
        [IO.File]::WriteAllText((Join-Path $repo 'fixture.txt'), 'fixture', [Text.UTF8Encoding]::new($false))
        & git -C $repo add fixture.txt
        & git -C $repo commit --quiet -F $validMessage
        if ($LASTEXITCODE -ne 0) { throw 'Could not create valid self-test commit.' }
        $head = (& git -C $repo rev-parse HEAD).Trim()
        Assert-OrchestratorCommitMessageContent -Content ((& git -C $repo show -s --format=%B $head) -join [Environment]::NewLine) -Context 'valid self-test commit'

        & git -C $repo commit --quiet --allow-empty -m 'PLAN, TASK 2/3 - INVALID EMPTY BODY'
        if ($LASTEXITCODE -ne 0) { throw 'Could not create invalid self-test commit.' }
        $invalidHead = (& git -C $repo rev-parse HEAD).Trim()
        $invalidCommitRejected = $false
        try {
            Assert-OrchestratorCommitMessageContent -Content ((& git -C $repo show -s --format=%B $invalidHead) -join [Environment]::NewLine) -Context 'invalid self-test commit'
        } catch {
            $invalidCommitRejected = $true
        }
        if (-not $invalidCommitRejected) {
            throw 'Post-commit empty-body negative control unexpectedly succeeded.'
        }
        $powershell = Join-Path $PSHOME 'powershell.exe'

        $null = & $powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath verify-commit-message -MessageFile $validMessage
        if ($LASTEXITCODE -ne 0) { throw 'Public commit-message action rejected the valid control.' }
        $savedErrorPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        $null = & $powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath verify-commit-message -MessageFile $invalidMessage 2>&1
        $invalidActionExitCode = $LASTEXITCODE
        $ErrorActionPreference = $savedErrorPreference
        if ($invalidActionExitCode -eq 0) { throw 'Public commit-message action accepted the empty-body control.' }

        Write-FakeModelEvent -Path $mainSession
        Write-FakeModelEvent -Path $planWorkerSession
        Write-FakeModelEvent -Path $duckSession
        Write-FakeTokenEvent -Path $mainSession -InputTokens 100 -CachedTokens 50 -OutputTokens 20
        $null = & $powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath start-goal -MasterPlanPath $plan -LedgerPath $ledger -MainSessionFile $mainSession -At '2026-08-18T09:00:00+10:00'
        if ($LASTEXITCODE -ne 0) { throw 'start-goal self-test failed.' }

        Write-FakeTokenEvent -Path $mainSession -InputTokens 150 -CachedTokens 70 -OutputTokens 30
        $null = & $powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath start-task -MasterPlanPath $plan -LedgerPath $ledger -Task 'PLAN-T1' -Title 'Test task' -Step implementation -Kind implementation -Label 'Implementation' -At '2026-08-18T09:01:00+10:00'
        if ($LASTEXITCODE -ne 0) { throw 'start-task self-test failed.' }

        Write-FakeTokenEvent -Path $mainSession -InputTokens 200 -CachedTokens 90 -OutputTokens 40
        $null = & $powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath show -MasterPlanPath $plan -LedgerPath $ledger -At '2026-08-18T09:05:00+10:00'
        if ($LASTEXITCODE -ne 0) { throw 'live show self-test failed.' }
        $liveStep = Import-Csv -LiteralPath $ledger | Where-Object { $_.record_type -eq 'step' -and $_.status -eq 'in_progress' }
        if ($liveStep.combined_input_tokens -ne '50' -or $liveStep.combined_output_tokens -ne '10' -or
            $liveStep.combined_cached_input_tokens -ne '20' -or $liveStep.elapsed_seconds -ne '240') {
            throw 'Live show did not refresh the open step counters and elapsed time.'
        }

        Write-FakeTokenEvent -Path $planWorkerSession -InputTokens 10 -CachedTokens 5 -OutputTokens 2
        $null = & $powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath start-task -MasterPlanPath $plan -LedgerPath $ledger -Task 'PLAN-T2' -Title 'Concurrent worker task' -Step implementation -Kind implementation -Label 'Concurrent implementation' -MainSessionFile $planWorkerSession -MainBaselineZero -At '2026-08-18T09:05:10+10:00'
        if ($LASTEXITCODE -ne 0) { throw 'concurrent start-task self-test failed.' }

        Write-FakeTokenEvent -Path $planWorkerSession -InputTokens 50 -CachedTokens 20 -OutputTokens 10
        $null = & $powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath show -MasterPlanPath $plan -LedgerPath $ledger -At '2026-08-18T09:06:00+10:00'
        if ($LASTEXITCODE -ne 0) { throw 'concurrent live show self-test failed.' }
        $liveTasks = @(Import-Csv -LiteralPath $ledger | Where-Object { $_.record_type -eq 'task' -and $_.status -eq 'in_progress' })
        if ($liveTasks.Count -ne 2) {
            throw 'Concurrent show did not preserve both independently active task rows.'
        }

        $null = & $powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath stop-task -MasterPlanPath $plan -LedgerPath $ledger -Task 'PLAN-T2' -Outcome 'handed off without a commit' -At '2026-08-18T09:06:10+10:00'
        if ($LASTEXITCODE -ne 0) { throw 'concurrent stop-task self-test failed.' }

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
        $ledgerBeforeFailure = [Convert]::ToBase64String([IO.File]::ReadAllBytes($ledger))
        $savedErrorPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        $null = & $powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath finish-task -MasterPlanPath $plan -LedgerPath $ledger -Task 'PLAN-T1' -Outcome 'must fail' -Commit $invalidHead -RepositoryRoot $repo.Trim() -SkipPushVerification -At '2026-08-18T09:32:00+10:00' 2>&1
        $invalidFinishExitCode = $LASTEXITCODE
        $ErrorActionPreference = $savedErrorPreference
        if ($invalidFinishExitCode -eq 0) { throw 'finish-task accepted an empty-body commit.' }
        $ledgerAfterFailure = [Convert]::ToBase64String([IO.File]::ReadAllBytes($ledger))
        if ($ledgerAfterFailure -ne $ledgerBeforeFailure) {
            throw 'Rejected finish-task commit-body control mutated the live ledger.'
        }

        $null = & $powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath finish-task -MasterPlanPath $plan -LedgerPath $ledger -Task 'PLAN-T1' -Outcome 'pushed' -Commit $head -RepositoryRoot $repo.Trim() -SkipPushVerification -At '2026-08-18T09:33:00+10:00'
        if ($LASTEXITCODE -ne 0) { throw 'finish-task self-test failed.' }

        Write-FakeTokenEvent -Path $mainSession -InputTokens 430 -CachedTokens 195 -OutputTokens 100
        $null = & $powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath finish-goal -MasterPlanPath $plan -LedgerPath $ledger -Outcome 'complete' -At '2026-08-18T09:34:00+10:00'
        if ($LASTEXITCODE -ne 0) { throw 'finish-goal self-test failed.' }

        $rows = @(Import-Csv -LiteralPath $ledger)
        $taskRow = @($rows | Where-Object { $_.record_type -eq 'task' -and $_.task_id -eq 'PLAN-T1' })
        $workerTaskRow = @($rows | Where-Object { $_.record_type -eq 'task' -and $_.task_id -eq 'PLAN-T2' })
        $duckRow = @($rows | Where-Object { $_.record_type -eq 'step' -and $_.step_id -eq 'rubber-duck-01' })
        $goalRow = @($rows | Where-Object { $_.record_type -eq 'goal' })
        $stateRow = @($rows | Where-Object { $_.record_type -eq 'state' })
        if ($taskRow.Count -ne 1 -or $workerTaskRow.Count -ne 1 -or $duckRow.Count -ne 1 -or
            $goalRow.Count -ne 1 -or $stateRow.Count -ne 1) {
            throw 'CSV ledger did not render one goal, both tasks, the duck step, and recovery-state row.'
        }
        if ($taskRow[0].combined_input_tokens -ne '370' -or
            $taskRow[0].combined_output_tokens -ne '85' -or
            $taskRow[0].combined_cached_input_tokens -ne '180' -or
            $taskRow[0].combined_api_cost_usd -ne '0.003590' -or
            $taskRow[0].duck_passes -ne '1' -or $taskRow[0].fix_cycles -ne '1' -or
            $taskRow[0].findings -ne '2' -or $taskRow[0].validation_seconds -ne '600' -or
            $taskRow[0].portfolio_percent_complete -ne '40.00' -or
            $taskRow[0].plan_percent_complete -ne '33.33' -or $taskRow[0].commit_hash -ne $head) {
            throw 'CSV task summary did not preserve usage, cost, review, progress, validation, and commit evidence.'
        }
        if ($duckRow[0].main_input_tokens -ne '20' -or $duckRow[0].main_output_tokens -ne '5' -or
            $duckRow[0].main_cached_input_tokens -ne '10' -or
            $duckRow[0].reviewer_input_tokens -ne '100' -or $duckRow[0].reviewer_output_tokens -ne '20' -or
            $duckRow[0].reviewer_cached_input_tokens -ne '60' -or
            $duckRow[0].combined_api_cost_usd -ne '0.001035' -or $duckRow[0].findings -ne '2') {
            throw 'CSV rubber-duck row did not preserve main/reviewer usage, cost, and findings.'
        }
        if ($workerTaskRow[0].main_input_tokens -ne '50' -or
            $workerTaskRow[0].main_output_tokens -ne '10' -or
            $workerTaskRow[0].main_cached_input_tokens -ne '20' -or
            $workerTaskRow[0].combined_api_cost_usd -ne '0.000460' -or
            -not [string]::IsNullOrWhiteSpace($workerTaskRow[0].commit_hash) -or
            $workerTaskRow[0].outcome -ne 'handed off without a commit') {
            throw 'CSV concurrent worker task did not preserve its independent primary-session usage.'
        }
        if ($goalRow[0].combined_input_tokens -ne '480' -or
            $goalRow[0].combined_output_tokens -ne '110' -or
            $goalRow[0].combined_cached_input_tokens -ne '225' -or
            $goalRow[0].combined_api_cost_usd -ne '0.004688' -or
            $goalRow[0].pricing_source -ne $script:PricingSource) {
            throw 'CSV goal row did not preserve complete usage, cost, and pricing provenance.'
        }
        if ([string]::IsNullOrWhiteSpace($stateRow[0].state_base64)) {
            throw 'CSV recovery-state row is empty.'
        }

        Write-Output 'PASS: concurrent live CSV work-ledger tasks, no-commit handoff, transitions, progress, API cost, reviewer accounting, findings, validation timing, and commit attribution.'
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

if ($Action -eq 'verify-commit-message') {
    if ([string]::IsNullOrWhiteSpace($MessageFile)) {
        throw 'verify-commit-message requires -MessageFile.'
    }
    Assert-OrchestratorCommitMessage -Path $MessageFile
    Write-Output "PASS: orchestrator commit message preserves required rationale and evidence: $([IO.Path]::GetFullPath($MessageFile))"
    exit 0
}

Invoke-LedgerAction
