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
  statements across source. The checker's ratchet
  `MAX_SOURCE_THROW_TOKENS = 294`
  ([check_runtime_boundaries.py](../tools/check_runtime_boundaries.py)) is frozen
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
not track a throw count** — the `MAX_SOURCE_THROW_TOKENS` regex ratchet is
deleted by plan 03; progress is measured by throws actually converted, not by a
frozen budget. Where exceptions are genuinely appropriate (external IO at a
boundary), say so.

## Approach

- [x] **Phase 0 — Categorize all 283 throws** by lane: F (should-never-happen
  engine invariant), R (external input/environment: scene/asset/file IO), P
  (probe/automation assertion).
- [ ] **Phase 1 — F → `SB_FATAL`.** Convert physics capacity guards and
  frame-loop invariants. This removes unwinding through the message loop and
  profiling RAII — the core robustness win.
- [ ] **Phase 2 — P → `FailAutomation`.** Route replay/interaction probe throws
  to the machine-readable automation channel with `ok=false` + message.
- [ ] **Phase 3 — R → `SbResult`.** Convert scene/asset/file IO failures to
  value-carrying results reported at the boundary.
- [ ] **Phase 4 — No throw count.** The `MAX_SOURCE_THROW_TOKENS` ratchet is
  deleted by plan 03. Do not reinstate any budget; verify conversions by
  `rg "throw "` + review.

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
- [ ] **1.1** Convert **F** sites (physics capacity guards, frame-loop
  invariants) to `SB_FATAL(owner, ...)`, **one subsystem at a time**. Gate:
  `validate_physics` for physics, `validate_full` otherwise. Commit per
  subsystem.

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
- [ ] **2.1** Convert **P** sites (replay/interaction probes) to the
  `FailAutomation(...)` channel with `ok=false` + message. Gate: `validate_full`
  + replay scrub. Commit.
- [ ] **3.1** Convert **R** sites (scene/asset/file IO) to `SbResult` reported at
  the boundary, **one boundary at a time**. Gate: `validate_full`. Commit.
- [ ] **4.1** Do **not** maintain any throw count. The `MAX_SOURCE_THROW_TOKENS`
  ratchet is deleted by plan 03. Verify progress by re-running
  `rg -n "throw " SkullbonezSource` and confirming the F/R/P sites are converted;
  build + review are the gate. No regex budget is reinstated.

## Validation

`tools\validate_full.bat`; `tools\validate_physics.bat` for the physics
conversions (byte-exact).

## Acceptance (measurable)

- [ ] `throw` count materially reduced from 283; `SB_FATAL` is the mechanism for
  engine invariants (well above 2 sites).
- [ ] No `throw` remains in per-frame hot loops or the DX12 present path.
- [ ] No throw count is tracked anywhere (no regex ratchet reinstated);
  conversions are verified by grep + review.
- [ ] `AGENTS.md` describes the lanes the code actually uses.
