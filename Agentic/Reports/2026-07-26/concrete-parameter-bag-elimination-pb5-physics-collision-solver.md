# Concrete Parameter-Bag Elimination PB5 Physics Collision And Solver

Date: 2026-07-26
Implementation base: `275c0915`
Branch: `nightrunner-25th-JUL-26`

## Result

PB5 passes. Collision candidate collection, object narrowphase, and persistent
contact solving now cross concrete stage-owner APIs without authority-free
context bags. The following retired symbols have zero definitions/usages:

- `PersistentContactSolverContext`;
- `PhysicsContactSolverStageContext`;
- `ObjectNarrowphasePairStageContext`;
- `PhysicsBroadphaseStageContext`;
- `BroadphaseCandidateFilterContext`.

The repository-wide threshold-13 inventory remains empty.

## Concrete Physics Boundaries

`PhysicsContactSolverStage` is the sole owner of persistent contacts, the
warm-start cache, solver rows, statistics, and bounded consequence storage.
Its 12-parameter `Solve` operation consumes concrete stores, a normalized
`PersistentContactSolverStepPolicy`, candidate/sleep/terrain rows,
`PhysicsStepDiagnostics`, the step duration, and profiler. The former
stateless solver object and both solver contexts are gone.

`PhysicsNarrowphaseStage` consumes concrete stores and a focused
`ObjectNarrowphaseStepPolicy`. `PhysicsNarrowphaseWakeAccess` gained only
read-only sleep and underwater-lock queries in addition to its existing
synchronous wake capability. The private `ObjectNarrowphaseIslandStage`
remains a stack-only concrete WorkerPool callable. Serial events are committed
immediately; parallel events retain one original-pair slot and are committed
in ascending pair order after island completion.

`PhysicsBroadphaseStage::Run` now consumes stores, settings, joints, sleep and
awake rows, diagnostics, step tolerances, and profiler directly. `SpatialGrid`
provides distinct unfiltered and filtered entrypoints. The shared private
implementation receives direct arguments and an optional Debug-only
sleep-pruned output vector; it has no nullable filter, context, owner,
predicate callback, or type erasure.

## Review Repairs

Focused solver tests initially exposed NaN values after fixture migration.
Debug intentionally poisons default-constructed `Vector3` values; the migrated
`ColliderStore` fixtures had used those defaults for velocities and local box
offsets. Explicit zero vectors restore the original test state. The
edge-versus-face support-direction assertions remain unchanged.

The introduced candidate `emplace_back` was reviewed manually. Admission
fails fatally before either the fixed grid ceiling or caller-reserved capacity
can be exceeded, so emission cannot grow storage or silently drop a pair.

## Comment Audit

Touched-source inventory: 19 files checked, 19 compliant, 0 deferred.

The audit verified or corrected:

- contact-stage ownership and synchronous borrow lifetimes;
- solver-policy normalization and bounded consequence publication;
- canonical broadphase order and candidate-capacity failure;
- filtered/unfiltered SpatialGrid responsibilities;
- serial and parallel narrowphase event ordering;
- wake-access query and mutation boundaries;
- Debug fixture initialization requirements.

Every touched source-bearing file has the required learning header and local
concept, reason, invariant, lifetime, or hazard comments where needed.

## Static Proofs

The five retired-symbol scans return no rows.
`tools/inventory_wide_signatures.py --threshold 13 --format json` returns
`[]`. Dependency validation passes all 27 rules and 46 fixtures, and the
downward-Replay include proof returns no rows.

The allocation-policy checker reports 0 allowlist errors after scanning 459
files. Introduced-line review found no `PhysicsStepContext`, replacement
service/context/bindings bag, inheritance, interface, virtual dispatch,
callback pack, type erasure, or unbounded runtime allocation path.

## Validation

- Profile and Debug warning-as-error builds: PASS, zero warnings/errors;
- focused persistent-contact solver tests: PASS, 5 cases / 44 assertions;
- focused narrowphase-island tests: PASS, 1 case / 1,028 assertions;
- focused solver-broadphase tests: PASS, 4 cases / 10 assertions;
- focused SpatialGrid tests: PASS, 15 cases / 8,507 assertions;
- `tools\validate_physics.bat`: PASS in 58.0 seconds with the 44,401-line
  regression oracle byte-exact;
- `tools\validate_perf.bat`: PASS in 87.6 seconds with zero guarded
  steady-gameplay allocations, no DX12 regression, and no physics-bench
  regression;
- `tools\validate_full.bat`: PASS in 337.4 seconds:
  - formatting and `Related:` paths;
  - dependency graph;
  - Profile/Automation/Debug builds;
  - mandatory CPU and coverage chain;
  - Automation, Replay, and prediction runtime lanes;
  - DX12 validation with zero errors and no baseline refresh;
  - 44,401-line physics regression byte-exact.
