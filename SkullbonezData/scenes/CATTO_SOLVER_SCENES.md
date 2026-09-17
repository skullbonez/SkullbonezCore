# Catto solver scenes

Twenty-one runnable Skullbonez scenes adapt twenty of Erin Catto's Solver2D
samples, including both the 20-row and 100-row pyramid sizes. These fixtures
provide a shared starting collection for future PGS/TGS/third-solver comparisons.
They currently use Skullbonez's existing solver; this change adds no solver switch.

## Source and scope

The [Solver2D repository](https://github.com/erincatto/solver2d) links the exact
[video supplied by the user](https://www.youtube.com/watch?v=sKHf_o_UCzI&t=596s).
Geometry and initial conditions were adapted from its MIT-licensed sample code
at commit `1e0492d81f68c7831cfa549699dd98bcb8454060`.
Video frames were not accessible, so this inventory does **not** claim a verified
shot-by-shot match, timestamp mapping, or identification of the scene at 9:56.

[The manifest](catto_solver_manifest.json) records a source link, body/joint
counts and adaptations for every case, plus all six source samples requiring
further engine support. The [MIT notice](../../ThirdPtySource/solver2d_samples_LICENSE.txt)
retains Erin Catto's copyright.

## Run

From the repository root, build and open a scene:

```powershell
tools\validate_build.bat Profile
.\Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData/scenes/catto_arch.scene.json
```

Each scene has a front camera and runs indefinitely. Playback explicitly sets
`pauseSnapshotState: false`, so the first domino's authored velocity does not
turn the whole scene into a paused inspection snapshot. The ordinary collection is
[catto_solver_tests.suite.json](catto_solver_tests.suite.json); the 5,050-dynamic-body
pyramid is separated into [catto_solver_stress.suite.json](catto_solver_stress.suite.json).
The large scene declares its own 5,148-body capacity. Disable replay when
running it: its dense contact snapshot exceeds the current 8 MiB replay snapshot
cap. Ordinary scene loading and load-only sweeps honor its authored capacity.

```powershell
.\Profile\SKULLBONEZ_CORE.exe --replay off --scene SkullbonezData/scenes/catto_pyramid_100.scene.json
```

The reproducible harness builds on Skarness and keeps commands, scene-state
observations, result summaries, executable/scene hashes, and before/after captures
under a fresh session directory:

```powershell
python tools/generate_catto_solver_scenes.py --check
python tools/validate_catto_solver_scenes.py --check
tools\validate_build.bat Automation
python tools/validate_catto_solver_scenes.py --session TestOutput/skarness/catto-check
python tools/validate_catto_playback.py --session TestOutput/skarness/catto-playback
python tools/validate_catto_solver_scenes.py --session TestOutput/skarness/catto-large --include-stress --select pyramid_100
```

Use `--select bridge` to select a case or `--ticks 1200` for a longer run. The
runner disables replay recording and defaults to 120 native ticks. Its checks establish runtime identity,
authored starting poses, finite sampled motion, and screenshot capture. They do
not certify stack stability, constraint accuracy, or numerical agreement with
Solver2D. `authoredJoints` in results is the fixture count, not a queried runtime
constraint count. The large case resolves each object separately because a bulk
catalog exceeds the existing Skarness pipe buffer. Every owned session is stopped
on completion; failure evidence is retained.

Edit [the generator](../../tools/generate_catto_solver_scenes.py), then run it
without `--check` to regenerate scenes, arch hulls, suites and the manifest.

## Deliberate adaptations

- Bodies move freely in 3D. XY geometry is scaled by four; circles become spheres,
  and default box depth is four world units. No planar lock is implied.
- Mass uses source 2D area times density to preserve mass ratios; inertia is 3D.
  Native sleep, drag, friction and CCD policies remain active. Per-shape source
  friction is not reproduced.
- Gravity is scaled from -10 to -40 (Joint Grid: -80; Confined: zero), while the
  native tick rate remains 120 Hz rather than Solver2D's 60 Hz.
- Floor slabs are tiled to fit existing broadphase reservations. Their top plane
  matches the source; seams add contact features. Hidden safety terrain lies at
  least 1,000 world units below the initial scene.
- Revolute joints become native 3D point joints with 40 Hz frequency, damping 1,
  and zero slack. Source hinge motors, angular limits, collision masks and body
  damping are not reproduced. Joint Grid is reduced to 20 by 20 nodes.
- Far cases retain large absolute offsets but do not apply the local fourfold
  scale to those offsets. Far Pyramid's x origin changes from 100,000 to 90,000
  to fit Skullbonez's world bounds. These changes alter floating-point sensitivity.
- Arch blocks are baked extrusions of the source quadrilaterals. Card House keeps
  the original thinness; unstable behavior is not hidden by thickening it.

## Inventory

| Scene | Source sample | Dynamic bodies | Authored joints |
|---|---|---:|---:|
| [catto_single_box.scene.json](catto_single_box.scene.json) | Single Box | 1 | 0 |
| [catto_high_mass_ratio_1.scene.json](catto_high_mass_ratio_1.scene.json) | High Mass Ratio 1 | 165 | 0 |
| [catto_high_mass_ratio_2.scene.json](catto_high_mass_ratio_2.scene.json) | High Mass Ratio 2 | 3 | 0 |
| [catto_high_mass_ratio_3.scene.json](catto_high_mass_ratio_3.scene.json) | High Mass Ratio 3 | 3 | 0 |
| [catto_overlap_recovery.scene.json](catto_overlap_recovery.scene.json) | Overlap Recovery | 10 | 0 |
| [catto_vertical_stack.scene.json](catto_vertical_stack.scene.json) | Vertical Stack | 15 | 0 |
| [catto_pyramid_20.scene.json](catto_pyramid_20.scene.json) | Pyramid (20 rows) | 210 | 0 |
| [catto_pyramid_100.scene.json](catto_pyramid_100.scene.json) | Pyramid (100 rows) | 5050 | 0 |
| [catto_double_domino.scene.json](catto_double_domino.scene.json) | Double Domino | 15 | 0 |
| [catto_card_house.scene.json](catto_card_house.scene.json) | Card House | 40 | 0 |
| [catto_circle_stack.scene.json](catto_circle_stack.scene.json) | Circle Stack | 10 | 0 |
| [catto_confined.scene.json](catto_confined.scene.json) | Confined | 625 | 0 |
| [catto_arch.scene.json](catto_arch.scene.json) | Arch | 21 | 0 |
| [catto_bridge.scene.json](catto_bridge.scene.json) | Bridge | 160 | 161 |
| [catto_ball_and_chain.scene.json](catto_ball_and_chain.scene.json) | Ball & Chain | 41 | 41 |
| [catto_joint_grid.scene.json](catto_joint_grid.scene.json) | Joint Grid (20 x 20) | 393 | 760 |
| [catto_stretched_chain.scene.json](catto_stretched_chain.scene.json) | Stretched Chain | 40 | 40 |
| [catto_far_pyramid.scene.json](catto_far_pyramid.scene.json) | Far Pyramid | 55 | 0 |
| [catto_far_stack.scene.json](catto_far_stack.scene.json) | Far Stack | 5 | 0 |
| [catto_far_recovery.scene.json](catto_far_recovery.scene.json) | Far Recovery | 10 | 0 |
| [catto_far_chain.scene.json](catto_far_chain.scene.json) | Far Chain | 40 | 40 |

## Source samples needing further support

- **Warm Start Energy:** Requires deleting the heavy top body at step 120 (2 seconds in Solver2D); static preload is not the test.
- **Friction Ramp:** Five independent exact friction coefficients and mixing are essential; scene primitives do not expose these values.
- **Rush:** Requires constant-magnitude inward force recomputed every step, not one initial velocity or inverse-square gravity.
- **Ragdoll:** Source capsule/compound bodies, hinge limits and motor friction are not the native point-joint ragdoll model.
- **Ragdoll Stress:** Requires motor-driven obstacles and timed spawn/despawn in addition to source ragdoll constraints.
- **Far Ragdoll Pile:** Depends on the same source ragdoll model; substituting the native template would change the test.

## Validation recorded on 2026-09-17

- All 21 scenes loaded, resolved their authored identities, published the expected
  scene/physics body counts, and advanced 120 ticks with finite sampled state.
  Initial screenshots were inspected for all scenes, along with representative
  post-simulation captures.
- Ordinary evidence: `TestOutput/skarness/catto-verified-3/results.json`.
  Large-pyramid evidence: `TestOutput/skarness/catto-stress-verified/results.json`.
  These initial result files retain the scene hashes and executable hash from
  that run. The later normal-playback regression is separate evidence.
- `validate_fast.bat` passed with `SKORE_SIZE_DIFF_BASE` set to the worktree's
  starting commit `e59e397f865defb87a23f4c6c1d989896544369b`, avoiding inherited
  C++ changes from before this task. The primary suite passed 1,094 test cases
  (one skipped). Log: `TestOutput/catto-validate-fast-scoped.log`.
- Automation and Profile builds passed; a subsequent Profile build completed
  without further compilation. Generator and fixture integrity checks passed.
- Summary: `TestOutput/catto-validation-summary.json`. These local artifacts
  are intentionally outside version control. No accepted physics or image
  baselines were changed.

The normal-playback regression reproduces the original first-domino freeze and
verifies motion through startup, reset, and a round trip to Single Box. It uses
`run.resume` and observed render turns, then asserts the first two dominoes tip;
forced Physics steps would conceal the Inspect-mode failure. This check also
runs from `validate_skarness.bat`. The actual cross-scene pause remains an
explicit Scene-panel checkbox named **Pause across scenes**; the badge only
shows it when enabled and no longer advertises the unrelated `P` shortcut.

Follow-up evidence is in `TestOutput/skarness/catto-playback-final/results.json`
and `TestOutput/skarness/catto-scenes-pause-fixed/results.json`. All 20 ordinary
scenes passed again after the playback change. The fast gate passed 1,094 tests,
and the DX12 visual gate passed without changing references. The impulse and
solver are unchanged. `TestOutput/catto-playback-fix-summary.json` records the
causes and exact regression cases.
