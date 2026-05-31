---
name: skore-build
description: Build SkullbonezCore with the repository MSBuild wrapper.
---

# skore-build

Use when the user asks to build, compile, or rebuild.

## Commands

```bat
tools\validate_build.bat Debug
tools\validate_build.bat Profile
tools\validate_build.bat Release
```

The wrapper locates MSBuild, builds x64, and enforces zero warnings.

Build outputs:
- `Debug\SKULLBONEZ_CORE.exe`
- `Profile\SKULLBONEZ_CORE.exe`
- `Release\SKULLBONEZ_CORE.exe`

If the build fails with `LNK1168`, an exe is locked. Kill only a PID you launched, then rebuild.
