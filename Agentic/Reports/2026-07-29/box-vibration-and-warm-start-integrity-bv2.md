# Box Vibration And Warm-Start Integrity — BV2

Date: 2026-07-29
Branch: `nightrunner-29th-JUL-26`
Measured source tip before BV2: `63d7e92f8d35ad07f4d9170a19233470d65f36cf`
Scope: BV2 only; no BV3 terrain-seed, BV4 convergence, BV5
position-correction, or baseline-regeneration work

## Result

Box and convex-polytope SAT selection now applies a challenger margin only
when a candidate would change the winning axis type. A candidate within the
same type still wins on any strictly smaller overlap. The deterministic scan
order remains A faces, B faces, then edge cross-products.

The shared margin is:

```text
same axis type: 0
different axis type: max(1e-4, contactSkin * 0.25)
```

This changes no retained state and adds no temporal owner. It stabilizes the
feature family chosen during one narrowphase evaluation, which in turn
stabilizes the complete 32-bit feature id carried into the persistent-contact
key.

## Current Feature And Key Proof

The current source was re-resolved before implementation:

- feature kind remains in bits 15:14;
- box-face ids retain the reference owner in `refCode` bit 3, which is bit 13
  of the complete feature id;
- box-edge uses a distinct kind, so face/edge changes re-key the row;
- `PersistentContactSolver` keeps the full 32-bit feature id beside two 15-bit
  body indices: `lo[61:47] | hi[46:32] | featureId[31:0]`.

Therefore both a face/edge change and an A/B reference change remain observable
warm-start identity changes on the current tree.

## Recovered Regression And Margin Evidence

The contact-identity regression from
`origin/codex/contact-identity-regression-29th-jul-26` commit `27906417` was
adapted by hand to the current test source. No stale production or baseline hunk
was imported.

The rocking sweep covers seven lateral offsets and 61 tilt samples per offset.
It pins one feature kind and, for face contacts, one reference owner across the
tilt crossover. Current-tip controlled bracketing produced:

| Contact-band fraction | Rocking oracle |
|---:|---:|
| Stateless `1e-4` (BV1 source) | FAIL; 338 assertions |
| 0.05 | FAIL; 174 reference-owner assertions |
| 0.10 | PASS; 1,715/1,715 assertions |
| 0.15 | PASS; 1,715/1,715 assertions |
| 0.20 | PASS; 1,715/1,715 assertions |
| 0.25 | PASS; 1,715/1,715 assertions |

A direct current-tip rebuild with the stateless selector reproduces the
preservation commit's exact 338 failures, proving the recovered regression is
red without BV2 rather than only at an undersized experimental margin.

A second oracle places two same-family A-face candidates only `0.00005` apart,
below the cross-family `1e-4` floor. The smaller overlap wins, proving the
hysteresis did not weaken strict within-type SAT selection.

The synthetic crossover alone would permit `0.10`, so the wall structural
measurement decides the final value rather than the synthetic fixture.

## Wall Structural Rates

The pre/post traces were produced from the same BV1 source tip and configuration.
The pre trace compiled the old stateless `1e-4` margin at the shared tip; the
post trace used the final 25% band. The metric groups solver contacts by frame
and brick pair, decodes kind/reference ownership from `feature_id`, and counts
only consecutive-frame transitions.

Frames 400-500, all wall-brick pairs:

| Selector | Pair-frames | Face/edge switches | Reference swaps |
|---|---:|---:|---:|
| Stateless `1e-4` | 31,158 | 1,604 | 326 |
| `0.10 * contactSkin` | 32,391 | 142 | 289 |
| Final `0.25 * contactSkin` | 31,679 | **116** | **281** |

The final selector cuts face/edge churn by **92.77%** from the current-tip
stateless control. Its residual rate is **0.366%** of pair-frames.

The symptom-region filter covers columns 2-6 and 14-17, away from the direct
impact columns:

| Selector | Pair-frames | Face/edge switches | Reference swaps |
|---|---:|---:|---:|
| Stateless `1e-4` | 14,521 | 754 | 197 |
| Final `0.25 * contactSkin` | 15,054 | **54** | **127** |

In the slow-topple region, face/edge churn falls **92.84%** and reference swaps
fall **35.53%**. The remaining reference transitions are under 1% of pair-frames
and occur in a deliberately chaotic impact scene; they are recorded rather than
misrepresented as zero.

The 25% band is therefore retained as a measured safety margin: 5% fails the
controlled crossover, 10% leaves more wall churn, and 25% materially stabilizes
the feature-family boundary while preserving strict same-family selection.

## Controlled BV0/BV1 Metric And Determinism

The exact BV0 scene, launch shape, and SQL metric were rerun from the final BV2
Debug binary:

| Frames 300-1199 | BV0 | BV1 | BV2 |
|---|---:|---:|---:|
| Downward-to-upward `vel_y` flips | 566 | 0 | **0** |
| Bricks affected | 4/4 | 0/4 | **0/4** |
| Worst brick | 181 | 0 | **0** |
| Frames at 12-iteration cap | 900/900 | 0/900 | **0/900** |
| Minimum solver iterations | 12 | 1 | **1** |
| Cache misses / lookups | 2,169 / 8,465 | 0 / 1,800 | **0 / 1,800** |
| Maximum absolute `vel_y` | 3.914273 | 0.0 | **0.0** |

Two independently generated BV2 CSVs are byte-identical:

```text
Debug/bv2_box_a.csv
Debug/bv2_box_b.csv
SHA-256 CF7C7A3BB1DD46DCAEC24DF899E3A56DF240A88F7AD73C2379BE3B4949A745D1
```

BV2 therefore preserves BV1's complete controlled bounce/convergence result
and remains deterministic.

## Validation And Baseline Deferral

| Command | Result |
|---|---|
| Focused Profile build | PASS; zero warnings/errors |
| `Profile\SKULLBONEZ_TESTS.exe --test-case="Object contact manifold:*"` | PASS; 6 cases / 2,064 assertions |
| `tools\validate_format.bat` | PASS |
| `tools\validate_tests.bat` | PASS; 461 cases / 2,423,809 assertions |
| `tools\validate_physics.bat` | Expected cumulative golden mismatch only |
| `tools\validate_physics_deep.bat` | Expected cumulative golden mismatch only |
| `git diff --check` | PASS |

Both Physics gates build and generate deterministic output, then stop at the
planned stale `physics_regression_varied.csv`: 14,834 lines differ, first at
line 4,013. Deep Physics separately reports byte-exact matches for
`bullet_sweep_wall.csv`, `bullet_sweep_object.csv`,
`bullet_sweep_terrain.csv`, `shooting_reaction_volley.csv`, and
`space_three_body_chaos.csv`.

No physics, SkullScope, replay, visual, or DX12 baseline was regenerated. The
plan still requires one cumulative refresh from the final Debug binary after
BV1-BV5.

## SkullScope Accounting

Generated artifacts:

| Artifact | Bytes |
|---|---:|
| `Debug/bv2_wall_pre.physicsdiag.ndjson` | 511,906,786 |
| `Debug/bv2_wall_pre.physicsdiag.sqlite` | 278,319,104 |
| `Debug/bv2_wall_f10.physicsdiag.ndjson` | 528,979,894 |
| `Debug/bv2_wall_f10.physicsdiag.sqlite` | 287,264,768 |
| `Debug/bv2_wall_f25.physicsdiag.ndjson` | 541,334,318 |
| `Debug/bv2_wall_f25.physicsdiag.sqlite` | 293,924,864 |
| `Debug/bv2_box_a.physicsdiag.ndjson` | 7,825,030 |
| `Debug/bv2_box_a.physicsdiag.sqlite` | 3,821,568 |

The model read only bounded summary/schema/aggregate query output, not raw
NDJSON, SQLite, or CSV content. Query output was not truncated. The structural
queries returned one row each; the BV0/BV1 controlled query returned one row.

## Comment Audit

Whole-file touched-source audit against
`Agentic/Skills/comment-style-audit/skill.md` and
`Agentic/Reference/comment-style-guide.md`:

- [x] `SkullbonezSource/Physics/ObjectContactManifold.cpp`
- [x] `SkullbonezTests/TestObjectContactManifold.cpp`

The production file explains axis-type hysteresis, strict same-family
comparison, warm-start identity impact, the measured 25% choice, and the
byte-exact validation consequence. The test file defines SAT and the
reference-owner bit and teaches why either family change loses cache identity.
Existing feature encoding, clipping, narrowphase, and deterministic-baseline
claims were checked against the post-change source. All `Related` paths resolve.

Checked: 2. Deferred: 0. Unchecked: 0.

## Review Accounting

A fresh read-only rubber-duck review independently reproduced the wall
structural totals, checked the recovered oracle and feature-bit packing,
confirmed the 25% evidence and strict same-family selection, and found no stale
source, baseline, BV3 behavior, or governance defect.

Verdict: **CLEAR — no BV2 implementation blocker.** BV6 still owns the plan's
final cumulative ownership/behavior review after baseline regeneration.
