---
name: skore-launch
description: Launch the SkullbonezCore executable. Invoke when the user asks to run, launch, or start SkullbonezCore.
---

## Launching SkullbonezCore

The executable is built into:
- Debug:   `{REPO}\Debug\SKULLBONEZ_CORE.exe`
- Release: `{REPO}\Release\SKULLBONEZ_CORE.exe`

### Check the exe exists before launching

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
Test-Path "$REPO\Debug\SKULLBONEZ_CORE.exe"
```

If it does not exist, build first using the `build-skullbonez-core` skill.

### Launch (Debug build)

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
Start-Process "$REPO\Debug\SKULLBONEZ_CORE.exe" -WorkingDirectory $REPO
```

### Launch (Release build)

```pwsh
$REPO = (git rev-parse --show-toplevel).Trim()
Start-Process "$REPO\Release\SKULLBONEZ_CORE.exe" -WorkingDirectory $REPO
```

Use `-WorkingDirectory $REPO` so the game can locate data files relative to the repo root.
