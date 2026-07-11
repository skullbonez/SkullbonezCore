# Behavioral Test Depth

Date: 2026-07-10 (reconciled)
Status: Complete — 6/6 phases complete
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
- [x] **P3 — Parser and serializer boundaries.** Existing tests cover truncated
  JSON, missing camera, wrong member type, unknown asset, and malformed style.
  Remaining: use the production parse → scene/entity+physics creation → owner
  mutation → save → parse → recreate path for a mixed-shape
  `assetInstances[]` fixture. Compare stable object/asset/instance/part identity,
  asset and behavior roots, ordered topology, composed transforms, physics
  descriptors, and every durable render material field by object id. Inspect
  saved JSON for `assetLibraries[]`, `assetInstances[]`, and per-part live state.
  Acceptance: no parser-only or object-set-only test that ignores runtime
  creation, identity, authored properties, or save schema.
  Evidence: `119b359c` and `7fdd91d3` added the schema-v2 mixed-shape no-`Run`
  owner recreation, complete durable state comparisons, and a waited production
  building-asset save/reload probe.
- [x] **P4 — Replay snapshot round-trip.** Snapshot capture/restore reproduces a
  future solver sample and nonzero solver hash without launching the full engine.
- [x] **P5 — Injected-bug drill.** Locally break one solver clamp, one manifold
  reduction rule, one parser guard, and one replay restore field. Record the
  exact failing test/assertion for each, then revert the defects. This is
  evidence that tests can fail for the intended reason, not merely pass.
  Manifold evidence complete: the pre-fix first-four truncation failed
  `Object contact manifold: reduced tilted face starts with deepest retained
  point` at all three deterministic poses (for example, retained depth
  `0.472089` versus expected deepest depth `0.833631`). Parser evidence is also
  complete: the orphan standalone suite exposed that a missing material `mode`
  recorded Lane R failure and then dereferenced the null member; `Material
  authoring rejects malformed options` failed by access violation before the
  early-return repair and passed afterward. Removing the friction-cone clamp
  failed at `4.04061 <= 0.1001`; dropping restored angular velocity produced
  solver hash `4558989638039294353` versus `8448418270499344807`. Both defects
  were restored before final validation.
- [x] **P6 — Sustaining and gate integration.** Complete
  `validation-gate-integrity.md` V1/V2/V5 so doctest, interaction-policy,
  scene-parser, and DX12-architecture targets run from the CPU umbrella and
  `validate_full`. Update `AGENTS.md` so a new standalone test target must join
  the umbrella in the same commit. Acceptance: deliberately fail one test in
  each executable and prove `validate_full` stops before runtime launch.
  Evidence: all four executable mutations reached their named test and stopped
  the umbrella. The DX12 drill exposed and fixed signed fatal exits escaping the
  wrapper and fatal tests aborting the parent. See
  `Agentic/Reports/behavioral_test_depth_closure_20260711.md`.

## Dependencies

- P2 can proceed independently but any engine-source seam change requires the
  physics gate.
- P3 coordinates with `runtime-shell-decomposition.md` D2 and
  scene extraction C1-C3 plus `physics-authority-and-identity.md` C0-C5. Binding
  design: `Agentic/Reports/scene_asset_roundtrip_design_20260710.md`.
- P4/R5 work coordinates with
  `replay-architecture-and-right-sizing.md`.
- P6 depends on `validation-gate-integrity.md`.

## Acceptance

- [x] Solver, manifold, parser/serializer, and replay restore each have named
  behavioral tests.
- [x] P5 records all four expected failures.
- [x] Every first-party test executable runs from the CPU umbrella.
- [x] `validate_full` cannot pass with a broken CPU test.
- [x] End-to-end physics/DX12 baselines remain determinism/visual evidence, not
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

- 2026-07-11: P5/P6 closure is recorded in
  `Agentic/Reports/behavioral_test_depth_closure_20260711.md`.
  `tools\validate_dx12_arch_tests.bat` passed 50 cases in 26.1s and final
  `tools\validate_full.bat` passed in 96.5s with zero warnings, DX12 InfoQueue
  errors = 0, matching captures, and byte-exact varied physics.

- 2026-07-11: promoted the authored 37-body, 1,200-frame
  `physics_bench_varied.scene.json` workload to the normal gate's 44,401-row
  byte-exact CSV contract. The CSV records named body pose, quaternion, linear
  and angular velocity, grounded state, sleeping state, and sleep inhibition.
  If one process emits repeated complete CSV passes, validation requires every
  pass to be byte-identical and commits only one canonical pass.
  The prior seeded 20-body solver workload remains exact-signature coverage in
  the deep gate rather than disappearing with its committed full CSV. Fast,
  physics, and deep-physics gates passed from the final artifacts; the deep
  gate matched the varied CSV, legacy solver signature, and SkullScope packet.
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
- 2026-07-10: the new CPU umbrella passed all four first-party targets in
  27.796s (78/78 doctest cases, 1,883 assertions, interaction Debug/Release,
  scene parser, and DX12 architecture). V1/V2/V5 are complete; P6 remains open
  for the executable-level mutation drill.
- 2026-07-10: `tools\validate_tests.bat` passed 75/75 doctest cases and
  1,669/1,669 assertions after parser Lane R tests.
- Log: `Agentic/Reports/validate_tests_plan04_parser_result_20260710.log`.
