---
name: skore-build-pass
description: Standard development loop for SkullbonezCore. Plan, code, build, render test, smoke test, commit. Invoke after completing a code change to verify and commit it.
---

## Standard Build Pass

The full verify-and-commit loop after a code change. Every step must pass before proceeding to the next.

### Step 1: Build

Build using the `skore-build` skill. Must produce **0 errors and 0 warnings**.

```pwsh
$msbuild = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
& $msbuild "Y:\SkullbonezCore\SKULLBONEZ_CORE.sln" /p:Configuration=Debug /p:Platform=x86 /nologo /v:minimal
```

**If build fails**: Fix errors, rebuild. Do not proceed.

### Step 2: Render Test

Run the `skore-render-test` skill. It launches the engine with a scene file containing a `screenshot` directive, the engine writes a BMP via `glReadPixels`, and the skill compares it pixel-by-pixel against the baseline.

**Pass criteria**: <0.1% pixel difference (expect pixel-perfect in most cases).

**If render test fails**: View `Debug\screenshot.bmp` to inspect visually, investigate and fix before proceeding.

### Step 3: Legacy Smoke Test

Quick check that default mode (no --scene) still runs without crashing:

```pwsh
$proc = Get-Process SKULLBONEZ_CORE -ErrorAction SilentlyContinue
if ($proc) { Stop-Process -Id $proc.Id -Force; Start-Sleep 1 }

Start-Process "Y:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe" -WorkingDirectory "Y:\SkullbonezCore"
Start-Sleep 5
$proc = Get-Process SKULLBONEZ_CORE -ErrorAction SilentlyContinue
if ($proc) {
    Write-Host "PASS: Legacy mode running (PID $($proc.Id))"
    Stop-Process -Id $proc.Id -Force
} else {
    Write-Host "FAIL: Legacy mode crashed"
}
```

**If smoke test fails**: Debug with `skore-cdb-debug` skill.

### Step 4: Commit

Only if all previous steps pass:

```pwsh
cd Y:\SkullbonezCore
git add -A
git commit -m "<descriptive message>

Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

### When to update the baseline

If a change **intentionally** alters rendering (e.g. migrating a subsystem to shaders), recapture the baseline after visual verification. See the `skore-render-test` skill for instructions.

Include the updated baseline in the same commit.
