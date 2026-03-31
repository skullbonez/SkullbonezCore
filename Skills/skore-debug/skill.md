---
name: skore-debug
description: Launch SkullbonezCore under the Visual Studio debugger to verify it starts and renders correctly. Invoke when the user asks to debug, test, or verify SkullbonezCore is running without errors.
---

## Debugging SkullbonezCore in Visual Studio

Launch the Debug exe under the VS debugger so exceptions, output logs, and rendering errors are visible and can be iterated on.

### Check the exe exists before launching

```pwsh
Test-Path "Y:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe"
```

If it does not exist, build first using the `build-skullbonez-core` skill.

### Launch under the Visual Studio debugger

Prefer using the **existing Visual Studio instance** via DTE COM automation — opens the solution if needed, then starts debugging (equivalent to F5):

```pwsh
$dte = try { [System.Runtime.InteropServices.Marshal]::GetActiveObject("VisualStudio.DTE.17.0") } catch { $null }

if ($dte) {
    # Open the solution if not already open
    if (!$dte.Solution.IsOpen -or $dte.Solution.FullName -notlike "*SKULLBONEZ*") {
        $dte.Solution.Open("Y:\SkullbonezCore\SKULLBONEZ_CORE.sln")
        Start-Sleep 3
    }
    $dte.ExecuteCommand("Debug.Start")
    Write-Host "Debugging started via existing VS instance"
} else {
    # Fall back to opening a new VS instance with the solution
    $devenv = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" `
        -latest -requires Microsoft.VisualStudio.Workload.NativeDesktop `
        -find "Common7\IDE\devenv.exe" | Select-Object -First 1
    Start-Process $devenv -ArgumentList '"Y:\SkullbonezCore\SKULLBONEZ_CORE.sln"' -WorkingDirectory "Y:\SkullbonezCore"
    Write-Host "Opened solution in new VS instance - press F5 to debug"
}
```

### Verifying it is running

Wait ~5 seconds, then check the process is alive and the window exists:

```pwsh
# Check process is still alive (crash = not found)
$proc = Get-Process SKULLBONEZ_CORE -ErrorAction SilentlyContinue
if ($proc) { Write-Host "Running (PID $($proc.Id))" } else { Write-Host "Process not found - likely crashed" }

# Check the window is visible
Add-Type -TypeDefinition 'using System;using System.Runtime.InteropServices;public class Win32{[DllImport("user32.dll")]public static extern IntPtr FindWindow(string c,string t);}'
$hwnd = [Win32]::FindWindow($null, "::SKULLBONEZ CORE::")
if ($hwnd -ne [IntPtr]::Zero) { Write-Host "Window found - rendering OK" } else { Write-Host "Window not found" }
```

### If it fails to start

- Check the Visual Studio Output window and Debug console for exception messages
- All exceptions use `std::runtime_error` and will be caught and shown in a message box
- Ensure `SkullbonezData\` assets are present relative to `Y:\SkullbonezCore\`
- If the issue is in code, fix it, rebuild using `build-skullbonez-core`, then retry
