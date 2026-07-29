# Box Vibration And Warm-Start Integrity — BV1

Date: 2026-07-29
Branch: `nightrunner-29th-JUL-26`
Measured source tip before BV1: `9b12badbfdbba1e134d8d958ea44e6606d0f21c2`
Scope: BV1 only; no BV2 SAT hysteresis, BV3 terrain-seed, BV4 convergence,
BV5 correction-divisor, or baseline-regeneration work

## Result

Object/object restitution now applies only to a fresh contact row. Before bias
selection, `PersistentContactSolveTransaction::PrecomputeRows` asks the existing
sorted cache whether the pair-and-feature row carried normal or tangent load in
the previous frame:

- no cached load and closing speed over the threshold: retain restitution;
- cached load: suppress restitution and continue through the existing
  object/object Baumgarte penetration calculation;
- terrain: retain the previous branch byte for byte.

The implementation does not add retained state, change cache ordering, or move
authority. `HasCachedImpulse` already owns the exact sorted key lookup used by
row reduction and answers the intended prior-load question without consuming
or reordering the cache.

## Controlled Vibration Measurement

The exact BV0 launch and one-row SkullScope query from
`box-vibration-and-warm-start-integrity-bv0-t0.md` were rerun from the final BV1
Debug binary.

| Frames 300-1199 | BV0 | BV1 | Change |
|---|---:|---:|---:|
| Downward-to-upward `vel_y` flips | 566 | **0** | **-100%** |
| Bricks affected | 4/4 | **0/4** | all four cleared |
| Worst brick | 181 | **0** | -100% |
| Frames at 12-iteration cap | 900/900 | **0/900** | -100% |
| Minimum solver iterations | 12 | **1** | early-out restored |
| Cache misses / lookups | 2,169 / 8,465 | **0 / 1,800** | misses eliminated |
| Maximum absolute `vel_y` | 3.914273 | **0.0** | settled |

Two independent post-change `--physics-regression-log` outputs are byte
identical:

```text
Debug/box_vibration_bv1a.csv
Debug/box_vibration_bv1b.csv
SHA-256 1EDDE31D1CF8E987C71445DC98E5AB89EC16F49AFC0398F44B2BB38B4B82118E
```

## Focused Behavior Proof

`Persistent contact solver: object restitution applies only to a fresh contact`
uses the same fixed sphere/object geometry and high closing speed twice.

- The fresh row rebounds and stores positive load.
- A second solver restored from that cache reports one hit.
- The persistent row's bias equals
  `baumgarteBeta * penetration / dt`, proving suppression falls through to
  penetration repair rather than leaving the row without bias.
- Its separating speed is below the fresh restitution result.

The complete focused harness passes:

| Filter | Result |
|---|---:|
| `Persistent contact solver:*` | 10 cases / 128 assertions |
| `Object contact manifold:*` | 4 cases / 345 assertions |
| `Property invariant: friction and restitution outputs stay bounded*` | 1 case / 36 assertions |

## Terrain-Only Byte Proof

`terrain_contact_probe_debug.scene.json` contains one ball and no object/object
candidate. It was run from the pre-change Debug binary and the final BV1 Debug
binary with identical fixed-step arguments:

```powershell
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step `
  --shadows off --hide-top-text --frames 120 `
  --scene SkullbonezData/scenes/terrain_contact_probe_debug.scene.json `
  --physics-regression-log Debug/bv1_terrain_<before|after>.csv
```

Both 18,274-byte CSVs have SHA-256
`823D6756B9CDB11E973CDC7E38DD8EFC1F32A00EFC40B65F5CB207130C9E8631`.
This is a byte-exact A/B proof that BV1 did not alter terrain response.

## Cache Reach Limit

`StoreCache` admits only rows whose `supportsRestingPolicy` flag is true. The
object classifier excludes a vertically supported box balanced on an edge
(thin manifold, upward/downward normal, and no box face aligned to that normal).
It also excludes the applicable non-sphere convex-hull one-point footprint.
Those rows cannot carry prior load into BV1, so they keep fresh-impact
restitution.

That is not a categorical edge/corner exclusion. A lateral box edge/corner row
does not trigger the vertical box-support veto and may retain
`supportsRestingPolicy`; when it stores load, BV1 suppresses restitution on
reuse. The focused reach oracle pins both sides with current classification
seams: vertical tilted-box edge support produces no cache entry, while the
equivalent lateral thin box contact caches and its reused row receives the
exact Baumgarte bias.

## Validation And Baseline Deferral

| Command | Result |
|---|---|
| Focused Profile build and filters above | PASS |
| `tools\validate_tests.bat` | PASS; 458 cases |
| `tools\validate_physics.bat` | Expected golden mismatch only |
| `tools\validate_physics_deep.bat` | Expected golden mismatch only |
| `python tools/inventory_function_complexity.py --repo . --strict` | PASS after current-body digest refresh |
| `git diff --check` | PASS |

Both Physics gates built and generated deterministic output successfully, then
stopped at the planned stale `physics_regression_varied.csv`: 14,534 lines
differ, first at frame 139. Deep Physics separately reports byte-exact matches
for `bullet_sweep_wall.csv`, `bullet_sweep_object.csv`,
`bullet_sweep_terrain.csv`, `shooting_reaction_volley.csv`, and
`space_three_body_chaos.csv`.

No physics, replay, visual, or DX12 baseline was regenerated. The plan requires
one cumulative refresh from the final Debug binary after BV1-BV5, so this
intentional divergence remains explicit until that final refresh.

The 458-case unit gate preceded the review correction. The added reach oracle
was then built and rerun in the complete focused solver filter: 10 cases / 128
assertions pass.

`PrecomputeRows` remains one guarded, synchronous row-precompute phase. The
edited body still jointly owns effective masses, contact-kind bias, friction
bounds, warm-start impulses, and trace values, and no borrowed owner survives
return. Its retained complexity ruling was refreshed to the current body hash;
the ownership judgment did not change.

## Comment Audit

Whole-file touched-source audit against
`Agentic/Skills/comment-style-audit/skill.md` and
`Agentic/Reference/comment-style-guide.md`:

- [x] `SkullbonezSource/Physics/PersistentContactSolver.cpp`
- [x] `SkullbonezTests/TestPersistentContactSolver.cpp`

The production concept block now explains fresh impact, prior cached load,
Baumgarte fallthrough, exact resting-footprint cache reach, and byte-exact
baseline impact. The focused tests pin the fresh/cached bias and both sides of
the vertical-versus-lateral thin box classification boundary.
Existing ownership, phase-order, terrain-seed, and cache-admission claims were
checked against the post-change source and remain current.

Checked: 2. Deferred: 0. Unchecked: 0.

## Review Accounting

An independent read-only review found one blocking overstatement: the first
handoff overgeneralized cache exclusion from contact shape, while the source
gates admission on `supportsRestingPolicy`. The wording is corrected
through production, test, plan, report, and SessionState, and the focused
vertical-versus-lateral reach oracle now pins the exact current classification.
The same reviewer reran that focused oracle (1 case / 9 assertions) and returned
**CLEAR — no remaining blocker**.
BV6 still owns the plan's final independent review after cumulative behavior
and baseline regeneration are complete.
