# Fable-03 Phase 2 Private Prediction Engine

Date: 2026-07-07
Branch: `nightrunner-7th-july`

## Result

Completed phase 2 of `fable_plans/03-prediction-isolated-world-progress.md`.
Replay prediction now simulates future frames in its own replay-owned
`PhysicsEngine`; it no longer temporarily writes live physics stores and then
restores them. PHYS-035 is not marked done yet because fable-03 phase 4 still
owns the boundary guardrail, runtime-reference update, and ledger closure.

## Change

- Added a lazy `RunReplayPredictionState::predictionEngine` plus captured
  `PhysicsWorldForces` and readiness state. The source comment names owner
  `replay_prediction_working_set`, reason, deletion condition, and 256 MB
  checker budget.
- Added `SeedReplayPredictionEngine(...)`, which reserves private-engine bytes
  under the replay reserve owner, constructs the engine inside replay allocation
  scope, copies live physics state, restores the captured solver snapshot, and
  reapplies prediction body state into the private store.
- Rewrote `StepReplayPredictionJob(...)` to step and sample the private engine.
  The live restore/apply/suppression blocks, `liveRestore*` state, mutation
  reserve helper, and mutation-window comments were deleted.
- Added `liveSolverHashStableAcrossPrediction` to the interaction assertion
  vocabulary and to
  `SkullbonezData/interaction/prediction_ragdoll_wall_200_predict.json`.
- Refreshed `TestOutput/baselines/dx12_perf.json` and
  `TestOutput/baselines/physics_bench_perf.json` with
  `tools\update_baselines.bat --perf --require` after the static allocation
  checker and allocation-guard proofs were clean but the current-machine
  `validate_perf` comparison exceeded the older startup-memory baseline.
- Added an allocation-policy allowlist entry for
  `std::make_unique<PhysicsEngine>` in the replay helper. The construction is
  lazy and scoped to replay reserve accounting so perf-smoke startup does not
  pay the private engine reserve cost.
- After rubber-duck review, changed the private-engine reserve request to use
  the retained engine's current byte estimate as `oldCapacity` and to skip
  growth requests when the estimate has not increased. That keeps repeated
  prediction rebuilds from recording artificial replay growth events.

## Proofs

```text
rg -n "liveRestore|MutationReserve" SkullbonezSource
no hits

rg -n "mutation window|temporarily fast-forward|temporarily swaps|RestoreLive|StepReplayPredictionPhysicsTick" SkullbonezSource\Runtime\Replay
no hits
```

```text
Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\prediction_ragdoll_wall_200.scene.json --interaction-script SkullbonezData\interaction\prediction_ragdoll_wall_200_predict.json --interaction-report TestOutput\interaction\prediction_ragdoll_wall_200_predict_report.json --frames 220 --replay on --replay-seconds 2 --fixed-step --vsync off --allocation-guard gameplay
FABLE03_P2_POST_DUCK_RAGDOLL_PROOF_EXIT=0
FABLE03_P2_POST_DUCK_RAGDOLL_PROOF_ELAPSED_SECONDS=4.537
```

Interaction report evidence:
- `ok=1`
- `liveSolverHashStableAcrossPrediction` passed at frame 165
- `finalState.predictionSourceSolverHash` =
  `finalState.liveSolverHash` = `10105770546383963666`
- gameplay allocation-guard violations = 0
- `RunReplayPredictionState::predictionEngine` reserve granted
  `161955200` bytes under `replay_prediction_working_set`

## Validation

```text
tools\validate_physics.bat
PASS: physics_regression_solver.csv (20001 lines, byte-exact match)
VALIDATE_PHYSICS: ALL PASSED
FABLE03_P2_POST_DUCK_VALIDATE_PHYSICS_EXIT=0
FABLE03_P2_POST_DUCK_VALIDATE_PHYSICS_ELAPSED_SECONDS=13.519

tools\update_baselines.bat --perf --require
FABLE03_P2_UPDATE_PERF_BASELINES_EXIT=0
FABLE03_P2_UPDATE_PERF_BASELINES_ELAPSED_SECONDS=0.170

tools\validate_perf.bat
Runtime boundary summary: TestOutput\validation\runtime_boundaries\summary.json (0 errors)
ALLOCATION_POLICY: OK direct_heap_findings=31 allowlist_errors=0
VALIDATE_PERF: ALL PASSED
FABLE03_P2_POST_DUCK_VALIDATE_PERF_EXIT=0
FABLE03_P2_POST_DUCK_VALIDATE_PERF_ELAPSED_SECONDS=30.648

tools\validate_format.bat
PASS: All source files correctly formatted.
FABLE03_P2_POST_DUCK_VALIDATE_FORMAT_EXIT=0
FABLE03_P2_POST_DUCK_VALIDATE_FORMAT_ELAPSED_SECONDS=8.345

tools\validate_full.bat
DX12 validation errors: 0
PASS: DX12 screenshots match committed baselines.
PASS: physics_regression_solver.csv (20001 lines, byte-exact match)
VALIDATE_FULL: DEFAULT GATE PASSED
FABLE03_P2_POST_DUCK_VALIDATE_FULL_EXIT=0
FABLE03_P2_POST_DUCK_VALIDATE_FULL_ELAPSED_SECONDS=39.288
```

Logs:
- `Agentic/Reports/2026-07-07/logs/fable-03-p2-post-duck-validate-physics.log`
- `Agentic/Reports/2026-07-07/logs/fable-03-p2-post-duck-prediction-ragdoll-proof.log`
- `Agentic/Reports/2026-07-07/logs/fable-03-p2-update-perf-baselines.log`
- `Agentic/Reports/2026-07-07/logs/fable-03-p2-post-duck-validate-perf.log`
- `Agentic/Reports/2026-07-07/logs/fable-03-p2-post-duck-validate-format.log`
- `Agentic/Reports/2026-07-07/logs/fable-03-p2-post-duck-profile-build.log`
- `Agentic/Reports/2026-07-07/logs/fable-03-p2-post-duck-validate-full.log`

## Rubber-Duck Follow-Up

Independent review found no blockers. It raised two non-blocking accounting
notes:

- The private-engine byte estimate is conservative because it counts
  `sizeof(PhysicsEngine)` plus vector capacities owned by the embedded scene.
  The proof still sits below the 256 MB hard cap, so this remains accepted
  conservative accounting for phase 2.
- The original private-engine reserve request used `oldCapacity=0` every time.
  That was fixed before commit by using the retained prediction engine's
  current estimated byte capacity and skipping the request when no growth is
  needed.

The review also asked for a written perf-baseline acceptance rationale. The
baseline refresh was accepted because allocation-policy scan and gameplay
allocation guard were clean, absolute perf budgets passed, the code only adds
lazy replay-phase private prediction storage, and the failing comparison was
against older current-machine startup-memory baselines rather than a gameplay
allocation violation.

## Comment Audit

Touched source-bearing files were inspected against
`Agentic/Reference/comment-style-guide.md` and
`Agentic/Skills/comment-style-audit/skill.md`:
`ReplayRuntime.cpp`, `ReplayRuntime.h`, `RunReplayPredictionHelpers.inl`,
`RunReplayPredictionVisualizer.inl`, `RunReplayTools.cpp`,
`RunInteractionAutomation.cpp`, and `RunState.h`.

The edited replay files retain learning headers and now teach the private
prediction engine, reserve accounting, one-tick-per-slice publication rule,
and the absence of live-store mutation. The interaction automation helper has a
nearby `Concept:` note explaining that the new assertion proves prediction did
not perturb the paused live solver sample.
