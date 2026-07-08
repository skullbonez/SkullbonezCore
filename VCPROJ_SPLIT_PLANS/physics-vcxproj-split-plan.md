# Physics VCXPROJ Split Plan

Date: 2026-07-08
Status: Proposed
Owner: Build / Physics architecture

## Goal

Split the physics engine into its own Visual Studio static-library project,
`SKULLBONEZ_PHYSICS.vcxproj`, in the same spirit as `SKULLBONEZ_MATHS.vcxproj`.

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

## Current Coupling To Resolve

The first split may need temporary exceptions, but the final boundary should not
leave physics depending on rendering or runtime presentation.

Known coupling to inspect during implementation:

- `PhysicsEngine` / `PhysicsScene` expose render instance presentation records
  and mutable render instance storage.
- `TornadoField` has render-command-facing draw helpers.
- Runtime render passes consume physics debug visualizers, collider/body stores,
  tornado configs, and physics pipeline/debug records.
- Rendering code reads physics shape/store types such as `ColliderStore`,
  `PhysicsBodyStore`, and `ConvexHullShape`.
- Physics includes core utilities such as `Common.h`, `Config.h`, `FatalError.h`,
  `Profiler.h`, `WorkerPool.h`, and diagnostics sinks.

Core utilities and maths dependencies are acceptable for the initial library if
they remain dependency-direction neutral. Rendering/runtime dependencies should
move out of physics or be isolated behind narrow data records.

## Non-Goals

- Do not rewrite solver behavior as part of the project split.
- Do not change physics determinism or refresh baselines.
- Do not remove Butterfly Effect / replay prediction functionality.
- Do not split physics into a DLL. Use a static library like maths unless a
  separate owner decision says otherwise.
- Do not use this as a broad formatting or include-order pass.

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
`SKULLBONEZ_CORE`, create a deliberately small `SKULLBONEZ_CORELIB` or
`SKULLBONEZ_PLATFORM` follow-up plan rather than reaching backward from physics
into the executable.

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
out before moving the simulation file into the library.

## Step-By-Step Implementation

### Phase 0 - Inventory

- [ ] **0.1** Inventory physics `.cpp` and `.h` files currently listed in
  `SKULLBONEZ_CORE.vcxproj`, `SKULLBONEZ_TESTS.vcxproj`, and their `.filters`.
  Record which are pure simulation, which are debug/diagnostics, and which touch
  rendering/runtime concepts. No code change.
- [ ] **0.2** Inventory link dependencies for the candidate physics files:
  maths, core utility objects, rendering, runtime allocation, profiler, worker
  pool, replay snapshot types, and diagnostics sinks. No code change.
- [ ] **0.3** Decide whether core utility code needed by physics remains in
  `SKULLBONEZ_CORE` temporarily, is moved to a small shared static lib, or is
  header-only enough for the physics lib. Record the decision before project
  edits.

### Phase 1 - Create The Static Library Project

- [ ] **1.1** Add `SKULLBONEZ_PHYSICS.vcxproj` and
  `SKULLBONEZ_PHYSICS.vcxproj.filters`, modelled on `SKULLBONEZ_MATHS.vcxproj`
  for Debug/Profile/Release static-library output and x64-only platform.
  Reference `SKULLBONEZ_MATHS.vcxproj`.
- [ ] **1.2** Add `SKULLBONEZ_PHYSICS` to `SKULLBONEZ_CORE.sln` with stable GUIDs
  and solution configuration entries for Debug/Profile/Release x64.
- [ ] **1.3** Add all eligible physics headers to the physics project filters.
  Keep headers visible even when implementation files are phased in later.

### Phase 2 - Move Pure Physics Compilation

- [ ] **2.1** Move pure physics `.cpp` compile entries from `SKULLBONEZ_CORE` to
  `SKULLBONEZ_PHYSICS`; add a project reference from `SKULLBONEZ_CORE` to
  `SKULLBONEZ_PHYSICS`. Gate: `tools\validate_build.bat Profile`.
- [ ] **2.2** Remove duplicate physics `.cpp` compile entries from
  `SKULLBONEZ_TESTS`; add a project reference from `SKULLBONEZ_TESTS` to
  `SKULLBONEZ_PHYSICS`. Gate: `tools\validate_tests.bat`.
- [ ] **2.3** Confirm the same physics object code is linked by runtime and tests.
  No test-only duplicate physics implementation files should remain.

### Phase 3 - Break Render/Runtime Back Edges

- [ ] **3.1** Move render presentation ownership out of `PhysicsEngine` /
  `PhysicsScene` into runtime/rendering owner types, or expose only plain
  physics-owned data records that rendering consumes externally. Gate:
  `tools\validate_full.bat`.
- [ ] **3.2** Move tornado visual draw helpers out of `Physics\TornadoField.*`
  into runtime/rendering, leaving physics with tornado simulation/config data.
  Gate: `tools\validate_dx12_renderer.bat` and `tools\validate_physics.bat`.
- [ ] **3.3** Keep physics debug data production in physics, but move debug draw
  submission and visualizer ownership to runtime/rendering. Gate:
  `tools\validate_dx12_renderer.bat`.

### Phase 4 - Tighten The Boundary

- [ ] **4.1** Verify `SKULLBONEZ_PHYSICS.vcxproj` does not reference
  `SKULLBONEZ_CORE.vcxproj`, rendering backend projects/files, runtime render
  owners, UI, editor, or app launch code.
- [ ] **4.2** Verify `SKULLBONEZ_CORE.vcxproj` and `SKULLBONEZ_TESTS.vcxproj` no
  longer compile physics `.cpp` files directly.
- [ ] **4.3** Add a lightweight project-file test or script check that fails if
  physics `.cpp` files are re-added directly to core/tests instead of the physics
  library. Prefer a small structural check over a broad regex policy.

## Validation

Use the smallest gate after each slice:

- Project-file creation only: `tools\validate_build.bat Profile`
- Tests project linkage: `tools\validate_tests.bat`
- Physics implementation movement: `tools\validate_physics.bat`
- Render/debug visual ownership movement: `tools\validate_dx12_renderer.bat`
- Broad final sign-off: `tools\validate_full.bat`

Before final completion, run:

```bat
tools\validate_tests.bat
tools\validate_physics.bat
tools\validate_full.bat
```

## Acceptance

- [ ] `SKULLBONEZ_PHYSICS.vcxproj` and `.filters` exist and build a static
  library for Debug/Profile/Release x64.
- [ ] `SKULLBONEZ_CORE.sln` includes `SKULLBONEZ_PHYSICS`.
- [ ] `SKULLBONEZ_CORE.vcxproj` references `SKULLBONEZ_PHYSICS` instead of
  compiling physics `.cpp` files directly.
- [ ] `SKULLBONEZ_TESTS.vcxproj` references `SKULLBONEZ_PHYSICS` instead of
  compiling physics `.cpp` files directly.
- [ ] `SKULLBONEZ_PHYSICS` depends on `SKULLBONEZ_MATHS` and approved neutral
  utility code only; it does not depend on runtime render owners, UI, editor, or
  executable-only app wiring.
- [ ] Runtime and tests link the same physics implementation.
- [ ] Physics determinism remains byte-exact.
- [ ] DX12 validation remains clean after moving visual/debug ownership.

## Notes For Implementer

This split is valuable even if it starts as a build boundary, but do not stop
there if the project graph still hides render/runtime back edges inside physics.
The final win is architectural: physics can be reasoned about, built, and tested
as an engine subsystem rather than as a pile of files compiled into whoever
needs them.
