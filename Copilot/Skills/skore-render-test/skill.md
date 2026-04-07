---
name: skore-render-test
description: Run render test scenes through SkullbonezCore, capture screenshots, and validate against baselines. Invoke when the user asks to run a render test, capture a scene screenshot, or verify rendering output.
---

## Render Test Skill

Launches SkullbonezCore with `--suite SkullbonezData/scenes/render_tests.suite`, which runs both render test scenes **in a single process launch**. Each scene produces 2 screenshots (before and after GL context reset). Validates all 4 outputs against baselines with a three-tier comparison system.

Two test scenes are in the suite:
1. **water_ball_test** — Single ball, simple scene (verifies terrain, skybox, water rendering)
2. **legacy_smoke** — 300 seeded random balls, full legacy code path (verifies sphere rendering at scale)

The **perf test** (`perf_test.scene`) is run separately via `--scene` — it takes 10 seconds and is intentionally not part of the suite.

### Prerequisites

The Debug exe must exist at `Y:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe`. If not, build first using the `skore-build` skill.

### Steps

#### 1. Run both scenes

Both scenes are run in a single process invocation via the render test suite. Each scene still destroys and recreates the GL context (for `test_gl_reset`), producing two screenshots per scene (before and after reset) — 4 screenshots total.

```pwsh
$proc = Get-Process SKULLBONEZ_CORE -ErrorAction SilentlyContinue
if ($proc) { Stop-Process -Id $proc.Id -Force; Start-Sleep 1 }

# Clean up old screenshots
Remove-Item "Y:\SkullbonezCore\Debug\screenshot.bmp" -ErrorAction SilentlyContinue
Remove-Item "Y:\SkullbonezCore\Debug\screenshot_reset.bmp" -ErrorAction SilentlyContinue
Remove-Item "Y:\SkullbonezCore\Debug\legacy_smoke.bmp" -ErrorAction SilentlyContinue
Remove-Item "Y:\SkullbonezCore\Debug\legacy_smoke_reset.bmp" -ErrorAction SilentlyContinue

# Run all render test scenes in one process via the suite file
$proc = Start-Process "Y:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe" `
    -ArgumentList "--suite SkullbonezData/scenes/render_tests.suite" `
    -WorkingDirectory "Y:\SkullbonezCore" -PassThru
$proc.WaitForExit(30000) | Out-Null
if (!$proc.HasExited) { Stop-Process -Id $proc.Id -Force; Write-Host "FAIL: suite timed out" }

$s1 = Test-Path "Y:\SkullbonezCore\Debug\screenshot.bmp"
$s2 = Test-Path "Y:\SkullbonezCore\Debug\screenshot_reset.bmp"
$s3 = Test-Path "Y:\SkullbonezCore\Debug\legacy_smoke.bmp"
$s4 = Test-Path "Y:\SkullbonezCore\Debug\legacy_smoke_reset.bmp"
Write-Host "water_ball_test: pass1=$s1 pass2=$s2"
Write-Host "legacy_smoke: pass1=$s3 pass2=$s4"
```

#### 2. Convert screenshots to PNG

The API cannot display BMP files (causes schema validation error). **Always** convert before any comparison or viewing:

```pwsh
py -c "
from PIL import Image
for src, dst in [
    (r'Y:\SkullbonezCore\Debug\screenshot.bmp', r'Y:\SkullbonezCore\Debug\screenshot_water.png'),
    (r'Y:\SkullbonezCore\Debug\screenshot_reset.bmp', r'Y:\SkullbonezCore\Debug\screenshot_water_reset.png'),
    (r'Y:\SkullbonezCore\Debug\legacy_smoke.bmp', r'Y:\SkullbonezCore\Debug\legacy_smoke.png'),
    (r'Y:\SkullbonezCore\Debug\legacy_smoke_reset.bmp', r'Y:\SkullbonezCore\Debug\legacy_smoke_reset.png'),
]:
    Image.open(src).save(dst)
print('Converted 4 screenshots to PNG')
"
```

**IMPORTANT**: Never send `.bmp` files to the `view` tool — always use the converted `.png` files.

#### 3. Tolerance pixel comparison

Compares baseline vs pass1, baseline vs pass2 (after GL reset), and pass1 vs pass2 for each scene. All 6 pairs must pass.

```pwsh
py -c "
from PIL import Image

TOLERANCE = 5  # per-channel (R, G, B) allowed deviation

def compare(baseline_path, current_path, name):
    baseline = Image.open(baseline_path).convert('RGB')
    current  = Image.open(current_path).convert('RGB')

    if baseline.size != current.size:
        print(f'FAIL [{name}]: Size mismatch {baseline.size} vs {current.size}')
        return False

    bp = baseline.load()
    cp = current.load()
    w, h = baseline.size
    total = w * h
    exact_diff = 0
    tolerance_diff = 0

    for y in range(h):
        for x in range(w):
            br, bg, bb = bp[x, y]
            cr, cg, cb = cp[x, y]
            if (br, bg, bb) != (cr, cg, cb):
                exact_diff += 1
                if abs(br-cr) > TOLERANCE or abs(bg-cg) > TOLERANCE or abs(bb-cb) > TOLERANCE:
                    tolerance_diff += 1

    exact_pct = round(exact_diff / total * 100, 4)
    tol_pct   = round(tolerance_diff / total * 100, 4)
    print(f'[{name}] Exact diff: {exact_diff} ({exact_pct}%) | Beyond tolerance: {tolerance_diff} ({tol_pct}%)')

    if tolerance_diff == 0:
        print(f'PIXEL_PASS [{name}]')
        return True
    elif tol_pct < 0.5:
        print(f'PIXEL_PASS [{name}]: {tol_pct}% beyond tolerance — acceptable')
        return True
    else:
        print(f'PIXEL_FAIL [{name}]: {tol_pct}% beyond tolerance — needs visual review')
        return False

print('=== water_ball_test ===')
r1 = compare(r'Y:\SkullbonezCore\Copilot\Skills\skore-render-test\baseline_water_ball_test.png',
             r'Y:\SkullbonezCore\Debug\screenshot_water.png', 'baseline vs pass1')
r2 = compare(r'Y:\SkullbonezCore\Copilot\Skills\skore-render-test\baseline_water_ball_test.png',
             r'Y:\SkullbonezCore\Debug\screenshot_water_reset.png', 'baseline vs pass2')
r3 = compare(r'Y:\SkullbonezCore\Debug\screenshot_water.png',
             r'Y:\SkullbonezCore\Debug\screenshot_water_reset.png', 'pass1 vs pass2')

print()
print('=== legacy_smoke ===')
r4 = compare(r'Y:\SkullbonezCore\Copilot\Skills\skore-render-test\baseline_legacy_smoke.png',
             r'Y:\SkullbonezCore\Debug\legacy_smoke.png', 'baseline vs pass1')
r5 = compare(r'Y:\SkullbonezCore\Copilot\Skills\skore-render-test\baseline_legacy_smoke.png',
             r'Y:\SkullbonezCore\Debug\legacy_smoke_reset.png', 'baseline vs pass2')
r6 = compare(r'Y:\SkullbonezCore\Debug\legacy_smoke.png',
             r'Y:\SkullbonezCore\Debug\legacy_smoke_reset.png', 'pass1 vs pass2')

print()
if all([r1,r2,r3,r4,r5,r6]):
    print('ALL PIXEL TESTS PASSED')
else:
    print('PIXEL TESTS FAILED - proceed to step 4 for LLM visual comparison')
"
```

**Pass criteria**: <0.5% of pixels beyond ±5 per-channel tolerance for each of the 6 pairs.

If pixel tests pass → **done**, no further steps needed.
If pixel tests fail → proceed to step 4.

#### 4. LLM visual comparison

For each scene that failed pixel comparison, view the baseline and current screenshot side-by-side and evaluate:

**View the images** (use the `view` tool):
- Baseline: `Y:\SkullbonezCore\Copilot\Skills\skore-render-test\baseline_water_ball_test.png`
- Current: `Y:\SkullbonezCore\Debug\screenshot_water.png`
- Baseline: `Y:\SkullbonezCore\Copilot\Skills\skore-render-test\baseline_legacy_smoke.png`
- Current: `Y:\SkullbonezCore\Debug\legacy_smoke.png`

**Evaluate each pair against this checklist:**
1. Are all expected objects present? (terrain, skybox, spheres, water, shadows)
2. Are objects in the same positions?
3. Is the lighting direction consistent? (highlights on same side)
4. Are textures applied correctly? (no missing/wrong textures)
5. Is the overall color palette similar? (no dramatic color shifts)
6. Are there any rendering artifacts? (black triangles, z-fighting, missing faces)

**Verdict per scene:**
- If all 6 checks pass → `LLM_PASS` — the scenes are functionally equivalent despite pixel differences (expected during shader migration)
- If any check fails → `LLM_FAIL` — there is a visual regression

#### 5. Combined verdict

| Pixel Result | LLM Result | Overall | Action |
|---|---|---|---|
| PASS | (skip) | **PASS** | No further action |
| FAIL | PASS | **PASS** | Differences are cosmetic (shading model change). Consider updating baselines. |
| FAIL | FAIL | **FAIL** | Use `ask_user` to prompt the user to inspect the images. Show them the current screenshots with the `view` tool and report what the LLM checklist flagged. Do not proceed until the user decides. |

**When both fail**: You MUST use the `ask_user` tool to prompt the user. Show them the failing screenshots and explain what the LLM visual comparison flagged. The user decides whether to accept, investigate, or revert.

### Updating baselines

When a change **intentionally** alters rendering (e.g. migrating a subsystem to shaders), archive the old baselines first, then recapture.

#### Step 1 — Archive old baselines

```pwsh
$commit = (git -C "Y:\SkullbonezCore" rev-parse HEAD).Trim()
$histDir = "Y:\SkullbonezCore\Copilot\Skills\skore-render-test\baseline_history\$commit"
New-Item -ItemType Directory -Path $histDir -Force | Out-Null
Get-ChildItem "Y:\SkullbonezCore\Copilot\Skills\skore-render-test\baseline_*.png" | ForEach-Object {
    Copy-Item $_.FullName -Destination "$histDir\$($_.Name)"
    Write-Host "Archived: $($_.Name) -> baseline_history\$commit\"
}
```

#### Step 2 — Write new baselines

```pwsh
py -c "
from PIL import Image
pairs = [
    (r'Y:\SkullbonezCore\Debug\screenshot.bmp',        r'Y:\SkullbonezCore\Copilot\Skills\skore-render-test\baseline_water_ball_test.png'),
    (r'Y:\SkullbonezCore\Debug\screenshot_reset.bmp',  r'Y:\SkullbonezCore\Copilot\Skills\skore-render-test\baseline_water_ball_test_reset.png'),
    (r'Y:\SkullbonezCore\Debug\legacy_smoke.bmp',      r'Y:\SkullbonezCore\Copilot\Skills\skore-render-test\baseline_legacy_smoke.png'),
    (r'Y:\SkullbonezCore\Debug\legacy_smoke_reset.bmp',r'Y:\SkullbonezCore\Copilot\Skills\skore-render-test\baseline_legacy_smoke_reset.png'),
]
for src, dst in pairs:
    Image.open(src).save(dst)
    print(f'Updated: {dst.split(chr(92))[-1]}')
"
```

Include both the updated baselines **and** the `baseline_history/` folder in the same commit.

### Scene file directives

```
physics off|on                        # default: on
text off|on                           # default: on
frames <N>|unlimited                  # default: unlimited
seed <N>                              # fixed RNG seed (deterministic balls)
legacy_balls <N>                      # generate N random balls (like legacy mode)
test_gl_reset                         # test GL context recreation (2-pass screenshot)
perf_log <path>                       # enable per-frame timing CSV (triggers 2x5s perf run)
screenshot <path> frame <N>           # capture after frame N
screenshot <path> ms <N>              # capture after N milliseconds
camera <name> <px py pz vx vy vz ux uy uz>
ball <name> <x y z r mass moment rest> [fx fy fz fpx fpy fpz]
```

## Performance Test

Runs SkullbonezCore for 10 seconds (2 passes × 5 seconds) with 300 balls, physics, and text overlay. Logs per-frame physics and render times plus memory checkpoints. After analysis, writes a JSON artifact for regression tracking.

### Running the perf test

```pwsh
# 1. Run the perf test scene (10 seconds total)
$proc = Get-Process SKULLBONEZ_CORE -ErrorAction SilentlyContinue
if ($proc) { Stop-Process -Id $proc.Id -Force; Start-Sleep 1 }
Remove-Item "Y:\SkullbonezCore\Debug\perf_log.csv" -ErrorAction SilentlyContinue
$proc = Start-Process "Y:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe" `
    -ArgumentList "--scene SkullbonezData/scenes/perf_test.scene" `
    -WorkingDirectory "Y:\SkullbonezCore" -PassThru
$proc.WaitForExit(30000) | Out-Null
if (!$proc.HasExited) { Stop-Process -Id $proc.Id -Force; Write-Host "FAIL: perf test didn't exit" }
if (Test-Path "Y:\SkullbonezCore\Debug\perf_log.csv") {
    Write-Host "PASS: perf_log.csv generated"
} else { Write-Host "FAIL: No perf_log.csv" }

# 2. Analyze and write artifact
py "Y:\SkullbonezCore\Copilot\Skills\skore-render-test\analyze_perf.py"
```

### Artifact format

Written to `Skills/skore-render-test/perf_history/{commit_hash}.json`:

```json
{
  "commit": "ab2729c",
  "total_frames": 601,
  "physics_ms": { "min": 0.76, "max": 2.80, "avg": 1.72, "p50": 1.72, "p99": 2.58, "p99_9": 2.80 },
  "render_ms":  { "min": 0.95, "max": 4.33, "avg": 1.35, "p50": 1.36, "p99": 1.78, "p99_9": 3.99 },
  "mem_start_mb": 63.62,
  "mem_restart_mb": 89.86,
  "mem_end_mb": 90.47
}
```

The analysis script automatically compares against the previous artifact and reports deltas.
