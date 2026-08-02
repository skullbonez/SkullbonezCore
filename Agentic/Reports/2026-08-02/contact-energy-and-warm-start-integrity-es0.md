# Contact Energy And Warm-Start Integrity — ES0

Date: 2026-08-02
Branch: `nightrunner-1st-AUG-26`
Measured source tip: `d595a613`
Scope: ES0 only; no production source, tracked scene, or baseline changed

## Result

The authoritative solver has no settled height in the required 4/8/16/32/64/
128 sweep. Four and eight levels complete 2,400 frames but never sleep. Eight
levels exceeds the locked scene-energy envelope and reaches `9.878548` upward
speed. Sixteen levels becomes unstable and exhausts the scene-committed
candidate-pair list after frame 858. Thirty-two and 64 levels reach only frame
37 before the same fatal. The 128-level scene exhausts the committed spatial-
grid entry reservation before frame zero.

The existing four-brick defect reproduces exactly at 566 meaningful vertical-
velocity reversals and 900/900 measured frames at the 12-iteration cap. The
6,800-frame 200-box impact completes deterministically, but the striker leaves
the terrain, reaches y `-28683.777344` and speed `1355.243376`, so 210/211
dynamic bodies sleep and the scene never fully settles. Its peak mechanical
gain, `127.770669`, exceeds the locked `57.608805` scene tolerance.

These results do not lower or widen the plan's 32/64 acceptance target. They
replace its pre-implementation forecast with executable current-state evidence.

## Temporary Tower Contract

The ignored sweep scenes vary only dynamic level count:

- levels: 4, 8, 16, 32, 64, and 128;
- one fixed foundation at x/z `492`, half-extents `[12, 2.9, 12]`;
- centered dynamic slabs, half-extents `[10, 2.9, 10]`, mass `30`, restitution
  `0.08`, 5.88-unit vertical pitch, no rotation or disturbance;
- gravity `-50`, fluid disabled, fixed step, production solver settings and the
  unchanged 12-iteration cap; and
- 2,400 frames (40 simulated seconds).

The x/z center and slab thickness keep the resting fixture inside the current
24-unit broadphase-cell occupancy contract so serial contact depth is the
intended scaling variable. Two rejected temporary layouts demonstrated why this
matters: thin slabs at world x/z 500 straddled grid boundaries, and relocating
those same thin slabs still overfilled y-cell locality. Neither rejected layout
contributes to the acceptance measurements below.

Trace command template, run twice per height:

```powershell
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step `
  --shadows off --hide-top-text --automation-hidden-window --frames 2400 `
  --scene TestOutput/contact_energy_es0/tower_<N>.scene.json `
  --physics-diag TestOutput/contact_energy_es0/tower_<N>_<run>.physicsdiag.ndjson `
  --physics-regression-log TestOutput/contact_energy_es0/tower_<N>_<run>.csv
```

## Tower Measurements

Mechanical energy is per-frame kinetic energy plus `mass × 50 × y` for dynamic
bodies. “Contact +Σ” is the sum of positive frame-to-frame mechanical deltas on
frames with contacts; it is attribution evidence, not an allowance. A fatal row
reports the last complete diagnostic frame rather than pretending the scene had
a final topology.

| Levels | Outcome / last frame | Peak kinetic | Initial mechanical | Peak gain | Max contact gain / +Σ | Max up speed / height gain | Max penetration | Iterations at cap | Cache hit / miss | Final sleep / support / islands |
|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 4 | complete / 2399 | 350.117227 | 88,189.586168 | 0 | 334.819335 / 19,915.301351 | 3.199648 / 0 | 0.033712 | 2,395 | 17,814 / 4,887 | 0 / 4 / 1 |
| 8 | complete / 2399 | 5,733.402193 | 317,499.172336 | **2,768.690920** | 3,234.823157 / 132,025.320992 | **9.878548** / 0.545108 | 0.196192 | 2,394 | 36,527 / 11,141 | 0 / 8 / 1 |
| 16 | candidate-list fatal / 858 | 12,379.731543 | 1,199,478.343172 | 0 before fatal | 5,961.047501 / 198,531.328234 | 13.757618 / 0.148636 | 0.829965 | 855 | 28,930 / 8,519 | no final topology |
| 32 | candidate-list fatal / 37 | 36,320.843754 | 4,656,876.570844 | 0 before fatal | 3,417.095434 / 4,752.131544 | 6.129463 / 0 | 0.200018 | 34 | 1,087 / 461 | no final topology |
| 64 | candidate-list fatal / 37 | 151,723.447840 | 18,345,432.927188 | 0 before fatal | 3,333.534306 / 4,584.846412 | 6.129463 / 0 | 0.200018 | 34 | 1,087 / 461 | no final topology |
| 128 | spatial-entry fatal / before frame 0 | unavailable | unavailable | unavailable | unavailable | unavailable | unavailable | unavailable | unavailable | no frame emitted |

Post-frame-300 downward-to-upward crossings through the existing
`-0.01/+0.01` dead band are 1,369 across 4/4 bodies at four levels, 1,988
across 8/8 at eight levels, and 886 across 16/16 before its fatal. None of the
completed heights ever puts one dynamic body to sleep.

The exact current capacity failures are:

```text
16: Canonical candidate list exhausted; capacity/high-water 68.
32: Canonical candidate list exhausted; capacity/high-water 132.
64: Canonical candidate list exhausted; capacity/high-water 260.
128: SpatialGrid.entries requested 2057 with runtime capacity/high-water 2056.
```

The 128-level fatal opens a modal MSVC assertion dialog. After its exact stderr
message was flushed, the harness closed only that measured process so the repeat
could run. Both repeats produced the same stderr hash.

## Four-Brick Reproduction

Exact command, run twice:

```powershell
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step `
  --shadows off --hide-top-text --automation-hidden-window --frames 1200 `
  --scene SkullbonezData/scenes/box_vibration_t0.scene.json `
  --physics-diag TestOutput/contact_energy_es0/bv0_<run>.physicsdiag.ndjson `
  --physics-regression-log TestOutput/contact_energy_es0/bv0_<run>.csv
```

Frames 300-1199 reproduce the locked BV0 row exactly: 566 flips, 4/4 affected
bricks, 181 worst-brick flips, 900/900 frames at 12 iterations, 2,169 misses in
8,465 cache lookups, and maximum absolute/upward vertical speed `3.914273`.
All four dynamic bricks are supported and none sleeps at frame 1199.

Initial mechanical energy is `44,690.558168`; the trace never exceeds it.
Peak kinetic energy is `391.070102`, maximum contact-frame gain is `409.621312`,
positive contact-delta sum is `10,872.769418`, and maximum penetration is
`0.033819`. Energy non-gain alone therefore cannot substitute for the launch,
cache, convergence, support, and sleep requirements.

## 200-Box Impact

Exact command, run twice:

```powershell
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step `
  --shadows off --hide-top-text --automation-hidden-window --frames 6800 `
  --scene SkullbonezData/scenes/prediction_ragdoll_wall_200.scene.json `
  --physics-diag TestOutput/contact_energy_es0/wall200_<run>.physicsdiag.ndjson `
  --physics-regression-log TestOutput/contact_energy_es0/wall200_<run>.csv
```

The scene contains 211 dynamic bodies after ragdoll expansion. It records:

- initial mechanical energy `3,775,450.653466`, peak `3,775,578.424135`, and
  unexplained peak gain `127.770669`;
- peak kinetic energy `151,549,008.829324`, maximum contact-frame gain
  `260.365477`, and positive contact-delta sum `2,020.146289`;
- maximum upward speed after frame 300 `12.125853`, 7,482 meaningful vertical
  reversals across 201 bodies, and maximum height gain `2.697791`;
- maximum penetration `2.022036`, 978 peak / 80.452 average contacts, 1,679
  cap-bound frames, and 436,671 cache hits / 84,940 misses; and
- final 210/211 sleeping, 10 supported, 131 dynamic islands, with the striker
  still awake at y `-28683.777344` and speed `1355.243376`.

The current wall is deterministic but fails energy, launch, body-retention, and
complete-settling semantics. No chaotic final pose is being frozen.

## Deterministic Repeat Proof

Every completed or partial trace/CSV pair is byte-identical across two runs.
Fatal stderr is also identical.

| Workload | Trace SHA-256 | CSV SHA-256 | Fatal stderr SHA-256 |
|---|---|---|---|
| four-brick | `3a226f72cf56da6b95327bd41637c64c4fd82a8c2185ef85c80465d75cd03524` | `e3012237092a008ba058cd775e38bdf3ffd0370100875ffc7f9c7c7da2661464` | none |
| tower 4 | `2124f1838a2f67c9cdabe6b374c92d3cb2ac212d6f1fc9e92b96dce9a43fff73` | `67ff54c0281cbff8421a5041a97a34fcf36abc336e856a996e2191c54aeca951` | none |
| tower 8 | `0c84736799302d93a4afcb2d66a5997eb292e94ebd2eae2216798fca1f7de11a` | `b62c081572b55d1e81dae0bba85f5cfb4de0e4b6268d4bc0f35d58ff4b5bb53c` | none |
| tower 16 | `9327711aec4abf6cc311861ed0000c6716040d96e7a1dbdb7d20b46fe0ec422c` | `b52f24641324afad7d2488f49d859f976bfeb1f4f6d159a541b4ce15775fc88e` | `a587f7428dbef3a70e56da054bfb3081ff9eb32cb6c70784cfc63d6ac3ce8138` |
| tower 32 | `fc40b62c5729856332c3a25a01c693c9a84cffe8761a2d1b94d802c9fbf13486` | `b3d04c47ea5beb807517f8ea482a0ea23d5460aa4e59a95a92276c66efd5c847` | `02ba282561f414b4604bee6800547c8057a88bebfeeeb4577563645ff2f63bd43` |
| tower 64 | `4cca29d6a2cf2b6bd7a3c7005ad238217f3b580b93cf6b8d6ca4c77220d84f79` | `fd9043085b8afc7845ef7e5d962097c0429eb20dde29bc2ebeea782273171bd7` | `8b4f855d8e08e92001cb654c9df1501cc0d6224884efe04f6d86ec375c1ccb41` |
| tower 128 | empty before frame zero | none | `5104def7f87a84ec852799ea66c950e84308b039a0f98546fd77543e8e1a4963` |
| wall 200 | `1903f0d91f240448d86f9d160bcd1813e8d7a1cd2414c75705bc94e2a35a42eb` | `7d063922107658102d1c8bfd7e35f77a92adffb2a3f7fd7608a3a95be7c44437` | none |

## Locked Acceptance Envelope

ES1/ES2 must encode these values without weakening them after solver edits:

1. Closed, zero-bias solve energy tolerance:
   `max(1e-6, 64 * FLT_EPSILON * max(E_before, 1))`.
2. Linear-momentum component tolerance uses the same 64-epsilon factor against
   `max(sum(mass * abs(component velocity)), 1)`. Angular momentum uses the
   corresponding `r × m v + I_world omega` magnitude scale.
3. A biased solve passes only when
   `E_after <= E_before + explicit_separation_work + max(1e-6,
   128 * FLT_EPSILON * scale)`. Restitution, friction, cached impulse reuse,
   synthetic terrain support, and positional repair must remain separately
   attributable; none is folded into tolerance.
4. Gravity scenes use
   `max(0.05, 128 * FLT_EPSILON * abs(initial_mechanical))`. The resulting
   current bounds are `0.681924` four-brick, `1.345666` tower 4, `4.844653`
   tower 8, `18.302587` tower 16, `71.058297` tower 32, `279.929091` tower 64,
   and `57.608805` wall 200.
5. A settled tower has zero post-frame-300 crossings through the existing
   `-0.01/+0.01` velocity dead band, no post-frame-300 upward speed above
   `0.25`, maximum penetration at most `0.05`, zero cache misses in its final
   300 frames, all dynamic bodies supported, and all dynamic bodies permanently
   asleep by frame 2400. The solver settings and 12-iteration cap do not change.
6. Towers 32 and 64 must complete and satisfy every requirement. Tower 128 is
   measured honestly but remains a stretch. Capacity failure is not an accepted
   topology.
7. The 200-box scene must stay finite, retain every body above its physical
   terrain support, remain inside the scene-energy envelope, have no repeated
   upward-launch cycle, and put all 211 dynamic bodies permanently to sleep by
   frame 6800. Its exact chaotic pose is deliberately unconstrained.
8. Every required trace and semantic result must repeat exactly and across the
   later worker-count witnesses.

The ignored control probe uses two equal unit masses. Exact elastic restitution
preserves energy `1.0` inside the `7.62939453125e-6` closed tolerance. Both an
over-restitution result and a 1% oversized normal impulse produce `1.0201` and
fail. A clean scene-roundoff sample at half the scene tolerance passes, while a
two-tolerance planted injection fails. All five sensitivity checks pass; the
probe JSON SHA-256 is
`2fe94a6d82f9a3c9730870eb6cbd274fa075374c653c4898e6f889df5c899a22`.

## SkullScope Accounting

Raw traces and SQLite caches were never read by the model. Primary artifacts:

| Trace | NDJSON bytes | SQLite bytes |
|---|---:|---:|
| `bv0_g` | 18,823,863 | 8,249,344 |
| `tower_4_g` | 37,856,676 | 15,818,752 |
| `tower_8_g` | 57,326,322 | 25,546,752 |
| `tower_16_g` | 35,589,431 | 16,650,240 |
| `tower_32_g` | 2,099,135 | 1,216,512 |
| `tower_64_g` | 3,269,091 | 1,675,264 |
| `wall200_g` | 1,579,164,069 | 815,837,184 |

Every model-facing command used the read-only SQL escape hatch and `--limit`:

```powershell
tools\physics_query.bat TestOutput/contact_energy_es0/bv0_g.physicsdiag.ndjson sql "<exact BV0 flip/cache query>" --limit 5
tools\physics_query.bat TestOutput/contact_energy_es0/bv0_g.physicsdiag.ndjson sql "<BV0 energy query>" --limit 5
tools\physics_query.bat TestOutput/contact_energy_es0/tower_4_g.physicsdiag.ndjson sql "<mechanical range probe>" --limit 5
tools\physics_query.bat TestOutput/contact_energy_es0/tower_<4|8|16|32|64>_g.physicsdiag.ndjson sql "<tower metrics query with matching level>" --limit 5
tools\physics_query.bat TestOutput/contact_energy_es0/tower_<4|8|16>_g.physicsdiag.ndjson sql "<post-frame-300 flip query>" --limit 5
tools\physics_query.bat TestOutput/contact_energy_es0/wall200_g.physicsdiag.ndjson sql "<wall metrics query>" --limit 5
tools\physics_query.bat TestOutput/contact_energy_es0/wall200_g.physicsdiag.ndjson sql "<final-lowest-body query>" --limit 10
tools\physics_query.bat TestOutput/contact_energy_es0/wall200_g.physicsdiag.ndjson sql "<per-body extrema query>" --limit 10
tools\physics_query.bat TestOutput/contact_energy_es0/wall200_g.physicsdiag.ndjson sql "<post-frame-300 flip query>" --limit 5
```

Per-query model-read characters / retained output-file bytes were: BV0 metric
620/1,242; BV0 energy 742/1,486; tower-4 range 408/818; tower metrics
1,491/2,984, 1,508/3,018, 1,505/3,012, 1,494/2,990, and 1,496/2,994;
tower flip queries 447/896, 447/896, and 450/902; wall metrics 1,500/3,002;
wall final-lowest 1,809/3,620; wall extrema 1,969/3,940; and wall flips
449/900. `Tee-Object` retained UTF-16LE files, hence the byte/character
difference.

Total GPT-read SkullScope output: **16,335 characters**. No successful query
was truncated. One initial multiline wrapper invocation returned no query JSON
because the batch boundary split the statement; it was replaced by the exact
single-line commands above and contributed no trace data. Raw NDJSON, SQLite,
and regression CSV artifacts were not read by the model.

## Validation And Change Scope

ES0 is documentation-only in the tracked repository. The ignored generator,
scenes, controls, traces, caches, query packets, logs, and CSVs remain under
`TestOutput/contact_energy_es0/`. No production source, tracked scene, test,
configuration, baseline, or golden changed, so repository validation is not
required for this phase. `git diff --check` is the pre-commit hygiene gate.

No rubber-duck review is appropriate for this incremental pre-edit census; the
plan requires its independent review at ES6 after implementation and complete
evidence exist.
