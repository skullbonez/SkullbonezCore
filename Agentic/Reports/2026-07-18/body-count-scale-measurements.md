# Physics Body-Count Scale Measurements

Date: 2026-07-18 through 2026-07-20

Campaign: `physics-body-count-scale-campaign`

Final branch: `main` (the owner explicitly directed P7 closure and commit on
`main`; no branch switch was performed)

Task boundary: P0-P7 complete; P6 is evidence-deferred and the campaign is
closed

## P0 Owner Rulings

- The campaign continues on feature branch `nightrunner-18th-july`.
- The sleeping-heavy fixture is accepted at authored capacity 6,000 with
  5,000 dynamic bodies: 4,000 fixed-seed sleepers and 1,000 continuously
  perturbed awake movers. A fixed broadphase-scale witness keeps the authored
  bounds away from knife-edge selection behavior.
- The final P7 target is ratified as at least 4,000 awake bodies within the
  approximately 1 ms physics-step budget on the reference Threadripper 3970X.
- P1's canonical pair-order transition uses the clarified same-state,
  dual-driver work-membership protocol recorded below.
- P5 now owns an automatic evidence gate: P6 proceeds only when the two
  prescribed captures meet the ratified 15% threshold; otherwise it is
  recorded as evidence-deferred and P7 continues.
- P0 refreshes no baseline, golden, screenshot, authored format, or coverage
  floor.

## Fixed Before Matrix

Profile medians are milliseconds over 1,140 captured frames. The existing
inclusive solver-owner interval is
`Frame/Physics/Narrowphase/PersistentContacts`.

| Marker / counter | scale_200 | scale_520 | scale_1000 | scale_2000 | sleepy_5000 |
|---|---:|---:|---:|---:|---:|
| `Frame/Physics` | 0.1106 | 0.8486 | 1.0688 | 1.7852 | 2.1479 |
| `Frame/Physics/Broadphase` | 0.0245 | 0.1986 | 0.3377 | 0.6487 | 0.6963 |
| `Frame/Physics/Broadphase/GridInsert` | 0.0180 | 0.1764 | 0.2538 | 0.4628 | 0.4456 |
| `Frame/Physics/Broadphase/CandidatePairs` | 0.0017 | 0.0074 | 0.0222 | 0.0821 | 0.1976 |
| `Frame/Physics/Broadphase/PruneSleepPairs` | 0.0001 | 0.0002 | 0.0003 | 0.0004 | 0.0001 |
| `Frame/Physics/ApplyForces` | 0.0262 | 0.1812 | 0.1827 | 0.2051 | 0.2108 |
| `Frame/Physics/Integrate` | 0.0224 | 0.1995 | 0.2079 | 0.2455 | 0.2595 |
| `Frame/Physics/Narrowphase` | 0.0004 | 0.0030 | 0.0070 | 0.0221 | 0.0011 |
| Inclusive solver owner (`PersistentContacts`) | 0.0071 | 0.0305 | 0.0569 | 0.1353 | 0.0027 |
| Awake / total bodies | 200 / 200 | 520 / 520 | 1,000 / 1,000 | 2,000 / 2,000 | 1,000 / 5,000 |
| Bodies reinserted this step | 0 | 0 | 0 | 0 | 0 |
| Estimated hot bytes/body/step | 245.0 | 245.0 | 245.0 | 245.0 | 95.4 |

The reinsertion counter is deliberately zero until P2 owns persistent-grid
maintenance. The before matrix proves the central problem: 1,000 awake bodies
inside a 5,000-body store cost 2.1479 ms, roughly twice the all-awake 1,000-body
step, because sleeping rows still pay broadphase and bookkeeping costs.

## Hot-Byte Accounting

The diagnostic is a static logical byte model multiplied by the rows each
pass actually visits; it is not a hardware bandwidth counter. Per step it
charges 58 bytes to every body, 81 additional bytes to an awake force-stage
row, and 106 additional bytes to an awake integration row. Therefore an
all-awake body reports 245 bytes, while 5,000 total / 1,000 awake reports
`(5000*58 + 1000*(81+106))/5000 = 95.4` bytes/body/step.

The Profile-only post-wake scan is serial and outside the physics worker loops.
It avoids shared-counter races and attributes bodies released from fixed state
or awakened during the step to the integration pass that actually consumes
them.

## Capacity And Memory

The authored active capacity is 6,000; fixed runtime stores retain their
8,192-row hard maximum. The active capacity is now applied after scene load to
all runtime capacity consumers, including replay topology.

| Scene | Process private start MB | Restart MB | End MB |
|---|---:|---:|---:|
| scale_200 | 142.75 | 209.45 | 209.45 |
| scale_520 | 142.75 | 214.42 | 214.42 |
| scale_1000 | 142.65 | 222.39 | 222.38 |
| scale_2000 | 142.80 | 238.62 | 238.60 |
| sleepy_5000 | 144.59 | 288.08 | 284.95 |

Focused sleepy-scene accounting reported 94,118,476 owned bytes:
GameModel 6,187,328; PhysicsBodyStore 851,968; ColliderStore 59,506,688;
render storage 2,064,384; PhysicsWorld 25,508,108.

## P0 Determinism Evidence

The final Debug executable produced byte-identical CSVs at worker counts 0,
1, and 4 for every required scene. The retained worker-0 artifact sizes and
SHA-256 values are:

| Scene | Bytes | SHA-256 |
|---|---:|---|
| scale_200 | 33,831,818 | `3DAD84BE52C8C7A9BCA029B11543E841E54B2F14DFE24E7FCF14D0CBE5BF522A` |
| scale_520 | 88,116,534 | `157A5B652465630BE861A558F621DE5A56A86A257D54F6BF6BEF256DC965EE21` |
| scale_1000 | 169,579,366 | `0A690DFC021AA1AD9203A399DEF47ECC45861E43EF16CB60991AE36A973449C2` |
| scale_2000 | 340,430,236 | `CE357FE9FDCE7BE18945712E33C52B516B46D55F297AE67CFE3247256DC8C1BB` |
| sleepy_5000 | 912,830,920 | `67FDBC3D7A6FBDDD6FC15E0393A9DF137AA3777193EE8E9F564090C7B7174C76` |
| regression_varied | 12,663,724 | `51877A2C7D4976343245243BC4CDA24BA7F5AAF57028D3A897F3C2904D410C93` |

The 18-process matrix took 1,461.3 seconds. Worker-1 and worker-4 duplicates
were deleted after comparison (36 files, 3,114,910,094 bytes); worker-0
artifacts remain ignored under `TestOutput/p0_determinism/`.

Provenance: Profile executable 3,278,848 bytes,
SHA-256 `413C14BC2BCCE0D1366048BBA17DB9A2D3CF8385882B3425ADEA1860DA20744D`;
sleepy scene 3,070,330 bytes,
SHA-256 `F253B3E736542770B8FF372FF993BF156DB7BB4C119156E767D93F03D8CA111A`.

## P0 Validation And Review

- `tools\validate_perf.bat`: passed from final reviewed source; zero-warning
  Profile build, allocation guard, analyzer self-test, generator check,
  selected-ball structural probe, DX12/physics budgets, and the five-scene
  matrix all passed.
- `tools\validate_full.bat`: passed in approximately 132 seconds; 292/292
  tests and 21,458 assertions, all coverage floors, zero DX12 validation
  errors, screenshot comparisons, replay/Automation smoke, and the 44,401-line
  byte-exact varied-scene physics baseline passed.
- `Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers --frames 1
  --scene SkullbonezData\scenes\physics_scale_200.scene.json`: passed with
  platform markers enabled and 63 workers.
- Independent review is clear after the capacity-consumer, Profile-init,
  fixed-body awake-count, hot-byte framing, worker-counter race,
  fixed-to-dynamic release, and sleep-disabled count findings were corrected.
- Touched-source comment audit: 16/16 files compliant, zero deferred; see
  `physics-body-count-p0-comment-audit.md`.

## P1 Canonical Pair-Order Transition

`SpatialGrid` now stages each newly discovered normalized pair under its lower
body index, performs two fixed-buffer radix passes over the upper index, and
emits ascending `(minIndex,maxIndex)` order. Discovery order remains an internal
cell-storage detail. The staging capacity is fixed at four pairs per supported
scene row and fails through Lane F with owner, capacity, high-water, and phase
diagnostics instead of allocating or dropping collision work.

`PhysicsBroadphaseStage` preserves that order through fixed/joint/sleep pruning.
The rare fast-small-sweep path re-canonicalizes only when it actually appends a
pair. `SleepPrunedPair` remains a pipeline-trace diagnostic only: it is absent
from the deterministic CSV, and its one-record-per-removed-pair semantics are
preserved until P3 owns its relocation or retirement.

### Same-State Dual-Driver Oracle

The Debug-only `SKORE_P1_PAIR_DRIVER=legacy|canonical` probe reserves its shadow
and normalization vectors at construction. For 360 fixed ticks it compares:

- normalized raw work immediately after grid emission; and
- normalized final solver-visible work after augmentation and all pruning.

Both the legacy-driving/canonical-shadow and canonical-driving/legacy-shadow
directions passed on every required scene. The command shape was:

```bat
set SKORE_P1_PAIR_DRIVER=<legacy-or-canonical>
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step --shadows off --workers 0 --frames 420 --scene SkullbonezData\scenes\<scene>.scene.json
```

The extra 60 frames cover startup before exactly 360 fixed ticks. Final logs are
ignored under `TestOutput/p1_pair_oracle_360_final/`.

| Scene | Legacy-driver seconds | Canonical-driver seconds | Final evidence |
|---|---:|---:|---|
| scale_200 | 8.1 | 8.1 | `ticks=360 boundaries=raw,final` |
| scale_520 | 12.1 | 12.1 | `ticks=360 boundaries=raw,final` |
| scale_1000 | 19.2 | 19.2 | `ticks=360 boundaries=raw,final` |
| scale_2000 | 36.4 | 36.4 | `ticks=360 boundaries=raw,final` |
| sleepy_5000 | 45.5 | 45.4 | `ticks=360 boundaries=raw,final` |
| regression_varied | 7.1 | 7.1 | `ticks=360 boundaries=raw,final` |

This is the corrected proof required by the owner clarification: each pair of
paths consumes the same pre-broadphase state. It does not incorrectly require
two independently evolved projected-Gauss-Seidel trajectories to remain equal.

### Transition Artifact Evidence

Before refresh, `validate_physics` failed only the varied-scene CSV: 15,036
lines differed, first at line 3,791 / frame 102. `validate_physics_deep` proved
the bullet-sweep wall/object/terrain, shooting, and three-body artifacts stayed
byte-exact; the varied, stacking, and at-rest physics outcomes moved.

The authorized final-Debug refreshes are:

| Artifact | Old SHA-256 | New SHA-256 | New shape |
|---|---|---|---|
| `physics_regression_varied.csv` | `C0E90E1C97F4CCF862FC4DBCF8E112BA72B9A382267B81A36C195C2C5A8BD98D` | `DC273C8D6CBA688E71967A100D0C65A084591F78A252B5289F213B9BC8D4AFE9` | 6,330,217 bytes; 44,401 lines |
| `physics_known_issue_signatures.json` | `64BB57B475A98208FC1F3512AC50E8DF067105DED28218252613E196540252E1` | `44E29849DFFB230971006E9021A7468AB725A5B7C673A0ED4FE0BFB6654CDA9F` | stacking + at-rest signatures |

The refreshed stacking witness is 3,081,060 bytes / 22,501 lines with SHA-256
`B090287EA600123295A5B737EECC35E51740405FA7B294F955329CAFAC24C362`;
at-rest is 7,662,543 bytes / 54,001 lines with SHA-256
`2D0D2BEA6E682A54D27D698D7D0D798FF2BF8CADCCFD6C78B0CC01E6CC6D8280`.

The binding 2026-07-19 MASTER directive authorized the complete bounded-
divergence assessment and transition without another owner response. The
assessment reused the existing 4,200-frame Automation report produced by one
engine process and one prediction generation; no second replay generation was
launched for approval or reconciliation.

The complete replay causal delta is bounded and structurally local:

- all 199 old topology IDs remain, body 11 is the sole addition, and no node is
  removed;
- every retained node keeps its parent, depth, and contact-derived
  classification;
- 123/199 retained nodes keep the same first frame; the other 76 have signed
  drift from -176 to +246 frames, absolute median 0 across all retained nodes,
  linear p95 approximately 97, and maximum 246 over the 2,401-tick horizon;
- the added node is `{id:11,parentId:1,firstFrame:301,depth:1,
  contactDerived:false}`; and
- target ID, fixed-step mode, horizon, tick count, ghost requests, horizon
  markers, authored scene/config/script/shader hashes, and schema stay fixed.

After the causal packet matched, the checker exposed the coupled visual packet
delta from the same 200-box replay-golden class. It is also a direct consequence
of the canonical solve order: authored/moved/settled bricks remain 200/200/200,
generation count remains one, affected/future nodes move 199 to 200, trajectory
records move 797 to 801, and points move 957,601 to 962,401. Toppled and
sustained-toppled bricks move together from 187 to 175. Camera, branch, target,
source frame, reveal-frame sequence, publication state, ordinary-line geometry,
and all provenance inputs are unchanged. The generated-geometry hashes and
counts change consistently with those four extra trajectory records; there is
no unrelated screenshot, shader, scene, config, or schema change.

| Reconciled artifact | Old SHA-256 | New SHA-256 |
|---|---|---|
| `replay_visual_fidelity_200_box.json` | `77F2044158694097E55A92D348AD2529D982E8730878EA89C767786CF7FE56DF` | `ECD0B5937DB203ACF00B9138346915539C9BDA5ED0DEC16F3D960A5E0AC3FE47` |
| `replay_visual_fidelity_200_box_causal.json` | `7988F296ACA6B8C3E01A8EA99CECE0E4C072107BBBCDC56A604B957D559D5022` | `FC854376FF2922B628FBAC174694FDEF932BCDE24B7E98E82FF3B85D32B90489` |
| `physics_query_varied.json` | `1212BEF02C96E58DAA02956EA3BD689AEE962E1CB8800DAC32AED7D9E6599303` | `5D5035064141B039F95ECAB17AAD3B3BDA0C7B2D3F610B3AB402155960AA0284` |

The complete 21-query SkullScope packet keeps identical query names and list
shapes, contains no non-finite value, and remains self-identical (`eventDiff=[]`,
`firstDifferentFrame=null`). `supported_rows` moves 617 to 621. Peak energy
moves only 142,247.666238 to 142,443.995165 (+0.14%), peak speed moves
43.451390 to 43.385919 (-0.15%), and peak penetration moves 0.271794 to
0.244428 (-10.1%); no sustained/growing-penetration event exists. This rules
out hidden work loss, NaN propagation, or explosive energy.

The final trace command used by the committed query generator was:

```bat
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_bench_varied.scene.json --physics-diag C:\SkullbonezCore\Debug\physics_query_varied.physicsdiag.ndjson
```

The final trace is 103,785,259 bytes and its SQLite cache is 50,823,168 bytes.
Bounded model-facing queries were:

| Query | Output characters / UTF-8 bytes |
|---|---:|
| `tools\physics_query.bat Debug\physics_query_varied.physicsdiag.ndjson summary --limit 8` | 8,894 / 8,894 |
| `tools\physics_query.bat Debug\physics_query_varied.physicsdiag.ndjson events --limit 20` | 21,806 / 21,806 |
| `tools\physics_query.bat Debug\physics_query_varied.physicsdiag.ndjson questions penetration_spikes` | 1,454 / 1,454 |
| `tools\physics_query.bat Debug\physics_query_varied.physicsdiag.ndjson events --type penetration_sustained,penetration_growing --limit 50` | 310 / 310 |
| `tools\physics_query.bat Debug\physics_query_varied.physicsdiag.ndjson contacts --top penetration --limit 20` | 9,730 / 9,730 |

Total GPT-read query output was 42,194 characters / 42,194 UTF-8 bytes; no
query output was truncated. Raw NDJSON and SQLite contents were never ingested.

The existing replay report passes exact equality against both reconciled
packets for all 2,401 ticks. Negative, incomplete-horizon, causal activation,
causal topology, causal segment, semantic-packet, artifact-byte, prediction-
artifact, ten determinism-mutation, and launcher-shape controls all pass. Final
mapped gates also pass from the final source and artifacts:
`tools\validate_physics.bat` in 55.917 seconds,
`tools\validate_physics_deep.bat` in 136.931 seconds, and
`tools\validate_perf.bat` in 107.318 seconds. The builds report zero warnings
and errors, the varied/deep/query artifacts match exactly, performance reports
no regressions, and the allocation guard records zero steady-gameplay
violations.

### P1 Worker-Count Determinism

The final Debug executable produced byte-identical full-scene CSVs at worker
counts 0, 1, and 4 for all six scenes. The 18-process matrix took 1,453.4
seconds. Worker-1/4 CSV duplicates were deleted only after comparison; retained
worker-0 evidence remains ignored under `TestOutput/p1_determinism/`.

| Scene | Bytes | SHA-256 |
|---|---:|---|
| scale_200 | 33,831,818 | `3DAD84BE52C8C7A9BCA029B11543E841E54B2F14DFE24E7FCF14D0CBE5BF522A` |
| scale_520 | 88,116,798 | `9B8F705074EE7158E7F8B6A8483D49E69632EA857613F83FC551A4FEA8499B5E` |
| scale_1000 | 169,580,528 | `F277D16226591F5B258C0D4C0F8BE54EAD52A9CE679EF49C03970305B4F8DE8C` |
| scale_2000 | 340,410,150 | `AA9D3BFD770ABB838D2EDBF836F33C110E4FBE730437C16739E7F7D847CD36DC` |
| sleepy_5000 | 912,830,920 | `67FDBC3D7A6FBDDD6FC15E0393A9DF137AA3777193EE8E9F564090C7B7174C76` |
| regression_varied | 12,660,434 | `8E9092CB7F28EAFC0D9F167E90CF9D5292D022485D6AE93D591FB758CAEA6387` |

### P1 Measurement Matrix

Profile medians are milliseconds over 1,140 captured frames from the final P1
source. Top-level physics remains within noise of P0 across the matrix; the
canonical emitter's added candidate work is small in absolute terms and does
not produce an unexplained step regression.

| Marker / counter | scale_200 | scale_520 | scale_1000 | scale_2000 | sleepy_5000 |
|---|---:|---:|---:|---:|---:|
| `Frame/Physics` | 0.1066 | 0.8405 | 1.0828 | 1.7407 | 2.0722 |
| `Frame/Physics/Broadphase` | 0.0226 | 0.2032 | 0.3446 | 0.6735 | 0.7299 |
| `Frame/Physics/Broadphase/GridInsert` | 0.0168 | 0.1779 | 0.2782 | 0.4631 | 0.5121 |
| `Frame/Physics/Broadphase/CandidatePairs` | 0.0021 | 0.0087 | 0.0251 | 0.0963 | 0.1792 |
| `Frame/Physics/Broadphase/PruneSleepPairs` | 0.0001 | 0.0002 | 0.0003 | 0.0004 | 0.0001 |
| `Frame/Physics/ApplyForces` | 0.0259 | 0.1763 | 0.1810 | 0.1861 | 0.1941 |
| `Frame/Physics/Integrate` | 0.0221 | 0.1987 | 0.2112 | 0.2355 | 0.2464 |
| `Frame/Physics/Narrowphase` | 0.0004 | 0.0030 | 0.0069 | 0.0226 | 0.0010 |
| Inclusive solver owner (`PersistentContacts`) | 0.0067 | 0.0302 | 0.0563 | 0.1326 | 0.0025 |
| Awake / total bodies | 200 / 200 | 520 / 520 | 1,000 / 1,000 | 2,000 / 2,000 | 1,000 / 5,000 |
| Bodies reinserted this step | 0 | 0 | 0 | 0 | 0 |
| Estimated hot bytes/body/step | 245.0 | 245.0 | 245.0 | 245.0 | 95.4 |

`tools\validate_perf.bat` completed from final source in approximately 168
seconds. Profile, Automation, and Debug builds had zero warnings/errors;
allocation guard, analyzer self-test, structural probe, budgets, and the five
scale captures passed. The focused SpatialGrid suite passed 8/8 cases and 27/27
assertions. P1's touched-source comment audit covers 5/5 files with zero
deferred; see `physics-body-count-p1-comment-audit.md`.

## P2 Persistent Incremental Grid

P2 replaces per-step grid reconstruction with persistent integer-range
membership. Each body owns its last inserted `CellRange`; identical ranges are
true no-ops, while movement adds and removes only the six disjoint slabs between
old and new ranges. Fixed hash buckets and persistent entries use intrusive
bucket/object chains, removal back-links, and free lists, so dense-row retirement
and long travel reuse storage without allocation. `SetCellSize` treats an
identical value as a hot no-op and cold-clears only after a real size change.

Velocity-dependent occupancy remains separate. `BeginFrame` expires the prior
stamped swept/CCD overlay and removes retired dense rows, while `InsertSwept`
adds only cells outside current persistent membership. Candidate collection
combines the persistent and current overlay occupants before the P1 canonical
emitter. Singleton persistent buckets are skipped unless the current overlay
raises occupancy to two; this recovered the expected candidate cost without
changing membership. The stage marker is now `GridMaintain`, and
`BodiesReinserted` counts moved persistent bodies only, excluding first
insertion and overlay work.

Focused coverage exercises unchanged reinsertion, single-cell range deltas,
body-count shrink, overlay expiry, changed/same cell-size behavior, long-travel
entry and bucket reuse, crowded-cell canonicalization, and legal combined
persistent-plus-overlay saturation. The full final-source doctest run passed
318/318 cases and 30,352/30,352 assertions; the targeted SpatialGrid set passed
14/14 cases and 8,503 assertions, and the child fatal contract passed 1/1 case
and 53 assertions.

### P2 Worker-Count Determinism

The final Debug source reproduced every retained P1 witness byte-for-byte at
worker counts 0, 1, and 4 across all six scenes: 18/18 processes in 1,479.979
seconds. Scale scenes used their 600-frame horizon and `physics_bench_varied`
used its 1,200-frame horizon. An earlier diagnostic mistakenly used 600 frames
for the varied witness; a bounded streaming comparison showed equality through
frame 600 and only the horizon/header boundary differed. The corrected complete
matrix is the acceptance evidence below. Worker duplicates were removed only
after exact comparison; retained P1 witnesses remain unchanged.

| Scene | Bytes | SHA-256 |
|---|---:|---|
| scale_200 | 33,831,818 | `3DAD84BE52C8C7A9BCA029B11543E841E54B2F14DFE24E7FCF14D0CBE5BF522A` |
| scale_520 | 88,116,798 | `9B8F705074EE7158E7F8B6A8483D49E69632EA857613F83FC551A4FEA8499B5E` |
| scale_1000 | 169,580,528 | `F277D16226591F5B258C0D4C0F8BE54EAD52A9CE679EF49C03970305B4F8DE8C` |
| scale_2000 | 340,410,150 | `AA9D3BFD770ABB838D2EDBF836F33C110E4FBE730437C16739E7F7D847CD36DC` |
| sleepy_5000 | 912,830,920 | `67FDBC3D7A6FBDDD6FC15E0393A9DF137AA3777193EE8E9F564090C7B7174C76` |
| regression_varied | 12,660,434 | `8E9092CB7F28EAFC0D9F167E90CF9D5292D022485D6AE93D591FB758CAEA6387` |

### P2 Measurement Matrix

Profile medians are milliseconds over the final performance captures. P2
improves total step time at 200, 520, 1,000, and sleepy 5,000 bodies. The
2,000-body step is 4.3% above P1 but 11.4% below the plan's 2.05 ms B4 target;
`GridMaintain` itself falls 50.9% there. Persistent linked-entry traversal raises
candidate collection relative to P1, particularly in the dense scale scenes;
that measured cache/bandwidth cost is owned by P4's hot-state diet.

| Marker / counter | scale_200 | scale_520 | scale_1000 | scale_2000 | sleepy_5000 |
|---|---:|---:|---:|---:|---:|
| `Frame/Physics` | 0.1050 | 0.7722 | 1.0299 | 1.8161 | 1.9813 |
| `Frame/Physics/Broadphase` | 0.0209 | 0.1046 | 0.2395 | 0.6081 | 0.4930 |
| `Frame/Physics/Broadphase/GridMaintain` | 0.0138 | 0.0622 | 0.1160 | 0.2272 | 0.2130 |
| `Frame/Physics/Broadphase/CandidatePairs` | 0.0032 | 0.0268 | 0.0713 | 0.2255 | 0.2367 |
| `Frame/Physics/Broadphase/PruneSleepPairs` | 0.0001 | 0.0002 | 0.0003 | 0.0004 | 0.0002 |
| `Frame/Physics/ApplyForces` | 0.0265 | 0.1779 | 0.1844 | 0.2000 | 0.2107 |
| `Frame/Physics/Integrate` | 0.0214 | 0.1986 | 0.2098 | 0.2389 | 0.2549 |
| `Frame/Physics/Narrowphase` | 0.0004 | 0.0033 | 0.0073 | 0.0238 | 0.0010 |
| Inclusive solver owner (`PersistentContacts`) | 0.0067 | 0.0309 | 0.0570 | 0.1373 | 0.0027 |
| Awake / total bodies | 200 / 200 | 520 / 520 | 1,000 / 1,000 | 2,000 / 2,000 | 1,000 / 5,000 |
| Bodies reinserted this step | 24 | 63 | 121 | 233 | 142 |
| Estimated hot bytes/body/step | 245.0 | 245.0 | 245.0 | 245.0 | 95.4 |

Final `tools\validate_perf.bat` passed its allocation guard, analyzer self-test,
structural probe, budgets, and all five scale captures. Final
`tools\validate_physics.bat` built Profile and Debug with zero warnings/errors
and matched the 44,401-line varied oracle byte-for-byte. Final
`tools\validate_full.bat` passed formatting (276 headers), all 318 doctest cases,
every coverage floor, interaction/parser/DX12 architecture suites, Automation
replay/prediction smoke, three zero-error DX12 baseline comparisons, and exact
physics determinism. P2's touched-source comment audit covers 7/7 files with
zero deferred; see `../2026-07-19/physics-body-count-p2-comment-audit.md`.

## P3 Zero-Cost Sleepers

P3 gives `PhysicsSleepController` ownership of a fixed-capacity ascending dense
awake list plus reverse positions. Transition sites update it incrementally;
topology, replay, configuration, and same-count authored replacement are cold
rebuild boundaries. Parallel tornado/narrowphase wakes atomically select one
winner, publish its body index into fixed storage, and let the frame sequencer
flush publications after worker barriers. This preserves the original same-step
force behavior without exposing list mutation to worker scheduling.

Force, integration, terrain, and broadphase maintenance now consume the awake
span. Sleepers remain in persistent grid membership, so awake movers still
discover and wake them. Per-frame pair-source cell stamps restrict production
candidate traversal to awake-reachable cells. Sleep/sleep pairs are rejected at
emission and the old `PruneSleepPairs` pass and marker are deleted. Debug retains
a geometry-only admission predicate and canonically merges solver-visible plus
sleep-pruned pairs when recording `BroadphaseCandidate`; therefore its observable
pre-P3 diagnostic stream remains exact while Profile/Release do no restored work.

The first deep-gate attempt exposed the diagnostic relocation as a SkullScope
query-packet mismatch. A baseline refresh was not authorized, so the generated
candidate was discarded, the Debug diagnostic contract was restored in source,
and the original committed query baseline subsequently matched exactly. A
separate harness-only issue occurred when `Start-Process -WindowStyle Hidden`
created a DX12 window at 0 x 0; the PID-scoped matrix was rerun in visible
consoles and completed. Neither issue is a remaining product blocker.

### P3 Worker-Count Determinism

The final Debug source reproduced every retained P1 witness byte-for-byte at
worker counts 0, 1, and 4 across all six scenes: 18/18 processes in 1,473.56
seconds. Scale scenes used 600 frames and `physics_bench_varied` used 1,200.

| Scene | Bytes | SHA-256 |
|---|---:|---|
| scale_200 | 33,831,818 | `3DAD84BE52C8C7A9BCA029B11543E841E54B2F14DFE24E7FCF14D0CBE5BF522A` |
| scale_520 | 88,116,798 | `9B8F705074EE7158E7F8B6A8483D49E69632EA857613F83FC551A4FEA8499B5E` |
| scale_1000 | 169,580,528 | `F277D16226591F5B258C0D4C0F8BE54EAD52A9CE679EF49C03970305B4F8DE8C` |
| scale_2000 | 340,410,150 | `AA9D3BFD770ABB838D2EDBF836F33C110E4FBE730437C16739E7F7D847CD36DC` |
| sleepy_5000 | 912,830,920 | `67FDBC3D7A6FBDDD6FC15E0393A9DF137AA3777193EE8E9F564090C7B7174C76` |
| regression_varied | 12,660,434 | `8E9092CB7F28EAFC0D9F167E90CF9D5292D022485D6AE93D591FB758CAEA6387` |

### P3 Measurement Matrix

Profile P50 values are milliseconds from the final performance captures.
Sleepy-5,000 physics improves 6.0% from P2 (1.9813 to 1.8628 ms), but remains
above scale-1,000 because P4 still owns measured full-row and cache/bandwidth
work. The deleted prune pass records zero rather than hiding time elsewhere.

| Marker / counter | scale_200 | scale_520 | scale_1000 | scale_2000 | sleepy_5000 |
|---|---:|---:|---:|---:|---:|
| `Frame` | 0.4337 | 1.4449 | 2.0030 | 3.3085 | 4.0181 |
| `Frame/Physics` | 0.1094 | 0.8126 | 1.1484 | 2.0171 | 1.8628 |
| `Frame/Physics/Broadphase` | 0.0246 | 0.1583 | 0.3710 | 0.8555 | 0.3795 |
| `Frame/Physics/Broadphase/GridSetup` | 0.0002 | 0.0006 | 0.0008 | 0.0010 | 0.0006 |
| `Frame/Physics/Broadphase/GridMaintain` | 0.0179 | 0.1307 | 0.2914 | 0.5928 | 0.1746 |
| `Frame/Physics/Broadphase/CandidatePairs` | 0.0029 | 0.0123 | 0.0404 | 0.1515 | 0.1815 |
| `Frame/Physics/Broadphase/PruneSleepPairs` | deleted / 0 | deleted / 0 | deleted / 0 | deleted / 0 | deleted / 0 |
| `Frame/Physics/ApplyForces` | 0.0260 | 0.1746 | 0.1802 | 0.1905 | 0.1973 |
| `Frame/Physics/Terrain/Detect` | 0.0176 | 0.1987 | 0.2229 | 0.2738 | 0.2340 |
| `Frame/Physics/Integrate` | 0.0215 | 0.1956 | 0.2090 | 0.2388 | 0.2438 |
| `Frame/Physics/Narrowphase` | 0.0003 | 0.0032 | 0.0075 | 0.0236 | 0.0011 |
| Inclusive solver owner (`PersistentContacts`) | 0.0066 | 0.0304 | 0.0568 | 0.1347 | 0.0027 |
| Awake / total bodies | 200 / 200 | 520 / 520 | 1,000 / 1,000 | 2,000 / 2,000 | 1,000 / 5,000 |
| Bodies reinserted this step | 24 | 63 | 121 | 233 | 142 |
| Estimated hot bytes/body/step | 245.0 | 245.0 | 245.0 | 245.0 | 95.4 |

Final `tools\validate_tests.bat` passed 321/321 cases and 30,365/30,365
assertions, including the Legacy-default and single-active-UI startup contract.
Final `tools\validate_physics.bat`, `tools\validate_physics_deep.bat`, and
`tools\validate_perf.bat` passed with zero-warning Profile/Debug builds, the
44,401-line byte-exact oracle, the unchanged query packet, zero steady-gameplay
allocations/reserve violations, and passing scale budgets. The final one-frame
`--platform-profiler-markers` launch exited 0 in 3.573 seconds. P3's
touched-source comment audit covers 20/20 files with zero deferred; see
`../2026-07-19/physics-body-count-p3-comment-audit.md`.

### P3 SkullScope Cost Accounting

Trace command:

```text
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_bench_varied.scene.json --physics-diag C:\SkullbonezCore\Debug\physics_query_varied.physicsdiag.ndjson
```

The trace artifact is 103,785,259 bytes and its SQLite cache is 50,823,168
bytes. Each query used this command prefix:

```text
C:\Users\sesch\AppData\Local\Programs\Python\Python312\python.exe C:\SkullbonezCore\tools\physics_query.py C:\SkullbonezCore\Debug\physics_query_varied.physicsdiag.ndjson
```

| # | Query arguments | Output bytes/chars |
|---:|---|---:|
| 1 | `summary --limit 8` | 8,894 |
| 2 | `events --limit 20` | 21,806 |
| 3 | `contact-audio-summary --limit 8` | 9,400 |
| 4 | `contact-audio-events --limit 8` | 4,597 |
| 5 | `contact-audio-rejections --reason propagated_impulse --limit 8` | 393 |
| 6 | `contact-audio-body --body roll_a --limit 8` | 5,588 |
| 7 | `contact-audio-timeline --limit 8` | 2,047 |
| 8 | `frame 600 --limit 8` | 8,624 |
| 9 | `body roll_a --frames 0:1200 --limit 12` | 8,433 |
| 10 | `energy --frames 0:1200 --limit 12` | 6,420 |
| 11 | `events --type penetration_sustained,penetration_growing --limit 20` | 310 |
| 12 | `contacts --top penetration --limit 12` | 5,948 |
| 13 | `island 1 --frame 1199 --limit 12` | 715 |
| 14 | `stacks --frames 0:1200 --limit 12` | 3,803 |
| 15 | `rolling --frames 0:1200 --limit 12` | 7,442 |
| 16 | `broadphase --frames 0:1200 --limit 12` | 3,036 |
| 17 | `solver --frames 0:1200 --limit 12` | 3,531 |
| 18 | `pipeline --frames 0:1200 --limit 12` | 5,276 |
| 19 | `questions penetration_spikes` | 1,454 |
| 20 | `questions stack_health` | 1,354 |
| 21 | `compare C:\SkullbonezCore\Debug\physics_query_varied.physicsdiag.ndjson --limit 8` | 943 |

Total bounded query output was 110,014 UTF-8 bytes/characters. The harness
captured raw query JSON without exposing it to the model, so GPT-read raw query
output was 0 bytes; only the bounded size accounting above was inspected.

## P4 Hot-State Bandwidth Diet

P4 moved steady bookkeeping from the total body count to the sleep owner's
ascending awake set without changing simulation arithmetic or persisted state.
The accepted changes are:

- Same-timestep `m_timeRemaining` reset writes awake rows only. Topology/count
  or exact timestep changes retain the former all-row initialization as a cold
  replay/diagnostic boundary.
- `PhysicsSleepController` imports and exports body-store sleep flags only when
  topology, replay, or configuration invalidates its derived awake index.
  Ordinary fixed steps retain the controller-owned sleep rows directly.
- Sleep eligibility, quiet-counter advancement, can-sleep checks, and final
  transitions walk the ascending awake set. The final transition cursor remains
  on a compacted slot so consecutive sleepers cannot be skipped.
- Dormant underwater sleepers are scanned only after a cold awake-list rebuild,
  an explicit sleep seed, or an exact fluid-surface-height change. An ordinary
  sleep transition probes and locks its exact body immediately.

The production mutation audit found every external sleep change already crosses
an explicit synchronization boundary: scene wake/velocity/seed commands mirror
the store immediately, sleep enablement mirrors immediately, and topology,
editor, and replay mutation invalidate the derived index. No parallel worker
mutates the sorted list.

Three candidate moves were rejected:

- Force accumulation and final integration are not adjacent. Tornado,
  broadphase, narrowphase, terrain, persistent-contact, joint, and support work
  intervene and may change wake, velocity, or remaining-time state. Fusing
  across them would change operation order.
- Mutual gravity keeps its triangular pair scratch and canonical serial
  reduction because regrouping additions would violate the exact-sum contract.
- No cold-store split was accepted without a measured cold-only replacement.
  Moving a field while retaining the same hot-loop read is relocation, not a
  bandwidth reduction.

### P4 Logical Byte Accounting

P3 charged `58 all + 81 force-awake + 106 integrate-awake` logical bytes. P4
moves 16 bytes of sleep/CCD guard traffic from every row to the awake set, so
the model becomes `42 all + 16 awake-bookkeeping + 81 + 106`.

| Fixture | P3 bytes/body/step | P4 bytes/body/step | Delta |
|---|---:|---:|---:|
| All awake | 245.0 | 245.0 | 0.0 (the 16 bytes move from all-row to awake-row) |
| 1,000 awake / 5,000 total | 95.4 | 82.6 | -12.8 (-13.42%) |

No pass fusion was accepted, so there is no fusion-specific byte delta to
report. The accepted compaction group is exactly the 16-byte accounting move
above.

### P4 Measurement Matrix

Profile P50 values are milliseconds from the final `validate_perf` capture.
Sleepy-5,000 `Frame/Physics` falls from P3's 1.8628 ms to 1.3331 ms
(-28.44%), and total `Frame` falls from 4.0181 ms to 3.4816 ms (-13.35%).
All-awake physics deltas range from +4.84% at scale-200 to -2.60% at
scale-2,000 while their logical byte count remains exactly 245.0; those small
mixed movements are treated as capture noise, not an arithmetic optimization
claim.

| Marker / counter | scale_200 | scale_520 | scale_1000 | scale_2000 | sleepy_5000 |
|---|---:|---:|---:|---:|---:|
| `Frame` | 0.4545 | 1.4850 | 2.0373 | 3.2728 | 3.4816 |
| `Frame/Physics` | 0.1147 | 0.8379 | 1.1629 | 1.9646 | 1.3331 |
| `Frame/Physics/Broadphase` | 0.0279 | 0.1669 | 0.3691 | 0.8367 | 0.3490 |
| `Frame/Physics/Broadphase/GridSetup` | 0.0002 | 0.0007 | 0.0008 | 0.0011 | 0.0007 |
| `Frame/Physics/Broadphase/GridMaintain` | 0.0217 | 0.1382 | 0.2893 | 0.5618 | 0.1802 |
| `Frame/Physics/Broadphase/CandidatePairs` | 0.0029 | 0.0123 | 0.0402 | 0.1467 | 0.1682 |
| `Frame/Physics/Broadphase/PruneSleepPairs` | deleted / 0 | deleted / 0 | deleted / 0 | deleted / 0 | deleted / 0 |
| `Frame/Physics/ApplyForces` | 0.0268 | 0.1801 | 0.1886 | 0.1961 | 0.1856 |
| `Frame/Physics/Terrain/Detect` | 0.0185 | 0.2035 | 0.2264 | 0.2777 | 0.2331 |
| `Frame/Physics/Integrate` | 0.0217 | 0.1990 | 0.2130 | 0.2355 | 0.2311 |
| `Frame/Physics/Narrowphase` | 0.0004 | 0.0032 | 0.0079 | 0.0238 | 0.0010 |
| Inclusive solver owner (`PersistentContacts`) | 0.0075 | 0.0306 | 0.0566 | 0.1361 | 0.0026 |
| Awake / total bodies | 200 / 200 | 520 / 520 | 1,000 / 1,000 | 2,000 / 2,000 | 1,000 / 5,000 |
| Bodies reinserted this step | 24 | 63 | 121 | 233 | 142 |
| Estimated hot bytes/body/step | 245.0 | 245.0 | 245.0 | 245.0 | 82.6 |

### P4 Worker-Count Determinism

The final Debug source reproduced every retained P1/P3 witness byte-for-byte at
worker counts 0, 1, and 4 across all six scenes: 18/18 processes in about
1,852 seconds (30m52s). Scale scenes used 600 frames and
`physics_bench_varied` used 1,200. Native byte comparison and SHA-256 matched
before each duplicate was deleted; retained witnesses were not modified.

| Scene | Bytes | SHA-256 |
|---|---:|---|
| scale_200 | 33,831,818 | `3DAD84BE52C8C7A9BCA029B11543E841E54B2F14DFE24E7FCF14D0CBE5BF522A` |
| scale_520 | 88,116,798 | `9B8F705074EE7158E7F8B6A8483D49E69632EA857613F83FC551A4FEA8499B5E` |
| scale_1000 | 169,580,528 | `F277D16226591F5B258C0D4C0F8BE54EAD52A9CE679EF49C03970305B4F8DE8C` |
| scale_2000 | 340,410,150 | `AA9D3BFD770ABB838D2EDBF836F33C110E4FBE730437C16739E7F7D847CD36DC` |
| sleepy_5000 | 912,830,920 | `67FDBC3D7A6FBDDD6FC15E0393A9DF137AA3777193EE8E9F564090C7B7174C76` |
| regression_varied | 12,660,434 | `8E9092CB7F28EAFC0D9F167E90CF9D5292D022485D6AE93D591FB758CAEA6387` |

### P4 Validation And Resolved Blockers

- Focused sleep tests pass 6/6 cases and 47/47 assertions. The first run exposed
  a fixture that seeded only controller state; the production cold-mirror
  contract correctly treats body-store authored state as authoritative. The
  fixture now seeds both production owners and the rerun is clean.
- `tools\validate_physics.bat` passes in about 91 seconds: standalone/runtime
  handle smokes pass, the 44,401-line varied CSV is byte-exact, and Profile plus
  Debug build with zero warnings/errors.
- `tools\validate_perf.bat` completes with passing absolute budgets, analyzer
  and structural checks, zero steady-gameplay allocation violations, and the
  five scale captures above.
- The first `tools\validate_full.bat` attempt stopped at format preflight on
  inherited P3 inline-comment alignment in `SpatialGrid.h`. Ten formatting-only
  lines were aligned; `validate_format` then passed all 276 headers. The full
  rerun passes 322 doctest cases/30,378 assertions, every coverage floor,
  interaction/parser/DX12 architecture tests, Automation replay/prediction,
  three zero-error DX12 baseline comparisons, and exact physics validation.
- The one-frame `--platform-profiler-markers` smoke exits 0 in 1.388 seconds.
  Legacy remains the default; the inactive ImGui context records zero frames
  and zero draws, and no UI source is changed.

No baseline, golden, scene, config, authored-data, or UI artifact changed. P4's
touched-source comment audit covers 6/6 files with zero deferred; see
`../2026-07-19/physics-body-count-p4-comment-audit.md`.

## P5 Automatic Solver-Tier Evidence Gate

P5 changes no runtime behavior. It consolidates the accepted P0-P4 attribution
and measures the two prescribed P4-tip witnesses twice before making the
standing numeric decision about P6.

### Consolidated P0-P4 Attribution

The table below keeps one accounting convention throughout: each value is that
task tip's standard Profile `Frame/Physics` P50 in milliseconds, and each delta
is relative to the immediately preceding task. Small all-awake movements are
capture-to-capture noise unless a changed child marker and implementation
boundary support attribution.

| Task tip | scale_200 | scale_520 | scale_1000 | scale_2000 | sleepy_5000 |
|---|---:|---:|---:|---:|---:|
| P0 | 0.1106 | 0.8486 | 1.0688 | 1.7852 | 2.1479 |
| P1 | 0.1066 (-0.0040) | 0.8405 (-0.0081) | 1.0828 (+0.0140) | 1.7407 (-0.0445) | 2.0722 (-0.0757) |
| P2 | 0.1050 (-0.0016) | 0.7722 (-0.0683) | 1.0299 (-0.0529) | 1.8161 (+0.0754) | 1.9813 (-0.0909) |
| P3 | 0.1094 (+0.0044) | 0.8126 (+0.0404) | 1.1484 (+0.1185) | 2.0171 (+0.2010) | 1.8628 (-0.1185) |
| P4 | 0.1147 (+0.0053) | 0.8379 (+0.0253) | 1.1629 (+0.0145) | 1.9646 (-0.0525) | 1.3331 (-0.5297) |
| P0 to P4 | +0.0041 | -0.0107 | +0.0941 | +0.1794 | -0.8148 |

- P1 established canonical pair order and its same-state oracle. It was a
  determinism transition, not a performance optimization; the small mixed
  deltas therefore establish the transitioned reference rather than a claimed
  speedup.
- P2 replaced full grid rebuilds with persistent incremental membership. On the
  sleepy witness `GridInsert`/`GridMaintain` moved from 0.5121 to 0.2130 ms and
  inclusive Broadphase moved from 0.7299 to 0.4930 ms. Scale-2,000's step P50
  rose 0.0754 ms despite its grid-maintenance reduction, so that whole-step
  movement was not attributed to the grid optimization.
- P3 kept sleepers resident but omitted their steady force, integration,
  terrain, and production pair work. Sleepy Broadphase moved from 0.4930 to
  0.3795 ms and the step moved down 0.1185 ms. The all-awake increases are
  recorded honestly and were not presented as sleeper-path gains.
- P4 moved sleep mirroring, CCD-clock reset, eligibility, counters,
  transitions, and ordinary underwater decisions onto the awake set. It made
  the largest measured sleepy-scene move: -0.5297 ms, alongside the logical
  byte estimate falling from 95.4 to 82.6 bytes/body/step. No unsafe fusion or
  unmeasured cold-store split was accepted.

Across P0-P4, sleepy-5,000 improves by 0.8148 ms (37.93%) while scale-2,000 is
0.1794 ms (10.05%) above its P0 capture. The campaign has therefore removed
substantial dormant-body work but has not hidden the remaining all-awake
solver opportunity that P5 measures directly.

### Exact P4-Tip Captures

`tools\validate_build.bat Profile` rebuilt the P4 tip in 12.52 seconds with
zero warnings and zero errors. Every analyzed CSV identifies commit
`f758a15b4`; the resulting Profile executable is 4,134,400 bytes with SHA-256
`35EC724AF36C0B038ECBB6E05F995CDBD0A161C4FF94B7167EE6CD5848D3B790`.

The scale command was executed twice, taking 8.462 and 7.122 seconds:

```text
Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step --shadows off --no-contact-audio --frames 600 --scene SkullbonezData\scenes\physics_scale_2000.scene.json
```

The deterministic 200-brick-wall command was executed twice, taking 5.134 and
5.127 seconds:

```text
Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step --shadows off --no-contact-audio --frames 600 --scene SkullbonezData\scenes\prediction_ragdoll_wall_200.scene.json
```

Each process produced 1,140 post-warmup samples. P50 values are milliseconds;
the trigger share is
`PersistentContacts P50 / Frame/Physics P50 * 100`.

| Witness repeat | `Frame/Physics` | `Narrowphase` | `PersistentContacts` | Solver share |
|---|---:|---:|---:|---:|
| scale-2,000 repeat 1 | 1.9507 | 0.0251 | 0.1386 | 7.1051% |
| scale-2,000 repeat 2 | 2.0626 | 0.0241 | 0.1353 | 6.5597% |
| wall-200 repeat 1 | 1.7811 | 0.1249 | 1.3230 | 74.2799% |
| wall-200 repeat 2 | 1.8026 | 0.1245 | 1.3405 | 74.3648% |

The ignored raw CSV witnesses are retained locally under
`TestOutput/p5_tier_gate/`. Their SHA-256 values are:

| Capture | Bytes | SHA-256 |
|---|---:|---|
| scale-2,000 repeat 1 | 762,283 | `82BD075E2EBF34FBAACEBC1A9CB6184D4BD35C17A9A4A754068471292DD20B9E` |
| scale-2,000 repeat 2 | 762,283 | `325FB162EA8C79A98298B54AD78CFE563B839F9E262F57E7B7D4FEF0080BC800` |
| wall-200 repeat 1 | 883,347 | `7CD5B919F569E1FC10480824498BD40FA7AFC8EC9EC024AC93C96B6F0C3C5064` |
| wall-200 repeat 2 | 883,347 | `D07B26055B3291D5DBB3AB99B933F5A68FD6531657CEF408C1942A2A44CA2ED1` |

The wall scene needed only a temporary `logging.perfLog` directive. Its Git
blob before and after capture is exactly
`407b718bff13ff0eab1a466e62aa5851f4d556ef` (SHA-256
`41EC952BA22FAE36E462B750D894909DD0EE58975554D696AEA16220C0F2C934`),
and the tracked diff is empty. No authored simulation value, baseline, golden,
config, coverage floor, or UI selection changed. Legacy remains the default and
the two UI surfaces remain mutually exclusive.

### Automatic Decision

P6 is automatically authorized. Both wall repeats independently exceed the
standing 15% threshold by a wide margin; no subjective bottleneck ruling or
additional owner response is needed. P6 must still satisfy its same-state
oracle, allocation/capacity, 0/1/4/8 determinism, artifact-authority, mapped
gate, and benefit requirements. In particular, its accepted four-worker result
must reduce wall `PersistentContacts` P50 by at least 10% from this P5 serial
reference without regressing `Frame/Physics` by more than 2% on either witness.

If P6 is reversed and closed as evidence-deferred under those fallback rules,
the future retry condition is a later upstream physics change followed by a
fresh two-repeat exact-tip capture of both witnesses; at least one witness must
again meet the 15% trigger before graph coloring is reconsidered. P5 itself is
documentation plus targeted evidence only, so no repository validation suite
is required.

## P6 Deterministic Solver-Coloring Decision (Evidence-Deferred)

P6 followed the transition protocol before any artifact move. The prototype
construction-reserved fixed scratch for at most 32,768 contact rows and 8,192
bodies, assigned the smallest available color in canonical row order, retained
canonical manifold construction, sorted-cache lookup, warm-start application,
delta reduction, early-out, trace emission, cache store, correction, and
writeback, and dispatched only conflict-free iterative row batches through
`WorkerPool::ParallelForNoAlloc`. A Debug same-state oracle checked canonical
row membership, exactly-once coverage, body conflict freedom, per-color
canonical order, and ascending color offsets. Its history-free focused fixture
passed 7/7 assertions. No baseline or replay artifact was changed.

The autonomous benefit/stability decision then rejected the prototype before
the separate point-joint graph was implemented. Each wall launch used this
shape with `<workers>` set to 1, 4, then 8:

```text
Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step --shadows off --no-contact-audio --workers <workers> --frames 600 --scene SkullbonezData\scenes\prediction_ragdoll_wall_200.scene.json
```

| Workers | Completion | Samples | `Frame/Physics` P50 | `PersistentContacts` P50 | Decision evidence |
|---:|---|---:|---:|---:|---|
| 1 | 600 frames, exit 0 | 1,140 | 2.6911 ms | 2.2096 ms | Complete colored-serial witness; already slower than P5 |
| 4 | Access violation `0xC0000005` at frame 264 | 234 | 4.3563 ms* | 3.8058 ms* | Stability/invariance failure |
| 8 | Access violation `0xC0000005` at frame 188 | 158 | N/A | N/A | Crashed before the active-wall interval |

`*` The four-worker values are the 65 available active-wall samples at frames
200-264, not a complete-run median. They are recorded only as rejection
evidence. The complete one-worker result alone misses the required benefit by a
wide margin: 2.2096 ms is 64.8% above the slower 1.3405 ms P5 serial reference,
where acceptance required at least 10% below that reference. Its 2.6911 ms
physics P50 is 49.3% above the slower 1.8026 ms P5 wall reference, where the
allowed regression ceiling was 2%.

The ignored raw probes remain local under `TestOutput/p6_coloring_probe/`:

| Capture | Bytes | SHA-256 |
|---|---:|---|
| workers 1 | 897,253 | `113C8F4932ADCC45FBCE2ABAB80E98DFBDBF23A01C3DFDBAF2C896D36C36999C` |
| workers 4 partial | 192,758 | `34D22CD3EAE311007FC8F1506A1E9434D6D41743FA37D36C8C3D75C5543D93A5` |
| workers 8 partial | 135,337 | `367A8C876F99C7EAE1B68FF6AA7DA43445E3616092CE21668763ED123CA23CC0` |

Per the binding fallback, every P6 source, focused-test, scene-logging, and
artifact edit was reversed without stash/reset/checkout. The tracked runtime
tree and wall scene are byte-identical to P5; the wall scene Git blob is again
`407b718bff13ff0eab1a466e62aa5851f4d556ef`. No physics/deep CSV, replay
golden, authored scene/config/schema, coverage floor, screenshot, UI default,
or UI coexistence behavior moved. Because no source remains touched, there is
no P6 retained-source comment-audit inventory. The final P5-behavior mapped
gates all passed from the restored tip:

| Gate | Approximate wall time | Restored-P5 result |
|---|---:|---|
| `tools\\validate_physics.bat` | 80 s | PASS; standalone/handle smokes and the 44,401-line varied baseline matched byte-exactly |
| `tools\\validate_physics_deep.bat` | 128 s | PASS; varied, bullet sweep, shooting, known-signature, and SkullScope query baselines matched exactly |
| `tools\\validate_perf.bat` | 109 s | PASS; allocation guard, selected-object path, DX12/physics perf, and scale matrix completed |
| `tools\\validate_replay_visual_fidelity.bat` | 275 s | PASS without refresh; 2,401 ticks, 200 moved bricks, 175 toppled bricks, one presented cascade, and every false-pass control behaved as expected |
| `tools\\validate_full.bat` | 170 s | PASS; 322/322 cases, 30,378/30,378 assertions, every coverage floor, zero DX12 validation errors, three passing image comparisons, and byte-exact physics |

The deep gate generated its established deterministic SkullScope trace and ran
the committed bounded query packet internally. The packet matched exactly; no
raw NDJSON, SQLite cache, or query text was exposed to the model during P6, so
P6 model-ingested SkullScope data is 0 bytes. The prototype's local perf logs
listed above were the only raw P6 artifacts inspected.

Future retry remains conditional: a later upstream physics change must be
followed by two fresh exact-tip captures of scale-2,000 and the deterministic
wall, and at least one witness must again put `PersistentContacts` at 15% or
more of `Frame/Physics` before graph coloring is reconsidered.

## P7 Final Matrix, Acceptance, And Closure

The owner explicitly accepted the deterministic sleeper/canonical-order
behavior and directed all affected baselines to be rewritten. P7 retained the
P1 canonical pair ordering, P2-P4 sleeper path, and P6 fallback. It added a
direct multithreaded determinism fixture and closed the one review-discovered
allocation-policy hole: every sleep-support-edge producer now shares one
fail-before-grow boundary with a fixed `MAX_SCENE_OBJECTS * 4` semantic cap,
actual reserved-capacity verification, high-water reporting, and gameplay-phase
Lane F diagnostics.

### Final Profile Matrix

These are the final post-format `tools\validate_perf.bat` captures from the
source and accepted perf baselines prepared for the closure commit. Values are
P50 milliseconds over 1,140 samples. `GridMaintain` is the P2 persistent-grid
replacement for the P0 `GridInsert` row.

| Marker / counter | scale_200 | scale_520 | scale_1000 | scale_2000 | sleepy_5000 |
|---|---:|---:|---:|---:|---:|
| `Frame` | 0.4227 | 1.4257 | 1.9172 | 3.1528 | 3.3160 |
| `Frame/Physics` | 0.1075 | 0.8021 | 1.0850 | 1.8878 | 1.3026 |
| `Frame/Physics/Broadphase` | 0.0235 | 0.1550 | 0.3416 | 0.8114 | 0.3448 |
| `Frame/Physics/Broadphase/GridMaintain` | 0.0171 | 0.1283 | 0.2657 | 0.5346 | 0.1733 |
| `Frame/Physics/Broadphase/CandidatePairs` | 0.0027 | 0.0122 | 0.0372 | 0.1424 | 0.1607 |
| `Frame/Physics/ApplyForces` | 0.0254 | 0.1725 | 0.1780 | 0.1971 | 0.1894 |
| `Frame/Physics/Terrain/Detect` | 0.0170 | 0.1975 | 0.2150 | 0.2797 | 0.2374 |
| `Frame/Physics/Integrate` | 0.0236 | 0.1920 | 0.2043 | 0.2405 | 0.2335 |
| `Frame/Physics/Narrowphase` | 0.0004 | 0.0031 | 0.0075 | 0.0244 | 0.0011 |
| Inclusive solver owner (`PersistentContacts`) | 0.0064 | 0.0304 | 0.0548 | 0.1315 | 0.0025 |
| Awake / total bodies | 200 / 200 | 520 / 520 | 1,000 / 1,000 | 2,000 / 2,000 | 1,000 / 5,000 |
| Bodies reinserted this step | 24 | 63 | 121 | 233 | 142 |
| Estimated hot bytes/body/step | 245.0 | 245.0 | 245.0 | 245.0 | 82.6 |

### Before-To-Final Physics Result

| Scene | P0 `Frame/Physics` P50 | P7 final P50 | Delta | Result |
|---|---:|---:|---:|---|
| scale_200 | 0.1106 ms | 0.1075 ms | -2.80% | faster |
| scale_520 | 0.8486 ms | 0.8021 ms | -5.48% | faster |
| scale_1000 | 1.0688 ms | 1.0850 ms | +1.52% | slower |
| scale_2000 | 1.7852 ms | 1.8878 ms | +5.75% | slower |
| sleepy_5000 | 2.1479 ms | 1.3026 ms | -39.36% | faster |

The sleeping-heavy scene now costs only 20.06% more than the all-awake
scale-1,000 step despite storing four thousand additional dormant bodies. P0's
equivalent overhead was 100.96%, so dormant-store overhead fell by 80.90
percentage points. The trade is explicit: the final all-awake scale-1,000 and
scale-2,000 captures are 1.52% and 5.75% slower than P0, while sleeping-heavy
physics is 39.36% faster. The owner accepted that trade.

The ratified at-least-4,000-awake target is missed. A dedicated no-sleep launch
of `physics_scale_sleepy_5000.scene.json` held 5,000/5,000 bodies awake and
measured `Frame/Physics` P50 at 3.8445 ms, not approximately 1 ms. The sleeping-
heavy scaling goal is met: 1,000 awake rows inside the 5,000-row store measure
1.3026 ms and 82.6 logical hot bytes/body/step.

### 200-Block Scene End State

The final 60 frames (2341-2400) of both deterministic repeats give the direct
end-of-scene comparison:

| Version | `Frame/Physics` P50 | Awake bodies | End-state explanation |
|---|---:|---:|---|
| P0 | 0.1021 ms | 0 | striker remained over finite terrain and slept at frame 2140 |
| P7 final | 0.0278 ms | 1 | striker crossed terrain x=1000 around frame 1719 and continued unsupported freefall |

The final end block is 0.0743 ms faster, a 72.77% reduction or 3.67x speedup.
The one awake body is `prediction_striker_ball`; it has no contact or support
and is physically correct to remain awake. Canonical `(min,max)` pair ordering
is the correct determinism contract. Neither evolved order is intrinsically a
more accurate physics model; the tiny order-dependent velocity sign changes
which side of the finite terrain boundary the striker reaches.

### Determinism Proof

The new doctest fixture creates 520 bodies and 256 overlapping pairs, proves
that the parallel dispatch thresholds are crossed, exercises a sleep
transition, and compares byte-exact final state at 0, 1, and 4 workers. The
separate six-scene process matrix also passed all 18 processes exactly:

```text
PASS processes=18 exact=18 scenes=6 workers=0,1,4 total_seconds=1,457.858
```

That matrix executable was built from retained implementation commit
`8bc929bfe` immediately before the two format-only corrections. Those changes
altered only declaration wrapping and continuation indentation; the final full
and deep gates rebuilt the corrected source and passed.

This certifies algorithmic and multithreaded determinism for the tested engine
paths. It does not certify cross-platform/cross-toolchain determinism, and the
engine has snapshot/replay facilities rather than rollback-resimulation
determinism.

### Accepted Baselines

- All physics baseline writers ran from the final Debug executable. The varied
  CSV, known-issue signatures, and 21-query SkullScope JSON regenerated to their
  existing bytes: SHA-256 `DC273C8D...AFE9`, `44E29849...DA9F`, and
  `5D503506...0284`, respectively.
- `dx12_perf.json` and `physics_bench_perf.json` were refreshed from commit
  `8bc929bfe`; the approved baseline commit changed from `45e1222d`.
- The authoritative replay run retained all behavioral fields and changed only
  final-source provenance: working/capture commits now name full commit
  `8bc929bfe8d7d545011c4aa14d2c1ebb5b2c4f82`, and the causal file derives its
  `visualBaselineSha256` from the refreshed visual file
  (`a3db2039...d3dc`).

### Review, Audit, And Final Gates

The independent reviewer found one blocking issue: raw support-edge vector
appends could grow in steady gameplay. The shared capped append/validation
boundary above resolved it; the review then closed with no blocking findings.
The 9/9 touched-source comment audit has zero deferred files and is recorded in
`../2026-07-20/physics-body-count-p7-comment-audit.md`.

The first final `validate_full` attempt stopped after 7.163 seconds on two
format-only findings (`SleepIslandSystem.h` declaration wrapping and one
`PhysicsSleepController.cpp` continuation indent). Both were corrected without
changing control flow, data, or ordering. Final-source gates then passed:

| Gate | Wall time | Result |
|---|---:|---|
| `tools\validate_full.bat` | 174.573 s | PASS; 323/323 cases, 61,096 assertions, all coverage floors, Automation replay/prediction smoke, zero DX12 validation errors, three image baselines, byte-exact physics |
| `tools\validate_physics_deep.bat` | 138.033 s | PASS; all six CSVs, known signatures, shooting reactions, and 21-query SkullScope baseline exact |
| `tools\validate_perf.bat` | 104.668 s | PASS; absolute budgets, structural/allocation checks, accepted perf baselines, scale matrix, zero-warning Profile/Debug builds |
| `tools\validate_replay_visual_fidelity.bat` | 438.261 s | PASS; 2,401 ticks, 200 moved, 175 toppled, one presentation, every false-pass control |
| `tools\run_graphics_stress.bat 1` | 61.808 s | PASS; full bounded minute, no crash, PID-scoped timeout shutdown |
| one-frame `--platform-profiler-markers` | 1.391 s | PASS; marker emission enabled and clean exit |

The deep gate generated
`Debug\physics_query_varied.physicsdiag.ndjson` (103,785,259 bytes) and its
SQLite cache (50,823,168 bytes). It internally executed the committed 21-query
packet and compared the result exactly. No query output text was exposed to the
model, so GPT-read query data is 0 bytes total (0 bytes per query); raw trace
and cache sizes are disk artifacts, not model-ingested data.
