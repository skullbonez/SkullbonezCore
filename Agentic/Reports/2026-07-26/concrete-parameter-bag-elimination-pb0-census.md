# Concrete Parameter-Bag Elimination PB0 Census

Date: 2026-07-26  
Implementation tip: `e61e82a6`  
Branch: `nightrunner-25th-JUL-26`  
Scope: PB0 documentation-only census and owner-design ratification

## Result

PB0 passes. All 22 registered rows were re-audited at the implementation tip.
The audit adds eight repair rows and carries forward the three 13-parameter
render/UI operations assigned by GV1. Every other reviewed hit receives an
explicit retain ruling below.

The implementation census is therefore:

- 22 registered aggregate rows;
- 8 PB0-added repair rows, including two exact symbol families;
- 3 existing 13-parameter operations assigned to PB3;
- 0 source changes and no repository validation required.

All replacement designs use concrete values, concrete owner operations, or a
stack-scoped phase transaction. None permits inheritance, virtual dispatch,
type erasure, callback interfaces, retained host pointers, or renamed service
bags.

## Registered Rows

| # | Target and current evidence | Concrete endpoint and lifetime | Deletion proof and mapped gate |
|---:|---|---|---|
| 1 | `SceneSaveRequest`, `SceneSnapshotWriter.h:66`. Production construction: `EditorTools.cpp:470`, `Run.cpp:824`, `SceneController.Load.cpp:550`; test construction: `TestSceneSnapshotWriter.cpp:638`; sole consumer: `SceneSnapshotWriter.cpp:311`. The editor path initializes only through `textOn`, leaving later persisted values defaulted. | Retain the domain name only as `{path, SceneWorldSaveState, SceneSessionSaveState, PresentationSaveState}`. Each concrete owner publishes only its own values. The request is stack-local and the writer retains nothing. | Old scalar fields disappear from the request. PB1: focused all-field/all-entry serialization tests, `validate_tests`, `validate_full`. |
| 2 | `SceneSaveView`, `SceneSnapshotWriter.h:51`. Constructed beside row 1 at `EditorTools.cpp:460`, `Run.cpp:813`, `SceneController.Load.cpp:540`, and `TestSceneSnapshotWriter.cpp:637`; forwarded through `BehaviorGroupAt:180`, `AddSceneObjectGroupJson:185`, `ResolveLiveSceneRow:221`, and `BuildLiveStateJson:248`. | Delete. `SceneSnapshotWriter::Save` consumes the SceneWorld-owned save publication in row 1 directly. | `rg -n '\bSceneSaveView\b' SkullbonezSource SkullbonezTests` returns no rows. PB1 gates. |
| 3 | `SceneLoadPolicyInputs`, `SceneController.h:154`. Seven constructions: `InputFrameExecution.cpp:478,1289`, `RunFrame.cpp:1200,1352`, `Run.cpp:599,854`, `RuntimeStressController.cpp:1126`; forwarded by `SceneRequestExecution.cpp:51-78` and `SceneController.Load.cpp:633-647`; unpacked by `SceneController::Load` at `SceneController.Load.cpp:845-866`. | Delete the master bundle. Existing `SceneLoadTransaction` stays the concrete sequencing owner and borrows focused owners only within its load phases. It retains request, detached outputs, and cursor values only. | `rg -n '\bSceneLoadPolicyInputs\b' SkullbonezSource SkullbonezTests` returns no rows. PB1 lifecycle/transaction tests, `validate_tests`, `validate_full`. |
| 4 | `SceneLoadConsumerOutputs` already has zero definitions/usages. The post-GV form is private `SceneLoadTransaction::Outputs`, `SceneLoadTransaction.h:163`, reset at `SceneController.Load.cpp:584` and consumed only by transaction phases at `:665` and `:803`. | Already absorbed correctly. Keep the private detached values; no caller can construct or recover them. | The symbol remains absent under the row 3 proof. PB1 verifies the GV result. |
| 5 | `RuntimePointerRouteInput`, `InputRouter.h:160`. Sole construction `InputFrameExecution.cpp:1180-1207`; sole consumer `InputRouter.Interactions.cpp:235`; projected to editor at `:268-282`, mouse pickup `:318-342`, Replay `:377-401`, launcher `:406-413`, and attached-camera selection `:353-373`. | `InputRouter` remains precedence owner. Route independently meaningful pointer-edge, gesture, and world-ray values to concrete consumers; retain only existing router state. | `rg -n '\bRuntimePointerRouteInput\b' SkullbonezSource SkullbonezTests` returns no rows. PB2 focused precedence tests, `validate_fast`, `validate_full`. |
| 6 | `EditorPointerRouteInput`, `InputRouter.h:117`. Constructed at `InputRouter.Interactions.cpp:268-282`; consumed at `EditorInteractionTools.cpp:1677`; projected again for preview `:1727-1735`, gizmo `:1777-1786`, gesture `:1810-1820`, scale `:1860-1865`, and selection `:1879-1893`. | Delete the middle projection. Concrete RuntimeTools operations borrow only their gesture/ray values. Preserve preview -> end-drag -> begin-gizmo/scale -> selection order. | `rg -n '\bEditorPointerRouteInput\b' SkullbonezSource SkullbonezTests` returns no rows. PB2 gates. |
| 7 | `MousePickupPointerInput`, `RuntimeTools.h:242`. Constructed at `InputRouter.Interactions.cpp:318-342`; consumed at `MousePickupTools.cpp:56`; pick facts are repacked to `RuntimePickRequest` at `:137-145`. | `RuntimeTools` remains mouse-pick owner and retains only `RunMousePickupState`. The operation borrows focused gesture, ray, camera, SceneWorld, router, and interaction operands per call. | `rg -n '\bMousePickupPointerInput\b' SkullbonezSource SkullbonezTests` returns no rows. PB2 gates. |
| 8 | `RenderFrameContext`, `RuntimeRenderPasses.h:212-292`; builder declaration `RuntimeRenderer.h:245`, definition `RuntimeRenderer.cpp:1567-1638`, sole construction `:1816`. It feeds graph phases at `RuntimeRenderer.cpp:429,827,907,975,997,1111,1144,1191,1240,1306,1332,1373,1424` and pass phases throughout `RuntimeRenderPasses.cpp`. | Delete the builder and 45-field context. `RuntimeRenderer` phases combine the borrowed `RuntimeRenderModelFrameView`, focused policy values, and renderer-owned resources directly. Shadow/reflection results remain stack values. | `rg -n '\bRenderFrameContext\b|\bBuildRenderFrameContext\b' SkullbonezSource` returns no rows. PB3 `validate_dx12_renderer`, graphics stress, `validate_perf`, final full gate. |
| 9 | `UiTextPassInputs`, `RuntimeRenderPasses.h:437-463`. Built at `OperatorEditorFrameComposer.cpp:634-648`, passed through `RuntimeRenderer.cpp:2301,1537-1563`, graph callback `:577`, then consumed at `UiTextPass.cpp:307`; helpers consume it at `:209,214`. | Split concrete UI-text work by output responsibility: diagnostics/timers, ordinary HUD, Replay overlay, operator UI, and submission. `UiTextPass` retains only its own text/draw state and borrows publications/resources per phase. | `rg -n '\bUiTextPassInputs\b' SkullbonezSource` returns no rows. PB3 renderer/stress/perf gates plus focused UI/overlay tests. |
| 10 | `ReplayOverlayRenderContext`, `ReplayOverlayPackets.h:82-100`. Sole construction `OperatorEditorFrameComposer.cpp:608-621`; embedded at `:645`; forwarded at `UiTextPass.cpp:206-212`; consumers are declared at `ReplayOverlayRenderer.h:79-98` and implemented at `ReplayOverlayRenderer.cpp:108,914,950,1076,1251`. Three UI-mode fields are unused. | Retain `ReplayOverlayStateView` as the immutable Replay publication. Move concrete drawing to Runtime/Render; render resources remain concrete owner members and the pass borrows publication, gesture, scene flag, dimensions, and time. | `rg -n '\bReplayOverlayRenderContext\b' SkullbonezSource` returns no rows. PB3 gates plus `validate_replay_visual_fidelity`; no baseline refresh. |
| 11 | `ReplayCaptureInput`, `ReplayRecorder.h:457-482`. Normal construction `RunFrame.cpp:165-180`; verifier construction `ReplayRestoreService.h:232-250`; progressively mutated by `ReplayRuntime.cpp:1401-1408`, `ReplayTimeline.cpp:355-387`, and `ReplayPresentation.cpp:73-77`; flattened by `ReplayRecorder.cpp:2068` and solver capture at `:2827`. | `ReplayTimeline` becomes the concrete capture-sequencing owner. Reuse real `ReplayBranchInfo`, `ReplayCameraSample`, `ReplayWorldPresentationSample`, and `ReplayLauncherVisualSample` domain values. Recorders retain rings/cursors/samples and borrow stores per phase. | `rg -n '\bReplayCaptureInput\b' SkullbonezSource` returns no rows. PB4 focused capture, artifact, visual-fidelity, allocation-policy, and full gates. |
| 12 | `ReplayCameraFocusRequest`, `ReplayPresentation.h:206-223`. Sole construction `ReplayAuthoringCauseTree.cpp:935-950`; sole consumer `ReplayPresentation.cpp:431-447`, which copies it field-for-field into `m_camera`. | Delete. A focused concrete `ReplayPresentation` operation applies the selected `RunReplayCauseTreeRow`, row/focus identity, and resolved geometry directly to `m_camera`, preserving private restore fields. | `rg -n '\bReplayCameraFocusRequest\b' SkullbonezSource` returns no rows. PB4 focused camera and visual-fidelity gates. |
| 13 | `PersistentContactSolverContext`, `PhysicsContactSolverStage.h:125`. Constructed at `PhysicsContactSolverStage.cpp:160` and test fixture `TestPersistentContactSolver.cpp:225`; sole production consumer `PersistentContactSolver.cpp:96` immediately aliases nearly every field. | `PhysicsContactSolverStage` absorbs the stateless row solver and policy phases. It retains its existing reserved vectors and borrows stores, stage owners, diagnostics, candidate span, settings/forces, `dt`, and profiler synchronously. | `rg -n '\bPersistentContactSolverContext\b' SkullbonezSource SkullbonezTests` returns no rows. PB5 focused solver tests, physics, perf, full. |
| 14 | `PhysicsContactSolverStageContext`, `PhysicsContactSolverStage.h:154`. Sole construction `PhysicsWorld.cpp:1012`; sole consumer `PhysicsContactSolverStage.cpp:154`; exists only to repack row 13 and includes an unused `colliderStore`. | Same concrete stage endpoint as row 13. Derive rows/counts/capacities from concrete stores and keep the stage boundary at or below 12 parameters. | `rg -n '\bPhysicsContactSolverStageContext\b' SkullbonezSource SkullbonezTests` returns no rows. PB5 gates. |
| 15 | `ObjectNarrowphasePairStageContext`, `PhysicsNarrowphaseStage.h:93`. Constructed at `PhysicsWorld.cpp:879` and `TestPhysicsStageState.cpp:574`; consumed at `PhysicsNarrowphaseStage.cpp:369`, `PhysicsNarrowphaseStage.Execution.cpp:56,218`, and forwarded unchanged to the private island worker. | `PhysicsNarrowphaseStage` retains its reserved scratch. Serial and parallel concrete entries borrow stores/stages, normalized step policy, terrain/forces/buoyancy/time, and profiler. Return the serial event by value and preserve ordered post-worker commit. | `rg -n '\bObjectNarrowphasePairStageContext\b' SkullbonezSource SkullbonezTests` returns no rows. PB5 pair-slot, collision, determinism, physics/perf/full gates. |
| 16 | `PhysicsSleepIslandStageContext`, `PhysicsSleepController.h:130`. Constructed at `PhysicsWorld.cpp:1068` and `TestPhysicsStageState.cpp:449,504`; consumed by `RunIslandStage`, `PhysicsSleepController.cpp:521`, then forwarded to `ApplyTransitions` at `:735,738`. | `PhysicsSleepController` retains its sleep/island/queue scratch. `RunIslandStage` borrows concrete stores, terrain/forces/buoyancy/time, contact/joint rows, diagnostics, and `PhysicsSleepStepPolicy`; `ApplyTransitions` receives only narrower phase operands. | `rg -n '\bPhysicsSleepIslandStageContext\b' SkullbonezSource SkullbonezTests` returns no rows. PB6 sleep/joint tests, physics/perf/full. |
| 17 | `PhysicsSleepWakeContext`, `PhysicsSleepController.h:112`. Constructed at `PhysicsWorld.cpp:625,648` and `PhysicsSleepController.cpp:493`; consumed across `PhysicsSleepController.Wake.cpp:121,185,216,279,362` and forwarded to three island operations at `:428-430`. | Split basic and force-refresh wake operations instead of nullable owner pointers and `applyForces`. Retain controller sleep/queue/island scratch. Preserve concrete `PhysicsNarrowphaseWakeAccess`, which enforces atomic one-winner wake behavior. | `rg -n '\bPhysicsSleepWakeContext\b' SkullbonezSource SkullbonezTests` returns no rows. PB6 wake/underwater/determinism gates. |
| 18 | `PhysicsBroadphaseStageContext`, `PhysicsBroadphaseStage.h:70`. Sole construction `PhysicsWorld.cpp:857`; sole consumer `PhysicsBroadphaseStage.cpp:505`; immediately repacked into `BroadphaseCandidateFilterContext` at `:508`. | Broadphase retains grid/candidate/key/membership/oracle storage and borrows concrete stores, settings, joints, sleep, diagnostics, `dt`, and profiler. Derive rows/count/awake/contact skin. | `rg -n '\bPhysicsBroadphaseStageContext\b' SkullbonezSource SkullbonezTests` returns no rows. PB5 broadphase, spatial-grid, determinism, physics/perf/full gates. |
| 19 | `ExternalForceBodyContext`, `ExternalForceStage.h:83`. Sole construction `PhysicsWorld.cpp:707`; sole consumer `ExternalForceStage.cpp:173`; worker lambda closes over it at `:191-274`. | Keep `ExternalForceFrameInput` as the real Gameplay value. The stage borrows stores, forces, concrete wake capability, sleep/underwater spans, execution policy, and worker pool. Constants move into the stage; retained lists remain fixed-capacity. | `rg -n '\bExternalForceBodyContext\b' SkullbonezSource SkullbonezTests` returns no rows. PB6 tornado witness, serial/parallel determinism, physics/perf/full gates. |
| 20 | `TerrainCandidateCommitContext`, `PhysicsTerrainStage.h:88`. Sole construction `PhysicsWorld.cpp:962`; reused for prepare `PhysicsTerrainStage.cpp:162` and commit `:215`, called at World `:982,1002`; commit uses only the two sleep spans. | Keep `PreparedTerrainCandidateCommit`. Prepare borrows stores, terrain, buoyancy, settings, profiler, body/time/sweep values; commit receives only prepared values and focused sleep outputs. | `rg -n '\bTerrainCandidateCommitContext\b' SkullbonezSource SkullbonezTests` returns no rows. PB6 terrain/determinism/byte-exact physics gates. |
| 21 | `ReplayRestoreOwnerContext`, `ReplayValidation.cpp:1357-1369`. Sole construction `:1889-1898`; consumers `:1371,1470,1522`; calls `:1499,1900,1944`; repacked into event and step contexts. | Replace the entire restore stack with a concrete stack-scoped phase transaction. It owns artifact/checkpoint/target/rollback/result/failure/cursor values; phase methods borrow concrete owners synchronously. | `rg -n '\bReplayRestoreOwnerContext\b' SkullbonezSource` returns no rows. PB4 cursor/failure tests, artifact, visual-fidelity, allocation-policy, full. |
| 22 | `ReplayRestoreStepContext`, `ReplayValidation.cpp:1044-1056`. Sole construction `:1538-1547`; sole consumer `StepReplayRestoreTarget`, `:1098-1231`; template callbacks carry capture/request behavior. | Same transaction as row 21. Step counters/results remain transaction values; concrete methods replace template/lambda callbacks. No owner reference or callback is stored. | `rg -n '\bReplayRestoreStepContext\b' SkullbonezSource` returns no rows. PB4 gates. |

## PB0-Added Repair Rows

These eight rows are binding additions to PB1-PB6. A grouped row names every
member and closes only when every named symbol is removed or transformed as
stated.

| Added row | Evidence | Binding endpoint / task |
|---:|---|---|
| A1 `EditorSaveHotkeyContext` | `EditorTools.h:91`; construction `InputRouter.Interactions.cpp:765`; consumer `EditorTools.cpp:433`. One switch combines SceneWorld/session save authority with unrelated capture authority. | Split concrete scene-save and screenshot operations. No combined owner bag. PB1. |
| A2 `UiTextPassState` | `RuntimeRenderPasses.h:401-435`; construction `OperatorEditorFrameComposer.cpp:557-583`; wrapped by row 9 at `:634-648`. Current evidence supersedes its older read-projection ruling: it is a 29-field caller projection across timers, UI, diagnostics, Replay, and render presentation and would preserve row 9 under another name. | Eliminate with row 9's output-responsibility phases. PB3. |
| A3 Runtime render graph callback payload family | Thirteen private structs at `RuntimeRenderer.cpp:121-265`: `CinematicPostGraphCallbackData`, `ShadowGraphCallbackData`, `ReflectionGraphCallbackData`, `ObjectGraphCallbackData`, `TerrainGraphCallbackData`, `WaterGraphCallbackData`, `DebugOverlayGraphCallbackData`, `SceneTargetGraphCallbackData`, `BackbufferAcquireGraphCallbackData`, `SkyboxGraphCallbackData`, `UiTextGraphCallbackData`, `ReplayGhostGraphCallbackData`, `DevelopmentUiGraphCallbackData`. Each is constructed in its graph builder and carries pointers into row 8 or the same multi-owner frame surface. | Remove the multi-owner callback packets while decomposing row 8. A render-graph ABI thunk may retain only a pointer to one concrete pass/phase owner plus cohesive stack values; it may not recreate the frame context. PB3. |
| A4 Replay restore support family | `ReplaySolverSampleRestoreContext`, `ReplayRestoreTransaction`, and `ReplayArtifactTopologyOwners`, `ReplayRestoreTransactions.h:57-101`; `ReplayRestoreEventContext`, `ReplayValidation.cpp:357`; `ReplaySceneTimelineResetOwners`, `ReplayCoordination.h:295`; restore template/callback seams at `ReplayValidation.cpp:1097,1247,1521,1568` and lambdas near `:1722,1730,1822,1916,1928,1940`. They store or repack owner references around rows 21-22 and the current `ReplayRestoreTransaction` has no cursor. | Transform `ReplayRestoreTransaction` into the real value-and-cursor owner and delete the other named owner bags/callback seams. Phase methods borrow owners. PB4. |
| A5 `BroadphaseCandidateFilterContext` | `SolverBroadphaseStage.h:49`; projected from row 18 at `PhysicsBroadphaseStage.cpp:508`; four test constructions in `TestSolverBroadphaseStage.cpp`. | Delete. `SpatialGrid`/filter helpers take seven focused values explicitly. PB5. |
| A6 `TerrainDetectionStageContext` | `PhysicsTerrainStage.h:73`; construction `PhysicsWorld.cpp:952` and `TestTerrain.cpp:159`; retained by the nested worker thunk. | Delete. Concrete `PhysicsTerrainStage::Detect` borrows stores/sleep/settings/clock directly; worker storage remains fixed and no-alloc. PB6. |
| A7 `ApplyForcesStageContext` | `PhysicsStageContexts.h:50`; construction `PhysicsWorld.cpp:823`; forwarding operator `PhysicsForceStage.cpp:149`; wrapped again by `ApplyForces` at `:442`. | Delete the forwarding aggregate. The concrete force stage accepts focused hot-path values directly. PB6. |
| A8 `IntegrateRemainingStageContext` | `PhysicsStageContexts.h:70`; construction `PhysicsWorld.cpp:1053`; forwarding operator `PhysicsForceStage.cpp:164`; consumed at `:472`. | Delete the forwarding aggregate. The concrete integration phase accepts focused values directly. PB6. |

## Existing Ceiling Defects Assigned To PB3

GV1 assigned these operations to PB0 because they exceed the binding
12-parameter ceiling. PB3 repairs them alongside rows 8-10:

| Operation | Evidence | Endpoint |
|---|---|---|
| `UiDrawSubmission::SubmitWithPreviews` | `UiDrawSubmission.cpp:260`, 13 parameters; sole caller `UiTextPass.cpp:1144`; forwards to `SubmitCommands`. | A concrete stack-scoped command-order transaction may retain only draw cursor, clip, and order values and borrow base/preview resources per phase. |
| `UiDrawSubmission::SubmitCommands` | `UiDrawSubmission.cpp:289`, 13 parameters; called by `Submit` and `SubmitWithPreviews`; preserves text/quad/preview interleaving. | Same command-order owner; exact interleaving remains tested and allocation-free. |
| `OperatorEditorFrameComposer::Render` | `OperatorEditorFrameComposer.cpp:218`, 13 parameters; caller `RunFrame.cpp:612`. | Split value composition from render submission using the real `OperatorEditorFrameView`; no replacement frame/service bag. |

## Explicit Retain Rulings

These reviewed shapes are not PB repair rows. A later task may challenge a
ruling only with new evidence and an owner decision.

| Shape/group | Ruling |
|---|---|
| `SceneDefaultsSaveView` | `retain-prior`. GV1 already ruled this one synchronous cold-save borrow at `SceneController.h:138`, construction `SceneRequestExecution.cpp:133`, consumer `SceneController.Load.cpp:1582`; no pointer survives reload. It is distinct from `SceneSaveView`. |
| `RuntimeRenderer::FrameEntryContext` | `retain-prior`. One narrowed synchronous render-call contract, construction `RunRender.cpp:192`, entry `RuntimeRenderer.cpp:2307`; it is not row 8's 45-field service bag. |
| Ten private `*GraphInputs` and the six non-UI `*PassInputs` | `retain-prior`. GV1 found each feeds a distinct concrete graph node/pass and does not converge on one apply transaction. `UiTextPassInputs` remains row 9. |
| `RenderResourceContext` | `retain`. Concrete DX12 resource-lifetime boundary; it does not combine frame policy/publications and is not a replacement for row 8. |
| `PrimitiveRenderContext` | `retain-prior`. Concrete hot render-command resource/value boundary, previously ruled across every consumer in `wide-signature-w1-rulings.md`. |
| `ReplayWorkspaceFrameInput` | `retain-prior`. Top-level value-only input-turn message; mutable owners stay explicit and synchronous. |
| `ReplayWorldPointerInput` | `retain-prior`. Focused Replay gesture/pick command value accepted in `wide-signature-w1-rulings.md:121`; mutable stores/owners remain explicit. |
| `EditorPointerPreviewInput`, `EditorPointerSelectionInput`, `EditorGizmoDragPointerInput`, `LauncherPointerInput` | `retain`. Small consumer-specific command values, not the union-of-consumer-needs bag. They carry no owner pointers and do not grant cross-domain authority. |
| `ReplayPathPickInput`, `RuntimePickRequest` | `retain`. Independently meaningful pick-request values with multiple domain consumers. |
| `ReplayStartupLoadInput` | `retain-prior`. Production startup boundary intentionally exposes only camera/interaction reset values and excludes solver/topology/diagnostic authority. |
| `ReplayOverlayStateView` | `retain`. Immutable Replay publication with independent Legacy and ImGui consumers. |
| `ExternalForceFrameInput`, `PreparedTerrainCandidateCommit`, `PhysicsSleepStepPolicy`, `PersistentContactSolverSideEffects` | `retain`. Cohesive Gameplay/frame, prepared-commit, policy, and bounded side-effect values respectively. |
| `PhysicsNarrowphaseWakeAccess` | `retain`. Concrete stack-only capability that enforces atomic one-winner wake behavior; no virtual dispatch or heap. |

## Task Mapping And Closure Inventory

- PB1: rows 1-4 and A1.
- PB2: rows 5-7.
- PB3: rows 8-10, A2-A3, and the three ceiling defects.
- PB4: rows 11-12, 21-22, and A4.
- PB5: rows 13-15, 18, and A5.
- PB6: rows 16-17, 19-20, and A6-A8.
- PB7: re-audit all 22 registered rows, all eight added rows, and all three
  ceiling operations; reconcile every retain ruling; complete the source
  comment audit and independent hostile review.

PB0 changed documentation only. Per the repository validation map, no
repository validation was required or run.
