# Contact Energy And Warm-Start Integrity — ES3

Date: 2026-08-02
Branch: `nightrunner-1st-AUG-26`
Scope: object/object fresh-impact restitution only; no terrain, SAT, friction,
position-correction, iteration, capacity, scene, or baseline change

## Result

Object/object restitution now applies only when the current body pair did not
carry contact load in the previous completed solve. The sorted persistent cache
already owns that one-frame loaded-contact lifetime: object keys store the body-
pair prefix above the feature ID, and a solve with no contact clears it. ES3
therefore distinguishes two questions in one bounded cache walk:

- did any prior row for this pair carry normal or tangent load? This suppresses
  renewed restitution from rocking or `omega × r`, even if the current manifold
  selects another feature; and
- does this exact feature have a compatible cached impulse? Only that exact row
  may warm-start the current geometry.

The first exact entry remains authoritative if duplicate keys exist, preserving
the prior `lower_bound` behavior. The fused lookup avoids adding a second binary
search to each object row. Terrain keeps its previous exact-key lookup and is
byte-identical. Mutual-gravity elastic contacts deliberately bypass the
persistent-resting suppression.

## Focused Proof

`Persistent contact solver: restitution follows loaded object-pair lifetime`
pins the complete lifecycle in 14 assertions:

- a fresh 0.75-restitution sphere impact separates;
- a prior loaded pair with a deliberately changed feature ID reports an exact
  cache miss but receives Baumgarte bias, not renewed restitution;
- a complete no-contact solve clears the lifecycle and the next impact receives
  the original restitution again; and
- mutual-gravity elastic policy remains elastic and does not warm-start.

The focused Profile run combines this case with all five ES1 contact-energy
oracle cases: 6 cases / 101 assertions pass. The final-source four-brick and
64-level tower CSVs are byte-identical to the first measured ES3 implementation,
proving the fused lookup retained its behavior after the first-match correction.

## Four-Brick Result

The exact 1,200-frame fixture changes from persistent vibration to permanent
sleep without changing scene or solver settings:

| Metric | ES0 | ES3 |
|---|---:|---:|
| post-300 downward-to-upward flips | 566 | **0** |
| maximum post-300 upward speed | 3.914273 | **0** |
| permanent all-sleep frame | none | **294** |
| final sleeping bodies | 0/4 | **4/4** |
| maximum penetration | 0.033819 | **0.017987** |
| cap-bound frames | 900/900 measured tail | **291 total; one iteration after frame 300** |
| cache misses while any dynamic body is awake | 2,169 total in the all-awake ES0 run | **318** |
| final-300 cache misses | 2,169 in ES0's measured tail | 900, all after the island is already asleep |

The semantic checker still reports `cache_tail` because it sums diagnostic
lookups after every body is asleep, and `support` because only the terrain-root
brick retains the frame-local `sleep_supported` bit after all four bodies join
one sleeping island. Those signals no longer describe motion, but the remaining
feature/cache churn is retained as ES4 attribution rather than being hidden by
loosening the checker in ES3.

## Giant Tower And Wall Attribution

The 64-level tower remains incomplete, but moves in the intended direction:

| Metric | ES0 | ES3 |
|---|---:|---:|
| last complete frame | 37 | **41** |
| mechanical gain before fatal | 0 | **0** |
| downward-to-upward flips | not meaningful before frame 300 | **0** |
| maximum penetration | 0.200018 | 0.305018 |
| cache hits / misses | 1,087 / 461 | **1,229 / 404** |

The unchanged candidate-list fatal and increased penetration show that
restitution was a real cause but not the complete tower failure. ES4 must now
adjudicate feature identity and cached-geometry validity without increasing
capacity.

The exact 6,800-frame 200-box wall remains complete with all 211 bodies retained:

| Metric | ES0 | ES3 |
|---|---:|---:|
| post-300 vertical reversals | 7,482 | **6,363** |
| maximum post-300 upward speed | 12.125853 | **9.090012** |
| final-300 relaunches | not separately accepted | **0** |
| final-300 cache misses | nonzero in the cumulative run | **0** |
| peak gain over initial mechanical energy | 127.770669 | 127.770669 |
| final sleeping bodies | 210/211 | 210/211 |

The one below-support striker, precision-envelope violation, and final awake
body are unchanged residual failures. ES3 neither worsens nor disguises them.

## Terrain And Determinism Proof

The 120-frame terrain-only regression witness is byte-identical before and
after ES3:

```text
bytes   18,274
SHA-256 823D6756B9CDB11E973CDC7E38DD8EFC1F32A00EFC40B65F5CB207130C9E8631
```

Final-source repeat hashes are:

```text
four-brick CSV F596D7E65EBD67C7D7F141344C3752EE7DFC4039312454B81CB43CC0CBA66A64
tower-64 CSV   A990FE1208B86B71CD00F766E5650E8503CF2D82FC9337762C6D52CA5C8B5DB0
```

Each matches its earlier ES3 run byte for byte.

## SkullScope Accounting

Raw NDJSON and SQLite contents were never shown to the model. Ignored primary
traces were 11,191,448 bytes for four-brick, 3,569,421 bytes for tower-64, and
1,659,223,431 bytes for wall-200. The model saw only bounded one-row summary,
cache-attribution, and duplicate-key aggregate answers; one four-row final-body
answer; and the checker's compact failure summaries. All SQL commands used
`--limit 5` except the wall final-body identity/terrain-clearance answer, which
used `--limit 300` for its exact 211-body bound. No bounded answer was truncated.

## Validation And Comment Audit

- direct Profile test build: PASS;
- focused lifecycle plus ES1 energy matrix: PASS, 6 cases / 101 assertions;
- final Debug runtime build: PASS;
- terrain-only byte comparison: PASS;
- four-brick, tower-64, and wall-200 bounded semantic measurements: complete;
- `tools\validate_tests.bat`: PASS, complete Profile suite;
- `tools\validate_fast.bat`: PASS, including format, metadata, dependency,
  ownership, Debug/Profile builds, tests, and compiled-symbol reachability;
- function-complexity owner review: retained `PrecomputeRows` as the single
  synchronous row-precompute phase; exact current body digest passes; and
- `git diff --check`: PASS.

Touched source-bearing comment audit is 2/2 with zero deferred:

- `SkullbonezSource/Physics/PersistentContactSolver.cpp`; and
- `SkullbonezTests/TestPersistentContactSolver.cpp`.

Both learning headers remain accurate. Nearby invariant, concept, and hazard
comments explain pair lifetime versus feature compatibility, restitution
fallthrough, elastic exclusion, duplicate first-match behavior, and the planted
feature-churn/no-contact lifecycle proof. Ownership and sequencing claims match
the post-change source. No term needs owner-approved wording.
