# Vector Frame Contract Closure - VF3 VectorReflect

Date: 2026-08-01
Branch: `nightrunner-1st-AUG-26`
Impact area: Maths vector semantics and unit tests
Phase: VF3 complete

## Outcome

`VectorReflect` now matches its established name and normalized-surface-normal
contract. The formula is the conventional plane reflection:

`incident - normal * (2 * dot(normal, incident))`

The previous formula returned the negative of that result. It preserved the
normal component and reversed the tangent, which was reflection about the
normal axis rather than across the surface plane described by the comment.

The CodeGraph and source census still finds zero first-party production
callers. The only first-party caller is the focused unit test, so this semantic
repair cannot reach production Physics, Replay, rendering, or scene artifacts.

## Focused Oracle

The Profile Vector3 selection passes 1 case / 7 assertions:

- oblique incidence `(3, -4, 5)` against the +Y normal produces `(3, 4, 5)`,
  retaining both tangent components and reversing the normal projection; and
- normal incidence `(0, -4, 0)` produces `(0, 4, 0)`, rejecting the prior
  normal-axis-mirror result.

The complete unit suite passes 460 cases / 2,423,070 assertions.

## Artifact Result

VF0 predicted zero committed movement because the helper has no production
caller. VF3 confirms that prediction: core Physics and every deep Physics
artifact compare exactly with the committed files. No baseline, golden, or
signature was regenerated. Physics does **not** need a new baseline.

## Comment Audit

Checklist path: this report.

- [x] `SkullbonezSource/Maths/Vector3.h`
- [x] `SkullbonezTests/TestVector3.cpp`

Checked: 2. Deferred: 0. Unchecked: none.

The implementation comment now names plane reflection and its tangent/normal
invariant. The test learning header states the same contract, and the oblique
plus normal-incidence expectations agree with the name and formula. Both
learning headers remain ownership-bearing and all `Related:` paths resolve.

## Validation

- Focused Profile Vector3 oracle: 1/1 case, 7/7 assertions, pass.
- Unit suite: 460/460 cases and 2,423,070/2,423,070 assertions, pass.
- `validate_physics`: pass with exact committed comparisons.
- `validate_physics_deep`: pass with every mapped artifact exact.
- Final `validate_fast`: all nine stages pass in 360.8 seconds.
- Independent read-only review: accepted with no blockers, non-blockers, or
  missing evidence.

The first fast attempt failed closed on the required header-format pipeline.
After formatting, the second reached strict symbol provenance and rejected
Automation objects whose timestamps predated the formatter's byte-identical
rewrites. Rebuilding Automation refreshed that provenance; the final complete
rerun passed and is the phase acceptance command.

No baseline refresh or owner decision is required for VF3.
