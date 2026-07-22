<#
File: Agentic/Skills/orchestrator/scripts/resolve_nightrunner_branch.ps1
Purpose:
  Resolve and optionally select the one canonical Night Runner branch.

Mental model:
  Existing Night Runner branches win, including historical spellings. Otherwise
  one local-date name is computed, reused locally or from origin, or created
  from the current tip. Dry-run and self-test modes never modify Git state.

Glossary:
  Canonical branch: nightrunner-<ordinal-day>-<MMM>-<YY>.
  Legacy branch: Earlier prefix or date-first Night Runner spelling that must be
    reused when already checked out.

Invariants:
  - Branch recognition is case-insensitive; Git branch output is preserved.
  - English month abbreviations are uppercase and independent of host culture.
  - Apply mode verifies the selected branch before returning it.

Related:
  - Agentic/Skills/orchestrator/SKILL.md owns the orchestration workflow.
#>
[CmdletBinding()]
param(
    [datetime]$Date = (Get-Date),
    [switch]$Apply,
    [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-OrdinalDay {
    param([Parameter(Mandatory)][int]$Day)

    if ($Day -lt 1 -or $Day -gt 31) {
        throw "Day must be in the range 1..31; got $Day."
    }

    $suffix = if ($Day -in 11, 12, 13) {
        'th'
    } else {
        switch ($Day % 10) {
            1 { 'st' }
            2 { 'nd' }
            3 { 'rd' }
            default { 'th' }
        }
    }
    return "$Day$suffix"
}

function Get-CanonicalNightRunnerBranch {
    param([Parameter(Mandatory)][datetime]$LocalDate)

    $ordinalDay = Get-OrdinalDay -Day $LocalDate.Day
    $month = $LocalDate.ToString('MMM', [Globalization.CultureInfo]::InvariantCulture).ToUpperInvariant()
    $year = $LocalDate.ToString('yy', [Globalization.CultureInfo]::InvariantCulture)
    return "nightrunner-$ordinalDay-$month-$year"
}

function Test-NightRunnerBranch {
    param([Parameter(Mandatory)][string]$Branch)

    return $Branch -match '(?i)^(nightrunner|night-runner)(-|$)' -or
           $Branch -match '(?i)(^|-)night-runner$'
}

function Invoke-SelfTest {
    $ordinalCases = @{
        1 = '1st'; 2 = '2nd'; 3 = '3rd'; 4 = '4th'; 11 = '11th'; 12 = '12th'
        13 = '13th'; 21 = '21st'; 22 = '22nd'; 23 = '23rd'; 31 = '31st'
    }
    foreach ($entry in $ordinalCases.GetEnumerator()) {
        $actual = Get-OrdinalDay -Day $entry.Key
        if ($actual -ne $entry.Value) {
            throw "Ordinal self-test failed: day=$($entry.Key) expected=$($entry.Value) actual=$actual"
        }
    }

    $canonical = Get-CanonicalNightRunnerBranch -LocalDate ([datetime]::new(2026, 7, 22))
    if ($canonical -ne 'nightrunner-22nd-JUL-26') {
        throw "Canonical self-test failed: $canonical"
    }

    foreach ($recognized in @('nightrunner-14th-july', 'Night-Runner-22nd-JUL-26', '15th-of-July-Night-Runner')) {
        if (-not (Test-NightRunnerBranch -Branch $recognized)) {
            throw "Recognition self-test failed: $recognized"
        }
    }
    foreach ($rejected in @('main', 'feature-night-runner-tools', 'overnight-runner-22')) {
        if (Test-NightRunnerBranch -Branch $rejected) {
            throw "Rejection self-test failed: $rejected"
        }
    }

    Write-Output 'PASS: Night Runner branch naming and recognition self-tests.'
}

if ($SelfTest) {
    Invoke-SelfTest
    exit 0
}

$repositoryRoot = (& git rev-parse --show-toplevel 2>$null)
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($repositoryRoot)) {
    throw 'Night Runner resolver must run inside a Git worktree.'
}
Set-Location -LiteralPath $repositoryRoot

$currentBranch = (& git branch --show-current).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($currentBranch)) {
    throw 'Night Runner resolver requires a named current branch.'
}
if (Test-NightRunnerBranch -Branch $currentBranch) {
    Write-Output $currentBranch
    exit 0
}

$targetBranch = Get-CanonicalNightRunnerBranch -LocalDate $Date
if (-not $Apply) {
    Write-Output $targetBranch
    exit 0
}

& git show-ref --verify --quiet "refs/heads/$targetBranch"
if ($LASTEXITCODE -eq 0) {
    & git switch $targetBranch
} else {
    $originUrl = (& git remote get-url origin 2>$null)
    if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($originUrl)) {
        # Why: refresh the remote-tracking name before deciding to create. A
        # stale local ref must not produce a duplicate same-day branch.
        & git fetch --prune origin
        if ($LASTEXITCODE -ne 0) {
            throw "Could not refresh origin before resolving '$targetBranch'."
        }
    }
    & git show-ref --verify --quiet "refs/remotes/origin/$targetBranch"
    if ($LASTEXITCODE -eq 0) {
        & git switch --track -c $targetBranch "origin/$targetBranch"
    } else {
        & git switch -c $targetBranch
    }
}
if ($LASTEXITCODE -ne 0) {
    throw "Could not select Night Runner branch '$targetBranch'."
}

$selectedBranch = (& git branch --show-current).Trim()
if ($LASTEXITCODE -ne 0 -or $selectedBranch -ne $targetBranch) {
    throw "Night Runner branch verification failed: expected='$targetBranch' actual='$selectedBranch'."
}
Write-Output $selectedBranch
