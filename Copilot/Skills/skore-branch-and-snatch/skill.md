---
name: skore-branch-and-snatch
description: Checkout another branch, build it (fixing errors), run the full pipeline to capture perf data, save results to session state, then switch back to main and compare against the baseline.
---

## Branch-and-Snatch Skill

Checks out another branch, builds it, runs the full pipeline to capture performance data, saves the perf artifacts to Copilot session state, then returns to main and runs a comparison against baseline-001. The branch gets compilation fixes committed but all pipeline output is discarded.

### Prerequisites

- Must be on `main` branch before starting
- Profile build configuration must be available
- `perf_compare.py` and `analyze_perf.py` must exist in `Copilot/Skills/skore-render-test/`

### Step 1: Pull latest

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
cd $REPO
git pull
```

### Step 2: Ask the user which branch to snatch

Use the `ask_user` tool to ask which branch. List remote branches for them to choose from:

```pwsh
git branch -r --no-merged main | ForEach-Object { $_.Trim() -replace '^origin/', '' } | Where-Object { $_ -ne 'HEAD' }
```

Then checkout the chosen branch:

```pwsh
git checkout <branch-name>
```

### Step 3: Build the branch (fix errors)

Build with Profile configuration using the `skore-build` skill approach. Fix any compilation errors — these are likely merge drift or missing changes on the branch.

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$proc = Get-Process SKULLBONEZ_CORE -ErrorAction SilentlyContinue
if ($proc) { Stop-Process -Id $proc.Id -Force; Start-Sleep 1 }

$msbuild = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
& $msbuild "$REPO\SKULLBONEZ_CORE.sln" /p:Configuration=Profile /p:Platform=x64 /nologo /v:minimal
```

If the build fails, fix the errors and rebuild. Keep track of which source files you change — these are the compilation fixes that will be committed in Step 6.

### Step 4: Run the full pipeline and save perf data

Run the pipeline steps: render test, perf test, analyze. **The key output is the perf CSV and JSON artifact.**

#### 4a. Render test (note results but don't block on failure)

Run the `skore-render-test` skill steps 1–3. Note pass/fail but continue regardless — we're here for perf data.

#### 4b. Perf test

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$proc = Get-Process SKULLBONEZ_CORE -ErrorAction SilentlyContinue
if ($proc) { Stop-Process -Id $proc.Id -Force; Start-Sleep 1 }
Remove-Item "$REPO\Profile\perf_log.csv" -ErrorAction SilentlyContinue

$proc = Start-Process "$REPO\Profile\SKULLBONEZ_CORE.exe" `
    -ArgumentList "--scene SkullbonezData/scenes/perf_test.scene" `
    -WorkingDirectory $REPO -PassThru
$proc.WaitForExit(30000) | Out-Null
if (!$proc.HasExited) { Stop-Process -Id $proc.Id -Force; Write-Host "FAIL: perf test timed out" }

if (Test-Path "$REPO\Profile\perf_log.csv") {
    Write-Host "PASS: perf_log.csv generated"
    py "$REPO\Copilot\Skills\skore-render-test\analyze_perf.py"
} else {
    Write-Host "FAIL: No perf_log.csv — cannot snatch perf data"
    # ABORT: Cannot continue without perf data
}
```

#### 4c. Save perf artifacts to Copilot session state

Copy both the raw CSV and the JSON artifact to the session `files/` directory. These survive branch switches and git operations.

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$SESSION_DIR = "<Copilot session files/ directory>"  # Agent: use your session state files/ path

# Save the raw CSV
Copy-Item "$REPO\Profile\perf_log.csv" "$SESSION_DIR\snatched_perf_log.csv"

# Save the JSON artifact (find the most recent one)
$json = Get-ChildItem "$REPO\TestOutput\perf_history\*.json" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($json) { Copy-Item $json.FullName "$SESSION_DIR\snatched_perf.json" }

# Note which branch was snatched
$branch = git branch --show-current
Set-Content "$SESSION_DIR\snatched_branch.txt" $branch
Write-Host "Saved perf data from branch: $branch"
```

### Step 5: Smoke test (optional, note result)

Run the legacy smoke test from the pipeline. Note pass/fail but continue.

### Step 6: Commit compilation fixes only

Stage **only** the source files you modified to fix build errors. Do not stage any pipeline output (screenshots, baselines, perf artifacts, archives).

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
cd $REPO

# Stage ONLY compilation fix files (list them explicitly)
git add <file1.cpp> <file2.h> ...

git commit -m "fix: compilation fixes for pipeline compatibility

Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

If there were no compilation fixes needed, skip this step.

### Step 7: Discard everything and switch to main

Discard all uncommitted changes and untracked files, then switch back to main.

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
cd $REPO
git checkout -- .
git clean -fd
git checkout main
```

### Step 8: Compare snatched perf data against main baseline

Copy the snatched CSV from session state back to `Profile/perf_log.csv`, then run `perf_compare.py` which compares it against `TestOutput/baseline-001/perf.json`.

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$SESSION_DIR = "<Copilot session files/ directory>"  # Agent: use your session state files/ path

# Restore the snatched CSV so perf_compare.py can read it
Copy-Item "$SESSION_DIR\snatched_perf_log.csv" "$REPO\Profile\perf_log.csv" -Force

# Read branch name for display
$branch = Get-Content "$SESSION_DIR\snatched_branch.txt"
Write-Host "Comparing snatched perf from branch: $branch  vs  baseline-001"

# Run comparison
py "$REPO\Copilot\Skills\skore-render-test\perf_compare.py"
```

The comparison table will show baseline-001 as "bas" and the snatched branch as "cur". Report the results to the user.

### Summary output

After Step 8, present the **full results** to the user. Do NOT summarise — print all of the following:

1. Which branch was snatched
2. Whether compilation fixes were needed (and committed)
3. Render test pass/fail (noted in Step 4a)
4. **The COMPLETE perf comparison table** — reproduce the entire `perf_compare.py` output (CPU table, GPU table, Memory table) in your response using emoji dots (🔵🟢🟡🔴) next to every delta cell. Do NOT abbreviate, omit rows, or summarise the table. Print every single row.
5. Whether any regressions were flagged
