# Math Fatal Removal — Try-APIs Plus Debug Assert In Vector3

Date: 2026-07-15
Status: Active — 0/4 tasks complete
Impact area: `Maths/Vector3` contract, every caller of `Normalise` and vector
division, physics determinism baselines, unit tests
Owner: maths

## Problem And Evidence

The math library terminates the process on degenerate input, by policy:

1. `Vector3::Normalise()` calls `SB_FATAL` on zero magnitude
   (`SkullbonezSource/Maths/Vector3.cpp:66`).
2. `operator/=( float )`, `operator/=( const Vector3& )`, `operator/( float )`
   and `operator/( const Vector3& )` fatal on zero divisors/components
   (declared with that contract at `SkullbonezSource/Maths/Vector3.h:65-72`).

Physics engines routinely produce near-zero vectors from stacked, jittering,
or interpenetrating bodies; a Release process kill at the bottom of the
numerics stack is the wrong lane. Lane-F fatal-invariant treatment
(`AGENTS.md` error-handling policy) is for owned engine state corruption, not
for numeric edge cases reachable from ordinary simulation input.

Adversarial review 2026-07-15, finding #5. Owner decision: Try-APIs plus a
Debug-only assert — the math library never fatals in Release, and callers that
can legitimately see degenerate input get an explicit reporting API.

## Goal

`SB_FATAL` no longer appears anywhere in `Maths/`. Plain `Normalise()` and the
divide operators keep a Debug-only assert as the misuse tripwire; new
`TryNormalise`-style APIs return success explicitly; every call site that can
receive degenerate input is migrated to a Try-API with an explicit,
deterministic fallback.

## Non-Goals

- No behavior change for well-formed input: results must remain bit-identical,
  so the physics baselines must pass with zero refresh.
- No exceptions, no `SbResult` in vector hot paths (a bool out-parameter API
  is the whole surface).
- No changes to `Matrix4`/`Quaternion`/`RotationMatrix` fatals in this plan;
  if the sweep finds equivalents there, record them as a follow-up row rather
  than growing this plan.

## Tasks

- [ ] T1 — API change in `Vector3.h`: add
      `bool TryNormalise();` (in-place, returns false and leaves the vector
      unmodified on zero magnitude) and
      `bool TryNormalised( Vector3& out ) const;` plus divide equivalents as
      needed by the call-site survey. Demote the fatals in `Normalise` and the
      divide operators to a Debug-only assert (`assert` or the engine debug
      macro — not `SB_FATAL`), documented as: Release computes IEEE results
      (inf/NaN propagate) and the deterministic gates are the backstop.
      Comment the lane decision (`Why:` P-adjacent caller contract, not lane
      F) per the error-handling policy table.
- [ ] T2 — Call-site survey and migration. `rg` every `Normalise(` and
      vector-divide use; classify each as (a) invariant-safe (input provably
      non-degenerate — leave on the plain API), or (b) reachable-degenerate
      (contact normals, impulse directions, user/editor input, near-zero
      relative velocities — migrate to Try-API with an explicit fallback that
      preserves current numeric behavior where the old path could not have
      fataled). The survey table (file, call, classification, fallback) goes
      in this plan before the migration commit.
- [ ] T3 — Unit tests: extend `SkullbonezTests/TestVector3.cpp` with
      zero-vector `TryNormalise` (returns false, value untouched), epsilon
      boundary behavior, and divide edge cases. Regression rule from
      `AGENTS.md` (bug-fix subsystems with existing coverage) applies.
- [ ] T4 — Final gates: `tools\validate_tests.bat` for the new coverage, then
      `tools\validate_physics.bat` byte-exact with no baseline refresh (proof
      that no migrated call site changed well-formed-input results), then
      grep-proof `SB_FATAL` is absent from `SkullbonezSource/Maths/`.

## Dependencies And Decisions

- Owner decision 2026-07-15: option "Try-APIs + debug assert" chosen over
  fallback-value-only APIs and over assert-only demotion.
- Depends on `vector3-inline-hot-math.md` landing first (same function bodies;
  avoid conflicting double edits).

## Acceptance

- Zero `SB_FATAL` in `Maths/`; Release builds cannot terminate from vector
  math.
- Survey table complete; every reachable-degenerate site uses a Try-API with a
  recorded fallback.
- `validate_physics` passes byte-exact with no refresh; new unit tests pass.

## Validation

- `tools\validate_tests.bat`, then `tools\validate_physics.bat`, output pasted
  at closure with an explicit no-baseline-change statement.
