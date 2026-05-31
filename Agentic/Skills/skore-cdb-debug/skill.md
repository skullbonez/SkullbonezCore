---
name: skore-cdb-debug
description: Debug SkullbonezCore crashes and hangs with the Windows Console Debugger.
---

# skore-cdb-debug

Use for crashes, hangs, stack traces, exception inspection, and scripted debugger runs.

## CDB

Known local path:

```text
G:\cdb\cdb.exe
```

If that path is missing, install Windows Debugging Tools or locate `cdb.exe` with the Windows SDK.

## Build Debug

```bat
tools\validate_build.bat Debug
```

## Crash Run

```powershell
$REPO = (Resolve-Path .).Path
& "G:\cdb\cdb.exe" -g -G -lines -y "$REPO\Debug" -srcpath "$REPO\SkullbonezSource" `
    "$REPO\Debug\SKULLBONEZ_CORE.exe" "--vsync" "off" "--scene" "SkullbonezData/scenes/water_ball_test.scene"
```

## Useful Commands

| Command | Use |
|---------|-----|
| `g` | Continue |
| `k` / `kP` | Stack trace |
| `~*k` | All thread stacks |
| `.ecxr` | Exception context |
| `!analyze -v` | Crash analysis |
| `dv` | Locals |
| `bp module!function` | Breakpoint |
| `bl` / `bc *` | List or clear breakpoints |
| `q` | Quit |

Attach to an existing process only by PID:

```powershell
& "G:\cdb\cdb.exe" -p <PID>
```
