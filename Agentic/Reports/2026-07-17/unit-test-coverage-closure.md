# Unit-Test Coverage Campaign Closure

Date: 2026-07-17
Branch: `nightrunner-16th-july`
Plan: `unit-test-coverage-campaign`, U0-U9 complete

## Outcome

The campaign added a mandatory Debug product-line coverage lane, raised every
ratified subsystem above its tier floor, and armed those floors as an enforced
gate. The final doctest runner contains 282 behavioral cases with 21,388
assertions. `validate_tests` completes in about 8 seconds on the campaign host,
well inside the 60-second budget.

The checker now fails closed when a configured product translation unit is
missing from Cobertura. This prevents project or link omissions from silently
shrinking a subsystem denominator. The global percentage remains report-only;
only the versioned subsystem floors are enforced.

## Baseline To Final Coverage

Command: `tools\validate_coverage.bat`
Tool: OpenCppCoverage 0.9.9.0
Baseline: `Agentic/Reports/2026-07-16/unit-coverage-baseline.md`

| Subsystem | Tier | U0 baseline | Final | Delta | Floor |
|---|---:|---:|---:|---:|---:|
| `maths` | 1 | 498 / 667 (74.66%) | 598 / 690 (86.67%) | +12.01 pp | 85% |
| `core_primitives` | 1 | 590 / 1,038 (56.84%) | 918 / 1,038 (88.44%) | +31.60 pp | 85% |
| `physics_stores` | 2 | 856 / 1,638 (52.26%) | 1,125 / 1,597 (70.44%) | +18.18 pp | 70% |
| `physics_stages_and_solver` | 2 | 2,452 / 4,544 (53.96%) | 3,217 / 4,544 (70.80%) | +16.84 pp | 70% |
| `replay_artifact_codecs` | 2 | not instrumented | 1,246 / 1,644 (75.79%) | newly measured | 70% |
| `startup` | 2 | not instrumented | 1,155 / 1,257 (91.89%) | newly measured | 70% |
| `config_and_schema` | 2 | 398 / 422 (94.31%) | 400 / 422 (94.79%) | +0.48 pp | 70% |
| `runtime_input_and_interaction` | 3 | 366 / 410 (89.27%) | 504 / 676 (74.56%) | -14.71 pp | 50% |
| `scene_logic` | 3 | 35 / 36 (97.22%) | 35 / 36 (97.22%) | 0.00 pp | 50% |
| `replay_value_seams` | 3 | 678 / 2,028 (33.43%) | 2,031 / 2,470 (82.23%) | +48.80 pp | 50% |

Whole instrumented product output moved from 11,008 / 20,653 lines (53.30%)
to 17,062 / 24,896 lines (68.53%), a 15.23-point increase. It is recorded as
an output, never a repository-wide target.

The runtime input percentage decreased while covered lines increased by 138
because U8 linked the 266-line `RuntimeInteractionController` owner into the
denominator. That is an honest scope expansion, not a regression.

## U10 Scope Rulings

The independent review found that U0's globs named several translation units
which were not linked into the Cobertura process. U9 resolved the positive-scope
defect and recorded these explicit decisions in `tools/coverage_floors.json`:

- `PhysicsApi.*` remains owned by the combined CPU lane's
  `RunPhysicsStandaloneSmoke`, rather than duplicating its large fixed-store
  matrix in the doctest floor.
- `ReplayPredictionArchive.*` remains owned by the replay visual-fidelity
  system gate. Its values require the concrete prediction/presentation owners,
  and U5's sole mega invocation proved the production build/load/rebuild path.
- engine-loop `SceneController.*` remains Tier 4. `SceneRequestQueue` is the
  pure scene-request value seam covered by the Tier-3 floor.
- `AmortizedTask.cpp` is a zero-executable-line template anchor. The behavior
  in `AmortizedTask.h` remains measured through its doctest cases.
- replay overlay layout and value-packet cases are pure CPU contracts. They do
  not load artifacts, launch presentation, or alter visual-fidelity goldens,
  so they do not consume another replay mega invocation.

Every remaining executable translation unit in the ratified floor map is now
listed under `required_instrumented_sources`; omission is a checker failure
before percentage evaluation.

## Behavioral Evidence

U1-U8 added contract tests for Maths degeneracies, result/allocation primitives,
physics store identity, broadphase/force/terrain stages, sleep and solver state,
replay codecs, startup parsing, schema/config migration, fixed-seed properties,
runtime interaction, scene requests, and replay value owners. U9 added:

- a real one-body solver-to-presentation artifact with checkpoints, events,
  cursors, solver hashes, and all public loader round-trips;
- all nine sphere/box/hull object-manifold dispatch pairings, terrain sweep and
  resting-contact paths, and box/hull partial-submersion force paths;
- a direct relaxed-versus-stretched point-joint island decision test, including
  the diagnostic sleep-block flag required by U4;
- a linked `ReplayTimeline` test for retention changes, recorder reset
  atomicity, and owner-event sequencing.

No assertion-free padding, wall-clock dependency, nondeterministic seed, or
private implementation-byte mirror was added. All five property tests use
bounded loops and literal seeds named in their cases. Exact replay layout
coordinates are retained only where they are user-visible hit/draw contracts;
the same cases also assert clamping, containment, and shared geometry.

## Independent Review

One read-only independent reviewer sampled 29 added cases across U1-U8. It
found no assertion-free padding and called out strong boundary, state-machine,
adversarial-decode, exact-diagnostic, precedence, and ring-wrap cases. Its four
blocking findings were resolved as follows:

1. Partial Cobertura scope omission: fixed with required-source enforcement and
   the explicit U10 rulings above.
2. Tier-2 floors below target at the reviewed U8 snapshot: closed by the U9
   artifact, manifold, terrain, buoyancy, timeline, and point-joint cases.
3. Missing U4 point-joint relaxation and U5 full-track evidence: covered by
   named direct behavioral cases.
4. Neutral replay layout naming: resolved by the recorded owner ruling that
   pure layout/value math is outside artifact/presentation-fidelity testing.

The reviewer also noted that exact layout pixels can become golden mirrors.
The retained assertions describe stable public geometry and are paired with
boundary and containment invariants; they do not mirror renderer output.

## Replay And Baseline Accounting

U5 issued the campaign's one and only
`tools\validate_replay_visual_fidelity.bat` invocation. It ran one Automation
engine process, generated one 2,401-tick/200-body artifact, and passed causal,
semantic-packet, byte-mutation, prediction-state, and determinism false-pass
controls. The engine portion took about 6 minutes 26 seconds after a 23.25-second
Automation build.

No later task launched that mega gate. No physics CSV baseline, visual golden,
scene value, shader, or authored behavior was changed. U5 updated only the
previously stale default-OFF SIMD config provenance hash and its dependent
visual-manifest hash after proving all behavioral golden values unchanged.

Owner ratification, 2026-07-17: the owner retroactively approved U5 commit
`409af3872` under the standing provenance-only reconciliation rule now recorded
in `AGENTS.md`. S4's config-v3 `physics_simd_kernels` default-OFF addition made
the metadata stale without changing runtime behavior. The mechanically verified
changes were:

- `configSha256`:
  `b4c4bd4bcc34459e78e23143a347a6d680f573b2ed803731c5c54630de47b4d6`
  → `fede1ca110a51b3368fabdf1e5b9712352e29a4b99139ee025b8da2ab3d7d3f1`,
  matching the committed `SkullbonezData/engine.cfg` bytes; and
- dependent `visualBaselineSha256`:
  `148451d83c520df15e4ee1bce589e7adc892ec0304babcf60c5ccdc39c8c8391`
  → `f4a247de2d7778b17d93f8dd421dad678aed145f96a100f9f4e84f35c64b82f4`,
  cascading solely from the visual manifest's provenance-field change.

All non-hash visual, causal, tick, scene, shader, artifact, and physics-baseline
values remained unchanged, and the reconciled replay visual-fidelity gate
passed. This closes the missing-approval finding without authorizing any other
kind of golden refresh.

The campaign's only product-source change was the prerequisite commit
`37b56828`, which sized sleep visual-id rows before the first mirror so Debug
coverage could execute the existing test suite. All U0-U9 campaign commits
after that prerequisite changed tests, tooling, project metadata, or docs only.

## Comment Quality Audit

Touched-file checklist:

- [x] `SkullbonezTests/TestCoverageFloorContracts.cpp`
- [x] `SkullbonezTests/TestPhysicsStageState.cpp`
- [x] `tools/check_coverage.py`

Checked: 3. Deferred: 0. Unchecked: 0. Headers teach the artifact, manifold,
buoyancy, point-joint relaxation, and positive-scope contracts; local comments
name the fixed-store lifetime and the diagnostic/fail-closed invariants. No
human-approved wording remains outstanding.

## Final Validation

- `tools\validate_fast.bat` — passed in 52.53 seconds. Formatting, project
  metadata, staged-size policy, Profile/Debug builds, and 282 doctest cases /
  21,388 assertions passed with zero warnings.
- `python tools\check_coverage.py --self-test` — passed the path merge,
  exclusion, floor-breach, and missing-required-source controls.
- `tools\validate_coverage.bat` — passed in 17.21 seconds. All ten subsystems
  met their armed floors and every required translation unit was present.
- `tools\validate_full.bat` — passed in 153.88 seconds. The mandatory CPU
  umbrella, Automation replay/prediction smoke, Debug build, DX12 renderer
  screenshot comparisons, zero InfoQueue errors, standalone physics smoke,
  and the 44,401-line byte-exact physics regression all passed.

Logs:

- `TestOutput/u9_final_validate_fast.log`
- `TestOutput/u9_final_validate_coverage.log`
- `TestOutput/u9_final_validate_full.log`

## Closure State

U0-U9 are complete. The live plan is deleted under MASTER inventory rule 4;
this report and git history are the evidence archive. The coverage campaign is
removed from the active/future ledger. `physics-soa-simd-1000-bodies` remains
paused at 6/9 under the owner's current instruction; neither S6 nor S7 was
started.
