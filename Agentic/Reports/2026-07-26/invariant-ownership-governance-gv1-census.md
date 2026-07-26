# Invariant Ownership Governance GV1 Offender Census

Date: 2026-07-26
Implementation tip: `852b8f2c`
Plan phase: GV1 — Ratify the offender census
Impact: documentation only; no source behavior changed

## Result

The implementation-tip census is complete. It found two logical
invariant-shaped repair rows:

1. the pre-ruled scene-load transaction, owned by GV2; and
2. the generated-scene control rebuild transaction, owned by GV3.

Only one repair row exists beyond scene load, so GV3 stays within its
three-row cap. The registered concrete parameter-bag plan retains its 22
authoritative targets. The current wide-signature sweep additionally found
three 13-parameter render/UI operations; those are assigned to PB0 because
they violate the standing 12-parameter ceiling but do not exhibit the
ordering/arbitration transaction shape governed by this plan.

No prior retain ruling was challenged. No source behavior changed.

## Disposition vocabulary

| Disposition | Meaning |
|---|---|
| `repair-GV2` | The candidate is part of the scene-load phase-order and mid-batch arbitration transaction. |
| `repair-GV3` | The candidate is an additional invariant-shaped offender outside the 22-row concrete-bag census. |
| `assign-PB0` | The candidate is a concrete bag/signature problem assigned to the companion plan, not an invariant-ordering repair here. |
| `retain-prior` | A cited owner ruling remains binding and the implementation-tip sweep found no new evidence. |
| `wide-only` | Width alone is not the extrusion signal: the candidate has no convergence of three-or-more sibling structs, a wide apply operation, and ordering/arbitration held only by comments. Existing wide-signature governance remains binding separately. |
| `out-of-scope` | The text hit is not a function-order or aggregate-family candidate. |

## Ruled logical candidates

| Candidate | Evidence | Invariant | Current owner | Prior ruling | Disposition |
|---|---|---|---|---|---|
| Scene-load execution and follow-up arbitration | `SkullbonezSource/Runtime/Scene/SceneController.h:152-237`; `SkullbonezSource/Runtime/Scene/SceneController.Load.cpp:599-777`; duplicated callers in `Runtime/App/Run.cpp`, `Runtime/App/RunFrame.cpp`, `Runtime/App/InputFrameExecution.cpp`, `Runtime/Scene/SceneRequestExecution.cpp`, and `Runtime/Capture/RuntimeStressController.cpp` | Load must complete before runtime reactions; reactions must complete before external presentation; later requests in the same batch must select the newly authored navigation/presentation values rather than submitted stale values. | None: comments and repeated caller order. | Pre-ruled repair by the live plan. | `repair-GV2` |
| Generated-scene UI rebuild | `SkullbonezSource/Runtime/Scene/SceneRuntimeGeneratedControls.h:50-117`; implementations at `SceneRuntimeGeneratedControls.cpp:95`, `:152`, `:215`, `:232`, and `:263` | A successful GPU drain and tool/simulation reset must precede topology repopulation; a failed drain must prevent mutation; returned replay/profile follow-up flags must be honored. | None: `SceneGeneratedControlPolicy`, `SceneGeneratedControlPresentation`, and `SceneGeneratedControlResetParticipants` carry parallel pieces while five free functions re-encode the sequence. | No prior retain ruling found. | `repair-GV3` |
| `CameraCollection::ResetRelativity` ordering comment | `SkullbonezSource/Runtime/Camera/CameraCollection.h:117`; implementation `CameraCollection.cpp:440-443`; owner-internal calls at `:218`, `:311`, `:319`; replay call at `Runtime/App/ReplayScrubberTools.cpp:224` | A deliberate relative-camera rebase copies the selected primary pose after a caller chooses that policy. | `CameraCollection` owns both the primary slot and relative snapshot. Combining this checkpoint with every pose update would change the existing policy because ordinary pose writes intentionally do not always rebase. | No contrary evidence; cohesive owner method. | `retain-prior` |
| Render graph/pass input suffix families | Ten private `*GraphInputs` at `SkullbonezSource/Runtime/Render/RuntimeRenderer.h:151-234`; seven `*PassInputs` at `RuntimeRenderPasses.h:308-503` | Each record feeds a different concrete graph-node or pass method; they do not converge on one wide apply transaction or share one phase cursor. | Individual RuntimeRenderer graph builders and concrete pass owners. | `UiTextPassInputs` is separately assigned to the concrete-bag plan; no new evidence challenges the other pass-specific shapes. | `wide-only` / `assign-PB0` for `UiTextPassInputs` |
| Prose-only “must observe” hits | `SkullbonezSource/Runtime/Interaction/OperatorCommandApplier.cpp:589`; `Runtime/DevelopmentTools/ImGuiEditorInputPolicy.h:19` | These comments describe shared-value visibility/input policy, not a required call transition among sibling aggregates. | Existing command applier and ImGui input policy owners. | None needed. | `out-of-scope` |

### Repair owner sketches

GV2 introduces concrete `SceneLoadPhaseCursor` and
`SceneLoadTransaction` owners in `Runtime/Scene`. The transaction stores
only request/output values and the phase cursor; owners enter phase methods as
synchronous borrows. Its methods own Load → RuntimeReactions → Presentation
and the two mid-batch which-value-wins queries. The six current call paths stop
hand-sequencing the free helpers.

GV3 introduces one concrete, stack-scoped generated-control transaction in
`Runtime/Scene`. Its value cursor enforces DrainAndReset → Repopulate →
PublishFollowUps. It stores policy/request/result values, never owner pointers;
simulation, tools, frame, presentation, and scene owners are borrowed only by
the phase that needs them. It replaces `ApplyUIModelCountOverride`,
`ApplyUISolverObjectCounts`, and the three
`ApplySceneGenerated*UICommand` wrappers with one tested owner-controlled
walk.

## Explicit prior non-offenders

| Shape | Current evidence | Prior evidence and ruling | GV1 result |
|---|---|---|---|
| Run frame phase results | `SkullbonezSource/Runtime/App/RunFrame.cpp:185-209` | `Agentic/Reports/2026-07-22/run-execute-frame-phase-decomposition-closure.md` records stack-only views/results and exact phase order. Values are returned by one phase and consumed by the next; they carry no owners. | `retain-prior`; no new evidence |
| `ReplayWorkspaceFrameInput` | `SkullbonezSource/Runtime/Replay/ReplayCoordination.h:82-105`; construction `Runtime/App/InputFrame.cpp:595`; consumer `Runtime/App/ReplayScrubberTools.cpp:586` | `Agentic/Reports/2026-07-23/wide-signature-w1-rulings.md:98` and `wide-signature-parameter-bag-remediation-closure.md:57-90`: top-level value-only input-turn message; mutable owners remain explicit and synchronous. | `retain-prior`; no new evidence |
| `SceneDefaultsSaveView` | `SkullbonezSource/Runtime/Scene/SceneController.h:137-145`; one construction at `SceneRequestExecution.cpp:120`; one synchronous consumer at `SceneController.Load.cpp:1515` | The live plan pre-rules the synchronous borrow; the header states that no pointer survives scene reload. This is distinct from the companion plan’s `SceneSaveView`. | `retain-prior`; no new evidence |
| `RuntimeRenderer::FrameEntryContext` | `SkullbonezSource/Runtime/Render/RuntimeRenderer.h:78-87`; construction `Runtime/App/RunRender.cpp:192`; entry `RuntimeRenderer.cpp:2307` | `Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md:59` and `2026-07-22/render-interface-retirement-closure.md:16-66`: synchronous render-call contract, narrowed to frame values and not stored. | `retain-prior`; no new evidence |

## Companion-plan assignment

All 22 owner-registered targets remain repair-required. GV1 does not retain or
reclassify any of them.

| # | Target | Implementation-tip evidence | Assignment |
|---:|---|---|---|
| 1 | `SceneSaveRequest` | `SkullbonezSource/Scene/SceneSnapshotWriter.h:66` | PB1 |
| 2 | `SceneSaveView` | `SkullbonezSource/Scene/SceneSnapshotWriter.h:51` | PB1 |
| 3 | `SceneLoadPolicyInputs` | `SkullbonezSource/Runtime/Scene/SceneController.h:152` | PB1 consuming GV2 |
| 4 | `SceneLoadConsumerOutputs` | `SkullbonezSource/Runtime/Scene/SceneController.h:194` | PB1 consuming GV2 |
| 5 | `RuntimePointerRouteInput` | `SkullbonezSource/Runtime/Input/InputRouter.h:160` | PB2 |
| 6 | `EditorPointerRouteInput` | `SkullbonezSource/Runtime/Input/InputRouter.h:117` | PB2 |
| 7 | `MousePickupPointerInput` | `SkullbonezSource/Runtime/Tools/RuntimeTools.h:242` | PB2 |
| 8 | `RenderFrameContext` | `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h:212` | PB3 |
| 9 | `UiTextPassInputs` | `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h:437` | PB3 |
| 10 | `ReplayOverlayRenderContext` | `SkullbonezSource/Runtime/Planning/ReplayOverlayPackets.h:82` | PB3 |
| 11 | `ReplayCaptureInput` | `SkullbonezSource/Runtime/Replay/ReplayRecorder.h:457` | PB4 |
| 12 | `ReplayCameraFocusRequest` | `SkullbonezSource/Runtime/Replay/ReplayPresentation.h:206` | PB4 |
| 13 | `PersistentContactSolverContext` | `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h:125` | PB5 |
| 14 | `PhysicsContactSolverStageContext` | `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h:154` | PB5 |
| 15 | `ObjectNarrowphasePairStageContext` | `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.h:93` | PB5 |
| 16 | `PhysicsSleepIslandStageContext` | `SkullbonezSource/Physics/Stages/PhysicsSleepController.h:130` | PB6 |
| 17 | `PhysicsSleepWakeContext` | `SkullbonezSource/Physics/Stages/PhysicsSleepController.h:112` | PB6 |
| 18 | `PhysicsBroadphaseStageContext` | `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h:70` | PB5 |
| 19 | `ExternalForceBodyContext` | `SkullbonezSource/Physics/Stages/ExternalForceStage.h:83` | PB6 |
| 20 | `TerrainCandidateCommitContext` | `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.h:88` | PB6 |
| 21 | `ReplayRestoreOwnerContext` | `SkullbonezSource/Runtime/App/ReplayValidation.cpp:1357` | PB4 |
| 22 | `ReplayRestoreStepContext` | `SkullbonezSource/Runtime/App/ReplayValidation.cpp:1044` | PB4 |

PB0 must add or explicitly absorb these current 13-parameter operations:

| Operation | Evidence | Reason |
|---|---|---|
| `UiDrawSubmission::SubmitWithPreviews` | `SkullbonezSource/Runtime/Render/UiDrawSubmission.cpp:260` | 13 parameters; concrete UI submission operation above the standing ceiling |
| `UiDrawSubmission::SubmitCommands` | `SkullbonezSource/Runtime/Render/UiDrawSubmission.cpp:289` | 13 parameters; internal UI submission operation above the standing ceiling |
| `OperatorEditorFrameComposer::Render` | `SkullbonezSource/Runtime/UI/OperatorEditorFrameComposer.cpp:218` | 13 parameters; composition boundary above the standing ceiling and adjacent to PB3 render/UI scope |

## Mechanical sweep coverage

The required suffix sweep produced exactly 22 definition hits: four
`SceneLoad*` rows, one `SceneGeneratedControlResetParticipants`, ten
private RuntimeRenderer `*GraphInputs`, and seven RuntimeRenderPasses
`*PassInputs`. Every group is ruled above.

The arbitration/ordering sweeps produced the two scene arbitration helpers,
the scene reaction/presentation ordering comments, the CameraCollection
checkpoint comment, and two prose-only “must observe” comments. Every hit is
ruled above.

The implementation-definition wide sweep produced exactly 201 operations.
Counts: 2 `repair-GV2`, 3 `assign-PB0`, and
196 `wide-only`. The complete list follows so no hit is hidden by
a subsystem summary.

| # | Evidence | Symbol | Arity | Disposition |
|---:|---|---|---:|---|
| 1 | `SkullbonezSource/Gameplay/TornadoVisualPass.cpp:92` | `EmitFxVertex` | 10 | `wide-only` |
| 2 | `SkullbonezSource/Maths/RotationMatrix.cpp:39` | `RotationMatrix::RotationMatrix` | 9 | `wide-only` |
| 3 | `SkullbonezSource/Physics/ObjectContactManifold.cpp:577` | `AcceptSatAxis` | 9 | `wide-only` |
| 4 | `SkullbonezSource/Physics/ObjectContactManifold.cpp:841` | `BuildBoxFaceContact` | 9 | `wide-only` |
| 5 | `SkullbonezSource/Physics/ObjectContactManifold.cpp:1284` | `AcceptPolyAxis` | 8 | `wide-only` |
| 6 | `SkullbonezSource/Physics/ObjectContactManifold.cpp:1602` | `BuildPolyFaceContact` | 9 | `wide-only` |
| 7 | `SkullbonezSource/Physics/ObjectContactManifold.cpp:1706` | `BuildBestPolyFaceContact` | 8 | `wide-only` |
| 8 | `SkullbonezSource/Physics/ObjectContactManifold.cpp:2015` | `SkullbonezCore::Physics::BuildObjectContactManifold` | 9 | `wide-only` |
| 9 | `SkullbonezSource/Physics/ObjectContactManifold.h:118` | `BuildObjectContactManifold` | 8 | `wide-only` |
| 10 | `SkullbonezSource/Physics/PhysicsApi.h:112` | `MakePhysicsBodyCreateDesc` | 11 | `wide-only` |
| 11 | `SkullbonezSource/Physics/PhysicsBodyStore.cpp:145` | `FindClosestBoxTerrainVertex` | 8 | `wide-only` |
| 12 | `SkullbonezSource/Physics/PhysicsBodyStore.cpp:195` | `FindClosestHullTerrainVertex` | 8 | `wide-only` |
| 13 | `SkullbonezSource/Physics/PhysicsBodyStore.cpp:643` | `CalculateBuoyancyRightingTorque` | 8 | `wide-only` |
| 14 | `SkullbonezSource/Physics/PhysicsBodyStore.cpp:843` | `ApplyWorldForces` | 8 | `wide-only` |
| 15 | `SkullbonezSource/Physics/PhysicsWorld.cpp:550` | `PhysicsWorld::RunPhysics` | 8 | `wide-only` |
| 16 | `SkullbonezSource/Physics/PhysicsWorld.cpp:767` | `PhysicsWorld::RunSolverPhysics` | 9 | `wide-only` |
| 17 | `SkullbonezSource/Physics/Ragdoll.cpp:174` | `ApplyConstraintImpulse` | 9 | `wide-only` |
| 18 | `SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp:82` | `ApplyForcesForSolverBody` | 11 | `wide-only` |
| 19 | `SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp:118` | `IntegrateRemainingSolverBody` | 9 | `wide-only` |
| 20 | `SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp:195` | `PhysicsForceStage::PrepareMutualGravityForces` | 8 | `wide-only` |
| 21 | `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.cpp:155` | `RefineObjectSweepContactTime` | 8 | `wide-only` |
| 22 | `SkullbonezSource/Physics/Stages/PhysicsSleepController.Wake.cpp:434` | `PhysicsNarrowphaseWakeAccess::PhysicsNarrowphaseWakeAccess` | 11 | `wide-only` |
| 23 | `SkullbonezSource/Physics/Stages/PhysicsSleepController.Wake.cpp:497` | `PhysicsSleepController::CreateNarrowphaseWakeAccess` | 9 | `wide-only` |
| 24 | `SkullbonezSource/Physics/TerrainSupportClassifier.h:326` | `ClassifyBoxTerrainSupportImpl` | 9 | `wide-only` |
| 25 | `SkullbonezSource/Physics/TerrainSupportClassifier.h:417` | `ClassifyBoxTerrainSupport` | 9 | `wide-only` |
| 26 | `SkullbonezSource/Rendering/DX12/Dx12Diagnostics.cpp:46` | `Dx12Diagnostics::BindSources` | 8 | `wide-only` |
| 27 | `SkullbonezSource/Rendering/DX12/MeshDX12.cpp:61` | `MeshDX12::Create` | 9 | `wide-only` |
| 28 | `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp:300` | `Dx12GeometryOwner::UploadAndDrawDynamicVB` | 8 | `wide-only` |
| 29 | `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp:385` | `Dx12GeometryOwner::DrawLinesColored` | 9 | `wide-only` |
| 30 | `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp:419` | `Dx12GeometryOwner::DrawLinesColoredFromBuffer` | 8 | `wide-only` |
| 31 | `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp:497` | `Dx12GeometryOwner::DrawTransientColoredTriangles` | 11 | `wide-only` |
| 32 | `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp:530` | `Dx12GeometryOwner::DrawColoredTrianglesFromBuffer` | 12 | `wide-only` |
| 33 | `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp:657` | `Dx12GeometryOwner::CreateInstancedMesh` | 12 | `wide-only` |
| 34 | `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp:345` | `Dx12PipelineOwner::FindOrCreatePSO` | 9 | `wide-only` |
| 35 | `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp:584` | `Dx12PipelineOwner::PrepareDraw` | 9 | `wide-only` |
| 36 | `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Textures.cpp:582` | `Dx12TextureOwner::CreateTexture2D` | 8 | `wide-only` |
| 37 | `SkullbonezSource/Rendering/PrimitiveBatchRenderer.cpp:585` | `PrimitiveBatchRenderer::BeginSphereBatch` | 8 | `wide-only` |
| 38 | `SkullbonezSource/Rendering/PrimitiveBatchRenderer.cpp:602` | `PrimitiveBatchRenderer::BeginBoxBatch` | 8 | `wide-only` |
| 39 | `SkullbonezSource/Rendering/PrimitiveBatchRenderer.cpp:619` | `PrimitiveBatchRenderer::BeginPineBatch` | 8 | `wide-only` |
| 40 | `SkullbonezSource/Rendering/PrimitiveBatchRenderer.cpp:898` | `PrimitiveBatchRenderer::DrawSphereBatchBegin` | 8 | `wide-only` |
| 41 | `SkullbonezSource/Rendering/PrimitiveBatchRenderer.cpp:1104` | `PrimitiveBatchRenderer::DrawBoxBatchBegin` | 8 | `wide-only` |
| 42 | `SkullbonezSource/Rendering/PrimitiveBatchRenderer.cpp:1222` | `PrimitiveBatchRenderer::DrawConvexHullModel` | 11 | `wide-only` |
| 43 | `SkullbonezSource/Rendering/PrimitiveBatchRenderer.cpp:1337` | `PrimitiveBatchRenderer::DrawPineBatchBegin` | 8 | `wide-only` |
| 44 | `SkullbonezSource/Rendering/RenderInstanceRenderer.cpp:334` | `RenderModelsForView` | 12 | `wide-only` |
| 45 | `SkullbonezSource/Rendering/RenderInstanceRenderer.cpp:592` | `RenderInstanceRenderer::RenderModels` | 12 | `wide-only` |
| 46 | `SkullbonezSource/Rendering/RenderInstanceRenderer.cpp:620` | `RenderInstanceRenderer::RenderReflectionModels` | 10 | `wide-only` |
| 47 | `SkullbonezSource/Rendering/RenderInstanceRenderer.cpp:816` | `RenderInstanceRenderer::RenderShadowCasters` | 10 | `wide-only` |
| 48 | `SkullbonezSource/Rendering/RenderInstanceRenderer.cpp:833` | `RenderInstanceRenderer::GetObjectShadowBounds` | 9 | `wide-only` |
| 49 | `SkullbonezSource/Rendering/Text.cpp:536` | `Text2d::BuildFont` | 8 | `wide-only` |
| 50 | `SkullbonezSource/Rendering/Text.cpp:730` | `Text2d::RenderTextInternal` | 8 | `wide-only` |
| 51 | `SkullbonezSource/Rendering/Text.cpp:883` | `Text2d::Render2dTextColor` | 9 | `wide-only` |
| 52 | `SkullbonezSource/Rendering/Text.cpp:908` | `Text2d::Render2dQuad` | 10 | `wide-only` |
| 53 | `SkullbonezSource/Rendering/Text.cpp:959` | `Text2d::BatchQuad` | 10 | `wide-only` |
| 54 | `SkullbonezSource/Rendering/Text.cpp:1025` | `Text2d::BatchTriangle` | 12 | `wide-only` |
| 55 | `SkullbonezSource/Runtime/App/InputFrame.cpp:634` | `ApplyRuntimeUIFrameCommands` | 8 | `wide-only` |
| 56 | `SkullbonezSource/Runtime/App/InputFrameExecution.cpp:103` | `SkullbonezCore::Runtime::ProcessInputFrame` | 8 | `wide-only` |
| 57 | `SkullbonezSource/Runtime/App/ReplayRuntime.cpp:740` | `ReplayRuntime::PrepareRenderOverlay` | 8 | `wide-only` |
| 58 | `SkullbonezSource/Runtime/App/ReplayRuntime.cpp:851` | `ReplayRuntime::BuildVisualProjectionForValidation` | 8 | `wide-only` |
| 59 | `SkullbonezSource/Runtime/App/ReplayRuntime.cpp:1022` | `ReplayRuntime::RouteWorldPointer` | 10 | `wide-only` |
| 60 | `SkullbonezSource/Runtime/App/ReplayRuntime.cpp:1113` | `ReplayRuntime::ApplyInputFocusLoss` | 8 | `wide-only` |
| 61 | `SkullbonezSource/Runtime/App/ReplayRuntime.cpp:1798` | `ReplayRuntime::UpdatePrediction` | 9 | `wide-only` |
| 62 | `SkullbonezSource/Runtime/App/ReplayScrubberTools.cpp:346` | `SkullbonezCore::Runtime::ReplayPresentationOperations::ExitInspectionCamera` | 10 | `wide-only` |
| 63 | `SkullbonezSource/Runtime/App/ReplayScrubberTools.cpp:506` | `ReplayRuntime::ExitInspectionCamera` | 8 | `wide-only` |
| 64 | `SkullbonezSource/Runtime/App/ReplayScrubberTools.cpp:586` | `ReplayRuntime::TickWorkspace` | 11 | `wide-only` |
| 65 | `SkullbonezSource/Runtime/App/ReplayScrubberTools.cpp:940` | `ApplyReplayLiveAdvanceAction` | 11 | `wide-only` |
| 66 | `SkullbonezSource/Runtime/App/ReplayScrubberTools.cpp:1019` | `HandleReplayPausePressed` | 11 | `wide-only` |
| 67 | `SkullbonezSource/Runtime/App/ReplayScrubberTools.cpp:1047` | `HandleReplayVelocityEditPressed` | 11 | `wide-only` |
| 68 | `SkullbonezSource/Runtime/App/ReplayScrubberTools.cpp:1119` | `SetReplayPredictionHorizonFromPointer` | 8 | `wide-only` |
| 69 | `SkullbonezSource/Runtime/App/ReplayScrubberTools.cpp:1275` | `HandleReplayPredictionHorizonPressed` | 9 | `wide-only` |
| 70 | `SkullbonezSource/Runtime/App/ReplayScrubberTools.cpp:1332` | `TickReplayScrubDrag` | 9 | `wide-only` |
| 71 | `SkullbonezSource/Runtime/App/ReplayScrubberTools.cpp:1376` | `TickReplayPredictionHorizonDrag` | 9 | `wide-only` |
| 72 | `SkullbonezSource/Runtime/App/ReplayScrubberTools.cpp:1543` | `ReplayRuntime::ApplyTransportCommand` | 9 | `wide-only` |
| 73 | `SkullbonezSource/Runtime/App/ReplayScrubberTools.cpp:1786` | `ReplayRuntime::TickScrubberInput` | 12 | `wide-only` |
| 74 | `SkullbonezSource/Runtime/App/ReplayValidation.Probes.cpp:1129` | `ReplayProbeRunner::VerifyLoadedPresentation` | 10 | `wide-only` |
| 75 | `SkullbonezSource/Runtime/App/ReplayValidation.Probes.cpp:1687` | `ReplayProbeRunner::PrepareBranchFileProbe` | 11 | `wide-only` |
| 76 | `SkullbonezSource/Runtime/App/ReplayValidation.cpp:867` | `PrepareReplayRestoreArtifactSelection` | 8 | `wide-only` |
| 77 | `SkullbonezSource/Runtime/App/ReplayValidation.cpp:960` | `FormatReplayRestoreDivergenceMessage` | 10 | `wide-only` |
| 78 | `SkullbonezSource/Runtime/App/ReplayValidation.cpp:1522` | `RunReplayRestoreTargetStep` | 9 | `wide-only` |
| 79 | `SkullbonezSource/Runtime/App/ReplayValidation.cpp:1690` | `ReplayRuntime::RestoreV2ArtifactTargetState` | 8 | `wide-only` |
| 80 | `SkullbonezSource/Runtime/App/ReplayValidation.cpp:1711` | `ReplayRuntime::RestoreV2ArtifactTargetStateImpl` | 9 | `wide-only` |
| 81 | `SkullbonezSource/Runtime/Camera/AttachedCameraController.cpp:863` | `AttachedCameraController::BuildFollowPose` | 9 | `wide-only` |
| 82 | `SkullbonezSource/Runtime/Camera/CameraControlState.cpp:41` | `CameraControlState::UpdateViewingOrientation` | 8 | `wide-only` |
| 83 | `SkullbonezSource/Runtime/Camera/CameraControlState.cpp:113` | `CameraControlState::TickControls` | 8 | `wide-only` |
| 84 | `SkullbonezSource/Runtime/Capture/CaptureController.cpp:83` | `CaptureController::TickAutoCycle` | 8 | `wide-only` |
| 85 | `SkullbonezSource/Runtime/Capture/CaptureSystem.cpp:346` | `CaptureSystem::TickAutoCycle` | 9 | `wide-only` |
| 86 | `SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp:1431` | `RuntimeTools::PrepareEditorGizmoGesture` | 11 | `wide-only` |
| 87 | `SkullbonezSource/Runtime/Editor/EditorObjectPlacement.cpp:103` | `MakeEditorBodyDesc` | 8 | `wide-only` |
| 88 | `SkullbonezSource/Runtime/Editor/EditorTracer.cpp:489` | `EditorTracer::EmitBoxTo` | 8 | `wide-only` |
| 89 | `SkullbonezSource/Runtime/Editor/EditorTracer.cpp:604` | `EditorTracer::EmitReplayRibbonSegmentTo` | 8 | `wide-only` |
| 90 | `SkullbonezSource/Runtime/Editor/EditorTracer.cpp:657` | `EditorTracer::EmitReplayRibbonGlowPairTo` | 9 | `wide-only` |
| 91 | `SkullbonezSource/Runtime/Editor/EditorTracer.cpp:679` | `EditorTracer::EmitReplayRibbonShapeOutlineTo` | 9 | `wide-only` |
| 92 | `SkullbonezSource/Runtime/Editor/EditorTracer.cpp:1403` | `EditorTracer::AddGizmo` | 8 | `wide-only` |
| 93 | `SkullbonezSource/Runtime/Editor/EditorTracer.cpp:1483` | `EditorTracer::AddReplayVelocityGizmo` | 10 | `wide-only` |
| 94 | `SkullbonezSource/Runtime/Editor/LauncherLaser.cpp:250` | `LauncherLaser::EmitQuad` | 8 | `wide-only` |
| 95 | `SkullbonezSource/Runtime/Editor/LauncherLaser.cpp:267` | `LauncherLaser::EmitRibbon` | 8 | `wide-only` |
| 96 | `SkullbonezSource/Runtime/Editor/LauncherLaser.cpp:280` | `LauncherLaser::EmitBillboardQuad` | 9 | `wide-only` |
| 97 | `SkullbonezSource/Runtime/Planning/ReplayPlanningRuntime.cpp:353` | `ReplayPlanningRuntime::FinishFrameAfterPrediction` | 8 | `wide-only` |
| 98 | `SkullbonezSource/Runtime/Prediction/ReplayAuthoringCauseTree.cpp:110` | `ResolveReplayCauseTreeBodyPosition` | 10 | `wide-only` |
| 99 | `SkullbonezSource/Runtime/Prediction/ReplayAuthoringCauseTree.cpp:837` | `ReplayPrediction::ActivateCauseTreeRow` | 10 | `wide-only` |
| 100 | `SkullbonezSource/Runtime/Prediction/ReplayCauseFocusSubmission.cpp:196` | `ReplayPredictionPresentation::RenderCauseFocusOverlay` | 8 | `wide-only` |
| 101 | `SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp:704` | `ReplayPrediction::BeginFrameSource` | 12 | `wide-only` |
| 102 | `SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp:1010` | `StepReplayPredictionJob` | 10 | `wide-only` |
| 103 | `SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp:1270` | `ReplayPrediction::AdvanceFrameWorker` | 8 | `wide-only` |
| 104 | `SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp:165` | `AddOrAccountReplayPathSegment` | 9 | `wide-only` |
| 105 | `SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp:403` | `ResolveReplayPathColor` | 9 | `wide-only` |
| 106 | `SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp:654` | `DrawReplayTrajectoryRecordSegments` | 11 | `wide-only` |
| 107 | `SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp:880` | `DrawReplayPredictionRootTrajectoryFromStore` | 10 | `wide-only` |
| 108 | `SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp:935` | `DrawReplayPredictionSmallSceneBodyTrajectories` | 9 | `wide-only` |
| 109 | `SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp:1038` | `DrawReplayPredictionChildTrajectoryRecord` | 11 | `wide-only` |
| 110 | `SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp:1159` | `DrawReplayPredictionChildTrajectoriesFromStore` | 8 | `wide-only` |
| 111 | `SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp:1359` | `DrawReplayPredictionAffectedBodyTrails` | 10 | `wide-only` |
| 112 | `SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp:1756` | `ReplayPredictionRetainedGeometry::EmitRecord` | 8 | `wide-only` |
| 113 | `SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp:1815` | `ReplayPredictionRetainedGeometry::AddPathSegment` | 8 | `wide-only` |
| 114 | `SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.cpp:242` | `BeginReplayTrajectoryRecord` | 8 | `wide-only` |
| 115 | `SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.cpp:496` | `BuildReplayPredictionChildTrajectoryRecord` | 8 | `wide-only` |
| 116 | `SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.cpp:581` | `AppendReplayPredictionChildTrajectoryFrames` | 8 | `wide-only` |
| 117 | `SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.cpp:988` | `BuildReplayPredictionAffectedBodyTrails` | 8 | `wide-only` |
| 118 | `SkullbonezSource/Runtime/Prediction/ReplayPredictionTopologyPublication.cpp:163` | `AssignReplayFutureNode` | 10 | `wide-only` |
| 119 | `SkullbonezSource/Runtime/Prediction/ReplayPredictionTopologyPublication.cpp:236` | `BuildReplayFutureNodesFromContacts` | 10 | `wide-only` |
| 120 | `SkullbonezSource/Runtime/Prediction/ReplayPredictionTopologyPublication.cpp:702` | `RetainReplayPredictionAffectedBodyMarkers` | 10 | `wide-only` |
| 121 | `SkullbonezSource/Runtime/Prediction/ReplayPredictionTopologyPublication.cpp:787` | `AddReplayPredictionFutureNode` | 10 | `wide-only` |
| 122 | `SkullbonezSource/Runtime/Prediction/ReplayPredictionTopologyPublication.cpp:994` | `UpdateReplayPredictionFutureNodeCache` | 8 | `wide-only` |
| 123 | `SkullbonezSource/Runtime/Prediction/ReplayPredictionTopologyPublication.cpp:1163` | `PrepareReplayPredictionOverlay` | 8 | `wide-only` |
| 124 | `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp:922` | `ShadowPass::RenderShadowMap` | 12 | `wide-only` |
| 125 | `SkullbonezSource/Runtime/Render/UiDrawSubmission.cpp:78` | `ImmediateUiSubmitter::Rect` | 8 | `wide-only` |
| 126 | `SkullbonezSource/Runtime/Render/UiDrawSubmission.cpp:106` | `ImmediateUiSubmitter::Triangle` | 10 | `wide-only` |
| 127 | `SkullbonezSource/Runtime/Render/UiDrawSubmission.cpp:122` | `ImmediateUiSubmitter::RoundedRect` | 9 | `wide-only` |
| 128 | `SkullbonezSource/Runtime/Render/UiDrawSubmission.cpp:188` | `ImmediateUiSubmitter::RoundedRectFill` | 9 | `wide-only` |
| 129 | `SkullbonezSource/Runtime/Render/UiDrawSubmission.cpp:234` | `UiDrawSubmission::Submit` | 10 | `wide-only` |
| 130 | `SkullbonezSource/Runtime/Render/UiDrawSubmission.cpp:260` | `UiDrawSubmission::SubmitWithPreviews` | 13 | `assign-PB0` |
| 131 | `SkullbonezSource/Runtime/Render/UiDrawSubmission.cpp:289` | `UiDrawSubmission::SubmitCommands` | 13 | `assign-PB0` |
| 132 | `SkullbonezSource/Runtime/Render/UiTextPass.cpp:147` | `DrawUiTestPattern` | 8 | `wide-only` |
| 133 | `SkullbonezSource/Runtime/Replay/ReplayAuthoringCauseTreeInput.cpp:117` | `ReplayAuthoring::TickCauseTreeInput` | 12 | `wide-only` |
| 134 | `SkullbonezSource/Runtime/Replay/ReplayAuthoringVelocity.cpp:533` | `ReplayAuthoring::TickVelocityEditInput` | 12 | `wide-only` |
| 135 | `SkullbonezSource/Runtime/Replay/ReplayAuthoringVelocity.cpp:857` | `ReplayAuthoring::TryPickVelocityEditTarget` | 11 | `wide-only` |
| 136 | `SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp:371` | `DescribeReplayScrubberAvailability` | 8 | `wide-only` |
| 137 | `SkullbonezSource/Runtime/Replay/ReplayPresentation.cpp:548` | `ReplayPresentation::ApplyPastTrajectoryUpdate` | 9 | `wide-only` |
| 138 | `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp:1645` | `SkullbonezCore::Runtime::ReplayEventCommandOperations::BuildCommand` | 10 | `wide-only` |
| 139 | `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp:1850` | `SkullbonezCore::Runtime::ReplayEventCommandOperations::BuildEditorTransform` | 8 | `wide-only` |
| 140 | `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp:2211` | `BuildManifest` | 10 | `wide-only` |
| 141 | `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp:132` | `MakeSceneBodyDesc` | 11 | `wide-only` |
| 142 | `SkullbonezSource/Runtime/Scene/SceneController.Load.cpp:492` | `UseFlatSlopeTerrain` | 8 | `wide-only` |
| 143 | `SkullbonezSource/Runtime/Scene/SceneController.Load.cpp:599` | `SkullbonezCore::Runtime::ApplySceneLoadRuntimeReactions` | 11 | `repair-GV2` |
| 144 | `SkullbonezSource/Runtime/Scene/SceneController.Load.cpp:736` | `SkullbonezCore::Runtime::ApplySceneLoadPresentationOutputs` | 8 | `repair-GV2` |
| 145 | `SkullbonezSource/Runtime/Scene/SceneRuntimeLoad.cpp:198` | `PrepareSceneRuntimeLoad` | 10 | `wide-only` |
| 146 | `SkullbonezSource/Runtime/UI/OperatorEditorFrameComposer.cpp:218` | `Render` | 13 | `assign-PB0` |
| 147 | `SkullbonezSource/Scene/AuthoredSceneParserAssets.cpp:543` | `AuthoredSceneParser::RecordAssetPart` | 9 | `wide-only` |
| 148 | `SkullbonezSource/UI/UI.cpp:357` | `InGameUI::UpdateInput` | 12 | `wide-only` |
| 149 | `SkullbonezSource/UI/UIComboBox.cpp:169` | `UIComboBox::Draw` | 8 | `wide-only` |
| 150 | `SkullbonezSource/UI/UIComboBox.cpp:188` | `UIComboBox::Draw` | 9 | `wide-only` |
| 151 | `SkullbonezSource/UI/UIDraw.cpp:59` | `UIDrawContext::Rect` | 8 | `wide-only` |
| 152 | `SkullbonezSource/UI/UIDraw.cpp:65` | `UIDrawContext::Triangle` | 10 | `wide-only` |
| 153 | `SkullbonezSource/UI/UIDraw.cpp:80` | `UIDrawContext::Outline` | 8 | `wide-only` |
| 154 | `SkullbonezSource/UI/UIDraw.cpp:89` | `UIDrawContext::RoundedRect` | 9 | `wide-only` |
| 155 | `SkullbonezSource/UI/UIDrawList.cpp:52` | `UIDrawList::AddRect` | 8 | `wide-only` |
| 156 | `SkullbonezSource/UI/UIDrawList.cpp:72` | `UIDrawList::AddRoundedRect` | 9 | `wide-only` |
| 157 | `SkullbonezSource/UI/UIDrawList.cpp:93` | `UIDrawList::AddTriangle` | 10 | `wide-only` |
| 158 | `SkullbonezSource/UI/UIDrawList.cpp:197` | `UIDrawList::AddPreviewImage` | 10 | `wide-only` |
| 159 | `SkullbonezSource/UI/UIDrawWidgets.cpp:140` | `DrawLabelValueAt` | 10 | `wide-only` |
| 160 | `SkullbonezSource/UI/UIDrawWidgets.cpp:180` | `DrawContentToggle` | 9 | `wide-only` |
| 161 | `SkullbonezSource/UI/UIDrawWidgets.cpp:201` | `DrawFooterStatCell` | 8 | `wide-only` |
| 162 | `SkullbonezSource/UI/UIDrawWidgets.cpp:216` | `DrawCompactFooterStat` | 8 | `wide-only` |
| 163 | `SkullbonezSource/UI/UIEditorMiniPaletteDraw.cpp:102` | `DrawEditorMiniTreeSilhouette` | 8 | `wide-only` |
| 164 | `SkullbonezSource/UI/UIEditorMiniPaletteDraw.cpp:733` | `DrawEditorMiniPaletteButton` | 8 | `wide-only` |
| 165 | `SkullbonezSource/UI/UIEditorMiniPaletteDraw.cpp:885` | `DrawEditorMiniPalette` | 11 | `wide-only` |
| 166 | `SkullbonezSource/UI/UIFrameComposition.cpp:445` | `BuildUIInteractionSignature` | 11 | `wide-only` |
| 167 | `SkullbonezSource/UI/UIProfilerOverlayPresenter.cpp:61` | `ProjectedDraw::Quad` | 8 | `wide-only` |
| 168 | `SkullbonezSource/UI/UIProfilerOverlayPresenter.cpp:77` | `ProjectedDraw::Text` | 8 | `wide-only` |
| 169 | `SkullbonezSource/UI/UIProfilerOverlayPresenter.cpp:99` | `UIProfilerOverlayPresenter::RecordOverlay` | 8 | `wide-only` |
| 170 | `SkullbonezSource/UI/UITabCinematic.cpp:530` | `HandleContentClick` | 8 | `wide-only` |
| 171 | `SkullbonezSource/UI/UITabCinematic.cpp:623` | `Draw` | 10 | `wide-only` |
| 172 | `SkullbonezSource/UI/UITabControls.cpp:75` | `HandleContentClick` | 11 | `wide-only` |
| 173 | `SkullbonezSource/UI/UITabControls.cpp:268` | `Draw` | 8 | `wide-only` |
| 174 | `SkullbonezSource/UI/UITabEditor.cpp:188` | `Draw` | 10 | `wide-only` |
| 175 | `SkullbonezSource/UI/UITabMemory.cpp:258` | `DrawMemoryOverlayLineSegment` | 10 | `wide-only` |
| 176 | `SkullbonezSource/UI/UITabMemory.cpp:541` | `DrawMemoryStackSegment` | 11 | `wide-only` |
| 177 | `SkullbonezSource/UI/UITabMemory.cpp:629` | `DrawMemoryRow` | 9 | `wide-only` |
| 178 | `SkullbonezSource/UI/UITabMemory.cpp:714` | `DrawReplayMemoryPolicyPanel` | 11 | `wide-only` |
| 179 | `SkullbonezSource/UI/UITabMemory.cpp:1523` | `Draw` | 11 | `wide-only` |
| 180 | `SkullbonezSource/UI/UITabMemory.cpp:1552` | `HandleContentClick` | 8 | `wide-only` |
| 181 | `SkullbonezSource/UI/UITabOptions.cpp:41` | `SetToggleBounds` | 8 | `wide-only` |
| 182 | `SkullbonezSource/UI/UITabOptions.cpp:94` | `HandleContentClick` | 9 | `wide-only` |
| 183 | `SkullbonezSource/UI/UITabOptions.cpp:209` | `Draw` | 9 | `wide-only` |
| 184 | `SkullbonezSource/UI/UITabPhysics.cpp:62` | `SetToggleBounds` | 8 | `wide-only` |
| 185 | `SkullbonezSource/UI/UITabPhysics.cpp:166` | `HandleContentClick` | 8 | `wide-only` |
| 186 | `SkullbonezSource/UI/UITabPhysics.cpp:648` | `Draw` | 11 | `wide-only` |
| 187 | `SkullbonezSource/UI/UITabProfiler.cpp:574` | `HandleContentClick` | 11 | `wide-only` |
| 188 | `SkullbonezSource/UI/UITabProfiler.cpp:732` | `Draw` | 9 | `wide-only` |
| 189 | `SkullbonezSource/UI/UITabProfilerHistogram.cpp:462` | `DrawHistogramLineSegment` | 10 | `wide-only` |
| 190 | `SkullbonezSource/UI/UITabProfilerHistogram.cpp:610` | `DrawHistogramCheckbox` | 8 | `wide-only` |
| 191 | `SkullbonezSource/UI/UITabProfilerHistogram.cpp:688` | `HandlePerformanceHistogramInput` | 10 | `wide-only` |
| 192 | `SkullbonezSource/UI/UITabScene.cpp:492` | `HandleComboWheel` | 9 | `wide-only` |
| 193 | `SkullbonezSource/UI/UITabScene.cpp:525` | `HandleOpenComboClick` | 9 | `wide-only` |
| 194 | `SkullbonezSource/UI/UITabScene.cpp:679` | `HandleTimeScaleClick` | 8 | `wide-only` |
| 195 | `SkullbonezSource/UI/UITabScene.cpp:734` | `Draw` | 10 | `wide-only` |
| 196 | `SkullbonezSource/UI/UITabSky.cpp:283` | `HandleContentClick` | 8 | `wide-only` |
| 197 | `SkullbonezSource/UI/UITabSky.cpp:368` | `Draw` | 10 | `wide-only` |
| 198 | `SkullbonezSource/UI/UIWindowInteractionOwner.cpp:538` | `UIWindowInteractionOwner::UpdateInput` | 12 | `wide-only` |
| 199 | `SkullbonezSource/World/Terrain.cpp:155` | `Terrain::TryCreateFromHeightMap` | 8 | `wide-only` |
| 200 | `SkullbonezSource/World/Terrain.cpp:582` | `Terrain::Render` | 9 | `wide-only` |
| 201 | `SkullbonezSource/World/WorldEnvironment.cpp:262` | `WorldEnvironment::RenderFluid` | 10 | `wide-only` |

## Acceptance

- Complete current-tip disposition table: PASS.
- Every sweep hit has a ruling and file:line evidence: PASS.
- Every repair-now row has a named invariant and owner sketch: PASS.
- Prior rulings are honored; none is challenged without evidence: PASS.
- Source behavior changes: none.
- Repository validation: not required for documentation-only GV1.
