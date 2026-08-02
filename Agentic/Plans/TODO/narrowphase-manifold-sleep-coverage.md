# Narrowphase Manifold And Sleep Coverage

Date: 2026-08-02
Status: IN PROGRESS — 2/6 phases complete
Impact area: Physics narrowphase manifolds, sleep controller and island system, tests
Owner: Physics narrowphase and sleep
Priority: First

## Problem And Evidence

The contact solver is the best-tested code in the repository and the narrowphase
that feeds it is the worst-tested code that matters. That asymmetry is measured,
not asserted.

`SkullbonezSource/Physics/ObjectContactManifold.cpp` is 2,133 lines and owns five
shape-pair paths: sphere/sphere, sphere/box closest-point with dominant-face
selection, box/box SAT plus reference/incident face clipping, sphere/hull, and
hull/hull polygon SAT with edge-edge axes. `SkullbonezTests/TestObjectContactManifold.cpp`
is 295 lines containing 5 `TEST_CASE` blocks.

Of those five cases, four assert box behavior. The only coverage for any hull
pairing is `CheckContactPair` inside "Coverage floor contract: every object
manifold shape pair publishes contacts", which asserts that contacts are finite
and nonempty. No assertion anywhere pins normal direction, penetration depth,
contact-point count, or contact-point placement for sphere/hull, box/hull, or
hull/hull. A hull manifold that returns a plausible-but-wrong normal passes the
entire suite today.

By contrast `SkullbonezTests/TestPersistentContactSolver.cpp` is 1,420 lines with
20 cases, including a closed-system energy and momentum oracle, planted-impulse
negative controls that prove the oracle can fail, and seeded property invariants.
That depth does not protect narrowphase: the solver consumes whatever rows the
manifold hands it and conserves energy correctly against a wrong normal just as
happily as against a right one.

Feature-ID stability is the second gap and it is load-bearing. `EncodeBoxFaceFeature`,
`EdgeId`, and the hull feature encoders produce the identity that becomes the
warm-start cache key at `PersistentContactSolveTransaction::MakeKey`
(`PersistentContactSolver.cpp:187`). Warm starting is what makes resting contact
and stacking work at all. Nothing currently tests that a settled box produces the
same feature IDs on consecutive frames under small perturbation, or that the
reference/incident face choice does not oscillate near a 45-degree contact. A
silent feature-ID churn disables warm starting without failing a single test —
it degrades stack quality and looks like a solver tuning problem.

The sleep system is split across `Stages/PhysicsSleepController.cpp` (936 lines),
`.Wake.cpp` (657), `.State.cpp` (237), `.h` (299), and `SleepIslandSystem.cpp`.
Thirteen `TEST_CASE` blocks across the suite mention sleep, but they exercise it
end-to-end through `TestDeterminism.cpp` and the solver. There is no test file
owned by the sleep controller. The wake propagation paths — `WakeSleepVisualIsland`,
`WakePointJointIsland`, `WakeRestingContactIsland`, underwater sleep lock, and
`RebuildAwakeBodyIndices`/`AddAwakeBodyIndex`/`RemoveAwakeBodyIndex` — are
reachable only indirectly, and a missed wake is the classic physics bug that
reproduces once in a hundred runs.

## Goal

Narrowphase output and sleep state transitions carry the same assertion depth the
contact solver already has: exact geometric expectations for every shape pair,
pinned feature-ID lifetime, deterministic reduction, negative controls that prove
the new assertions can fail, and a sleep controller whose wake paths are each
reachable from a focused test.

## Non-Goals

- No change to manifold or sleep behavior. Any physics baseline movement during
  this plan is a defect to repair, not a golden to refresh.
- No new shape pairs, no new sleep policy, no solver change.
- No property-based fuzzing framework. Seeded deterministic property cases follow
  the existing `[seed 0x...]` convention already used by the solver tests.
- No test file named for a gate, a metric, or this plan. Files are named for the
  subsystem whose behavior they pin.
- No coverage-percentage target. Raising a floor is a consequence, not the goal.

## Ownership

- `SkullbonezTests/TestObjectContactManifold.cpp` remains the narrowphase manifold
  owner and grows; it does not spawn a second manifold test file per shape pair.
- A new `SkullbonezTests/TestSleepController.cpp` owns sleep controller state and
  wake propagation. `TestDeterminism.cpp` keeps its end-to-end sleep cases and
  does not absorb unit-level sleep coverage.
- Feature-ID lifetime is narrowphase-owned and belongs in the manifold test file,
  not in the solver test file, even though the solver is the consumer.
- Test fixtures construct manifolds and sleep state directly from Physics-owned
  stores. No fixture may require a window, renderer, scene load, or worker pool.

## Phases

- [x] **NM0 — Census the narrowphase and sleep contracts.** Enumerate every shape
  pair path in `ObjectContactManifold.cpp` and `TerrainContactManifold.cpp` with
  its entry condition, normal convention, penetration sign, maximum point count,
  feature-encoding function, and reduction policy. Enumerate every sleep state
  transition and every wake entry point in `PhysicsSleepController` and
  `SleepIslandSystem`, with its trigger, the state it mutates, and whether any
  current test reaches it. Record which of the existing 5 manifold cases and 13
  sleep-touching cases assert behavior versus assert non-emptiness. The census is
  the phase deliverable and names the exact untested transitions; do not begin
  writing tests from a reading impression.

  Complete 2026-08-02. The current-source matrix covers all nine ordered object
  pairs, three terrain paths, shared reduction/feature policy, five manifold
  cases, thirteen named sleep cases plus the omnibus capacity probe, every
  sleep/wake transition, and exact NM1/NM2/NM4 gaps. Evidence:
  `../../Reports/2026-08-02/narrowphase-manifold-sleep-coverage-nm0-census.md`.

- [x] **NM1 — Pin geometric correctness for every shape pair.** For each pair,
  add cases with hand-computed expected values: contact normal direction and
  magnitude, penetration depth, point count, and point placement relative to the
  known configuration. Cover face-face, face-edge, edge-edge, vertex-face, and
  deep-overlap configurations for box/box and hull/hull, and inside/surface/near
  configurations for sphere/box and sphere/hull. Include the mixed box/hull path,
  which currently has no geometric assertion at all. Every expectation is derived
  from the configuration, not captured from current output.

  Complete 2026-08-02. Eight focused cases / 251 assertions pin all ordered
  object-family geometry and the complete box/hull topology matrix. The
  hand-derived hull edge/edge oracle exposed a reverse-support SAT tie defect;
  edge eligibility now retains the opposing support edges consistent with the
  final A-to-B normal. Tests, direct coverage, and byte-exact Physics pass with
  no baseline refresh. Evidence:
  `../../Reports/2026-08-02/narrowphase-manifold-sleep-coverage-nm1-geometry.md`.

- [ ] **NM2 — Pin feature-ID lifetime and reduction determinism.** Prove that a
  resting box/box and hull/hull contact produces the same feature IDs across
  consecutive frames under sub-slop perturbation, that the reference/incident
  face choice does not oscillate across a 45-degree sweep, and that
  `SelectContactCandidateIndices` returns the same selection regardless of
  candidate insertion order. Prove the deepest-point-first rule and the
  `featureId` tie-break are both actually exercised by at least one case each.
  Prove that a feature ID which changes produces a warm-start cache miss, so the
  coupling between narrowphase identity and solver behavior is visible in a test
  rather than only in a stack that settles badly.

- [ ] **NM3 — Add negative controls.** Following the pattern already established
  by "Contact energy oracle: planted restitution impulse and stale-geometry
  controls fail", prove the new assertions can fail: plant an inverted normal, a
  sign-flipped penetration, a truncated point count, an unstable feature ID, and
  a reduction that selects neighboring rather than spread points, and require each
  new assertion to reject it. An assertion that cannot be made to fail is not
  coverage and does not close its NM1/NM2 item.

- [ ] **NM4 — Pin the sleep controller state machine and wake propagation.** Add
  `TestSleepController.cpp` reaching every wake entry point named in NM0 directly:
  visual island wake, point-joint island wake, resting-contact island wake,
  explicit wake, underwater sleep lock and release, and awake-list rebuild plus
  incremental add/remove. Prove island membership is symmetric where the support
  edge is symmetric, that a wake propagates exactly one hop where the policy says
  one hop, that a sleeping body cannot be left in the awake list or the reverse,
  and that support-edge capacity exhaustion fails loudly through
  `ValidateSleepSupportEdgeCount` rather than silently dropping edges.

- [ ] **NM5 — Validate and close.** Run `tools\validate_tests.bat`, then
  `tools\validate_physics.bat` and `tools\validate_physics_deep.bat` to prove no
  baseline moved. Run `tools\validate_coverage.bat` directly because these tests
  are intended to raise subsystem coverage. Audit every touched source-bearing
  file against `Agentic/Skills/comment-style-audit/skill.md`. Obtain an
  independent read-only review specifically answering whether any new case asserts
  only non-emptiness, whether any expectation was captured from current output
  rather than derived, and whether any NM4 wake path is still reachable only
  indirectly.

## Dependencies And Decisions

- This plan runs first in the new portfolio because it is pure test addition with
  zero behavioral risk, and because it strengthens the net that
  `broadphase-pair-dedup-cost.md` relies on to prove byte-exactness.
- No phase carries baseline-refresh authority. If a new geometric assertion fails
  against current behavior, that is a narrowphase defect to investigate and the
  finding is recorded before any expectation is adjusted. An expectation must
  never be relaxed to match observed output without a recorded reason.
- Expectations are derived from configuration. Capturing current manifold output
  as a golden would reproduce the exact false-pass problem this plan exists to
  remove.
- NM2 depends on NM0's feature-encoding inventory. NM3 depends on NM1 and NM2
  existing to plant against.
- If NM1 or NM4 finds a real defect, the repair belongs in this plan when it is
  small and local, and a separate plan when it changes manifold or sleep policy.
  Physics behavior changes require owner approval before any baseline moves.

## Acceptance

The plan closes when every shape pair has geometric assertions on normal,
penetration, and point count; feature-ID lifetime and reduction determinism are
pinned; every new assertion has a proven negative control; every sleep wake entry
point is reachable from a focused test; no physics baseline moved; mapped gates
pass; the touched-source comment audit is complete; and independent review finds
no assertion that is structurally incapable of failing.

## Validation

- `tools\validate_tests.bat`
- `tools\validate_physics.bat` — byte-exact CSV, no baseline movement
- `tools\validate_physics_deep.bat` — bullet sweep, known-issue, SkullScope goldens
- `tools\validate_coverage.bat` run directly
- `tools\validate_fast.bat`
- Touched-source comment audit
- Independent read-only review of assertion strength and negative-control validity

## Related

- `../../../SkullbonezSource/Physics/ObjectContactManifold.cpp`
- `../../../SkullbonezSource/Physics/Stages/PhysicsSleepController.h`
- `../../../SkullbonezSource/Physics/SleepIslandSystem.h`
- `../../../SkullbonezTests/TestObjectContactManifold.cpp`
- `../../../SkullbonezTests/TestPersistentContactSolver.cpp`
- `broadphase-pair-dedup-cost.md`
