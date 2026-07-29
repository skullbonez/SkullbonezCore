# Persistent Contact Convergence Early-Out — CE0

Date: 2026-07-29

Branch: `nightrunner-30th-JUL-26`

Source tip: `536e0a60`

Task: CE0 — current-source convergence census

## Outcome

The post-Box 200-brick wall still reaches the 12-iteration cap in every frame
from 200 through 1199. Existing diagnostics prove the symptom and describe the
final row population, but they cannot identify which per-iteration impulse
delta keeps the early-out residual above its `1e-6` threshold. CE1 therefore
needs bounded diagnostic-only instrumentation before it can build an honest
controlled cause oracle.

No simulation source, setting, iteration count, baseline, or golden artifact
changed in CE0.

## Current-Source Census

| Measurement, frames 200-1199 | Value |
|---|---:|
| Frames sampled | 1,000 |
| Frames at 12 iterations | 1,000 |
| Minimum / average / maximum iterations | 12 / 12 / 12 |
| Contact rows | 565,635 |
| Cache hits / misses | 522,354 / 40,569 |
| Cache miss rate | 7.206847% |
| Warm-started rows | 540,626 / 565,635 (95.578598%) |
| Position-correction rows | 43,092 / 565,635 (7.618340%) |

The final contact population is dominated by cached, sleep-supporting rows:

| Contact family | Sleep support | Rows | Warm-started rows |
|---|---:|---:|---:|
| box/box | yes | 330,924 | 315,687 |
| box/terrain | yes | 110,141 | 110,141 |
| box/box | no | 42,233 | 35,410 |
| box/terrain | no | 14,000 | 14,000 |
| sphere/terrain | yes | 829 | 829 |
| sphere/box | no | 83 | 79 |
| sphere/box | yes | 17 | 13 |

These are final-frame row facts, not convergence attribution. A large family
can be converged while one smaller family continues to change, so row counts
alone cannot select the CE1 cause.

## Observability Finding

`PersistentContactSolveTransaction::SolveRows` accumulates
`deltaN^2 + deltaT1^2 + deltaT2^2` across every row into one local
`iterImpulseSq` and exits only below `1e-6`. Current durable diagnostics expose
the final iteration count but not that residual, its normal/tangent split, its
maximum row, or its terrain/object family.

The in-memory pipeline records do carry `deltaN`, accumulated normal impulse,
and accumulated tangent magnitude per row. That path is still insufficient:

- SkullScope exports only per-stage counts, not record scalar values;
- the pipeline owner caps each frame at 4,096 records;
- all 1,000 measured frames hit that cap;
- only 1,617,660 of the 6,787,620 required solver-iteration row records survive
  before the cap, or 23.8325%;
- record order mixes broadphase, manifold, terrain, warm-start, and iteration
  stages, so the retained prefix is not an unbiased row-family sample.

CE1 needs one bounded per-iteration aggregate owned by the contact-solver
diagnostics boundary: total squared delta, normal/tangent squared components,
maximum row delta and its terrain/object identity, and active-row counts. It
does not need uncapped per-row trace retention or a wider pipeline buffer.

## Trace And SkullScope Accounting

Trace command:

```powershell
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step `
  --shadows off --hide-top-text --frames 1200 `
  --scene SkullbonezData/scenes/prediction_ragdoll_wall_200.scene.json `
  --physics-diag Debug/ce0_wall.physicsdiag.ndjson
```

Artifacts:

| Artifact | Bytes |
|---|---:|
| `Debug/ce0_wall.physicsdiag.ndjson` | 522,057,745 |
| `Debug/ce0_wall.physicsdiag.sqlite` | 283,648,000 |

Queries read by GPT:

1. `solver --frames 200:1199 --limit 12`: 3,694 characters / 3,694 bytes.
2. `pipeline --frames 200:1199 --limit 12`: 5,418 characters / 5,418 bytes.
3. Bounded SQL grouped by `contact_type` and `supports_sleep`: 1,920
   characters / 1,920 bytes.
4. One-row SQL comparing exported versus required iteration records: 486
   characters / 486 bytes.

Total GPT-read output: 11,518 characters / 11,518 bytes. Raw NDJSON and SQLite
artifacts were not read by GPT. No query result was truncated.

## Validation

Documentation and measurement only. The runtime launch exited successfully,
all bounded queries reported `"truncated": false`, and the pre-measurement Box
closure already proved the exact final Debug binary through Physics, deep
Physics, unit, and full gates. No additional repository validation was
required for CE0.

