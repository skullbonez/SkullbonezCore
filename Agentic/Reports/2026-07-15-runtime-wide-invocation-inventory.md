# Runtime Wide-Invocation Inventory

Date: 2026-07-15
Owner: runtime shell
Source plan: `Agentic/Plans/TODO/runtime-signature-decomposition.md`

## Method

Multiline `rg` discovery was followed by a read-only balanced-token parser over every `SkullbonezSource/Runtime/**/*.cpp`. It removes comments/literals, balances parentheses/braces/brackets, excludes definitions and constructor initializers, and records invocations with at least seven top-level arguments. Rows group the invoked name, list every observed call site, and report the exact maximum count. Migrated rows preserve pre-migration counts.

## Inventory

| Function | File call sites | Exact max args | Existing view overlap | Disposition |
|---|---|---:|---|---|
| `ApplyInteractionTransitionCleanup` | `InputRouter.h; RunInput.cpp` | 12 | Interaction + Scene | Migrated to 5; derives camera/scene facts synchronously. |
| `ApplyInteractionTransition` | `InputRouter.h; RunInput.cpp; InputFrame.cpp` | 12 | Interaction + Scene | Migrated to 5; forwards narrow views, replay, restore mode. |
| `SetWorldInteractionOwner` | `InputRouter.h; RunInput.cpp; InputFrame.cpp; InputFrameExecution.cpp; RunFrame.cpp` | 13 | Interaction + Scene | Migrated to 6; owner/reason/restore remain operation values. |
| `ApplyCameraMode` | `InputRouter.h; RunInput.cpp; InputFrame.cpp; InputFrameExecution.cpp; RunFrame.cpp` | 9 | Interaction + Scene | Migrated to 6; retains no view or owner. |
| `CycleCameraMode` | `InputRouter.h; RunInput.cpp; InputFrameExecution.cpp` | 7 | Interaction + Scene | Migrated to 4; delegates mode application. |
| `HandleUnfocusedFrame` | `InputRouter.h; RunInput.cpp; InputFrameExecution.cpp` | 8 | Interaction + Scene | Migrated to 4; derives UI/camera/tools/scene. |
| `DispatchAfterUiDismiss` | `InputRouter.h; RunInput.cpp; InputFrameExecution.cpp` | 11 | Host + Interaction + Scene | Migrated to 6; two scalars form RuntimeAfterUiDismissInput. |
| `DispatchCaptureActions` | `InputRouter.h; RunInput.cpp; InputFrameExecution.cpp` | 7 | Host + Interaction + Scene | Migrated to 5; derives diagnostics/UI/camera/scene. |
| `RecordModeAction` | `InputRouter.h; RunInput.cpp; InputFrameExecution.cpp` | 7 | Interaction | Migrated to 4; derives identical mode facts. |
| `RouteEditorPointer` | `InputRouter.h; Editor/RunEditorTools.cpp; RunInput.cpp` | 9 | Host + Interaction + Scene | Migrated to 4; borrows assets/editor/scene owners. |
| `RouteRuntimePointer` | `InputRouter.h; RunInput.cpp; InputFrameExecution.cpp` | 17 | Host + Interaction + Scene | Migrated to 6; derives stores/camera facts. |
| `ActivateLoadedPresentationScrubber` | `SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp:1510`<br>`SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:2256` | 11 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `AddGizmo` | `SkullbonezSource/Runtime/Editor/RunEditorOverlayTools.cpp:176` | 8 | Editor values | Keep: cohesive geometry/selection/body/event payload. |
| `AddOrAccountReplayBaselinePathSegment` | `SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:661` | 7 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `AddOrAccountReplayPathSegment` | `SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:1056`<br>`SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:1170`<br>`SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:1363`<br>`SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:1556`<br>`SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:768` | 9 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `AddReplayFutureNodeToNodes` | `SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:2458` | 12 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `AddReplayPathSegment` | `SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:197` | 7 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `AddReplayPredictionFutureNode` | `SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:2498`<br>`SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:2595` | 10 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `AddReplayVelocityGizmo` | `SkullbonezSource/Runtime/Replay/ReplayAuthoringVelocity.cpp:879` | 10 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `AppendReplayPredictionChildTrajectoryFrames` | `SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:1177`<br>`SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:1185` | 8 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `AppendReplayRibbonVertex` | `SkullbonezSource/Runtime/Editor/RunEditorTracer.cpp:744` | 12 | Editor values | Keep: cohesive geometry/selection/body/event payload. |
| `AppendReportAction` | `SkullbonezSource/Runtime/InteractionAutomationController.cpp:1044`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:1060`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:1076`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:1092`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:1108`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:1123`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:1208`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:1215`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:1230`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:1249`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:1293`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:1322`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:1335`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:2963`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:3047`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:3065`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:3085`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:3092`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:3144`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:3167`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:3172`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:3243`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:3250`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:3256` | 7 | None; diagnostic values | Keep: explicit bounded log/probe schema; no owner plumbing. |
| `ApplyInputFocusLoss` | `SkullbonezSource/Runtime/RunInput.cpp:659` | 8 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `ApplyInteractionAutomationReplayControlClick` | `SkullbonezSource/Runtime/InteractionAutomationController.cpp:3096` | 9 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `ApplyInteractionAutomationReplayStateAction` | `SkullbonezSource/Runtime/InteractionAutomationController.cpp:2987` | 9 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `ApplyInteractionAutomationSolverTrackScrub` | `SkullbonezSource/Runtime/InteractionAutomationController.cpp:3108` | 8 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `ApplyPastTrajectoryUpdate` | `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp:1042`<br>`SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:185` | 9 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `ApplyReplayLiveAdvanceAction` | `SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp:848`<br>`SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp:890` | 11 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `ApplyReplayRestoreEditorPlaceEvent` | `SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:1355` | 12 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `ApplyRuntimeUIFrameCommands` | `SkullbonezSource/Runtime/InputFrameExecution.cpp:852` | 8 | Already four narrow views | Keep: combining them recreates a complete-frame authority bag. |
| `AssignReplayFutureNode` | `SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:1744`<br>`SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:1764` | 10 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `BeginBoxBatch` | `SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp:464` | 8 | Render/pass contexts | Keep: hot pass data; frame view leaks unrelated authority. |
| `BeginReplayPredictionJob` | `SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:3752` | 19 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `BeginReplayTrajectoryRecord` | `SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:1029`<br>`SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:1565`<br>`SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:846`<br>`SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:907`<br>`SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:974` | 8 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `BindTonemapPassParams` | `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp:2341` | 7 | Render/pass contexts | Keep: hot pass data; frame view leaks unrelated authority. |
| `BuildCauseTreeRows` | `SkullbonezSource/Runtime/Replay/ReplayAuthoringCauseTree.cpp:1121`<br>`SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp:448` | 8 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `BuildCommand` | `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp:1557`<br>`SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp:1589`<br>`SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp:1612`<br>`SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp:1652`<br>`SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp:1699`<br>`SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp:1772`<br>`SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp:1014` | 10 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `BuildFollowPose` | `SkullbonezSource/Runtime/AttachedCameraController.cpp:326`<br>`SkullbonezSource/Runtime/AttachedCameraController.cpp:379` | 9 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `BuildGraphicsStressStyleContext` | `SkullbonezSource/Runtime/RuntimeStressController.cpp:399` | 7 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `BuildManifest` | `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp:2328` | 10 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `BuildObjectContactManifold` | `SkullbonezSource/Runtime/Scene/SceneRuntime.cpp:430` | 8 | Scene values/contexts | Keep: cohesive setup/reset/body data; no repeated frame owners. |
| `BuildReplayFutureNodesFromContacts` | `SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:2479` | 10 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `BuildReplayPredictionAffectedBodyTrails` | `SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:2374`<br>`SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:1501` | 8 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `BuildReplayPredictionChildTrajectoryRecord` | `SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:1098`<br>`SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:1203`<br>`SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:1211` | 8 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `BuildReplayProbeVisualProjection` | `SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:2928` | 13 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `BuildRuntimeRenderInputs` | `SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp:2329` | 20 | Render/pass contexts | Keep: hot pass data; frame view leaks unrelated authority. |
| `BuildSceneAuthoredModelContext` | `SkullbonezSource/Runtime/Scene/RunScene.cpp:987` | 8 | Scene values/contexts | Keep: cohesive setup/reset/body data; no repeated frame owners. |
| `BuildSceneGeneratedModelContext` | `SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:2011`<br>`SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:2029`<br>`SkullbonezSource/Runtime/Scene/RunScene.cpp:747`<br>`SkullbonezSource/Runtime/Scene/RunScene.cpp:964` | 7 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `CaptureSystem::TickAutoCycle` | `SkullbonezSource/Runtime/CaptureController.cpp:91` | 9 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `CaptureTransition` | `SkullbonezSource/Runtime/RuntimeInteractionController.cpp:261`<br>`SkullbonezSource/Runtime/RuntimeInteractionController.cpp:273`<br>`SkullbonezSource/Runtime/RuntimeInteractionController.cpp:295`<br>`SkullbonezSource/Runtime/RuntimeInteractionController.cpp:536` | 7 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `CompleteReplayPredictionJobOnFrameThread` | `SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:3482`<br>`SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:3495` | 7 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `CreateInstancedMesh` | `SkullbonezSource/Runtime/Debug/CollisionVisualizer.cpp:193`<br>`SkullbonezSource/Runtime/Debug/CollisionVisualizer.cpp:224` | 10 | Debug-render values | Keep: fixed instance construction payload. |
| `CreateWindow` | `SkullbonezSource/Runtime/Window.cpp:357` | 11 | None; external API | Keep: platform positional contract. |
| `DescribeReplayScrubberSurface` | `SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:118`<br>`SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp:1223` | 12 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `DispatchReflectionRays` | `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp:1237` | 17 | Render/pass contexts | Keep: hot pass data; frame view leaks unrelated authority. |
| `DrawReplayPredictionAffectedBodyTrails` | `SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:1653` | 10 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `DrawReplayPredictionChildTrajectoriesFromStore` | `SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:1641` | 8 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `DrawReplayPredictionChildTrajectoryRecord` | `SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:1196`<br>`SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:1207` | 11 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `DrawReplayPredictionRagdollTorsoTrails` | `SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:1603`<br>`SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:1667` | 7 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `DrawReplayPredictionRetainedMarkerTrailFromStore` | `SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:906` | 7 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `DrawReplayPredictionRetainedMarkers` | `SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:1675` | 7 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `DrawReplayPredictionRootTrajectoryFromStore` | `SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:1616` | 9 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `DrawReplayPredictionSmallSceneBodyTrajectories` | `SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:1625` | 8 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `DrawReplayTrajectoryRecordSegments` | `SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:1096`<br>`SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:1250`<br>`SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:1273`<br>`SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:955` | 11 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `EmitBillboardQuad` | `SkullbonezSource/Runtime/Editor/LauncherLaser.cpp:335` | 9 | Editor values | Keep: cohesive geometry/selection/body/event payload. |
| `EmitBox` | `SkullbonezSource/Runtime/Editor/RunEditorTracer.cpp:1245`<br>`SkullbonezSource/Runtime/Editor/RunEditorTracer.cpp:917`<br>`SkullbonezSource/Runtime/Editor/RunEditorTracer.cpp:944`<br>`SkullbonezSource/Runtime/Editor/RunEditorTracer.cpp:958` | 7 | Editor values | Keep: cohesive geometry/selection/body/event payload. |
| `EmitBoxTo` | `SkullbonezSource/Runtime/Editor/RunEditorTracer.cpp:429`<br>`SkullbonezSource/Runtime/Editor/RunEditorTracer.cpp:453` | 8 | Editor values | Keep: cohesive geometry/selection/body/event payload. |
| `EmitFxQuad` | `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp:1723`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp:1775`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp:1812` | 14 | Render/pass contexts | Keep: hot pass data; frame view leaks unrelated authority. |
| `EmitFxVertex` | `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp:342`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp:343`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp:344`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp:345`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp:346`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp:347` | 10 | Render/pass contexts | Keep: hot pass data; frame view leaks unrelated authority. |
| `EmitQuad` | `SkullbonezSource/Runtime/Editor/LauncherLaser.cpp:255`<br>`SkullbonezSource/Runtime/Editor/LauncherLaser.cpp:270` | 8 | Editor values | Keep: cohesive geometry/selection/body/event payload. |
| `EmitReplayRibbonGlowPairTo` | `SkullbonezSource/Runtime/Editor/RunEditorTracer.cpp:1029`<br>`SkullbonezSource/Runtime/Editor/RunEditorTracer.cpp:1040`<br>`SkullbonezSource/Runtime/Editor/RunEditorTracer.cpp:1060` | 9 | Editor values | Keep: cohesive geometry/selection/body/event payload. |
| `EmitReplayRibbonSegmentTo` | `SkullbonezSource/Runtime/Editor/RunEditorTracer.cpp:559`<br>`SkullbonezSource/Runtime/Editor/RunEditorTracer.cpp:608`<br>`SkullbonezSource/Runtime/Editor/RunEditorTracer.cpp:648`<br>`SkullbonezSource/Runtime/Editor/RunEditorTracer.cpp:658` | 8 | Editor values | Keep: cohesive geometry/selection/body/event payload. |
| `EmitReplayRibbonShapeOutlineTo` | `SkullbonezSource/Runtime/Editor/RunEditorTracer.cpp:1122` | 9 | Editor values | Keep: cohesive geometry/selection/body/event payload. |
| `EmitRibbon` | `SkullbonezSource/Runtime/Editor/LauncherLaser.cpp:305`<br>`SkullbonezSource/Runtime/Editor/LauncherLaser.cpp:313`<br>`SkullbonezSource/Runtime/Editor/LauncherLaser.cpp:321`<br>`SkullbonezSource/Runtime/Editor/LauncherLaser.cpp:322`<br>`SkullbonezSource/Runtime/Editor/LauncherLaser.cpp:330`<br>`SkullbonezSource/Runtime/Editor/LauncherLaser.cpp:331`<br>`SkullbonezSource/Runtime/Editor/LauncherLaser.cpp:344`<br>`SkullbonezSource/Runtime/Editor/LauncherLaser.cpp:352` | 8 | Editor values | Keep: cohesive geometry/selection/body/event payload. |
| `EmitShapeOutlineTo` | `SkullbonezSource/Runtime/Editor/RunEditorTracer.cpp:1138`<br>`SkullbonezSource/Runtime/Editor/RunEditorTracer.cpp:1149`<br>`SkullbonezSource/Runtime/Editor/RunEditorTracer.cpp:487` | 7 | Editor values | Keep: cohesive geometry/selection/body/event payload. |
| `EnsureReplayRestoreCheckpointTopology` | `SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:3343` | 7 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `EnterInspectionCamera` | `SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp:438` | 7 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `EvaluateInteractionAutomationAssertion` | `SkullbonezSource/Runtime/InteractionAutomationController.cpp:3271` | 11 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `ExecuteObjectThroughRenderGraph` | `SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp:1886`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp:1925`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp:1938` | 10 | Render/pass contexts | Keep: hot pass data; frame view leaks unrelated authority. |
| `ExecutePending` | `SkullbonezSource/Runtime/InputFrameExecution.cpp:1041` | 23 | Scene overlap at caller | Keep: cold explicit composition avoids a Run/Scene backpointer. |
| `ExecuteReflectionThroughRenderGraph` | `SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp:1860` | 10 | Render/pass contexts | Keep: hot pass data; frame view leaks unrelated authority. |
| `ExecuteUiTextThroughRenderGraph` | `SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp:2246` | 13 | Render/pass contexts | Keep: hot pass data; frame view leaks unrelated authority. |
| `ExecuteWaterThroughRenderGraph` | `SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp:1909` | 10 | Render/pass contexts | Keep: hot pass data; frame view leaks unrelated authority. |
| `ExitInspectionCamera` | `SkullbonezSource/Runtime/InputFrameExecution.cpp:794`<br>`SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp:417`<br>`SkullbonezSource/Runtime/Run.cpp:465` | 10 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `FireLauncherProjectile` | `SkullbonezSource/Runtime/Tools/RuntimeTools.cpp:631` | 9 | Tool/physics values | Keep: cohesive launcher or typed body payload. |
| `FireLauncherRay` | `SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:1070`<br>`SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:566`<br>`SkullbonezSource/Runtime/Tools/RuntimeTools.cpp:682` | 8 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `FormatReplayRestoreDivergenceMessage` | `SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:1809` | 10 | None; diagnostic values | Keep: explicit bounded log/probe schema; no owner plumbing. |
| `GameObjects::GameModelRenderer::GetObjectShadowBounds` | `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp:832` | 8 | Render/pass contexts | Keep: hot pass data; frame view leaks unrelated authority. |
| `GameObjects::GameModelRenderer::RenderModels` | `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp:1320`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp:1392` | 13 | Render/pass contexts | Keep: hot pass data; frame view leaks unrelated authority. |
| `GameObjects::GameModelRenderer::RenderShadowCasters` | `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp:963` | 9 | Render/pass contexts | Keep: hot pass data; frame view leaks unrelated authority. |
| `HandleReplayPausePressed` | `SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp:1438` | 11 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `HandleReplayPredictionHorizonPressed` | `SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp:1474` | 9 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `HandleReplayScrubPressed` | `SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp:1528` | 7 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `HandleReplayVelocityEditPressed` | `SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp:1452` | 11 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `InitDXR` | `SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp:1609` | 7 | Render/pass contexts | Keep: hot pass data; frame view leaks unrelated authority. |
| `InjectInteractionAutomationReplayControlClick` | `SkullbonezSource/Runtime/InteractionAutomationController.cpp:1362`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:1390`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:1421`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:1451`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:1483`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:1531` | 7 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `InjectReplaySaveProbeLauncherCoverage` | `SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:2486` | 8 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `InjectReplaySaveProbePlacementCoverage` | `SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:2475` | 9 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `Load` | `SkullbonezSource/Runtime/InputFrameExecution.cpp:352`<br>`SkullbonezSource/Runtime/Run.cpp:616`<br>`SkullbonezSource/Runtime/Run.cpp:772`<br>`SkullbonezSource/Runtime/RunFrame.cpp:1173`<br>`SkullbonezSource/Runtime/RunFrame.cpp:1303`<br>`SkullbonezSource/Runtime/RuntimeStressController.cpp:1069`<br>`SkullbonezSource/Runtime/Scene/SceneRequestExecution.cpp:79` | 24 | Scene overlap at caller | Keep: cold explicit composition avoids a Run/Scene backpointer. |
| `LogReplayRestoreProbe` | `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp:306` | 9 | None; diagnostic values | Keep: explicit bounded log/probe schema; no owner plumbing. |
| `LogReplayRestoreResult` | `SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:973`<br>`SkullbonezSource/Runtime/RuntimeDiagnostics.cpp:640` | 20 | None; diagnostic values | Keep: explicit bounded log/probe schema; no owner plumbing. |
| `LogReplayRestoreTargetSuccess` | `SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:3427` | 8 | None; diagnostic values | Keep: explicit bounded log/probe schema; no owner plumbing. |
| `LogReplayScrubProbe` | `SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:2708` | 12 | None; diagnostic values | Keep: explicit bounded log/probe schema; no owner plumbing. |
| `LogReplayV2TargetRestoreDiagnostic` | `SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:1923`<br>`SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:3214` | 15 | None; diagnostic values | Keep: explicit bounded log/probe schema; no owner plumbing. |
| `MakeEditorBodyDesc` | `SkullbonezSource/Runtime/Editor/RunEditorObjectPlacement.cpp:273`<br>`SkullbonezSource/Runtime/Editor/RunEditorObjectPlacement.cpp:308`<br>`SkullbonezSource/Runtime/Editor/RunEditorObjectPlacement.cpp:367`<br>`SkullbonezSource/Runtime/Editor/RunEditorObjectPlacement.cpp:426`<br>`SkullbonezSource/Runtime/Editor/RunEditorObjectPlacement.cpp:465`<br>`SkullbonezSource/Runtime/Editor/RunEditorObjectPlacement.cpp:568`<br>`SkullbonezSource/Runtime/Editor/RunEditorObjectPlacement.cpp:593`<br>`SkullbonezSource/Runtime/Editor/RunEditorObjectPlacement.cpp:618` | 9 | Editor values | Keep: cohesive geometry/selection/body/event payload. |
| `MakeEditorHousePart` | `SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1097`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1113`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1129`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1145`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1161`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1177`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1193`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1209`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1225`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1241`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1257`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1273`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1289`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1305`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1321`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1337`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1353`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1369`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1385`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1401`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1417`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1433`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1449`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1465`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1481`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1497`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1513`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1529`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1545`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1561`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1577`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1593`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1609`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:1625` | 16 | Editor values | Keep: cohesive geometry/selection/body/event payload. |
| `MakeEditorTreePart` | `SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:721`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:740`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:760`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:781`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:795`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:809`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:823`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:852`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:866`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:880`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:894`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:921`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:935`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:949`<br>`SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.cpp:963` | 17 | Editor values | Keep: cohesive geometry/selection/body/event payload. |
| `MakeGeneratedBodyDesc` | `SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.cpp:207`<br>`SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.cpp:236`<br>`SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.cpp:313`<br>`SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.cpp:364` | 7 | Scene values/contexts | Keep: cohesive setup/reset/body data; no repeated frame owners. |
| `MakePhysicsBodyCreateDesc` | `SkullbonezSource/Runtime/Editor/RunEditorObjectPlacement.cpp:113`<br>`SkullbonezSource/Runtime/Init.cpp:661`<br>`SkullbonezSource/Runtime/Init.cpp:696`<br>`SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp:144`<br>`SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.cpp:98` | 12 | Editor values | Keep: cohesive geometry/selection/body/event payload. |
| `MakeSceneBodyDesc` | `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp:233`<br>`SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp:510`<br>`SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp:553`<br>`SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp:598`<br>`SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp:633`<br>`SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp:701`<br>`SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp:759` | 12 | Scene values/contexts | Keep: cohesive setup/reset/body data; no repeated frame owners. |
| `Outline` | `SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:246`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:277`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:311`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:322`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:431`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:456`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:470`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:483`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:559`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:572`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:611`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:676`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:689`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:731`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:743` | 8 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `Physics::MakePhysicsBodyCreateDesc` | `SkullbonezSource/Runtime/Tools/RuntimeTools.cpp:817` | 12 | Tool/physics values | Keep: cohesive launcher or typed body payload. |
| `PopulateReplayRestoreTargetResult` | `SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:3420` | 7 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `PrepareBranchFileProbe` | `SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:2344` | 11 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `PrepareEditorGizmoGesture` | `SkullbonezSource/Runtime/Editor/RunEditorTools.cpp:1734` | 12 | Editor values | Keep: cohesive geometry/selection/body/event payload. |
| `PreparePresentation` | `SkullbonezSource/Runtime/InteractionAutomationController.cpp:2579`<br>`SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp:1466`<br>`SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:171` | 7 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `PrepareRenderFrame` | `SkullbonezSource/Runtime/RunRender.cpp:137` | 15 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `PrepareReplayPredictionOverlay` | `SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:3889` | 8 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `PrepareReplayRestoreArtifactSelection` | `SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:3235` | 8 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `PrepareSceneRuntimeLoad` | `SkullbonezSource/Runtime/Scene/RunScene.cpp:593` | 9 | Scene values/contexts | Keep: cohesive setup/reset/body data; no repeated frame owners. |
| `Ragdoll::AddPreviewLines` | `SkullbonezSource/Runtime/Editor/RunEditorTracer.cpp:974` | 7 | Editor values | Keep: cohesive geometry/selection/body/event payload. |
| `Rect` | `SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:1000`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:1085`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:1109`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:1131`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:1139`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:332`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:413`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:442`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:443`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:444`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:471`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:472`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:582`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:699`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:753`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:860`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:992`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:58`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:70`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:71`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:72`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:78`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:79`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:80`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:81` | 8 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `Render` | `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp:1298`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp:1371`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp:1441` | 8 | Render/pass contexts | Keep: hot pass data; frame view leaks unrelated authority. |
| `RenderCauseFocusOverlay` | `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp:372`<br>`SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:239` | 7 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `RenderExecuteUiTextFrame` | `SkullbonezSource/Runtime/RunFrame.cpp:779` | 9 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `RenderFluid` | `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp:1516` | 9 | Render/pass contexts | Keep: hot pass data; frame view leaks unrelated authority. |
| `RenderShadowMap` | `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp:1028`<br>`SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp:1052` | 12 | Render/pass contexts | Keep: hot pass data; frame view leaks unrelated authority. |
| `RenderUiText` | `SkullbonezSource/Runtime/RunFrame.cpp:241` | 12 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `ReplayEventCommandOperations::BuildCommand` | `SkullbonezSource/Runtime/InputFrameExecution.cpp:174`<br>`SkullbonezSource/Runtime/InputFrameExecution.cpp:207`<br>`SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp:342`<br>`SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:3445`<br>`SkullbonezSource/Runtime/Scene/SceneRequestExecution.cpp:164` | 10 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `ReplayEventCommandOperations::BuildEditorPlace` | `SkullbonezSource/Runtime/Editor/RunEditorTools.cpp:1211`<br>`SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:2511` | 7 | Editor values | Keep: cohesive geometry/selection/body/event payload. |
| `ReplayEventCommandOperations::BuildEditorTransform` | `SkullbonezSource/Runtime/Editor/RunEditorTools.cpp:153`<br>`SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:2521` | 8 | Editor values | Keep: cohesive geometry/selection/body/event payload. |
| `ReplayEventCommandOperations::BuildGeneratedSceneConfig` | `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp:1024` | 7 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `ReplayEventCommandOperations::BuildLauncherFire` | `SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:2539`<br>`SkullbonezSource/Runtime/Tools/RuntimeTools.cpp:672` | 7 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `ReplayPresentationOperations::ActivateLoadedPresentation` | `SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp:528`<br>`SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:2961`<br>`SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:3621` | 17 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `ReplayPresentationOperations::EnterInspectionCamera` | `SkullbonezSource/Runtime/Replay/ReplayAuthoringCauseTree.cpp:952`<br>`SkullbonezSource/Runtime/Replay/ReplayAuthoringVelocity.cpp:514`<br>`SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp:1341`<br>`SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp:455` | 7 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `ReplayPresentationOperations::ExitInspectionCamera` | `SkullbonezSource/Runtime/Replay/ReplayAuthoringCauseTree.cpp:962`<br>`SkullbonezSource/Runtime/Replay/ReplayAuthoringVelocity.cpp:524`<br>`SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp:758`<br>`SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp:798`<br>`SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp:825`<br>`SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp:855`<br>`SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp:1351`<br>`SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp:473`<br>`SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp:652`<br>`SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp:667` | 10 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `ReplayV2Artifact::SavePresentationWithSolverHashes` | `SkullbonezSource/Runtime/InteractionAutomationController.cpp:3552`<br>`SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp:1378` | 7 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `ReplayVelocityAxisColor` | `SkullbonezSource/Runtime/Editor/RunEditorTracer.cpp:1312`<br>`SkullbonezSource/Runtime/Editor/RunEditorTracer.cpp:1333` | 7 | Editor values | Keep: cohesive geometry/selection/body/event payload. |
| `ResolveReplayCauseTreeBodyPosition` | `SkullbonezSource/Runtime/Replay/ReplayAuthoringCauseTree.cpp:984` | 10 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `ResolveReplayPathColor` | `SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:1047`<br>`SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:1111`<br>`SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:1161`<br>`SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:1262`<br>`SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:1285`<br>`SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:1354`<br>`SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:1547`<br>`SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:652`<br>`SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:872`<br>`SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp:967` | 9 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `RestoreReplayBodyState` | `SkullbonezSource/Runtime/Init.cpp:828`<br>`SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:2829` | 11 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `RestoreSceneRuntimeResetSnapshot` | `SkullbonezSource/Runtime/Scene/RunScene.cpp:1065` | 7 | Scene values/contexts | Keep: cohesive setup/reset/body data; no repeated frame owners. |
| `RestoreV2ArtifactTargetState` | `SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp:701`<br>`SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:2327`<br>`SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:2379` | 8 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `RestoreV2ArtifactTargetStateImpl` | `SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:2401`<br>`SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:3173` | 9 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `RetainReplayPredictionAffectedBodyMarkers` | `SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:3621` | 10 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `RetainReplayPredictionRootRestMarker` | `SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:3572` | 7 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `RoundedRect` | `SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:1120`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:237`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:268`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:301`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:389`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:398`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:422`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:447`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:474`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:550`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:602`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:625`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:634`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:643`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:663`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:719`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:886`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:903`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:977`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:307`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:393` | 9 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `RouteEditorPlacementScalePointer` | `SkullbonezSource/Runtime/Editor/RunEditorTools.cpp:1672` | 10 | Editor values | Keep: cohesive geometry/selection/body/event payload. |
| `RouteWorldPointer` | `SkullbonezSource/Runtime/RunInput.cpp:368` | 10 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `RunReplayRestoreTargetStep` | `SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:3386` | 8 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `RuntimeDiagnostics::LogReplayRestoreProbe` | `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp:1172` | 10 | None; diagnostic values | Keep: explicit bounded log/probe schema; no owner plumbing. |
| `RuntimeDiagnostics::LogReplayRestoreResult` | `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp:1208` | 20 | None; diagnostic values | Keep: explicit bounded log/probe schema; no owner plumbing. |
| `RuntimeDiagnostics::LogReplayScrubProbe` | `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp:1146` | 13 | None; diagnostic values | Keep: explicit bounded log/probe schema; no owner plumbing. |
| `RuntimeFileWriter::NextNumberedPath` | `SkullbonezSource/Runtime/Editor/EditorTools.cpp:437`<br>`SkullbonezSource/Runtime/Editor/EditorTools.cpp:481` | 7 | Editor values | Keep: cohesive geometry/selection/body/event payload. |
| `RuntimeInteractionController::CaptureTransition` | `SkullbonezSource/Runtime/RuntimeInteractionController.cpp:478` | 7 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `RuntimeTools::TryRayCastTestHit` | `SkullbonezSource/Runtime/Tools/RuntimeTools.cpp:483` | 7 | Tool/physics values | Keep: cohesive launcher or typed body payload. |
| `SB_FATAL` | `SkullbonezSource/Runtime/RuntimeStressController.cpp:1140`<br>`SkullbonezSource/Runtime/Scene/SceneController.Objects.cpp:275` | 10 | None; diagnostic values | Keep: explicit bounded log/probe schema; no owner plumbing. |
| `SaveCurrentEditableSceneSnapshot` | `SkullbonezSource/Runtime/Scene/RunScene.cpp:1269` | 8 | Scene values/contexts | Keep: cohesive setup/reset/body data; no repeated frame owners. |
| `SavePresentationWithTracks` | `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp:2584`<br>`SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp:2593`<br>`SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp:2604` | 7 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `SelectReplayRestoreTargetAndCheckpoint` | `SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:1519` | 7 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `SetReplayPredictionHorizonFromPointer` | `SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp:1096`<br>`SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp:1175` | 8 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `SetWindowPos` | `SkullbonezSource/Runtime/RuntimeStressController.cpp:1112` | 7 | None; external API | Keep: platform positional contract. |
| `StackWalk64` | `SkullbonezSource/Runtime/Init.cpp:175` | 9 | None; external API | Keep: platform positional contract. |
| `Step` | `SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:159`<br>`SkullbonezSource/Runtime/Scene/SceneController.cpp:85` | 7 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `StepReplayPredictionJob` | `SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:3802` | 10 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `Terrain::TryCreateFromHeightMap` | `SkullbonezSource/Runtime/Run.cpp:565`<br>`SkullbonezSource/Runtime/Scene/RunScene.cpp:409` | 8 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `Text` | `SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:1086`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:1095`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:204`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:868`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:875`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:895`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:317`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:324`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:403`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:404` | 7 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `Text2d::Render2dQuad` | `SkullbonezSource/Runtime/RunUiTextPass.cpp:1007`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:1082`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:219`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:425`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:426`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:427`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:428`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:429`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:430`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:431`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:432` | 9 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `Text2d::Render2dTextColor` | `SkullbonezSource/Runtime/RunUiTextPass.cpp:1008`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:1015`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:1023`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:1086`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:1138`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:1139`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:1140`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:1141`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:135`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:136`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:224`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:225`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:226`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:229`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:436`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:443` | 8 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `TickAutoCycle` | `SkullbonezSource/Runtime/RunFrame.cpp:1230` | 8 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `TickCauseTreeInput` | `SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp:587` | 24 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `TickControls` | `SkullbonezSource/Runtime/RunFrame.cpp:1348` | 10 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `TickExecutePostPhysicsVisualizers` | `SkullbonezSource/Runtime/RunFrame.cpp:714` | 8 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `TickReplayScrubberGesture` | `SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp:1543` | 13 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `TickScrubberInput` | `SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp:563` | 21 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `TickVelocityEditInput` | `SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp:614` | 24 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `TickWorkspace` | `SkullbonezSource/Runtime/InputFrame.cpp:569` | 11 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `TryGetEditorSelectionFrame` | `SkullbonezSource/Runtime/Editor/RunEditorGizmoTools.cpp:117`<br>`SkullbonezSource/Runtime/Editor/RunEditorGizmoTools.cpp:178`<br>`SkullbonezSource/Runtime/Editor/RunEditorGizmoTools.cpp:280`<br>`SkullbonezSource/Runtime/Editor/RunEditorGizmoTools.cpp:73`<br>`SkullbonezSource/Runtime/Editor/RunEditorTools.cpp:1450`<br>`SkullbonezSource/Runtime/Editor/RunEditorTools.cpp:1476` | 8 | Editor values | Keep: cohesive geometry/selection/body/event payload. |
| `TryGetReplayFutureDepthInNodes` | `SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:2439` | 7 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `TryRayCastTestHit` | `SkullbonezSource/Runtime/Tools/RuntimeTools.cpp:705`<br>`SkullbonezSource/Runtime/Tools/RuntimeTools.cpp:771` | 7 | Tool/physics values | Keep: cohesive launcher or typed body payload. |
| `TryResolveEditorBodyCollider` | `SkullbonezSource/Runtime/Editor/RunEditorGizmoTools.cpp:414`<br>`SkullbonezSource/Runtime/Editor/RunEditorTools.cpp:1287`<br>`SkullbonezSource/Runtime/Editor/RunEditorTools.cpp:1422`<br>`SkullbonezSource/Runtime/Editor/RunEditorTools.cpp:425`<br>`SkullbonezSource/Runtime/Editor/RunEditorTools.cpp:467`<br>`SkullbonezSource/Runtime/Editor/RunEditorTools.cpp:529` | 7 | Editor values | Keep: cohesive geometry/selection/body/event payload. |
| `TryTraceEditorSelectionOverlayFromStores` | `SkullbonezSource/Runtime/Editor/RunEditorOverlayTools.cpp:166` | 9 | Editor values | Keep: cohesive geometry/selection/body/event payload. |
| `UpdateFrame` | `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp:1402` | 18 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `UpdateInput` | `SkullbonezSource/Runtime/InputFrame.cpp:521` | 15 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `UpdatePrediction` | `SkullbonezSource/Runtime/RunFrame.cpp:704` | 8 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `UpdateReplayPrediction` | `SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:3857` | 21 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `UpdateReplayPredictionFutureNodeCache` | `SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:3524`<br>`SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:3584` | 8 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `UpdateViewingOrientation` | `SkullbonezSource/Runtime/RunRender.cpp:55` | 8 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `UseDefaultTerrain` | `SkullbonezSource/Runtime/Scene/RunScene.cpp:710`<br>`SkullbonezSource/Runtime/Scene/RunScene.cpp:911` | 7 | Scene values/contexts | Keep: cohesive setup/reset/body data; no repeated frame owners. |
| `UseFlatSlopeTerrain` | `SkullbonezSource/Runtime/Scene/RunScene.cpp:888` | 9 | Scene values/contexts | Keep: cohesive setup/reset/body data; no repeated frame owners. |
| `VerifyLoadedPresentation` | `SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:2288` | 10 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `WriteEventf` | `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp:496`<br>`SkullbonezSource/Runtime/RuntimeDiagnostics.cpp:488`<br>`SkullbonezSource/Runtime/Scene/RunScene.cpp:1192` | 11 | None; diagnostic values | Keep: explicit bounded log/probe schema; no owner plumbing. |
| `WriteInteractionAutomationReport` | `SkullbonezSource/Runtime/InteractionAutomationController.cpp:2878`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:3473`<br>`SkullbonezSource/Runtime/Run.cpp:506` | 7 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `WriteReplayRestoreStepFailure` | `SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:1819` | 7 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `Writef` | `SkullbonezSource/Runtime/Init.cpp:205`<br>`SkullbonezSource/Runtime/RuntimeDiagnostics.cpp:527`<br>`SkullbonezSource/Runtime/RuntimeDiagnostics.cpp:592`<br>`SkullbonezSource/Runtime/RuntimeDiagnostics.cpp:691`<br>`SkullbonezSource/Runtime/RuntimeDiagnostics.cpp:747`<br>`SkullbonezSource/Runtime/RuntimeDiagnostics.cpp:832` | 46 | None; diagnostic values | Keep: explicit bounded log/probe schema; no owner plumbing. |
| `addControl` | `SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp:236`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp:243`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp:250`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp:257`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp:264`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp:271`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp:278`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp:285`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp:292`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp:299`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp:306`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp:313`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp:320` | 7 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `addMarkerOption` | `SkullbonezSource/Runtime/RunUiTextPass.cpp:645`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:664` | 12 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `addNode` | `SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:1860`<br>`SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp:1864` | 9 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `addPreview` | `SkullbonezSource/Runtime/RunUiTextPass.cpp:940`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:951` | 7 | No complete view overlap | Keep: cohesive owner-specific value/API payload; a frame view increases authority. |
| `drawReplayRow` | `SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:502`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:513` | 8 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `drawText` | `SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:218`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:226`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:254`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:285`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:341`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:491`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:591`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:652`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:708`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:762`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:786`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:798`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:807` | 7 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `failWithDiagnostic` | `SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:3318` | 10 | Replay records/stores | Keep: cohesive replay data; frame views broaden hot/owner boundaries. |
| `fprintf` | `SkullbonezSource/Runtime/Allocation/RuntimeAllocationTracker.cpp:563`<br>`SkullbonezSource/Runtime/Allocation/RuntimeAllocationTracker.cpp:624`<br>`SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp:1003`<br>`SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp:132`<br>`SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp:150`<br>`SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp:189`<br>`SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp:243`<br>`SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp:264`<br>`SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp:286`<br>`SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp:932`<br>`SkullbonezSource/Runtime/Editor/LauncherTools.cpp:374`<br>`SkullbonezSource/Runtime/Editor/LauncherTools.cpp:388`<br>`SkullbonezSource/Runtime/Editor/LauncherTools.cpp:458`<br>`SkullbonezSource/Runtime/Editor/LauncherTools.cpp:474`<br>`SkullbonezSource/Runtime/Editor/LauncherTools.cpp:479`<br>`SkullbonezSource/Runtime/Init.cpp:2572`<br>`SkullbonezSource/Runtime/Init.cpp:2593`<br>`SkullbonezSource/Runtime/Init.cpp:912`<br>`SkullbonezSource/Runtime/Init.cpp:937`<br>`SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp:116`<br>`SkullbonezSource/Runtime/Replay/ReplayTimeline.cpp:327`<br>`SkullbonezSource/Runtime/RuntimeDiagnostics.cpp:290`<br>`SkullbonezSource/Runtime/Scene/SceneController.cpp:606` | 34 | None; diagnostic values | Keep: explicit bounded log/probe schema; no owner plumbing. |
| `printf` | `SkullbonezSource/Runtime/Replay/ReplayTimeline.cpp:192`<br>`SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:3091`<br>`SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:3150`<br>`SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:3478`<br>`SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:3674`<br>`SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:726`<br>`SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:736`<br>`SkullbonezSource/Runtime/Run.cpp:222`<br>`SkullbonezSource/Runtime/Run.cpp:395`<br>`SkullbonezSource/Runtime/Run.cpp:407`<br>`SkullbonezSource/Runtime/RunFrame.cpp:467`<br>`SkullbonezSource/Runtime/RuntimeStressController.cpp:1149`<br>`SkullbonezSource/Runtime/RuntimeStressController.cpp:1235` | 44 | None; diagnostic values | Keep: explicit bounded log/probe schema; no owner plumbing. |
| `snprintf` | `SkullbonezSource/Runtime/Editor/RunEditorObjectPlacement.cpp:527` | 7 | None; diagnostic values | Keep: explicit bounded log/probe schema; no owner plumbing. |
| `sprintf_s` | `SkullbonezSource/Runtime/Editor/RunEditorObjectPlacement.cpp:414`<br>`SkullbonezSource/Runtime/Editor/RunEditorObjectPlacement.cpp:461`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:2637`<br>`SkullbonezSource/Runtime/InteractionAutomationController.cpp:2660`<br>`SkullbonezSource/Runtime/Replay/ReplayAuthoringCauseTree.cpp:510`<br>`SkullbonezSource/Runtime/Replay/ReplayAuthoringCauseTree.cpp:521`<br>`SkullbonezSource/Runtime/Replay/ReplayAuthoringCauseTree.cpp:705`<br>`SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp:779`<br>`SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp:2545`<br>`SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp:3325`<br>`SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:1612`<br>`SkullbonezSource/Runtime/Replay/ReplayValidation.cpp:1648`<br>`SkullbonezSource/Runtime/RunUiTextPass.cpp:270`<br>`SkullbonezSource/Runtime/RuntimeFileWriter.cpp:118` | 28 | None; diagnostic values | Keep: explicit bounded log/probe schema; no owner plumbing. |
| `std::fprintf` | `SkullbonezSource/Runtime/Allocation/RuntimeReserveAllocator.cpp:307`<br>`SkullbonezSource/Runtime/Allocation/RuntimeReserveAllocator.cpp:454`<br>`SkullbonezSource/Runtime/Allocation/RuntimeReserveAllocator.cpp:669`<br>`SkullbonezSource/Runtime/Allocation/RuntimeReserveAllocator.cpp:705` | 19 | None; diagnostic values | Keep: explicit bounded log/probe schema; no owner plumbing. |
| `std::snprintf` | `SkullbonezSource/Runtime/InputController.cpp:446` | 8 | None; diagnostic values | Keep: explicit bounded log/probe schema; no owner plumbing. |
## Physics Boundary Survey

`ObjectNarrowphasePairStageContext` contains 19 fields. Runtime-side `Step`,
`MakePhysicsBodyCreateDesc`, and `RestoreReplayBodyState` calls reach 7–12
arguments but remain named, stack-bounded store/scratch contexts or typed body
values; runtime frame views do not carry them.

Inventory total: **208 distinct invoked names** (11 migrated, 197 retained
with an individual reason).

## 2026-07-16 Post-PhysicsWorld >=12-Argument Addendum

T1 of `wide-call-desc-struct-pass` reran the same read-only lexical method over
the current `SkullbonezSource/Runtime/**/*.cpp` tip. Comments and literals were
masked without changing line positions; parentheses, brackets, and braces were
balanced; definitions and constructor initializers were excluded; and commas
were counted only at the invocation's top level. The scan found **41 current
invoked names with a maximum of at least 12 arguments**. This addendum
supersedes the older line/count/disposition cells for that tail; the full
7-argument inventory above remains historical T0 evidence until T4 regenerates
all touched rows.

Classification is intentionally narrower than "could be wrapped in a struct":
`convert` means the call is chiefly constructing one value/payload and labels
improve the boundary. `keep` means the call performs an operation over live
owners, is a per-item hot-path primitive, is an exact diagnostic/platform
schema, or already is the normalization factory for the output descriptor.

| Current invoked name | Current call site(s), max args | T1 classification and concrete disposition |
|---|---|---|
| `addMarkerOption` | `RunUiTextPass.cpp:649,668`; 12 | **Convert (UI record):** both calls populate one `UIProfilerMarkerOption`; pass that existing record with designated fields. |
| `AddReplayFutureNodeToNodes` | `Replay/ReplayPrediction.cpp:2458`; 12 | **Convert (replay record):** introduce `ReplayFutureNodeDesc` beside the future-node helpers; keep the node container as the operation-specific argument. |
| `AppendReplayRibbonVertex` | `Editor/RunEditorTracer.cpp:744`; 12 | **Convert (editor record):** introduce `ReplayRibbonVertexDesc` beside the fixed tracer vertex encoder; its value fields are one encoded ribbon vertex payload. |
| `ApplyReplayRestoreEditorPlaceEvent` | `Replay/ReplayValidation.cpp:1355`; 12 | **Convert (replay record):** introduce `ReplayRestoreEditorPlaceEventDesc` beside the restore operation; keep the output reason as the operation result boundary. |
| `BeginReplayPredictionJob` | `Replay/ReplayPrediction.cpp:3752`; 19 | **Convert (replay record):** introduce `ReplayPredictionJobDesc` beside the prediction job owner with designated owner/value borrows. |
| `BuildReplayProbeVisualProjection` | `Replay/ReplayValidation.cpp:2928`; 13 | **Convert (replay record):** introduce `ReplayProbeVisualProjectionDesc` beside the cold probe projection. |
| `BuildRuntimeRenderInputs` | `Render/RuntimeRenderer.cpp:2332`; 20 | **Convert (render record):** delete the restating builder and directly designated-initialize the existing `RuntimeRenderInputs`/`RuntimeRenderServices`; no second desc type is needed. |
| `DescribeReplayScrubberSurface` | `Replay/ReplayOverlayRenderer.cpp:118`; `Replay/ReplayScrubberTools.cpp:1223`; 12 | **Convert (replay record):** introduce `ReplayScrubberSurfaceDesc` adjacent to `ReplayScrubberSurfaceInput` in `ReplayOverlayLayout.h`; the function derives the latter from the labeled request. |
| `DispatchReflectionRays` | `Render/RuntimeRenderPasses.cpp:1237`; 17 | **Keep (hot render operation):** one immediate ray-dispatch API call binds live command/resource state; another aggregate would duplicate the backend binding contract rather than construct an owned record. |
| `EmitFxQuad` | `Render/RuntimeRenderPasses.cpp:1723,1775,1812`; 14 | **Keep (per-quad hot primitive):** called for each emitted effect quad; an aggregate adds a per-item copy while the positional groups are fixed geometry/color scalars consumed immediately. |
| `ExecutePending` | `InputFrameExecution.cpp:1029`; 22 (was 23) | **Keep (cold composition boundary):** explicitly names scene/host/interaction owners for command execution; a desc would be a multi-domain services bag, not record construction. |
| `ExecuteUiTextThroughRenderGraph` | `Render/RuntimeRenderer.cpp:2249`; 13 | **Keep (render scheduling operation):** values are immediately copied into the existing `UiTextGraphCallbackData`; a second desc would duplicate that callback record and add another per-frame copy. |
| `fprintf` | `Diagnostics/DiagnosticsRuntime.cpp:264,286,932,1003`; `Startup/StartupProbeHarnesses.cpp:381,406`; 34 | **Keep (external diagnostic API):** variadic fields are governed by six exact format strings; replacing the C boundary would neither label nor construct an engine record. |
| `GameObjects::GameModelRenderer::RenderModels` | `Render/RuntimeRenderPasses.cpp:1320,1392`; 13 | **Keep (hot render operation):** invoked directly in object/reflection passes over live render resources; a record would be copied per pass and own no state. |
| `Load` | `InputFrameExecution.cpp:354`; `Run.cpp:526,683`; `RunFrame.cpp:1102,1231`; `RuntimeStressController.cpp:1070`; `Scene/SceneRequestExecution.cpp:80`; 23 (was 24) | **Keep (cold scene-owner boundary):** the explicit owner list prevents `SceneController` reach-back; wrapping it recreates the broad scene/runtime context prohibited by the ownership rules. |
| `LogReplayRestoreResult` | `Replay/ReplayValidation.cpp:973`; `RuntimeDiagnostics.cpp:640`; 20 | **Keep (diagnostic schema):** exact restore outcome fields feed stable logging/report text at two boundaries; they are emitted, not retained as a record. |
| `LogReplayScrubProbe` | `Replay/ReplayValidation.cpp:2708`; 12 | **Keep (diagnostic schema):** exact probe fields are serialized once in compatibility-sensitive order; no record consumer exists. |
| `LogReplayV2TargetRestoreDiagnostic` | `Replay/ReplayValidation.cpp:1923,3214`; 15 | **Keep (diagnostic schema):** the call emits bounded target-restore evidence directly and owns no constructed value. |
| `MakeEditorHousePart` | `Editor/RunEditorPlacementAssets.cpp:1097..1625` (34 constexpr table rows); 16 | **Convert (editor record):** designated-initialize the existing `EditorHousePartDefinition`; the helper only restates its field order. |
| `MakeEditorTreePart` | `Editor/RunEditorPlacementAssets.cpp:721,740,760,781,795,809,823,852,866,880,894,921,935,949,963`; 17 | **Convert (editor record):** add `EditorTreePartDesc` beside the definition so designated inputs remain labeled while the helper preserves contact-release derivation. |
| `MakePhysicsBodyCreateDesc` | `Scene/SceneAuthoredSetup.cpp:144`; `Startup/StartupProbeHarnesses.cpp:107,141`; 12 | **Keep (normalization factory):** this is already the single `PhysicsBodyCreateDesc` factory and computes shape metrics/world-inertia policy; another input desc would duplicate the output descriptor and widen this runtime-only plan into Physics API policy. |
| `MakeSceneBodyDesc` | `Scene/SceneAuthoredSetup.cpp:233,510,553,598,633,701,759`; 12 | **Keep (scene normalization factory):** converts authored `fixed` policy into the physics motion enum before delegating to the derived-metric factory; a second scene record would only mirror that existing output boundary. |
| `Physics::MakePhysicsBodyCreateDesc` | `Tools/RuntimeTools.cpp:817`; 12 | **Keep (normalization factory):** same Physics API derivation contract as the unqualified calls; one cold tool call does not justify a duplicate input descriptor. |
| `PrepareEditorGizmoGesture` | `Editor/RunEditorTools.cpp:1734`; 12 | **Keep (editor operation):** reads live scene/physics/interaction owners and writes an `EditorGizmoGesturePlan`; wrapping those owners would broaden authority rather than construct the output plan. |
| `PrepareRenderFrame` | `RunRender.cpp:125`; 15 | **Keep (frame sequencing boundary):** combines existing narrow frame views at one composition-root checkpoint; a new aggregate would recreate a complete-frame authority bag. |
| `printf` | `Replay/ReplayValidation.cpp:3478,3674`; `RuntimeStressController.cpp:1235`; 44 | **Keep (external diagnostic API):** three fixed format strings own their positional fields; no engine record crosses this C variadic boundary. |
| `RenderShadowMap` | `Render/RuntimeRenderPasses.cpp:1028,1052`; 12 | **Keep (hot render operation):** runs for terrain/object shadow passes over live pass resources; an aggregate would add two per-frame copies without becoming owned data. |
| `RenderUiText` | `RunFrame.cpp:258`; 12 | **Keep (frame sequencing boundary):** consumes existing UI/render/replay owners in final frame order; bundling them would create a UI services bag. |
| `ReplayPresentationOperations::ActivateLoadedPresentation` | `Replay/ReplayScrubberTools.cpp:528`; `Replay/ReplayValidation.cpp:2961,3621`; 17 | **Keep (replay owner operation):** coordinates timeline, scrubber, presentation, prediction, authoring, and optional diagnostics atomically; a desc would retain no value and obscure the multi-owner transaction. |
| `RuntimeDiagnostics::LogReplayRestoreResult` | `Diagnostics/DiagnosticsRuntime.cpp:1208`; 20 | **Keep (diagnostic schema):** member sink for the same exact restore-report fields; converting only this layer creates a redundant transient record. |
| `RuntimeDiagnostics::LogReplayScrubProbe` | `Diagnostics/DiagnosticsRuntime.cpp:1146`; 13 | **Keep (diagnostic schema):** member sink serializes fixed scrub-probe evidence and does not construct reusable state. |
| `sprintf_s` | `Replay/ReplayAuthoringCauseTree.cpp:705`; `Replay/ReplayRecorder.cpp:3325`; `Replay/ReplayValidation.cpp:1612`; 28 | **Keep (external formatting API):** three bounded buffers have independent exact format schemas; the variadic C call is the final serialization boundary. |
| `std::fprintf` | `Allocation/RuntimeReserveAllocator.cpp:307,454,669,705`; 19 | **Keep (allocation diagnostics):** four cold fatal/report rows serialize allocator-policy evidence directly; a record would add code on the failure path without a consumer. |
| `TickCauseTreeInput` | `Replay/ReplayScrubberTools.cpp:587`; 24 | **Keep (input operation):** mutates cause-tree/interaction state once per input frame across existing owners; a desc would be a mutable replay services bag. |
| `TickReplayScrubberGesture` | `Replay/ReplayScrubberTools.cpp:1543`; 13 | **Keep (input operation):** gesture state and live replay owners are consumed synchronously once per input frame, not assembled into a record. |
| `TickScrubberInput` | `Replay/ReplayScrubberTools.cpp:563`; 21 | **Keep (input operation):** top-level replay input sequencer over live owners; wrapping it would duplicate the existing frame boundary and broaden authority. |
| `TickVelocityEditInput` | `Replay/ReplayScrubberTools.cpp:614`; 24 | **Keep (input operation):** edits authoring/physics/interaction state synchronously; a desc would collect mutable cross-domain owners. |
| `UpdateFrame` | `Replay/ReplayRuntime.cpp:1402`; 18 | **Keep (replay owner operation):** per-frame replay coordinator over timeline/presentation owners; a struct would be a retained-looking whole-replay bag with no record semantics. |
| `UpdateInput` | `InputFrame.cpp:522`; 15 | **Keep (frame sequencing boundary):** consumes the four existing narrow frame views once; another aggregate would recreate the complete-frame bag the signature plan removed. |
| `UpdateReplayPrediction` | `Replay/ReplayPrediction.cpp:3857`; 21 | **Keep (prediction owner operation):** schedules live physics/worker/presentation state once per frame; wrapping mutable owners would obscure authority and add no constructed value. |
| `Writef` | `RuntimeDiagnostics.cpp:527,592,691,747,832`; 46 | **Keep (diagnostic API):** five fixed bounded log schemas terminate at the variadic writer; there is no common record shape or downstream record consumer. |

### T2/T3 conversion set and placement

The conversion set is therefore ten invoked names: one render row for T2 and
nine UI/replay/editor rows for T3. Structures stay adjacent to the operation or
existing output record, and every construction site uses designated fields:

- T2 directly constructs existing `RuntimeRenderInputs` and nested
  `RuntimeRenderServices` in `RuntimeRenderer.cpp`; the restating
  `BuildRuntimeRenderInputs` function is deleted.
- `ReplayPredictionJobDesc` and `ReplayFutureNodeDesc` stay in
  `ReplayPrediction.cpp`; neither is retained by prediction owners or workers.
- `ReplayProbeVisualProjectionDesc` and
  `ReplayRestoreEditorPlaceEventDesc` stay in `ReplayValidation.cpp` as cold
  stack-only request values.
- `ReplayScrubberSurfaceDesc` is public beside `ReplayScrubberSurfaceInput` in
  `ReplayOverlayLayout.h` because overlay rendering and input resolution are
  its two synchronous consumers.
- `ReplayRibbonVertexDesc` stays in `RunEditorTracer.cpp` beside the encoder.
- `EditorTreePartDesc` stays beside `EditorTreePartDefinition`; house rows use
  direct designated initialization of `EditorHousePartDefinition`.
- profiler marker calls directly designate the existing
  `UIProfilerMarkerOption` passed to the local bounded append lambda.

T1 is documentation-only; no repository validation is required. T4 will rerun
the balanced-token scan after the conversions, update the surviving exact
counts, and verify that every remaining >=12 row has the individual reason
recorded here rather than an old category boilerplate.

### T2 render conversion evidence

`BuildRuntimeRenderInputs` is deleted. `RuntimeRenderer::RenderFrameEntry`
now designated-initializes the existing `RuntimeRenderInputs` and nested
`RuntimeRenderServices` directly at the single call site, preserving the exact
20 source expressions and declaration order while labeling every borrow. No
second desc type, extra copy, stored frame value, or owner boundary was added.

Evidence on 2026-07-16:

- targeted `Profile|x64` build: passed in 11.50 s with zero warnings/errors;
- `tools\\validate_format.bat`: passed for all implementations and 251 headers;
- `tools\\validate_dx12_renderer.bat`: exited `0` in 51.94 s with zero-warning
  Profile/Debug builds, zero DX12 InfoQueue errors, and all committed screenshot
  comparisons passing;
- `tools\\run_graphics_stress.bat 1`: exited `0` in 62.72 s after the bounded
  one-minute PID-scoped run (PID 32932), with empty stderr, tracked overshoot
  `0`, and memory reconciliation delta `0`.

Comment-quality audit: 1/1 touched source file inspected, zero deferred. The
renderer retains its full learning header and the new local `Why:` comment
states the field-label and one-frame lifetime contract. No baseline, screenshot,
golden, scene, or authored-data file changed.

## 2026-07-16 Final Post-Conversion Truth Pass

T4 reran the same comment/literal-masked balanced-token parser over the
committed T3 tip. Definitions and constructor initializers were excluded, all
four delimiter kinds were balanced, and only top-level commas counted. The
16.89 s scan found **31 current invoked names with a maximum of at least 12
arguments**, down exactly ten from T1's 41-name tail. The ten-row conversion
reconciliation is complete:

| Converted name | Final max args and current site(s) | Reconciliation |
|---|---|---|
| `addMarkerOption` | 1 — `RunUiTextPass.cpp:634,654` | Two designated `UIProfilerMarkerOption` payloads. |
| `AddReplayFutureNodeToNodes` | 2 — `Replay/ReplayPrediction.cpp:2475` | Node container plus designated `ReplayFutureNodeDesc`. |
| `AppendReplayRibbonVertex` | 2 — `Editor/RunEditorTracer.cpp:754` | Vertex vector plus designated `ReplayRibbonVertexDesc`. |
| `ApplyReplayRestoreEditorPlaceEvent` | 2 — `Replay/ReplayValidation.cpp:1388` | Designated restore desc plus sequencing callback. |
| `BeginReplayPredictionJob` | 1 — `Replay/ReplayPrediction.cpp:3795` | One designated `ReplayPredictionJobDesc`. |
| `BuildReplayProbeVisualProjection` | 1 — `Replay/ReplayValidation.cpp:2961` | One designated cold-probe desc. |
| `BuildRuntimeRenderInputs` | 0 — deleted | Existing render input records are designated directly. |
| `DescribeReplayScrubberSurface` | 1 — `Replay/ReplayOverlayRenderer.cpp:117`; `Replay/ReplayScrubberTools.cpp:1223` | One designated surface request at each consumer. |
| `MakeEditorHousePart` | 0 — deleted | All 34 constexpr house rows designate the existing definition directly. |
| `MakeEditorTreePart` | 1 — `Editor/RunEditorPlacementAssets.cpp:671,690,710,732,746,760,774,803,817,831,845,872,886,900,914` | All 15 rows pass one designated desc while retaining derived contact release. |

The complete surviving tail follows. Each reason was rechecked against the
current call site rather than inherited by category; no boilerplate exemption
or unclassified >=12-argument invocation remains.

| Surviving invoked name | Current >=12-argument site(s), exact max | Individual keep reason |
|---|---|---|
| `DispatchReflectionRays` | `Render/RuntimeRenderPasses.cpp:1237`; 17 | **Hot render operation:** one immediate per-pass command/resource binding call; copying the same live bindings into another record would duplicate the backend contract without constructing owned data. |
| `EmitFxQuad` | `Render/RuntimeRenderPasses.cpp:1723,1775,1812`; 14 | **Per-quad hot primitive:** three effect paths invoke it for every emitted quad; a desc adds a per-item aggregate copy to fixed geometry/color scalars consumed immediately. |
| `ExecutePending` | `InputFrameExecution.cpp:1029`; 22 | **Cold composition boundary:** explicitly names scene, host, and interaction owners for command execution; an aggregate would be a mutable multi-domain services bag. |
| `ExecuteUiTextThroughRenderGraph` | `Render/RuntimeRenderer.cpp:2221`; 13 | **Render scheduling operation:** values are copied once into the already-existing `UiTextGraphCallbackData`; a second desc duplicates that record and adds a per-frame copy. |
| `fprintf` | `Diagnostics/DiagnosticsRuntime.cpp:264,286,932,1003`; `Startup/StartupProbeHarnesses.cpp:381,406`; 34 | **External diagnostic API:** six distinct fixed format strings own these positional schemas; no engine record exists beyond the final variadic serialization boundary. |
| `GameObjects::GameModelRenderer::RenderModels` | `Render/RuntimeRenderPasses.cpp:1320,1392`; 13 | **Hot render operation:** object and reflection passes consume live renderer resources directly; a copied pass record would own no state and execute twice per frame. |
| `Load` | `InputFrameExecution.cpp:354`; `Run.cpp:526,683`; `RunFrame.cpp:1102,1231`; `RuntimeStressController.cpp:1070`; `Scene/SceneRequestExecution.cpp:80`; 23 | **Cold scene-owner boundary:** the explicit owner list prevents `SceneController` reach-back; wrapping it recreates the prohibited broad scene/runtime context. |
| `LogReplayRestoreResult` | `Replay/ReplayValidation.cpp:988`; `RuntimeDiagnostics.cpp:640`; 20 | **Diagnostic schema:** two stable restore-report sinks emit the exact fields immediately; there is no retained record consumer to justify a copied desc. |
| `LogReplayScrubProbe` | `Replay/ReplayValidation.cpp:2742`; 12 | **Diagnostic schema:** one compatibility-sensitive scrub-probe row is serialized in fixed order and never retained. |
| `LogReplayV2TargetRestoreDiagnostic` | `Replay/ReplayValidation.cpp:1957,3248`; 15 | **Diagnostic schema:** two bounded target-restore evidence rows terminate directly at logging; no reusable value crosses the boundary. |
| `MakePhysicsBodyCreateDesc` | `Scene/SceneAuthoredSetup.cpp:144`; `Startup/StartupProbeHarnesses.cpp:107,141`; 12 | **Normalization factory:** this already produces `PhysicsBodyCreateDesc` and derives shape metrics/world-inertia policy; a second input desc duplicates the output contract and crosses into Physics API policy. |
| `MakeSceneBodyDesc` | `Scene/SceneAuthoredSetup.cpp:233,510,553,598,633,701,759`; 12 | **Scene normalization factory:** seven cold authored rows convert fixed policy to motion kind before delegating to the derived-metric factory; another record only mirrors the existing output. |
| `Physics::MakePhysicsBodyCreateDesc` | `Tools/RuntimeTools.cpp:817`; 12 | **Normalization factory:** the single cold tool call uses the same Physics derivation contract; a duplicate input record has no additional owner or consumer. |
| `PrepareEditorGizmoGesture` | `Editor/RunEditorTools.cpp:1734`; 12 | **Editor operation:** reads live scene/physics/interaction owners and writes `EditorGizmoGesturePlan`; wrapping the owners broadens authority rather than constructs the output plan. |
| `PrepareRenderFrame` | `RunRender.cpp:125`; 15 | **Frame sequencing boundary:** combines the existing narrow frame views once at the composition root; another aggregate recreates complete-frame authority. |
| `printf` | `Replay/ReplayValidation.cpp:3512,3708`; `RuntimeStressController.cpp:1235`; 44 | **External diagnostic API:** three independent fixed format strings terminate at the C variadic boundary; no common record or downstream consumer exists. |
| `RenderShadowMap` | `Render/RuntimeRenderPasses.cpp:1028,1052`; 12 | **Hot render operation:** terrain and object shadow passes consume live pass resources twice per frame; a desc adds two copies without owned state. |
| `RenderUiText` | `RunFrame.cpp:258`; 12 | **Frame sequencing boundary:** consumes existing UI/render/replay owners in final frame order; bundling them would create a UI services bag. |
| `ReplayPresentationOperations::ActivateLoadedPresentation` | `Replay/ReplayScrubberTools.cpp:528`; `Replay/ReplayValidation.cpp:2995,3655`; 17 | **Replay owner transaction:** atomically coordinates timeline, scrubber, presentation, prediction, authoring, and diagnostics; a transient desc obscures that live multi-owner operation and retains no value. |
| `RuntimeDiagnostics::LogReplayRestoreResult` | `Diagnostics/DiagnosticsRuntime.cpp:1208`; 20 | **Diagnostic sink:** the member boundary serializes the same exact restore fields; converting only this layer creates a redundant copied record. |
| `RuntimeDiagnostics::LogReplayScrubProbe` | `Diagnostics/DiagnosticsRuntime.cpp:1146`; 13 | **Diagnostic sink:** fixed scrub evidence is emitted immediately and does not construct reusable state. |
| `sprintf_s` | `Replay/ReplayAuthoringCauseTree.cpp:705`; `Replay/ReplayRecorder.cpp:3325`; `Replay/ReplayValidation.cpp:1646`; 28 | **External formatting API:** three bounded buffers have independent fixed schemas; the variadic call is their final serialization boundary. |
| `std::fprintf` | `Allocation/RuntimeReserveAllocator.cpp:307,454,669,705`; 19 | **Allocation diagnostics:** four cold fatal/report rows serialize policy evidence directly; a record adds failure-path code without a consumer. |
| `TickCauseTreeInput` | `Replay/ReplayScrubberTools.cpp:587`; 24 | **Input operation:** mutates cause-tree and interaction owners once per input frame; a desc would be a mutable replay services bag. |
| `TickReplayScrubberGesture` | `Replay/ReplayScrubberTools.cpp:1544`; 13 | **Input operation:** gesture state and live replay owners are consumed synchronously once per input frame, not assembled into a value. |
| `TickScrubberInput` | `Replay/ReplayScrubberTools.cpp:563`; 21 | **Input sequencing boundary:** top-level replay input coordinates live owners; an aggregate duplicates the existing frame boundary and broadens authority. |
| `TickVelocityEditInput` | `Replay/ReplayScrubberTools.cpp:614`; 24 | **Input operation:** synchronously edits authoring, physics, and interaction state; a desc would collect mutable cross-domain owners. |
| `UpdateFrame` | `Replay/ReplayRuntime.cpp:1402`; 18 | **Replay owner operation:** per-frame coordinator over live timeline/presentation owners; a whole-replay bag would have no record lifetime or value semantics. |
| `UpdateInput` | `InputFrame.cpp:522`; 15 | **Frame sequencing boundary:** consumes four existing narrow frame views once; another aggregate recreates the complete-frame bag removed by the signature plan. |
| `UpdateReplayPrediction` | `Replay/ReplayPrediction.cpp:3901`; 21 | **Prediction owner operation:** schedules live physics, worker, and presentation state once per frame; wrapping mutable owners obscures authority and constructs no value. |
| `Writef` | `RuntimeDiagnostics.cpp:527,592,691,747,832`; 46 | **Diagnostic API:** five bounded stable log schemas terminate at the variadic writer; there is no common record shape or retained consumer. |

Final proof: **10/10 conversion names reconciled, 31/31 surviving names carry
individual reasons, zero unclassified >=12-argument invocations**. T4 changes
documentation only; no repository validation is required.

## T5 Plan And Campaign Closure

The final committed T1–T4 range is `a7adf825^..1ee14258`. History names exactly
11 source files plus plan/report/session documentation and the two approved
replay manifests. It contains no screenshot, physics baseline, scene, shader,
asset, hull, or authored-config change. The only baseline-like diff is one
`configSha256` field and the dependent `visualBaselineSha256` field approved by
the owner on 2026-07-16. Both hashes recompute, and every replay tick, final-
state value, causal node/tick, artifact field, and other behavioral golden value
is unchanged.

Final validation on the committed T4 tip:

- `tools\\validate_full.bat`: exited `0` in 113.79 s. Formatting and 711/711
  project/filter rows passed; all CPU suites passed, including 202/202 doctest
  cases and 12,595/12,595 assertions; Profile, Automation, and Debug built with
  zero warnings/errors; replay/prediction smoke passed; DX12 reported zero
  InfoQueue errors and all three screenshot comparisons passed; standalone
  physics/runtime-handle smoke passed; and `physics_regression_varied.csv`
  matched all 44,401 lines byte-exactly.
- T3 replay fidelity is recorded accurately as the sole engine invocation plus
  CPU-only continuation, not as a successful batch exit. One engine process and
  one prediction generation produced the 2,401-tick report; after the approved
  provenance correction, the normal equality check and all nine false-pass/
  determinism controls passed without another engine launch.

Final touched-file comment checklist (this report is the retained checklist):

- [x] `Runtime/Render/RuntimeRenderer.cpp`
- [x] `Runtime/Editor/EditorPlacementAssets.h`
- [x] `Runtime/Editor/RunEditorPlacementAssets.cpp`
- [x] `Runtime/Editor/RunEditorTracer.cpp`
- [x] `Runtime/Replay/ReplayOverlayLayout.cpp`
- [x] `Runtime/Replay/ReplayOverlayLayout.h`
- [x] `Runtime/Replay/ReplayOverlayRenderer.cpp`
- [x] `Runtime/Replay/ReplayPrediction.cpp`
- [x] `Runtime/Replay/ReplayScrubberTools.cpp`
- [x] `Runtime/Replay/ReplayValidation.cpp`
- [x] `Runtime/RunUiTextPass.cpp`

Checked: **11/11**. Deferred/unchecked: **0**. Every file has the required
learning-header sections plus nearby lifetime/why/invariant teaching where the
new records need it. The independent final review found one inaccurate lifetime
claim beside `ReplayPredictionJobDesc`: three pointers are retained by the
scheduled worker until cancellation/completion. T5 corrected that comment to
match `ReplayPredictionWorkerOperation`; the remediation is strictly comments
and requires no repository validation.

Independent rubber-duck verdict: no behavioral mapping, allocation, exception,
determinism, DX12, provenance, arity-inventory, or remaining comment blocker.
Independent parsers found zero field/expression mismatches across the 20 render
inputs, 15 tree rows, 34 house rows, and every replay/UI/editor desc conversion,
and reproduced the exact 31-name surviving tail. Residual evidence risk is
limited to historical process-count proof: artifacts cannot prove a negative,
but the one-process launcher, `predictionGenerationCount=1`, sole report, and
CPU-only continuation agree.

Rubber-duck accounting: run `wide-call-desc-struct-pass-duck-01`, reviewer
`/root/wide_t5_final_review`, final plan review; prompt 1,013 characters;
response 2,725 characters; token counts not exposed; elapsed 12m 00.54s;
verdict one blocking comment defect, remediated;
no behavioral follow-up review required.

Acceptance is closed: ten construction names are 0–2 arguments, every surviving
>=12-argument invocation has an individual concrete reason, designated fields
are visible at every converted call site, all mapped/final gates pass, and the
runtime mass-reduction campaign is complete at 16/16.
