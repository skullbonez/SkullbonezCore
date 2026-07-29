# Box Vibration And Warm-Start Integrity — BV3

Date: 2026-07-29

Branch: `nightrunner-30th-JUL-26`

Task: BV3 — retire the terrain warm-start seed

## Outcome

The two fixed terrain support scales are deleted. Terrain contact no longer
floors accumulated normal impulse or friction against a body-weight constant.
When a terrain feature has no cache entry, its first-touch estimate is now
derived from that row's effective mass, the live signed gravity vector, the
fixed-step duration, and every contact row touching the body:

`normalMass * max(0, dot(gravityAcceleration, normal)) * dt / contactRowCount`

Every terrain row contributes to `m_persistentContactCounts`. A cache hit always
wins over the fresh estimate, including when the cached impulse is smaller.
Terrain friction is bounded only by `mu * accN`, and all non-elastic terrain
rows may retain their accumulated impulse without receiving resting or sleep
authority.

`terrainWarmStart` remains as a historical diagnostics/snapshot member because
Replay v2 conversion, hashing, and prediction diagnostics project that record.
Its semantics are now the cache-miss-only row estimate; no fixed scale or
minimum impulse remains.

Terrain restitution was not changed.

## Acceptance Evidence

- `TERRAIN_RESTING_SUPPORT_SEED_SCALE` and
  `TERRAIN_SHORELINE_SUPPORT_SEED_SCALE` are absent.
- All three `max(accN, terrainWarmStart)` friction/floor sites are absent.
- A reduced restored cache entry proves that a cache hit below the fresh
  first-touch estimate is applied unchanged.
- A two-row terrain fixture proves the estimates share one body load rather
  than each claiming the full load.
- The production two-point tilted shoreline fixture still inhibits sleep,
  creates two cache entries, and holds first-frame residual vertical speed
  below 8.5% of the injected speed. Its cache-hit frame improves again and does
  not acquire resting authority.
- The three pre-existing T3 policy variants were retained and expanded rather
  than deleted. All now use the same row-derived estimate and avoid the former
  upward top-box over-push.
- Focused Profile result: 11 persistent-contact cases / 167 assertions pass.
- Full unit result: 462 cases / 2,423,848 assertions pass.

## Bounded-Divergence Inspection

The plan defers baseline regeneration until the final Debug source state. The
mapped Physics gates therefore reach their golden comparison and stop on the
expected cumulative mismatch:

- `physics_regression_varied.csv`: 35,093 of 44,401 lines differ per run,
  beginning at line 1,276. The artifact contains two 44,401-line runs; the two
  runs are byte-identical to each other.
- `shooting_reaction_volley.csv`: 28 of 641 lines differ by small downstream
  floating-point changes. The semantic reaction checker passes all ten targets.
- `bullet_sweep_wall.csv`, `bullet_sweep_object.csv`,
  `bullet_sweep_terrain.csv`, and `space_three_body_chaos.csv` remain
  byte-exact.
- All four known-issue artifacts retain their line counts while their hashes
  move: seeded solver distribution (20,001), stacking stability (22,501),
  at-rest settling (54,001), and terrain contact (121).
- SkullScope's query packet moves consistently with the new terrain behavior;
  for example, the varied scene reports 356 sleeping rows instead of zero and
  523 supported rows instead of 621.
- Every changed physics CSV was scanned for `NaN`/`Inf`; none is present.
- No baseline or golden file was regenerated.

## Comment Audit

Touched-file inventory: 6/6 checked, 0 deferred:

- `SkullbonezSource/Physics/PersistentContactSolver.cpp`
- `SkullbonezSource/Physics/PersistentContactSolver.h`
- `SkullbonezSource/Physics/PhysicsDiagnosticsView.h`
- `SkullbonezSource/Physics/PhysicsSolverSnapshot.h`
- `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h`
- `SkullbonezTests/TestPersistentContactSolver.cpp`

The learning headers remain complete. New nearby comments explain signed live
gravity, all-row load sharing, cache precedence, solved-normal friction bounds,
and the Replay compatibility spelling. All touched `Related:` paths resolve.
The campaign-wide whole-file checklist remains BV6 work.

## Validation

- `tools\validate_build.bat Profile`: pass, zero warnings/errors.
- `tools\validate_format.bat`: pass across 573 source files and 319 headers.
- `Profile\SKULLBONEZ_TESTS.exe "--test-case=Persistent contact solver:*"`:
  11/11 cases, 167/167 assertions.
- `tools\validate_tests.bat`: pass; 462/462 cases and
  2,423,848/2,423,848 assertions.
- `tools\validate_physics.bat`: Debug build, lifecycle smoke, and scene run
  pass; stops at the deliberately deferred 35,093-line golden mismatch.
- `tools\validate_physics_deep.bat`: all scene launches pass; stops at the same
  deferred core mismatch plus the inspected 28-line shooting delta.
- `tools\check_shooting_reaction.py Debug\shooting_reaction_volley.csv`: pass
  for all ten targets.

BV3 is complete. BV4 owns the controlled convergence re-measurement; BV6 owns
the one-time final Debug baseline regeneration and matching rerun.
