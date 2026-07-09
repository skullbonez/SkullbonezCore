# Behavioral Test Depth

Date: 2026-07-09
Status: Proposed — 0%. **Prerequisite for engine-cleanup plan 03 steps
1.2/2.1** (the governance-apparatus deletion names "code review plus real
behavioral tests" as its replacement enforcement — these are those tests).
Impact area: SkullbonezTests, physics solver, scene parser, replay
Promoted from `engine-cleanup-plans/15-review-gaps.md` item 15.1.

## Problem

2,796 test lines against 145,023 lines of engine (~1.9%). The suite covers
math primitives, store/handle basics, a determinism harness, and a few
boundary fixtures (built by the completed audit plan 05 and fable-01 —
harness, vendored doctest, `SKULLBONEZ_TESTS.vcxproj`, and
`tools\validate_tests.bat` all exist). There is **no focused unit coverage**
for `PersistentContactSolver` stages, `ObjectContactManifold` reduction,
`TestSceneParser` error paths, or replay snapshot round-trips.

Regression safety instead leans on two end-to-end oracles — the byte-exact
physics CSV and DX12 screenshot diffs. Both are valuable but neither can
localize a failure, and both invalidate wholesale on any intentional behavior
change, which trains routine baseline refreshes (the exact hazard the Danger
Zones table warns about).

## Goal

A deliberately injected bug in solver clamping, manifold reduction, parser
error handling, or replay restore is caught by a named unit test in the
console runner — not only by a byte-diff against a golden file. The
end-to-end oracles remain the determinism/visual gates; they stop being the
*only* net.

## Phases

Each phase is one commit-sized slice; gate is `tools\validate_tests.bat`
unless noted. Tests live in `SkullbonezTests/` alongside the existing files
and follow the runtime static-allocation rules only where they compile engine
sources in.

- [ ] **P1 Solver stages.** Deterministic fixtures for: warm-start reuse
  (second solve of an identical manifold converges faster / starts from
  cached impulses), friction-cone clamp (tangential impulse never exceeds
  μ·normal), restitution bounce (post-solve separating velocity vs
  coefficient), and sleep thresholds (body below linear+angular thresholds
  for N ticks sleeps; impulse wakes it).
- [ ] **P2 Manifold reduction.** Contact reduction keeps the deepest point;
  a 4-point box-on-box stack manifold stays stable (same points, stable ids)
  across 2 steps; degenerate/coplanar inputs do not produce NaN or empty
  manifolds.
- [ ] **P3 Parser error paths.** Malformed scene JSON (truncated file, wrong
  type, missing required key, unknown asset name) produces recoverable Lane R
  failures with messages — never a fatal; a scene using `assetInstances[]`
  round-trips load → save → load with identical object sets.
- [ ] **P4 Replay snapshot round-trip.** Snapshot save → restore → solver
  hash equality at a fixed frame, hosted in the test runner without launching
  the full exe (the replay-owned `PhysicsEngine` from the prediction
  isolation work is the natural host).
- [ ] **P5 Injected-bug drill.** Locally break one solver clamp and one
  parser guard; confirm the new tests fail and name the defect, then revert.
  Record the drill result here — this is the acceptance evidence plan 03
  cites when it deletes the linter.
- [ ] **P6 Sustaining rule.** Keep AGENTS.md's existing
  regression-test-with-bugfix rule; every future slice in the physics,
  parser, or replay TODO plans that changes behavior in a covered area
  extends the covering test in the same commit. No new checker — this is a
  review expectation.

## Sequencing

- P1–P4 have no dependencies and can start immediately.
- Engine-cleanup plan 03 step 2.1 (strip the regex gates from AGENTS.md and
  point enforcement at review + these tests) should land only after at least
  P1 and P4 exist; step 1.2 (delete the linter) may proceed in parallel with
  the later phases but the P5 drill is the honest sign-off.
- P4 pairs naturally with `TODO/replay-prediction-and-memory.md` A-phase
  work; coordinate so the snapshot API is hosted once.

## Acceptance

- [ ] Solver, manifold, parser, and replay-restore behavior each have named
  tests runnable via `tools\validate_tests.bat`.
- [ ] The P5 injected-bug drill is recorded with both failures caught by unit
  tests.
- [ ] Plan 03's AGENTS.md rewrite cites this plan as the enforcement
  replacement.

## Validation map

| Slice | Gate |
|-------|------|
| Test-only additions | `tools\validate_tests.bat` |
| Any engine-source change made to enable testability | per the file-to-validation map (`validate_physics` for solver/manifold hosts, `validate_full` for parser/replay hosts) |
