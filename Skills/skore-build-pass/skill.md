---
name: skore-build-pass
description: Standard development loop for SkullbonezCore. Plan, code, build, render test, smoke test, commit. Invoke after completing a code change to verify and commit it.
---

## Standard Build Pass

The full verify-and-commit loop after a code change. Every step must pass before proceeding to the next.

### Step 1: Build

Build using the `skore-build` skill. Must produce **0 errors and 0 warnings**.

```pwsh
$msbuild = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
& $msbuild "Y:\SkullbonezCore\SKULLBONEZ_CORE.sln" /p:Configuration=Debug /p:Platform=x86 /nologo /v:minimal
```

**If build fails**: Fix errors, rebuild. Do not proceed.

### Step 2: Render Test

Kill any running instance, launch with scene, capture screenshot:

```pwsh
$proc = Get-Process SKULLBONEZ_CORE -ErrorAction SilentlyContinue
if ($proc) { Stop-Process -Id $proc.Id -Force; Start-Sleep 1 }

Start-Process "Y:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe" -ArgumentList "--scene SkullbonezData/scenes/water_ball_test.scene" -WorkingDirectory "Y:\SkullbonezCore"
Start-Sleep 4

powershell.exe -Command {
    Add-Type -AssemblyName System.Drawing
    Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32 {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] public static extern int GetClassName(IntPtr hWnd, System.Text.StringBuilder lpClassName, int nMaxCount);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
    $targetPid = (Get-Process SKULLBONEZ_CORE -ErrorAction SilentlyContinue).Id
    if (!$targetPid) { Write-Error "Process not running"; exit 1 }
    $found = $null
    [Win32]::EnumWindows({param($h,$l)
        $p = 0; [Win32]::GetWindowThreadProcessId($h,[ref]$p) | Out-Null
        if ($p -eq $targetPid -and [Win32]::IsWindowVisible($h)) {
            $sb = New-Object System.Text.StringBuilder 256
            [Win32]::GetClassName($h,$sb,256) | Out-Null
            if ($sb.ToString() -eq "SkullbonezWindow") { $script:found = $h; return $false }
        }
        return $true
    }, [IntPtr]::Zero) | Out-Null
    if (!$found) { Write-Error "Window not found"; exit 1 }
    [Win32]::SetForegroundWindow($found) | Out-Null
    Start-Sleep -Milliseconds 500
    $r = New-Object Win32+RECT
    [Win32]::GetWindowRect($found,[ref]$r) | Out-Null
    $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
    $bmp = New-Object System.Drawing.Bitmap $w,$h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left,$r.Top,0,0,(New-Object System.Drawing.Size $w,$h))
    $bmp.Save("Y:\SkullbonezCore\Debug\screenshot.png",[System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Write-Output "Y:\SkullbonezCore\Debug\screenshot.png"
}

$proc = Get-Process SKULLBONEZ_CORE -ErrorAction SilentlyContinue
if ($proc) { Stop-Process -Id $proc.Id -Force }
```

Then pixel compare against baseline, **excluding HUD text bands** (title bar 0-30, top HUD 30-120, bottom HUD 540-600):

```pwsh
powershell.exe -Command {
    Add-Type -AssemblyName System.Drawing
    $baseline = New-Object System.Drawing.Bitmap "Y:\SkullbonezCore\Skills\skore-render-test\baseline_water_ball_test.png"
    $current  = New-Object System.Drawing.Bitmap "Y:\SkullbonezCore\Debug\screenshot.png"

    if ($baseline.Width -ne $current.Width -or $baseline.Height -ne $current.Height) {
        Write-Host "FAIL: Size mismatch"
        $baseline.Dispose(); $current.Dispose()
        exit 1
    }

    # Compare scene region only (y=120 to y=540)
    $diffCount = 0; $maxDiff = 0; $totalPixels = 0
    for ($y = 120; $y -lt 540; $y++) {
        for ($x = 0; $x -lt $baseline.Width; $x++) {
            $totalPixels++
            $bp = $baseline.GetPixel($x, $y)
            $cp = $current.GetPixel($x, $y)
            if ($bp.ToArgb() -ne $cp.ToArgb()) {
                $diffCount++
                $rd = [Math]::Abs([int]$bp.R - [int]$cp.R)
                $gd = [Math]::Abs([int]$bp.G - [int]$cp.G)
                $bd = [Math]::Abs([int]$bp.B - [int]$cp.B)
                $pixDiff = [Math]::Max($rd, [Math]::Max($gd, $bd))
                if ($pixDiff -gt $maxDiff) { $maxDiff = $pixDiff }
            }
        }
    }

    $pct = [Math]::Round(($diffCount / $totalPixels) * 100, 4)
    Write-Host "Scene region (120-540): $totalPixels pixels"
    Write-Host "Different: $diffCount ($pct%)"
    Write-Host "Max channel diff: $maxDiff"

    if ($pct -lt 0.1) { Write-Host "PASS: Scene region stable ($pct% diff)" }
    else { Write-Host "FAIL: Scene region has $pct% diff — visual regression detected" }

    $baseline.Dispose(); $current.Dispose()
}
```

**Pass criteria**: Scene region (y=120 to y=540) must be <0.1% different. HUD text differences are expected and ignored.

**If render test fails**: View the screenshot, compare visually, investigate and fix before proceeding.

### Step 3: Legacy Smoke Test

Quick check that default mode (no --scene) still runs without crashing:

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

### Step 4: Commit

Only if all previous steps pass:

```pwsh
cd Y:\SkullbonezCore
git add -A
git commit -m "<descriptive message>

Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

### When to update the baseline

If a change **intentionally** alters rendering (e.g. migrating a subsystem to shaders), recapture the baseline after visual verification:

```pwsh
Copy-Item "Y:\SkullbonezCore\Debug\screenshot.png" "Y:\SkullbonezCore\Skills\skore-render-test\baseline_water_ball_test.png" -Force
```

Include the updated baseline in the same commit.
