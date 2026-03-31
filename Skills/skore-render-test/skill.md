---
name: skore-render-test
description: Run a scene file through SkullbonezCore in scene mode, capture a screenshot, and output the path. Invoke when the user asks to run a render test, capture a scene screenshot, or verify rendering output.
---

## Render Test Skill

Launches SkullbonezCore with a `.scene` file, waits for it to render, captures a screenshot of the window, and outputs the path to the saved image.

### Prerequisites

The Debug exe must exist at `Y:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe`. If not, build first using the `build-skullbonez-core` skill.

### Steps

#### 1. Kill any running instance

```pwsh
$proc = Get-Process SKULLBONEZ_CORE -ErrorAction SilentlyContinue
if ($proc) { Stop-Process -Id $proc.Id -Force; Start-Sleep 1 }
```

#### 2. Launch with scene file

Default scene is `SkullbonezData/scenes/water_ball_test.scene`. Replace with any `.scene` file path.

```pwsh
Start-Process "Y:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe" -ArgumentList "--scene SkullbonezData/scenes/water_ball_test.scene" -WorkingDirectory "Y:\SkullbonezCore"
Start-Sleep 4
```

#### 3. Capture screenshot and output path

Uses .NET Framework PowerShell to bring window to foreground, capture, save, and output the path:

```pwsh
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
    $outPath = "Y:\SkullbonezCore\Debug\screenshot.png"
    $bmp.Save($outPath,[System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Write-Output $outPath
}
```

#### 4. Kill the process after capture

```pwsh
$proc = Get-Process SKULLBONEZ_CORE -ErrorAction SilentlyContinue
if ($proc) { Stop-Process -Id $proc.Id -Force }
```

### Output

The skill outputs the path to the screenshot file (e.g. `Y:\SkullbonezCore\Debug\screenshot.png`). View the image and analyse the rendering output.

### What to look for

- **Terrain**: Brown heightmap ground visible
- **Skybox**: Sky visible at top of frame
- **Ball**: Textured sphere with lighting
- **Water**: Blue transparent fluid at correct height
- **Render order**: Ball partially submerged — water covers bottom half
- **Shadow**: Dark disc on ground/water beneath ball
- **HUD text**: FPS, physics time, render time, model count
