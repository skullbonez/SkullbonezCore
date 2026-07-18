# Physics Body-Count Scale Measurements

Date: 2026-07-18

Campaign: `physics-body-count-scale-campaign`

Branch: `nightrunner-18th-july`

Task boundary: P0 complete; P1 implementation and deterministic evidence
complete, artifact closure owner-gated

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

Two gate-owned artifacts remain explicitly owner-gated and are not modified:

- the one-process 200-box replay run passed its report (`ok=1`) but the golden
  comparison changed only `causal.topologyCount` from 199 to 200; and
- the varied SkullScope query result changed as a mechanical consequence of the
  transitioned varied CSV (including `supported_rows` 617 to 621), but P1's
  current policy says “no other baseline class.”

P1 cannot close until the owner explicitly approves the replay-golden instance
and authorizes the varied query artifact as part of this transition (or gives a
different plan-compliant resolution). No unauthorized artifact was refreshed.

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
