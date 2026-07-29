# Persistent Contact Convergence Early-Out CE1

Date: 2026-07-29

Branch: `nightrunner-30th-JUL-26`

Starting source tip: `8346f7dc`

Plan: `Agentic/Plans/TODO/persistent-contact-convergence-early-out.md`

## Outcome

CE1 identifies honest row-level non-convergence as the cause of the dense
wall's 12-iteration saturation. The settled wall is predominantly
normal-row-limited, while some frames retain a tangent-dominant maximum row.
The existing total squared impulse-delta metric is not stale, and the wall is
not held at the cap only because hundreds of individually quiet rows are summed
together.

The solver now publishes a diagnostics-only, fixed-capacity convergence trace.
It retains the first 64 iteration summaries, counts later iterations as dropped,
does not allocate, does not enter Replay v2 capture/restore, and does not feed
simulation or the early-out decision. Each sample records:

- the exact historical `iterImpulseSq` stopping value;
- separate normal and tangent squared-delta sums;
- normal- and tangent-changed row counts; and
- the maximum contributing row's total, normal, and tangent squared deltas,
  bodies, feature, and terrain/object identity.

SkullScope writes one `solver_iteration_summary` row per retained iteration.
`tools/physics_query.py` schema 7 imports those rows into
`solver_iteration_summaries`; the bounded `solver` query exposes aggregate and
worst-row summaries.

## Current-Source Wall Measurement

Command:

```bat
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step --shadows off --hide-top-text --automation-hidden-window --frames 1200 --scene SkullbonezData/scenes/prediction_ragdoll_wall_200.scene.json --physics-diag Debug/ce1_wall_components.physicsdiag.ndjson
```

Ignored artifacts:

| Artifact | Bytes |
|---|---:|
| `Debug/ce1_wall_components.physicsdiag.ndjson` | 528,424,386 |
| `Debug/ce1_wall_components.physicsdiag.sqlite` | 285,409,280 |

The NDJSON SHA-256 is
`C67699FF3F74A036CC03291B9B2D2AA7EC9FF5990C88B3C87D82CB1EF75B52A3`.

Frames 200-1199 preserve the CE0 final-row census:

- 1,000/1,000 frames use 12 iterations;
- 565,635 final contact rows;
- 522,354 cache hits and 40,569 misses, a 92.793153% hit rate;
- 540,626 warm-started rows, a 95.578598% warm-start rate; and
- zero dropped convergence iterations across 12,000 retained summaries.

Across settled-window frames 800-1199, no iteration in any frame falls below
`1.0e-6`. Iteration 12 averages:

| Metric | Value |
|---|---:|
| total stopping delta squared | 10.152900114 |
| normal delta squared | 9.130227718 |
| tangent delta squared | 1.022672791 |
| maximum total | 336.908234 |
| frames with terrain-dominant maximum row | 168 / 400 |
| frames with normal-dominant maximum row | 348 / 400 |
| frames with tangent-dominant maximum row | 52 / 400 |
| average maximum-row total | 2.439524975 |
| average maximum-row normal | 2.172612372 |
| average maximum-row tangent | 0.266912623 |

The final frame provides the direct row-level proof:

| Iteration | Total | Normal | Tangent | Maximum row | Max-row normal | Max-row tangent | Maximum family |
|---:|---:|---:|---:|---:|---:|---:|---|
| 1 | 905.243408 | 735.730713 | 169.513107 | 644.005859 | 582.002625 | 62.0032501 | terrain |
| 6 | 1.27643657 | 0.811765969 | 0.464670569 | 0.670891047 | 0.409067780 | 0.261823237 | object |
| 12 | 0.00662608864 | 0.00373604265 | 0.00289004459 | 0.00128794229 | 0.0000195642 | 0.001268378 | terrain |

At iteration 12 both the total and the largest single row remain above
`1.0e-6`. Changing from a sum to a maximum-row comparison would therefore not
make this final wall frame legitimately converge within the current cap. The
final frame's maximum row is tangent-dominant, so it is not used as evidence of
a universal normal-row cause. Across all 400 settled frames, every iteration-12
maximum row exceeds `1.0e-6`, and 348 are normal-dominant.

## Controlled Cause Oracle

`Persistent contact solver: object support chain exposes honest normal-row
non-convergence` builds one fixed box and three aligned dynamic boxes with only
adjacent object pairs. It proves:

- all 12 configured summaries are retained and no iteration is dropped;
- the total residual decreases but reaches the configured cap;
- the final total and maximum single row both exceed `1.0e-6`;
- the maximum row's own normal component exceeds `1.0e-6` while its tangent
  component remains below `1.0e-6`;
- the final normal sum equals the stopping metric within float tolerance;
- tangent work remains below `1.0e-6` and normal work is over 1,000 times
  larger; and
- the maximum row is object/object with valid body identities.

The focused case passes 16/16 assertions. A second 9-assertion fixture proves
the 64-row cap, explicit overflow count, and Replay restore exclusion.

This controlled result independently reproduces the wall diagnosis without
terrain, restitution, rolling policy, broadphase, or a chaotic end state.

## CE2 Owner Decision

CE1 does not justify an automatic behavior change. The current criterion is
reporting real row-level non-convergence, and the plan forbids increasing the
iteration count, weakening terrain behavior, or introducing a hidden tolerance
budget. A normalized, root-mean-square, or larger-threshold early-out would
change solver behavior by declaring active rows converged.

Recommended decision: retain the current stopping criterion and accept that
dense active walls can consume the configured 12 iterations. If the owner wants
a behavior-changing CE2, that decision must name the intended convergence
contract and authorize its deterministic baseline consequences. Until then CE2
is blocked by the plan's explicit decision gate. No simulation CSV or state
baseline is refreshed. The schema-7 diagnostic-query golden is mechanically
regenerated from the unchanged simulation behavior and checked by a second
byte-exact query-regression run.

## Query Accounting

Every model-facing query was bounded and returned one JSON line:

| Query | UTF-8 bytes |
|---|---:|
| `solver --frames 200:1199 --limit 12` | 9,357 |
| settled-window per-iteration SQL, `--limit 20` | 3,184 |
| frame-1199 iteration SQL, `--limit 20` | 1,603 |
| settled iteration-12 attribution SQL, `--limit 20` | 600 |
| **Total** | **14,744** |

No query was truncated. The raw NDJSON and SQLite artifacts were not ingested
into the model.

The schema-7 query golden was regenerated once, then the complete generator and
16-query packet were rerun without update. The second run matched byte-exactly.
Relative to `HEAD`, only the `solver` result changes: it adds
`convergenceStats` and `convergenceWorst`, removes no key, and leaves the other
15 query results unchanged. The resulting 110,967-byte golden has SHA-256
`F538EF166B71C584F669716F20AB8F98DC99B2F160F1FF830017F25FD72FCD78`.

## Runtime Isolation

`PhysicsWorld` enables collection only while its active diagnostics sink is
emitting step diagnostics. Private prediction worlds have no sink, and normal
Profile/Automation runs therefore select the clean solver specialization.
`SolveRows` dispatches once per solve; the false specialization has no
`PersistentContactIterationDiagnostics` member, construction, arithmetic, or
row branch. The true specialization remains available to the direct Profile
tests, so the formal cause oracle is not compiled away or vacuous.

The final performance gate passes its allocation guard, selected-ball live-path
check, DX12 budgets/regression comparison, and Physics benchmark
budgets/regression comparison.

## Validation

Completed on the final source before the owner-decision boundary:

- Profile focused object-chain oracle: 1 case / 16 assertions, PASS.
- Profile focused cap/replay-exclusion oracle: 1 case / 9 assertions, PASS.
- `tools/validate_format.bat`: PASS; 573 implementations, 319 headers, and
  1,541 repository-relative `Related:` paths.
- All four ownership inventories: PASS; aggregates 85/85, extraction scars
  1/1, every 12-or-more signature currently ruled, and function complexity
  40/40 with no new ruling.
- `tools/validate_tests.bat`: PASS; 465/465 cases and 2,423,885 assertions.
- `tools/validate_physics.bat`: PASS; the 44,401-line varied-scene baseline is
  byte-exact across two current-source runs.
- `tools/validate_physics_deep.bat`: PASS.
- `tools/validate_perf.bat`: PASS.
- `tools/validate_full.bat`: PASS.
- SkullScope query regression: exact PASS after the final solver isolation
  refactor.
- Deterministic repeat: all three 1,200-frame wall NDJSON artifacts are
  528,424,386 bytes and share SHA-256
  `C67699FF3F74A036CC03291B9B2D2AA7EC9FF5990C88B3C87D82CB1EF75B52A3`.
- Touched-source comment audit: 9/9 files inspected, zero deferred.
- Independent ownership/correctness review: zero blocking findings after the
  clean/attributed specialization correction.

The separate Replay prediction-determinism probe reports
`predictionTrajectorySubmissionStable=false` both at the clean starting tip
`8346f7dc` and on this source. That pre-existing probe failure is recorded as
blocked evidence rather than attributed to CE1; all mapped gates above pass.

CE2 remains blocked because this plan requires an explicit owner decision
before any behavior-changing convergence correction or baseline transition.
