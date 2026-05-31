# SkullbonezCore â€” Agent Guide

Concise operational reference for AI agents. Does not duplicate skill files â€” treat those as authoritative for their specific tasks.

---

## Mandatory Rules (Always)

### Ask Clearly
Use the structured user-input mechanism provided by your runtime when it exists. If it does not, ask concise plain-text questions. This includes:
- Choosing test scope before running the build pipeline
- Confirming a commit message
- Asking about performance acceptability
- Any decision that requires user input

### â±ï¸ Time All Large Tasks
Before starting any large task (multi-file refactor, new feature, pipeline run, debugging session):
1. Note the wall-clock start time
2. On completion, report elapsed time

Applies to anything expected to take >2 minutes or >10 tool calls.

### ðŸ”¬ Run the Pipeline After Every Code Change
After any code change, run the `skore-build-pipeline` skill before committing. Every commit must pass the full pipeline (build, test suite, visual regression, perf). See `Agentic/Skills/skore-build-pipeline/skill.md` for steps.

### ðŸ“ Update SessionState After Every Commit
`Agentic/SessionState.md` is the handoff document between sessions and machines. After every commit, update it: new last commit hash, completed backlog items, new bugs or notes. Do not leave it stale.

### ðŸš« Never Commit Without Permission
Always use the available structured user-input mechanism to confirm before committing or pushing, unless the user explicitly said "commit" or "push" when invoking the task.

### ðŸŽ¯ Kill Processes by PID Only
**Never** use `Stop-Process -Name` or `taskkill /IM`. Multiple agents may run independent copies of `SKULLBONEZ_CORE.exe` from different folders. Killing by name corrupts other agents' pipeline runs.

Always kill by PID: `Stop-Process -Id $proc.Id` â€” capture `$proc` via `Start-Process -PassThru`.

---

## Session Start (/init)

> **If your session state is empty or you are starting on a new machine:** `Agentic/SessionState.md` is committed to the repo and is the source of truth. Always read it â€” never assume project state from training data or prior context.

1. **Read `Agentic/SessionState.md`** â€” current branch, last commit, backlog, known bugs, uncommitted changes
2. **Load all skills** by reading each file in the Skills table below
3. **Ask the user** via the available user-input mechanism: *"I've loaded all skills and read the session state. Ready to continue?"*

Do not begin any work until all three steps are complete.

### Skills to Load

| Skill | Path |
|-------|------|
| **skore-build-pipeline** (use for every commit) | `Agentic/Skills/skore-build-pipeline/skill.md` |
| skore-render-test | `Agentic/Skills/skore-render-test/skill.md` |
| skore-build | `Agentic/Skills/skore-build/skill.md` |
| skore-cdb-debug | `Agentic/Skills/skore-cdb-debug/skill.md` |
| skore-launch | `Agentic/Skills/skore-launch/skill.md` |
| skore-branch-and-snatch | `Agentic/Skills/skore-branch-and-snatch/skill.md` |
| skore-cpu-profiler | `Agentic/Skills/skore-cpu-profiler/skill.md` |

---

## Codebase Orientation

A Windows C++17 3D physics/graphics engine, originally written in 2005, now fully modernised. Three rendering backends (GL 3.3, DX11, DX12) produce visually identical output.

**Entry point:** `WinMain` â†’ `SkullbonezWindow` (context creation) â†’ `SkullbonezRun` (main loop)

`SkullbonezRun::Initialise()` sets up all subsystems. `SkullbonezRun::Run()` drives: input â†’ physics â†’ collision â†’ render.

### Key Source Files

| What | File |
|------|------|
| Global constants, FNV hashes | `SkullbonezSource/SkullbonezCommon.h` |
| Main render loop | `SkullbonezSource/SkullbonezRun.h/.cpp` |
| Rendering backends | `SkullbonezSource/SkullbonezRenderBackendGL/DX11/DX12.cpp` |
| Physics / rigid body | `SkullbonezSource/SkullbonezRigidBody.cpp` |
| Broadphase spatial grid | `SkullbonezSource/SkullbonezSpatialGrid.h/.cpp` |
| Window / context creation | `SkullbonezSource/SkullbonezWindow.h/.cpp` |

### Key Data Files

| What | Path |
|------|------|
| GLSL/HLSL shaders | `SkullbonezData/shaders/` |
| Test scenes | `SkullbonezData/scenes/` |
| Engine config | `SkullbonezData/engine.cfg` |

### Documentation Style
Graphics API calls must be documented **verbosely** â€” thorough inline comments explaining what each call does and why, ASCII art diagrams where they aid understanding of resource layout, pipeline stages, or data flow, and links to official docs (Khronos for GL/GLSL, Microsoft Learn for DX11/DX12). Sparse or uncommented API code is not acceptable.

### Naming Conventions
- Classes: `Skullbonez` prefix, PascalCase â€” one class per `.h/.cpp` pair
- Methods: PascalCase â€” `GetPosition()`, `RenderFluid()`
- Members: camelCase with prefix â€” `m_` member, `is` bool, `f` float ctor param, `p` pointer
- Constants: `UPPER_CASE` in `SkullbonezCommon.h`

---

## Build

```pwsh
$msbuild = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
& $msbuild SKULLBONEZ_CORE.sln /p:Configuration=Debug /p:Platform=x64
```

- Target: **x64** â€” do not change
- Must produce **0 errors, 0 warnings** (`/W4`)
- If `LNK1168`: kill the running exe by PID first
- Python: use the repository validation scripts or `py`; Pillow required

---

## Key Gotchas

**GL context lifecycle** â€” `SkullbonezRun` destructor must fire before `wglDeleteContext`. Enforced by nested scope in `SkullbonezInit.cpp` â€” do not restructure it.

**Texture/camera lookups** â€” always use `constexpr` FNV-1a hash constants from `SkullbonezCommon.h`, never raw strings at runtime.

**No per-frame allocation** â€” the broadphase spatial grid uses a zero-allocation flat hash table. Don't introduce heap allocations in hot paths.

**Singleton reset** â€” after calling `Destroy()` on `SkyBox`, `TextureCollection`, `CameraCollection`, or `Window`, call `ResetGLResources()` before next use.

**Renderer selection** â€” `--renderer gl` / `--renderer dx11` / `--renderer dx12` at launch (default: GL).

---

## Debug Logging

Use `Log().Writef()` to write diagnostic data to a CSV or text file during a debug build. No setup required â€” the file is created on the first write and closed automatically on exit.

```cpp
Log().Writef( "Debug/physics.csv", "terrain,%d,%.2f,%.2f,%.2f\n", frame, x, y, z );
```

- `Log()` is a free function injected by `SkullbonezCommon.h` â€” available everywhere, no extra includes.
- Each unique filename gets its own file handle, lazily opened on first write.
- Same filename in subsequent calls appends to the already-open handle.
- **Compiled out entirely in Release** (`#ifdef _DEBUG` in `SkullbonezLog.cpp`) â€” zero overhead.
- Do not call `fopen` / `fclose` / `fflush` manually; the singleton owns the lifecycle.

To expose logging from a subsystem (e.g. `CollisionResponse`), add a `#ifdef _DEBUG` static path setter so `Run` can route the output path from the scene file:

```cpp
// In the subsystem header (debug only):
#ifdef _DEBUG
    static void SetLogPath( const char* path );
#endif

// In Run.cpp when loading a scene:
#ifdef _DEBUG
    CollisionResponse::SetLogPath( scene.GetPhysicsLogPath() );
#endif
```

The scene file opts in with: `physics_log Debug/my_output.csv`

---

## Key Docs

| Doc | Path |
|-----|------|
| Session state / backlog / bugs | `Agentic/SessionState.md` |
| Build pipeline (full steps) | `Agentic/Skills/skore-build-pipeline/skill.md` |
| Progress tracker | `Agentic/Plans/progress.md` |
