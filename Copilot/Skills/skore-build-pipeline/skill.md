---
name: skore-build-pipeline
description: Standard development pipeline for SkullbonezCore. Build, run full test suite, update baselines, commit. Invoke after completing a code change to verify and commit it.
---

## Standard Build Pipeline

The full verify-and-commit pipeline after a code change. **Every step must pass before proceeding to the next.** Every commit MUST include updated reference images and performance test artifacts.

> ⛔ **YOU MUST RUN ALL STEPS (0 through 9) BEFORE COMMITTING.** Fixing a formatting error in Step 0 or a build error in Step 2 does NOT mean you can commit — continue through the full pipeline. The only time you may stop early is if the pipeline crashes or the exe cannot be produced.

The engine supports in-process scene sequencing — all tests run in a **single process launch** via `--suite`. The suite file `SkullbonezData/scenes/render_tests.suite` defines the scene order:

1. `water_ball_test.scene` — render regression screenshot
2. `legacy_smoke.scene` — render regression screenshot (300 balls, deterministic)
3. `perf_test.scene` — performance regression (300 balls, physics, 2×5s passes)

### Step 0: Verify Formatting

Check that all source files are properly formatted. **Any formatting violations must be fixed before proceeding.**

Uses `--dry-run -Werror` which exits non-zero and prints warnings for any file that would be changed. Do NOT compare clang-format stdout against file contents — PowerShell's pipeline mangles the output.

Line endings are enforced by `.gitattributes` at commit time — no runtime check needed.

> **If a `.vcxproj`, `.sln`, or `.vcxproj.filters` file ever appears with wrong line endings** (e.g. LF instead of CRLF), the fix is: `Remove-Item <file>; git checkout -- <file>`. `git checkout --` is a no-op when the file exists and matches the index; deleting it forces git to write a fresh copy applying the `eol=` smudge filter.

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

Build using the `skore-build` skill with **Profile** configuration. Must produce **0 errors and 0 warnings**.

```pwsh
$REPO    = (git rev-parse --show-toplevel).Trim()

$msbuild = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
& $msbuild "$REPO\SKULLBONEZ_CORE.sln" /p:Configuration=Profile /p:Platform=x64 /nologo /v:minimal
```

**If build fails with LNK1168** (exe locked): kill the running `SKULLBONEZ_CORE.exe` process first, then rebuild.

### Step 3: Run Tri-Renderer Test Suite

Runs test suite for **OpenGL, DirectX 11, and DirectX 12** renderers. Each produces separate artifacts:

**OpenGL artifacts:**
- `Profile/gl_screenshot.bmp` — water_ball_test GL render
- `Profile/gl_legacy_smoke.bmp` — legacy_smoke GL render  
- `Profile/gl_perf_log.csv` — GL performance data (2×5s passes)

**DirectX 11 artifacts:**
- `Profile/dx11_screenshot.bmp` — water_ball_test DX11 render
- `Profile/dx11_legacy_smoke.bmp` — legacy_smoke DX11 render
- `Profile/dx11_perf_log.csv` — DX11 performance data (2×5s passes)

**DirectX 12 artifacts:**
- `Profile/dx12_screenshot.bmp` — water_ball_test DX12 render
- `Profile/dx12_legacy_smoke.bmp` — legacy_smoke DX12 render
- `Profile/dx12_perf_log.csv` — DX12 performance data (2×5s passes)

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()

# Clean old artifacts
Remove-Item "$REPO\Profile\*screenshot.bmp" -ErrorAction SilentlyContinue
Remove-Item "$REPO\Profile\*legacy_smoke.bmp" -ErrorAction SilentlyContinue  
Remove-Item "$REPO\Profile\*perf_log.csv" -ErrorAction SilentlyContinue
Remove-Item "$REPO\dx12_validation.txt" -ErrorAction SilentlyContinue

Write-Host "=== Running OpenGL Suite ==="
$proc = Start-Process "$REPO\Profile\SKULLBONEZ_CORE.exe" `
    -ArgumentList "--suite SkullbonezData/scenes/render_tests.suite" `
    -WorkingDirectory $REPO -PassThru
$proc.WaitForExit(30000) | Out-Null

# Rename GL artifacts
if (Test-Path "$REPO\Profile\screenshot.bmp") { 
    Move-Item "$REPO\Profile\screenshot.bmp" "$REPO\Profile\gl_screenshot.bmp" 
}
if (Test-Path "$REPO\Profile\legacy_smoke.bmp") { 
    Move-Item "$REPO\Profile\legacy_smoke.bmp" "$REPO\Profile\gl_legacy_smoke.bmp" 
}
if (Test-Path "$REPO\Profile\perf_log.csv") { 
    Move-Item "$REPO\Profile\perf_log.csv" "$REPO\Profile\gl_perf_log.csv" 
}

Write-Host "=== Running DirectX 11 Suite ==="
$proc = Start-Process "$REPO\Profile\SKULLBONEZ_CORE.exe" `
    -ArgumentList "--renderer dx11 --suite SkullbonezData/scenes/render_tests.suite" `
    -WorkingDirectory $REPO -PassThru
$proc.WaitForExit(30000) | Out-Null

# Rename DX11 artifacts  
if (Test-Path "$REPO\Profile\screenshot.bmp") { 
    Move-Item "$REPO\Profile\screenshot.bmp" "$REPO\Profile\dx11_screenshot.bmp" 
}
if (Test-Path "$REPO\Profile\legacy_smoke.bmp") { 
    Move-Item "$REPO\Profile\legacy_smoke.bmp" "$REPO\Profile\dx11_legacy_smoke.bmp" 
}
if (Test-Path "$REPO\Profile\perf_log.csv") { 
    Move-Item "$REPO\Profile\perf_log.csv" "$REPO\Profile\dx11_perf_log.csv" 
}

Write-Host "=== Running DirectX 12 Suite ==="
$proc = Start-Process "$REPO\Profile\SKULLBONEZ_CORE.exe" `
    -ArgumentList "--renderer dx12 --suite SkullbonezData/scenes/render_tests.suite" `
    -WorkingDirectory $REPO -PassThru
$proc.WaitForExit(30000) | Out-Null

# Rename DX12 artifacts  
if (Test-Path "$REPO\Profile\screenshot.bmp") { 
    Move-Item "$REPO\Profile\screenshot.bmp" "$REPO\Profile\dx12_screenshot.bmp" 
}
if (Test-Path "$REPO\Profile\legacy_smoke.bmp") { 
    Move-Item "$REPO\Profile\legacy_smoke.bmp" "$REPO\Profile\dx12_legacy_smoke.bmp" 
}
if (Test-Path "$REPO\Profile\perf_log.csv") { 
    Move-Item "$REPO\Profile\perf_log.csv" "$REPO\Profile\dx12_perf_log.csv" 
}

# Verify all artifacts exist
$allOk = $true
$artifacts = @("gl_screenshot.bmp", "gl_legacy_smoke.bmp", "gl_perf_log.csv", 
               "dx11_screenshot.bmp", "dx11_legacy_smoke.bmp", "dx11_perf_log.csv",
               "dx12_screenshot.bmp", "dx12_legacy_smoke.bmp", "dx12_perf_log.csv")
foreach ($f in $artifacts) {
    if (Test-Path "$REPO\Profile\$f") {
        Write-Host "  $f OK"
    } else {
        Write-Host "  $f MISSING"
        $allOk = $false
    }
}

if ($allOk) {
    Write-Host "PASS: All tri-renderer artifacts produced"
} else {
    Write-Host "FAIL: Missing artifacts — debug with skore-cdb-debug skill"
    exit 1
}
```

**If the suite fails or crashes**: Debug with `skore-cdb-debug` skill using the same `--suite` command line.

### Step 3.5: Validate DX12 Clean Debug Log

The DX12 backend writes `dx12_validation.txt` to the working directory at shutdown. The last line contains the error count. **This must be 0.**

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$valFile = "$REPO\dx12_validation.txt"

if (-not (Test-Path $valFile)) {
    Write-Host "FAIL: dx12_validation.txt not found — DX12 did not run or Shutdown() was not called"
    exit 1
}

$lines = Get-Content $valFile
$lastLine = ($lines | Select-Object -Last 1).Trim()

if ($lastLine -eq "0") {
    Write-Host "PASS: DX12 debug layer reported 0 errors"
} else {
    Write-Host "FAIL: DX12 debug layer reported $lastLine errors:"
    $lines | Where-Object { $_ -ne "---" -and $_ -ne $lastLine } | ForEach-Object { Write-Host "  $_" }
    exit 1
}
```

**If this step fails**, the DX12 backend has validation errors. Fix them before proceeding — a clean debug layer log is mandatory for every commit.

### Step 4: Validate Render Output & Cross-Renderer Comparison

**4A: Compare against baselines** — each renderer vs its own baseline:

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$env:SKORE_REPO = $REPO
py -c "
import os
from PIL import Image
_r = os.environ['SKORE_REPO']
pairs = [
    (_r + r'\Profile\gl_screenshot.bmp', _r + r'\TestOutput\baselines\baseline_gl_water_ball_test.png', 'GL water_ball_test'),
    (_r + r'\Profile\gl_legacy_smoke.bmp', _r + r'\TestOutput\baselines\baseline_gl_legacy_smoke.png', 'GL legacy_smoke'),
    (_r + r'\Profile\dx11_screenshot.bmp', _r + r'\TestOutput\baselines\baseline_dx11_water_ball_test.png', 'DX11 water_ball_test'),  
    (_r + r'\Profile\dx11_legacy_smoke.bmp', _r + r'\TestOutput\baselines\baseline_dx11_legacy_smoke.png', 'DX11 legacy_smoke'),
    (_r + r'\Profile\dx12_screenshot.bmp', _r + r'\TestOutput\baselines\baseline_dx12_water_ball_test.png', 'DX12 water_ball_test'),  
    (_r + r'\Profile\dx12_legacy_smoke.bmp', _r + r'\TestOutput\baselines\baseline_dx12_legacy_smoke.png', 'DX12 legacy_smoke'),
]
all_pass = True
for cap, base, name in pairs:
    if not os.path.exists(base):
        print(f'  {name}: NO BASELINE (will create in Step 5)')
        continue
    if not os.path.exists(cap):
        print(f'  {name}: CAPTURE MISSING')
        all_pass = False
        continue
    a = Image.open(cap).convert('RGB')
    b = Image.open(base).convert('RGB')
    if a.size != b.size:
        print(f'  {name}: SIZE MISMATCH {a.size} vs {b.size}')
        all_pass = False
        continue
    diff = sum(abs(pa-pb) for pa,pb in zip(a.tobytes(), b.tobytes()))
    if diff == 0:
        print(f'  {name}: IDENTICAL')
    else:
        pixels = a.size[0]*a.size[1]
        avg = diff / (pixels*3)
        print(f'  {name}: DIFF avg={avg:.4f} per channel')
        if avg > 5.0:  # Major difference threshold
            all_pass = False
if all_pass:
    print('PASS: All baseline comparisons acceptable')
else:
    print('NOTE: Baselines differ significantly — will update in Step 5')
"
```

**4B: Cross-renderer comparison** — GL vs DX11 and GL vs DX12 visual parity:

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$env:SKORE_REPO = $REPO
py -c "
import os
from PIL import Image
_r = os.environ['SKORE_REPO']
cross_pairs = [
    (_r + r'\Profile\gl_screenshot.bmp', _r + r'\Profile\dx11_screenshot.bmp', 'water_ball_test GL vs DX11'),
    (_r + r'\Profile\gl_legacy_smoke.bmp', _r + r'\Profile\dx11_legacy_smoke.bmp', 'legacy_smoke GL vs DX11'),
    (_r + r'\Profile\gl_screenshot.bmp', _r + r'\Profile\dx12_screenshot.bmp', 'water_ball_test GL vs DX12'),
    (_r + r'\Profile\gl_legacy_smoke.bmp', _r + r'\Profile\dx12_legacy_smoke.bmp', 'legacy_smoke GL vs DX12'),
]
print('\\n=== Cross-Renderer Visual Parity ===')
parity_ok = True
for gl_cap, dx11_cap, name in cross_pairs:
    if not os.path.exists(gl_cap) or not os.path.exists(dx11_cap):
        print(f'  {name}: MISSING CAPTURES')
        parity_ok = False
        continue
    a = Image.open(gl_cap).convert('RGB')
    b = Image.open(dx11_cap).convert('RGB')
    if a.size != b.size:
        print(f'  {name}: SIZE MISMATCH {a.size} vs {b.size}')
        parity_ok = False
        continue
    diff = sum(abs(pa-pb) for pa,pb in zip(a.tobytes(), b.tobytes()))
    if diff == 0:
        print(f'  {name}: IDENTICAL')
    else:
        pixels = a.size[0]*a.size[1]
        avg = diff / (pixels*3)
        print(f'  {name}: DIFF avg={avg:.4f} per channel')
        if avg > 10.0:  # Cross-renderer tolerance is higher (shadows disabled for DX11)
            parity_ok = False
if parity_ok:
    print('PASS: Cross-renderer visual parity acceptable')
else:
    print('WARNING: Significant cross-renderer differences detected')
"
```

**If render test fails**: Convert screenshots to PNG and send them to the model via the `view` tool for LLM visual comparison. **Only send PNGs to the model if local pixel comparison fails** — do not waste context on images that pass. If the change intentionally alters rendering, update baselines in Step 5. If unintentional, investigate and fix before proceeding.

### Step 5: Update Reference Images

**Mandatory for every commit.** Capture fresh baselines from all three renderers:

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$env:SKORE_REPO = $REPO
py -c "
import os
from PIL import Image
_r = os.environ['SKORE_REPO']
_b = _r + r'\TestOutput\baselines'

# Create baselines dir if needed
os.makedirs(_b, exist_ok=True)

# OpenGL baselines
if os.path.exists(_r + r'\Profile\gl_screenshot.bmp'):
    Image.open(_r + r'\Profile\gl_screenshot.bmp').save(_b + r'\baseline_gl_water_ball_test.png')
    print('Updated baseline_gl_water_ball_test.png')
if os.path.exists(_r + r'\Profile\gl_legacy_smoke.bmp'):
    Image.open(_r + r'\Profile\gl_legacy_smoke.bmp').save(_b + r'\baseline_gl_legacy_smoke.png')
    print('Updated baseline_gl_legacy_smoke.png')

# DirectX 11 baselines  
if os.path.exists(_r + r'\Profile\dx11_screenshot.bmp'):
    Image.open(_r + r'\Profile\dx11_screenshot.bmp').save(_b + r'\baseline_dx11_water_ball_test.png')
    print('Updated baseline_dx11_water_ball_test.png')
if os.path.exists(_r + r'\Profile\dx11_legacy_smoke.bmp'):
    Image.open(_r + r'\Profile\dx11_legacy_smoke.bmp').save(_b + r'\baseline_dx11_legacy_smoke.png')
    print('Updated baseline_dx11_legacy_smoke.png')

# DirectX 12 baselines  
if os.path.exists(_r + r'\Profile\dx12_screenshot.bmp'):
    Image.open(_r + r'\Profile\dx12_screenshot.bmp').save(_b + r'\baseline_dx12_water_ball_test.png')
    print('Updated baseline_dx12_water_ball_test.png')
if os.path.exists(_r + r'\Profile\dx12_legacy_smoke.bmp'):
    Image.open(_r + r'\Profile\dx12_legacy_smoke.bmp').save(_b + r'\baseline_dx12_legacy_smoke.png')
    print('Updated baseline_dx12_legacy_smoke.png')

print('All baselines updated')
"
```

This ensures baselines always reflect the latest committed state for all three renderers.

### Step 6: Analyze Performance & Compare Against Prior Commit

**Mandatory for every commit.** Determines the archive dir, writes renderer-named JSON artifacts, then compares each renderer against the nearest prior archive that has a matching artifact.

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$commit = (git rev-parse --short HEAD).Trim()

# Determine sequence number (max existing + 1; handle gaps and duplicates safely)
$allDirs = @(Get-ChildItem "$REPO\TestOutput" -Directory |
    Where-Object { $_.Name -match '^\d+_' } |
    Sort-Object { [int]($_.Name -split '_',2)[0] })
$maxSeq = if ($allDirs.Count -gt 0) { [int](($allDirs[-1].Name -split '_',2)[0]) } else { 0 }

# Re-use archive if this commit already has one (pipeline re-run scenario)
$existingForCommit = $allDirs | Where-Object { ($_.Name -split '_',2)[1] -eq $commit }
if ($existingForCommit) {
    $archiveDir = $existingForCommit[0].FullName
    Write-Host "Re-using archive: $($existingForCommit[0].Name)"
} else {
    $seq = $maxSeq + 1
    $archiveDir = "$REPO\TestOutput\$("{0:D3}" -f $seq)_$commit"
    New-Item -ItemType Directory -Path $archiveDir | Out-Null
    Write-Host "Created archive: $(Split-Path $archiveDir -Leaf)"
}

# Analyze all three renderers — writes {renderer}_perf.json into archive dir
Write-Host "`n=== GL Perf Analysis ==="
py "$REPO\Copilot\Skills\skore-render-test\analyze_perf.py" `
    --renderer gl `
    --csv "$REPO\Profile\gl_perf_log.csv" `
    --out-dir $archiveDir
if ($LASTEXITCODE -ne 0) { Write-Host "FAIL: GL perf analysis failed"; exit 1 }

Write-Host "`n=== DX11 Perf Analysis ==="
py "$REPO\Copilot\Skills\skore-render-test\analyze_perf.py" `
    --renderer dx11 `
    --csv "$REPO\Profile\dx11_perf_log.csv" `
    --out-dir $archiveDir
if ($LASTEXITCODE -ne 0) { Write-Host "FAIL: DX11 perf analysis failed"; exit 1 }

Write-Host "`n=== DX12 Perf Analysis ==="
py "$REPO\Copilot\Skills\skore-render-test\analyze_perf.py" `
    --renderer dx12 `
    --csv "$REPO\Profile\dx12_perf_log.csv" `
    --out-dir $archiveDir
if ($LASTEXITCODE -ne 0) { Write-Host "FAIL: DX12 perf analysis failed"; exit 1 }

# Compare each renderer against the nearest prior archive that has its JSON
# Walk backward through sorted archives, skip same-commit entries
foreach ($renderer in @("gl", "dx11", "dx12")) {
    $prevJson = $null
    foreach ($dir in ($allDirs | Sort-Object { [int]($_.Name -split '_',2)[0] } -Descending)) {
        $dirCommit = ($dir.Name -split '_',2)[1]
        if ($dirCommit -eq $commit) { continue }   # skip same-commit archives
        $candidate = "$($dir.FullName)\${renderer}_perf.json"
        if (Test-Path $candidate) { $prevJson = $candidate; break }
    }

    $currentJson = "$archiveDir\${renderer}_perf.json"
    if ($prevJson) {
        Write-Host "`n=== $($renderer.ToUpper()) Perf Comparison ==="
        py "$REPO\Copilot\Skills\skore-render-test\perf_compare.py" `
            --current $currentJson `
            --previous $prevJson
        if ($LASTEXITCODE -ne 0) {
            Write-Host "FAIL: $($renderer.ToUpper()) perf regression detected — investigate before committing"
            exit 1
        }
    } else {
        Write-Host "`n$($renderer.ToUpper()): No prior archive with ${renderer}_perf.json found — skipping compare (first run for this renderer)"
    }
}
```

**⚠️ MANDATORY: Print the COMPLETE output for ALL THREE renderers — every row, every emoji dot, the full CPU table, GPU table, and Memory table. Do NOT summarize, truncate, or paraphrase.**

**If regression thresholds are exceeded**, investigate before proceeding.

Regression thresholds: avg/p50 timing >10%, p99/p99.9 >20%, memory >5 MB.

### Step 7: Archive Screenshots to TestOutput

Screenshots are copied into the same archive dir created in Step 6. Perf JSONs are already there.

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$commit = (git rev-parse --short HEAD).Trim()

# Find the archive dir for this commit
$archiveDir = Get-ChildItem "$REPO\TestOutput" -Directory |
    Where-Object { ($_.Name -split '_',2)[1] -eq $commit } |
    Sort-Object { [int]($_.Name -split '_',2)[0] } |
    Select-Object -Last 1 -ExpandProperty FullName

if (-not $archiveDir) { Write-Host "FAIL: Archive dir not found for commit $commit"; exit 1 }

$env:SKORE_ARCHIVE = $archiveDir
$env:SKORE_REPO    = $REPO
py -c "
import os, shutil
from pathlib import Path
from PIL import Image

_r = Path(os.environ['SKORE_REPO'])
archive = Path(os.environ['SKORE_ARCHIVE'])

for src, dst in [
    (_r / 'Profile' / 'gl_screenshot.bmp',   archive / 'gl_water_ball_test.png'),
    (_r / 'Profile' / 'gl_legacy_smoke.bmp', archive / 'gl_legacy_smoke.png'),
    (_r / 'Profile' / 'dx11_screenshot.bmp',   archive / 'dx11_water_ball_test.png'),
    (_r / 'Profile' / 'dx11_legacy_smoke.bmp', archive / 'dx11_legacy_smoke.png'),
    (_r / 'Profile' / 'dx12_screenshot.bmp',   archive / 'dx12_water_ball_test.png'),
    (_r / 'Profile' / 'dx12_legacy_smoke.bmp', archive / 'dx12_legacy_smoke.png'),
]:
    if src.exists():
        Image.open(str(src)).save(str(dst))
        print(f'  Archived {dst.name}')

# Copy perf CSVs alongside the JSONs
for csv_name in ['gl_perf_log.csv', 'dx11_perf_log.csv', 'dx12_perf_log.csv']:
    src = _r / 'Profile' / csv_name
    if src.exists():
        shutil.copy2(str(src), str(archive / csv_name))
        print(f'  Archived {csv_name}')

print(f'Archive complete: {archive.name}')
"
if ($LASTEXITCODE -ne 0) { Write-Host "FAIL: Archive step failed"; exit 1 }
```


### Step 8: Lines of Code

Informational — counts logical LOC (excludes blanks and comments) across all `.h` and `.cpp` files in `SkullbonezSource/`. No pass/fail; just print and note the total in the commit message.

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
py "$REPO\Copilot\Skills\loc_count.py"
```

### Step 9: Confirm Commit

**Unless the user explicitly said "commit after pipeline succeeds" (or equivalent) when invoking the pipeline**, you MUST stop here and show the proposed commit message, then use the `ask_user` tool to ask whether to proceed.

Show the user:
- The proposed commit message
- A summary of what changed (files staged, LOC delta)

Then ask:

```
Use ask_user tool:
  question: "Pipeline passed. Commit with the message above?"
  choices: ["Yes, commit and push", "Yes, commit (no push)", "No, hold off"]
```

Only proceed to Step 10 if the user confirms. If they say no, stop and leave everything staged but uncommitted.

**Skip this step** (go straight to Step 10) **only if** the user's original request contained explicit commit intent, such as:
- "commit after pipeline succeeds"
- "run pipeline and commit"
- "build, test, and push"

### Step 10: Commit

Only if **all** previous steps pass and commit is confirmed. The commit MUST include:
- Code changes
- Updated reference images (from Step 5)
- Performance test artifact (from Step 6)
- TestOutput archive (from Step 7)

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
cd $REPO
git add -A
git commit -m "<descriptive message>

Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```
