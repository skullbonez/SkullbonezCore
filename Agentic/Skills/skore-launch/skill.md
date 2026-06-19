---
name: skore-launch
description: Launch SkullbonezCore with the correct working directory.
---

# skore-launch

Use `Start-Process` from the repository root and keep `-WorkingDirectory` set to the repo so assets resolve.

## Examples

```powershell
$REPO = (Resolve-Path .).Path
Start-Process "$REPO\Profile\SKULLBONEZ_CORE.exe" -WorkingDirectory $REPO
```

```powershell
$REPO = (Resolve-Path .).Path
$proc = Start-Process "$REPO\Profile\SKULLBONEZ_CORE.exe" `
    -ArgumentList "--renderer dx12 --vsync off --scene SkullbonezData/scenes/water_ball_test.scene.json" `
    -WorkingDirectory $REPO -PassThru
```

If the exe is missing, build first:

```bat
tools\validate_build.bat Profile
```

Kill only by PID from `$proc.Id` if you launched the process.
