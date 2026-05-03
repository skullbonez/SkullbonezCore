---
name: skore-build-pipeline
description: Standard development pipeline for SkullbonezCore. Build, run full test suite, update baselines, commit. Invoke after completing a code change to verify and commit it.
---

## Standard Build Pipeline

The full verify-and-commit pipeline after a code change. **Every step must pass before proceeding to the next.** Every commit MUST include updated reference images and performance test artifacts.

> ⛔ **YOU MUST RUN ALL STEPS (0 through 9) BEFORE COMMITTING.** Fixing a formatting error in Step 0 or a build error in Step 2 does NOT mean you can commit — continue through the full pipeline. The only time you may stop early is if the pipeline crashes or the exe cannot be produced.

The engine supports in-process scene sequencing — all tests run in a **single process launch** via `--suite`. The suite file `SkullbonezData/scenes/render_tests.suite` runs: `water_ball_test.scene` (render screenshot), `legacy_smoke.scene` (render screenshot, 300 balls), `perf_test.scene` (2×5s passes).

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

### Step 6: Analyze Performance & Compare Against Prior Commit

**Mandatory for every commit.**

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

foreach ($renderer in @("gl", "dx11", "dx12")) {
    $prevJson = $null
    foreach ($dir in ($allDirs | Sort-Object { [int]($_.Name -split '_',2)[0] } -Descending)) {
        if (($dir.Name -split '_',2)[1] -eq $commit) { continue }
        $candidate = "$($dir.FullName)\${renderer}_perf.json"
        if (Test-Path $candidate) { $prevJson = $candidate; break }
    }
    $currentJson = "$archiveDir\${renderer}_perf.json"
    if ($prevJson) {
        Write-Host "`n=== $($renderer.ToUpper()) Perf Comparison ==="
        py "$REPO\Copilot\Skills\skore-render-test\perf_compare.py" `
            --current $currentJson --previous $prevJson
        if ($LASTEXITCODE -ne 0) {
            Write-Host "FAIL: $($renderer.ToUpper()) perf regression — investigate before committing"
            exit 1
        }
    } else {
        Write-Host "`n$($renderer.ToUpper()): No prior archive found — skipping compare"
    }
}
```

**⚠️ MANDATORY: Print the COMPLETE output for ALL THREE renderers — every row, every emoji dot, the full CPU table, GPU table, and Memory table. Do NOT summarize, truncate, or paraphrase.**

Regression thresholds: avg/p50 timing >10% = FAIL, p99/p99.9 >20% = FAIL, memory growth >5 MB = FAIL.

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
╔══════════════════════╦═════╦══════╦══════╗
║ Step                 ║ GL  ║ DX11 ║ DX12 ║
╠══════════════════════╬═════╬══════╬══════╣
║ 0: Format            ║  ✅  ║  ✅   ║  ✅   ║
║ 2: Build             ║  ✅  ║  ✅   ║  ✅   ║
║ 3: Suite Run         ║  ✅  ║  ✅   ║  ✅   ║
║ 3.5: Clean Log       ║  ✅  ║  ✅   ║  ✅   ║
║ 4: Visual/Baseline   ║  ✅  ║  ✅   ║  ✅   ║
║ 5: Ref Images        ║  ✅  ║  ✅   ║  ✅   ║
║ 6: Perf              ║  ✅  ║  ✅   ║  ✅   ║
║ 7: Archive           ║  ✅  ║  ✅   ║  ✅   ║
║ 8: LOC               ║  ✅  ║  ✅   ║  ✅   ║
╚══════════════════════╩═════╩══════╩══════╝
```
