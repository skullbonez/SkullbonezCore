---
name: skore-build-pipeline
description: Standard development pipeline for SkullbonezCore. Build, render test, update baselines, perf test, smoke test, commit. Invoke after completing a code change to verify and commit it.
---

## Standard Build Pipeline

The full verify-and-commit pipeline after a code change. **Every step must pass before proceeding to the next.** Every commit MUST include updated reference images and performance test artifacts.

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
$proc    = Get-Process SKULLBONEZ_CORE -ErrorAction SilentlyContinue
if ($proc) { Stop-Process -Id $proc.Id -Force; Start-Sleep 1 }

$msbuild = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
& $msbuild "$REPO\SKULLBONEZ_CORE.sln" /p:Configuration=Profile /p:Platform=x64 /nologo /v:minimal
```

**If build fails**: Fix errors, rebuild. Do not proceed.

### Step 3: Render Test

Run the `skore-render-test` skill. Launches **both test scenes in a single process** via `--suite SkullbonezData/scenes/render_tests.suite`. Produces 4 screenshots (2 per scene — before and after GL reset), then runs pixel comparison against baselines. All 6 comparison pairs must pass.

**If render test fails**: Convert screenshots to PNG and send them to the model via the `view` tool for LLM visual comparison. **Only send PNGs to the model if local pixel comparison fails** — do not waste context on images that pass. Evaluate against the 6-point checklist in the `skore-render-test` skill. If the change intentionally alters rendering, update baselines in Step 4. If unintentional, investigate and fix before proceeding.

### Step 4: Update Reference Images

**Mandatory for every commit.** Capture fresh baselines from the current build:

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$env:SKORE_REPO = $REPO
py -c "
import os
from PIL import Image
_r = os.environ['SKORE_REPO']
_s = _r + r'\Copilot\Skills\skore-render-test'
Image.open(_r + r'\Profile\screenshot.bmp').save(_s        + r'\baseline_water_ball_test.png')
Image.open(_r + r'\Profile\screenshot_reset.bmp').save(_s  + r'\baseline_water_ball_test_reset.png')
Image.open(_r + r'\Profile\legacy_smoke.bmp').save(_s      + r'\baseline_legacy_smoke.png')
Image.open(_r + r'\Profile\legacy_smoke_reset.bmp').save(_s+ r'\baseline_legacy_smoke_reset.png')
print('Baselines updated')
"
```

This ensures baselines always reflect the latest committed state.

### Step 5: Performance Test

**Mandatory for every commit.** Separate from the render test suite — runs via `--scene` (not `--suite`) because it takes 10 seconds and should not block quick render test runs.

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
    Write-Host "FAIL: No perf_log.csv"
}
```

The analysis script writes a JSON artifact to `Skills/skore-render-test/perf_history/{commit}.json` and compares against the previous artifact. **If regression thresholds are exceeded**, investigate before proceeding.

Regression thresholds: avg/p50 timing >10%, p99/p99.9 >20%, memory >5 MB.

### Step 6: Archive to TestOutput

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

# Convert and copy screenshots
pairs = [
    (_r / 'Profile' / 'screenshot.bmp',        archive / 'water_ball_test.png'),
    (_r / 'Profile' / 'screenshot_reset.bmp',   archive / 'water_ball_test_reset.png'),
    (_r / 'Profile' / 'legacy_smoke.bmp',       archive / 'legacy_smoke.png'),
    (_r / 'Profile' / 'legacy_smoke_reset.bmp',  archive / 'legacy_smoke_reset.png'),
]
for src, dst in pairs:
    if src.exists():
        Image.open(str(src)).save(str(dst))

# Copy perf artifact
perf_src = _r / 'Copilot' / 'Skills' / 'skore-render-test' / 'perf_history' / f'{commit}.json'
if perf_src.exists():
    shutil.copy2(str(perf_src), str(archive / 'perf.json'))

print(f'Archived to {archive.name}')
"
```

### Step 7: Lines of Code

Informational — counts logical LOC (excludes blanks and comments) across all `.h` and `.cpp` files in `SkullbonezSource/`. No pass/fail; just print and note the total in the commit message.

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
py "$REPO\Copilot\Skills\loc_count.py"
```

### Step 8: Legacy Smoke Test

Quick check that default mode (no `--scene`) still runs without crashing:

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$proc = Get-Process SKULLBONEZ_CORE -ErrorAction SilentlyContinue
if ($proc) { Stop-Process -Id $proc.Id -Force; Start-Sleep 1 }

Start-Process "$REPO\Profile\SKULLBONEZ_CORE.exe" -WorkingDirectory $REPO
Start-Sleep 5
$proc = Get-Process SKULLBONEZ_CORE -ErrorAction SilentlyContinue
if ($proc) {
    Write-Host "PASS: Legacy mode running (PID $($proc.Id))"
    Stop-Process -Id $proc.Id -Force
} else {
    Write-Host "FAIL: Legacy mode crashed"
}
```

**If smoke test fails**: Debug with `skore-cdb-debug` skill.

### Step 9: Commit

Only if **all** previous steps pass. The commit MUST include:
- Code changes
- Updated reference images (from Step 4)
- Performance test artifact (from Step 5)
- TestOutput archive (from Step 6)

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
cd $REPO
git add -A
git commit -m "<descriptive message>

Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```
