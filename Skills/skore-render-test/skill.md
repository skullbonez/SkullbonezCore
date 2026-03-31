---
name: skore-render-test
description: Run a scene file through SkullbonezCore in scene mode, capture a screenshot, and output the path. Invoke when the user asks to run a render test, capture a scene screenshot, or verify rendering output.
---

## Render Test Skill

Launches SkullbonezCore with `.scene` files that have `screenshot` directives. The engine renders the scene, writes a BMP via `glReadPixels`, and exits. Then compares against baselines pixel-by-pixel.

Two test scenes are run:
1. **water_ball_test** — Single ball, simple scene (verifies terrain, skybox, water rendering)
2. **legacy_smoke** — 300 seeded random balls, full legacy code path (verifies sphere rendering at scale)

### Prerequisites

The Debug exe must exist at `Y:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe`. If not, build first using the `skore-build` skill.

### Steps

#### 1. Run both scenes

```pwsh
$proc = Get-Process SKULLBONEZ_CORE -ErrorAction SilentlyContinue
if ($proc) { Stop-Process -Id $proc.Id -Force; Start-Sleep 1 }

# Scene 1: water_ball_test
Remove-Item "Y:\SkullbonezCore\Debug\screenshot.bmp" -ErrorAction SilentlyContinue
$proc = Start-Process "Y:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe" `
    -ArgumentList "--scene SkullbonezData/scenes/water_ball_test.scene" `
    -WorkingDirectory "Y:\SkullbonezCore" -PassThru
$proc.WaitForExit(10000) | Out-Null
if (!$proc.HasExited) { Stop-Process -Id $proc.Id -Force; Write-Host "FAIL: water_ball_test didn't exit" }
if (Test-Path "Y:\SkullbonezCore\Debug\screenshot.bmp") {
    Write-Host "water_ball_test: Screenshot saved"
} else { Write-Host "FAIL: No water_ball_test screenshot" }

# Scene 2: legacy_smoke
Remove-Item "Y:\SkullbonezCore\Debug\legacy_smoke.bmp" -ErrorAction SilentlyContinue
$proc = Start-Process "Y:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe" `
    -ArgumentList "--scene SkullbonezData/scenes/legacy_smoke.scene" `
    -WorkingDirectory "Y:\SkullbonezCore" -PassThru
$proc.WaitForExit(15000) | Out-Null
if (!$proc.HasExited) { Stop-Process -Id $proc.Id -Force; Write-Host "FAIL: legacy_smoke didn't exit" }
if (Test-Path "Y:\SkullbonezCore\Debug\legacy_smoke.bmp") {
    Write-Host "legacy_smoke: Screenshot saved"
} else { Write-Host "FAIL: No legacy_smoke screenshot" }
```

#### 2. Pixel compare against baselines

```pwsh
powershell.exe -Command {
    Add-Type -AssemblyName System.Drawing

    function Compare-Baseline($baselinePath, $screenshotPath, $name) {
        if (!(Test-Path $screenshotPath)) { Write-Host "FAIL [$name]: No screenshot"; return $false }
        $baseline = New-Object System.Drawing.Bitmap $baselinePath
        $current  = New-Object System.Drawing.Bitmap $screenshotPath

        if ($baseline.Width -ne $current.Width -or $baseline.Height -ne $current.Height) {
            Write-Host "FAIL [$name]: Size mismatch $($baseline.Width)x$($baseline.Height) vs $($current.Width)x$($current.Height)"
            $baseline.Dispose(); $current.Dispose(); return $false
        }

        $diffCount = 0; $total = $baseline.Width * $baseline.Height
        for ($y = 0; $y -lt $baseline.Height; $y++) {
            for ($x = 0; $x -lt $baseline.Width; $x++) {
                if ($baseline.GetPixel($x, $y).ToArgb() -ne $current.GetPixel($x, $y).ToArgb()) { $diffCount++ }
            }
        }

        $pct = [Math]::Round(($diffCount / $total) * 100, 4)
        Write-Host "[$name] Pixels: $total | Different: $diffCount ($pct%)"
        if ($diffCount -eq 0) { Write-Host "PASS [$name]: Pixel-perfect match"; return $true }
        elseif ($pct -lt 0.1) { Write-Host "PASS [$name]: Negligible diff ($pct%)"; return $true }
        else { Write-Host "FAIL [$name]: $pct% pixels differ - visual regression"; return $false }
        $baseline.Dispose(); $current.Dispose()
    }

    $r1 = Compare-Baseline "Y:\SkullbonezCore\Skills\skore-render-test\baseline_water_ball_test.png" "Y:\SkullbonezCore\Debug\screenshot.bmp" "water_ball_test"
    $r2 = Compare-Baseline "Y:\SkullbonezCore\Skills\skore-render-test\baseline_legacy_smoke.png" "Y:\SkullbonezCore\Debug\legacy_smoke.bmp" "legacy_smoke"

    if ($r1 -and $r2) { Write-Host "`nALL RENDER TESTS PASSED" } else { Write-Host "`nRENDER TESTS FAILED" }
}
```

**Pass criteria**: <0.1% pixel difference per scene. Engine-side capture is deterministic so expect pixel-perfect in most cases.

**If render test fails**: View the screenshot BMP to inspect visually, investigate and fix before proceeding.

### Updating baselines

When a change **intentionally** alters rendering (e.g. migrating a subsystem to shaders), recapture baselines after visual verification:

```pwsh
py -c "
from PIL import Image
Image.open(r'Y:\SkullbonezCore\Debug\screenshot.bmp').save(r'Y:\SkullbonezCore\Skills\skore-render-test\baseline_water_ball_test.png')
Image.open(r'Y:\SkullbonezCore\Debug\legacy_smoke.bmp').save(r'Y:\SkullbonezCore\Skills\skore-render-test\baseline_legacy_smoke.png')
print('Baselines updated')
"
```

Include the updated baselines in the same commit.

### Scene file directives

```
physics off|on                        # default: on
text off|on                           # default: on
frames <N>|unlimited                  # default: unlimited
seed <N>                              # fixed RNG seed (deterministic balls)
legacy_balls <N>                      # generate N random balls (like legacy mode)
screenshot <path> frame <N>           # capture after frame N
screenshot <path> ms <N>              # capture after N milliseconds
camera <name> <px py pz vx vy vz ux uy uz>
ball <name> <x y z r mass moment rest> [fx fy fz fpx fpy fpz]
```
