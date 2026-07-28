# Replay Restore And Wide-Signature RG0 Census

Date: 2026-07-28
Branch: `nightrunner-28th-JUL-26`
Phase: RG0 — fresh qualitative review of exact-12 operations

## Inventory Result

`python tools/inventory_wide_signatures.py --self-test` passes. The repository
scan reports:

- 407 signatures with arity 7 or greater;
- 33 signatures with arity exactly 12;
- zero signatures over 12; and
- maximum arity 12.

The 33 rows below were reviewed from current source. No prior `Keep` or
`Migrated` disposition was accepted as evidence. Exact arity is a trigger for
the ownership questions, not an allowance and not an automatic defect.

## Fresh Ruling Vocabulary

- **Retain-owner:** the operation is on, or is the deliberate entry seam to, a
  concrete owner. Its parameters are synchronous participants/facts for one
  named operation. It introduces no courier, capability-slice set, callback
  pack, retained reach-back, or immediate aggregate destructuring.
- **Repair-RG2:** the Replay restore path spans topology, artifact selection,
  scene mutation, and completion/diagnostics authority. RG2 must move those
  responsibilities to focused operations while retaining
  `ReplayRestoreTransaction` only for its tested phase/arbitration invariant.

## Exact-12 Review

| # | Operation | Fresh ruling | Ownership reason |
|---:|---|---|---|
| 1 | `PhysicsContactSolverStage::Solve` | Retain-owner | Contact solving is one stage operation; body/collider state, candidate/contact scratch, sleep outputs, diagnostics, time step, and profiler expire with the call. No participant slice or courier is introduced. |
| 2 | `PhysicsForceStage::ApplyForces` | Retain-owner | The force stage owns force application and parallel arbitration. Inputs are the force facts, awake rows, stores, and execution policy for that one stage turn. |
| 3 | `PhysicsNarrowphaseStage::ProcessObjectNarrowphasePair` | Retain-owner | One narrowphase pair operation owns contact/wake event production. Pair index, event output, stores, policy, and scratch are pair-scoped. |
| 4 | `Dx12GeometryOwner::DrawColoredTrianglesFromBuffer` | Retain-owner | One geometry owner records one transient draw. Layout/style, viewport, buffer address, command list, draw gate, diagnostics, and raster state are one command-recording operation. |
| 5 | `Dx12GeometryOwner::CreateInstancedMesh` | Retain-owner | The low-level overload performs one mesh upload/construction from layout, device, command-list, upload-resource, address, and pointer facts. Its public overload derives those capabilities synchronously. |
| 6 | `RenderModelsForView` | Retain-owner | The compile-time visibility policy and passed render values drive one model submission pass. The helper performs the work and does not unpack a courier. |
| 7 | `RenderInstanceRenderer::RenderModels` | Retain-owner | This is the stable main-view entry seam to the compile-time implementation. It does not collect or retain authority, and the view specialization is a real policy boundary. |
| 8 | `Text2d::BatchTriangle` | Retain-owner | One text batch appends one coloured triangle. Coordinates and colour channels are primitive vertex values, not unrelated owners or hidden state. |
| 9 | `BeginRuntimeUIFrame` | Retain-owner | `Runtime/App` is the composition root and this operation establishes one post-UI frame order before keyboard commands. The returned value packet closes the call. |
| 10 | `InputRouter::RouteRuntimePointer` | Retain-owner | `InputRouter` is the repository’s sole retained input-routing owner. Pointer facts and participating owners are borrowed synchronously for one route decision. The stale prior “migrated to 6” label is rejected. |
| 11 | `InputRouter::DispatchAfterUiDismiss` | Retain-owner | The router owns after-UI command order and quick-exit arbitration. The stale prior “migrated to 6” label is rejected; the visible operation receives no bag or slice set. |
| 12 | `ReplayRuntime::ApplyLiveRestoreRequest` | **Repair-RG2** | Scene mutation, artifact selection, simulation/config/assets, UI overrides, generated-type policy, and the transaction cross concrete restore responsibilities. |
| 13 | `ReplayRuntime::CompleteLiveRestoreRequest` | **Repair-RG2** | Completion currently combines transaction advance, scene/diagnostics publication, input/interaction cleanup, and camera policy. Those decisions need focused owners/operations. |
| 14 | `ReplayRuntime::TickScrubberInput` | Retain-owner | Replay owns scrubber input arbitration. Scalars are immediate UI/frame facts; router, interaction, camera, and output borrows expire at return. |
| 15 | `StepReplayRestoreTarget` | **Repair-RG2** | One helper combines transaction phase, scene/world mutation, artifact/checkpoint/target selection, tools/assets/workers, capacity, and diagnostics. |
| 16 | `RebuildReplayGeneratedSceneTopology` | **Repair-RG2** | Generated topology rebuild, simulation/config, scene/UI/type overrides, event/checkpoint inputs, and diagnostic buffer ownership are currently fused. |
| 17 | `ReplayRuntime::RestoreV2ArtifactTargetState` | **Repair-RG2** | The prior same-class pure forwarder is gone, but the surviving operation still receives the full multi-owner restore surface. The prior `Keep` disposition is rejected. |
| 18 | `ReplayRuntime::TickProbes` | Retain-owner | Replay probe orchestration is one diagnostic phase at the App composition level. Timeline reset is already a value input; live owner borrows expire synchronously. |
| 19 | `ReplayRuntime::RunStartupProbeWorkflows` | Retain-owner | Startup probe execution is one Replay startup phase; the startup/load values and concrete composition owners are not retained or repackaged. |
| 20 | `EvaluateInteractionAutomationAssertion` | Retain-owner | Automation owns assertion evaluation. The action and detached views identify one assertion; live owners are inspected/applied synchronously. The prior `Keep` text is not reused. |
| 21 | `TickInteractionAutomationBeforeInput` | Retain-owner | Automation owns this named before-input phase and its ordering relative to input. Participating owners are the concrete composition surface for that phase. |
| 22 | `TickInteractionAutomationAfterRender` | Retain-owner | Automation owns this named after-render phase, including capture evidence. No cross-phase owner pointer or services aggregate is retained. |
| 23 | `RuntimeTools::RouteMousePickupPointer` | Retain-owner | `RuntimeTools` owns the mouse-pick gesture and retained drag plane. Rays/camera values are frame facts; world/router/interaction borrows expire at return. |
| 24 | `RenderReplayScrubberOverlay` | Retain-owner | Planning presentation converts one detached Replay view plus render/UI owners into one overlay submission. It does not mutate Replay state or retain owners. |
| 25 | `ReplayPrediction::BeginFrameSource` | Retain-owner | Prediction owns source selection/calibration for one build frame. Source facts and result output belong to that phase and are not a courier workaround. |
| 26 | `ShadowPass::RenderShadowMap` | Retain-owner | `ShadowPass` owns one shadow-map pass. Render resources, stores, optional worker policy, and caster batches are synchronous pass participants. |
| 27 | `ReplayAuthoring::TickCauseTreeInput` | Retain-owner | Replay authoring owns cause-tree input. Mutable owners and output references are one input-turn arbitration surface, not retained services. |
| 28 | `ReplayAuthoring::TickVelocityEditInput` | Retain-owner | Replay authoring owns velocity-edit input and its physics/interaction arbitration. Outputs are explicit decisions for the same turn. |
| 29 | `ReplaySolverRecorder::CaptureFrame` | Retain-owner | The recorder owns one frame capture. Branch/event/time samples and source stores are one immutable capture snapshot consumed before return. |
| 30 | `SceneLoadTransaction::Load` | Retain-owner | The transaction enforces scene-load phase order. Request/startup/config values and concrete load owners participate in that tested invariant. |
| 31 | `SceneController::Load` | Retain-owner | This is the controller’s public composition seam into the invariant-owning load transaction. The transaction is explicit and no replacement bag is formed. |
| 32 | `InGameUI::UpdateInput` | Retain-owner | `InGameUI` is the stable UI entry seam. Input, viewport, editor/camera facts, and scene options form one UI turn and are delegated to the concrete window-interaction owner. |
| 33 | `UIWindowInteractionOwner::UpdateInput` | Retain-owner | The interaction owner applies one UI input turn. Scalars are presentation facts and the scene-option span is a detached value view. |

Fresh totals:

- Retain-owner: 28
- Repair-RG2: 5
- Unreviewed: 0

## Cross-Row Findings

1. No exact-12 row receives every member of a capability-slice family.
2. No row justifies introducing a courier or broad `*Context`; every retain
   ruling names the concrete owner/phase or value operation.
3. The three thin public entry seams (`RenderInstanceRenderer::RenderModels`,
   `SceneController::Load`, and `InGameUI::UpdateInput`) are visible delegation
   boundaries, not aliases that claim to own the underlying work.
4. The five repair rows form one Replay restore/topology cluster and are owned
   by RG2. Splitting them independently would risk replacing one wide surface
   with another.
5. No lower numeric budget is introduced. RG1 must make a missing qualitative
   ruling fail while allowing a reviewer to disagree with a written ruling.

## Validation

This phase changes documentation only. The wide-signature self-test and current
repository scan pass; no repository build or runtime validation is required.
