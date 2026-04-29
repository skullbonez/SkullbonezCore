---
name: skore-build-pipeline
description: Standard development pipeline for SkullbonezCore. Build, run full test suite, update baselines, commit. Invoke after completing a code change to verify and commit it.
---

## Standard Build Pipeline

The full verify-and-commit pipeline after a code change. **Every step must pass before proceeding to the next.** Every commit MUST include updated reference images and performance test artifacts.

The engine supports in-process scene sequencing — all tests run in a **single process launch** via `--suite`. The suite file `SkullbonezData/scenes/render_tests.suite` defines the scene order:

1. `water_ball_test.scene` — render regression screenshot
2. `legacy_smoke.scene` — render regression screenshot (300 balls, deterministic)
3. `perf_test.scene` — performance regression (300 balls, physics, 2×5s passes)

### Step 0: Verify Formatting

Check that all source files are properly formatted. **Any formatting violations must be fixed before proceeding.**

Uses `--dry-run -Werror` which exits non-zero and prints warnings for any file that would be changed. Do NOT compare clang-format stdout against file contents — PowerShell's pipeline mangles the output.

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

If this fails, proceed to Step 1 (Format) to auto-fix, then re-run Step 0.

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

### Step 3: Run Full Test Suite

Launches all test scenes in a **single process** via `--suite`. This produces:
- `Profile/screenshot.bmp` — water_ball_test render capture
- `Profile/legacy_smoke.bmp` — legacy_smoke render capture (overwritten by final smoke run)
- `Profile/perf_log.csv` — performance data (2 passes)

All artifacts must exist after the run. Timeout is 30 seconds (render scenes are instant, perf test runs 2×5s).

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
Remove-Item "$REPO\Profile\screenshot.bmp" -ErrorAction SilentlyContinue
Remove-Item "$REPO\Profile\legacy_smoke.bmp" -ErrorAction SilentlyContinue
Remove-Item "$REPO\Profile\perf_log.csv" -ErrorAction SilentlyContinue

$proc = Start-Process "$REPO\Profile\SKULLBONEZ_CORE.exe" `
    -ArgumentList "--suite SkullbonezData/scenes/render_tests.suite" `
    -WorkingDirectory $REPO -PassThru
$proc.WaitForExit(30000) | Out-Null

$allOk = $true
foreach ($f in @("screenshot.bmp", "legacy_smoke.bmp", "perf_log.csv")) {
    if (Test-Path "$REPO\Profile\$f") {
        Write-Host "  $f OK"
    } else {
        Write-Host "  $f MISSING"
        $allOk = $false
    }
}

if ($allOk) {
    Write-Host "PASS: All suite artifacts produced"
} else {
    Write-Host "FAIL: Missing artifacts — debug with skore-cdb-debug skill"
}
```

**If the suite fails or crashes**: Debug with `skore-cdb-debug` skill using the same `--suite` command line.

### Step 4: Validate Render Output

Compare screenshots against baselines. Both must match (or intentionally differ if rendering changed).

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$env:SKORE_REPO = $REPO
py -c "
import os
from PIL import Image
_r = os.environ['SKORE_REPO']
pairs = [
    (_r + r'\Profile\screenshot.bmp', _r + r'\TestOutput\baselines\baseline_water_ball_test.png', 'water_ball_test'),
    (_r + r'\Profile\legacy_smoke.bmp', _r + r'\TestOutput\baselines\baseline_legacy_smoke.png', 'legacy_smoke'),
]
all_pass = True
for cap, base, name in pairs:
    if not os.path.exists(base):
        print(f'  {name}: NO BASELINE (will create in Step 5)')
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
        all_pass = False
if all_pass:
    print('PASS: All render tests match baselines')
else:
    print('NOTE: Baselines differ — will update in Step 5')
"
```

**If render test fails**: Convert screenshots to PNG and send them to the model via the `view` tool for LLM visual comparison. **Only send PNGs to the model if local pixel comparison fails** — do not waste context on images that pass. If the change intentionally alters rendering, update baselines in Step 5. If unintentional, investigate and fix before proceeding.

### Step 5: Update Reference Images

**Mandatory for every commit.** Capture fresh baselines from the current build:

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$env:SKORE_REPO = $REPO
py -c "
import os
from PIL import Image
_r = os.environ['SKORE_REPO']
_b = _r + r'\TestOutput\baselines'
Image.open(_r + r'\Profile\screenshot.bmp').save(_b + r'\baseline_water_ball_test.png')
Image.open(_r + r'\Profile\legacy_smoke.bmp').save(_b + r'\baseline_legacy_smoke.png')
print('Baselines updated')
"
```

This ensures baselines always reflect the latest committed state.

### Step 6: Analyze Performance

Run analysis scripts on the perf CSV produced by the suite run in Step 3.

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
py "$REPO\Copilot\Skills\skore-render-test\analyze_perf.py"
py "$REPO\Copilot\Skills\skore-render-test\perf_compare.py"
```

`analyze_perf.py` writes a JSON artifact to `TestOutput/perf_history/{commit}.json` and compares against the previous artifact. `perf_compare.py` prints the colored comparison table against `TestOutput/baseline-001/perf.json`.

**⚠️ MANDATORY: You MUST print the COMPLETE perf_compare.py output to the user — every row, every emoji dot, the full CPU table, GPU table, and Memory table. Do NOT summarize, truncate, or paraphrase. The user requires the full table every single pipeline run. If the output was saved to a temp file due to size, read it and print it in full.**

**If regression thresholds are exceeded**, investigate before proceeding.

Regression thresholds: avg/p50 timing >10%, p99/p99.9 >20%, memory >5 MB.

### Step 7: Archive to TestOutput

**Mandatory for every commit.** Creates a sequentially numbered directory in `TestOutput/` containing PNGs and perf data. This builds a permanent visual + performance timeline in the repo.

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$env:SKORE_REPO = $REPO
py -c "
import os, json, shutil, glob as g
from pathlib import Path
from PIL import Image

_r = Path(os.environ['SKORE_REPO'])
_to = _r / 'TestOutput'
_to.mkdir(exist_ok=True)

# Determine next sequence number
existing = sorted(g.glob(str(_to / '[0-9][0-9][0-9]_*')))
seq = len(existing) + 1

# Get commit hash
import subprocess
commit = subprocess.run(['git', 'rev-parse', '--short', 'HEAD'], capture_output=True, text=True, cwd=str(_r)).stdout.strip()

archive = _to / f'{seq:03d}_{commit}'
archive.mkdir()

# Convert and copy screenshots (one per scene, no reset images)
pairs = [
    (_r / 'Profile' / 'screenshot.bmp', archive / 'water_ball_test.png'),
    (_r / 'Profile' / 'legacy_smoke.bmp', archive / 'legacy_smoke.png'),
]
for src, dst in pairs:
    if src.exists():
        Image.open(str(src)).save(str(dst))

# Copy perf artifact
perf_src = _r / 'TestOutput' / 'perf_history' / f'{commit}.json'
if perf_src.exists():
    shutil.copy2(str(perf_src), str(archive / 'perf.json'))

print(f'Archived to {archive.name}')
"
```

### Step 8: Lines of Code

Informational — counts logical LOC (excludes blanks and comments) across all `.h` and `.cpp` files in `SkullbonezSource/`. No pass/fail; just print and note the total in the commit message.

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
py "$REPO\Copilot\Skills\loc_count.py"
```

### Step 9: Commit

Only if **all** previous steps pass. The commit MUST include:
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
