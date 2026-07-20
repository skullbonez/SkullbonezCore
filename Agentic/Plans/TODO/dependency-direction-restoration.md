# Dependency Direction Restoration

Status: Active — 4/6 tasks (L0-L3 complete; L4-L5 pending)
Owner: repository owner; registered 2026-07-20 as campaign plan 1 of 8
Evidence: `../../Reports/2026-07-20/engine-architecture-review.md` (finding A)
Ledger: L0-L5

## Objective

Make dependency direction real and grep-enforceable: `Core/` depends on
nothing above it, `Physics/` and `Rendering/` never include `Runtime/` or
`UI/`, and the direction rule is written into `AGENTS.md` so review can verify
it by include direction alone. This plan is almost entirely mechanical file
moves plus include/namespace fixups; it unblocks verifiability for every later
campaign plan.

## Problem / Evidence

At the 2026-07-20 review tip there is no enforceable lower layer:

- ~12 physics files and 3 DX12/Rendering files include
  `Runtime/Scene/SceneCapacity.h` (pure capacity constants misplaced in
  Runtime).
- Solver stage headers include `Runtime/Replay/ReplaySolverSnapshot.h`
  (`PhysicsWorld.h:55`, `PhysicsContactSolverStage.h:45`,
  `PhysicsSleepController.h:52`, `PhysicsStepDiagnostics.h:31`).
- `PhysicsWorld.cpp` includes `Runtime/Allocation/*` and
  `Runtime/Replay/ReplayRetainedMemory.h`; `RenderDeviceDX12.h:49` includes
  `RuntimeAllocationTracker.h`.
- `SimulationSystem` sits in `Physics/` but is `namespace Runtime` and
  includes `Runtime/RuntimeInteractionController.h`.
- `Core/Config.h:39`, `Core/WorkerPool.h:34`, `Core/SkullScope.cpp:33-37`,
  and `Core/Profiler.cpp:35-53` include upward into Runtime, Assets, Physics,
  and Rendering.

## Non-Goals

- No behavior change of any kind: no baseline, golden, screenshot, replay,
  or perf-artifact refresh is authorized.
- No renaming beyond what a file move forces (namespace/type renames are
  allowed only where a moved type's namespace would otherwise lie about its
  owner, and each such rename is recorded in the task evidence).
- No decomposition of the moved owners; this plan changes location and
  include direction only.
- The Core→Physics (`SkullScope`) and Core→Rendering (`Profiler`) inversions
  are bounded interface work in L4, not open-ended redesign.

## Binding Decisions

1. Target direction after closure:
   `Core → Maths → Assets/Physics/Rendering/Scene/World → Runtime → UI-facing
   runtime owners`. Physics and Rendering may include Core and Maths only.
2. `SceneCapacity.h` moves to `Core/` (it is capacity constants, not scene
   logic).
3. `Runtime/Allocation/*` moves to `Core/Allocation/` — allocation policy is
   cross-cutting engine policy, not runtime business logic. The
   `tools/check_allocation_policy.py` source roots, the allowlist rows in
   `tools/allocation_policy_allowlist.json`, and the `AGENTS.md`
   file-to-validation mapping row for `Runtime/Allocation/*` update in the
   same commit as the move.
4. `ReplaySolverSnapshot` becomes a physics-owned type:
   `Physics/PhysicsSolverSnapshot.h`, `namespace Physics`, with all call
   sites (replay included) updated in the same commit. No compatibility
   alias, forwarding header, or `using` shim may remain (Migration Cleanup
   Review Rule).
5. `SimulationSystem` moves to `Runtime/` where its namespace already lives.
6. Every include edge that survives against the target direction must be
   recorded in L5 with owner, reason, and deletion condition, or the plan is
   not closable.

## Tasks

- [x] L0 — Move `Runtime/Scene/SceneCapacity.h` to `Core/SceneCapacity.h`;
  update every includer (Physics, Rendering, Runtime, Core/Config.h);
  update `.vcxproj`/`.filters`. Proof: `grep -rn "Runtime/Scene/SceneCapacity"
  SkullbonezSource` returns zero rows. Validation: `tools\validate_full.bat`
  (multi-area mechanical sweep).
  - Evidence (2026-07-20): the capacity contract moved without namespace or
    value changes; all 22 include sites across Assets, Core, Physics,
    Rendering, and Runtime now resolve through `Core/SceneCapacity.h` (with
    the same-directory include in `Core/Config.h`). The project/filter entry
    and semantic filter validator classify it as Core. Both the exact stale
    path proof and an expanded all-include-spellings proof pass with zero
    stale Runtime/Scene edges.
  - Final gates: `tools\validate_full.bat` passed in 3m24.3s (0 project-filter
    errors, clean Profile/Debug builds, CPU/coverage/runtime lanes, zero DX12
    validation errors, three image baselines, and byte-exact physics), and
    `tools\run_graphics_stress.bat 1` completed its PID-bounded minute in
    60.85s with exit 0. No tracked behavioral artifact changed.
- [x] L1 — Move `Runtime/Allocation/*` to `Core/Allocation/`; update
  includers, project files, `tools/check_allocation_policy.py` roots,
  `tools/allocation_policy_allowlist.json` rows, and the `AGENTS.md` mapping
  row in the same commit. Validation: `tools\validate_fast.bat`, then
  `python tools\check_allocation_policy.py --self-test` and
  `python tools\check_allocation_policy.py --repo .`, then
  `tools\validate_perf.bat` (allocation guard semantics are path-sensitive),
  then `tools\validate_full.bat`.
  - Evidence (2026-07-20): all seven allocation-policy files moved physically
    to `Core/Allocation/`; 37 include edges, both production/test project and
    filter inventories, the semantic filter validator, allocation allowlist,
    coverage metadata, and the `AGENTS.md` validation mapping now use the Core
    path. `check_allocation_policy.py` already scans the broad `Core/` root and
    now documents that ownership. The semantic `Runtime::Allocation` namespace
    is deliberately unchanged in this mechanical slice.
  - Direction correction: moving the global allocation hook exposed its Tracy
    heap-event dependency on `Runtime/DevelopmentTools`. Heavy-event emission
    and viewer-connection pairing now live beside the hook in
    `Core/Allocation/DevelopmentToolAllocation`; the runtime Tracy owner only
    publishes the heavy-capture enabled state. This preserves the hard-capped
    development allocation owner and removes the would-be new Core→Runtime
    edge. A runtime-contract test proves the seam remains inert outside heavy
    capture.
  - Validation: final `tools\validate_fast.bat` passed in 1m12.6s after three
    diagnostic iterations caught formatting/header alignment and two stale
    test contracts. Allocation self-test passed; repository scan reported
    `scanned=403`, 39 direct-heap findings, 147 dynamic-STL-member findings,
    647 STL-growth findings, and zero allowlist errors. Direct coverage passed
    in 19.6s with all 10 floors and 70.07% whole-product coverage.
    `tools\validate_perf.bat` completed in about 1m44s with zero steady-gameplay
    allocations or reserve-policy violations. Final `tools\validate_full.bat`
    passed in 2m26.1s with zero warnings/errors, all CPU/coverage/runtime lanes,
    zero DX12 validation errors, three passing image baselines, and byte-exact
    physics. `tools\run_graphics_stress.bat 1` ran PID 51736 for 60.8s and
    exited cleanly through the PID-scoped timeout.
  - Blocker and reconciliation: the one permitted replay-fidelity invocation
    completed its single 2,401-tick engine generation, then exposed an inherited
    config provenance mismatch from the already-landed v4→v5 audio-removal
    migration (`f5fe4daa…c09e3fe` expected versus tracked `engine.cfg`
    `83401df0…e5c784a3f4`). Under the standing provenance-only ruling, only
    `configSha256` changed; its mechanically derived visual-manifest SHA changed
    `a3db2039…452d3dc` → `931463f2…e4f6dc`. No behavioral golden field moved.
    The existing generated report then passed the primary checker and all nine
    offline negative/determinism control groups in 31.6s; no second engine
    process or prediction generation was launched.
- [x] L2 — Move `Runtime/Replay/ReplaySolverSnapshot.h` to
  `Physics/PhysicsSolverSnapshot.h` with namespace/type rename per binding
  decision 4; investigate and resolve the `PhysicsWorld.cpp` include of
  `ReplayRetainedMemory.h` (either it moves with the reserve-allocator
  relocation in L1 or the include is deleted; record which). Proof:
  `grep -rn "Runtime/Replay" SkullbonezSource/Physics` returns zero rows.
  Validation: `tools\validate_physics.bat` (byte-exact),
  `tools\validate_replay_visual_fidelity.bat` (Replay-facing edit; one engine
  process, one generation, zero golden refresh per MASTER rule 11), then
  `tools\validate_full.bat`.
  - Evidence (2026-07-20): the retained solver contract is now
    `Physics/PhysicsSolverSnapshot.h` in `SkullbonezCore::Physics`. The four
    public value names are `PhysicsSolverSnapshot`,
    `PhysicsSolverStatsSample`, `PhysicsSolverPersistentContactSample`, and
    `PhysicsSolverContactCacheSample`. Every Physics, Runtime replay,
    SceneWorld, artifact, and test caller uses the owning namespace directly;
    there is no forwarding header, alias, or `using` compatibility shim. The
    old path and all five old contract names return zero exact-token rows.
  - `PhysicsWorld.cpp` no longer includes `ReplayRetainedMemory.h`. The fixed
    `replay_solver_snapshot` owner name and measured 8 MiB hard cap moved with
    the Physics snapshot contract; Runtime's replay policy table consumes those
    Physics constants for reporting. This preserves one registered replay-only
    growth policy without making Physics depend on Runtime. The final exact
    `Runtime/Replay` proof across `SkullbonezSource/Physics` returns zero rows.
  - Validation: an initial focused Profile build exposed only missing fully
    qualified Physics names in global-scope replay TUs; the corrected build
    passed in 16.8s with zero warnings/errors. Final
    `tools\validate_fast.bat` passed in 1m21.5s after exact formatter/header
    pipeline corrections, and the changed project-filter validator independently
    reported 718/718 items with zero errors. `tools\validate_physics.bat`
    passed in 53.4s, including the 44,401-line byte-exact regression CSV.
    `tools\validate_replay_visual_fidelity.bat` passed in 7m24.2s with exactly
    one engine process, one prediction start/generation, 2,401 ticks, 200 moved
    and 175 toppled bricks, one presented cascade, and all false-pass controls.
    Final `tools\validate_full.bat` passed in 2m24.2s with zero warnings/errors,
    all CPU/coverage/runtime lanes, zero DX12 validation errors, three passing
    image baselines, and byte-exact physics. No tracked behavioral artifact or
    golden changed.
- [x] L3 — Move `SimulationSystem.{h,cpp}` to `Runtime/`; fix the
  Rendering→`Runtime/WindowConstants.h` edges (move the constants to `Core/`
  or pass the values at the three DX12 call sites plus `Rendering/Text.cpp`;
  record which). Proof: `grep -rn '\.\./Runtime' SkullbonezSource/Rendering
  SkullbonezSource/Physics` returns zero rows (allocation/capacity edges now
  point at Core). Validation: `tools\validate_full.bat` plus
  `tools\validate_dx12_renderer.bat` + `tools\run_graphics_stress.bat 1`
  (DX12 TUs touched).
  - `SimulationSystem.{h,cpp}` moved from Physics to Runtime without a
    forwarding header or namespace alias. Production/test project ownership,
    filters, includes, tests, references, and manual-source links now use the
    Runtime path; the stepping implementation is unchanged apart from its
    same-directory Runtime include and the explicit Physics timestep include.
  - Choice recorded: `WindowConstants.h` moved to `Core/` because Assets,
    World, Rendering, Startup, Scene, and Window all consume the same stable
    process strings. Passing the data through every render/asset entry point
    would spread composition plumbing without creating a more specific owner.
    The constants and their values are unchanged.
  - The required zero-edge proof exposed DX12's remaining include of Runtime's
    process-wide Tracy marker owner. The complete `TracyClientOwner.{h,cpp}`
    moved to `Core/` and `SkullbonezCore::Core::DevelopmentTools`; Runtime,
    Core, Rendering, tests, projects, and the filter validator consume that
    owner directly. This preserves marker/lifecycle behavior and introduces no
    bridge, callback pack, forwarding wrapper, or compatibility alias.
  - Final proofs: `rg -n '\.\./Runtime' SkullbonezSource/Rendering
    SkullbonezSource/Physics` returns zero rows; exact scans for the old
    `Physics/SimulationSystem`, `Runtime/WindowConstants`, and
    `Runtime/DevelopmentTools/TracyClientOwner` live paths also return zero.
    The rename-aware diff identifies five real moves at 67%-98% similarity.
  - Comment-quality audit: all 41 touched source-bearing files inspected,
    41 checked, zero deferred/unchecked, and zero missing learning-header
    fields. The existing ImGui/Tracy comment checklist was reconciled to the
    relocated Core paths.
  - Validation: the first focused Profile build found only an unqualified
    sibling namespace in `Init.cpp`; the corrected retry passed in 12.7s with
    zero warnings/errors. The first fast gate found two now-empty test virtual
    filters; after removing them, final `tools\validate_fast.bat` passed in
    71.8s with production filters 718/718 and test filters 97/97.
    `tools\validate_replay_visual_fidelity.bat` passed once in 444.4s with
    exactly one engine process, 2,401 ticks, 200 moved/175 toppled bricks, and
    all controls. `tools\validate_full.bat` passed in 163.1s with all CPU,
    coverage, runtime, DX12, and byte-exact physics lanes. The explicit
    `tools\validate_dx12_renderer.bat` passed in 54.0s with zero InfoQueue
    errors and all three baselines; `tools\run_graphics_stress.bat 1` ran PID
    7296 for 61.5s and ended only at its PID-scoped timeout. No tracked
    behavioral artifact, baseline, golden, shader output, or scene changed.
- [ ] L4 — Core inversion pass: remove `Core/SkullScope.cpp`'s five physics
  includes (SkullScope moves to `Physics/Diagnostics/` or consumes a
  physics-supplied view; record the choice), remove `Core/Profiler.cpp`'s
  `Rendering/Text.h`/`IRenderDiagnostics.h` includes via a caller-supplied
  sink or relocation, and remove `Core/WorkerPool.h`'s `Assets/AssetKeys.h`
  include. Any edge that cannot be cheaply inverted is recorded as an
  explicit exception with owner, reason, and deletion condition. Validation:
  `tools\validate_full.bat`.
- [ ] L5 — Enforcement and closure: add the direction rule and the exact
  grep proofs to `AGENTS.md`; rerun all proofs from L0-L4 at final source;
  independent rubber-duck review of the whole plan (single end-of-plan
  review); record the surviving-exception table. Validation: final
  `tools\validate_full.bat` at closure tip.

## Acceptance

- All grep proofs pass at the closure tip, or every surviving edge is in the
  recorded exception table with owner, reason, and deletion condition.
- Zero behavioral artifact changed: physics CSV byte-exact, DX12 baselines
  unchanged, replay fidelity gate passes without golden refresh.
- `AGENTS.md` carries the direction rule and updated mapping rows.
- Independent review is clear; findings reopen the owning task.

## Validation Summary

Per-task gates as listed. Closure requires `validate_full` at final source
plus the L1 allocation-checker self-test/repo run and the L2 replay fidelity
gate evidence retained in the closure report.
