---
name: skore-cdb-debug
description: Debug SkullbonezCore using CDB (Console Debugger). Invoke when the user asks to debug a crash, inspect a hang, get a stack trace, or investigate a runtime error.
---

## Debug SkullbonezCore with CDB

CDB is the Windows console debugger — fully scriptable from the terminal. Use it to catch crashes, get stack traces, inspect variables, and set breakpoints.

### CDB Path

```
C:\Program Files (x86)\Windows Kits\10\Debuggers\x86\cdb.exe
```

### Quick crash diagnosis

Launch the exe under CDB. It will break on exceptions automatically:

```pwsh
& "C:\Program Files (x86)\Windows Kits\10\Debuggers\x86\cdb.exe" -g -G -lines -y "Y:\SkullbonezCore\Debug" -srcpath "Y:\SkullbonezCore\SkullbonezSource" "Y:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe"
```

Flags:
- `-g` — go on first breakpoint (skip initial break)
- `-G` — go on final breakpoint (skip exit break)
- `-lines` — enable source line info
- `-y` — symbol path (PDB location)
- `-srcpath` — source path for source-level debugging

### Launch with scene file

```pwsh
& "C:\Program Files (x86)\Windows Kits\10\Debuggers\x86\cdb.exe" -g -G -lines -y "Y:\SkullbonezCore\Debug" -srcpath "Y:\SkullbonezCore\SkullbonezSource" "Y:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe" "--scene" "SkullbonezData/scenes/water_ball_test.scene"
```

### Interactive debugging

Launch CDB in async mode so you can send commands interactively:

```pwsh
# Start CDB in async mode
powershell mode="async" shellId="cdb"
& "C:\Program Files (x86)\Windows Kits\10\Debuggers\x86\cdb.exe" -lines -y "Y:\SkullbonezCore\Debug" -srcpath "Y:\SkullbonezCore\SkullbonezSource" "Y:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe"
```

Then use `write_powershell` with the shellId to send CDB commands.

### Essential CDB commands

| Command | Description |
|---------|-------------|
| `g` | Go (continue execution) |
| `k` | Stack trace |
| `kP` | Stack trace with full parameters |
| `~*k` | Stack traces for all threads |
| `.ecxr` | Switch to exception context |
| `!analyze -v` | Verbose crash analysis |
| `bp module!function` | Set breakpoint |
| `bl` | List breakpoints |
| `bc *` | Clear all breakpoints |
| `dv` | Display local variables |
| `dt varname` | Display variable type/value |
| `p` | Step over |
| `t` | Step into |
| `gu` | Step out (go up) |
| `r` | Display registers |
| `.frame N` | Switch to stack frame N |
| `q` | Quit debugger |

### Typical crash debugging workflow

1. Launch in async mode (without `-g` so it breaks at start):
   ```pwsh
   & "C:\Program Files (x86)\Windows Kits\10\Debuggers\x86\cdb.exe" -lines -y "Y:\SkullbonezCore\Debug" -srcpath "Y:\SkullbonezCore\SkullbonezSource" "Y:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe"
   ```

2. Set breakpoints if needed:
   ```
   bp SkullbonezRun::Initialise
   bp SkullbonezRun::Render
   ```

3. Continue execution:
   ```
   g
   ```

4. When it crashes, get the analysis:
   ```
   !analyze -v
   k
   dv
   ```

5. Quit:
   ```
   q
   ```

### Setting breakpoints by source line

```
bp `SkullbonezRun.cpp:304`
```

### Conditional breakpoints

```
bp SkullbonezRun::Render ".if (poi(this+0x4) == 0) { k; } .else { gc }"
```

### Notes

- The exe is **Win32 (x86)** — always use the x86 CDB, not x64
- PDB files are in `Debug\` alongside the exe
- Source files are in `SkullbonezSource\`
- If the process is already running, attach instead: `cdb -p <PID>`
