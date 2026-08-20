# Headless Linux Physics Baseline Comparison

Date: 2026-08-20
Status: WNF - owner requested plan only; 0/5 tasks complete
Impact area: Physics, authored scene setup, portable tests, diagnostics, and CI
Owner: Physics determinism envelope
Priority: Parked until explicitly moved out of `WNF/`
Commit name: `LINUX_PHYSICS_BASELINE`

## Owner Direction

Create a renderer-free Linux path that runs the existing authored Physics
benches and compares their output directly with the committed Windows baselines.
The baselines are already in the repository; Linux must consume them rather
than requiring a Windows job, downloaded artifact, or newly generated Linux
golden.

This file is deliberately parked in `WNF/`. Do not register it in
`MASTER-PLAN.md`, `WORK_LEDGER.csv`, or `SessionState.md`, and do not treat it as
active implementation authority until the owner explicitly reactivates it.

## Goal

Add a small headless executable and test entry point that:

- loads an authored scene from the repository;
- constructs the same Physics bodies, colliders, terrain, initial impulses, and
  configuration as the Windows runtime path;
- executes the scene's exact fixed-step playback count without creating a
  window, renderer, DX12 device, audio system, or presentation frame loop;
- writes the existing Physics regression CSV contract; and
- byte-compares the result with the matching committed file under
  `TestOutput/baselines/`, reporting the first differing row and bounded context.

The first required CI witness is
`SkullbonezData/scenes/physics_bench_varied.scene.json` against
`TestOutput/baselines/physics_regression_varied.csv`. The runner must also be a
general bench tool so the existing deep artifacts can be exercised deliberately:

| Scene | Output contract | Existing Windows baseline |
|---|---|---|
| `physics_bench_varied.scene.json` | Physics regression CSV | `physics_regression_varied.csv` |
| `bullet_sweep_wall.scene.json` | collision-time CSV | `bullet_sweep_wall.csv` |
| `bullet_sweep_object.scene.json` | collision-time CSV | `bullet_sweep_object.csv` |
| `bullet_sweep_terrain.scene.json` | collision-time CSV | `bullet_sweep_terrain.csv` |
| `shooting_reaction_volley.scene.json` | Physics regression CSV | `shooting_reaction_volley.csv` |
| `three_body_chaos.scene.json` | Physics regression CSV | `space_three_body_chaos.csv` |

Only benches whose complete product setup is supported may be admitted to the
manifest. Unsupported gameplay or automation behavior must fail with a named
reason; it must not be silently omitted from a nominally equivalent run.

## Existing Evidence And The Actual Gap

- `CMakeLists.txt` already compiles the renderer-free production Physics,
  Maths, authored-scene parser, Terrain, Gameplay, and portable doctest sources
  with `-ffp-contract=off` on Linux.
- `.github/workflows/portable-linux-diagnostics.yml` already runs GCC, Clang,
  ASan, UBSan, and TSan lanes, but today it only runs the portable unit suite.
- `SkullbonezTests/TestDeterminism.cpp` proves exact 0/1/4-worker agreement
  inside one process/platform. It does not compare Linux output with Windows.
- `tools/check_physics_regression.py` already performs a binary comparison and
  gives bounded first-difference diagnostics. Its current actual-output paths
  are hardcoded under `Debug/`.
- `PhysicsDiagnosticsSink::EmitRegressionLog` is debug-only and formats
  positions and velocities to four decimals and quaternions to six. Therefore
  this plan proves cross-platform equality of the established regression
  observable, not raw floating-point state equality.
- The canonical scene-to-Physics projection is currently embedded in
  `Runtime/Scene/SceneAuthoredSetup.cpp`. In particular, insertion order,
  Euler-to-quaternion conversion, inertia construction, material identity,
  initial impulses, terrain, fixed/dynamic policy, sleeping seeds, and runtime
  Physics configuration can affect the baseline before the first tick.

The missing work is product-equivalent headless orchestration, not another
Physics solver implementation.

## Design Constraints

1. **One authored setup path.** Extract or expose a narrow renderer-free owner
   for the product scene-to-Physics projection and have both the Windows runtime
   and headless runner call it. Do not copy the ball/box setup loops into a test.
   A duplicated harness could agree with its own golden while the application
   initializes the same scene differently.
2. **No Rendering dependency.** The headless target must link only the CPU
   closure it actually uses. Renderer-free macros may remove presentation, but
   never alter Physics inputs or tick ordering.
3. **Exact fixed-step schedule.** Execute exactly the scene playback count at
   `PHYSICS_FIXED_DT`. Do not route through the presentation-frame accumulator,
   the five-tick cap, elapsed wall time, or any dropped-tick policy.
4. **Stable identity and order.** Preserve authored body insertion order,
   scene-object identity, names, and diagnostic row order. A sort added only to
   make files agree is a failure.
5. **Reuse the CSV contract.** Move the narrow record/formatting logic behind a
   portable diagnostic seam, or compile it under an explicit regression-output
   capability used by both targets. Do not maintain separate Windows and Linux
   format strings.
6. **Locale and newline closure.** Force locale-independent numeric formatting
   and canonical LF bytes so a mismatch means data/format divergence rather
   than host locale or newline translation.
7. **Baselines remain Windows-owned.** CI receives read-only baseline paths.
   The Linux command exposes no update switch, never rewrites a golden, and
   uploads actual/diff evidence only on failure.
8. **No false byte-exact claim.** A byte-identical rounded CSV is an important
   cross-platform regression pass, but it is not proof that the underlying
   binary32 values are equal. The separate unrounded experiment owns that
   question.

## Tasks

### LPB0 - Pin The Headless Contract And Negative Controls

- [ ] Add a manifest describing scene path, playback ticks, output mode,
  expected output name, and committed baseline path for each supported bench.
- [ ] Record the expected `physics_bench_varied` body count, frame count, row
  count, and baseline SHA-256 so a zero-body, early-exit, or wrong-file run
  cannot pass accidentally.
- [ ] Add planted controls proving that a changed scene byte, one changed CSV
  value, a missing final frame, reordered bodies, and CRLF output all fail with
  bounded diagnostics.

### LPB1 - Share Product Authored Physics Setup

- [ ] Isolate the renderer-free authored Physics projection from
  `SceneAuthoredSetup` behind one narrow owner used by the Windows runtime and
  the headless executable.
- [ ] Preserve capacity reservation, terrain view lifetime, body/collider
  insertion order, Euler conversion, inertia, materials, impulses, sleeping
  seeds, world forces, and all relevant `EngineConfig` values.
- [ ] Add a Windows A/B test showing the existing application path and the new
  headless path produce identical frame-zero Physics state and the same rounded
  CSV for `physics_bench_varied` before Linux is used as evidence.
- [ ] Run the dependency graph checker during implementation design and reject
  any upward include, broad context/service bag, callback pack, or second
  retained simulation owner introduced to make the extraction convenient.

### LPB2 - Add The Portable Headless Bench Runner

- [ ] Add a dedicated executable rather than teaching the doctest process to
  impersonate the application lifecycle.
- [ ] Accept explicit scene, output, and baseline paths; default to no baseline
  update capability.
- [ ] Load the scene, establish the same Physics configuration, execute exact
  ticks, and emit the shared regression or collision-time format without any
  graphics initialization.
- [ ] Return distinct nonzero results for parse/setup failure, simulation
  failure, missing/incomplete output, and baseline mismatch. Print the first
  divergent frame/body/field when the CSV schema permits it.
- [ ] Prove two consecutive same-binary runs are byte-identical before using a
  Windows-vs-Linux comparison as the signal.

### LPB3 - Compare Linux Directly With The Committed Windows Baseline

- [ ] Generalize `tools/check_physics_regression.py` to accept an explicit
  actual-output directory or exact actual/baseline pair while preserving the
  current Windows defaults and `--deep` behavior.
- [ ] Add a CTest label or explicit workflow step for the core varied-scene
  comparison. Run it in both GCC and Clang warning-clean lanes; retain the
  ordinary unit/sanitizer matrix independently.
- [ ] Measure runtime before deciding whether the full deep manifest belongs in
  every scheduled lane, one scheduled lane, or a separate manual diagnostic.
  The core varied comparison is mandatory regardless of that choice.
- [ ] On failure, upload the actual CSV, comparison summary, compiler/version,
  scene hash, baseline hash, and exact command line. Do not upload an enormous
  unbounded line diff.

### LPB4 - Close Parity And Document The Gate

- [ ] Pass the core baseline with GCC and Clang on Linux and with the existing
  Windows `tools/validate_physics.bat` path from one source revision.
- [ ] Exercise every admitted deep-manifest row and record any intentionally
  unsupported row with the missing product owner needed for parity.
- [ ] Pass warning-clean builds, portable tests, the focused headless tests,
  dependency validation, and the existing Windows Physics gate without a
  baseline refresh.
- [ ] Perform the touched-source comment audit and an independent ownership
  review of the shared setup seam.
- [ ] Update `tools/README.md` to state exactly what the Linux comparison proves:
  equality with the committed rounded Windows regression observable, not raw
  cross-platform float identity.

## Acceptance

The plan is complete when a clean Linux runner can execute
`physics_bench_varied` without a graphics stack and report a byte-exact match
against the committed Windows CSV, GCC and Clang both exercise the comparison,
the Windows gate still produces the same bytes, planted divergence controls
fail, and the implementation uses the same authored Physics setup and output
contract as the product path.

No task in this plan authorizes a baseline refresh. A real mismatch is evidence
to investigate, not permission to bless Linux output.

## Reactivation Condition

Move this file from `WNF/` to `TODO/` only when the owner explicitly selects the
headless Linux comparison for implementation. At that point, refresh all file,
symbol, workflow, and baseline hashes against the then-current tree before
starting LPB0.
