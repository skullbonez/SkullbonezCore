# Contact Solve Phase Ownership

Date: 2026-07-29
Owner: skullbonez
State: Not started
Ledger tasks: 5 (CS0-CS4)
Branch: TBD (register at start)
PR: TBD

## Goal

Give `PhysicsContactSolverStage::Solve` an invariant-owning phase type so the
engine's most determinism-sensitive operation is reviewable, and do it without
moving a single physics bit.

`Solve` is not a god object — it is one cohesive operation. The defect is that
its thirteen ordered passes, twenty-eight closures, and shared mutable solver-body
state are all expressed inside one 1,721-line lexical body, so the call order
that correctness depends on is enforced only by the sequence of statements. That
is precisely the case the Invariant Ownership Rule says must be enforced by a
type with a phase cursor.

## Problem And Evidence

Measured on 2026-07-29 against `main` tip `90e4d52f`.

- `SkullbonezSource/Physics/PersistentContactSolver.cpp:121-1841` —
  `PhysicsContactSolverStage::Solve`, 1,721 lines, 28 closure definitions,
  maximum brace depth 7.
- The body already carries thirteen named passes delimited by profile scopes:
  `BodySetup` (300), `BuildManifolds` (803), `Terrain/Rows` (975),
  `Precompute` (1105), `SolveRows` (1362), `PointSupportInstability` (1456),
  `Terrain/RestPolicy` (1465), `WriteBack` (1559), `DebugContacts` (1591),
  `PositionCorrection` (1636), `CacheStore` (1722), `FixedContactRelease`
  (1770), plus the entry policy/setup region.
- Source comments already name the ordering as "Second pass" (1097),
  "Third pass" (1353), "Fourth pass" (1631), "Final pass" (1718). The phases
  are known; only the type expressing them is missing.

Secondary target, same defect class, far lower stakes:
`ParseAction` at `SkullbonezSource/Runtime/Automation/InteractionAutomationController.cpp:1362`,
~1,096 lines and 164 branch keywords in one body.

## Design Constraints

- **Byte-exact is the oracle, not a nice-to-have.** Under `/fp:precise` with
  `#pragma fp_contract(off)` a pure extraction preserves evaluation order and
  therefore preserves bits. If any physics baseline byte moves, the extraction
  changed arithmetic — revert the task and re-extract. Never refresh a baseline
  to accommodate this plan. This plan has no bounded-divergence allowance.
- **A phase type, not a context bag.** The transaction owns the phase cursor,
  the solver-body working set, and impulse application. That is real authority:
  a caller cannot apply an impulse or advance a pass without it, and an
  out-of-order phase call is lane-F fatal. Its header states that exact
  phase-order invariant, and a focused test walks the legal order and proves
  each illegal transition fatal.
- **Banned outcomes.** A `*Context`/`*Services`/`*Operands` bag; a slice set
  where one operation still receives every slice; free `Apply*` functions taking
  a wide participant list; `Solve` reduced to a forwarder while the same
  authority stays reachable through sibling translation units; member-prefixed
  locals rebinding parameters so a lifted body needs no internal edits.
- **The literature comments move with their code.** The Catto section/equation
  citations and the `ENGINE-SPECIFIC:` divergence markers are the highest-value
  comments in the repository. Each must land beside the code it explains, not be
  collected into a header preamble.

## Non-Goals

- Changing solver behavior, iteration count, warm-start policy, or contact
  reduction.
- Parallelizing the PGS loop. Graph-colored solver parallelism is separate work
  with its own evidence gate.
- Splitting `PersistentContactSolver.cpp` merely to reduce file length. File
  count is not the measure; the God-Object Closure Rule judges the logical
  surface.

## Ledger

- [ ] CS0 — Census. Record every closure in `Solve` with its captures, every
  piece of state that crosses a pass boundary, and the exact read/write set per
  pass. Classify each closure as pass-local, shared-arithmetic (`applyImpulse`,
  `applyInvInertia`, `makeKey`, `hasCachedImpulse`, `conservativeContactRadius`),
  or side-effect publication. Name the phase boundaries and the authority the
  transaction must own. Produce a baseline byte-exact physics artifact from the
  final Debug executable to compare every later task against.
- [ ] CS1 — Install the phase owner. Add the non-copyable transaction holding the
  phase cursor, solver-body working set, and impulse application, with lane-F
  fatal on illegal transitions and its stated invariant in the header. `Solve`
  still contains the pass bodies; only the shared arithmetic and cursor move.
  Byte-exact required.
- [ ] CS2 — Move the row-construction phases (`BodySetup`, `BuildManifolds`,
  `Terrain/Rows`, `Precompute`) behind phase methods, carrying their Catto and
  engine-specific comments with them. Byte-exact required.
- [ ] CS3 — Move the solve and post-solve phases (`SolveRows`,
  `PointSupportInstability`, `Terrain/RestPolicy`, `WriteBack`,
  `PositionCorrection`, `CacheStore`, `FixedContactRelease`, `DebugContacts`)
  behind phase methods. Byte-exact required.
- [ ] CS4 — Closure. Decompose `ParseAction` into per-action-kind parsing with a
  table-driven dispatch, rerun all three existing ownership inventories plus the
  new complexity inventory, clear the `repair-plan` rulings this plan owns, pass
  one independent ownership review answering all five required review questions,
  and run every mapped gate.

## Dependencies

- `function-complexity-review-trigger` CX1 must have seeded the `repair-plan`
  rulings that CS4 clears. CS0-CS3 may run before that plan closes.

## Acceptance

- `Solve` and every extracted phase are below the ratified complexity trigger,
  or carry a `retain-owner` ruling with a concrete cohesion reason.
- The transaction's header names the exact phase order it enforces, and a
  focused test proves the legal walk succeeds and every illegal transition is
  fatal.
- Physics output is byte-exact against the CS0 baseline at every task boundary.
  Zero baselines, goldens, or artifacts are refreshed by this plan.
- `validate_perf` shows no regression in the `Frame/Physics/Narrowphase/PersistentContacts`
  inclusive marker or its children.
- All thirteen profile scope names survive with unchanged strings so historical
  traces remain comparable.
- Independent review records zero authority-free aggregates, zero whole-surface
  slice operations, zero extraction scars, zero renamed-shape reappearances, and
  zero false header invariants.

## Validation

- Iteration: focused Profile build, `TestPersistentContactSolver`,
  `TestObjectContactManifold`, `TestDeterminism`.
- CS1-CS3, each: `tools\validate_physics.bat` — byte-exact CSV diff is the pass
  condition — plus `tools\validate_perf.bat`.
- CS4: `tools\validate_all_cpu_tests.bat`, `tools\validate_physics_deep.bat`,
  `tools\validate_perf.bat`, and `tools\validate_full.bat`.
  `SkullbonezSource/Runtime/Automation/*` also requires `validate_full` under
  the `Runtime/*` mapping row.

## Comment-Audit Checklist

- [ ] `SkullbonezSource/Physics/PersistentContactSolver.h`
- [ ] `SkullbonezSource/Physics/PersistentContactSolver.cpp`
- [ ] `SkullbonezSource/Physics/ContactSolverCommon.h`
- [ ] `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h`
- [ ] `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.cpp`
- [ ] `SkullbonezSource/Runtime/Automation/InteractionAutomationController.cpp`
- [ ] `SkullbonezTests/TestPersistentContactSolver.cpp`

Reconcile against `git diff --name-only` at CS4 and add any newly touched file.
