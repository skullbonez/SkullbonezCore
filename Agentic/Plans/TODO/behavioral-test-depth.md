# Behavioral Test Depth

Date: 2026-07-10 (reconciled)
Status: In progress — 3/6 phases complete; P3 and P5 partially complete
Impact area: SkullbonezTests, physics solver/manifold, scene parser/serializer,
replay restore
Owner: subsystem behavior tests

## Problem

The current tracked test surface is about 5,823 lines against 172,036 tracked
engine/shader source lines (approximately 3.4%). Line ratio is only a warning,
not an acceptance metric: the material problem is that several high-risk
behaviors still depend primarily on byte-exact physics CSVs and DX12 screenshots.
Those end-to-end oracles are valuable, but they do not localize failures and are
easy to invalidate wholesale during intentional behavior changes.

Current named gaps are manifold reduction, scene `assetInstances[]`
serialization round-trip, injected-bug evidence, and automatic execution of all
first-party test targets from the broad gate.

## Goal

A defect in solver clamping, manifold reduction, parser error handling, scene
round-trip, or replay restore is caught by a named CPU test before an end-to-end
baseline changes. Every first-party test target runs from one mandatory CPU
umbrella and from the broad PR gate.

## Phases

- [x] **P1 — Solver stages.** Deterministic tests cover warm-start reuse,
  friction-cone clamp, restitution bounce, and sleep/wake thresholds.
  Evidence: `validate_tests` passed 72/72 cases and 1,643 assertions on
  2026-07-09.
- [x] **P2 — Manifold reduction.** Direct fixtures prove deepest-point
  retention, stable four-point box manifolds across two steps, stable feature
  ids, and finite non-empty output for degenerate/coplanar inputs. Acceptance:
  an intentionally broken reduction rule fails the named test.
- [ ] **P3 — Parser and serializer boundaries.** Existing tests cover truncated
  JSON, missing camera, wrong member type, unknown asset, and malformed style.
  Remaining: load → save → load a scene using `assetInstances[]` and compare
  authored object identity, grouping, transforms, asset names, physics
  descriptors, and render material intent. Acceptance: no object-set-only test
  that ignores identity or authored properties.
- [x] **P4 — Replay snapshot round-trip.** Snapshot capture/restore reproduces a
  future solver sample and nonzero solver hash without launching the full engine.
- [ ] **P5 — Injected-bug drill.** Locally break one solver clamp, one manifold
  reduction rule, one parser guard, and one replay restore field. Record the
  exact failing test/assertion for each, then revert the defects. This is
  evidence that tests can fail for the intended reason, not merely pass.
  Manifold evidence complete: the pre-fix first-four truncation failed
  `Object contact manifold: reduced tilted face starts with deepest retained
  point` at all three deterministic poses (for example, retained depth
  `0.472089` versus expected deepest depth `0.833631`). Solver, parser, and
  replay injected-failure evidence remain.
- [ ] **P6 — Sustaining and gate integration.** Complete
  `validation-gate-integrity.md` V1/V2/V5 so doctest, interaction-policy,
  scene-parser, and DX12-architecture targets run from the CPU umbrella and
  `validate_full`. Update `AGENTS.md` so a new standalone test target must join
  the umbrella in the same commit. Acceptance: deliberately fail one test in
  each executable and prove `validate_full` stops before runtime launch.

## Dependencies

- P2 can proceed independently but any engine-source seam change requires the
  physics gate.
- P3 coordinates with `runtime-shell-decomposition.md` D2 and
  `physics-authority-and-identity.md` C2.
- P4/R5 work coordinates with
  `replay-architecture-and-right-sizing.md`.
- P6 depends on `validation-gate-integrity.md`.

## Acceptance

- [ ] Solver, manifold, parser/serializer, and replay restore each have named
  behavioral tests.
- [ ] P5 records all four expected failures.
- [ ] Every first-party test executable runs from the CPU umbrella.
- [ ] `validate_full` cannot pass with a broken CPU test.
- [ ] End-to-end physics/DX12 baselines remain determinism/visual evidence, not
  the only regression net.

## Validation

| Slice | Gate |
|---|---|
| Test-only additions | current owning test script; CPU umbrella after V1 |
| Solver/manifold seam | CPU tests + `tools\validate_physics.bat` |
| Parser/serializer seam | parser tests + CPU umbrella + `tools\validate_full.bat` |
| Replay seam | CPU tests + `tools\validate_replay_scrub.bat` |
| Gate integration | `tools\validate_fast.bat`, then changed umbrella/full script |

## Latest Evidence

- 2026-07-10: added direct box-manifold fixtures for stable four-row feature
  identity, tilted-face deepest-point retention, exact coplanar contact, and a
  zero-height slab. The deepest-point test failed against the original
  first-four truncation at three assertions, then passed after box and hull face
  clipping shared one deepest-first/spread-maximizing fixed-capacity reducer.
- 2026-07-10: `tools\validate_tests.bat` passed 78/78 doctest cases and
  1,883/1,883 assertions after the manifold correction and final feature-id
  checks (Profile build: zero warnings and zero errors).
- 2026-07-10: `tools\validate_physics.bat` passed the standalone physics smoke
  and matched all 20,001 core solver CSV lines byte-for-byte; Debug and Profile
  builds completed with zero warnings and zero errors.
- 2026-07-10: `tools\validate_tests.bat` passed 75/75 doctest cases and
  1,669/1,669 assertions after parser Lane R tests.
- Log: `Agentic/Reports/validate_tests_plan04_parser_result_20260710.log`.
