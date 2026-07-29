# Box Vibration And Warm-Start Integrity — BV4

Date: 2026-07-29

Branch: `nightrunner-30th-JUL-26`

Task: BV4 — re-measure convergence

## Outcome

The controlled four-brick fixture remains completely healthy after BV3, but the
200-brick wall still never reaches the solver early-out.

| Measurement window | BV0 | BV2 | BV4 |
|---|---:|---:|---:|
| Controlled downward-to-upward flips, frames 300-1199 | 566 | 0 | **0** |
| Controlled frames at 12 iterations | 900/900 | 0/900 | **0/900** |
| Controlled minimum iterations | 12 | 1 | **1** |
| Controlled cache misses / lookups | 2,169 / 8,465 | 0 / 1,800 | **0 / 1,800** |
| Wall frames at 12 iterations, frames 200-1199 | 1,000/1,000 | not re-measured | **1,000/1,000** |
| Wall minimum / average / maximum iterations | 12 / 12 / 12 | not re-measured | **12 / 12 / 12** |
| Wall cache misses / lookups | 78,594 / 443,784 | not re-measured | **41,901 / 580,467** |
| Wall cache-miss rate | 17.71% | not re-measured | **7.218498%** |
| Wall warm-started rows / contact rows | not recorded | not re-measured | **557,764 / 582,238 (95.796564%)** |

The current wall has materially healthier identity/cache behavior, but that did
not restore convergence. Raising the iteration count remains rejected: the
original investigation already measured worse behavior at 48 iterations, and
the current evidence shows a stop-condition or residual-quality problem rather
than proof that 12 iterations are insufficient.

Per BV4's acceptance rule, the persistent wall non-convergence is registered as
the separate queued plan
`Agentic/Plans/TODO/persistent-contact-convergence-early-out.md`. It starts after
this campaign closes and does not block BV5.

## Determinism

Two independent controlled CSV launches are byte-identical:

```text
Debug/bv4_box.csv
Debug/bv4_box_repeat.csv
Bytes each: 842,927
SHA-256: CF7C7A3BB1DD46DCAEC24DF899E3A56DF240A88F7AD73C2379BE3B4949A745D1
```

That hash is also identical to BV2, so BV3 did not perturb the controlled
fixture's settled result.

## Trace Commands

```powershell
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step `
  --shadows off --hide-top-text --frames 1200 `
  --scene SkullbonezData/scenes/box_vibration_t0.scene.json `
  --physics-diag Debug/bv4_box.physicsdiag.ndjson `
  --physics-regression-log Debug/bv4_box.csv

Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step `
  --shadows off --hide-top-text --frames 1200 `
  --scene SkullbonezData/scenes/box_vibration_t0.scene.json `
  --physics-regression-log Debug/bv4_box_repeat.csv

Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step `
  --shadows off --hide-top-text --frames 1200 `
  --scene SkullbonezData/scenes/prediction_ragdoll_wall_200.scene.json `
  --physics-diag Debug/bv4_wall.physicsdiag.ndjson
```

## SkullScope Accounting

Artifacts:

| Artifact | Bytes |
|---|---:|
| `Debug/bv4_box.physicsdiag.ndjson` | 7,825,030 |
| `Debug/bv4_box.physicsdiag.sqlite` | 3,821,568 |
| `Debug/bv4_wall.physicsdiag.ndjson` | 529,376,742 |
| `Debug/bv4_wall.physicsdiag.sqlite` | 289,185,792 |

Queries read by GPT:

1. `tools\physics_query.bat Debug\bv4_box.physicsdiag.ndjson sql "<BV0 one-row SQL>" --limit 5`
   — 559 characters / 559 bytes.
2. `tools\physics_query.bat Debug\bv4_wall.physicsdiag.ndjson solver --frames 200:1199 --limit 12`
   — 3,699 characters / 3,699 bytes.
3. `tools\physics_query.bat Debug\bv4_wall.physicsdiag.ndjson sql "<one-row convergence aggregate>" --limit 5`
   — 686 characters / 686 bytes.

Total GPT-read SkullScope output: 4,944 characters / 4,944 bytes. Raw NDJSON,
SQLite, and CSV artifacts were not read by GPT. No query output was truncated.

## Validation

- `tools\validate_build.bat Debug`: pass, zero warnings/errors.
- Both runtime launches exit successfully.
- Both bounded SkullScope SQL results return one row with
  `"truncated": false`.
- Documentation and measurement only; no repository validation gate is
  required.

BV4 is complete. Continue BV5; do not raise the solver iteration count.
