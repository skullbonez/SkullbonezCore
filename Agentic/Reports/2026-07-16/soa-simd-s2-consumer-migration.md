# Physics SoA/SIMD S2 Consumer Migration Evidence

Date: 2026-07-16
Plan task: S2
Commit subject target: `physics-soa-simd-1000-bodies, TASK 3 / 9, 33% OVERALL COMPLETE — migrate consumers to direct SoA views`

## Result

`PhysicsBodyRecord` now contains cold metadata only. The 20 aligned component
arrays in `PhysicsBodyStore` are the sole live authority for position,
orientation, linear and angular velocity, inverse mass and inertia, bounding
radius, fixed state, and awake state. Stage contexts borrow narrow const or
mutable spans, while cold creation/restore boundaries use the one-row
`PhysicsBodyHotState` value.

Every physics stage owner, standalone API path, replay capture/restore/probe,
presentation mirror, editor tool, automation hash, diagnostics sink, scene
snapshot, and focused test was migrated to direct hot-field views. The S1
compatibility authority, dirty state, record-to-array copy, array-to-record
copy, and prepare-view helpers were deleted. Searches for
`HotFieldAuthority`, `PrepareMutableRecordView`, `PrepareRecordView`,
`PrepareHotFields`, `CopyRecordViewToHotFields`, and
`CopyHotFieldsToRecordView` return zero source/test results.

The migration preserves iteration order and performs the same scalar arithmetic
on one-row values. It adds no SIMD, FMA, dispatch, FTZ/DAZ, baseline refresh, or
golden refresh. The final broad gate proves the original 44,401-line physics
artifact remains byte-exact. The sole replay mega invocation also passed its
durable artifact save/load oracle and byte-level false-pass controls with one
engine process and one prediction generation, so the already-canonicalized R3
bookkeeping mismatch remains exact rather than being excluded or weakened by
S2.

## Validation

- Targeted Profile product build: 0 warnings, 0 errors in 8.47 s.
- Targeted Profile test build: 0 warnings, 0 errors in 2.72 s.
- Direct focused test executable: 204/204 cases and 17,324/17,324 assertions
  passed in 3.70 s.
- Targeted Debug build initially exposed Debug-only diagnostics, launcher, and
  replay-probe consumers; after migration it passed with 0 warnings and 0
  errors in 12.21 s. A later targeted Automation build exposed and closed the
  Automation-only editor fingerprint/velocity consumer, then passed with 0
  warnings and 0 errors in 15.21 s.
- `tools\validate_tests.bat`: 204/204 cases and 17,324/17,324 assertions passed
  in 13.13 s. Log SHA-256:
  `F8417A708597DDE31F06C1DFD725335E8ED5A8BAEAD76992E275C671D7D67B3E`.
- `tools\validate_physics.bat`: passed in 55.58 s; the 44,401-line
  `physics_regression_varied.csv` matched byte-for-byte. The final
  `tools\validate_full.bat` repeated this exact physics comparison after all
  configuration-specific repairs. Explicit physics log SHA-256:
  `5D11105E2E3955E27572EDB2D7CA836ACF2D3E1A808B9E56457ABD2B80D5E270`.
- `tools\validate_full.bat`: passed from final source in 127.12 s. CPU tests,
  Profile/Automation/Debug builds, automation and replay/prediction smokes,
  DX12 screenshots, zero DX12 InfoQueue errors, standalone physics, and the
  byte-exact varied CSV all passed. Log SHA-256:
  `686B0F3CEFB62B33811FA0F7EFE5A6269F605F1495A7E6ED64E1C758941C952C`.
- `python tools\check_allocation_policy.py --self-test` and
  `python tools\check_allocation_policy.py --repo .`: passed in 8.52 s;
  370 files scanned and 0 allowlist errors. No allowlist row changed.
- Exactly one `tools\validate_replay_visual_fidelity.bat` invocation: passed in
  427.08 s with `engine_processes=1`, `prediction_starts=1`, 2,401 ticks, 200
  moved bricks, 187 toppled bricks, 199 causal nodes, durable save/load proof,
  and all false-pass controls. Log SHA-256:
  `E5C10E71415A28F02B80A791CB2D609805B87AA4FBAD1A8C1D94273EBF1349F2`.
- No baseline, golden, screenshot reference, shader source, or perf reference
  changed.

Formatting-only gate failures were repaired only in touched S2 files. The first
broad run then exposed the Automation-only stale consumer described above; no
runtime lane or mega invocation was consumed by those preflight failures.

## Comment Quality Audit Checklist

Checklist path: this report. Touched source-bearing inventory: 61. Checked: 61.
Deferred: 0. Unchecked: none. Every item has a learning header with `File`,
`Purpose`, and `Glossary`; changed ownership-sensitive areas also document the
hot-array authority, borrowed span lifetime, determinism, or cold/hot boundary.
No term needs human-approved wording.

- [x] `Agentic/Manuals/SkullbonezCoreManual/build_manual.py`
- [x] `SkullbonezSource/Physics/BuoyancySystem.cpp`
- [x] `SkullbonezSource/Physics/BuoyancySystem.h`
- [x] `SkullbonezSource/Physics/PersistentContactSolver.cpp`
- [x] `SkullbonezSource/Physics/PersistentContactSolver.h`
- [x] `SkullbonezSource/Physics/PhysicsApi.cpp`
- [x] `SkullbonezSource/Physics/PhysicsApi.h`
- [x] `SkullbonezSource/Physics/PhysicsBodyStore.cpp`
- [x] `SkullbonezSource/Physics/PhysicsBodyStore.h`
- [x] `SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp`
- [x] `SkullbonezSource/Physics/PhysicsScene.cpp`
- [x] `SkullbonezSource/Physics/PhysicsWorld.cpp`
- [x] `SkullbonezSource/Physics/Ragdoll.cpp`
- [x] `SkullbonezSource/Physics/SleepIslandSystem.cpp`
- [x] `SkullbonezSource/Physics/SleepIslandSystem.h`
- [x] `SkullbonezSource/Physics/SolverBroadphaseStage.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsForceStage.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsSleepController.Wake.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsSleepController.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsStageContexts.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.h`
- [x] `SkullbonezSource/Physics/TornadoGameplay.cpp`
- [x] `SkullbonezSource/Rendering/RenderInstanceStore.cpp`
- [x] `SkullbonezSource/Rendering/RenderInstanceStore.h`
- [x] `SkullbonezSource/Runtime/AttachedCameraController.cpp`
- [x] `SkullbonezSource/Runtime/Debug/PhysicsDebugVisualizer.cpp`
- [x] `SkullbonezSource/Runtime/Editor/LauncherTools.cpp`
- [x] `SkullbonezSource/Runtime/Editor/RunEditorHistory.cpp`
- [x] `SkullbonezSource/Runtime/Editor/RunEditorOverlayTools.cpp`
- [x] `SkullbonezSource/Runtime/Editor/RunEditorTools.cpp`
- [x] `SkullbonezSource/Runtime/Editor/RunMousePickupTools.cpp`
- [x] `SkullbonezSource/Runtime/InteractionAutomationController.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayAuthoringCauseTree.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayAuthoringVelocity.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayValidation.Probes.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayValidation.cpp`
- [x] `SkullbonezSource/Runtime/RunFrame.cpp`
- [x] `SkullbonezSource/Runtime/RuntimePickService.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneController.Objects.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntime.cpp`
- [x] `SkullbonezSource/Runtime/Startup/StartupProbeHarnesses.cpp`
- [x] `SkullbonezSource/Runtime/Tools/RuntimeTools.cpp`
- [x] `SkullbonezSource/Scene/SceneSnapshotWriter.cpp`
- [x] `SkullbonezTests/TestDeterminism.cpp`
- [x] `SkullbonezTests/TestPersistentContactSolver.cpp`
- [x] `SkullbonezTests/TestPhysicsHandles.cpp`
- [x] `SkullbonezTests/TestSceneEntityStore.cpp`
- [x] `SkullbonezTests/TestSceneSnapshotWriter.cpp`
- [x] `SkullbonezTests/TestSolverBroadphaseStage.cpp`
