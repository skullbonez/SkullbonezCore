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

The Profile exe must exist at `{REPO}\Profile\SKULLBONEZ_CORE.exe`. If not, build first using the `skore-build` skill.

### Steps

#### 1. Run both scenes

Both scenes are run in a single process invocation via the render test suite. Each scene still destroys and recreates the GL context (for `test_gl_reset`), producing two screenshots per scene (before and after reset) — 4 screenshots total.

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()

# Clean up old screenshots
Remove-Item "$REPO\Profile\screenshot.bmp" -ErrorAction SilentlyContinue
Remove-Item "$REPO\Profile\screenshot_reset.bmp" -ErrorAction SilentlyContinue
Remove-Item "$REPO\Profile\legacy_smoke.bmp" -ErrorAction SilentlyContinue
Remove-Item "$REPO\Profile\legacy_smoke_reset.bmp" -ErrorAction SilentlyContinue

# Run all render test scenes in one process via the suite file
$proc = Start-Process "$REPO\Profile\SKULLBONEZ_CORE.exe" `
    -ArgumentList "--suite SkullbonezData/scenes/render_tests.suite" `
    -WorkingDirectory $REPO -PassThru
$proc.WaitForExit(30000) | Out-Null

$s1 = Test-Path "$REPO\Profile\screenshot.bmp"
$s2 = Test-Path "$REPO\Profile\screenshot_reset.bmp"
$s3 = Test-Path "$REPO\Profile\legacy_smoke.bmp"
$s4 = Test-Path "$REPO\Profile\legacy_smoke_reset.bmp"
Write-Host "water_ball_test: pass1=$s1 pass2=$s2"
Write-Host "legacy_smoke: pass1=$s3 pass2=$s4"
```

#### 2. Convert screenshots to PNG

The API cannot display BMP files (causes schema validation error). **Always** convert before any comparison or viewing:

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$env:SKORE_REPO = $REPO
py -c "
import os
from PIL import Image
_r = os.environ['SKORE_REPO']
for src, dst in [
    (_r + r'\Profile\screenshot.bmp',        _r + r'\Profile\screenshot_water.png'),
    (_r + r'\Profile\screenshot_reset.bmp',  _r + r'\Profile\screenshot_water_reset.png'),
    (_r + r'\Profile\legacy_smoke.bmp',      _r + r'\Profile\legacy_smoke.png'),
    (_r + r'\Profile\legacy_smoke_reset.bmp',_r + r'\Profile\legacy_smoke_reset.png'),
]:
    Image.open(src).save(dst)
print('Converted 4 screenshots to PNG')
"
```

**IMPORTANT**: Never send `.bmp` files to the `view` tool — always use the converted `.png` files.

#### 3. Tolerance pixel comparison

Compares baseline vs pass1, and pass1 vs pass2 (GL reset verification) for each scene. All 4 pairs must pass.

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$env:SKORE_REPO = $REPO
py -c "
import os
from PIL import Image

TOLERANCE = 5  # per-channel (R, G, B) allowed deviation
_r = os.environ['SKORE_REPO']

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

_baselines = _r + r'\TestOutput\baselines'
print('=== water_ball_test ===')
r1 = compare(_baselines + r'\baseline_water_ball_test.png', _r + r'\Profile\screenshot_water.png',       'baseline vs pass1')
r2 = compare(_r + r'\Profile\screenshot_water.png',         _r + r'\Profile\screenshot_water_reset.png',  'pass1 vs pass2 (GL reset)')

print()
print('=== legacy_smoke ===')
r3 = compare(_baselines + r'\baseline_legacy_smoke.png', _r + r'\Profile\legacy_smoke.png',       'baseline vs pass1')
r4 = compare(_r + r'\Profile\legacy_smoke.png',          _r + r'\Profile\legacy_smoke_reset.png',  'pass1 vs pass2 (GL reset)')

print()
if all([r1,r2,r3,r4]):
    print('ALL PIXEL TESTS PASSED')
else:
    print('PIXEL TESTS FAILED - proceed to step 4 for LLM visual comparison')
"
```

**Pass criteria**: <0.5% of pixels beyond ±5 per-channel tolerance for each of the 4 pairs.

If pixel tests pass → **done**, no further steps needed.
If pixel tests fail → proceed to step 4.

#### 4. LLM visual comparison

For each scene that failed pixel comparison, view the baseline and current screenshot side-by-side and evaluate:

**View the images** (use the `view` tool):
- Baseline: `{REPO}\TestOutput\baselines\baseline_water_ball_test.png`
- Current: `{REPO}\Profile\screenshot_water.png`
- Baseline: `{REPO}\TestOutput\baselines\baseline_legacy_smoke.png`
- Current: `{REPO}\Profile\legacy_smoke.png`

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

When a change **intentionally** alters rendering (e.g. migrating a subsystem to shaders), overwrite the baselines directly — no archiving needed (git history is the archive).

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
$env:SKORE_REPO = $REPO
py -c "
import os
from PIL import Image
_r = os.environ['SKORE_REPO']
_b = _r + r'\TestOutput\baselines'
pairs = [
    (_r + r'\Profile\screenshot.bmp', _b + r'\baseline_water_ball_test.png'),
    (_r + r'\Profile\legacy_smoke.bmp', _b + r'\baseline_legacy_smoke.png'),
]
for src, dst in pairs:
    Image.open(src).save(dst)
    print('Updated: ' + dst.split(chr(92))[-1])
"
```

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
$REPO = (git rev-parse --show-toplevel).Trim()
Remove-Item "$REPO\Profile\perf_log.csv" -ErrorAction SilentlyContinue
$proc = Start-Process "$REPO\Profile\SKULLBONEZ_CORE.exe" `
    -ArgumentList "--scene SkullbonezData/scenes/perf_test.scene" `
    -WorkingDirectory $REPO -PassThru
$proc.WaitForExit(30000) | Out-Null
if (Test-Path "$REPO\Profile\perf_log.csv") {
    Write-Host "PASS: perf_log.csv generated"
} else { Write-Host "FAIL: No perf_log.csv" }

# 2. Analyze and write artifact
py "$REPO\Copilot\Skills\skore-render-test\analyze_perf.py"

# 3. Compare against baseline-001
py "$REPO\Copilot\Skills\skore-render-test\perf_compare.py"
```

### Artifact format

Written to `Skills/skore-render-test/perf_history/{commit_hash}.json`:

```json
{
  "commit": "ab2729c",
  "machine": {
    "hostname": "MYPC",
    "cpu": "Intel(R) Core(TM) i7-9700K CPU @ 3.60GHz",
    "cores": 8,
    "ram_gb": 31.9,
    "os": "10.0.19045"
  },
  "total_frames": 601,
  "physics_ms": { "min": 0.76, "max": 2.80, "avg": 1.72, "p50": 1.72, "p99": 2.58, "p99_9": 2.80 },
  "render_ms":  { "min": 0.95, "max": 4.33, "avg": 1.35, "p50": 1.36, "p99": 1.78, "p99_9": 3.99 },
  "mem_start_mb": 63.62,
  "mem_restart_mb": 89.86,
  "mem_end_mb": 90.47
}
```

The analysis script automatically compares against the previous artifact and reports deltas.
