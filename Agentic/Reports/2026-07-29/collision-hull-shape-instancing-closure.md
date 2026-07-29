# Collision Hull Shape Instancing Closure Evidence

Date: 2026-07-29  
Plan: archived under ledger rule 4 after HS0-HS3 closure  
Branch: `nightrunner-29th-JUL-26`  
Measurement revisions:

- Before instancing: `e23a5227ebc60901ba11ea429e016fc6f24e75b2`
- After HS2: `93b1a5e2c6b6024a6cafab86a5aea6fa90754c17`

This report records the complete HS3 measurement, validation, comment-audit,
and independent-review evidence.

## Outcome

The three acceptance scenes reduce committed hull-store backing from 911,352
bytes to 387,504 bytes, a saving of 523,848 bytes (57.4803%). The current
footprint includes both the shared 7,176-byte `ConvexHullShape` row and its
276-byte `HullShapeIdentity` row; the old footprint contained one 7,176-byte
shape row per hull collider.

The direct hull manifold marker does not show a material timing change on the
two dynamic hull scenes. Median-of-three average time moves by -1.504% for
`convex_hull_stress_sleep` and -0.182% for `convex_hull_stacking`. The wider
marker set is mixed at sub-microsecond absolute deltas, so this evidence does
not claim an improvement. It records the result required by the plan and leaves
the repository performance gate to decide closure.

`asd` is a storage witness, not a dynamic narrowphase witness: all 51 committed
physics rows are fixed, and none of its six 2,340-row profiler artifacts emit a
`Frame/Physics/Narrowphase` column. Its marker is therefore reported as not
measurable rather than as zero.

## Exact Committed Hull Backing

Runtime reserve/capacity telemetry supplies the element sizes, committed
capacities, live counts, high-water counts, and resident bytes at both
revisions.

| Scene | Before rows | Before bytes | After shared rows | After shape bytes | After identity bytes | After total bytes | Saved bytes | Reduction |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `asd` | 51 | 365,976 | 12 | 86,112 | 3,312 | 89,424 | 276,552 | 75.5656% |
| `convex_hull_stress_sleep` | 42 | 301,392 | 20 | 143,520 | 5,520 | 149,040 | 152,352 | 50.5495% |
| `convex_hull_stacking` | 34 | 243,984 | 20 | 143,520 | 5,520 | 149,040 | 94,944 | 38.9140% |
| **Total** | **127** | **911,352** | **52** | **373,152** | **14,352** | **387,504** | **523,848** | **57.4803%** |

The arithmetic reconciles exactly with runtime telemetry:

```text
before = authored hull collider rows * 7,176
after  = distinct scene-lifetime hull variants * (7,176 + 276)
```

Before telemetry says `Exact scene convex-hull-collider count`. After telemetry
says `Distinct shareable plus explicitly unique scene convex-hull variant
count`. Every listed capacity equals live equals high-water for the measured
load.

Raw logs. Before-side telemetry is retained in the first alternating timing
launch for each scene; after-side telemetry is also retained in the dedicated
two-frame capacity launch:

- `TestOutput/validation/agent_logs/hs3/before_perf_asd_r1.out.log`
- `TestOutput/validation/agent_logs/hs3/before_perf_convex_hull_stress_sleep_r1.out.log`
- `TestOutput/validation/agent_logs/hs3/before_perf_convex_hull_stacking_r1.out.log`
- `TestOutput/validation/agent_logs/hs3/after_capacity_asd.out.log`
- `TestOutput/validation/agent_logs/hs3/after_capacity_convex_hull_stress_sleep.out.log`
- `TestOutput/validation/agent_logs/hs3/after_capacity_convex_hull_stacking.out.log`

## Narrowphase Measurement

### Workload

Each revision used its own detached worktree and Profile binary. Temporary,
uncommitted copies of the three scene files added only `logging.perfLog`; the
committed scenes, config, baselines, goldens, schemas, and performance
artifacts were not changed. Each launch used:

```text
--renderer dx12 --vsync off --fixed-step --shadows off --frames 1200
```

Each process performed two profiler passes and emitted 2,340 measured frame
rows. Three runs per scene/revision, alternating revision order, produced 18
successful processes and 42,120 frame rows.

`Frame/Physics/Narrowphase/ObjectManifold` is the primary marker because it
directly wraps the exact shape-pair manifold dispatch that walks hull faces and
edges. The outer `Frame/Physics/Narrowphase` and persistent-contact exact
manifold marker are included so the evidence cannot hide a neighboring cost.
Values below are the median of the three independently analyzed run statistics.

| Scene | Marker | Before avg ms | After avg ms | Avg delta | Before p50 ms | After p50 ms | P50 delta | Before p99 ms | After p99 ms | P99 delta |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `convex_hull_stress_sleep` | `Frame/Physics/Narrowphase/ObjectManifold` | 0.0133 | 0.0131 | -1.504% | 0.0084 | 0.0082 | -2.381% | 0.0871 | 0.0893 | +2.526% |
| `convex_hull_stress_sleep` | `Frame/Physics/Narrowphase` | 0.0049 | 0.0050 | +2.041% | 0.0031 | 0.0033 | +6.452% | 0.0615 | 0.0615 | 0.000% |
| `convex_hull_stress_sleep` | `.../PersistentContacts/BuildManifolds/ExactObjectManifold` | 0.0117 | 0.0115 | -1.709% | 0.0089 | 0.0087 | -2.247% | 0.0535 | 0.0521 | -2.617% |
| `convex_hull_stacking` | `Frame/Physics/Narrowphase/ObjectManifold` | 0.0550 | 0.0549 | -0.182% | 0.0460 | 0.0462 | +0.435% | 0.0970 | 0.0980 | +1.031% |
| `convex_hull_stacking` | `Frame/Physics/Narrowphase` | 0.0036 | 0.0038 | +5.556% | 0.0031 | 0.0034 | +9.677% | 0.0091 | 0.0086 | -5.495% |
| `convex_hull_stacking` | `.../PersistentContacts/BuildManifolds/ExactObjectManifold` | 0.0554 | 0.0552 | -0.361% | 0.0466 | 0.0468 | +0.429% | 0.0959 | 0.0975 | +1.668% |

The outer-marker percentages amplify 0.0001-0.0003 ms differences because the
marker is only about four microseconds. The directly affected object-manifold
marker is stable. This matrix supports “no material measured change,” not
“proved faster”; `tools\validate_perf.bat` remains the binding regression gate.

Raw CSVs and one JSON analysis per run are under:

- `TestOutput/validation/agent_logs/hs3/*_perf_log.csv`
- `TestOutput/validation/agent_logs/hs3/analysis/*.json`
- `TestOutput/validation/agent_logs/hs3/analysis/*.analysis.log`

## Exact Commands

Detached comparison worktrees:

```powershell
git worktree add --detach C:\SkullbonezCore-hs3-before e23a5227ebc60901ba11ea429e016fc6f24e75b2
git worktree add --detach C:\SkullbonezCore-hs3-after 93b1a5e2c6b6024a6cafab86a5aea6fa90754c17
git -C C:\SkullbonezCore-hs3-before submodule update --init --recursive
git -C C:\SkullbonezCore-hs3-after submodule update --init --recursive
```

Profile builds, run once per worktree:

```bat
tools\validate_build.bat Profile
```

After-revision dedicated capacity launch, run for each scene:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step --shadows off --frames 2 --scene SkullbonezData/scenes/<scene>.scene.json
```

Timing launch, run three times per revision/scene in alternating order after
adding only the temporary `logging.perfLog` scene field:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step --shadows off --frames 1200 --scene SkullbonezData/scenes/<scene>.scene.json
```

Each raw CSV was analyzed with the matching revision's repository tool:

```powershell
python Agentic/Skills/skore-render-test/analyze_perf.py `
  --renderer hs3_<revision>_<scene>_r<repeat> `
  --csv <raw-csv> `
  --out-dir C:\SkullbonezCore\TestOutput\validation\agent_logs\hs3\analysis
```

## Localized Tooling Issue

The first isolated Profile builds failed because newly created Git worktrees
contained empty ImGui and Tracy submodule directories. Both failures reported
the same seven missing source files and changed no shared source. Initializing
the pinned submodules in each worktree resolved the issue; both retries built
with zero warnings and zero errors in about 50 seconds. Evidence:

- `TestOutput/validation/agent_logs/hs3/before_build_profile.out.log`
- `TestOutput/validation/agent_logs/hs3/after_build_profile.out.log`
- `TestOutput/validation/agent_logs/hs3/before_build_profile_retry.out.log`
- `TestOutput/validation/agent_logs/hs3/after_build_profile_retry.out.log`

## Comment Audit Preparation

The three previously deferred plan-scope files were inspected against
`Agentic/Skills/comment-style-audit/skill.md` and the comment guide:

- `SkullbonezSource/Physics/CollisionShape.h`
- `SkullbonezSource/Physics/ConvexHullShape.h`
- `SkullbonezTests/TestConvexHull.cpp`

`ConvexHullShape.h` and `TestConvexHull.cpp` used the behavior-sensitive
“narrowphase” term without defining it. Their glossaries now define the term.
`CollisionShape.h` now states the canonical scene-lifetime row and index
invariant, and `ConvexHullShape.h` distinguishes cold authored scaling from
immutable stored topology. These are documentation-only source corrections.
Ownership, immutability, determinism, topology, and stable shared-index claims
were checked against the post-HS2 source. The plan checklist closes 18/18 with
zero deferred files. No behavior changed.

## Closure Validation

- `tools\validate_physics_deep.bat` passes every broad/deep byte-exact Physics
  baseline and the exact SkullScope query evidence.
- `tools\validate_perf.bat` passes both absolute-budget and comparison lanes.
  The first invocation overlapped the isolated measurement workload and
  reported DX12-only timing contention while the focused Physics benchmark and
  absolute budgets passed. The unchanged idle-machine rerun passed completely;
  no baseline was changed.
- The single authoritative `tools\validate_replay_visual_fidelity.bat`
  invocation passes 17/17 typed controls, the 2,401-tick one-presentation
  artifact, and every false-pass control.
- `tools\validate_full.bat` passes 441/441 tests and 2,420,993 assertions,
  coverage, UI and architecture lanes, DX12 validation, and byte-exact Physics.
- Format, dependency direction, strict function complexity at the ratified
  400/6 triggers (40/40), authority-free aggregates (85/85), the sole unrelated
  ruled extraction scar, and all 12-or-more-parameter signature rulings pass.

## Independent Review

Independent rubber-duck review closed with zero blockers after correcting the
three before-side raw-log references in this report. The reviewer confirmed:

- `HullShapeIdentity` is a tested strong value with a real invariant, not an
  authority-free bag;
- canonical reconstruction and arbitrary-scale fallback preserve identity
  honesty;
- hull deletion/replacement retains stable scene-lifetime rows without
  overwriting shared geometry;
- Replay clone rows are shared within the destination and independently owned
  from the source;
- identity lookup is confined to cold append/reservation paths, with no
  refcount, lookup, or added indirection in narrowphase or solver loops;
- sphere/box compaction and replacement remain unchanged; and
- no capability slice, extraction scar, wide-signature drift, dependency
  violation, or false ownership/comment claim was introduced.

The reviewer noted one non-blocking depth opportunity: same-kind shared-hull
replacement is source-reviewed and covered by the broader lifecycle matrix but
does not have its own isolated test case. It is not a plan acceptance gap.

## Closure Status

HS3 and the plan are complete:

- exact before/after committed hull backing is recorded;
- a real same-workload narrowphase matrix is recorded without assuming an
  improvement;
- all mapped closure gates pass;
- the comment audit is 18/18 with zero deferred files;
- independent review has zero blockers; and
- no baseline, golden, config, schema, allowlist, or committed runtime artifact
  was refreshed.
