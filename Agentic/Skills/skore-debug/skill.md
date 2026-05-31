---
name: skore-debug
description: Open SkullbonezCore in Visual Studio for interactive debugging.
---

# skore-debug

Prefer `skore-cdb-debug` for automated crash and hang diagnosis. Use this skill when the user specifically wants Visual Studio debugging.

## Build

```bat
tools\validate_build.bat Debug
```

## Open Solution

```powershell
$REPO = (Resolve-Path .).Path
$devenv = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" `
    -latest -requires Microsoft.VisualStudio.Workload.NativeDesktop `
    -find "Common7\IDE\devenv.exe" | Select-Object -First 1
Start-Process $devenv -ArgumentList "`"$REPO\SKULLBONEZ_CORE.sln`"" -WorkingDirectory $REPO
```

Use F5 in Visual Studio, or configure command arguments such as:

```text
--renderer dx12 --vsync off --scene SkullbonezData/scenes/water_ball_test.scene
```
