# Narrowphase Manifold And Sleep Coverage - NM3 Negative Controls

Date: 2026-08-02
Branch: `nightrunner-2nd-AUG-26`
Baseline: `d26163eddc2c42ce1dcdd6d37f6a63ee4d926416`
Plan progress: 4/6
Portfolio progress: 4/26 (15%)

## Outcome

Every NM1 geometry family and NM2 identity/reduction contract now has an
explicit planted-failure path. One focused case changes exactly one contract at
a time and proves the same predicate used by the positive fixture rejects the
bad output. The controls cover inverted normal, sign-flipped penetration,
truncated point count, unstable feature identity, and a reducer that retains a
neighbor instead of the required tangent spread.

No production file, manifold behavior, solver policy, baseline, golden,
capacity, asset, or configuration changed.

## Shared Oracle Design

The manifold test helpers now separate pure pass/fail predicates from doctest
reporting wrappers:

- `VectorNear` owns the three-component normal and point tolerance;
- `PointSetMatches` owns exact row count plus unordered one-to-one point matching;
- `UniformPenetrationMatches` owns nonempty, per-row signed depth;
- `FeatureIdsEqual` owns nonempty ordered row identity across frames; and
- `SelectionMatchesFeatureIds` owns four-row count, bounded indices, and ordered
  reduction identity.

Positive NM1/NM2 fixtures and planted NM3 controls call these same predicates.
The authored unit-box face patch and six-candidate spread fixture are also
shared values, so the controls cannot pass by substituting a looser expected
shape or a second reduction oracle.

## Planted-Control Matrix

| Plant | Isolated mutation | Rejected contract |
|---|---|---|
| Inverted normal | Negate the derived +X unit-box face normal and preserve all rows. | `VectorNear` rejects direction while the unmodified manifold passes. |
| Sign-flipped penetration | Multiply every derived 0.2 row depth by -1 and preserve normal/count/points. | `UniformPenetrationMatches` rejects the sign and magnitude. |
| Truncated patch | Decrement the four-row face patch to three without changing its stored points. | `PointSetMatches` rejects the exact authored row count before point matching. |
| Unstable feature | Flip one bit in the second sub-slop frame's first feature id and preserve pose/count/order. | `FeatureIdsEqual` rejects temporal identity churn. |
| Neighboring reduction | Keep deepest feature 100 and first spread feature 20, then select nearby feature 10 before opposite/orthogonal coverage. | `SelectionMatchesFeatureIds` rejects `{100, 20, 10, 30}` against the production spread `{100, 20, 30, 40}`. |

The focused negative-control case passes 13/13 assertions. The complete object-
manifold family passes 17/17 cases and 1,447/1,447 assertions after the shared-
predicate refactor.

## Touched-Source Comment Audit

Audit skill: `Agentic/Skills/comment-style-audit/skill.md`

| File | Result | Evidence |
|---|---|---|
| `SkullbonezTests/TestObjectContactManifold.cpp` | Pass | The learning header owns positive/negative predicate coupling; the planted geometry block documents one-fault isolation, and the neighboring reducer plant explains the exact failure mode it represents. |

Checked: 1/1. Deferred: 0.

## Validation

| Command | Result |
|---|---|
| Focused Profile build and `Profile\\SKULLBONEZ_TESTS.exe --test-case="Object contact manifold oracles:*"` | Pass: 1/1 case, 13/13 assertions. |
| Complete Profile object-manifold family | Pass: 17/17 cases, 1,447/1,447 assertions. |
| `tools\\validate_tests.bat` | Final-source pass in 45.9 seconds; 129/129 project/filter items and the complete Profile harness passed. |
| `tools\\validate_coverage.bat` | Final-source pass in 64.8 seconds; Physics stages/solver remains 4,966/5,760 lines (86.22%) and every subsystem floor passes. |
| `tools\\validate_format.bat` | Pass in 45.1 seconds; 587 source files, 327 headers, and every repository-relative `Related:` path are clean. |
| `git diff --check` | Pass. |

NM3 is an ordinary incremental slice, so no rubber-duck review is appropriate.
The mandatory independent plan-level review remains owned by NM5.
