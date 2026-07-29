# Box Vibration And Warm-Start Integrity — BV0 T0

Date: 2026-07-29  
Branch: `nightrunner-29th-JUL-26`  
Measured source tip: `6a88c9c04b5258df9c807a400ace5b8997d3417b`  
Scope: BV0 only; no solver behavior or baseline was changed

## Result

`SkullbonezData/scenes/box_vibration_t0.scene.json` is the controlled T0
instrument. It contains one fixed box foundation and four directly stacked
dynamic bricks. The only authored disturbance is a `0.25` x / `0.05` z linear
velocity on the top brick, plus alternating `+/-0.15` degree yaw. There is no
projectile, ragdoll, generated body, or terrain contact.

The current solver reproduces the defect deterministically after the initial
settling period:

| Frames 300-1199 | T0 |
|---|---:|
| Downward-to-upward `vel_y` flips crossing `-0.01` to `+0.01` | **566** |
| Bricks affected | **4/4** |
| Worst brick | **181 flips** |
| Solver frames at the 12-iteration cap | **900/900** |
| Minimum solver iterations | **12** |
| Cache misses / lookups | **2,169 / 8,465 (25.62%)** |
| Maximum absolute `vel_y` | **3.914273** |

The stack remains a controlled instrument rather than turning into a free-fall
or pile-divergence case. At frame 1199 all five bodies are one supported island,
the four dynamic bricks remain awake, there are 11 contacts, and maximum body
speed is `0.656912`. Over the measured window, vertical position spans are
`0.019336`, `0.060186`, `0.139087`, and `0.210285` from bottom to top.

Two independently generated deterministic CSVs are byte-identical:

```text
Debug/box_vibration_t0c.csv
Debug/box_vibration_t0d.csv
SHA-256 E3012237092A008BA058CD775E38BDF3FFD0370100875FFC7F9C7C7DA2661464
```

## Reproduction

Generate the trace and byte-exact CSV:

```powershell
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step `
  --shadows off --hide-top-text --frames 1200 `
  --scene SkullbonezData/scenes/box_vibration_t0.scene.json `
  --physics-diag Debug/box_vibration_t0.physicsdiag.ndjson `
  --physics-regression-log Debug/box_vibration_t0.csv
```

The T0 metric is the following bounded, one-row SkullScope query:

```powershell
tools\physics_query.bat Debug\box_vibration_t0.physicsdiag.ndjson sql `
  "with samples as (
     select frame,name,vel_y,
            lag(vel_y) over (partition by name order by frame) as prev_vel_y
     from bodies
     where name like 'brick_%' and frame between 300 and 1199
   ),
   flips as (
     select name,count(*) as flips
     from samples
     where prev_vel_y < -0.01 and vel_y > 0.01
     group by name
   ),
   solver_window as (
     select * from solver_stats where frame between 300 and 1199
   )
   select coalesce(sum(flips),0) as downward_to_upward_flips,
          count(*) as affected_bricks,
          coalesce(max(flips),0) as worst_brick_flips,
          (select min(solver_iterations) from solver_window)
            as min_solver_iterations,
          (select sum(case when solver_iterations=12 then 1 else 0 end)
             from solver_window) as frames_at_cap,
          (select count(*) from solver_window) as solver_frames,
          (select sum(cache_misses) from solver_window) as cache_misses,
          (select sum(cache_hits)+sum(cache_misses) from solver_window)
            as cache_lookups,
          (select max(abs(vel_y)) from bodies
             where name like 'brick_%' and frame between 300 and 1199)
            as max_abs_vel_y
   from flips" --limit 5
```

The `0.01` crossing threshold excludes sign changes caused only by near-zero
rounding chatter. Frame 300 begins the acceptance window after the authored
disturbance and initial gaps have resolved; the remaining 900 frames measure
sustained behavior.

## Current-Source Re-Resolution

The plan's 2026-07-27/28 line numbers and key description were re-resolved
against `6a88c9c0` before recording T0:

- `PersistentContactSolver.h:49` defines the current 15-bit body mask
  `0x7fff`.
- `PersistentContactSolver.cpp:159-180` packs an object key as
  `lo[61:47] | hi[46:32] | featureId[31:0]`; terrain uses bit 62 plus one
  15-bit body and the full 32-bit feature. The stale narrowphase comment that
  said the cache kept only 16 feature bits was corrected without changing code.
- `PersistentContactSolver.cpp:183-196` considers a key cached only when a
  stored normal or tangent impulse carries load.
- `ObjectContactManifold.cpp:265-284` still encodes box-face kind `2` and
  box-edge kind `3` at bit 14. Face keys retain four-bit reference and incident
  codes plus the clipped point id. Therefore `36480` remains face-A reference
  face 3 / incident face 2, while `43200` remains face-B reference face 2 /
  incident face 3.
- `ObjectContactManifold.cpp:605-615` still uses the stateless `1.0e-4` SAT
  tie epsilon.
- `PersistentContactSolver.cpp:996-1018` still applies object restitution from
  closing speed without consulting prior cached load.
- `PersistentContactSolver.cpp:1060-1089` still limits warm-start lookup to
  `supportsRestingPolicy`, and `:1766` applies the same restriction to cache
  admission.
- `PersistentContactSolver.cpp:790-839` still derives the terrain seed from
  vertical gravity, `abs(normal.y)`, and manifold point count. Terrain friction
  still uses that seed at `:1044-1048`, `:1077-1078`, and `:1233`;
  `:1110-1112` still floors `accN` to the seed. Object rows increment
  `m_persistentContactCounts` at `:693-694`; terrain rows do not.
- `PersistentContactSolver.cpp:1694-1695` still applies each position
  correction row without a manifold-point divisor.

These facts preserve the plan's root-cause direction while replacing its aged
line numbers and obsolete low-16-bit key description.

## T0 Acceptance Harness

BV0 freezes the following cumulative harness for BV1-BV6:

1. Exact BV0 trace command and SQL metric above.
2. Two-run byte comparison of `--physics-regression-log` outputs for the BV0
   scene.
3. Focused unit oracles:
   - `Object contact manifold: unchanged box stack keeps four stable face rows`
   - `Object contact manifold: boundary-band feature selection is stable across ten evaluations`
   - `Persistent contact solver: warm-start cache is reused on a matching terrain row`
   - `Persistent contact solver: full terrain seed prevents first-frame resting sink`
   - `Persistent contact solver: shoreline seed prevents one-frame edge bob without becoming cached support`
   - `Persistent contact solver: terrain seed strength bounds one-iteration three-box stack sink`
   - `Persistent contact solver: restitution creates separating terrain velocity`
   - `Property invariant: friction and restitution outputs stay bounded [seed 0x16C0111D]`
   - `Persistent contact solver: a box gains sleep support only after toppling from its edge`
4. `tools\validate_physics.bat`
5. `tools\validate_physics_deep.bat`
6. `tools\validate_tests.bat`
7. `tools\validate_full.bat` for the new tracked scene and final plan closure.

No baseline regeneration belongs to BV0.

## Validation

All mapped BV0 gates pass:

| Command | Result | Elapsed |
|---|---|---:|
| `Profile\SKULLBONEZ_TESTS.exe --test-case="Object contact manifold:*"` | 4 cases / 345 assertions | focused |
| `Profile\SKULLBONEZ_TESTS.exe --test-case="Persistent contact solver:*"` | 8 cases / 108 assertions | focused |
| `Profile\SKULLBONEZ_TESTS.exe --test-case="Property invariant: friction and restitution outputs stay bounded*"` | 1 case / 36 assertions | focused |
| `tools\validate_physics.bat` | all passed; byte-exact baseline | 35.7 s |
| `tools\validate_physics_deep.bat` | all passed | 108.5 s |
| `tools\validate_tests.bat` | 457 cases / 2,422,070 assertions | 45.1 s |
| `tools\validate_full.bat` | default gate passed | 350.5 s |

`git diff --check` also passes. No baseline or golden artifact changed.

## SkullScope Accounting

- Runtime trace:
  `Debug/box_vibration_t0c.physicsdiag.ndjson`, `11,762,116` bytes.
- SQLite cache:
  `Debug/box_vibration_t0c.physicsdiag.sqlite`, `6,344,704` bytes.
- Discarded seven-brick tuning trace: summary `3,087` characters, solver
  `3,582`, and metric `553`.
- Final four-brick candidate first run: summary `2,709` characters and metric
  `595`.
- Final repeated T0 metric: `594` characters.
- Bounded-position SQL: `694` characters.
- Total GPT-read SkullScope output: `11,814` characters.
- Raw NDJSON, SQLite, and CSV artifacts were not read by the model. No query
  output was truncated.

Queries read:

```powershell
tools\physics_query.bat Debug\box_vibration_t0a.physicsdiag.ndjson summary
tools\physics_query.bat Debug\box_vibration_t0a.physicsdiag.ndjson solver --frames 300:1199 --limit 12
tools\physics_query.bat Debug\box_vibration_t0a.physicsdiag.ndjson sql "<T0 SQL above>" --limit 5
tools\physics_query.bat Debug\box_vibration_t0b.physicsdiag.ndjson summary
tools\physics_query.bat Debug\box_vibration_t0b.physicsdiag.ndjson sql "<T0 SQL above>" --limit 5
tools\physics_query.bat Debug\box_vibration_t0c.physicsdiag.ndjson sql "<T0 SQL above>" --limit 5
tools\physics_query.bat Debug\box_vibration_t0c.physicsdiag.ndjson sql `
  "select name,round(min(pos_y),6) as min_pos_y,
          round(max(pos_y),6) as max_pos_y,
          round(max(pos_y)-min(pos_y),6) as y_span,
          round(max(abs(vel_y)),6) as max_abs_vel_y
   from bodies
   where name like 'brick_%' and frame between 300 and 1199
   group by name order by name" --limit 10
```

## Comment Audit

Touched comment-bearing source scope:

- `SkullbonezSource/Physics/ObjectContactManifold.cpp` — whole-file learning
  header, glossary, invariants, feature-key comments, and related paths
  inspected; the obsolete low-16-bit cache claim was corrected.

Fixture scope:

- `SkullbonezData/scenes/box_vibration_t0.scene.json` — JSON does not admit a
  learning header; field names, deterministic playback settings, and object
  names were inspected against the current parser contract.

Checked: 2. Deferred: 0. Unchecked: 0.

## Independent Review

A fresh read-only rubber-duck review reproduced the T0 metric, final supported
state, bounded position spans, repeat trace/CSV identity, focused oracle counts,
current key packing, ledger state, and comment-audit scope. It confirmed that
the diff contains no BV1 behavior, baseline, or golden change.

Verdict: **CLEAR — no material blocker.**
