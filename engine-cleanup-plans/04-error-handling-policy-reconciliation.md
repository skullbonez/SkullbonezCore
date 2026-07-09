# 04 — Error-Handling Policy Reconciliation

Date: 2026-07-08
Status: In Progress
Priority: P1
Owner: Runtime / Physics / Rendering
Source issue: audit iss-03 (severity 4)

## Problem

The documented error policy and the code disagree by two orders of magnitude.

Verified evidence:

- [`AGENTS.md`](../AGENTS.md) (Error Handling Policy) states *"Exceptions are
  banned for new engine code… Any new `throw` is a review failure,"* mandating
  three lanes: `SB_FATAL` (fatal invariant), `SbResult` (recoverable),
  `FailAutomation` (probe).
- Reality (verified greps): **2** `SB_FATAL(` call sites vs **283** `throw`
  statements across source. The now-deleted runtime-boundary checker had frozen
  the ratchet `MAX_SOURCE_THROW_TOKENS = 294`
  at ~today's count — blessing every existing exception as permanent.
- Throws are the failure path in the very subsystems the policy names first:
  per-frame loops in `RunFrame`, physics capacity guards in `PhysicsWorld`,
  `ThrowIfFailed` in the DX12 backend, and even `AllocateOrThrow` inside the
  allocation-gate file. Unwinding through RAII profiling scopes and the Win32
  message loop is exactly the robustness/determinism hazard `SB_FATAL` exists to
  avoid.

A rule contradicted 140:1 by its own code is worse than no rule.

## Goal

Make policy and code agree. Convert throws to the lane that actually fits. **Do
not track a throw count**: plan 03 deleted the `MAX_SOURCE_THROW_TOKENS`
regex ratchet; progress is measured by throws actually converted, not by a
frozen budget. Where exceptions are genuinely appropriate (external IO at a
boundary), say so.

## Approach

- [x] **Phase 0 — Categorize all 283 throws** by lane: F (should-never-happen
  engine invariant), R (external input/environment: scene/asset/file IO), P
  (probe/automation assertion).
- [x] **Phase 1 — F → `SB_FATAL` / allocator-safe fatal.** Convert physics
  capacity guards and frame-loop invariants. This removes unwinding through the
  message loop and profiling RAII — the core robustness win.
- [x] **Phase 2 — P → `FailAutomation`.** Route replay/interaction probe throws
  to the machine-readable automation channel with `ok=false` + message.
- [ ] **Phase 3 — R → `SbResult`.** Convert scene/asset/file IO failures to
  value-carrying results reported at the boundary.
- [ ] **Phase 4 — No throw count.** The `MAX_SOURCE_THROW_TOKENS` ratchet is
  deleted by plan 03. Do not reinstate any budget; verify conversions by
  `rg "throw "` + review.

## Owner Decision - 2026-07-09

Continue Plan 04 in lane order: convert Lane F throws to `SB_FATAL` where safe,
then Lane P to probe/report failures, then Lane R to recoverable results.
Allocator-hook fatal handling needs an allocator-safe strategy; do not blindly
call `SB_FATAL` from allocation hooks if it can allocate or use unsafe engine
logging. Math throw conversions may require coordinated test-contract changes.

## Risks / determinism

Physics throw conversions touch a determinism-critical path — the conversions
must be behavior-preserving on the success path. Gate with byte-exact physics
after Phase 1.

## Step-by-step implementation

Do steps in order; validate and commit per step. Physics conversions are
byte-exact gated.

- [x] **0.1** `rg -n "throw " SkullbonezSource` and tag each of the ~283 sites in
  a table as **F** (engine invariant), **R** (external input/IO), or **P**
  (probe/automation). No code change. Commit the table.

  Completed 2026-07-09:
  - Added `04-throw-site-lane-inventory.md` with one row per current throw
    statement.
  - Used strict inventory command `rg -n "^\s*throw\b" SkullbonezSource` so
    comments mentioning `throw` do not inflate the count, while bare `throw;`
    rethrows are included.
  - Current source has 257 throw statements, down from the stale 283 count in
    the original audit. Classification summary: F = 137, R = 116, P = 4.
  - Documentation-only step; no repository validation required.
- [x] **1.1** Convert **F** sites (physics capacity guards, frame-loop
  invariants) to `SB_FATAL(owner, ...)` or an allocator-safe fatal for the
  allocation hook, **one subsystem at a time**. Gate: `validate_physics` for
  physics, `validate_full` otherwise. Commit per subsystem.

  Progress 2026-07-09, physics/terrain fatal-invariant sub-slice:
  - Converted five F sites from `throw std::runtime_error` to `SB_FATAL`:
    `Terrain::GetQuadCacheIndex`, `Terrain::QueryCollisionData`,
    `Terrain::LocatePolygon`, `SweepTerrainContact`, and
    `PhysicsBodyStore::BuildReplayBodyIdsForReload`.
  - Strict source throw statement inventory now reports 252 sites, down from the
    Step 0.1 baseline of 257. `SB_FATAL` call sites now report 35.
  - Comment-style audit scope:
    `SkullbonezSource/Physics/PhysicsBodyStore.cpp`,
    `SkullbonezSource/Physics/TerrainContactManifold.cpp`, and
    `SkullbonezSource/World/Terrain.cpp`; checked 3, deferred 0.
  - Required gate passed: `tools\validate_physics.bat` exited 0 in
    25.9732165 seconds. Log:
    `Agentic/Reports/validate_physics_plan04_fatal_invariants_20260709.log`.

  Progress 2026-07-09, WorkerPool fatal-invariant sub-slice:
  - Converted four F sites from `throw std::runtime_error` to `SB_FATAL`:
    `WorkerPool::Submit`, `WorkerPool::BuildChunks`, and both
    `WorkerPool::SubmitParallelChunk` lifetime/capacity guards.
  - Updated the fixed parallel task queue comment to describe fatal capacity
    failure instead of exception unwinding.
  - Strict source throw statement inventory now reports 248 sites, down from the
    previous sub-slice count of 252. `SB_FATAL` macro invocations now report 36
    via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope: `SkullbonezSource/Core/WorkerPool.cpp` and
    `SkullbonezSource/Core/WorkerPool.h`; checked 2, deferred 0.
  - Required gate passed: `tools\validate_full.bat` exited 0 in
    68.1528472 seconds. Log:
    `Agentic/Reports/validate_full_plan04_workerpool_fatals_20260709.log`.

  Progress 2026-07-09, RunFrame fatal-invariant sub-slice:
  - Converted the frame-loop render-backend lifetime guard in
    `Run::Execute` from `throw std::runtime_error` to `SB_FATAL`.
  - Strict source throw statement inventory now reports 247 sites, down from the
    previous sub-slice count of 248. `SB_FATAL` macro invocations now report 37
    via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope: `SkullbonezSource/Runtime/RunFrame.cpp`;
    checked 1, deferred 0.
  - Required gate passed: `tools\validate_full.bat` exited 0 in
    58.0382996 seconds. Log:
    `Agentic/Reports/validate_full_plan04_runframe_fatal_20260709.log`.

  Progress 2026-07-09, RunRender graph-callback fatal-invariant sub-slice:
  - Converted fourteen F sites from `throw std::runtime_error` to `SB_FATAL`:
    all graph callback missing-execution-data guards in `RunRender.cpp` plus the
    VolumetricLight graph transient materialization guard.
  - Strict source throw statement inventory now reports 233 sites, down from the
    previous sub-slice count of 247. `SB_FATAL` macro invocations now report 51
    via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope: `SkullbonezSource/Runtime/RunRender.cpp`;
    checked 1, deferred 0.
  - Required gate passed: `tools\validate_full.bat` exited 0 in
    57.9009828 seconds. Log:
    `Agentic/Reports/validate_full_plan04_runrender_fatals_20260709.log`.

  Progress 2026-07-09, RenderGraph fatal-invariant sub-slice:
  - Converted twenty-one F sites from `throw std::runtime_error` to `SB_FATAL`:
    all fixed-capacity, resource-handle, pass-index, callback-contract,
    subresource-state, transition, and transient-allocation guards in
    `RenderGraph.cpp`/`RenderGraph.h`.
  - Strict source throw statement inventory now reports 212 sites, down from the
    previous sub-slice count of 233. `SB_FATAL` macro invocations now report 72
    via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope: `SkullbonezSource/Rendering/RenderGraph.cpp` and
    `SkullbonezSource/Rendering/RenderGraph.h`; checked 2, deferred 0.
  - Required gate passed: `tools\validate_full.bat` exited 0 in
    00:01:15.7980914 after a touched-file clang-format fix. Log:
    `Agentic/Reports/validate_full_plan04_rendergraph_fatals_20260709.log`.

  Progress 2026-07-09, RenderDeviceDX12 fatal-invariant sub-slice:
  - Converted twenty-six F sites from `throw std::runtime_error` to `SB_FATAL`:
    `RenderDeviceDX12.cpp` rows 106, 108, 110-114, 116-117, 121, 125-139,
    and 141 from the Step 0.1 inventory. The remaining throws in this file are
    the rows classified R and are intentionally left for the recoverable-result
    phase.
  - Strict source throw statement inventory now reports 186 sites, down from the
    previous sub-slice count of 212. `SB_FATAL` macro invocations now report 98
    via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp`; checked 1,
    deferred 0. The DX12 render-device learning header glossary was tightened
    for descriptor heaps, shader-visible heaps, PSOs, root signatures, resource
    states, and fences.
  - Required gate passed: `tools\validate_full.bat` exited 0 in
    00:00:56.3313673. Log:
    `Agentic/Reports/validate_full_plan04_renderdevice_fatals_20260709.log`.

  Progress 2026-07-09, RenderBackendDX12 graph/transient fatal-invariant
  sub-slice:
  - Converted fourteen F sites from `throw std::runtime_error` to `SB_FATAL`:
    `RenderBackendDX12.cpp` rows 53-56, 58-62, and 64-68 from the Step 0.1
    inventory. The R rows in the same file remain deferred to the
    recoverable-result phase.
  - Strict source throw statement inventory now reports 172 sites, down from the
    previous sub-slice count of 186. `SB_FATAL` macro invocations now report 112
    via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`; checked 1,
    deferred 0.
  - Required gate passed: `tools\validate_full.bat` exited 0 in
    00:00:58.5526692. Log:
    `Agentic/Reports/validate_full_plan04_renderbackend_graph_fatals_20260709.log`.

  Progress 2026-07-09, CameraCollection fatal-invariant sub-slice:
  - Converted eight F sites from `throw std::runtime_error` to `SB_FATAL`:
    `CameraCollection.cpp` rows 224 through 231 from the Step 0.1 inventory.
    The replacements cover fixed camera-slot capacity, selected-camera
    preconditions, missing tween terrain state, and missing camera-hash lookup
    invariants.
  - Strict anchored source throw statement inventory now reports 164 sites,
    down from the previous sub-slice count of 172. `SB_FATAL` macro invocations
    now report 120 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Runtime/CameraCollection.cpp`; checked 1, deferred 0.
    The existing learning header already covers the camera-slot, selected-pose,
    and render-pose invariants touched by this slice.
  - Required gate passed: `tools\validate_full.bat` exited 0 in
    00:00:55.0646955. Log:
    `Agentic/Reports/validate_full_plan04_cameracollection_fatals_20260709.log`.

  Progress 2026-07-09, TestScene collection fatal-invariant sub-slice:
  - Converted twelve F sites from `throw std::runtime_error` to a local
    `SB_FATAL("TestScene", ...)` helper: `TestScene.cpp` rows 10 through 21
    from the Step 0.1 inventory. The replacements cover parsed scene collection
    getter bounds for cameras, bodies, states, constraints, broadphase
    expectations, and material overrides after scene parsing has succeeded.
  - Strict anchored source throw statement inventory now reports 152 sites,
    down from the previous sub-slice count of 164. `SB_FATAL` macro invocations
    now report 121 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`; this slice uses
    one shared helper for twelve fatal getter paths.
  - Comment-style audit scope: `SkullbonezSource/Scene/TestScene.cpp`;
    checked 1, deferred 0. The learning header now calls out scene collection
    getter invariants and the Lane F/Lane R split.
  - Required gate passed: `tools\validate_full.bat` exited 0 in
    00:00:57.7012411. Log:
    `Agentic/Reports/validate_full_plan04_testscene_fatals_20260709.log`.

  Progress 2026-07-09, Input window-bridge fatal-invariant sub-slice:
  - Converted two F sites from `throw std::runtime_error` to a local
    `SB_FATAL("Input", ...)` helper: `Input.cpp` rows 203 and 206 from the
    Step 0.1 inventory. The neighboring Win32 cursor API failures in the same
    file remain Lane R for the recoverable-result phase.
  - Strict anchored source throw statement inventory now reports 150 sites,
    down from the previous sub-slice count of 152. `SB_FATAL` macro invocations
    now report 122 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope: `SkullbonezSource/Runtime/Input.cpp`; checked 1,
    deferred 0. The learning header now names the input window bridge and its
    runtime startup binding invariant.
  - Required gate passed: `tools\validate_full.bat` exited 0 in
    00:00:55.3535461. Log:
    `Agentic/Reports/validate_full_plan04_input_bridge_fatals_20260709.log`.

  Progress 2026-07-09, GameModelRenderer shadow-batch fatal-invariant
  sub-slice:
  - Converted one F site from `throw std::runtime_error` to
    `SB_FATAL("GameModelRenderer", ...)`: `GameModelRenderer.cpp` row 4 from
    the Step 0.1 inventory. The replacement covers fixed shadow-caster batch
    reserve exhaustion after steady render capacity should already be prepared.
  - Strict anchored source throw statement inventory now reports 149 sites,
    down from the previous sub-slice count of 150. `SB_FATAL` macro invocations
    now report 123 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Rendering/GameModelRenderer.cpp`; checked 1, deferred 0.
    The learning header now names the shadow batch fixed-capacity invariant.
  - Required gate passed: `tools\validate_full.bat` exited 0 in
    00:00:55.5201577. Log:
    `Agentic/Reports/validate_full_plan04_gamemodelrenderer_fatals_20260709.log`.

  Progress 2026-07-09, SceneRuntime scene-object-id fatal-invariant sub-slice:
  - Converted one F site from `throw std::runtime_error` to
    `SB_FATAL("SceneRuntime", ...)`: `SceneRuntime.cpp` row 254 from the Step
    0.1 inventory. The replacement covers scene object id range exhaustion;
    id 0 remains reserved as "not assigned" and live allocations must not wrap
    or cross the uint32 ceiling.
  - Strict anchored source throw statement inventory now reports 148 sites,
    down from the previous sub-slice count of 149. `SB_FATAL` macro invocations
    now report 124 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Runtime/Scene/SceneRuntime.cpp`; checked 1, deferred 0.
    The learning header and local allocation comment now name the scene object
    id cursor invariant.
  - Required gate passed: `tools\validate_full.bat` exited 0 in
    00:00:54.4774207. Log:
    `Agentic/Reports/validate_full_plan04_sceneruntime_fatals_20260709.log`.

  Progress 2026-07-09, AssetSystem registration/shader-key
  fatal-invariant sub-slice:
  - Converted three F sites from `throw std::invalid_argument` to
    `SB_FATAL("AssetSystem", ...)`: `AssetSystem.cpp` rows 251 through 253 from
    the Step 0.1 inventory. The replacements cover blank logical asset names,
    blank relative paths, and blank shader lookup keys as owner API contract
    violations; non-empty asset file/path failures remain Lane R work.
  - Strict anchored source throw statement inventory now reports 145 sites,
    down from the previous sub-slice count of 148. `SB_FATAL` macro invocations
    now report 127 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope: `SkullbonezSource/Assets/AssetSystem.cpp`;
    checked 1, deferred 0. The learning header now names logical asset names,
    shader base names, and the registry precondition invariant.
  - First required gate attempt failed at formatting because the touched file
    needed clang-format. The file was formatted directly with the Visual Studio
    LLVM `clang-format.exe`; no broad formatter was run.
  - Required gate then passed: `tools\validate_full.bat` exited 0 in
    00:00:57.1070909. Log:
    `Agentic/Reports/validate_full_plan04_assetsystem_fatals_20260709.log`.

  Progress 2026-07-09, TLASDX12 instance-count fatal-invariant sub-slice:
  - Converted one F site from `throw std::runtime_error` to
    `SB_FATAL("TLAS", ...)`: `TLASDX12.cpp` row 197 from the Step 0.1
    inventory. The remaining TLAS throws are DX12 resource-creation Lane R sites
    and are intentionally left for the recoverable-result phase.
  - Strict anchored source throw statement inventory now reports 144 sites,
    down from the previous sub-slice count of 145. `SB_FATAL` macro invocations
    now report 128 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Rendering/DX12/TLASDX12.cpp`; checked 1, deferred 0. The
    learning header and local build guard now name the TLAS max-instance buffer
    ownership invariant.
  - Required gate passed: `tools\validate_dx12_renderer.bat` exited 0 in
    00:00:25.7839246. Log:
    `Agentic/Reports/validate_dx12_renderer_plan04_tlas_fatal_20260709.log`.

  Progress 2026-07-09, FramebufferDX12 backend-lifetime fatal-invariant
  sub-slice:
  - Converted one F site from `throw std::runtime_error` to
    `SB_FATAL("FramebufferDX12", ...)`: `FramebufferDX12.cpp` row 43 from the
    Step 0.1 inventory. The color/depth texture creation throws in the same file
    remain Lane R for the recoverable-result phase.
  - Strict anchored source throw statement inventory now reports 143 sites,
    down from the previous sub-slice count of 144. `SB_FATAL` macro invocations
    now report 129 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Rendering/DX12/FramebufferDX12.cpp`; checked 1,
    deferred 0. The learning header and local create guard now name the
    initialized-backend lifetime invariant.
  - Required gate passed: `tools\validate_dx12_renderer.bat` exited 0 in
    00:00:26.4805520. Log:
    `Agentic/Reports/validate_dx12_renderer_plan04_framebuffer_fatal_20260709.log`.

  Progress 2026-07-09, MeshDX12 upload-buffer fatal-invariant sub-slice:
  - Converted one F site from `throw std::runtime_error` to
    `SB_FATAL("MeshDX12", ...)`: `MeshDX12.cpp` row 93 from the Step 0.1
    inventory. The committed-resource creation throw in the same file remains
    Lane R for the recoverable-result phase.
  - Strict anchored source throw statement inventory now reports 142 sites,
    down from the previous sub-slice count of 143. `SB_FATAL` macro invocations
    now report 130 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Rendering/DX12/MeshDX12.cpp`; checked 1, deferred 0. The
    learning header and local create guard now name the frame upload arena
    ownership invariant.
  - Required gate passed: `tools\validate_dx12_renderer.bat` exited 0 in
    00:00:26.3269250. Log:
    `Agentic/Reports/validate_dx12_renderer_plan04_mesh_fatal_20260709.log`.

  Progress 2026-07-09, RenderBackendDX12 dynamic-geometry PSO-cache
  fatal-invariant sub-slice:
  - Converted one F site from `throw std::runtime_error` to
    `SB_FATAL("RenderBackendDX12", ...)`:
    `RenderBackendDX12.DynamicGeometry.cpp` row 51 from the Step 0.1 inventory.
    The two DX12 device/shader creation throws in the same file remain Lane R
    for the recoverable-result phase.
  - Strict anchored source throw statement inventory now reports 141 sites,
    down from the previous sub-slice count of 142. `SB_FATAL` macro invocations
    now report 131 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp`;
    checked 1, deferred 0. The learning header and local PSO-cache guard now
    name the fixed render-target-format cache invariant.
  - Required gate passed: `tools\validate_dx12_renderer.bat` exited 0 in
    00:00:27.3493942. Log:
    `Agentic/Reports/validate_dx12_renderer_plan04_dynamic_geometry_fatal_20260709.log`.

  Progress 2026-07-09, RenderBackendDX12 pipeline-cache/descriptor-heap
  fatal-invariant sub-slice:
  - Converted three F sites from `throw std::runtime_error` to
    `SB_FATAL("RenderBackendDX12", ...)`:
    `RenderBackendDX12.Pipeline.cpp` rows 89 through 91 from the Step 0.1
    inventory. The two DX12 device/shader creation throws in the same file
    remain Lane R for the recoverable-result phase.
  - Strict anchored source throw statement inventory now reports 138 sites,
    down from the previous sub-slice count of 141. `SB_FATAL` macro invocations
    now report 134 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp`; checked
    1, deferred 0. The learning header and local guards now name the fixed PSO
    cache and framebuffer descriptor heap capacity invariants.
  - Required gate passed: `tools\validate_dx12_renderer.bat` exited 0 in
    00:00:27.3149044. Log:
    `Agentic/Reports/validate_dx12_renderer_plan04_pipeline_fatals_20260709.log`.

  Progress 2026-07-09, RenderBackendDX12 platform-profiler GPU stack
  fatal-invariant sub-slice:
  - Converted one F site from `throw std::runtime_error` to
    `SB_FATAL("RenderBackendDX12", ...)`:
    `RenderBackendDX12.Profiler.cpp` row 101 from the Step 0.1 inventory. The
    DX12 HRESULT helper throw in the same file remains Lane R for the
    recoverable-result phase.
  - Removed an unused local descriptor-heap fatal reporter from the profiler
    split file while touching the helper area; it had no call sites in that
    file.
  - Strict anchored source throw statement inventory now reports 137 sites,
    down from the previous sub-slice count of 138. `SB_FATAL` macro invocations
    now report 135 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Profiler.cpp`; checked
    1, deferred 0. The learning header and local guard now name the GPU timer,
    PIX, platform-profiler GPU stack, and fixed stack-depth invariants.
  - Required renderer gate passed: `tools\validate_dx12_renderer.bat` exited 0
    in 00:00:27.3000145. Log:
    `Agentic/Reports/validate_dx12_renderer_plan04_profiler_fatal_20260709.log`.
  - Required platform-profiler marker run passed:
    `Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers --frames 5 --vsync off`
    exited 0 in 00:00:09.1357466. Log:
    `Agentic/Reports/platform_profiler_markers_plan04_profiler_fatal_20260709.log`.

  Progress 2026-07-09, RunScene DXR render-facet fatal-invariant sub-slice:
  - Converted one F site from `throw std::runtime_error` to
    `SB_FATAL("RunScene", ...)`: `RunScene.cpp` row 250 from the Step 0.1
    inventory. The replacement covers missing render resource, command, or
    diagnostics facets during DXR reflection helper-mesh warm-up.
  - Strict anchored source throw statement inventory now reports 136 sites,
    down from the previous sub-slice count of 137. `SB_FATAL` macro invocations
    now report 136 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Runtime/Scene/RunScene.cpp`; checked 1, deferred 0. The
    learning header and local guard now name render backend facets and the DXR
    reflection facet-binding invariant.
  - Required gate passed: `tools\validate_full.bat` exited 0 in
    00:01:00.4684976. Log:
    `Agentic/Reports/validate_full_plan04_runscene_fatal_20260709.log`.

  Progress 2026-07-09, TextureCollection capacity/context/hash
  fatal-invariant sub-slice:
  - Converted six F sites from `throw std::runtime_error` /
    `throw std::invalid_argument` to `SB_FATAL("TextureCollection", ...)`:
    `TextureCollection.cpp` rows 212 through 214, 216, 218, and 222 from the
    Step 0.1 inventory. The replacements cover fixed texture-slot capacity,
    render resource/command context preconditions, invalid fixed-table slot
    access, and the non-zero legacy hash precondition.
  - The remaining `TextureCollection.cpp` throws are Lane R asset/file/backend
    failures and are intentionally left for the recoverable-result phase.
  - Strict anchored source throw statement inventory now reports 130 sites,
    down from the previous sub-slice count of 136. `SB_FATAL` macro invocations
    now report 142 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Assets/TextureCollection.cpp`; checked 1, deferred 0.
    The learning header and local guards now name legacy hashes, backend
    handles, render resource/command contexts, fixed texture-slot capacity, and
    legacy direct-create hash invariants.
  - Required gate passed: `tools\validate_full.bat` exited 0 in
    00:00:54.6370293. Log:
    `Agentic/Reports/validate_full_plan04_texturecollection_fatals_20260709.log`.

  Progress 2026-07-09, RenderBackendDX12 DXR TLAS-capacity
  fatal-invariant sub-slice:
  - Converted one F site from `throw std::runtime_error` to
    `SB_FATAL("RenderBackendDX12", ...)`:
    `RenderBackendDX12.DXR.cpp` row 86 from the Step 0.1 inventory. The
    replacement covers a TLAS rebuild requesting more sphere/model instances
    than the active DXR initialization reserved.
  - The remaining `RenderBackendDX12.DXR.cpp` throws are Lane R DXR
    shader/file/device/resource failures and are intentionally left for the
    recoverable-result phase.
  - Strict anchored source throw statement inventory now reports 129 sites,
    down from the previous sub-slice count of 130. `SB_FATAL` macro invocations
    now report 143 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp`; checked 1,
    deferred 0. The learning header and local guard now name the TLAS active
    model instance capacity invariant.
  - Required gate passed: `tools\validate_dx12_renderer.bat` exited 0 in
    00:00:27.3583295. Log:
    `Agentic/Reports/validate_dx12_renderer_plan04_dxr_tlas_fatal_20260709.log`.

  Progress 2026-07-09, pure math precondition fatal-invariant sub-slice:
  - Converted eight F sites from `throw std::runtime_error` to `SB_FATAL`:
    `GeometricMath.cpp` rows 40 through 42 and `Vector3.cpp` rows 74 through
    78 from the Step 0.1 inventory. The replacements cover zero plane normals,
    out-of-segment intersection points, collinear barycentric triangles, zero
    vector normalization, scalar divide by zero, and component-wise divide by
    zero.
  - Updated `Vector3` and `GeometricMath` public comments to describe fatal
    preconditions, and adjusted the unit tests to validate caller-detectable
    guard states instead of catching fatal paths in-process.
  - Strict anchored source throw statement inventory now reports 121 sites,
    down from the previous sub-slice count of 129. `SB_FATAL` macro invocations
    now report 151 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Maths/GeometricMath.cpp`,
    `SkullbonezSource/Maths/GeometricMath.h`,
    `SkullbonezSource/Maths/Vector3.cpp`,
    `SkullbonezSource/Maths/Vector3.h`,
    `SkullbonezTests/TestGeometricMath.cpp`, and
    `SkullbonezTests/TestVector3.cpp`; checked 6, deferred 0. This was a
    touched-file audit, so no subsystem checklist plan was required.
  - Required gates passed: `tools\validate_tests.bat` exited 0 in
    00:00:08.4134429 with 61 test cases and 1540 assertions passing, and
    `tools\validate_fast.bat` exited 0 in 00:01:13.5353013 after applying the
    targeted header-format alignment to `Vector3.h`. Logs:
    `Agentic/Reports/validate_tests_plan04_math_fatals_20260709.log` and
    `Agentic/Reports/validate_fast_plan04_math_fatals_20260709.log`.
- [x] **2.1** Convert **P** sites (replay/interaction probes) to the
  `FailAutomation(...)` channel with `ok=false` + message. Gate: `validate_full`
  + replay scrub. Commit.

  Completed 2026-07-09, probe/automation failure sub-slice:
  - Converted all four P rows from exception exits to bounded failure reporting:
    `Run.cpp` rows 247-248 now use `RunReplayProbeState::RecordFailure` or
    `SbResult`, and `RunInteractionAutomation.cpp` rows 255-256 now write the
    interaction report with `ok=false`, quit the message loop, and return the
    failure through `Run::InteractionAutomationResult()` at the process
    boundary.
  - `Init.cpp` now reports startup replay-probe failures before `Initialise()`,
    reports bad interaction automation setup through a non-throwing
    `SbResult`, and returns exit code 1 after `Execute()` when interaction
    automation has written a failed report.
  - Strict anchored source throw statement inventory now reports 117 sites,
    down from the previous sub-slice count of 121. `SB_FATAL` macro invocations
    remain 151 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope: `SkullbonezSource/Runtime/Init.cpp`,
    `SkullbonezSource/Runtime/Run.cpp`, `SkullbonezSource/Runtime/Run.h`, and
    `SkullbonezSource/Runtime/RunInteractionAutomation.cpp`; checked 4,
    deferred 0. This was a touched-file audit, so no subsystem checklist plan
    was required.
  - Required gates passed: `tools\validate_full.bat` exited 0 in
    00:01:05.7535614, `tools\validate_replay_scrub.bat` exited 0 in
    00:00:11.1128721, and the added focused interaction gate
    `tools\validate_interaction_clicks.bat` exited 0 in 00:00:14.1358083.
    Logs: `Agentic/Reports/validate_full_plan04_probe_failures_20260709.log`,
    `Agentic/Reports/validate_replay_scrub_plan04_probe_failures_20260709.log`,
    and
    `Agentic/Reports/validate_interaction_clicks_plan04_probe_failures_20260709.log`.
- [ ] **3.1** Convert **R** sites (scene/asset/file IO) to `SbResult` reported at
  the boundary, **one boundary at a time**. Gate: `validate_full`. Commit.

  Progress 2026-07-09, replay-load recoverable boundary sub-slice:
  - Converted the command-line replay v2 presentation artifact load failure in
    `SkullbonezSource/Runtime/Init.cpp` row 232 from an exception exit to
    `SbResult::Failure("Runtime/ReplayLoad", ...)` returned through the
    existing `reportRunResult` process-boundary reporter.
  - Strict anchored source throw statement inventory now reports 116 sites,
    down from the previous sub-slice count of 117. `SB_FATAL` macro invocations
    remain 151 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope: `SkullbonezSource/Runtime/Init.cpp`; checked 1,
    deferred 0. This was a touched-file audit, so no subsystem checklist plan
    was required.
  - Required gates passed: `tools\validate_full.bat` exited 0 with
    `VALIDATE_FULL: DEFAULT GATE PASSED` (log timestamp delta
    00:01:02.4353853), and the focused replay boundary gate
    `tools\validate_replay_v2_artifact.bat` exited 0 in 00:00:57.3068451 with
    `VALIDATE_REPLAY_V2_ARTIFACT: ALL PASSED`. Logs:
    `Agentic/Reports/validate_full_plan04_replay_load_result_20260709.log` and
    `Agentic/Reports/validate_replay_v2_artifact_plan04_replay_load_result_20260709.log`.

  Progress 2026-07-09, authored scene object-group metadata sub-slice:
  - Converted `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp` row 249
    from `throw std::runtime_error` to a Lane R
    `SbResult::Failure("Runtime/SceneAuthoredSetup", ...)` returned through
    `SceneAuthoredSetup::SetUpGameModels` and the existing `Run::LoadScene`
    scene-load reporter.
  - Strict anchored source throw statement inventory now reports 115 sites,
    down from the previous sub-slice count of 116. `SB_FATAL` macro invocations
    remain 151 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp`; checked 1,
    deferred 0. This was a touched-file audit, so no subsystem checklist plan
    was required.
  - Focused build passed: `tools\validate_build.bat Profile` exited 0 in
    00:00:05.4846485 with 0 warnings and 0 errors.
  - First `tools\validate_full.bat` attempt failed formatting after
    00:00:32.5653233. The touched file was formatted with the repo
    `tools\find_clang_format.bat` locator, and `tools\validate_format.bat`
    then exited 0 in 00:00:09.3112561.
  - Required gate then passed: `tools\validate_full.bat` exited 0 in
    00:00:56.0458616 with `VALIDATE_FULL: DEFAULT GATE PASSED`, DX12
    validation errors 0, screenshots matching baselines, and
    `physics_regression_solver.csv` byte-exact. Final log:
    `Agentic/Reports/validate_full_plan04_scene_group_result_20260709_rerun.log`.

  Progress 2026-07-09, Window creation recoverable startup boundary sub-slice:
  - Converted `SkullbonezSource/Runtime/Window.cpp` row 210 from
    `throw std::runtime_error` to `SbResult::Failure("Runtime/Window", ...)`
    returned through `Window::CreateAppWindow`.
  - `WinMain` now reports that startup failure to `stderr`, shuts down
    `WorkerPool`, calls `CoUninitialize()`, and exits 1 before `GetDC` or
    render-backend startup.
  - Strict anchored source throw statement inventory now reports 114 sites,
    down from the previous sub-slice count of 115. `SB_FATAL` macro invocations
    remain 151 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope: `SkullbonezSource/Runtime/Init.cpp`,
    `SkullbonezSource/Runtime/Window.cpp`, and
    `SkullbonezSource/Runtime/Window.h`; checked 3, deferred 0. This was a
    touched-file audit, so no subsystem checklist plan was required.
  - Focused build passed: `tools\validate_build.bat Profile` exited 0 in
    00:00:10.2404826 with 0 warnings and 0 errors.
  - First `tools\validate_full.bat` attempt failed formatting after
    00:00:40.4021576. The touched header was fixed with
    `tools\align_header_inline_comments.py --repo . --write
    .\SkullbonezSource\Runtime\Window.h`, and `tools\validate_format.bat` then
    exited 0 in 00:00:09.3233037.
  - Required gate then passed: `tools\validate_full.bat` exited 0 in
    00:01:05.6535012 with `VALIDATE_FULL: DEFAULT GATE PASSED`, DX12
    validation errors 0, screenshots matching baselines, and
    `physics_regression_solver.csv` byte-exact. Final log:
    `Agentic/Reports/validate_full_plan04_window_create_result_20260709_rerun.log`.

  Progress 2026-07-09, capture screenshot recoverable result sub-slice:
  - Converted `SkullbonezSource/Runtime/CaptureSystem.cpp` rows 198 through
    201 from screenshot readback/file-output exceptions to Lane R
    `SbResult::Failure("Runtime/CaptureSystem", ...)` results.
  - `CaptureSystem`, `CaptureController`, and `Run::SaveScreenshot` now return
    `SbResult`. Frame screenshot automation and auto-cycle captures exit with
    diagnostics on failed capture results instead of logging successful scene
    completion; interaction automation writes report `ok=false`; live-style
    capture writes `capture_error`; UI screenshot commands report to `stderr`.
  - Strict anchored source throw statement inventory now reports 110 sites,
    down from the previous sub-slice count of 114. `SB_FATAL` macro invocations
    remain 151 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope: `SkullbonezSource/Runtime/CaptureController.cpp`,
    `SkullbonezSource/Runtime/CaptureController.h`,
    `SkullbonezSource/Runtime/CaptureSystem.cpp`,
    `SkullbonezSource/Runtime/CaptureSystem.h`,
    `SkullbonezSource/Runtime/LiveStyleController.h`,
    `SkullbonezSource/Runtime/Run.h`,
    `SkullbonezSource/Runtime/RunCapture.cpp`,
    `SkullbonezSource/Runtime/RunFrame.cpp`,
    `SkullbonezSource/Runtime/RunInput.cpp`,
    `SkullbonezSource/Runtime/RunInteractionAutomation.cpp`, and
    `SkullbonezSource/Runtime/RunLiveStyle.cpp`; checked 11, deferred 0. This
    was a touched-file audit, so no subsystem checklist plan was required.
  - Focused build passed: `tools\validate_build.bat Profile` exited 0 in
    00:00:10.8437565 with 0 warnings and 0 errors.
  - Required gate passed: `tools\validate_full.bat` exited 0 in
    00:01:04.6668478 with `VALIDATE_FULL: DEFAULT GATE PASSED`, project filters
    0 errors, runtime boundaries 0 errors, DX12 validation errors 0,
    screenshots matching baselines, and `physics_regression_solver.csv`
    byte-exact. Final log:
    `Agentic/Reports/validate_full_plan04_capture_result_20260709.log`.

  Progress 2026-07-09, Input cursor/client-coordinate recoverable result
  sub-slice:
  - Converted `SkullbonezSource/Runtime/Input.cpp` rows 202, 204, 205, 207,
    and 208 from Win32 cursor/client-coordinate exceptions to Lane R
    `SbResult::Failure("Runtime/Input", ...)` results.
  - `Input::GetMouseCoordinates` and `Input::GetClientMouseCoordinates` now
    return `Input::MouseCoordinatesResult`; cursor warping and centering now
    return `SbResult`. Run frame input reports recoverable cursor failures to
    `stderr`, camera mouse-look resets without stale deltas, UI automation
    overrides avoid Win32 cursor reads, and optional editor/replay gestures skip
    the current frame when client coordinates are unavailable.
  - Strict anchored source throw statement inventory now reports 105 sites,
    down from the previous sub-slice count of 110. `SB_FATAL` macro invocations
    remain 151 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Runtime/Editor/RunEditorTools.cpp`,
    `SkullbonezSource/Runtime/Editor/RunMousePickupTools.cpp`,
    `SkullbonezSource/Runtime/Input.cpp`,
    `SkullbonezSource/Runtime/Input.h`,
    `SkullbonezSource/Runtime/InputController.cpp`,
    `SkullbonezSource/Runtime/InputController.h`,
    `SkullbonezSource/Runtime/Replay/RunReplayCauseTreeTools.cpp`,
    `SkullbonezSource/Runtime/Replay/RunReplayScrubberTools.cpp`,
    `SkullbonezSource/Runtime/Replay/RunReplayVelocityEdit.cpp`,
    `SkullbonezSource/Runtime/RunInput.cpp`, and
    `SkullbonezSource/UI/UIInput.cpp`; checked 11, deferred 0. This was a
    touched-file audit, so no subsystem checklist plan was required.
  - Focused build passed: `tools\validate_build.bat Profile` exited 0 in
    00:00:12.6993802 with 0 warnings and 0 errors. Log:
    `Agentic/Reports/validate_build_profile_plan04_input_result_20260709.log`.
  - First `tools\validate_full.bat` attempt failed formatting after
    00:00:44.8937859 because `SkullbonezSource/Runtime/Input.h` needed the
    header formatting pipeline. The touched headers were formatted directly
    with clang-format plus `tools\align_header_inline_comments.py`, and
    `tools\validate_format.bat` passed in 00:00:09.3041026.
  - Required gate then passed on the final source state:
    `tools\validate_full.bat` exited 0 in 00:01:06.3735933 with
    `VALIDATE_FULL: DEFAULT GATE PASSED`, project filters 0 errors, runtime
    boundaries 0 errors, source formatting clean, DX12 validation errors 0,
    screenshots matching baselines, and `physics_regression_solver.csv`
    byte-exact. Final log:
    `Agentic/Reports/validate_full_plan04_input_result_20260709_final.log`.

  Progress 2026-07-09, Timer high-resolution counter recoverable startup
  boundary sub-slice:
  - Converted `SkullbonezSource/Core/Timer.cpp` row 223 from a constructor-time
    exception to a Lane R `SbResult::Failure("Core/Timer", ...)` returned by
    explicit timer startup.
  - `Timer` now default-constructs inert storage, `Timer::Initialise()` captures
    the high-resolution counter, `RunTimerState::Initialise()` initializes the
    five Run-owned timers together, and `Run::Initialise()` reports timer
    startup failure through the existing process-boundary runtime result path.
  - Timer sampling before successful startup, or a post-startup counter failure,
    is now a `SB_FATAL("Core/Timer", ...)` owner invariant rather than a
    recoverable environment error.
  - Strict anchored source throw statement inventory now reports 104 sites,
    down from the previous sub-slice count of 105. `SB_FATAL` macro invocations
    now report 153 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope: `SkullbonezSource/Core/Timer.cpp`,
    `SkullbonezSource/Core/Timer.h`,
    `SkullbonezSource/Runtime/Run.cpp`, and
    `SkullbonezSource/Runtime/RunTimerState.h`; checked 4, deferred 0. This
    was a touched-file audit, so no subsystem checklist plan was required.
  - Focused build passed: `tools\validate_build.bat Profile` exited 0 in
    00:00:10.9987094 with 0 warnings and 0 errors. Log:
    `Agentic/Reports/validate_build_profile_plan04_timer_result_20260709.log`.
  - Required gate passed: `tools\validate_full.bat` exited 0 in
    00:01:06.2075778 with `VALIDATE_FULL: DEFAULT GATE PASSED`, project filters
    0 errors, runtime boundaries 0 errors, Profile/Debug builds 0
    warnings/errors, source formatting clean, DX12 validation errors 0,
    screenshots matching baselines, and `physics_regression_solver.csv`
    byte-exact. Final log:
    `Agentic/Reports/validate_full_plan04_timer_result_20260709.log`.

  Progress 2026-07-09, text/font SDF atlas recoverable startup boundary
  sub-slice:
  - Converted `SkullbonezSource/Rendering/Text.cpp` inventory rows 96 and 97
    from startup atlas generation/load-after-generate exceptions to Lane R
    `SbResult::Failure("Rendering/Text", ...)` results.
  - `Text2d::BuildFont`, `UiTextPass::EnsureGpuResources`, and
    `RuntimeRenderer::EnsureUiTextResources` now return `SbResult`, and
    `Run::Initialise()` reports atlas setup failure through
    `m_lastSceneLoadResult` before scene loading begins.
  - Strict anchored source throw statement inventory now reports 102 sites,
    down from the previous sub-slice count of 104. `SB_FATAL` macro
    invocations remain 153 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope: `SkullbonezSource/Rendering/Text.cpp`,
    `SkullbonezSource/Rendering/Text.h`,
    `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h`,
    `SkullbonezSource/Runtime/Render/RuntimeRenderer.h`,
    `SkullbonezSource/Runtime/Run.cpp`,
    `SkullbonezSource/Runtime/RunRender.cpp`, and
    `SkullbonezSource/Runtime/RunUiTextPass.cpp`; checked 7, deferred 0. This
    was a touched-file audit, so no subsystem checklist plan was required.
  - Focused build passed: `tools\validate_build.bat Profile` exited 0 in
    00:00:10.8513394 with 0 warnings and 0 errors. Log:
    `Agentic/Reports/validate_build_profile_plan04_text_result_20260709.log`.
  - Required gate passed: `tools\validate_full.bat` exited 0 in
    00:01:01.1972670 with `VALIDATE_FULL: DEFAULT GATE PASSED`, project filters
    0 errors, runtime boundaries 0 errors, Profile/Debug builds 0
    warnings/errors, source formatting clean, DX12 validation errors 0,
    screenshots matching baselines, and `physics_regression_solver.csv`
    byte-exact. Final log:
    `Agentic/Reports/validate_full_plan04_text_result_20260709.log`.

  Progress 2026-07-09, DX12 screenshot readback recoverable result sub-slice:
  - Converted `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Readback.cpp`
    inventory rows 94 and 95 from readback exceptions to Lane R results returned
    through `IRenderCaptureBackend::CaptureBackbuffer` and
    `CaptureSystem::SaveBackbufferBmp`.
  - Removed the unused readback `ThrowIfFailed` helper. Readback buffer creation
    failure now restores the backbuffer state and returns
    `SbResult::Failure("Rendering/DX12", ...)`; `CaptureSystem` propagates
    readback failures and checks for short pixel buffers before BMP writes.
  - Strict anchored source throw statement inventory now reports 100 sites,
    down from the previous sub-slice count of 102. `SB_FATAL` macro
    invocations remain 153 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Rendering/IRenderCaptureBackend.h`,
    `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`,
    `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Readback.cpp`, and
    `SkullbonezSource/Runtime/CaptureSystem.cpp`; checked 4, deferred 0. This
    was a touched-file audit, so no subsystem checklist plan was required.
  - Focused build passed: `tools\validate_build.bat Profile` exited 0 in
    00:00:11.0128484 with 0 warnings and 0 errors. Log:
    `Agentic/Reports/validate_build_profile_plan04_dx12_readback_result_20260709.log`.
  - Required DX12 gate passed: `tools\validate_dx12_renderer.bat` exited 0 with
    `VALIDATE_DX12_RENDERER: ALL PASSED`, DX12 validation errors 0, and
    screenshots matching committed baselines. Log:
    `Agentic/Reports/validate_dx12_renderer_plan04_readback_result_20260709_final.log`.
  - Required Runtime gate passed: `tools\validate_full.bat` exited 0 in
    00:00:47.7232961 with `VALIDATE_FULL: DEFAULT GATE PASSED`, project filters
    0 errors, runtime boundaries 0 errors, Profile/Debug builds 0
    warnings/errors, source formatting clean, DX12 validation errors 0,
    screenshots matching baselines, and `physics_regression_solver.csv`
    byte-exact. Final log:
    `Agentic/Reports/validate_full_plan04_dx12_readback_result_20260709.log`.

  Progress 2026-07-09, DXR initialization recoverable result batch:
  - Converted fourteen DXR setup/resource rows from exceptions to Lane R results:
    `BLASDX12.cpp` rows 5 through 7, `RenderBackendDX12.DXR.cpp` rows 79
    through 85, `SBTDX12.cpp` row 184, and `TLASDX12.cpp` rows 194 through
    196 from the Step 0.1 inventory.
  - `IRenderRayTracing::InitDXR`, `RenderBackendDX12::InitDXR`, DXR root
    signature/pipeline/reflection-UAV helpers, `BLAS::Build`, `TLAS::Init`, and
    `SBT::Build` now return `SbResult`. `Run::LoadScene` reports failed DXR
    setup through the existing scene-load result path.
  - The failure path that can occur after the terrain BLAS command has been
    recorded now drains the command list before releasing DXR resources, so
    cleanup does not leave recorded commands pointing at freed acceleration
    structure memory.
  - Strict anchored source throw statement inventory now reports 86 sites, down
    from the previous sub-slice count of 100. `SB_FATAL` macro invocations
    remain 153 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Rendering/IRenderRayTracing.h`,
    `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`,
    `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp`,
    `SkullbonezSource/Rendering/DX12/BLASDX12.h`,
    `SkullbonezSource/Rendering/DX12/BLASDX12.cpp`,
    `SkullbonezSource/Rendering/DX12/TLASDX12.h`,
    `SkullbonezSource/Rendering/DX12/TLASDX12.cpp`,
    `SkullbonezSource/Rendering/DX12/SBTDX12.h`,
    `SkullbonezSource/Rendering/DX12/SBTDX12.cpp`, and
    `SkullbonezSource/Runtime/Scene/RunScene.cpp`; checked 10, deferred 0. This
    was a touched-file audit, so no subsystem checklist plan was required.
  - Focused build passed: `tools\validate_build.bat Profile` exited 0 in
    00:00:09.8573624 with 0 warnings and 0 errors. Log:
    `Agentic/Reports/validate_build_profile_plan04_dxr_init_result_20260709.log`.
  - Required DX12 gate passed: `tools\validate_dx12_renderer.bat` exited 0 in
    00:00:36.2265314 with `VALIDATE_DX12_RENDERER: ALL PASSED`, DX12 validation
    errors 0, and screenshots matching committed baselines. Log:
    `Agentic/Reports/validate_dx12_renderer_plan04_dxr_init_result_20260709.log`.
  - Required Runtime gate passed: `tools\validate_full.bat` exited 0 in
    00:00:49.5630120 with `VALIDATE_FULL: DEFAULT GATE PASSED`, project filters
    0 errors, runtime boundaries 0 errors, Profile/Debug builds 0
    warnings/errors, source formatting clean, DX12 validation errors 0,
    screenshots matching baselines, and `physics_regression_solver.csv`
    byte-exact. Final log:
    `Agentic/Reports/validate_full_plan04_dxr_init_result_20260709.log`.

  Progress 2026-07-09, DX12 device/backend startup recoverable result batch:
  - Converted the render startup boundary from boolean/exception flow to Lane R
    results: `IRenderDeviceLifecycle::Init`, `RenderBackendDX12::Init`,
    `Dx12RenderDevice::Init`, and `Runtime/Init.cpp` now use `SbResult` for
    startup failure propagation.
  - Device startup failures now return `SbResult::Failure("Rendering/DX12", ...)`
    for factory/device/queue/swap-chain creation, swap-chain QI, command
    allocator/list/fence creation, and frame fence event creation. Backend startup
    also returns recoverable results for RTV/DSV/SRV/staging descriptor heap
    creation and initial swap-chain back-buffer acquisition.
  - `InitRenderBackend` publishes `RuntimeRenderBackendView` borrows only after a
    successful backend init. `WinMain` reports render startup failures with the
    existing owner/message stderr path before cleaning up the window.
  - Strict anchored source throw statement inventory now reports 85 sites, down
    from the previous sub-slice count of 86. Several converted startup rows were
    helper-call sites, so the anchored statement drop is the frame-fence
    `CreateEvent` row; remaining `ThrowIfFailed` helpers still cover non-startup
    present/resize/upload/readback paths. `SB_FATAL` macro invocations remain 153
    via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Rendering/IRenderDeviceLifecycle.h`,
    `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`,
    `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`,
    `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.h`,
    `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp`, and
    `SkullbonezSource/Runtime/Init.cpp`; checked 6, deferred 0. This was a
    touched-file audit, so no subsystem checklist plan was required.
  - Focused build passed: `tools\validate_build.bat Profile` exited 0 in
    00:00:06.8814348 with 0 warnings and 0 errors. Log:
    `Agentic/Reports/validate_build_profile_plan04_device_backend_init_result_20260709.log`.
  - Required DX12 gate passed after a targeted `clang-format` fix for
    `RenderDeviceDX12.cpp`: `tools\validate_dx12_renderer.bat` exited 0 in
    00:00:38.4244381 with `VALIDATE_DX12_RENDERER: ALL PASSED`, formatting clean,
    DX12 validation errors 0, and screenshots matching committed baselines. Final
    log:
    `Agentic/Reports/validate_dx12_renderer_plan04_device_backend_init_result_20260709_rerun.log`.
  - Required Runtime gate passed: `tools\validate_full.bat` exited 0 in
    00:00:48.5940872 with `VALIDATE_FULL: DEFAULT GATE PASSED`, project filters
    0 errors, runtime boundaries 0 errors, Profile/Debug builds 0
    warnings/errors, source formatting clean, DX12 validation errors 0,
    screenshots matching baselines, and `physics_regression_solver.csv`
    byte-exact. Final log:
    `Agentic/Reports/validate_full_plan04_device_backend_init_result_20260709.log`.

  Progress 2026-07-09, DX12 root-signature/gen-mips startup result batch:
  - Converted startup-only `RenderBackendDX12::CreateRootSignature` and
    `RenderBackendDX12::InitGenMipsPipeline` from throwing setup helpers to Lane R
    `SbResult` returns propagated through `RenderBackendDX12::Init`.
  - Main root-signature serialization/creation failures and generate-mips shader
    file, compile, root-signature, and compute-PSO setup failures now report
    `SbResult::Failure("Rendering/DX12", ...)`. The shared texture
    `ThrowIfFailed` helper remains for non-startup texture upload paths.
  - Strict anchored source throw statement inventory now reports 81 sites, down
    from the previous sub-slice count of 85. `SB_FATAL` macro invocations remain
    153 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`,
    `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`, and
    `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Textures.cpp`; checked 3,
    deferred 0. This was a touched-file audit, so no subsystem checklist plan was
    required.
  - Focused build passed: `tools\validate_build.bat Profile` exited 0 in
    00:00:09.4761390 with 0 warnings and 0 errors. Log:
    `Agentic/Reports/validate_build_profile_plan04_root_mips_result_20260709.log`.
  - Required DX12 gate passed: `tools\validate_dx12_renderer.bat` exited 0 in
    00:00:27.5943890 with `VALIDATE_DX12_RENDERER: ALL PASSED`, formatting clean,
    DX12 validation errors 0, and screenshots matching committed baselines. Log:
    `Agentic/Reports/validate_dx12_renderer_plan04_root_mips_result_20260709.log`.
  - `tools\validate_full.bat` was not required for this sub-slice because the
    final diff only touched DX12 renderer startup files and Plan 04 documentation.

  Progress 2026-07-09, RenderDeviceDX12 descriptor fatal-invariant follow-up:
  - Converted six shader-visible descriptor allocator capacity/index/caller
    invariant throws to `SB_FATAL("RenderDeviceDX12", ...)`: static SRV heap
    exhaustion, transient SRV heap exhaustion, zero-count transient range
    requests, transient range exhaustion, shader-visible descriptor index
    validation, and staging descriptor index validation.
  - These rows were listed in the original inventory's R bucket with the note to
    keep them recoverable unless the message identified an owner invariant. This
    batch applies that exception because fixed descriptor capacity and invalid
    descriptor-table indices are render-owner invariants.
  - Strict anchored source throw statement inventory now reports 75 sites, down
    from the previous sub-slice count of 81. `SB_FATAL` macro invocations now
    report 159 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp`; checked 1, deferred
    0. This was a touched-file audit, so no subsystem checklist plan was
    required.
  - Focused build passed: `tools\validate_build.bat Profile` exited 0 in
    00:00:05.3974010 with 0 warnings and 0 errors. Log:
    `Agentic/Reports/validate_build_profile_plan04_descriptor_fatals_20260709.log`.
  - Required DX12 gate first failed formatting on
    `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp`; a targeted
    VS-bundled `clang-format` pass was applied to that file only. The rerun
    passed: `tools\validate_dx12_renderer.bat` exited 0 in 00:00:27.1200192 with
    `VALIDATE_DX12_RENDERER: ALL PASSED`, formatting clean, DX12 validation
    errors 0, and screenshots matching committed baselines. Final log:
    `Agentic/Reports/validate_dx12_renderer_plan04_descriptor_fatals_20260709_rerun.log`.
  - `tools\validate_full.bat` was not required for this sub-slice because the
    final diff only touched DX12 renderer code and Plan 04 documentation.

  Progress 2026-07-09, DX12 invariant leftovers fatal batch:
  - Converted three remaining DX12 owner-invariant throws to `SB_FATAL`: invalid
    shader-visible descriptor allocator init geometry, readback map bounds that
    exceed the allocated readback buffer, and unsupported render-graph color
    transient formats.
  - These rows were listed in the original inventory's R bucket with the note to
    keep them recoverable unless the message identified an owner invariant. This
    batch applies that exception because invalid allocator geometry, invalid
    readback map ranges, and unsupported graph enum values are backend/caller
    contract violations. D3D12 resource creation and fence/device failures remain
    deferred to recoverable-result batches.
  - Strict anchored source throw statement inventory now reports 72 sites, down
    from the previous sub-slice count of 75. `SB_FATAL` macro invocations now
    report 162 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp` and
    `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp`; checked 2, deferred
    0. This was a touched-file audit, so no subsystem checklist plan was
    required.
  - Focused build passed: `tools\validate_build.bat Profile` exited 0 in
    00:00:06.8697852 with 0 warnings and 0 errors. Log:
    `Agentic/Reports/validate_build_profile_plan04_dx12_invariant_leftovers_20260709.log`.
  - Required DX12 gate passed: `tools\validate_dx12_renderer.bat` exited 0 in
    00:00:26.8442917 with `VALIDATE_DX12_RENDERER: ALL PASSED`, formatting
    clean, DX12 validation errors 0, and screenshots matching committed
    baselines. Log:
    `Agentic/Reports/validate_dx12_renderer_plan04_dx12_invariant_leftovers_20260709.log`.
  - `tools\validate_full.bat` was not required for this sub-slice because the
    final diff only touched DX12 renderer code and Plan 04 documentation.

  Progress 2026-07-09, unused DX12 throw-helper cleanup:
  - Removed three dead local `ThrowIfFailed` helpers from
    `RenderBackendDX12.Resources.cpp`, `RenderBackendDX12.Profiler.cpp`, and
    `RenderBackendDX12.Pipeline.cpp`. These helpers had no call sites but still
    contributed strict throw statements.
  - Strict anchored source throw statement inventory now reports 69 sites, down
    from the previous sub-slice count of 72. `SB_FATAL` macro invocations remain
    162 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Resources.cpp`,
    `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Profiler.cpp`, and
    `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp`; checked 3,
    deferred 0. This was a touched-file audit, so no subsystem checklist plan was
    required.
  - Focused build passed: `tools\validate_build.bat Profile` exited 0 in
    00:00:05.7470693 with 0 warnings and 0 errors. Log:
    `Agentic/Reports/validate_build_profile_plan04_unused_dx12_helpers_20260709.log`.
  - Required DX12 gate passed: `tools\validate_dx12_renderer.bat` exited 0 in
    00:00:24.5943424 with `VALIDATE_DX12_RENDERER: ALL PASSED`, formatting
    clean, DX12 validation errors 0, and screenshots matching committed
    baselines. Log:
    `Agentic/Reports/validate_dx12_renderer_plan04_unused_dx12_helpers_20260709.log`.
  - `tools\validate_full.bat` was not required for this sub-slice because the
    final diff only touched DX12 renderer code and Plan 04 documentation.

  Progress 2026-07-09, TextureCollection recoverable texture-load boundary:
  - Converted six texture asset/file/backend failure rows from exception exits to
    Lane R results: `TextureCollection.cpp` rows 211, 215, 217, 219, 220, and
    221 from the Step 0.1 inventory.
  - `TextureCollection` now returns `SbResult` for lazy texture residency,
    JPEG/source-asset loading, texture selection, and source-asset rebuilds.
    `GetTextureHandle` returns a small handle result so DXR reflection can skip
    dispatch when required textures are unavailable.
  - Startup texture rebuild and skybox resource reset now report failures
    through `Run::Initialise` / `m_lastSceneLoadResult`. Per-frame skybox,
    object, terrain, replay-ghost, and reflection pass texture failures are
    reported at the pass boundary and skip the affected draw/dispatch.
  - The former missing-index throw is now guarded behind the public result
    contract and treated as a fatal owner invariant if an internal caller skips
    that contract. `TextureCollection::DeleteTexture` no-ops for nonresident
    hashes instead of entering the fatal index path.
  - Strict anchored source throw statement inventory now reports 63 sites, down
    from the previous sub-slice count of 69. `SB_FATAL` macro invocations now
    report 163 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Assets/TextureCollection.cpp`,
    `SkullbonezSource/Assets/TextureCollection.h`,
    `SkullbonezSource/Runtime/Render/RuntimeRenderer.h`,
    `SkullbonezSource/Runtime/Run.cpp`,
    `SkullbonezSource/Runtime/Run.h`,
    `SkullbonezSource/Runtime/RunPasses.cpp`,
    `SkullbonezSource/Runtime/RunRender.cpp`,
    `SkullbonezSource/World/SkyBox.cpp`, and
    `SkullbonezSource/World/SkyBox.h`; checked 9, deferred 0. This was a
    touched-file audit, so no subsystem checklist plan was required.
  - Focused build passed: `tools\validate_build.bat Profile` exited 0 in
    00:00:06.7432938 with 0 warnings and 0 errors. Log:
    `Agentic/Reports/validate_build_profile_plan04_texture_result_20260709.log`.
  - Required DX12 gate passed on the final tree: `tools\validate_dx12_renderer.bat`
    exited 0 in 00:00:36.5685613 with `VALIDATE_DX12_RENDERER: ALL PASSED`,
    formatting clean, DX12 validation errors 0, and screenshots matching
    committed baselines. Log:
    `Agentic/Reports/validate_dx12_renderer_plan04_texture_result_20260709.log`.
  - `tools\validate_full.bat` was not required for this sub-slice because the
    behavioral surface is renderer texture/resource handling and the required
    DX12 renderer gate passed.

  Progress 2026-07-09, ConvexHullShape recoverable hull-load boundary:
  - Converted forty-one authored hull parse/load rows from exception exits to
    Lane R results: `ConvexHullShape.cpp` rows 143 through 183 from the Step 0.1
    inventory.
  - `ConvexHullShape::TryLoadFromFile` now returns `SbResult` with bounded
    owner/message diagnostics and uses an RAII file handle so parse failures do
    not need rethrow blocks to close the file. `LoadFromFile` remains as a
    legacy/test valid-path wrapper that turns an unexpected failure into
    `SB_FATAL("Physics/ConvexHullShape", ...)`.
  - Scene-authored hull setup returns hull-load failures through the existing
    `SceneAuthoredSetup::SetUpGameModels` / `Run::LoadScene` result boundary.
    Test-scene default-mass lookup feeds failures into the existing parser
    `Fail` boundary. Editor placement and editor asset caches log and skip the
    affected placement/cache entry instead of terminating the app.
  - Scaled-hull face degeneration is now a fatal owner invariant because
    positive finite copy-scale should preserve already validated baked hull
    topology.
  - Strict anchored source throw statement inventory now reports 22 sites, down
    from the previous sub-slice count of 63. `SB_FATAL` macro invocations now
    report 165 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Physics/ConvexHullShape.cpp`,
    `SkullbonezSource/Physics/ConvexHullShape.h`,
    `SkullbonezSource/Runtime/Editor/RunEditorObjectPlacement.cpp`,
    `SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp`,
    `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp`, and
    `SkullbonezSource/Scene/TestSceneParser.cpp`; checked 6, deferred 0. This
    was a touched-file audit, so no subsystem checklist plan was required.
  - Focused build passed: `tools\validate_build.bat Profile` exited 0 in
    00:00:18.2303251 with 0 warnings and 0 errors. Log:
    `Agentic/Reports/validate_build_profile_plan04_convex_hull_result_20260709.log`.
  - Runtime boundary checker passed: `python tools\check_runtime_boundaries.py
    --repo . --json-out Agentic/Reports/check_runtime_boundaries_plan04_convex_hull_result_20260709.json`
    exited 0 in 00:00:19.5329653 with 0 errors. The generated JSON summary was
    removed from the worktree after the pass; log:
    `Agentic/Reports/check_runtime_boundaries_plan04_convex_hull_result_20260709.log`.
  - Required physics gate passed on the final source tree:
    `tools\validate_physics.bat` exited 0 in 00:00:29.0767839 with
    `VALIDATE_PHYSICS: ALL PASSED`, Debug/Profile builds 0 warnings/errors, and
    deterministic physics output matching the committed baseline. Log:
    `Agentic/Reports/validate_physics_plan04_convex_hull_result_20260709.log`.

  Progress 2026-07-09, DX12 lifecycle/depth-stencil/texture recoverable batch:
  - Converted five strict DX12 rows from exception exits to Lane R result or
    failure-handle reporting: `RenderBackendDX12.cpp` rows 52, 71, 72, and 73
    plus `RenderBackendDX12.Textures.cpp` row 102 from the Step 0.1 inventory.
  - `IRenderDeviceLifecycle::Present` and `Resize` now return `SbResult`.
    `Run::Execute` returns `SbResult` so Present/device-loss failures flow to
    the existing `reportRunResult` startup/frame boundary. `Window::HandleScreenResize`
    returns `SbResult`; startup checks the initial resize result, and WndProc logs
    resize failures before posting quit.
  - `RenderBackendDX12::CreateDepthStencil` now returns `SbResult` through both
    startup and resize. `RenderBackendDX12::CreateTexture2D` logs resource
    creation failure and returns texture handle `0`, matching the existing
    nonresident texture contract that `TextureCollection` reports at its Lane R
    boundary.
  - Strict anchored source throw statement inventory now reports 17 sites, down
    from the previous sub-slice count of 22. `SB_FATAL` macro invocations remain
    165 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Physics/ConvexHullShape.cpp`,
    `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Textures.cpp`,
    `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`,
    `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`,
    `SkullbonezSource/Rendering/IRenderDeviceLifecycle.h`,
    `SkullbonezSource/Runtime/Init.cpp`, `SkullbonezSource/Runtime/Run.h`,
    `SkullbonezSource/Runtime/RunFrame.cpp`,
    `SkullbonezSource/Runtime/Window.cpp`, and
    `SkullbonezSource/Runtime/Window.h`; checked 10, deferred 0. The
    `ConvexHullShape.cpp` entry was a one-file clang-format correction required
    by the DX12 gate; it changed formatting only.
  - Focused build passed: `tools\validate_build.bat Profile` exited 0 in
    00:00:19.3 with 0 warnings and 0 errors. Log:
    `Agentic/Reports/validate_build_profile_plan04_dx12_lifecycle_texture_20260709.log`.
  - Required DX12 gate passed on the final source tree:
    `tools\validate_dx12_renderer.bat` exited 0 in 00:00:47.1 with
    `VALIDATE_DX12_RENDERER: ALL PASSED`, formatting clean, DX12 validation
    errors 0, and screenshots matching committed baselines. The first attempt
    stopped at formatting on `ConvexHullShape.cpp`; the final rerun passed after
    a targeted one-file format fix. Log:
    `Agentic/Reports/validate_dx12_renderer_plan04_lifecycle_texture_20260709.log`.
  - Runtime boundary checker passed: `python tools\check_runtime_boundaries.py`
    exited 0 in 00:00:19.6 with 0 errors. Log:
    `Agentic/Reports/check_runtime_boundaries_plan04_lifecycle_texture_20260709.log`.

  Progress 2026-07-09, DX12 PSO/dynamic-geometry recoverable batch:
  - Converted three strict DX12 rows from exception exits to Lane R logged
    skip/neutral-handle paths: `RenderBackendDX12.DynamicGeometry.cpp` rows 49
    and 50 plus `RenderBackendDX12.Pipeline.cpp` row 88 from the Step 0.1
    inventory.
  - `RenderBackendDX12::CreatePSO` logs PSO creation failures and returns
    `nullptr`; `PrepareDraw` now returns `false` when no valid PSO can be bound,
    and mesh/dynamic/instanced draw callers skip the affected draw. Graphics PSO
    cache exhaustion remains `SB_FATAL` because it is a fixed-capacity backend
    invariant.
  - `EnsureGridLinePipeline` logs debug-line PSO creation failures and skips the
    diagnostic overlay draw. `CreateInstancedMesh` logs failed static vertex
    buffer creation and returns handle `0`, matching the existing upload/draw
    no-op contract for invalid instanced mesh handles.
  - Strict anchored source throw statement inventory now reports 14 sites, down
    from the previous sub-slice count of 17. `SB_FATAL` macro invocations remain
    165 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp`,
    `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp`,
    `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`, and
    `SkullbonezSource/Rendering/DX12/MeshDX12.cpp`; checked 4, deferred 0. This
    was a touched-file audit, so no subsystem checklist plan was required.
  - Required DX12 gate passed on the final source tree:
    `tools\validate_dx12_renderer.bat` exited 0 in 00:00:34.3 with
    `VALIDATE_DX12_RENDERER: ALL PASSED`, formatting clean, Profile/Debug builds
    0 warnings/errors, DX12 validation errors 0, and screenshots matching
    committed baselines. Log:
    `Agentic/Reports/validate_dx12_renderer_plan04_pso_result_20260709.log`.
  - Runtime boundary checker passed: `python tools\check_runtime_boundaries.py`
    exited 0 in 00:00:19.5 with 0 errors. Log:
    `Agentic/Reports/check_runtime_boundaries_plan04_pso_result_20260709.log`.

  Progress 2026-07-09, DX12 shader compile/reflection status batch:
  - Converted four strict `ShaderDX12` rows from exception exits to logged
    `false` returns: rows 186 through 189 from the Step 0.1 inventory.
  - `ShaderDX12::Compile` now reports missing shader files, vertex shader
    compile failures, and pixel shader compile failures with DX12 log events
    before returning `false`.
  - `ShaderDX12::ReflectCB` now returns `false` for missing bytecode or
    `D3DReflect` failure after logging the stage/path/HRESULT. This keeps the
    shader wrapper itself in Lane R status-return form while leaving the
    outer `IRenderResourceFactory::CreateShader` failure contract for the
    dedicated resource-factory batch.
  - Strict anchored source throw statement inventory now reports 10 sites, down
    from the previous sub-slice count of 14. `SB_FATAL` macro invocations remain
    165 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Rendering/DX12/ShaderDX12.cpp` and
    `SkullbonezSource/Rendering/DX12/ShaderDX12.h`; checked 2, deferred 0. This
    was a touched-file audit, so no subsystem checklist plan was required.
  - Required DX12 gate passed on the final source tree:
    `tools\validate_dx12_renderer.bat` exited 0 in 00:00:34.5 with
    `VALIDATE_DX12_RENDERER: ALL PASSED`, formatting clean, Profile/Debug builds
    0 warnings/errors, DX12 validation errors 0, and screenshots matching
    committed baselines. Log:
    `Agentic/Reports/validate_dx12_renderer_plan04_shader_status_20260709.log`.
  - Runtime boundary checker passed: `python tools\check_runtime_boundaries.py`
    exited 0 in 00:00:19.5 with 0 errors. Log:
    `Agentic/Reports/check_runtime_boundaries_plan04_shader_status_20260709.log`.

  Progress 2026-07-09, DX12 fence/readback recoverable result batch:
  - Converted three strict `RenderDeviceDX12` rows from exception exits to Lane R
    result/neutral-return paths: local `ThrowIfFailed`, command queue
    `Signal`, and fence `SetEventOnCompletion` from the remaining Step 0.1
    inventory.
  - `Dx12FenceTimeline::Signal`, `SignalAndWait`, and `WaitForValue` now return
    `SbResult`. `RenderBackendDX12::Present` propagates fence signal/wait
    failures to the existing frame boundary, while profiler and allocator wait
    maintenance paths log and skip unsafe readback or command-list reopen work.
  - `Dx12ReadbackBuffer::MapRead` now returns `nullptr` on map failure; screenshot
    capture converts that to `SbResult::Failure("Rendering/DX12", ...)`, and GPU
    timer readback logs/drops the pending sample.
  - Frame upload buffer creation/map failures now return `false` through the
    existing upload-system startup contract instead of using the removed local
    throw helper.
  - Strict anchored source throw statement inventory now reports 7 sites, down
    from the previous sub-slice count of 10. `SB_FATAL` macro invocations remain
    165 via `rg -n "SB_FATAL\s*\(" SkullbonezSource`.
  - Comment-style audit scope:
    `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp`,
    `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h`,
    `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Profiler.cpp`,
    `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Readback.cpp`,
    `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp`, and
    `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.h`; checked 6, deferred 0.
    This was a touched-file audit, so no subsystem checklist plan was required.
  - Required DX12 gate passed on the final source tree:
    `tools\validate_dx12_renderer.bat` exited 0 in 00:00:40.0 with
    `VALIDATE_DX12_RENDERER: ALL PASSED`, formatting clean, Profile/Debug builds
    0 warnings/errors, DX12 validation errors 0, and screenshots matching
    committed baselines. Log:
    `Agentic/Reports/validate_dx12_renderer_plan04_dx12_fence_result_20260709.log`.
  - Runtime boundary checker passed: `python tools\check_runtime_boundaries.py`
    exited 0 in 00:00:18.3 with 0 errors. Log:
    `Agentic/Reports/check_runtime_boundaries_plan04_dx12_fence_result_20260709.log`.

  Progress 2026-07-10, RuntimeAllocationTracker allocator-safe fatal sub-slice:
  - Converted the final Lane F strict throw, `RuntimeAllocationTracker.cpp` row
    393 from the Step 0.1 inventory, from `throw std::bad_alloc()` to a local
    allocator-safe fatal path.
  - `FatalAllocationFailure()` formats a bounded stack buffer, writes it to the
    debugger/stderr, breaks in Debug/Profile, and aborts. It intentionally does
    not call `SB_FATAL` because `SB_FATAL` uses `EngineLog`, which is unsafe
    after `malloc` has already failed inside global `operator new`.
  - Strict anchored source throw statement inventory now reports 6 sites, all
    Lane R rows. The remaining rows are `TestSceneParser` recoverable scene
    parse failure and DX12 resource/shader creation failures.
  - Comment-style audit scope:
    `SkullbonezSource/Runtime/Allocation/RuntimeAllocationTracker.cpp`; checked
    1, deferred 0. The new local comment names the Lane F allocator-hook hazard.
  - Focused build passed: `tools\validate_build.bat Profile` exited 0 in
    00:00:05.53 with 0 warnings/errors. Log:
    `Agentic/Reports/validate_build_profile_plan04_alloc_fatal_20260710.log`.
  - Required gate passed: `tools\validate_perf.bat` completed with Profile and
    Debug builds at 0 warnings/errors, allocation policy
    `allowlist_errors=0`, allocation guard `gameplay_violations=0`, runtime
    reserve `policy_violations=0`, DX12 perf no regressions, and physics_bench
    no regressions. Log:
    `Agentic/Reports/validate_perf_plan04_alloc_fatal_20260710.log`.
- [ ] **4.1** Do **not** maintain any throw count. The `MAX_SOURCE_THROW_TOKENS`
  ratchet is deleted by plan 03. Verify progress by re-running
  `rg -n "throw " SkullbonezSource` and confirming the F/R/P sites are converted;
  build + review are the gate. No regex budget is reinstated.

## Validation

`tools\validate_full.bat`; `tools\validate_physics.bat` for the physics
conversions (byte-exact); `tools\validate_perf.bat` for runtime allocation
policy code.

## Acceptance (measurable)

- [ ] `throw` count materially reduced from 283; `SB_FATAL` or an
  allocator-safe fatal is the mechanism for engine invariants (well above 2
  sites).
- [ ] No `throw` remains in per-frame hot loops or the DX12 present path.
- [ ] No throw count is tracked anywhere (no regex ratchet reinstated);
  conversions are verified by grep + review.
- [ ] `AGENTS.md` describes the lanes the code actually uses.
