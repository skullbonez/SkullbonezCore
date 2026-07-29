# Runtime Include-Closure Reduction Closure

Date: 2026-07-29  
Branch: `nightrunner-29th-JUL-26`  
Plan: `runtime-include-closure-reduction`  
Result: complete

## Outcome

The public `PhysicsEngine` contract no longer exposes `PhysicsWorld`, its
sixteen solver-stage headers, or `SpatialGrid`. `PhysicsWorld.h` and
`SpatialGrid.h` are now reachable only from Physics implementation translation
units.

The final first-party include graph contains 254 translation units and 318
headers. The strict heavy-TU acceptance count falls from 17 to 16, and the
maximum closure falls from 255 to 248.

| Measure | IC0 baseline | Final | Change |
|---|---:|---:|---:|
| TUs above 200 headers | 17 | 16 | -1 |
| TUs above 150 headers | 26 | 23 | -3 |
| TUs above 100 headers | 62 | 55 | -7 |
| Lower median | 38 | 39 | +1 |
| Maximum | 255 | 248 | -7 |
| First-party headers | 317 | 318 | +1 concrete value contract |

The median increase is reported honestly. The work targeted the heavy tail,
not the already-small median.

## Heavy-Header Fan-In

| Header | IC0 TUs | Final TUs | Non-Physics final |
|---|---:|---:|---:|
| `Physics/PhysicsBodyStore.h` | 113 | 113 | not an acceptance target |
| `Physics/SpatialGrid.h` | 73 | 8 | 0 |
| `Physics/PhysicsWorld.h` | 66 | 6 | 0 |
| `Physics/PhysicsEngine.h` | 61 | 58 | not an acceptance target |
| `UI/UI.h` | 43 | 43 | not an acceptance target |
| `Runtime/Scene/SceneController.h` | 37 | 37 | not an acceptance target |

The census uses the same tracked-source resolver recorded in
`runtime-include-closure-reduction-ic0-census.md`: local-relative,
`SkullbonezSource`-relative, then unique-name resolution followed by transitive
walking of first-party include edges.

## Implemented Boundaries

- Six value-only consumers include `PhysicsApi.h` instead of
  `PhysicsEngine.h`.
- `PhysicsEngine` retains sole `PhysicsWorld` authority through one fixed-size
  `unique_ptr` with out-of-line lifetime. The pointer remains private, solver
  loops remain inside `PhysicsWorld`, and engine-side fixed-tree body iteration
  binds the world before looping.
- `PhysicsDiagnosticsView.h` owns concrete Physics diagnostics records and the
  cohesive borrowed diagnostics view.
- `PhysicsBroadphaseDebugView.h` publishes only bounded active-cell values.
  `PhysicsStageCapacity.h` remains the authoritative owner of the 8,192-bucket
  solver ceiling; the diagnostic contract derives its bound from that owner.
- Runtime visualization and automation consume spans of detached cell values;
  neither receives `SpatialGrid` authority.
- The unused `SceneSessionState.h -> SpatialGrid.h` edge is gone.
- `SceneLoadTransaction.h` forward-declares `SceneLoadBeginResult`; source files
  that use the complete value include `SceneLoadPreparation.h` directly. This
  removed the last one-header closure row needed to move
  `SceneLoadTransaction.Reset.cpp` from 201 to 200.
- `UI.cpp` directly includes the Cinematic and Sky tab declarations it invokes.
- `UI.h` retains the profiler tab and removes nine redundant direct tab
  includes; the corrected starting direct-tab census was ten, not eleven.

No forwarding header, compatibility alias, umbrella forward-declaration file,
service/context bag, or alternate owner convention was introduced.

## Accounting Correction

`ReplayPredictionEngineMemoryBytes` previously added both the complete
`PhysicsWorld` memory total and the debug/broadphase subset already contained in
that total. The duplicate subset addition is removed. This is an intentional
telemetry and reserve-request correction, not a claim that all runtime metadata
is byte-identical. Physics, replay artifacts, and visual output remain
byte-identical under the mapped gates.

## Clean Rebuild Timing

Both final rebuilds used:

```text
MSBuild SKULLBONEZ_CORE.sln /t:Rebuild /p:Configuration=<config>
  /p:Platform=x64 /nologo /v:normal
  /clp:Summary;PerformanceSummary /warnaserror
```

| Configuration | IC0 | Final | Delta | Result |
|---|---:|---:|---:|---|
| Debug x64 | 43.066 s | 44.860 s | +1.794 s (+4.17%) | PASS |
| Profile x64 | 44.614 s | 45.358 s | +0.744 s (+1.67%) | PASS |

These are single local samples. Build time is unchanged to slightly slower; no
speedup is claimed. Ignored raw logs are
`TestOutput/validation/agent_logs/ic3_rebuild_debug.out.log` and
`ic3_rebuild_profile.out.log`.

## Validation

All final mapped gates pass:

- `tools\validate_full.bat`
- `tools\validate_physics.bat`
- `tools\validate_dx12_renderer.bat`
- `tools\run_graphics_stress.bat 1`
- `tools\validate_replay_visual_fidelity.bat`

The full gate passes formatting, 789/789 project/filter items, the current
dependency proof, allocation policy, all four ownership inventories, Profile
and Debug builds, CPU tests, and runtime lanes. Function complexity is 40/40
current rulings at the ratified 400-line/depth-6 triggers.

Physics determinism remains byte-exact. DX12 InfoQueue reports zero validation
errors, and all three committed screenshots pass without baseline refresh:

| Capture | Average diff | Maximum diff | Pixels over 10 |
|---|---:|---:|---:|
| `water_ball_test` | 0.0000 | 0 | 0 |
| `solver_smoke` | 0.0001 | 33 | 3 |
| `space_three_body` | 0.4245 | 255 | 6,722 |

Replay visual fidelity passes one 2,401-tick authoritative engine run with 200
moved wall bricks, 175 toppled wall bricks, 200 causal nodes, one presented
cascade, and every negative/false-pass control detected.

The first full-gate attempt found formatting in three touched files. The second
found the expected exact-body digest drift in `SkullScope::EmitFrame` after its
published cell type changed. Both localized findings were repaired; the final
gate passes. No baseline, golden, scene, engine configuration, or schema was
refreshed.

## Independent Review

The independent review first found three closure blockers: remaining Runtime
`SpatialGrid` consumers, duplicated replay memory accounting, and an overstrong
pointer-crossing claim. Its working-tree re-review then found the solver
capacity temporarily owned by the diagnostic value header and the expected
complexity digest drift. Each finding was repaired before closure. The final
read-only verdict reports no remaining blocker.

## Comment Audit

The exact plan-start-to-final tracked source-bearing inventory is 30/30 checked,
with zero deferred:

- [x] `SkullbonezSource/Physics/Diagnostics/SkullScope.cpp`
- [x] `SkullbonezSource/Physics/PhysicsBroadphaseDebugView.h`
- [x] `SkullbonezSource/Physics/PhysicsDiagnosticsView.h`
- [x] `SkullbonezSource/Physics/PhysicsEngine.cpp`
- [x] `SkullbonezSource/Physics/PhysicsEngine.h`
- [x] `SkullbonezSource/Physics/PhysicsStageCapacity.h`
- [x] `SkullbonezSource/Physics/PhysicsWorld.h`
- [x] `SkullbonezSource/Physics/SpatialGrid.cpp`
- [x] `SkullbonezSource/Physics/SpatialGrid.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h`
- [x] `SkullbonezSource/Runtime/Automation/RuntimeValidationHarness.cpp`
- [x] `SkullbonezSource/Runtime/Automation/RuntimeValidationHarness.h`
- [x] `SkullbonezSource/Runtime/Camera/AttachedCameraController.cpp`
- [x] `SkullbonezSource/Runtime/Debug/BroadphaseVisualizer.cpp`
- [x] `SkullbonezSource/Runtime/Debug/BroadphaseVisualizer.h`
- [x] `SkullbonezSource/Runtime/Diagnostics/RuntimeOverlayDiagnostics.cpp`
- [x] `SkullbonezSource/Runtime/Editor/EditorGizmoTools.cpp`
- [x] `SkullbonezSource/Runtime/Editor/EditorObjectPlacement.cpp`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPrediction.h`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.cpp`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.cpp`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionScheduling.cpp`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionTopologyPublication.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneLoadTransaction.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneSessionState.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneSessionState.h`
- [x] `SkullbonezSource/UI/UI.cpp`
- [x] `SkullbonezSource/UI/UI.h`
- [x] `tools/validate_project_filters.py`

Every file has the required learning-header context or qualifies as a
self-explanatory tooling table edit. Dense ownership, lifetime, capacity,
diagnostic-projection, reserve-accounting, and loop-binding decisions have
nearby `Why:`, `Invariant:`, `Lifetime:`, or `Hazard:` guidance where needed.
