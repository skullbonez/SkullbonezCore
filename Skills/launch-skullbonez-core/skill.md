---
name: launch-skullbonez-core
description: Launch the SkullbonezCore executable. Invoke when the user asks to run, launch, or start SkullbonezCore.
---

## Launching SkullbonezCore

The executable is built into:
- Debug:   `Y:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe`
- Release: `Y:\SkullbonezCore\Release\SKULLBONEZ_CORE.exe`

### Check the exe exists before launching

```pwsh
Test-Path "Y:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe"
```

If it does not exist, build first using the `build-skullbonez-core` skill.

### Launch (Debug build)

```pwsh
Start-Process "Y:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe" -WorkingDirectory "Y:\SkullbonezCore"
```

### Launch (Release build)

```pwsh
Start-Process "Y:\SkullbonezCore\Release\SKULLBONEZ_CORE.exe" -WorkingDirectory "Y:\SkullbonezCore"
```

Use `-WorkingDirectory "Y:\SkullbonezCore"` so the game can locate data files relative to the repo root.
