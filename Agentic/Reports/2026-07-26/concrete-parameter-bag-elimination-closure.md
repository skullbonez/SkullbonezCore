# Concrete Parameter-Bag Elimination Closure

Date: 2026-07-26

Branch: `nightrunner-25th-JUL-26`

Plan result: PB0-PB7 complete (8/8)

Ratified implementation base: `e61e82a6`

## Outcome

All 30 repair rows and the three assigned wide operations are closed. The
repository now crosses the repaired boundaries through concrete owner
publications, direct focused operations, operation-specific graph ABI records,
or stack-scoped invariant owners. The final source introduces no inheritance,
interface, virtual dispatch, type erasure, callback pack, service bag, retained
owner borrow, or replacement context bag.

The final threshold-13 inventory is empty. The three assigned wide operations
finish at 11, 11, and 9 parameters.

## Final Census

`SceneSaveRequest` is the only retained registered name. It is reduced to the
approved four-field domain request: output path, `SceneWorldSaveState`,
`SceneSessionSaveState`, and `PresentationSaveState`.

| # | Registered repair row | Final endpoint / deletion proof |
|---:|---|---|
| 1 | `SceneSaveRequest` | Retained only in the approved four-field form |
| 2 | `SceneSaveView` | Deleted; writer consumes the request directly |
| 3 | `SceneLoadPolicyInputs` | Deleted; `SceneLoadTransaction` owns policy values |
| 4 | `SceneLoadConsumerOutputs` | Deleted; transaction owns output arbitration |
| 5 | `RuntimePointerRouteInput` | Deleted; `InputRouter` calls focused owners directly |
| 6 | `EditorPointerRouteInput` | Deleted; editor consumes the semantic pointer/ray values |
| 7 | `MousePickupPointerInput` | Deleted; mouse pickup consumes direct gesture/ray facts |
| 8 | `RenderFrameContext` | Deleted; `RuntimeRenderer` schedules concrete frame operations |
| 9 | `UiTextPassInputs` | Deleted; `UiTextPass` exposes focused draw/projection operations |
| 10 | `ReplayOverlayRenderContext` | Deleted; Planning publishes immutable overlay values |
| 11 | `ReplayCaptureInput` | Deleted; recorder consumes owner-produced capture values |
| 12 | `ReplayCameraFocusRequest` | Deleted; Replay presentation mutates its concrete camera state |
| 13 | `PersistentContactSolverContext` | Deleted; solver stage accepts direct stores/settings |
| 14 | `PhysicsContactSolverStageContext` | Deleted; no outer-to-inner repack remains |
| 15 | `ObjectNarrowphasePairStageContext` | Deleted; narrowphase consumes focused Physics views |
| 16 | `PhysicsSleepIslandStageContext` | Deleted; sleep owner coordinates island work directly |
| 17 | `PhysicsSleepWakeContext` | Deleted; wake operations borrow focused same-owner views |
| 18 | `PhysicsBroadphaseStageContext` | Deleted; broadphase consumes direct stores/settings/step values |
| 19 | `ExternalForceBodyContext` | Deleted; force stage uses direct Physics collaborators |
| 20 | `TerrainCandidateCommitContext` | Deleted; prepared values and concrete stores cross directly |
| 21 | `ReplayRestoreOwnerContext` | Deleted; owners are synchronous phase-method borrows |
| 22 | `ReplayRestoreStepContext` | Deleted; target/checkpoint values belong to the transaction |
| 23 | `EditorSaveHotkeyContext` | Deleted; scene save and screenshot are separate operations |
| 24 | `UiTextPassState` | Deleted; pass-owned draw state and direct operations replace it |
| 25 | Runtime render `*GraphCallbackData` family | Deleted; remaining `Ui*GraphInvocation` records are single-operation ABIs |
| 26 | Replay restore support family | Deleted; `ReplayRestoreTransaction` owns values and phase cursor only |
| 27 | `BroadphaseCandidateFilterContext` | Deleted; focused broadphase filter values cross directly |
| 28 | `TerrainDetectionStageContext` | Deleted; terrain stage accepts direct views |
| 29 | `ApplyForcesStageContext` | Deleted; force stage accepts direct values |
| 30 | `IntegrateRemainingStageContext` | Deleted; integration accepts direct values |

The final static sweep finds zero rows for all 32 retired spellings represented
by those 30 rows, zero rows for the retired 13-name graph callback-data family,
and zero rows for the rejected UI phase/payload/broad-render surfaces.

## Before / After Ownership Shapes

- Scene save: callers formerly flattened or partially initialized writer data.
  The three production entry policies now compose the same complete owner
  publications through `SceneSaveOperations`.
- Scene load: caller-built policy/output bags became the phase-checked
  `SceneLoadTransaction`.
- Pointer routing: Runtime-to-editor-to-tools projections became direct owner
  calls sequenced by the value-only `RuntimePointerArbitration`.
- Render/UI: the 45-field frame context, UI phase bags, broad callback union,
  and broad render method became focused `RuntimeRenderer` graph operations.
  Ten stack-scoped `Ui*GraphInvocation` records each describe one callback ABI;
  none nests another payload, gathers the union, or survives `RenderUiText`.
- Replay: capture and camera request bags became owner-produced values/direct
  camera mutation; restore became `ReplayRestoreTransaction`.
- Collision/solver: five contexts became direct stage/store/settings calls.
- Sleep/force/terrain: seven contexts and the obsolete shared header became
  direct stage operations that preserve worker and wake ordering.

## Scene Save Proof

The only request construction is:

```cpp
SceneSaveRequest {
    path,
    world,
    session,
    presentation
}
```

Every production entry uses the same complete publication path:

1. `EditorTools::HandleEditorSceneSaveHotkey` calls
   `TrySaveNextEditorSceneSnapshot`.
2. `Run::RunSceneLoadOnly` calls `SaveSceneLoadOnlySnapshot`.
3. `SceneController::Load` calls `SaveEditableSceneBeforeReplacement`.

Focused tests serialize all three policies and prove that their world, session,
and presentation publications survive in the written artifact.

## Invariant Owners And Focused Tests

- `SceneLoadTransaction` stores detached values and a phase cursor; its tests
  exhaust legal and illegal load/runtime/presentation transitions.
- `ReplayRestoreTransaction` stores artifact/checkpoint/target/result values and
  a phase cursor; concrete owners are borrowed only by synchronous phase calls.
- `RuntimePointerArbitration` stores only next/active/winning stages. Production
  routing uses it for Editor -> MousePickup -> AttachedCamera -> Replay ->
  Launcher, and focused tests exhaust all 32 five-stage claim masks.
- Save-entry focused tests add 68 assertions. Pointer arbitration adds 224
  assertions. The final doctest suite passes 401 cases and 2,403,889 assertions.

## Static Architecture Proof

- Allocation policy: 460 files scanned, zero allowlist errors.
- Dependency graph: zero findings.
- All 28 manual direction proofs are empty, including Core, Physics/Rendering,
  Gameplay, UI-to-Runtime, Runtime-package, Replay-boundary, and
  Rendering-feature-neutral checks.
- No added `class I*`, virtual method, `std::function`, `void*`, `Services`, or
  `Bindings` substitution appears in the campaign diff.
- The final wide-signature inventory at threshold 13 is empty.
- The original three assigned operations finish at 11/11/9 parameters.

## Independent Hostile Review

Reviewer: `/root/pb7_hostile_review`

The single read-only review initially returned `BLOCK`:

1. six UI phase structs were built together, stored in one broad graph
   invocation, and unpacked into a monolithic UI render operation;
2. `RuntimeRenderDiagnosticsSnapshot` claimed detachment while borrowing the
   renderer name through `const char*`;
3. PB1 lacked focused coverage for each save entry policy, and PB2 lacked
   focused pointer-precedence coverage.

All findings were remediated:

- the six phase structs, intermediate `UiText*GraphPayload` records, broad
  `UiTextGraphInvocation`, broad `UiOperatorGraphInvocation`,
  `UiTextPass::Render`, and `RenderOperatorSurface` are deleted;
- `RuntimeRenderer` registers focused Chrome, operator projection/submission,
  overlay, Replay, and finalize callbacks in one compile/execute cycle;
- `RenderDiagnosticsReadout` owns bounded `std::array<char, 64>` name storage;
- all three save policies have complete-publication tests; and
- pointer arbitration exhausts all 32 precedence masks.

The reviewer reported no material ownership finding in Scene load, Replay
capture/focus/restore, or PB5/PB6 Physics. The final census found no unresolved
review issue.

## Final Validation

No baseline, golden, screenshot, Replay artifact, scene, configuration, Physics
CSV, or allocation-policy inventory was refreshed.

| Command | Final-source result |
|---|---|
| `tools\validate_tests.bat` | PASS in 4.5 s; 401/401 cases, 2,403,889 assertions |
| `tools\validate_fast.bat` | PASS in 62.8 s; formatting, 784 project items, dependencies, Profile/Debug |
| `tools\validate_dx12_renderer.bat` | PASS in 51.9 s; zero InfoQueue errors, committed baselines accepted |
| `tools\run_graphics_stress.bat 1` | PASS in 61 s; expected PID timeout, no crash |
| `tools\validate_perf.bat` | PASS in 89.2 s |
| `tools\validate_replay_v2_artifact.bat` | PASS in 37.1 s |
| `tools\validate_replay_allocation_policy.bat` | PASS in 3.9 s; strict two-generation probe |
| `tools\validate_replay_scrub.bat` | PASS in 412.7 s; one engine process, one generation, 2,401 ticks |
| `tools\validate_physics.bat` | PASS in 22.5 s |
| `tools\validate_full.bat` | PASS in 230.9 s; CPU/coverage, five engine processes, byte-exact 44,401-line Physics oracle |

The first `validate_tests` attempt exposed the missing project-filter rule for
the new Scene save implementation. The rule was added and the final run passed.
The first `validate_fast` attempt exposed only mechanical formatting in six
touched files. The repository formatter was applied; `git status` confirmed
that no unrelated file acquired a diff, and the final run passed.

## Comment Audit

The exact final tracked source-bearing inventory from `e61e82a6` is 91 files:
91 checked, 0 deferred, 0 unchecked, and zero inventory delta after
regeneration.

1. `SkullbonezSource/Physics/PersistentContactSolver.cpp`
2. `SkullbonezSource/Physics/PersistentContactSolver.h`
3. `SkullbonezSource/Physics/PhysicsWorld.cpp`
4. `SkullbonezSource/Physics/PhysicsWorld.h`
5. `SkullbonezSource/Physics/SolverBroadphaseStage.h`
6. `SkullbonezSource/Physics/SpatialGrid.cpp`
7. `SkullbonezSource/Physics/SpatialGrid.h`
8. `SkullbonezSource/Physics/Stages/ExternalForceStage.cpp`
9. `SkullbonezSource/Physics/Stages/ExternalForceStage.h`
10. `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp`
11. `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h`
12. `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.cpp`
13. `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h`
14. `SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp`
15. `SkullbonezSource/Physics/Stages/PhysicsForceStage.h`
16. `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.Execution.cpp`
17. `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.cpp`
18. `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.h`
19. `SkullbonezSource/Physics/Stages/PhysicsSleepController.Wake.cpp`
20. `SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp`
21. `SkullbonezSource/Physics/Stages/PhysicsSleepController.h`
22. `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.cpp`
23. `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.h`
24. `SkullbonezSource/Runtime/App/InputFrame.cpp`
25. `SkullbonezSource/Runtime/App/InputFrameExecution.cpp`
26. `SkullbonezSource/Runtime/App/InputRouter.Interactions.cpp`
27. `SkullbonezSource/Runtime/App/ReplayRuntime.cpp`
28. `SkullbonezSource/Runtime/App/ReplayRuntime.h`
29. `SkullbonezSource/Runtime/App/ReplayScrubberTools.cpp`
30. `SkullbonezSource/Runtime/App/ReplayValidation.Probes.cpp`
31. `SkullbonezSource/Runtime/App/ReplayValidation.cpp`
32. `SkullbonezSource/Runtime/App/Run.cpp`
33. `SkullbonezSource/Runtime/App/RunFrame.cpp`
34. `SkullbonezSource/Runtime/Capture/RuntimeStressController.cpp`
35. `SkullbonezSource/Runtime/Diagnostics/OverlayDebugState.h`
36. `SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp`
37. `SkullbonezSource/Runtime/Editor/EditorTools.cpp`
38. `SkullbonezSource/Runtime/Editor/EditorTools.h`
39. `SkullbonezSource/Runtime/Editor/MousePickupTools.cpp`
40. `SkullbonezSource/Runtime/Input/InputRouter.cpp`
41. `SkullbonezSource/Runtime/Input/InputRouter.h`
42. `SkullbonezSource/Runtime/Planning/ReplayOverlayPackets.h`
43. `SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.cpp`
44. `SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.h`
45. `SkullbonezSource/Runtime/Prediction/ReplayAuthoringCauseTree.cpp`
46. `SkullbonezSource/Runtime/Render/RenderResourceLifecycle.cpp`
47. `SkullbonezSource/Runtime/Render/RenderResourceLifecycle.h`
48. `SkullbonezSource/Runtime/Render/RuntimeRenderHost.h`
49. `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp`
50. `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h`
51. `SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp`
52. `SkullbonezSource/Runtime/Render/RuntimeRenderer.h`
53. `SkullbonezSource/Runtime/Render/UiDrawSubmission.cpp`
54. `SkullbonezSource/Runtime/Render/UiDrawSubmission.h`
55. `SkullbonezSource/Runtime/Render/UiTextPass.cpp`
56. `SkullbonezSource/Runtime/Replay/ReplayCoordination.h`
57. `SkullbonezSource/Runtime/Replay/ReplayPresentation.cpp`
58. `SkullbonezSource/Runtime/Replay/ReplayPresentation.h`
59. `SkullbonezSource/Runtime/Replay/ReplayProbeState.h`
60. `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp`
61. `SkullbonezSource/Runtime/Replay/ReplayRecorder.h`
62. `SkullbonezSource/Runtime/Replay/ReplayRestoreService.h`
63. `SkullbonezSource/Runtime/Replay/ReplayRestoreTransactions.h`
64. `SkullbonezSource/Runtime/Replay/ReplayScrubber.h`
65. `SkullbonezSource/Runtime/Replay/ReplayTimeline.cpp`
66. `SkullbonezSource/Runtime/Replay/ReplayTimeline.h`
67. `SkullbonezSource/Runtime/Scene/SceneController.Load.cpp`
68. `SkullbonezSource/Runtime/Scene/SceneController.h`
69. `SkullbonezSource/Runtime/Scene/SceneLoadTransaction.h`
70. `SkullbonezSource/Runtime/Scene/SceneRequestExecution.cpp`
71. `SkullbonezSource/Runtime/Scene/SceneRuntime.cpp`
72. `SkullbonezSource/Runtime/Scene/SceneRuntime.h`
73. `SkullbonezSource/Runtime/Scene/SceneSaveOperations.cpp`
74. `SkullbonezSource/Runtime/Scene/SceneSaveOperations.h`
75. `SkullbonezSource/Runtime/Scene/SceneWorld.cpp`
76. `SkullbonezSource/Runtime/Scene/SceneWorld.h`
77. `SkullbonezSource/Runtime/Tools/RuntimeTools.h`
78. `SkullbonezSource/Runtime/UI/OperatorEditorFrameComposer.cpp`
79. `SkullbonezSource/Runtime/UI/OperatorEditorFrameComposer.h`
80. `SkullbonezSource/Scene/SceneSnapshotWriter.cpp`
81. `SkullbonezSource/Scene/SceneSnapshotWriter.h`
82. `SkullbonezTests/TestCoverageFloorContracts.cpp`
83. `SkullbonezTests/TestInputRouter.cpp`
84. `SkullbonezTests/TestOwnerRequestQueues.cpp`
85. `SkullbonezTests/TestPersistentContactSolver.cpp`
86. `SkullbonezTests/TestPhysicsStageState.cpp`
87. `SkullbonezTests/TestSceneSnapshotWriter.cpp`
88. `SkullbonezTests/TestSolverBroadphaseStage.cpp`
89. `SkullbonezTests/TestSpatialGrid.cpp`
90. `SkullbonezTests/TestTerrain.cpp`
91. `tools/validate_project_filters.py`

## Handoff

PB0-PB7 are complete. The plan leaves the live inventory under rule 4. There
are no remaining active/future implementation plans, so the live denominator
returns from 8 to 0.
