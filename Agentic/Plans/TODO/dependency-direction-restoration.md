# Dependency Direction Restoration

Status: Active — 2/6 tasks (L0-L1 complete; L2-L5 pending)
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
- [ ] L2 — Move `Runtime/Replay/ReplaySolverSnapshot.h` to
  `Physics/PhysicsSolverSnapshot.h` with namespace/type rename per binding
  decision 4; investigate and resolve the `PhysicsWorld.cpp` include of
  `ReplayRetainedMemory.h` (either it moves with the reserve-allocator
  relocation in L1 or the include is deleted; record which). Proof:
  `grep -rn "Runtime/Replay" SkullbonezSource/Physics` returns zero rows.
  Validation: `tools\validate_physics.bat` (byte-exact),
  `tools\validate_replay_visual_fidelity.bat` (Replay-facing edit; one engine
  process, one generation, zero golden refresh per MASTER rule 11), then
  `tools\validate_full.bat`.
- [ ] L3 — Move `SimulationSystem.{h,cpp}` to `Runtime/`; fix the
  Rendering→`Runtime/WindowConstants.h` edges (move the constants to `Core/`
  or pass the values at the three DX12 call sites plus `Rendering/Text.cpp`;
  record which). Proof: `grep -rn '\.\./Runtime' SkullbonezSource/Rendering
  SkullbonezSource/Physics` returns zero rows (allocation/capacity edges now
  point at Core). Validation: `tools\validate_full.bat` plus
  `tools\validate_dx12_renderer.bat` + `tools\run_graphics_stress.bat 1`
  (DX12 TUs touched).
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
