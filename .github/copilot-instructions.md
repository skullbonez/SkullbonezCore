# Copilot Instructions for SkullbonezCore

A Windows 3D graphics and physics simulation engine written in C++ using OpenGL, originally authored in 2005 and modernized through a series of cleanup phases.

## Workflow Rules

- **At the start of every session**, do the following in order:
  1. Read `Copilot/SessionState.md` to understand current progress, remaining work, and known bugs
  2. Load all skills by invoking each skill file below — ask the user "I've loaded all skills and read the session state. Ready to continue?" before doing any work
- **After every change**, run the `skore-build-pipeline` skill to build, test, update baselines, run the perf test, and commit. Every commit must include updated reference images and a perf artifact.
- **Never commit or push to GitHub** unless the user explicitly asks.

## Skills — Load These at Session Start

| Skill | Path |
|-------|------|
| **skore-build-pipeline** (use this for every commit) | `Copilot/Skills/skore-build-pipeline/skill.md` |
| skore-render-test | `Copilot/Skills/skore-render-test/skill.md` |
| skore-build | `Copilot/Skills/skore-build/skill.md` |
| skore-cdb-debug | `Copilot/Skills/skore-cdb-debug/skill.md` |
| skore-launch | `Copilot/Skills/skore-launch/skill.md` |

## Key Docs

| Doc | Path |
|-----|------|
| **Session state / handoff** | `Copilot/SessionState.md` |
| **Known bugs** | `Copilot/Bugs.md` |
| Progress tracker | `Copilot/Plans/progress.md` |
| FFP migration plan | `Copilot/Plans/ffp-to-shader-migration.md` |

## Build

Open `SKULLBONEZ_CORE.sln` in Visual Studio 2019+ and build, or use MSBuild:

```bat
msbuild SKULLBONEZ_CORE.sln /p:Configuration=Debug /p:Platform=Win32
msbuild SKULLBONEZ_CORE.sln /p:Configuration=Release /p:Platform=Win32
```

- Output: `Debug\SKULLBONEZ_CORE.exe` or `Release\SKULLBONEZ_CORE.exe`
- Target: **Win32 (x86)** — do not change to x64
- Toolset: v143 (VS2019+)
- The build must produce **zero warnings** (compiled at `/W4`)

There are no automated tests.

## Architecture

**Entry point** → `SkullbonezInit.cpp` (WinMain) → `SkullbonezWindow` (OpenGL context) → `SkullbonezRun` (main loop)

`SkullbonezRun` owns and orchestrates everything: it initialises all subsystems in `Initialise()` and drives input→physics→collision→render each frame in `Run()`.

**Layer breakdown:**

| Layer | Key Classes |
|---|---|
| Platform | `SkullbonezWindow` (Singleton), `SkullbonezInput`, `SkullbonezTimer` |
| Rendering | `SkullbonezTextureCollection` (Singleton), `SkullbonezSkyBox` (Singleton), `SkullbonezTerrain`, `SkullbonezHelper`, `SkullbonezText` |
| Camera | `SkullbonezCameraCollection` (Singleton, 3 fixed cameras), `SkullbonezCamera` |
| Game Objects | `SkullbonezGameModelCollection` (`std::vector<GameModel>`), `SkullbonezGameModel` |
| Physics | `SkullbonezRigidBody`, `SkullbonezCollisionResponse`, `SkullbonezWorldEnvironment` |
| Collision | `SkullbonezDynamicsObject` (abstract), `SkullbonezBoundingSphere` (concrete) |
| Math | `SkullbonezVector3`, `SkullbonezQuaternion`, `SkullbonezRotationMatrix`, `SkullbonezGeometricMath` |

**`SkullbonezCommon.h`** is the global configuration header — it holds all `#define` constants, compile-time configuration flags (`FULLSCREEN_MODE`, `SCREEN_X`, etc.), universal includes, and the `constexpr` FNV-1a hash function used for string lookups at compile time.

## Key Conventions

### Naming
- All project classes are prefixed `Skullbonez` and live one-class-per-file: `SkullbonezFoo.h` / `SkullbonezFoo.cpp`
- Methods: PascalCase — `GetPosition()`, `RunPhysics()`, `CalculateGravity()`
- Members: camelCase with intent prefixes — `is` (bools), `f` (floats in ctors), `p` (pointers), `s` (static/struct members), `c` (class-instance members)
- Constants: `UPPER_CASE_UNDERSCORE` in `SkullbonezCommon.h`

### Namespace hierarchy
```
SkullbonezCore::Basics
SkullbonezCore::Environment
SkullbonezCore::Textures
SkullbonezCore::Hardware
SkullbonezCore::Math::Vector / ::Orientation / ::CollisionDetection
SkullbonezCore::Physics
SkullbonezCore::GameObjects
```

### Singletons
`SkullbonezWindow`, `SkullbonezTextureCollection`, `SkullbonezCameraCollection`, and `SkullbonezSkyBox` are singletons accessed via static instance methods. Implemented with static locals (not `new`).

### Compile-time string keys (hashes)
Textures and cameras are looked up by `constexpr` FNV-1a hashes defined in `SkullbonezCommon.h`:
```cpp
// e.g. TEXTURE_GROUND, CAMERA_GAME_MODEL_1
textureCollection.GetTexture(TEXTURE_GROUND);
```
Never use raw string keys at runtime; add a new `constexpr` hash constant to `SkullbonezCommon.h` instead.

### Memory / ownership
- **No raw `new`/`delete`** — use `std::unique_ptr` for owned heap objects
- Fixed-size resource collections (textures, cameras) use stack-allocated arrays sized by constants in `SkullbonezCommon.h`
- Variable-size game-object collections use `std::vector`
- Per-frame heap allocations are forbidden (the allocation elimination refactor is complete)

### Error handling
Use `std::runtime_error` (or a subclass). The old `catch(char*)` pattern has been removed.

### Platform & CRT
Windows-only. Use secure CRT variants: `fopen_s`, `strcpy_s`, `vsprintf_s`. Do not introduce POSIX equivalents.

## Assets
- Textures: JPEG files in `SkullbonezData\` loaded via the bundled `ThirdPtySource\JPEG` library
- Terrain heightmap: `SkullbonezData\terrain.raw` (binary RAW format)
- `ThirdPtySource\MemMgr\` is present but not used — do not re-enable it
