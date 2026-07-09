# Physics VCXPROJ Split Plan

Date: 2026-07-09 (revised; original 2026-07-08)
Status: Proposed
Owner: Build / Physics architecture

## Goal

Split the physics engine into its own Visual Studio static-library project,
`SKULLBONEZ_PHYSICS.vcxproj`, in the same spirit as `SKULLBONEZ_MATHS.vcxproj`,
with **zero performance change** in every existing configuration. As a final,
separate phase, enable whole-program optimization (`/GL` + `/LTCG`) for the
Release configuration and add a new manual-numbers-only `Profile-WPO`
configuration; Debug and Profile stay byte-for-byte untouched.

This is not only a build cleanup. The split should make physics ownership and
dependencies visible:

- `SKULLBONEZ_PHYSICS` owns simulation, collision, solver, body/collider stores,
  public physics handles/API, deterministic diagnostics data, and physics tests'
  reusable implementation code.
- `SKULLBONEZ_CORE` owns runtime orchestration, rendering, UI, editor, replay
  presentation, and app-specific feature wiring.
- `SKULLBONEZ_TESTS` links physics as a library instead of compiling physics
  `.cpp` files directly.

## Why

Physics is large enough to deserve a build boundary. Today `SKULLBONEZ_CORE` and
`SKULLBONEZ_TESTS` both compile many `SkullbonezSource\Physics\*.cpp` files
directly, while maths already has a clean static-library project. A physics
library would:

- shorten and clarify project files,
- make test linkage match runtime linkage,
- expose physics dependencies that currently hide inside the monolithic core
  project,
- create a natural boundary for the ongoing PhysicsWorld/solver decomposition,
- make it harder for rendering/runtime concepts to creep into physics unnoticed.

## Verified Build Ground Truth (2026-07-09)

These facts were verified from the actual build tlogs
(`Profile\SKULLBONEZ_CORE.tlog\CL.command.1.tlog`,
`Profile\SKULLBONEZ_MATHS\SKULLBONEZ_MATHS.tlog\CL.command.1.tlog`) and the
project files. They are the performance contract for this plan:

- **No `/GL` and no `/LTCG` exist anywhere in the build today.** There is no
  cross-translation-unit optimization to lose by archiving objects into a
  static library. The `.obj` files produced are identical either way.
- Non-debug configurations get `/O2 /fp:precise /MT /GR- /EHsc /std:c++17`
  through the MSBuild platform-props defaults, not through explicit
  `<Optimization>` tags. `SKULLBONEZ_MATHS` (static lib) TUs compile with flags
  **identical** to `SKULLBONEZ_CORE` (exe) TUs, config for config.
- Per-config preprocessor defines: Profile adds
  `SKULLBONEZ_PROFILE_ENABLED;SKULLBONEZ_PLATFORM_PROFILER_PIX;USE_PIX`; all
  configs carry `_HAS_STD_BYTE=0`. These are ODR/behavior-sensitive and must be
  cloned exactly into the new project.
- `/fp:precise` and the absence of any `/arch` flag are determinism-sensitive.
  The new project must not introduce either.
- `tools\validate_build.bat` passes any configuration name through to msbuild
  (`/p:Configuration=%CONFIG%`) and builds with `/warnaserror`, so a new
  `Profile-WPO` configuration works with existing tooling once it exists in the
  solution; only the script's usage comment needs updating.

Consequence: the split is performance-neutral **by construction**, and the
proof is free — after a pure project-file move with zero source changes,
`tools\validate_physics.bat` must stay byte-exact. If the CSV moves at all, the
new project's flags are wrong; stop and fix the flags, do not touch baselines.

## Current Coupling To Resolve

The first split may need temporary exceptions, but the final boundary should not
leave physics depending on rendering or runtime presentation.

Known coupling, with the tornado path mapped precisely (it is the loudest back
edge and is evicted first, in Phase 1):

- **Tornado debug draw**: `TornadoField::RenderVectors` and
  `TornadoSystem::RenderVectors` (`SkullbonezSource\Physics\TornadoField.h`
  L111/L144) take `Rendering::IRenderCommandContext&`, forcing a Rendering
  forward-declaration into the header and Rendering includes into
  `TornadoField.cpp` (L311/L327). The feature then tunnels through a five-layer
  pass-through tower: `RunPasses.cpp` L1783 →
  `PhysicsEngine::RenderTornadoFieldVectors` → `PhysicsScene` (L747) →
  `PhysicsWorld` (L1600) → `TornadoField/TornadoSystem::RenderVectors`, plus a
  parallel entry on `GameModelCollection` (L1554). `PhysicsWorld.h` forward
  declares `IRenderCommandContext` only for this path.
  Everything the visualization needs is **already public physics data**:
  `GetConfig()`, `GetElapsedSeconds()`, `ActiveVortices()`, and the pure static
  helpers `TornadoField::SampleAccelerationForConfig(...)` and
  `TornadoSystem::BuildActiveVortices(...)`. The fix is inversion, not
  relocation: a runtime-side overlay reads that data and submits lines itself.
- `PhysicsEngine` / `PhysicsScene` expose render instance presentation records
  and mutable render instance storage.
- Runtime render passes consume physics debug visualizers, collider/body stores,
  tornado configs, and physics pipeline/debug records.
- Rendering code reads physics shape/store types such as `ColliderStore`,
  `PhysicsBodyStore`, and `ConvexHullShape`. (Rendering depending on physics
  data types is the acceptable direction; physics depending on rendering is
  not.)
- Physics includes core utilities such as `Common.h`, `Config.h`, `FatalError.h`,
  `Profiler.h`, `WorkerPool.h`, and diagnostics sinks.

Core utilities and maths dependencies are acceptable for the initial library.
Note for Phase 0: a static library may carry unresolved externals — physics
calling `WorkerPool`/`Profiler`/`FatalError` symbols compiled only into the
executables is legal as long as each final exe link provides them (CORE and
TESTS both already do). A small `SKULLBONEZ_CORELIB` is a follow-up plan, not a
prerequisite.

**Tornado simulation stays inside the physics library.** `ApplyTornadoField` is
called mid-solver (`PhysicsWorld.cpp` L2197, between force integration and
broadphase), its capture/eject arrays are in the replay solver snapshot, and its
call position is determinism-load-bearing. Only the visualization is a back
edge. Do not move force application out of the fixed step or introduce a
callback across the library boundary.

## Non-Goals

- Do not rewrite solver behavior as part of the project split.
- Do not change physics determinism or refresh baselines.
- Do not remove Butterfly Effect / replay prediction functionality.
- Do not split physics into a DLL. A DLL boundary is the one project structure
  that genuinely costs performance (import-call indirection, opaque to LTCG).
  Use a static library like maths unless a separate owner decision says
  otherwise.
- Do not use this as a broad formatting or include-order pass.
- Do not add `/arch` flags or change `/fp:precise` anywhere, in any phase.
- Do not touch Debug or Profile flags in Phase 6; WPO applies to Release and
  the new Profile-WPO configuration only.

## Proposed Target Shape

### Project Graph

```text
SKULLBONEZ_MATHS.lib
        ^
        |
SKULLBONEZ_PHYSICS.lib
        ^
        |
SKULLBONEZ_CORE.exe

SKULLBONEZ_TESTS.exe -> SKULLBONEZ_PHYSICS.lib + SKULLBONEZ_MATHS.lib
```

If physics still needs core utility objects that are compiled only into
`SKULLBONEZ_CORE`, rely on exe-link resolution short-term (see Phase 0 note) and
raise a deliberately small `SKULLBONEZ_CORELIB` follow-up plan rather than
reaching backward from physics into the executable.

### Configurations (after Phase 6)

| Configuration | Optimization | WPO (`/GL`+`/LTCG`) | Purpose |
|---------------|--------------|---------------------|---------|
| Debug | `/Od` | no | Physics logging, CDB, determinism baselines |
| Profile | `/O2` | no | Validation gates, perf baselines, agent builds |
| Profile-WPO | `/O2` | **yes** | Manual performance numbers only; never a gate |
| Release | `/O2` | **yes** | Shipping |

### Candidate Physics Library Contents

Start with source files that already live under `SkullbonezSource\Physics\` and
are used by both runtime and tests:

- `BoundingBox.cpp`
- `BoundingSphere.cpp`
- `ColliderStore.cpp`
- `ConvexHullShape.cpp`
- `ObjectContactManifold.cpp`
- `PersistentContactSolver.cpp`
- `PhysicsApi.cpp`
- `PhysicsBodyStore.cpp`
- `PhysicsDiagnosticsSink.cpp`
- `PhysicsEngine.cpp`
- `PhysicsObjectPolicy.cpp`
- `PhysicsScene.cpp`
- `PhysicsWorld.cpp`
- `Ragdoll.cpp`
- `SimulationSystem.cpp`
- `SleepIslandSystem.cpp`
- `SpatialGrid.cpp`
- `TerrainContactManifold.cpp`
- `TornadoField.cpp`

Reclassify any file that cannot link cleanly without rendering/runtime symbols.
If a file is mostly simulation plus a small render/debug helper, split the helper
out before moving the simulation file into the library (Phase 1 does exactly
this for the tornado and debug visualizers, before any file moves).

## Step-By-Step Implementation

Do steps in order. Phase 1 (break render back edges) deliberately comes
**before** the project is created, so the library is born clean instead of
carrying temporary Rendering includes that Phase 5's boundary check would then
have to unwind.

### Phase 0 - Inventory

- [ ] **0.1** Inventory physics `.cpp` and `.h` files currently listed in
  `SKULLBONEZ_CORE.vcxproj`, `SKULLBONEZ_TESTS.vcxproj`, and their `.filters`.
  Record which are pure simulation, which are debug/diagnostics, and which touch
  rendering/runtime concepts. No code change.
- [ ] **0.2** Inventory link dependencies for the candidate physics files:
  maths, core utility objects, rendering, runtime allocation, profiler, worker
  pool, replay snapshot types, and diagnostics sinks. Classify each as
  header-only, exe-link-resolved (acceptable unresolved external), or a true
  back edge that Phase 1 must break. No code change.
- [ ] **0.3** Record the core-utility decision: which symbols the physics lib
  will reference as unresolved externals resolved by each exe link, and whether
  a `SKULLBONEZ_CORELIB` follow-up plan is warranted. This decision must not
  block Phase 2. No code change.

### Phase 1 - Break Render/Runtime Back Edges (before any project edits)

- [ ] **1.1** Evict the tornado debug draw by inversion. Add a runtime-side
  overlay (suggested: `SkullbonezSource\Runtime\Debug\TornadoDebugOverlay.cpp/h`)
  that reads tornado configs and elapsed seconds through the existing public
  accessors, rebuilds active vortices via the public static
  `TornadoSystem::BuildActiveVortices(...)`, samples the field via the public
  static `TornadoField::SampleAccelerationForConfig(...)`, and submits debug
  lines itself through the runtime's own render command context. Wire the
  `RunPasses.cpp` call site to the overlay. Then **delete**:
  `TornadoField::RenderVectors`, `TornadoSystem::RenderVectors`, the
  `m_lineData` scratch member, `PhysicsWorld::RenderTornadoFieldVectors`,
  `PhysicsScene::RenderTornadoFieldVectors`,
  `PhysicsEngine::RenderTornadoFieldVectors`,
  `GameModelCollection::RenderTornadoFieldVectors`, and the
  `Rendering::IRenderCommandContext` forward declarations from `TornadoField.h`
  and `PhysicsWorld.h`. No simulation code changes. Gates:
  `tools\validate_dx12_renderer.bat` (overlay still draws) and
  `tools\validate_physics.bat` (must be byte-exact — nothing in the fixed step
  changed).
- [ ] **1.2** Move render instance presentation ownership out of
  `PhysicsEngine` / `PhysicsScene` into runtime/rendering owner types, or expose
  only plain physics-owned data records that rendering consumes externally.
  Gate: `tools\validate_full.bat`.
- [ ] **1.3** Keep physics debug data production in physics, but move debug draw
  submission and visualizer ownership to runtime/rendering, using the same
  inversion pattern as 1.1 (physics exposes data; runtime draws). Gate:
  `tools\validate_dx12_renderer.bat`.
- [ ] **1.4** `rg -n "Rendering::" SkullbonezSource/Physics` — confirm no
  physics header or source references rendering types. Record any approved
  exceptions here before proceeding. No code change.

### Phase 2 - Create The Static Library Project

- [ ] **2.1** Add `SKULLBONEZ_PHYSICS.vcxproj` and
  `SKULLBONEZ_PHYSICS.vcxproj.filters`, cloned from `SKULLBONEZ_MATHS.vcxproj`
  for Debug/Profile/Release static-library output and x64-only platform.
  **Clone the per-config `ClCompile` blocks and define lists exactly** —
  Profile must carry
  `SKULLBONEZ_PROFILE_ENABLED;SKULLBONEZ_PLATFORM_PROFILER_PIX;USE_PIX`, all
  configs must carry `_HAS_STD_BYTE=0`, and the project must not add
  `<Optimization>` overrides, `/arch` flags, or any `/fp` change. This step is
  the entire performance guarantee of the split.
- [ ] **2.2** Add `SKULLBONEZ_PHYSICS` to `SKULLBONEZ_CORE.sln` with stable GUIDs
  and solution configuration entries for Debug/Profile/Release x64.
- [ ] **2.3** Add all eligible physics headers to the physics project filters.
  Keep headers visible even when implementation files are phased in later.

### Phase 3 - Move Pure Physics Compilation

- [ ] **3.1** Move pure physics `.cpp` compile entries from `SKULLBONEZ_CORE` to
  `SKULLBONEZ_PHYSICS`; add a project reference from `SKULLBONEZ_CORE` to
  `SKULLBONEZ_PHYSICS`. Gate: `tools\validate_build.bat Profile`.
- [ ] **3.2** Flag-parity proof: read
  `Profile\SKULLBONEZ_PHYSICS\SKULLBONEZ_PHYSICS.tlog\CL.command.1.tlog` and
  confirm physics TUs compile with the same flag set they had inside CORE
  (`/O2 /fp:precise /MT /GR- /EHsc /std:c++17` and the per-config defines).
  Paste the flag line into this plan. No code change.
- [ ] **3.3** Remove duplicate physics `.cpp` compile entries from
  `SKULLBONEZ_TESTS`; add a project reference from `SKULLBONEZ_TESTS` to
  `SKULLBONEZ_PHYSICS`. Gate: `tools\validate_tests.bat`.
- [ ] **3.4** Neutrality proof: with zero source changes in this phase, run
  `tools\validate_physics.bat`. The CSV must be byte-exact. If it is not, the
  new project's flags differ from CORE's — fix the project file; never touch
  baselines from this plan. Gate: `tools\validate_physics.bat`.
- [ ] **3.5** Confirm the same physics object code is linked by runtime and
  tests. No test-only duplicate physics implementation files should remain.

### Phase 4 - Tighten The Boundary

- [ ] **4.1** Verify `SKULLBONEZ_PHYSICS.vcxproj` does not reference
  `SKULLBONEZ_CORE.vcxproj`, rendering backend projects/files, runtime render
  owners, UI, editor, or app launch code.
- [ ] **4.2** Verify `SKULLBONEZ_CORE.vcxproj` and `SKULLBONEZ_TESTS.vcxproj` no
  longer compile physics `.cpp` files directly.
- [ ] **4.3** Add a lightweight project-file test or script check that fails if
  physics `.cpp` files are re-added directly to core/tests instead of the physics
  library. Prefer a small structural check over a broad regex policy.

### Phase 5 - Broad Sign-Off For The Split

- [ ] **5.1** Run the full gate set for the completed split before starting any
  flag work:

  ```bat
  tools\validate_tests.bat
  tools\validate_physics.bat
  tools\validate_full.bat
  ```

### Phase 6 - Release WPO + Profile-WPO (final; flags only)

Whole-program optimization is enabled here and only here, after the split is
proven neutral, so any behavior or warning change is attributable to the flags
alone. MSVC LTCG works across static libraries: `/GL` objects in
`SKULLBONEZ_MATHS.lib` and `SKULLBONEZ_PHYSICS.lib` carry compiler IL, and the
`/LTCG` exe link performs cross-module code generation across the whole
program, cross-library inlining included. Setting
`<WholeProgramOptimization>true</WholeProgramOptimization>` in a configuration
PropertyGroup makes MSBuild pass `/GL` to the compiler and LTCG to the
librarian/linker automatically.

- [ ] **6.1** Release WPO: add
  `<WholeProgramOptimization>true</WholeProgramOptimization>` to the
  `Release|x64` configuration PropertyGroup of **all four** projects
  (`SKULLBONEZ_MATHS`, `SKULLBONEZ_PHYSICS`, `SKULLBONEZ_CORE`,
  `SKULLBONEZ_TESTS`), and
  `<LinkTimeCodeGeneration>UseLinkTimeCodeGeneration</LinkTimeCodeGeneration>`
  to the `Release|x64` `<Link>` blocks of the two executables. All four must
  move together: once the libs contain `/GL` IL objects, every Release exe link
  must be `/LTCG`. Gate: `tools\validate_build.bat Release` — note the script
  builds with `/warnaserror`, and LTCG performs code generation at link time,
  so link-stage warnings count against the zero-warnings contract like any
  other.
- [ ] **6.2** Release smoke: launch the Release exe once manually and exercise a
  physics scene briefly. This is a sanity check only — no baselines are ever
  generated from Release (determinism baselines come from Debug; visual/perf
  gates run Profile), so Release WPO cannot invalidate any committed artifact.
- [ ] **6.3** Create the `Profile-WPO|x64` configuration: add it to
  `SKULLBONEZ_CORE.sln` (solution config + per-project mappings for all four
  projects) and to each project as an exact clone of that project's `Profile`
  blocks — same defines (`SKULLBONEZ_PROFILE_ENABLED`,
  `SKULLBONEZ_PLATFORM_PROFILER_PIX`, `USE_PIX`, `_HAS_STD_BYTE=0`), same
  runtime library, PIX wiring, and warning level — with exactly two deltas:
  `WholeProgramOptimization` true (plus exe-link LTCG as in 6.1) and
  `OutDir`/`IntDir` of `Profile-WPO\` so it never clobbers `Profile\`
  artifacts that validation baselines depend on. Gate:
  `tools\validate_build.bat Profile-WPO` (the script forwards arbitrary
  configuration names).
- [ ] **6.4** Update the usage comment in `tools\validate_build.bat` from
  `Debug | Release | Profile` to include `Profile-WPO`. Comment-only tools
  change; no validation required beyond 6.3 having already exercised the
  script.
- [ ] **6.5** Confirm by `git diff` that no `Debug|x64` or `Profile|x64` block
  in any project changed in this phase. Debug and Profile must remain
  byte-identical in flags so validation gates, perf baselines, and determinism
  baselines are untouched.
- [ ] **6.6** Manual numbers workflow (document, do not automate): build with
  `tools\validate_build.bat Profile-WPO`, run
  `Profile-WPO\SKULLBONEZ_CORE.exe` by hand, and read frame-time numbers from
  the profiler HUD/markers. **Profile-WPO is never wired into validation
  scripts, never compared against committed perf baselines, and no baseline of
  any kind is ever regenerated from it.** It exists solely to answer "what
  would WPO buy us" with the profiler still compiled in. Comparisons are
  Profile-WPO vs Profile, same scene, same machine, reported as manual numbers.
- [ ] **6.7** Final gate for the plan: `tools\validate_full.bat` (proves the
  normal Debug/Profile pipeline is still green after the project-file edits).

## Validation

Use the smallest gate after each slice:

- Tornado/debug draw eviction: `tools\validate_dx12_renderer.bat` +
  `tools\validate_physics.bat`
- Presentation record ownership moves: `tools\validate_full.bat`
- Project-file creation only: `tools\validate_build.bat Profile`
- Tests project linkage: `tools\validate_tests.bat`
- Physics compile movement (no source change): `tools\validate_physics.bat`
  byte-exact
- Release WPO: `tools\validate_build.bat Release` + manual smoke launch
- Profile-WPO creation: `tools\validate_build.bat Profile-WPO`
- Broad final sign-off: `tools\validate_full.bat`

## Acceptance

Split (structural):

- [ ] `SKULLBONEZ_PHYSICS.vcxproj` and `.filters` exist and build a static
  library for Debug/Profile/Release x64 (and Profile-WPO after Phase 6).
- [ ] `SKULLBONEZ_CORE.sln` includes `SKULLBONEZ_PHYSICS`.
- [ ] `SKULLBONEZ_CORE.vcxproj` references `SKULLBONEZ_PHYSICS` instead of
  compiling physics `.cpp` files directly.
- [ ] `SKULLBONEZ_TESTS.vcxproj` references `SKULLBONEZ_PHYSICS` instead of
  compiling physics `.cpp` files directly.
- [ ] `SKULLBONEZ_PHYSICS` depends on `SKULLBONEZ_MATHS` and approved neutral
  utility code only; no physics header or source references rendering types,
  runtime render owners, UI, editor, or executable-only app wiring.
- [ ] The tornado pass-through tower is gone: no `RenderVectors` /
  `RenderTornadoFieldVectors` members remain on `TornadoField`,
  `TornadoSystem`, `PhysicsWorld`, `PhysicsScene`, `PhysicsEngine`, or
  `GameModelCollection`; the runtime overlay owns tornado visualization.
- [ ] Runtime and tests link the same physics implementation.
- [ ] Physics determinism remains byte-exact through every phase, with no
  baseline refreshed by this plan.
- [ ] DX12 validation remains clean after moving visual/debug ownership.

Flags (Phase 6):

- [ ] `Release|x64` compiles with `/GL` and links with `/LTCG` in all four
  projects, with zero warnings under `/warnaserror`.
- [ ] `Profile-WPO|x64` exists in the solution and all four projects, builds
  via `tools\validate_build.bat Profile-WPO`, outputs to `Profile-WPO\`, and
  carries the Profile profiler/PIX defines.
- [ ] No `Debug|x64` or `Profile|x64` compiler or linker setting changed
  (verified by diff and by tlog flag comparison).
- [ ] Profile-WPO is referenced by no validation script, gate, or baseline.

## Notes For Implementer

This split is valuable even if it starts as a build boundary, but do not stop
there if the project graph still hides render/runtime back edges inside physics.
The final win is architectural: physics can be reasoned about, built, and tested
as an engine subsystem rather than as a pile of files compiled into whoever
needs them.

On performance: the split itself is provably neutral (see Verified Build Ground
Truth) — the same objects reach the same linker. The performance *decision* in
this plan is Phase 6 only, it is additive (Release and a new config gain WPO;
nothing loses anything), and its cost is confined to Release/Profile-WPO link
times. Routine agent/validation builds stay on Profile and pay nothing.

On determinism: baselines are generated from Debug and gated on Debug/Profile
runs; neither config's flags change in this plan. Release/Profile-WPO codegen
changes therefore cannot invalidate committed baselines. Never generate or
refresh any baseline from a WPO build.
