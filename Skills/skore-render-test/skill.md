---
name: skore-render-test
description: Run a scene file through SkullbonezCore in scene mode, capture a screenshot, and output the path. Invoke when the user asks to run a render test, capture a scene screenshot, or verify rendering output.
---

## Render Test Skill

Launches SkullbonezCore with a `.scene` file that has a `screenshot` directive. The engine renders the scene, writes a BMP via `glReadPixels`, and exits. Then compares against the baseline pixel-by-pixel.

### Prerequisites

The Debug exe must exist at `Y:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe`. If not, build first using the `skore-build` skill.

### Steps

#### 1. Run scene with screenshot directive

The scene file must include a `screenshot` directive (e.g. `screenshot Debug/screenshot.bmp frame 1`). The engine saves the framebuffer and exits automatically.

```pwsh
$proc = Get-Process SKULLBONEZ_CORE -ErrorAction SilentlyContinue
if ($proc) { Stop-Process -Id $proc.Id -Force; Start-Sleep 1 }

Remove-Item "Y:\SkullbonezCore\Debug\screenshot.bmp" -ErrorAction SilentlyContinue

$proc = Start-Process "Y:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe" `
    -ArgumentList "--scene SkullbonezData/scenes/water_ball_test.scene" `
    -WorkingDirectory "Y:\SkullbonezCore" -PassThru
$proc.WaitForExit(10000) | Out-Null
if (!$proc.HasExited) { Stop-Process -Id $proc.Id -Force; Write-Host "FAIL: Process didn't exit" }

if (Test-Path "Y:\SkullbonezCore\Debug\screenshot.bmp") {
    Write-Host "Screenshot saved: $((Get-Item Y:\SkullbonezCore\Debug\screenshot.bmp).Length) bytes"
} else {
    Write-Host "FAIL: No screenshot produced"
}
```

#### 2. Pixel compare against baseline

```pwsh
powershell.exe -Command {
    Add-Type -AssemblyName System.Drawing
    $baseline = New-Object System.Drawing.Bitmap "Y:\SkullbonezCore\Skills\skore-render-test\baseline_water_ball_test.png"
    $current  = New-Object System.Drawing.Bitmap "Y:\SkullbonezCore\Debug\screenshot.bmp"

    if ($baseline.Width -ne $current.Width -or $baseline.Height -ne $current.Height) {
        Write-Host "FAIL: Size mismatch $($baseline.Width)x$($baseline.Height) vs $($current.Width)x$($current.Height)"
        $baseline.Dispose(); $current.Dispose()
        exit 1
    }

    $diffCount = 0; $total = $baseline.Width * $baseline.Height
    for ($y = 0; $y -lt $baseline.Height; $y++) {
        for ($x = 0; $x -lt $baseline.Width; $x++) {
            if ($baseline.GetPixel($x, $y).ToArgb() -ne $current.GetPixel($x, $y).ToArgb()) { $diffCount++ }
        }
    }

    $pct = [Math]::Round(($diffCount / $total) * 100, 4)
    Write-Host "Pixels: $total | Different: $diffCount ($pct%)"
    if ($diffCount -eq 0) { Write-Host "PASS: Pixel-perfect match" }
    elseif ($pct -lt 0.1) { Write-Host "PASS: Negligible diff ($pct%)" }
    else { Write-Host "FAIL: $pct% pixels differ - visual regression detected" }

    $baseline.Dispose(); $current.Dispose()
}
```

**Pass criteria**: <0.1% pixel difference. Engine-side capture is deterministic so expect pixel-perfect in most cases.

**If render test fails**: View `Debug\screenshot.bmp` to inspect visually, investigate and fix before proceeding.

### Updating the baseline

When a change **intentionally** alters rendering (e.g. migrating a subsystem to shaders), recapture the baseline after visual verification. Run the scene, then convert and copy:

```pwsh
powershell.exe -Command {
    Add-Type -AssemblyName System.Drawing
    $bmp = New-Object System.Drawing.Bitmap "Y:\SkullbonezCore\Debug\screenshot.bmp"
    $bmp.Save("Y:\SkullbonezCore\Skills\skore-render-test\baseline_water_ball_test.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}
```

Include the updated baseline in the same commit.

### Scene file screenshot directives

```
screenshot <output_path> frame <N>    # capture after frame N
screenshot <output_path> ms <N>       # capture after N milliseconds
```
