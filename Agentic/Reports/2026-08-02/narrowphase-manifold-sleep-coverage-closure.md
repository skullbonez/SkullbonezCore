# Narrowphase Manifold And Sleep Coverage Closure

Date: 2026-08-02
Result: NM0-NM5 complete; remaining live portfolio 0/20 (0%)

## Closed Contracts

- The NM0 census records every ordered object and terrain manifold path,
  feature encoding and reduction rule, current assertion depth, sleep state
  transition, and wake entry point before any new expectation is authored.
- Eight NM1 geometry cases derive normal, penetration, row count, and point
  placement from authored sphere, box, and brick-hull poses. The edge/edge hull
  oracle exposed and repaired a reverse-support SAT tie that selected back-side
  edges despite otherwise plausible normal and depth output.
- NM2 pins sub-slop box and hull feature lifetime, the single incident-face
  transition over a 41-pose sweep, all deepest/tie/spread/invalid reducer rules
  across 720 insertion permutations, and a real persistent-cache miss when only
  narrowphase feature identity changes.
- NM3 reuses the positive predicates to reject isolated inverted-normal,
  sign-flipped-depth, truncated-patch, one-bit feature-churn, and neighboring-
  reducer plants. No planted control substitutes a second oracle.
- NM4 directly exercises visual fan-out including explicit zero id, complete
  point-joint and transitive resting-contact components, disconnected/reversed
  controls, exactly-once same-step force, all underwater wake states, sorted
  awake remove/add/rebuild, symmetric fixed-point support, reserved-capacity
  fatal, and the existing 32,768-edge semantic ceiling.

No Physics, SkullScope, Replay, visual, or other tracked baseline was refreshed
or replaced.

## Focused Evidence

| Phase | Evidence |
|---|---|
| NM1 | 8 focused cases / 251 assertions across every ordered object family and box/hull topology |
| NM2 | 4 focused cases / 1,716 assertions, including all 720 reducer permutations and actual cache hit/miss behavior |
| NM3 | 1 planted-control case / 13 assertions; complete object-manifold family 17 cases / 1,447 assertions |
| NM4 | 7 focused cases / 183 assertions; runtime fatal probe directly distinguishes two-row reservation exhaustion from the semantic ceiling |
| Coverage | Physics stages and solver 5,050 / 5,760 lines, 87.67%; whole instrumented product 23,506 / 30,151 lines, 77.96% |

## Governance And Comment Audit

All seven mandatory current-source inventories passed after Automation, Debug,
and Profile objects were refreshed:

- build configuration: no blocker or dropped-inheritance finding;
- compiled reachability: 79/79 rows carry current exact rulings, and the new
  reducer seam is production-rooted rather than test-only;
- invariant aggregates: 1,207 candidates, 88 gated, 88 ruled, zero unruled;
- extraction scars: the sole unrelated `WorkerPool` alias remains exactly
  ruled, zero unruled;
- wide signatures: every operation at or above the 12-parameter review trigger
  has a current owner ruling;
- function complexity: 6,336 functions, 40 triggered, 40 ruled; and
- glossary terms: 993 definitions, 993 unique terms, zero multi-file drift or
  ruling issue.

The touched-source comment audit inspected 5/5 source-bearing files with zero
deferrals:

1. `SkullbonezSource/Physics/ObjectContactManifold.h`
2. `SkullbonezSource/Physics/ObjectContactManifold.cpp`
3. `SkullbonezTests/TestObjectContactManifold.cpp`
4. `SkullbonezTests/TestSleepController.cpp`
5. `SkullbonezTests/TestRuntimeContracts.cpp`

Each file has an ownership/flow-bearing summary, resolved permanent `Related:`
paths, and local invariant/lifetime/hazard comments where the new seam or test
fixture would otherwise hide capacity, ordering, or failure semantics. This was
a touched-file audit, so no subsystem checklist was required.

## Independent Review

Fresh read-only agent `/root/nm5_rubber_duck` reviewed the complete baseline-to-
HEAD diff plus production implementations, tests, project metadata, and exact
rulings using the global and repository rubber-duck skills. Prompt size was
2,430 characters; the review body was 5,208 characters. Token count and total
elapsed time were unavailable and are not estimated.

Verdict: **ACCEPT**, with zero blocking and zero non-blocking findings. The
review confirmed that no new case is smoke/non-emptiness-only, no expected
value is captured from current output, every NM3 plant is independently
sensitive, every named NM4 path is directly exercised, and the synchronous
allocation-free candidate reducer is cohesively Physics-owned and used by both
production face reducers.

## Final Validation

| Gate | Result | Elapsed |
|---|---|---:|
| `tools\validate_tests.bat` | PASS; 130/130 project/filter items and complete unit harness | 46.3 s |
| `tools\validate_physics.bat` | PASS; byte-exact determinism baseline | 25.2 s |
| `tools\validate_physics_deep.bat` | PASS; deep Physics/known-issue/SkullScope evidence unchanged | 111.7 s |
| `tools\validate_coverage.bat` | PASS; all ten subsystem floors | 58.4 s |
| `tools\validate_build.bat Automation` | PASS; refreshed the third reachability object root | 24.3 s |
| strict compiled reachability retry | PASS; all 79 rows ruled | 158.0 s |
| `tools\validate_fast.bat` final rerun | PASS; all nine stages | 369.1 s |

The first fast attempt reached its final reachability stage after 314.2 seconds
and failed closed because 20 Automation objects predated current source. A
targeted Automation build refreshed only derived build output; strict
reachability then passed, and the complete fast gate passed from the top. No
rule, ruling, source expectation, or baseline was weakened to resolve that
evidence failure.
