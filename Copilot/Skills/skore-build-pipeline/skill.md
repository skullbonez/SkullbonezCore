---
name: skore-build-pipeline
description: Standard development pipeline for SkullbonezCore. Build, run full test suite, update baselines, commit. Invoke after completing a code change to verify and commit it.
---

## Standard Build Pipeline

The full verify-and-commit pipeline after a code change. **Every step must pass before proceeding to the next.** Every commit MUST include updated reference images and performance test artifacts.

> ⛔ **YOU MUST RUN ALL STEPS (0 through 9) BEFORE COMMITTING.** Fixing a formatting error in Step 0 or a build error in Step 2 does NOT mean you can commit — continue through the full pipeline. The only time you may stop early is if the pipeline crashes or the exe cannot be produced.

The engine supports in-process scene sequencing — all tests run in a **single process launch** via `--suite`. The suite file `SkullbonezData/scenes/render_tests.suite` runs: `water_ball_test.scene` (render screenshot), `legacy_smoke.scene` (render screenshot, 300 balls), `perf_test.scene` (2×5s passes).

### Step -1: Choose Test Scope

**Always ask this before starting**, unless the user already specified (e.g. "run render tests only").

Use the `ask_user` tool:
```
question: "Which tests should the pipeline run?"
choices:
  - "Both render + perf (full pipeline) (Recommended)"
  - "Both render + perf + physics bench (full + bench)"
  - "Both render + perf + physics regression (full + regression)"
  - "Both render + perf + bench + regression (full + all)"
  - "Render tests only (skip perf)"
  - "Perf tests only (skip render baselines)"
  - "None — format + build + commit only"
```

Record the answer as `$testScope` and apply these rules for the remaining steps:

| Scope | Step 3 suite file | Step 4 (baseline check) | Step 5 (update baselines) | Step 6 (perf analysis) | Step 6.5 (physics bench) | Step 6.75 (physics regression) |
|-------|-------------------|-------------------------|---------------------------|------------------------|--------------------------|--------------------------------|
| Both (full) | `render_tests.suite` | ✅ Run | ✅ Run | ✅ Run | ⏭️ Skip | ⏭️ Skip |
| Full + bench | `render_tests.suite` | ✅ Run | ✅ Run | ✅ Run | ✅ Run | ⏭️ Skip |
| Full + regression | `render_tests.suite` | ✅ Run | ✅ Run | ✅ Run | ⏭️ Skip | ✅ Run |
| Full + all | `render_tests.suite` | ✅ Run | ✅ Run | ✅ Run | ✅ Run | ✅ Run |
| Render only | `render_only.suite`* | ✅ Run | ✅ Run | ⏭️ Skip | ⏭️ Skip | ⏭️ Skip |
| Perf only | `perf_only.suite`* | ⏭️ Skip | ⏭️ Skip | ✅ Run | ⏭️ Skip | ⏭️ Skip |
| None | ⏭️ Skip steps 3–7 | ⏭️ Skip | ⏭️ Skip | ⏭️ Skip | ⏭️ Skip | ⏭️ Skip |

*If the named suite doesn't exist yet, use `render_tests.suite` for render-only and pass `--scene SkullbonezData/scenes/perf_test.scene` for perf-only.

When scope is **None**, jump directly from Step 2 (build) to Step 8 (LOC) then Step 9 (confirm commit). Steps 3–7 are skipped entirely — no suite run, no baselines, no perf artifacts. The commit message should note "no test artifacts (build-only commit)".

### Step 0: Verify Formatting

Uses `--dry-run -Werror` which exits non-zero for any file that would be changed. Do NOT compare clang-format stdout against file contents — PowerShell's pipeline mangles the output.

Line endings are enforced by `.gitattributes` at commit time — no runtime check needed.

> **If a `.vcxproj`, `.sln`, or `.vcxproj.filters` file ever appears with wrong line endings**, the fix is: `Remove-Item <file>; git checkout -- <file>`. `git checkout --` is a no-op when the file exists; deleting it forces git to write a fresh copy applying the `eol=` smudge filter.

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$clangfmt = "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\Llvm\x64\bin\clang-format.exe"
$files = @(Get-ChildItem "$REPO\SkullbonezSource\*.cpp") + @(Get-ChildItem "$REPO\SkullbonezSource\*.h")
$bad = @()

foreach ($f in $files) {
    & $clangfmt --dry-run -Werror $f.FullName 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { $bad += $f.Name }
}

if ($bad.Count -gt 0) {
    Write-Host "FAIL: $($bad.Count) files need formatting:"
    $bad | ForEach-Object { Write-Host "  $_" }
    Write-Host "Run Step 1 to auto-fix."
    exit 1
}

Write-Host "PASS: All $($files.Count) files are correctly formatted"
```

If this fails, proceed to Step 1 (Format) to auto-fix, then re-run Step 0. **After Step 0 passes, continue through the full pipeline — do NOT commit yet.**

### Step 1: Format (auto-fix)

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()

# Collapse multi-line param lists (script strips inline param comments to avoid
# them being merged into the middle of the collapsed line)
py "$REPO\Copilot\Skills\collapse_params.py"

# Apply clang-format in-place (Allman braces, spaces, LF line endings, etc.)
$clangfmt = "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\Llvm\x64\bin\clang-format.exe"
$files = @(Get-ChildItem "$REPO\SkullbonezSource\*.cpp") + @(Get-ChildItem "$REPO\SkullbonezSource\*.h")
foreach ($f in $files) { & $clangfmt -i $f.FullName }
Write-Host "Formatted $($files.Count) files"
```

**If clang-format is not found**: check VS2022 is installed with C++ LLVM tools.

### Step 2: Build

Build the **Profile** configuration. Must produce **0 errors and 0 warnings**.

```pwsh
$REPO    = (git rev-parse --show-toplevel).Trim()

$msbuild = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
& $msbuild "$REPO\SKULLBONEZ_CORE.sln" /p:Configuration=Profile /p:Platform=x64 /nologo /v:minimal
```

**If build fails with LNK1168** (exe locked): kill the running `SKULLBONEZ_CORE.exe` process first, then rebuild.

### Step 3: Run Tri-Renderer Test Suite

Runs the full suite for **OpenGL, DirectX 11, and DirectX 12**. Stdout/stderr are captured per renderer for Step 3.5 validation.

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()

# Clean old artifacts
Remove-Item "$REPO\Profile\*screenshot.bmp"  -ErrorAction SilentlyContinue
Remove-Item "$REPO\Profile\*legacy_smoke.bmp" -ErrorAction SilentlyContinue
Remove-Item "$REPO\Profile\*perf_log.csv"    -ErrorAction SilentlyContinue
Remove-Item "$REPO\dx12_validation.txt"       -ErrorAction SilentlyContinue
Remove-Item "$REPO\Profile\*_stdout.txt"      -ErrorAction SilentlyContinue
Remove-Item "$REPO\Profile\*_stderr.txt"      -ErrorAction SilentlyContinue

foreach ($renderer in @("gl", "dx11", "dx12")) {
    $rendererArgs = if ($renderer -eq "gl") {
        "--suite SkullbonezData/scenes/render_tests.suite"
    } else {
        "--renderer $renderer --suite SkullbonezData/scenes/render_tests.suite"
    }
    Write-Host "=== Running $($renderer.ToUpper()) Suite ==="
    $proc = Start-Process "$REPO\Profile\SKULLBONEZ_CORE.exe" `
        -ArgumentList $rendererArgs `
        -WorkingDirectory $REPO -PassThru `
        -RedirectStandardOutput "$REPO\Profile\${renderer}_stdout.txt" `
        -RedirectStandardError  "$REPO\Profile\${renderer}_stderr.txt"
    if (-not $proc.WaitForExit(120000)) {
        Write-Host "FAIL: $($renderer.ToUpper()) suite timed out after 120s"
        $proc.Kill(); exit 1
    }
    if ($proc.ExitCode -ne 0) {
        Write-Host "FAIL: $($renderer.ToUpper()) exited with code $($proc.ExitCode)"
        exit 1
    }
    foreach ($file in @("screenshot.bmp", "legacy_smoke.bmp", "perf_log.csv")) {
        if (Test-Path "$REPO\Profile\$file") {
            Move-Item "$REPO\Profile\$file" "$REPO\Profile\${renderer}_$file"
        }
    }
}

$missing = @()
foreach ($f in @("gl_screenshot.bmp","gl_legacy_smoke.bmp","gl_perf_log.csv",
                  "dx11_screenshot.bmp","dx11_legacy_smoke.bmp","dx11_perf_log.csv",
                  "dx12_screenshot.bmp","dx12_legacy_smoke.bmp","dx12_perf_log.csv")) {
    if (-not (Test-Path "$REPO\Profile\$f")) { $missing += $f }
}
if ($missing.Count -gt 0) {
    $missing | ForEach-Object { Write-Host "  MISSING: $_" }
    Write-Host "FAIL: Missing artifacts — debug with skore-cdb-debug skill"
    exit 1
}
Write-Host "PASS: All tri-renderer artifacts produced"
```

**If the suite fails or crashes**: Debug with `skore-cdb-debug` skill using the same `--suite` command line.

### Step 3.5: Validate Clean Run (All Renderers)

All renderers must produce no error or warning output to stdout/stderr. DX12 is also checked against its in-process InfoQueue validation file.

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$allClean = $true

foreach ($renderer in @("gl", "dx11", "dx12")) {
    $lines = @()
    foreach ($stream in @("stdout", "stderr")) {
        $f = "$REPO\Profile\${renderer}_${stream}.txt"
        if (Test-Path $f) { $lines += Get-Content $f }
    }
    $problems = $lines | Where-Object { $_ -match '(?i)\berror\b|\bwarning\b|\bfailed\b' }
    if ($problems) {
        Write-Host "FAIL [$($renderer.ToUpper())]: Unexpected output:"
        $problems | ForEach-Object { Write-Host "  $_" }
        $allClean = $false
    } else {
        Write-Host "PASS [$($renderer.ToUpper())]: Clean stdout/stderr"
    }
}

# DX12 also validated via in-process InfoQueue file
$valFile = "$REPO\dx12_validation.txt"
if (-not (Test-Path $valFile)) {
    Write-Host "FAIL [DX12]: dx12_validation.txt not found"
    $allClean = $false
} else {
    $lastLine = ((Get-Content $valFile) | Select-Object -Last 1).Trim()
    if ($lastLine -eq "0") {
        Write-Host "PASS [DX12]: InfoQueue reported 0 errors"
    } else {
        Write-Host "FAIL [DX12]: InfoQueue reported $lastLine validation errors:"
        Get-Content $valFile | Where-Object { $_ -ne "---" -and $_ -ne $lastLine } | ForEach-Object { Write-Host "  $_" }
        $allClean = $false
    }
}

if (-not $allClean) { exit 1 }
```

### Step 4: Validate Render Output & Cross-Renderer Comparison

Compares each renderer against its own baseline and checks cross-renderer visual parity.

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$env:SKORE_REPO = $REPO
py -c "
import os
from PIL import Image
_r = os.environ['SKORE_REPO']
_b = _r + r'\TestOutput\baselines'
_p = _r + r'\Profile'

print('=== Baseline Comparisons ===')
baseline_ok = True
for cap, base, name in [
    (_p + r'\gl_screenshot.bmp',    _b + r'\baseline_gl_water_ball_test.png',   'GL water_ball_test'),
    (_p + r'\gl_legacy_smoke.bmp',  _b + r'\baseline_gl_legacy_smoke.png',      'GL legacy_smoke'),
    (_p + r'\dx11_screenshot.bmp',  _b + r'\baseline_dx11_water_ball_test.png', 'DX11 water_ball_test'),
    (_p + r'\dx11_legacy_smoke.bmp',_b + r'\baseline_dx11_legacy_smoke.png',    'DX11 legacy_smoke'),
    (_p + r'\dx12_screenshot.bmp',  _b + r'\baseline_dx12_water_ball_test.png', 'DX12 water_ball_test'),
    (_p + r'\dx12_legacy_smoke.bmp',_b + r'\baseline_dx12_legacy_smoke.png',    'DX12 legacy_smoke'),
]:
    if not os.path.exists(base):
        print(f'  {name}: NO BASELINE (will create in Step 5)'); continue
    if not os.path.exists(cap):
        print(f'  {name}: CAPTURE MISSING'); baseline_ok = False; continue
    a, b = Image.open(cap).convert('RGB'), Image.open(base).convert('RGB')
    if a.size != b.size:
        print(f'  {name}: SIZE MISMATCH {a.size} vs {b.size}'); baseline_ok = False; continue
    avg = sum(abs(pa-pb) for pa,pb in zip(a.tobytes(), b.tobytes())) / (a.size[0]*a.size[1]*3)
    label = 'IDENTICAL' if avg == 0 else f'DIFF avg={avg:.4f}'
    if avg > 5.0: label += ' FAIL'; baseline_ok = False
    print(f'  {name}: {label}')
print('PASS: All baseline comparisons acceptable' if baseline_ok else 'NOTE: Significant differences — update baselines in Step 5 if intentional')

print()
print('=== Cross-Renderer Parity ===')
parity_ok = True
for a_path, b_path, name in [
    (_p + r'\gl_screenshot.bmp',   _p + r'\dx11_screenshot.bmp',   'water_ball_test GL vs DX11'),
    (_p + r'\gl_legacy_smoke.bmp', _p + r'\dx11_legacy_smoke.bmp', 'legacy_smoke GL vs DX11'),
    (_p + r'\gl_screenshot.bmp',   _p + r'\dx12_screenshot.bmp',   'water_ball_test GL vs DX12'),
    (_p + r'\gl_legacy_smoke.bmp', _p + r'\dx12_legacy_smoke.bmp', 'legacy_smoke GL vs DX12'),
]:
    if not os.path.exists(a_path) or not os.path.exists(b_path):
        print(f'  {name}: MISSING CAPTURES'); parity_ok = False; continue
    a, b = Image.open(a_path).convert('RGB'), Image.open(b_path).convert('RGB')
    if a.size != b.size:
        print(f'  {name}: SIZE MISMATCH {a.size} vs {b.size}'); parity_ok = False; continue
    avg = sum(abs(pa-pb) for pa,pb in zip(a.tobytes(), b.tobytes())) / (a.size[0]*a.size[1]*3)
    label = 'IDENTICAL' if avg == 0 else f'DIFF avg={avg:.4f}'
    if avg > 10.0: label += ' FAIL'; parity_ok = False
    print(f'  {name}: {label}')
print('PASS: Cross-renderer parity acceptable' if parity_ok else 'WARNING: Significant cross-renderer differences')
"
```

**If render test fails**: Convert screenshots to PNG and send via the `view` tool for LLM visual comparison. Only send PNGs if the local pixel comparison fails. If the change intentionally alters rendering, update baselines in Step 5; otherwise investigate and fix.

### Step 5: Update Reference Images

**Mandatory for every commit.**

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$env:SKORE_REPO = $REPO
py -c "
import os
from PIL import Image
_r = os.environ['SKORE_REPO']
_b = _r + r'\TestOutput\baselines'
os.makedirs(_b, exist_ok=True)
for src, dst in [
    (r'\Profile\gl_screenshot.bmp',    r'\baseline_gl_water_ball_test.png'),
    (r'\Profile\gl_legacy_smoke.bmp',  r'\baseline_gl_legacy_smoke.png'),
    (r'\Profile\dx11_screenshot.bmp',  r'\baseline_dx11_water_ball_test.png'),
    (r'\Profile\dx11_legacy_smoke.bmp',r'\baseline_dx11_legacy_smoke.png'),
    (r'\Profile\dx12_screenshot.bmp',  r'\baseline_dx12_water_ball_test.png'),
    (r'\Profile\dx12_legacy_smoke.bmp',r'\baseline_dx12_legacy_smoke.png'),
]:
    if os.path.exists(_r + src):
        Image.open(_r + src).save(_b + dst)
        print(f'  Updated {os.path.basename(_b + dst)}')
print('All baselines updated')
"
```

### Step 6: Generate & Display Performance Data

**Mandatory for every commit.**

Generate performance JSON artifacts and display the comparison tables without LLM analysis. User decides if regression is acceptable.

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$commit = (git rev-parse --short HEAD).Trim()

$allDirs = @(Get-ChildItem "$REPO\TestOutput" -Directory |
    Where-Object { $_.Name -match '^\d+_' } |
    Sort-Object { [int]($_.Name -split '_',2)[0] })

$existing = $allDirs | Where-Object { ($_.Name -split '_',2)[1] -eq $commit }
if ($existing) {
    $archiveDir = $existing[0].FullName
    Write-Host "Re-using archive: $($existing[0].Name)"
} else {
    $maxSeq = if ($allDirs.Count -gt 0) { [int](($allDirs[-1].Name -split '_',2)[0]) } else { 0 }
    $archiveDir = "$REPO\TestOutput\$("{0:D3}" -f ($maxSeq + 1))_$commit"
    New-Item -ItemType Directory -Path $archiveDir | Out-Null
    Write-Host "Created archive: $(Split-Path $archiveDir -Leaf)"
}

foreach ($renderer in @("gl", "dx11", "dx12")) {
    Write-Host "`n=== $($renderer.ToUpper()) Perf Analysis ==="
    py "$REPO\Copilot\Skills\skore-render-test\analyze_perf.py" `
        --renderer $renderer `
        --csv "$REPO\Profile\${renderer}_perf_log.csv" `
        --out-dir $archiveDir
    if ($LASTEXITCODE -ne 0) { Write-Host "FAIL: $($renderer.ToUpper()) perf analysis failed"; exit 1 }
}

# JSON artifacts are now written — delete the raw CSVs (large and redundant vs the JSON)
foreach ($renderer in @("gl", "dx11", "dx12")) {
    $csv = "$REPO\Profile\${renderer}_perf_log.csv"
    if (Test-Path $csv) { Remove-Item $csv; Write-Host "Deleted: ${renderer}_perf_log.csv" }
}
# Also purge any CSVs previously archived to TestOutput
Get-ChildItem "$REPO\TestOutput" -Recurse -Filter "*_perf_log.csv" | Remove-Item
Write-Host "Cleaned up archived CSVs from TestOutput"

# Display comparison tables for all three renderers without analysis
foreach ($renderer in @("gl", "dx11", "dx12")) {
    $prevJson = $null
    foreach ($dir in ($allDirs | Sort-Object { [int]($_.Name -split '_',2)[0] } -Descending)) {
        if (($dir.Name -split '_',2)[1] -eq $commit) { continue }
        $candidate = "$($dir.FullName)\${renderer}_perf.json"
        if (Test-Path $candidate) { $prevJson = $candidate; break }
    }
    # Fall back to committed baseline if no archive history exists
    if (-not $prevJson) {
        $candidate = "$REPO\TestOutput\baselines\${renderer}_perf.json"
        if (Test-Path $candidate) { $prevJson = $candidate }
    }
    $currentJson = "$archiveDir\${renderer}_perf.json"
    if ($prevJson) {
        Write-Host "`n=== $($renderer.ToUpper()) Perf Comparison ==="
        py "$REPO\Copilot\Skills\skore-render-test\perf_compare.py" `
            --current $currentJson --previous $prevJson
    } else {
        Write-Host "`n$($renderer.ToUpper()): No prior archive found — displaying current tables:"
        py -c "
import json
with open(r'$currentJson') as f:
    data = json.load(f)
    if 'tables' in data:
        for table_name, table_data in data['tables'].items():
            print(f'\n{table_name}:')
            print(table_data)
"
    }
}

# Ask user to confirm performance is acceptable before continuing
Write-Host "`n" + ("="*60)
Write-Host "Review the performance tables above."
Write-Host "="*60
```

After displaying all tables, **you MUST print both an improvements section AND a regressions section** before asking the user. Neither section is optional — if there are no entries in a section, write "None".

#### 📈 Improvements (MANDATORY — always shown)

Print every marker where avg OR p50 improved by more than the ramped threshold. Group by renderer. Format:

```
📈 IMPROVEMENTS
  GL   Frame/Text            avg -56.6%  p50 -54.8%
  GL   Frame/Render/Water    avg  -5.5%  p50  -3.4%
  DX11 Frame/Text            avg -20.5%  p50 -13.9%
  DX12 Frame/Text            avg  -9.5%  p50 -10.9%
```

If there are no improvements: print `📈 IMPROVEMENTS  None`

#### 📉 Regressions (MANDATORY — always shown)

Print every 🔴 or 🟡 marker. For each, show avg and p50 side-by-side, then a verdict:
- If avg regresses but p50 is stable/improving → label **"avg-only noise (stall)"**
- If both avg and p50 regress → label **"REAL REGRESSION — investigate"**

```
📉 REGRESSIONS
  GL   Frame/Input            avg +36.3%  p50  +1.5%  → avg-only noise (stall)
  DX11 Frame/Input            avg +20.1%  p50  -1.7%  → avg-only noise (stall)
  DX12 Frame/Input            avg +243.6% p50  -1.6%  → avg-only noise (stall)
```

If there are no regressions: print `📉 REGRESSIONS  None`

#### Summary sentence

End with one sentence: *"X improvements, Y regressions (Z real, W noise)."*

Then ask the user:
```
Use ask_user tool:
  question: "Performance summary above — acceptable to continue?"
  choices: ["Yes, continue", "No, abort commit"]
```

If user says "No", exit with code 1 to abort. Otherwise continue to Step 6.5 (or Step 7 if bench is not in scope).

### Step 6.5: Physics Benchmark (optional — only when scope includes bench)

Runs the 4-mode physics benchmark suite (GL renderer only — physics is CPU-side and
renderer-independent) and writes `physics_bench.json` to the archive directory.

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$commit = (git rev-parse --short HEAD).Trim()

# Locate archive dir created in Step 6
$archiveDir = Get-ChildItem "$REPO\TestOutput" -Directory |
    Where-Object { ($_.Name -split '_',2)[1] -eq $commit } |
    Sort-Object { [int]($_.Name -split '_',2)[0] } |
    Select-Object -Last 1 -ExpandProperty FullName
if (-not $archiveDir) { Write-Host "FAIL: Archive dir not found for $commit"; exit 1 }

# Clean old bench CSVs
Remove-Item "$REPO\Profile\*bench_perf_log.csv" -ErrorAction SilentlyContinue

# Run all 4 bench scenes (GL only — physics is renderer-independent)
Write-Host "=== Running physics_bench.suite ==="
$benchProc = Start-Process "$REPO\Profile\SKULLBONEZ_CORE.exe" `
    -ArgumentList "--suite SkullbonezData/scenes/physics_bench.suite" `
    -WorkingDirectory $REPO -PassThru `
    -RedirectStandardOutput "$REPO\Profile\bench_stdout.txt" `
    -RedirectStandardError  "$REPO\Profile\bench_stderr.txt"
$benchProcId = $benchProc.Id
$done = $benchProc.WaitForExit(300000)
if (-not $done) {
    Write-Host "FAIL: physics bench timed out"
    [System.Diagnostics.Process]::GetProcessById($benchProcId).Kill()
    exit 1
}
if ($benchProc.ExitCode -ne 0) {
    Write-Host "FAIL: bench suite exited $($benchProc.ExitCode)"
    Get-Content "$REPO\Profile\bench_stderr.txt" | Select-Object -Last 10
    exit 1
}

# Find previous physics_bench.json for delta comparison
$prevBenchJson = $null
$allDirs = @(Get-ChildItem "$REPO\TestOutput" -Directory |
    Where-Object { $_.Name -match '^\d+_' } |
    Sort-Object { [int]($_.Name -split '_',2)[0] })
foreach ($dir in ($allDirs | Sort-Object { [int]($_.Name -split '_',2)[0] } -Descending)) {
    if (($dir.Name -split '_',2)[1] -eq $commit) { continue }
    $candidate = "$($dir.FullName)\physics_bench.json"
    if (Test-Path $candidate) { $prevBenchJson = $candidate; break }
}

# Run report — writes physics_bench.json to archive and prints table
$benchArgs = "--out-dir `"$archiveDir`""
if ($prevBenchJson) { $benchArgs += " --previous `"$prevBenchJson`"" }
Write-Host "`n=== Physics Bench Report ==="
Invoke-Expression "py `"$REPO\Copilot\Skills\bench_report.py`" $benchArgs"
if ($LASTEXITCODE -ne 0) { Write-Host "FAIL: bench_report.py failed"; exit 1 }

# Clean up temp files
Remove-Item "$REPO\Profile\bench_stdout.txt","$REPO\Profile\bench_stderr.txt" -ErrorAction SilentlyContinue
Write-Host "PASS: physics_bench.json written to $(Split-Path $archiveDir -Leaf)"
```

### Step 6.75: Physics Regression Test (optional — only when scope includes regression)

Builds the Debug exe, runs both regression scenes, and diffs the output CSVs against committed baselines.
Physics logging is Debug-only (Log singleton is a no-op in Release/Profile). Both scenes use `fixed_step` + `seed 42` so output is **exactly** deterministic — any single differing byte is a real regression.

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()

# Build Debug (needed for physics logging)
Write-Host "=== Building Debug for physics regression ==="
$msbuild = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
& $msbuild "$REPO\SKULLBONEZ_CORE.sln" /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal
if ($LASTEXITCODE -ne 0) { Write-Host "FAIL: Debug build failed"; exit 1 }

# Clean old outputs
Remove-Item "$REPO\Debug\physics_regression_*.csv" -ErrorAction SilentlyContinue

# Run legacy regression scene
Write-Host "=== Running physics_regression_legacy ==="
$p = Start-Process "$REPO\Debug\SKULLBONEZ_CORE.exe" `
    -ArgumentList "--scene SkullbonezData/scenes/physics_regression_legacy.scene" `
    -WorkingDirectory $REPO -PassThru -NoNewWindow
$id = $p.Id
if (-not $p.WaitForExit(60000)) {
    Write-Host "FAIL: physics_regression_legacy timed out"
    [System.Diagnostics.Process]::GetProcessById($id).Kill(); exit 1
}
if ($p.ExitCode -ne 0) { Write-Host "FAIL: legacy scene exited $($p.ExitCode)"; exit 1 }

# Run solver regression scene
Write-Host "=== Running physics_regression_solver ==="
$p = Start-Process "$REPO\Debug\SKULLBONEZ_CORE.exe" `
    -ArgumentList "--scene SkullbonezData/scenes/physics_regression_solver.scene" `
    -WorkingDirectory $REPO -PassThru -NoNewWindow
$id = $p.Id
if (-not $p.WaitForExit(60000)) {
    Write-Host "FAIL: physics_regression_solver timed out"
    [System.Diagnostics.Process]::GetProcessById($id).Kill(); exit 1
}
if ($p.ExitCode -ne 0) { Write-Host "FAIL: solver scene exited $($p.ExitCode)"; exit 1 }

# Compare CSVs to baselines
$env:SKORE_REPO = $REPO
py -c "
import sys, os, shutil
_r = os.environ['SKORE_REPO']
baseline_dir = os.path.join(_r, 'TestOutput', 'baselines')

tests = [
    (r'Debug\physics_regression_legacy.csv', 'physics_regression_legacy.csv'),
    (r'Debug\physics_regression_solver.csv', 'physics_regression_solver.csv'),
]
all_pass = True
for output_rel, baseline_name in tests:
    output_path = os.path.join(_r, output_rel)
    baseline_path = os.path.join(baseline_dir, baseline_name)
    if not os.path.exists(output_path):
        print(f'  FAIL: {output_rel} not produced')
        all_pass = False
        continue
    if not os.path.exists(baseline_path):
        shutil.copy(output_path, baseline_path)
        with open(output_path) as f:
            lines = f.readlines()
        print(f'  BASELINE CREATED: {baseline_name} ({len(lines)} lines)')
        continue
    with open(output_path) as f:
        current = f.readlines()
    with open(baseline_path) as f:
        baseline = f.readlines()
    if current == baseline:
        print(f'  PASS: {baseline_name} ({len(current)} lines, exact match)')
    else:
        if len(current) != len(baseline):
            print(f'  FAIL: {baseline_name} row count {len(current)} vs baseline {len(baseline)}')
        else:
            diffs = [(i+1, b.rstrip(), c.rstrip()) for i,(b,c) in enumerate(zip(baseline,current)) if b!=c]
            print(f'  FAIL: {baseline_name} {len(diffs)} lines differ (first at line {diffs[0][0]}):')
            for lineno, b, c in diffs[:3]:
                print(f'    line {lineno}:')
                print(f'      baseline: {b}')
                print(f'      current:  {c}')
        all_pass = False
sys.exit(0 if all_pass else 1)
"
if ($LASTEXITCODE -ne 0) { Write-Host "FAIL: Physics regression test failed"; exit 1 }
Write-Host "PASS: Physics regression test passed"
```

**If regression fails**: The physics output changed. Investigate whether the change is intentional (physics bugfix, solver tuning) or a regression. If intentional, update the baselines by deleting the CSV files in `TestOutput/baselines/` and re-running — the script will recreate them.

**Note:** The Debug exe uses a window (same as Profile). It will open and close automatically when the scene finishes (`frames 300` + exit on completion).

### Step 7: Archive Screenshots to TestOutput

Screenshots are copied into the archive dir from Step 6. CSVs are not archived (deleted in Step 6 after JSON is written).

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$commit = (git rev-parse --short HEAD).Trim()

$archiveDir = Get-ChildItem "$REPO\TestOutput" -Directory |
    Where-Object { ($_.Name -split '_',2)[1] -eq $commit } |
    Sort-Object { [int]($_.Name -split '_',2)[0] } |
    Select-Object -Last 1 -ExpandProperty FullName

if (-not $archiveDir) { Write-Host "FAIL: Archive dir not found for $commit"; exit 1 }

$env:SKORE_ARCHIVE = $archiveDir
$env:SKORE_REPO    = $REPO
py -c "
import os
from pathlib import Path
from PIL import Image
_r = Path(os.environ['SKORE_REPO'])
archive = Path(os.environ['SKORE_ARCHIVE'])
for src, dst in [
    (_r/'Profile'/'gl_screenshot.bmp',    archive/'gl_water_ball_test.png'),
    (_r/'Profile'/'gl_legacy_smoke.bmp',  archive/'gl_legacy_smoke.png'),
    (_r/'Profile'/'dx11_screenshot.bmp',  archive/'dx11_water_ball_test.png'),
    (_r/'Profile'/'dx11_legacy_smoke.bmp',archive/'dx11_legacy_smoke.png'),
    (_r/'Profile'/'dx12_screenshot.bmp',  archive/'dx12_water_ball_test.png'),
    (_r/'Profile'/'dx12_legacy_smoke.bmp',archive/'dx12_legacy_smoke.png'),
]:
    if src.exists():
        Image.open(str(src)).save(str(dst))
        print(f'  {dst.name}')
print(f'Archive complete: {archive.name}')
"
if ($LASTEXITCODE -ne 0) { Write-Host "FAIL: Archive step failed"; exit 1 }
```

### Step 8: Lines of Code

Informational — logical LOC across `SkullbonezSource/` (excludes blanks and comments). Note the total in the commit message.

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
py "$REPO\Copilot\Skills\loc_count.py"
```

### Step 8.5: Update SessionState.md

**Mandatory before every commit.** `Copilot/SessionState.md` is the handoff document between sessions — it must reflect the commit about to be made.

Update the following fields:
- **Last commit**: set to the short SHA that will be created (use `git rev-parse --short HEAD` as a placeholder; amend after commit if needed)
- **Branch**: current branch name
- **Completed backlog items**: tick off anything finished in this session
- **Known bugs / notes**: add any new findings, remove resolved items
- **Uncommitted changes**: clear this section (everything is being committed)

Use the `edit` tool to update `Copilot/SessionState.md` directly. Do not skip this step even for small commits.

### Step 9: Confirm Commit

Show the proposed commit message and ask the user whether to proceed, **unless** the user explicitly said "commit" or "push" when invoking the pipeline.

Show the proposed commit message and a brief summary of what changed (files staged, LOC delta), then use the `ask_user` tool:

```
Use ask_user tool:
  question: "Pipeline passed. Commit with the message above?"
  choices: ["Yes, commit and push", "Yes, commit (no push)", "No, hold off"]
```

Only proceed to Step 10 if confirmed. If they say no, stop and leave everything staged but uncommitted.

### Step 10: Commit

Only if all previous steps pass and commit is confirmed. The commit MUST include code changes, updated reference images (Step 5), performance artifact (Step 6), and TestOutput archive (Step 7).

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
cd $REPO
git add -A
git commit -m "<descriptive message>

Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

### Step 11: Pipeline Summary Matrix

**MANDATORY — always print this after all steps complete, regardless of whether a commit was made.**

Print the following table with a ✅ for every cell (the pipeline exits on first failure, so reaching this step means everything passed):

```
╔══════════════════════════╦═════╦══════╦══════╗
║ Step                     ║ GL  ║ DX11 ║ DX12 ║
╠══════════════════════════╬═════╬══════╬══════╣
║ 0: Format                ║  ✅  ║  ✅   ║  ✅   ║
║ 2: Build                 ║  ✅  ║  ✅   ║  ✅   ║
║ 3: Suite Run             ║  ✅  ║  ✅   ║  ✅   ║
║ 3.5: Clean Log           ║  ✅  ║  ✅   ║  ✅   ║
║ 4: Visual/Baseline       ║  ✅  ║  ✅   ║  ✅   ║
║ 5: Ref Images            ║  ✅  ║  ✅   ║  ✅   ║
║ 6: Perf                  ║  ✅  ║  ✅   ║  ✅   ║
║ 6.5: Physics Bench       ║  ✅  ║  n/a  ║  n/a  ║
║ 6.75: Physics Regression ║  ✅  ║  n/a  ║  n/a  ║
║ 7: Archive               ║  ✅  ║  ✅   ║  ✅   ║
║ 8: LOC                   ║  ✅  ║  ✅   ║  ✅   ║
║ 8.5: SessionState        ║  ✅  ║  ✅   ║  ✅   ║
╚══════════════════════════╩═════╩══════╩══════╝
```

Note: Step 6.5 only appears in the matrix when scope includes physics bench. Print `(skipped)` in place of ✅ when not in scope.
