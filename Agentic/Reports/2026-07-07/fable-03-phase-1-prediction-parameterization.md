# Fable-03 Phase 1 Prediction Parameterization

Date: 2026-07-07
Branch: `nightrunner-7th-july`

## Result

Completed phase 1 of `fable_plans/03-prediction-isolated-world-progress.md`.
This is behavior-preserving groundwork for PHYS-035: prediction still uses the
live mutation window, but body capture/apply/sample helpers now receive an
explicit `PhysicsBodyStore` input and a staged pure prediction tick exists for
the phase-2 private-engine switch.

## Change

- Recorded phase-0 discovery answers in the fable-03 progress checklist:
  `PhysicsEngine` construction/ownership, reserve shape, `PhysicsWorld`
  snapshot coverage, zero physics singleton/global hits, body-helper access,
  and collider immutability during stepping.
- Changed `CaptureReplayPredictionBodyState`,
  `ApplyReplayPredictionBodyState`, and `CaptureReplayPredictionFrame` to take
  an explicit body store instead of reading it internally through
  `GameModelCollection::GetPhysicsEngine()`.
- Updated visualizer call sites to pass the live `PhysicsBodyStore`.
- Added staged `StepPredictionEngineTick(...)` beside the current live
  `StepReplayPredictionPhysicsTick(...)`. It is marked `[[maybe_unused]]`
  until phase 2 switches prediction to a private `PhysicsEngine`.

## Validation

```text
tools\validate_build.bat Profile
Build succeeded.
    0 Warning(s)
    0 Error(s)
FABLE03_P1_BUILD_PROFILE_EXIT=0
FABLE03_P1_BUILD_PROFILE_ELAPSED_SECONDS=8.955

tools\validate_physics.bat
PASS: physics_regression_solver.csv (20001 lines, byte-exact match)
VALIDATE_PHYSICS: ALL PASSED
FABLE03_P1_VALIDATE_PHYSICS_EXIT=0
FABLE03_P1_VALIDATE_PHYSICS_ELAPSED_SECONDS=18.514

tools\validate_full.bat
DX12 validation errors: 0
PASS: DX12 screenshots match committed baselines.
PASS: physics_regression_solver.csv (20001 lines, byte-exact match)
VALIDATE_FULL: DEFAULT GATE PASSED
FABLE03_P1_VALIDATE_FULL_EXIT=0
FABLE03_P1_VALIDATE_FULL_ELAPSED_SECONDS=40.456
```

Logs:
- `Agentic/Reports/2026-07-07/logs/fable-03-p1-profile-build.log`
- `Agentic/Reports/2026-07-07/logs/fable-03-p1-validate-physics.log`
- `Agentic/Reports/2026-07-07/logs/fable-03-p1-validate-full.log`

## Comment Audit

Touched source-bearing files were inspected against
`Agentic/Reference/comment-style-guide.md` and
`Agentic/Skills/comment-style-audit/skill.md`:
`RunReplayPredictionHelpers.inl`, `RunReplayPredictionVisualizer.inl`, and
`RunReplayTools.cpp`. All three already had learning headers. The new
prediction tick has a nearby `Concept:` comment explaining why it has no
presentation side effects and why it is staged for phase 2.
