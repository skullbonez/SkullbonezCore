# Downward Domain Bleed DB3 — Buoyancy-Owned Per-Body Facts

Date: 2026-07-26

Plan: `Agentic/Plans/TODO/downward-domain-bleed-remediation.md`

Result: Complete (DB3 of 6; portfolio ledger 5/19, 26%)

## Outcome

`PhysicsBodyRecord` no longer owns `volume`, `projectedSurfaceArea`,
`dragCoefficient`, `submergedVolumePercent`, or `contactEpsilon`.
`BuoyancySystem` now owns one fixed-capacity `BuoyancyBodyFacts` row per live
body. The row is statically fixed at five floats and the store is one
`PhysicsFixedList<BuoyancyBodyFacts, 8192>` with no heap or fallback growth
path.

`PhysicsEngine` is the topology transaction owner. Body registration appends
authored descriptors, body state, buoyancy facts, and collider state or rolls
the partial command back. Refresh, coordinated shape update, swap-last
destruction, replay trim, descriptor reload, and clear apply the same model-row
operation to the buoyancy store. Debug and fixed-step preflight checks require
body, collider, and buoyancy counts to match before a stage receives any row.

Force integration, terrain support, underwater sleep policy, CCD pose
integration, and wake paths borrow explicit spans or one row. They retain no
buoyancy owner pointer. Cold editor, UI, memory-diagnostic, and Automation
fingerprint reads use `PhysicsEngine::ReadBuoyancyFacts`; no Runtime caller
mutates the store.

## Field And Consumer Map

| Fact | Final owner | Fixed-step consumer |
|---|---|---|
| `volume` | `BuoyancyBodyFacts` | buoyancy-force magnitude |
| `projectedSurfaceArea` | `BuoyancyBodyFacts` | viscous drag |
| `dragCoefficient` | `BuoyancyBodyFacts` | linear/angular drag and righting torque |
| `submergedVolumePercent` | `BuoyancyBodyFacts` | targeted underwater sleep lock; reset after pose integration |
| `contactEpsilon` | `BuoyancyBodyFacts` | terrain support and terrain contact-body view |

Collider-owned shape/material facts remain in `ColliderStore`. The separate
collider `projectedSurfaceArea` and `dragCoefficient` fields were not part of
the DB3 ruling and remain collider-authoritative for collider/editor queries.

## Lifecycle And Layout Proof

- Append preflights the fixed capacity and stamps all five descriptor facts in
  the retired assignment order.
- Descriptor refresh and coordinated shape update reset the transient
  submersion value exactly where body-record refresh previously reset it.
- Middle-row destruction copies the final buoyancy row into the retired row
  before popping, matching body/collider swap-last compaction.
- Replay trim only shrinks the live prefix after body and buoyancy counts are
  preflighted.
- `Clear` destroys the live prefix without releasing or growing backing
  storage.
- `sizeof(BuoyancyBodyFacts) == sizeof(float) * 5` and
  `offsetof(PhysicsBodyRecord, rotationalInertia) == 16` are both compile-time
  and focused-test assertions.
- The focused lifecycle test covers append, refresh and transient reset,
  swap-last erase, trim, and clear.

The allocation checker cannot infer the concrete receiver type from member
call spelling. Its allowlist therefore records only the two syntactic
false-positives in `BuoyancySystem.cpp`: append constructs one inline
`PhysicsFixedList` row, and resize is preflighted to shrink only. The recorded
cap is 8,192 five-float rows (163,840 bytes). This is not a runtime allocation
exception or Replay growth privilege.

## Determinism And Replay Proof

`tools\validate_physics.bat` passed on the final physics source. The generated
CSV contained two complete 44,401-line runs. The regression checker proved the
runs byte-identical, canonicalized one complete run, and matched it
byte-for-byte against the committed baseline.

The move preserves the prior assignment order, per-stage read order, floating
point expressions, and reset sites. No solver snapshot, Replay artifact, or
known-signature schema contains any of the five facts, so no schema or
round-trip fixture changed.

`tools\validate_physics_deep.bat` was not required. The DB0 census and final
source scan show that none of the moved fields feeds SkullScope or a deep
known-signature baseline.

No baseline, golden, Replay artifact, screenshot, shader, scene, config, or
physics CSV reference was refreshed.

## Static Proofs

```text
PhysicsBodyRecord extraction
PASS: none of volume, projectedSurfaceArea, dragCoefficient,
      submergedVolumePercent, or contactEpsilon remains in the struct

rg -n '^#include[[:space:]]+.*(World|Scene|Assets|Gameplay|Runtime|UI)/' \
  SkullbonezSource/Physics
PASS: no rows

rg -n 'Geometry::Terrain' SkullbonezSource/Physics
PASS: no rows

rg -n '^#include[[:space:]]+.*Runtime/Replay/' \
  SkullbonezSource/Physics SkullbonezSource/Rendering \
  SkullbonezSource/Scene SkullbonezSource/World SkullbonezSource/Core
PASS: no rows

rg -n '\b(volume|projectedSurfaceArea|dragCoefficient|submergedVolumePercent|contactEpsilon)\b' \
  SkullbonezSource/Physics/Diagnostics
PASS: no rows
```

## Validation

| Gate | Result |
|---|---|
| Focused buoyancy lifecycle test | PASS inside the unit suite; compact layout, append, refresh/reset, swap-last erase, trim, and clear |
| `tools\validate_tests.bat` | PASS on final source; 393 cases / 2,403,315 assertions |
| `tools\validate_physics.bat` | PASS; lifecycle smoke, two byte-identical 44,401-line runs, committed-baseline match, ready builds |
| `tools\validate_perf.bat` | PASS; all perf/scale comparisons report no regression |
| Runtime allocation guard | PASS; zero gameplay allocations and zero reserve-policy violations |
| `python tools/check_allocation_policy.py --self-test` | PASS |
| `python tools/check_allocation_policy.py --repo .` | PASS; zero allowlist errors and no unapproved findings |
| `tools\validate_dependency_graph.bat` | PASS; 27 rules, 43 negative fixtures, one project fixture, zero repository findings |
| `python tools/check_related_paths.py --repo .` | PASS; 566 files scanned, 1,501 repository paths, zero findings |
| `git diff --check` | PASS |

The first unit-wrapper invocation reported one transient failure (392/393).
Without changing source or reference data, the same executable passed all
393 cases directly; both subsequent required wrapper invocations passed all
393 cases and 2,403,315 assertions.

The first performance invocation stopped at two allocation-checker spelling
findings for fixed-list append/shrink. The precise fixed-capacity syntactic
row described above resolved those false positives. The next invocation
reached the Automation build and exposed four stale fingerprint reads from
`PhysicsBodyRecord`; those reads now use the immutable buoyancy span. The final
performance invocation passed the complete gate.

## Registered Follow-Up Debt

DB3 added buoyancy spans to existing synchronous Physics stage contexts. It
introduced no new context type. The data-only contexts already registered by
`concrete-parameter-bag-elimination` remain owned by PB5/PB6 and do not acquire
persistent pointers, callbacks, virtual dispatch, or new state authority here.

## Comment Audit Checklist

The repository comment-audit skill inspected every source-bearing path in the
final DB3 diff. It verified learning headers, body-level ownership/lifetime
claims, dense-row invariants, fixed-capacity policy, determinism-sensitive
comments, and all repository-relative `Related:` paths.

- [x] `SkullbonezSource/Physics/BuoyancySystem.cpp`
- [x] `SkullbonezSource/Physics/BuoyancySystem.h`
- [x] `SkullbonezSource/Physics/PhysicsBodyStore.cpp`
- [x] `SkullbonezSource/Physics/PhysicsBodyStore.h`
- [x] `SkullbonezSource/Physics/PhysicsEngine.cpp`
- [x] `SkullbonezSource/Physics/PhysicsEngine.h`
- [x] `SkullbonezSource/Physics/PhysicsWorld.cpp`
- [x] `SkullbonezSource/Physics/PhysicsWorld.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsSleepController.Wake.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsSleepController.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsStageContexts.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.h`
- [x] `SkullbonezSource/Runtime/Automation/InteractionAutomationController.cpp`
- [x] `SkullbonezSource/Runtime/Diagnostics/SceneMemoryDiagnostics.cpp`
- [x] `SkullbonezSource/Runtime/Editor/EditorHistory.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneWorld.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneWorld.h`
- [x] `SkullbonezSource/Runtime/UI/OperatorEditorFrameComposer.cpp`
- [x] `SkullbonezTests/TestCoverageFloorContracts.cpp`
- [x] `SkullbonezTests/TestPhysicsHandles.cpp`
- [x] `SkullbonezTests/TestPhysicsStageState.cpp`
- [x] `SkullbonezTests/TestTerrain.cpp`

Checklist path: this report. Checked: 27. Deferred: 0. Unchecked: 0.
