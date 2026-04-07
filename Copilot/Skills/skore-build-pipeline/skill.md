---
name: skore-build-pipeline
description: Standard development pipeline for SkullbonezCore. Build, render test, update baselines, perf test, smoke test, commit. Invoke after completing a code change to verify and commit it.
---

## Standard Build Pipeline

The full verify-and-commit pipeline after a code change. **Every step must pass before proceeding to the next.** Every commit MUST include updated reference images and performance test artifacts.

### Step 1: Build

Build using the `skore-build` skill. Must produce **0 errors and 0 warnings**.

```pwsh
$proc = Get-Process SKULLBONEZ_CORE -ErrorAction SilentlyContinue
if ($proc) { Stop-Process -Id $proc.Id -Force; Start-Sleep 1 }

$msbuild = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
& $msbuild "Y:\SkullbonezCore\SKULLBONEZ_CORE.sln" /p:Configuration=Debug /p:Platform=x86 /nologo /v:minimal
```

**If build fails**: Fix errors, rebuild. Do not proceed.

### Step 2: Render Test

Run the `skore-render-test` skill. Launches **both test scenes in a single process** via `--suite SkullbonezData/scenes/render_tests.suite`. Produces 4 screenshots (2 per scene — before and after GL reset), then runs pixel comparison against baselines. All 6 comparison pairs must pass.

**If render test fails**: Convert screenshots to PNG and send them to the model via the `view` tool for LLM visual comparison. **Only send PNGs to the model if local pixel comparison fails** — do not waste context on images that pass. Evaluate against the 6-point checklist in the `skore-render-test` skill. If the change intentionally alters rendering, update baselines in Step 3. If unintentional, investigate and fix before proceeding.

### Step 3: Update Reference Images

**Mandatory for every commit.** Capture fresh baselines from the current build:

```pwsh
py -c "
from PIL import Image
Image.open(r'Y:\SkullbonezCore\Debug\screenshot.bmp').save(r'Y:\SkullbonezCore\Copilot\Skills\skore-render-test\baseline_water_ball_test.png')
Image.open(r'Y:\SkullbonezCore\Debug\screenshot_reset.bmp').save(r'Y:\SkullbonezCore\Copilot\Skills\skore-render-test\baseline_water_ball_test_reset.png')
Image.open(r'Y:\SkullbonezCore\Debug\legacy_smoke.bmp').save(r'Y:\SkullbonezCore\Copilot\Skills\skore-render-test\baseline_legacy_smoke.png')
Image.open(r'Y:\SkullbonezCore\Debug\legacy_smoke_reset.bmp').save(r'Y:\SkullbonezCore\Copilot\Skills\skore-render-test\baseline_legacy_smoke_reset.png')
print('Baselines updated')
"
```

This ensures baselines always reflect the latest committed state.

### Step 4: Performance Test

**Mandatory for every commit.** Separate from the render test suite — runs via `--scene` (not `--suite`) because it takes 10 seconds and should not block quick render test runs.

```pwsh
$proc = Get-Process SKULLBONEZ_CORE -ErrorAction SilentlyContinue
if ($proc) { Stop-Process -Id $proc.Id -Force; Start-Sleep 1 }
Remove-Item "Y:\SkullbonezCore\Debug\perf_log.csv" -ErrorAction SilentlyContinue

$proc = Start-Process "Y:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe" `
    -ArgumentList "--scene SkullbonezData/scenes/perf_test.scene" `
    -WorkingDirectory "Y:\SkullbonezCore" -PassThru
$proc.WaitForExit(30000) | Out-Null
if (!$proc.HasExited) { Stop-Process -Id $proc.Id -Force; Write-Host "FAIL: perf test timed out" }

if (Test-Path "Y:\SkullbonezCore\Debug\perf_log.csv") {
    Write-Host "PASS: perf_log.csv generated"
    py "Y:\SkullbonezCore\Copilot\Skills\skore-render-test\analyze_perf.py"
} else {
    Write-Host "FAIL: No perf_log.csv"
}
```

The analysis script writes a JSON artifact to `Skills/skore-render-test/perf_history/{commit}.json` and compares against the previous artifact. **If regression thresholds are exceeded**, investigate before proceeding.

Regression thresholds: avg/p50 timing >10%, p99/p99.9 >20%, memory >5 MB.

### Step 5: Legacy Smoke Test

Quick check that default mode (no `--scene`) still runs without crashing:

```pwsh
$proc = Get-Process SKULLBONEZ_CORE -ErrorAction SilentlyContinue
if ($proc) { Stop-Process -Id $proc.Id -Force; Start-Sleep 1 }

Start-Process "Y:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe" -WorkingDirectory "Y:\SkullbonezCore"
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

### Step 6: Commit

Only if **all** previous steps pass. The commit MUST include:
- Code changes
- Updated reference images (from Step 3)
- Performance test artifact (from Step 4)

```pwsh
cd Y:\SkullbonezCore
git add -A
git commit -m "<descriptive message>

Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```
