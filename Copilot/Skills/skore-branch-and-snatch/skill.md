---
name: skore-branch-and-snatch
description: Checkout another branch, build it (fixing errors), run the full pipeline to capture perf data, save results to session state, then switch back to main and compare against the baseline.
---

## Branch-and-Snatch Skill

Checks out another branch, builds it, runs the perf suite (both GL and DX11), saves the perf JSON artifacts to Copilot session state, then returns to main and compares the snatched JSONs against the most recent matching archive on main. The branch gets compilation fixes committed but all pipeline output is discarded.

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

$msbuild = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
& $msbuild "$REPO\SKULLBONEZ_CORE.sln" /p:Configuration=Profile /p:Platform=x64 /nologo /v:minimal
```

If the build fails with LNK1168 (exe locked), kill the running process first, then rebuild.

### Step 4: Run the full pipeline and save perf data

Run the pipeline steps: render test, perf test, analyze. **The key output is the perf CSV and JSON artifact.**

#### 4a. Render test (note results but don't block on failure)

Run the `skore-render-test` skill steps 1–3. Note pass/fail but continue regardless — we're here for perf data.

#### 4b. Perf test

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$SESSION_DIR = "<Copilot session files/ directory>"  # Agent: use your session state files/ path

# Clean old CSVs
Remove-Item "$REPO\Profile\gl_perf_log.csv"   -ErrorAction SilentlyContinue
Remove-Item "$REPO\Profile\dx11_perf_log.csv" -ErrorAction SilentlyContinue

# GL pass
$proc = Start-Process "$REPO\Profile\SKULLBONEZ_CORE.exe" `
    -ArgumentList "--suite SkullbonezData/scenes/render_tests.suite" `
    -WorkingDirectory $REPO -PassThru
$proc.WaitForExit(30000) | Out-Null
if (Test-Path "$REPO\Profile\perf_log.csv") {
    Move-Item "$REPO\Profile\perf_log.csv" "$REPO\Profile\gl_perf_log.csv"
    Write-Host "PASS: gl_perf_log.csv generated"
} else {
    Write-Host "FAIL: No gl_perf_log.csv — cannot snatch perf data"
    # ABORT
}

# DX11 pass
$proc = Start-Process "$REPO\Profile\SKULLBONEZ_CORE.exe" `
    -ArgumentList "--renderer dx11 --suite SkullbonezData/scenes/render_tests.suite" `
    -WorkingDirectory $REPO -PassThru
$proc.WaitForExit(30000) | Out-Null
if (Test-Path "$REPO\Profile\perf_log.csv") {
    Move-Item "$REPO\Profile\perf_log.csv" "$REPO\Profile\dx11_perf_log.csv"
    Write-Host "PASS: dx11_perf_log.csv generated"
} else {
    Write-Host "FAIL: No dx11_perf_log.csv — cannot snatch perf data"
    # ABORT
}

# Analyze both renderers into a temp dir
$snatched = "$SESSION_DIR\snatched"
New-Item -ItemType Directory -Force -Path $snatched | Out-Null
py "$REPO\Copilot\Skills\skore-render-test\analyze_perf.py" `
    --renderer gl   --csv "$REPO\Profile\gl_perf_log.csv"   --out-dir $snatched
py "$REPO\Copilot\Skills\skore-render-test\analyze_perf.py" `
    --renderer dx11 --csv "$REPO\Profile\dx11_perf_log.csv" --out-dir $snatched
```

#### 4c. Save perf artifacts to Copilot session state

The JSONs are already written to `$SESSION_DIR\snatched\` in step 4b. Save the branch name too.

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$SESSION_DIR = "<Copilot session files/ directory>"  # Agent: use your session state files/ path

# Note which branch was snatched
$branch = git branch --show-current
Set-Content "$SESSION_DIR\snatched_branch.txt" $branch
Write-Host "Saved perf data from branch: $branch"
Write-Host "  gl_perf.json and dx11_perf.json are in $SESSION_DIR\snatched\"
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

For each renderer, find the most recent `TestOutput/NNN_{commit}/` archive on main that contains the matching `{renderer}_perf.json`, then compare with `perf_compare.py`.

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$SESSION_DIR = "<Copilot session files/ directory>"  # Agent: use your session state files/ path

$branch = Get-Content "$SESSION_DIR\snatched_branch.txt"

foreach ($renderer in @("gl", "dx11")) {
    $snatched = "$SESSION_DIR\snatched\${renderer}_perf.json"
    if (-not (Test-Path $snatched)) {
        Write-Host "${renderer}: snatched JSON not found — skipping"
        continue
    }

    # Find latest archive on main with this renderer's artifact
    $prevJson = Get-ChildItem "$REPO\TestOutput" -Directory |
        Where-Object { $_.Name -match '^\d+_' } |
        Sort-Object { [int]($_.Name -split '_',2)[0] } -Descending |
        ForEach-Object { "$($_.FullName)\${renderer}_perf.json" } |
        Where-Object { Test-Path $_ } |
        Select-Object -First 1

    if ($prevJson) {
        Write-Host "`n=== $($renderer.ToUpper()) — branch '$branch' vs main ==="
        py "$REPO\Copilot\Skills\skore-render-test\perf_compare.py" `
            --current $snatched --previous $prevJson
    } else {
        Write-Host "${renderer}: No main archive with ${renderer}_perf.json — skipping compare"
    }
}
```

The comparison table shows the branch-under-test as "current" and the most recent main commit as "previous". Report the results to the user.

### Summary output

After Step 8, present the **full results** to the user. Do NOT summarise — print all of the following:

1. Which branch was snatched
2. Whether compilation fixes were needed (and committed)
3. Render test pass/fail (noted in Step 4a)
4. **The COMPLETE perf comparison table** — reproduce the entire `perf_compare.py` output (CPU table, GPU table, Memory table) in your response using emoji dots (🔵🟢🟡🔴) next to every delta cell. Do NOT abbreviate, omit rows, or summarise the table. Print every single row.
5. Whether any regressions were flagged
