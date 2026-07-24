# Solar Prediction Presentation Correction Closure

Date: 2026-07-25

Branch: `nightrunner-24th-JUL-26`

Result: Complete (SP0-SP3, 4/4)

## Outcome

Solar-system scenes now use an oblique XY orbital plane with Z up, retained
prediction geometry refreshes while a velocity drag is held, and the Mars
demonstration projects a complete 120-second, 14,401-frame future. The scene
contains the Sun, eight planets, 22 named moons, and one rocket.

The original long radial paths were authored motion, not a second prediction
integrator: the rocket began above solar escape speed and Callisto/Titania left
their parent systems. The corrected scene retains every moon within 1.5 times
its initial parent-relative radius over 120 seconds. Live and prediction paths
continue to use the same fixed-step `PhysicsEngine::Step` contract.

## Numeric Proof

| Measurement | Result |
|---|---:|
| Prediction horizon | 120 s / 14,401 frames |
| Live and Replay rocket endpoint | `(18.169813, 89.437309, 0)` |
| Closest non-contact Earth pass | 2.273692 units at 1.433333 s |
| Closest non-contact Mars arrival | 1.630474 units at 7.600000 s |
| Mars miss without Earth gravity | 17.56 units |
| Maximum rocket heliocentric radius | 165.45 units |
| Final rocket heliocentric radius | 91.26 units |
| Maximum scene-body heliocentric radius | below 450 units |
| Moon parent-relative bound | all 22 below 1.5x initial radius |

The focused Automation probe reads the actual `ReplayPrediction` publication
and asserts its final target point against the live 120-second physics oracle.
The no-Earth-gravity negative control establishes that the improved Mars
arrival depends on the Earth assist rather than only on launch authoring.

## Presentation And Runtime Policy

- One Render-tab section exposes width, alpha, and analytic edge feather for
  future, causal, baseline, and marker paths, plus selected-path emphasis.
- The accepted edge-feather range is consistently `0.25..1.25` in config, UI,
  command application, CPU packing, and shader consumption.
- A live appearance edit invalidates only retained packed geometry; prediction
  samples remain authoritative. Automation observed one real initialization
  invalidation through `SetReplayTrajectoryAppearance`.
- `Save path style` persists all 13 values to
  `SkullbonezData/engine.cfg`; reload and stable output order are covered.
- Both solar scenes explicitly disable shadows and water reflections. Disabled
  shadows now clear stale receiver values without scheduling `ShadowMapPass`.
  The real renderer frame snapshot reports `shadowPassExecuted=false`,
  `terrainShadowValid=false`, `objectShadowValid=false`, and
  `reflectionPassExecuted=false`.

## Validation

| Check | Result |
|---|---|
| `tools\validate_tests.bat` | PASS — 383 tests / 2,403,142 assertions |
| 120-second solar Automation probe | PASS — 16/16 assertions, exact Replay endpoint, no space passes |
| `tools\validate_alt_velocity_visualization.bat` | PASS — instant and amortized held-drag generations update before release |
| `tools\validate_fast.bat` | PASS — formatting, metadata, Profile/Debug builds |
| `tools\validate_full.bat` | PASS — CPU, Automation/replay, DX12, and physics lanes in 165.7 s |
| Runtime allocation policy | PASS — no unapproved finding |
| Dependency direction | PASS — all 23 standing proofs returned no rows |
| Comment audit | PASS — 39/39 touched source-bearing files inspected; 0 deferred |
| CodeGraph sync | PASS — 1,056 files / 32,489 nodes, current |

`tools\validate_replay_visual_fidelity.bat` passed its build, launcher, and 17
focused tests. The task-authorized config provenance leaf was reconciled to
`56ce99286c3fefc6491b8927169b1ac7471922690ccb0e80b4430e1b0f5be9cb`,
which changed the causal baseline's visual hash to
`037e63dc2a4c83dd01ea6827a3f361a67f02b49ef314db683563da3f95fb7e4a`.
The remaining offline gate is blocked by unrelated pre-existing shader-tree
provenance (`69e4feec...` expected, `9eb65830...` current). No shader source was
changed or baseline-authorized by this task, so that provenance was not
silently rewritten. The final broad DX12 lane passes with zero validation
errors and accepted screenshots.

## Independent Review

Independent `$rubber-duck` reviewer `/root/solar_slingshot_review` rejected the
first candidate. It found two blocking defects (an empty disabled shadow pass
and a `3.0` feather range clamped to `1.25`) and three evidence gaps (actual
Replay/live endpoint equality, parent-relative moon bounds, and real appearance
invalidation). All findings were remediated before the final gates. Per the
orchestrator contract, the independent review was not looped after remediation.

## Evidence

- Probe report:
  `TestOutput/interaction/solar_system_mars_slingshot_report.json`
- Slingshot capture:
  `TestOutput/interaction/solar_system_mars_slingshot.bmp`
- Render controls capture:
  `TestOutput/interaction/solar_path_ui_open.bmp`
