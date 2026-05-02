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

### Step 3: Run Dual Renderer Test Suite

Runs test suite for **both OpenGL and DirectX** renderers. Each produces separate artifacts:

**OpenGL artifacts:**
- `Profile/gl_screenshot.bmp` — water_ball_test GL render
- `Profile/gl_legacy_smoke.bmp` — legacy_smoke GL render  
- `Profile/gl_perf_log.csv` — GL performance data (2×5s passes)

**DirectX artifacts:**
- `Profile/dx11_screenshot.bmp` — water_ball_test DX11 render
- `Profile/dx11_legacy_smoke.bmp` — legacy_smoke DX11 render
- `Profile/dx11_perf_log.csv` — DX11 performance data (2×5s passes)

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()

# Clean old artifacts
Remove-Item "$REPO\Profile\*screenshot.bmp" -ErrorAction SilentlyContinue
Remove-Item "$REPO\Profile\*legacy_smoke.bmp" -ErrorAction SilentlyContinue  
Remove-Item "$REPO\Profile\*perf_log.csv" -ErrorAction SilentlyContinue

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

Write-Host "=== Running DirectX Suite ==="
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

# Verify all artifacts exist
$allOk = $true
$artifacts = @("gl_screenshot.bmp", "gl_legacy_smoke.bmp", "gl_perf_log.csv", 
               "dx11_screenshot.bmp", "dx11_legacy_smoke.bmp", "dx11_perf_log.csv")
foreach ($f in $artifacts) {
    if (Test-Path "$REPO\Profile\$f") {
        Write-Host "  $f OK"
    } else {
        Write-Host "  $f MISSING"
        $allOk = $false
    }
}

if ($allOk) {
    Write-Host "PASS: All dual-renderer artifacts produced"
} else {
    Write-Host "FAIL: Missing artifacts — debug with skore-cdb-debug skill"
}
```

**If the suite fails or crashes**: Debug with `skore-cdb-debug` skill using the same `--suite` command line.

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

**4B: Cross-renderer comparison** — GL vs DX11 visual parity:

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

**Mandatory for every commit.** Capture fresh baselines from both renderers:

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

# DirectX baselines  
if os.path.exists(_r + r'\Profile\dx11_screenshot.bmp'):
    Image.open(_r + r'\Profile\dx11_screenshot.bmp').save(_b + r'\baseline_dx11_water_ball_test.png')
    print('Updated baseline_dx11_water_ball_test.png')
if os.path.exists(_r + r'\Profile\dx11_legacy_smoke.bmp'):
    Image.open(_r + r'\Profile\dx11_legacy_smoke.bmp').save(_b + r'\baseline_dx11_legacy_smoke.png')
    print('Updated baseline_dx11_legacy_smoke.png')

print('All baselines updated')
"
```

This ensures baselines always reflect the latest committed state for both renderers.

### Step 6: Analyze Performance (Dual Renderer)

Run analysis for **both OpenGL and DirectX** performance data:

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()

Write-Host "=== OpenGL Performance Analysis ==="
# Analyze GL perf data (temporarily copy to expected location)
Copy-Item "$REPO\Profile\gl_perf_log.csv" "$REPO\Profile\perf_log.csv"
py "$REPO\Copilot\Skills\skore-render-test\analyze_perf.py"
py "$REPO\Copilot\Skills\skore-render-test\perf_compare.py"

Write-Host ""
Write-Host "=== DirectX Performance Analysis ==="  
# Analyze DX11 perf data
Copy-Item "$REPO\Profile\dx11_perf_log.csv" "$REPO\Profile\perf_log.csv"
py "$REPO\Copilot\Skills\skore-render-test\analyze_perf.py"
py "$REPO\Copilot\Skills\skore-render-test\perf_compare.py"

# Clean up temp file
Remove-Item "$REPO\Profile\perf_log.csv" -ErrorAction SilentlyContinue
```

Both renderers use the same JSON artifact file (latest overwrites). The analysis scripts detect renderer type automatically and compare against appropriate history.

**⚠️ MANDATORY: You MUST print the COMPLETE output for BOTH renderers — every row, every emoji dot, the full CPU table, GPU table, and Memory table for GL AND DX11. Do NOT summarize, truncate, or paraphrase.**

**If regression thresholds are exceeded**, investigate before proceeding.

Regression thresholds: avg/p50 timing >10%, p99/p99.9 >20%, memory >5 MB.

### Step 7: Archive to TestOutput (Dual Renderer)

**Mandatory for every commit.** Creates a sequentially numbered directory in `TestOutput/` containing PNGs and perf data from **both renderers**. This builds a permanent visual + performance timeline.

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

# Convert and copy screenshots from BOTH renderers
dual_pairs = [
    # OpenGL captures
    (_r / 'Profile' / 'gl_screenshot.bmp', archive / 'gl_water_ball_test.png'),
    (_r / 'Profile' / 'gl_legacy_smoke.bmp', archive / 'gl_legacy_smoke.png'),
    # DirectX captures  
    (_r / 'Profile' / 'dx11_screenshot.bmp', archive / 'dx11_water_ball_test.png'),
    (_r / 'Profile' / 'dx11_legacy_smoke.bmp', archive / 'dx11_legacy_smoke.png'),
]
for src, dst in dual_pairs:
    if src.exists():
        Image.open(str(src)).save(str(dst))
        print(f'  Archived {dst.name}')

# Copy perf artifacts (both CSV and JSON)
gl_csv = _r / 'Profile' / 'gl_perf_log.csv'  
dx11_csv = _r / 'Profile' / 'dx11_perf_log.csv'
if gl_csv.exists():
    shutil.copy2(str(gl_csv), str(archive / 'gl_perf_log.csv'))
if dx11_csv.exists():
    shutil.copy2(str(dx11_csv), str(archive / 'dx11_perf_log.csv'))

perf_json = _r / 'TestOutput' / 'perf_history' / f'{commit}.json'
if perf_json.exists():
    shutil.copy2(str(perf_json), str(archive / 'perf.json'))

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
