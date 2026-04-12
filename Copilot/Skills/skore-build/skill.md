---
name: skore-build
description: Build the SkullbonezCore solution using MSBuild. Invoke when the user asks to build, compile, or rebuild SkullbonezCore.
---

## Building SkullbonezCore

The solution is at `SKULLBONEZ_CORE.sln` in the repo root.

Available configurations:
- `Debug|x86`
- `Release|x86`

### Build command

Use MSBuild via the Visual Studio Developer tools. Find MSBuild with `vswhere`, then invoke it using **`mode="async"`** so the user sees output streaming live as the build progresses. Read output with `read_powershell` after completion.

```pwsh
$REPO     = (git rev-parse --show-toplevel).Trim()
$msbuild  = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
& $msbuild "$REPO\SKULLBONEZ_CORE.sln" /p:Configuration=Debug /p:Platform=x86
```

Replace `Debug` with `Release` for a release build.

### Rebuild (clean + build)

```pwsh
$REPO    = (git rev-parse --show-toplevel).Trim()
$msbuild = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
& $msbuild "$REPO\SKULLBONEZ_CORE.sln" /t:Rebuild /p:Configuration=Debug /p:Platform=x86
```

### Output

Build artifacts land in:
- `{REPO}\Debug\` for Debug builds
- `{REPO}\Release\` for Release builds
