# Concrete Parameter-Bag Elimination PB6 Physics Sleep Force And Terrain

Date: 2026-07-26
Implementation base: `80fe897b`
Branch: `nightrunner-25th-JUL-26`

## Result

PB6 passes. Sleep, wake, external-force, terrain, force-application, and final
integration stages now consume concrete owners and focused values. The
following retired symbols have zero definitions/usages:

- `PhysicsSleepIslandStageContext`;
- `PhysicsSleepWakeContext`;
- `ExternalForceBodyContext`;
- `TerrainCandidateCommitContext`;
- `TerrainDetectionStageContext`;
- `ApplyForcesStageContext`;
- `IntegrateRemainingStageContext`.

`PhysicsStageContexts.h` is deleted along with its project/filter entries. The
repository-wide threshold-13 inventory remains empty.

## Concrete Physics Boundaries

`ExternalForceStage::ApplyBodyForces` accepts the published input, concrete
body/collider stores, the existing scoped narrowphase wake capability, and
execution/WorkerPool owners. Marker identity remains stage-owned, and
sleep/underwater queries stay behind the sleep-owner capability.

`PhysicsForceStage::ApplyForces` is a 12-parameter direct operation;
`IntegrateRemaining` is a 10-parameter direct operation. Both derive hot store
views once and traverse the sleep owner's ascending awake list through the
same serial or no-allocation WorkerPool partition.

`PhysicsTerrainStage` uses an 11-parameter detection operation, a 9-parameter
candidate preparation operation, and a three-parameter commit of the existing
`PreparedTerrainCandidateCommit` value plus the two sleep result spans.
Workers still write one body-indexed candidate slot, while candidate
preparation, diagnostics, and commit remain serial in model order.

`PhysicsSleepController` now exposes direct island-stage inputs and separate
wake operations. Explicit zero-dt wake clears owned state/cache without
applying forces. Collider-aware explicit wake refreshes underwater lock state.
Same-step wake preserves state -> clock -> force -> cache -> sorted-awake
publication order. No behavior flag or nullable owner pointer selects among
those paths.

## Comment Audit

Touched-source inventory: 14 surviving source-bearing files checked, 14
compliant, 0 deferred. The deleted `PhysicsStageContexts.h` is recorded
separately.

The audit verified or corrected:

- wake-state, cache, clock, force, and awake-list ordering;
- sleep-island owner state and synchronous borrow lifetimes;
- terrain worker-slot identity and serial commit order;
- force/integration awake-row order and WorkerPool partitioning;
- external-force field/body order and wake authority;
- fixed-capacity scratch and no-allocation requirements.

Every surviving touched source-bearing file has the required learning header
and local concept, reason, invariant, lifetime, or hazard comments where
needed.

## Static Proofs

The seven retired-symbol scans and the deleted-header scan return no rows.
`tools/inventory_wide_signatures.py --threshold 13 --format json` returns
`[]`. Project/filter validation passes with 782 production and 782 filter
items. Dependency validation passes all 27 rules and 46 fixtures, and the
downward-Replay include proof returns no rows.

The allocation-policy checker reports 0 allowlist errors after scanning 458
files. Introduced-line review found no master Physics context, replacement
service/context/bindings bag, inheritance, interface, virtual dispatch,
callback pack, type erasure, owner reach-back, or unbounded runtime
allocation path.

## Validation

- Profile and Debug warning-as-error test-project builds: PASS, zero
  warnings/errors;
- focused sleep, terrain, external-force determinism, and mutual-gravity
  tests: PASS, 19 cases / 20,383 assertions;
- `tools\validate_physics.bat`: PASS in 79.7 seconds with the 44,401-line
  regression oracle byte-exact;
- `tools\validate_perf.bat`: PASS in 90.5 seconds with zero guarded
  steady-gameplay allocations, no DX12 regression, and no physics-bench
  regression;
- `tools\validate_full.bat`: PASS in 228.4 seconds:
  - formatting and `Related:` paths;
  - 782/782 project filters;
  - dependency graph;
  - Profile/Automation/Debug builds;
  - mandatory CPU and coverage chain;
  - Automation, Replay, and prediction runtime lanes;
  - DX12 validation with zero errors and no baseline refresh;
  - 44,401-line physics regression byte-exact.
