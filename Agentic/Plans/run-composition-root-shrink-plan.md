# Run Composition Root Shrink Plan

Date: 2026-06-26
Status: Active architecture cleanup plan; launcher helper and fire slices validated
Impact area: runtime architecture, editor tools, replay tools, scene runtime, render host boundaries
Validation for this document-only change: none required

## Goal

Make `Run` shrink in source, not just in intent.

A refactor only counts when it deletes `Run::` declarations from
`SkullbonezSource/Runtime/Run.h`. Moving code between `Run*.cpp` files, adding
subsystem state, or adding callbacks from a subsystem back into `Run` is not
enough.

## Current In-Flight Branch Note

The first launcher helper slice is implemented and validated in:

- `SkullbonezSource/Runtime/Editor/LauncherTools.cpp`
- `SkullbonezSource/Runtime/Run.h`
- `SkullbonezSource/Runtime/RunFrame.cpp`
- `SkullbonezSource/Runtime/Scene/RunScene.cpp`
- `SkullbonezSource/Runtime/Tools/RuntimeTools.cpp`
- `SkullbonezSource/Runtime/Tools/RuntimeTools.h`
- `tools/check_runtime_boundaries.py`

That slice moved ray-test line clear/add/tick behavior and launcher model/terrain
hit tests into `RuntimeTools`, routes scene/reset/replay restore call sites
through `m_runtimeTools`, and adds a `Run.h` private-method-count ratchet in
`tools/check_runtime_boundaries.py`.

Deleted `Run.h` declarations:

- `ClearRayCastTestLines`
- `AddRayCastTestLine`
- `TickRayCastTestLines`
- `TryRayCastTestHit`
- `TryLauncherTerrainHit`

New owner methods:

- `RuntimeTools::ClearRayCastTestLines()`
- `RuntimeTools::AddRayCastTestLine(...)`
- `RuntimeTools::TickRayCastTestLines(float)`
- `RuntimeTools::TryRayCastTestHit(...) const`
- `RuntimeTools::TryLauncherTerrainHit(...) const`

Validation:

- Targeted build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\run_composition_launcher_tools_profile_build_rerun.log`;
  passed with 0 warnings and 0 errors.
- Rubber duck: reviewer Bohr found no blocking defect; noted that the slice
  should not be treated as the whole launcher extraction because fire dispatch,
  laser/projectile behavior, and launcher repro snapshot helpers still live on
  `Run`.
- Pre-commit gate: `tools\validate_fast.bat`, logged at
  `TestOutput\validation\run_composition_launcher_tools_validate_fast_final.log`;
  passed formatting, project filters, runtime boundaries, and Profile/Debug
  builds.
- Broad gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\run_composition_launcher_tools_validate_full.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv`.

Remaining launcher shrink work is to move or delete the still-live `Run::`
launcher methods for fire dispatch, laser/projectile behavior, and launcher
repro snapshot helpers. This helper slice also keeps a compatibility bridge that
accepts caller-owned `std::vector<GameModel>` and raw terrain input; shrink that
bridge as the runtime tools boundary gains more ownership.

## Launcher Fire Slice

The second launcher slice moved laser/projectile fire behavior into
`RuntimeTools` while keeping `Run::FireRayCastTest()` as the temporary
composition-root dispatcher that computes camera rays, records replay fire
events, and updates `SceneState().modelCount` after projectile insertion.
Replay restore now applies launcher fire events through the same `RuntimeTools`
methods.

Deleted `Run.h` declarations:

- `FireLauncherLaser`
- `FireLauncherProjectile`

Deleted `Run::` definitions:

- `Run::FireLauncherLaser`
- `Run::FireLauncherProjectile`

New owner methods:

- `RuntimeTools::FireLauncherLaser(...)`
- `RuntimeTools::FireLauncherProjectile(...)`

Validation:

- Targeted build: `tools\validate_build.bat Profile`, logged at
  `TestOutput\validation\run_composition_launcher_fire_profile_build.log`;
  passed with 0 warnings and 0 errors.
- Rubber duck: reviewer Hooke found no blocking defect; noted that the new
  methods still accept `GameModelCollection&`, `WorldEnvironment&`, and raw
  `Terrain*`, which is acceptable compatibility debt for this slice.
- Formatting check: `tools\validate_format.bat`, logged at
  `TestOutput\validation\run_composition_launcher_fire_validate_format_rerun.log`;
  passed after targeted formatting of the touched launcher/runtime files.
- Pre-commit gate: `tools\validate_full.bat`, logged at
  `TestOutput\validation\run_composition_launcher_fire_validate_full_rerun.log`;
  passed project filters, runtime boundaries, Profile/Debug builds, DX12
  validation with 0 errors and matching screenshots, and byte-exact
  `physics_regression_solver.csv`.

Remaining launcher shrink work is now limited to the still-live
`Run::FireRayCastTest()` dispatcher and the debug-only launcher repro target and
snapshot helpers. Broader editor, replay, scene runtime, and render-host slices
remain active plan work.

## Rules

- Each implementation slice must remove a coherent cluster of `Run::` methods.
- New subsystem state without moved behavior does not count as shrinkage.
- New callbacks from subsystem code back into `Run` are migration debt.
- Behavior-preserving slices should stay small enough to validate and revert.
- Each PR should report deleted `Run.h` declarations and deleted `Run::`
  definitions.

## First Slices

1. Move launcher behavior into `RuntimeTools`.
   - Move ray-test lines, hit tests, laser fire, projectile fire, and launcher
     visual sample helpers out of `Run`.
   - Target files: `Runtime/Editor/LauncherTools.cpp`,
     `Runtime/Tools/RuntimeTools.*`.

2. Move editor behavior into an editor tool owner.
   - Move placement preview, gizmo drag, object placement, editor UI commands,
     save hotkeys, and editor overlay generation out of `Run`.
   - Target files: `Runtime/Editor/RunEditorTools.cpp`,
     `Runtime/Editor/EditorTools.*`, `Runtime/Tools/RuntimeTools.*`.

3. Move replay UI/tool behavior into `ReplayRuntime`.
   - Move scrubber input, cause tree rows, velocity edit, prediction jobs,
     focus mask building, and replay overlay construction out of `Run`.
   - Target files: `Runtime/Replay/RunReplayTools.cpp`,
     `Runtime/Replay/ReplayRuntime.*`.

4. Make scene loading owned by scene runtime code.
   - Stop using `SceneRuntimeCoordinator` as a callback shell around
     `Run::LoadScene`.
   - Move reset snapshot, UI override clearing, perf-log close, generated setup,
     authored setup, world/terrain setup, and scene advancement side effects.
   - Target files: `Runtime/Scene/RunScene.cpp`,
     `Runtime/Scene/SceneRuntimeCoordinator.*`.

5. Split `RuntimeRenderHost`.
   - Replace the wide render host with narrow render-facing views:
     world/models, replay overlay, tool overlay, UI, diagnostics.
   - This unblocks deleting render callbacks such as editor overlay and replay
     prediction ghost rendering from `Run`.

## Adjacent Architecture Plan

This plan covers architecture work from the broader engine assessment that is
not directly solved by shrinking `Run`. Keep these as separate implementation
slices. Do not mix them into the in-flight launcher extraction.

### 1. Make Physics Stores Authoritative

Problem: `PhysicsBodyStore`, `ColliderStore`, and `RenderInstanceStore` exist,
but physics stepping still takes `GameModelCollection&`.

Actions:

- Move body transform, velocity, mass, sleep, force, and impulse authority into
  `PhysicsBodyStore`.
- Move shape, restitution, drag, broadphase radius, and release metadata into
  `ColliderStore`.
- Change `PhysicsEngine::Step()` and `PhysicsWorld::RunPhysics()` to operate on
  stores and command buffers instead of `GameModelCollection&`.
- Keep compatibility writeback to `GameModel` only while render, replay, editor,
  and scene snapshot code still need it.
- Add or tighten the boundary check that blocks new physics-layer
  `GameModelCollection` dependencies.

Validation:

- `tools\validate_physics.bat`
- Add `tools\validate_perf.bat` for storage layout, broadphase, or hot-loop work.

### 2. Make Render Instances A Projection

Problem: production rendering still treats `GameModelCollection` as the render
scene view.

Actions:

- Make `RenderInstanceStore` the render-facing source for transforms, material
  intent, fixed-body feedback, visibility, and shadow participation.
- Move object, shadow, and DXR instance paths away from direct `GameModel`
  iteration.
- Keep temporary old/new projection comparison if it catches material,
  transform, or visibility drift.

Validation:

- `tools\validate_dx12_renderer.bat`
- Add `tools\validate_perf.bat` for object batching or instance upload changes.

### 3. Move One Real Pass Under `RenderGraph`

Problem: `RenderGraph` records pass/resource intent, but command recording still
lives outside the graph.

Actions:

- Add pass callback support to `RenderGraph`.
- Pick one low-risk first pass: a fullscreen, post, or diagnostic pass with no
  DXR and no swapchain ownership.
- Have the graph own barriers for that pass.
- Compare graph-owned barriers against existing live barrier diagnostics before
  expanding to scene, water, shadow, or present paths.

Validation:

- `tools\validate_dx12_renderer.bat`
- Verify `dx12_validation.txt` remains zero-error.

### 4. Split Renderer Capability Interfaces Under Pressure

Problem: `IRenderBackend` still exposes lifecycle, resources, capture, DXR, GPU
timers, debug lines, dynamic geometry, and instancing in one interface.

Actions:

- Keep `IRenderBackend` as the compatibility facade.
- Introduce narrow views only when a caller benefits immediately:
  capture/readback, GPU timers, debug draw, dynamic geometry, DXR reflection.
- Remove no-op optional methods only after callers use explicit capability
  queries or narrow views.

Validation:

- `tools\validate_dx12_renderer.bat`
- Use `tools\validate_full.bat` if runtime lifecycle, resize, or device reset
  behavior changes.

### 5. Mature Assets, Materials, And Water Ownership

Problem: `AssetSystem` owns source records, but material, mesh, GPU cache,
terrain, water, sky, and post ownership are still transitional.

Actions:

- Add material and mesh source records.
- Add cache invalidation and hot reload policy.
- Separate source asset lifetime from GPU resource lifetime.
- Move water render resources and material/style binding toward the water
  pass/material layer.
- Keep `WorldEnvironment` focused on world simulation data.

Validation:

- `tools\validate_dx12_renderer.bat`
- Use `tools\validate_full.bat` if scene load or runtime resource lifecycle
  changes.

### 6. Tighten Scene And Config Schemas

Problem: scene JSON is deterministic and useful, but many fields still use
handwritten parser bodies.

Actions:

- Add typed schema helpers for high-churn areas: objects, physics, cinematic,
  capture/logging, UI, and asset instances.
- Improve diagnostics so errors name the field, expected type/range, and source
  path.
- Keep scene/style/asset formats deterministic and snapshot-friendly.

Validation:

- `tools\validate_fast.bat` for parser-only cleanup.
- `tools\validate_full.bat` if scene load behavior can change.

### 7. Preserve Observability As Architecture

Problem: refactors are only safe because this repo has strong validation and
diagnostic contracts. Those contracts should grow with the boundaries.

Actions:

- Add a private-method-count ratchet for `Run.h`.
- Keep the physics `GameModelCollection` dependency allowlist shrinking.
- Block `RuntimeRenderHost` growth without an explicit allowlist update.
- Improve profiler reporting for unbucketed time and parent/child accounting.
- Keep SkullScope query output bounded and report data-size cost when used.

Validation:

- `tools\validate_fast.bat` for boundary/tooling checks.
- `tools\validate_perf.bat` for profiler accounting changes.

### Not Now

- Do not add worker parallelism until physics stores are authoritative.
- Do not introduce a broad ECS before body, collider, render, and scene identity
  are explicit.
- Do not restore GL or DX11 runtime paths.
- Do not combine baseline refreshes with cleanup refactors.

## Ratchet

Extend `tools/check_runtime_boundaries.py` so `Run.h` cannot grow in private
method count without an explicit allowlist update.

For each shrink slice, record:

- deleted `Run.h` declarations,
- deleted `Run::` definitions,
- new owner class/methods,
- validation command selected for the PR gate.

## Validation

Documentation-only updates need no validation. Implementation slices should use:

- launcher/editor/input/tool behavior: `tools\validate_full.bat`;
- physics impulses or body mutation: `tools\validate_physics.bat`;
- render host or overlay rendering: `tools\validate_dx12_renderer.bat`;
- broad uncertain slices: `tools\agent_validate.bat`.
