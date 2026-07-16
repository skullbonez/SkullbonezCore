# PhysicsWorld Stage-Owner Decomposition Closure

Date: 2026-07-15

Branch: `15th-of-July-Night-Runner`

Plan: `physicsworld-stage-owner-decomposition` P0-P10 (11/11)

## Outcome

The campaign is complete. `PhysicsWorld` is the stable public facade,
composition root, and deterministic fixed-step sequencer. Seven concrete stage
owners retain broadphase, forces/integration, narrowphase, terrain, persistent
contacts, sleep, and diagnostics authority. No stage stores a `PhysicsWorld`
pointer/reference, concrete sibling owner, callback pack, `void*` authority
escape, or mutable multi-domain context.

The final fixed-step order remains sleep mirror/underwater lock, force and
tornado, broadphase, narrowphase with ordered commit, terrain with ordered
commit, persistent contacts, point joints/support, remaining-time integration,
sleep-island transition, and caller-boundary diagnostics. The committed physics
baseline was never refreshed during P0-P10.

## Final Ownership Review

The mandatory independent rubber-duck review first reopened P7/P9 with three
credible findings:

1. `PhysicsWorldFacade.cpp` was a prohibited translation-unit split of the
   facade rather than an ownership transfer.
2. Narrowphase/sleep contexts held concrete sibling owners.
3. `PhysicsWorld::RunSolverPhysics` still interpreted sleep thresholds.

Remediation deleted `PhysicsWorldFacade.cpp` and reunited all facade methods in
`PhysicsWorld.cpp`; replaced concrete sibling access with opaque, owner-minted
`PhysicsNarrowphaseWakeAccess` and `PhysicsContactCacheWakeAccess` capabilities;
moved sleep policy into `PhysicsSleepController::ResolveStepPolicy`; and moved
remaining-time integration dispatch into the already-approved
`PhysicsForceStage` lane. The repeat review found **zero credible blocking
ownership findings**. A final follow-up also confirmed that
`PhysicsSleepController.State.cpp` is only a cohesive physical partition of the
same sleep owner, not a second owner or forwarding facade.

Two non-blocking API-breadth residuals were recorded: `PhysicsWorld.cpp` is
1,162 lines, above the approximate 1,000-line target, and a few mutable bounded
buffer views remain for synchronous stage/replay transfer. The independent
review found the remaining world methods cohesive facade/composition work and
the buffer views bounded, synchronous, documented borrows with no retained
owner escape.

Final implementation-unit counts are recorded in
`Agentic/Reports/2026-07-15/physicsworld-ownership-map.md`; every stage unit is
below 700 lines. The formatted sleep units are 585 (`.cpp`), 196 (`.State.cpp`),
and 466 (`.Wake.cpp`).

## Validation Evidence

All final gates ran from the remediated, formatted source. Commands ran through
the available shell and mirrored complete output under `TestOutput/`.

| Command | Time | Result |
|---|---:|---|
| `tools\validate_build.bat Debug` | 23.86 s | PASS; 0 warnings, 0 errors after opaque wake-capability remediation. |
| `tools\validate_format.bat` | targeted | PASS; all source formatted and 245 headers aligned. |
| `tools\validate_project_filters.bat` | 1.75 s | PASS; 0 errors across 699 project and 699 filter items. |
| `tools\validate_full.bat` | 279.29 s | PASS; CPU umbrella, Profile/Automation/Debug zero-warning builds, replay/prediction smoke, DX12 suite with 0 InfoQueue errors and three passing screenshot comparisons, both physics smoke lanes, and 44,401-line varied baseline byte-exact. |
| `tools\validate_perf.bat` | 79.43 s | PASS; DX12 and physics-bench comparisons reported no regressions, allocation guard recorded 0 gameplay violations, Profile/Debug binaries ready. |
| `python tools\check_allocation_policy.py --self-test` | final scan | PASS. |
| `python tools\check_allocation_policy.py --repo .` | final scan | PASS; 355 files, 39 direct-heap findings, 139 dynamic-STL findings, 656 STL-growth findings, 0 allowlist errors. |

Final logs:

- `TestOutput/physicsworld_p10_validate_full.log`
- `TestOutput/physicsworld_p10_validate_perf.log`
- `TestOutput/physicsworld_p10_validate_format.log`
- `TestOutput/physicsworld_p10_validate_project_filters.log`
- `TestOutput/physicsworld_p10_opaque_wake_build.log`

## Performance Comparison

P0 certification versus P10 averages:

| Run | Marker | P0 avg ms | P10 avg ms | Change |
|---|---|---:|---:|---:|
| DX12 | `Frame/Physics` | 0.1731 | 0.1811 | +4.6% |
| DX12 | `Frame/Physics/Step` | 0.1730 | 0.1810 | +4.6% |
| Physics bench | `Frame/Physics` | 0.0613 | 0.0599 | -2.3% |
| Physics bench | `Frame/Physics/Step` | 0.0612 | 0.0598 | -2.3% |

The mandatory gate's own pinned comparisons also passed: DX12 physics averaged
0.1811 ms against 0.2248 ms and physics-bench physics averaged 0.0599 ms
against 0.0712 ms. No regression exceeded the gate's noise thresholds.

## Comment Quality Audit

The campaign-wide inventory was rebuilt from `git diff --name-only` against
pre-P0 tip `687734c69d4059b9fd02e44d3c532e680c8a2156`, plus untracked final-source
files. Audit result: **27/27 inspected, 0 deferred**.

The inventory comprises `PersistentContactSolver.cpp`, `PhysicsWorld.cpp/.h`,
`Ragdoll.cpp/.h`, `SleepIslandSystem.h`, all broadphase/contact/force/
narrowphase/sleep/context/diagnostics/terrain stage source and headers including
`PhysicsSleepController.State.cpp`, `TornadoGameplay.cpp/.h`, and
`tools/validate_project_filters.py`. Every file has the required learning
header; dense ownership, lifetime, ordering, capacity, worker-reduction,
same-pass wake, replay-transfer, and validation-sensitive behavior has nearby
`Concept:`, `Why:`, `Invariant:`, `Lifetime:`, or `Hazard:` guidance where the
comment style guide calls for it.

## Deletion Proof

- `PhysicsWorldFacade.cpp` is absent from disk, project membership, filters, and
  allocation allowlist.
- Grep across the logical module finds no `PhysicsWorld*`/`PhysicsWorld&` in a
  stage and no concrete sibling-stage reference; the only stage references are
  private worker functors referring to their own stage.
- All eleven direct `PhysicsWorld` members are concrete owners or one of the
  four P0-approved stay-behind values.
- No committed physics, DX12 screenshot, or performance baseline changed.
- Independent whole-module review and all final closure gates are green.
