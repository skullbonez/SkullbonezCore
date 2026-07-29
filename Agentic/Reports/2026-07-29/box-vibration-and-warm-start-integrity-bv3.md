# Box Vibration And Warm-Start Integrity BV3

Date: 2026-07-29
Branch: `nightrunner-29th-JUL-26`
Starting tip: `12507cf6`
Scope: BV3 only — retire the terrain warm-start seed
Status: Complete; owner ratified, BV4 remains next

## Outcome

BV3 removes fabricated terrain support load without replacing it:

- `TERRAIN_RESTING_SUPPORT_SEED_SCALE` and
  `TERRAIN_SHORELINE_SUPPORT_SEED_SCALE` are deleted.
- Terrain row construction no longer derives an impulse from
  `mass * gravityMagnitude * abs(normal.y) * dt / pointCount`.
- Terrain friction uses the row's solved `accN`; no friction bound takes
  `max(accN, terrainWarmStart)`.
- Every touching inelastic terrain row may reuse and store a solved cache
  impulse. Sleep eligibility remains a separate resting-policy decision.
- Terrain rows increment the per-body persistent-contact count, so a body with
  several supports reports the actual row surface.
- The unsupported shallow-row zero-mass bypass is gone. Those rows now solve
  and cache through the same bounded impulse path.

The focused first-touch fixture reaches the exact `0.5` support impulse in one
iteration from the ordinary effective mass and closing velocity. A replacement
gravity-derived assist was therefore unnecessary. The replay/diagnostic
`terrainWarmStart` value remains schema-compatible but production leaves it
zero.

Terrain restitution thresholds, the terrain resting/rolling policy, and their
authored coefficients were not changed.

## Focused Proof

The existing T3 terrain fixture was evolved rather than deleted. Its original
58 assertions remain represented inside a larger 10-case, 266-assertion
`Persistent contact solver:*` filter.

| Proof | Result |
|---|---:|
| First-frame terrain support without fabricated load | Exact `accN = 0.5` in one iteration; zero fabricated warm start |
| Tilted two-point shoreline cold solve | Both rows solve positive impulses, both cache, count is two |
| Sixteen shoreline gravity refeeds | Two cache hits and two warm-started rows every frame; contact-band `max(abs(vy)) < 0.005` |
| One-iteration stack variants | Full/shoreline/zero seed-policy labels are behaviorally identical with `terrainWarmStart = 0` |
| Terrain friction cone | Bound is `mu * cached.accN` |
| Friction/restitution property invariant | 1 case / 36 assertions pass |
| Sleep-support edge | 1 case / 10 assertions pass |
| Complete unit harness | 461 cases / 2,423,947 assertions pass |

The friction-model audit now has explicit row classifications rather than a
terrain exception fed by fabricated load: stable object rows retain the
documented Catto constant bound, normal-coupled object rows use solved `accN`,
and terrain rows use solved `accN`. BV3 closes the terrain part of finding #6;
it does not disguise the remaining intentional object-row distinction.

## Controlled BV0/BV1 Preservation

`box_vibration_t0.scene.json` was rerun for 1,200 fixed-step frames. Querying
frames 300-1199 produces the same accepted BV1 result:

| Metric | BV3 result |
|---|---:|
| Meaningful supported-brick vertical-velocity flips | 0 |
| Affected supported bricks | 0 |
| Worst per-brick flip count | 0 |
| Minimum solver iterations | 1 |
| Iteration-cap frames | 0 / 900 |
| Cache misses / lookups | 0 / 1,800 |
| Maximum absolute supported-brick `vel_y` | 0 |

## Shoreline A/B

The pre-change Debug executable and the rebuilt BV3 Debug executable each ran
`buoyancy_shoreline_lever.scene.json` for 600 fixed-step frames. The bounded
comparison uses frames 400-599.

| Body | Before y-span | After y-span | Before max `abs(vy)` | After max `abs(vy)` | Sign flips before→after |
|---|---:|---:|---:|---:|---:|
| Jetty | 0.252942 | 0.200759 | 0.922442 | 0.640196 | 3→1 |
| Half-grounded log | 0.733778 | 0.809637 | 1.871337 | 0.588056 | 2→0 |
| Wet-end log | 0 | 0 | 0 | 0 | 0→0 |
| Wide plank | 0.332012 | 0.341209 | 1.223521 | 1.260678 | 3→1 |
| **Visible total** | — | — | — | — | **8→2** |

The small mixed span changes do not produce a shoreline bobbing regression:
three moving bodies reduce sign flips, two substantially reduce maximum
vertical speed, the stationary wet-end log remains stationary, and the focused
sixteen-step tilted shoreline proof stays inside the contact band.

Two independent post-change CSV captures are byte-identical:
`A1BFA298FDB6E6155B66DAFB26CF30051C483ABBDACF18BFCB27DE231FD42C72`.

## Validation And Planned Golden Movement

| Gate | Result |
|---|---|
| Focused Profile test build and solver filters | PASS |
| Direct complete Profile unit executable | PASS — 461/461 cases, 2,423,947 assertions |
| `tools\validate_physics.bat` | Expected cumulative stale golden only; rebuilt/reran successfully, then reported 34,588 differing lines in `physics_regression_varied.csv` |
| `tools\validate_physics_deep.bat` | Expected cumulative stale goldens only; varied has 34,588 differing lines and shooting reaction volley has 28; bullet wall/object/terrain and space three-body remain byte-exact |
| Known-issue regression query | Expected cumulative solver-distribution, stacking, at-rest, and terrain-probe signature movement |
| Physics query regression | Expected cumulative `supported_rows` movement from 621 to 683 and bounded timeline value movement |

The generated `physics_regression_varied.csv` still contains two byte-identical
complete runs, so determinism survives the cumulative BV1-BV3 response changes.
No baseline or golden artifact was regenerated. The plan reserves one inspected
Debug refresh for the final cumulative transition after BV4/BV5.

## Artifact Accounting

All measurements are regenerable untracked Debug artifacts:

| Artifact | Bytes |
|---|---:|
| `Debug/bv3_shoreline_before.physicsdiag.ndjson` | 4,472,593 |
| `Debug/bv3_shoreline_before.csv` | 381,699 |
| `Debug/bv3_shoreline_after.physicsdiag.ndjson` | 5,064,903 |
| `Debug/bv3_shoreline_after.csv` | 378,275 |
| `Debug/bv3_shoreline_after_repeat.csv` | 378,275 |
| `Debug/bv3_box.physicsdiag.ndjson` | 7,825,030 |
| `Debug/bv3_box.csv` | 842,927 |

Only bounded query summaries and hashes were used for conclusions; raw trace
volume is not part of the repository change.

## Governance And Comment Audit

`PersistentContactSolveTransaction::PrecomputeRows` still owns one guarded
pre-solve phase: it turns constructed rows and prior cache values into effective
masses, bias, friction bounds, warm-start impulses, and diagnostics, and retains
no borrowed owner after return. Its current-body ruling was refreshed to the
post-BV3 digest and points here.

The comment-style audit inspected every touched source-bearing file in full:

- [x] `SkullbonezSource/Physics/PersistentContactSolver.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h`
- [x] `SkullbonezTests/TestPersistentContactSolver.cpp`

Checked: 3. Deferred: 0. Unchecked: 0.

BV3 is an incremental implementation stage, so the orchestrator's independent
rubber-duck closure review remains assigned to BV6.

## Next

Continue with BV4 convergence remeasurement. Do not regenerate cumulative
physics goldens until the plan's final bounded-divergence refresh.
