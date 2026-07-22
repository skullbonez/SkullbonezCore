# Owner Fan-Out Reduction OF4 — Frame-View Diet

Date: 2026-07-22
Branch: `nightrunner`

## Result

The frame-view call surface fell from 25 helpers / 68 view-parameter slots to
21 helpers / 36 slots. Four helpers no longer receive a view at all, and
single- or two-owner slices now use direct parameters. The four root views keep
their 6 / 6 / 7 / 4 fields because every field still has at least one real
consumer; no root view reached the two-field deletion threshold.

`RuntimeUiTextFrameFacts` is now a copyable value record. Its borrowed
`RuntimeInteractionGesture&` became the two enum facts the composer actually
reads, and mutable `OperatorEditorFrameView&` output is an explicit `Render`
parameter. The two label pointers remain stable vocabulary pointers returned by
their owners; neither receiving UI surface copies label bytes.

## Final Consumer Matrix

Names inside braces are the only fields read from that view by the helper.
Delegated reads are called out where a helper forwards the same view to another
row. Direct parameters introduced by OF4 follow the semicolon.

| Helper | Host view | Interaction view | Scene view | Presentation view / direct diet |
|---|---|---|---|---|
| `ProcessInputFrame` | all 6 | all 6 | all 7 | all 4 |
| `BeginRuntimeUIFrame` | — | all 6 | — | direct `Window`, timers, scene controller |
| `ApplyRuntimeUIFrameCommands` | `{assets, workerPool, window, profiler}` | all 6 | `{config, launchOptions, timers, overlays, simulation, sceneController}` | `{renderDefaults, renderBackendView, renderer}` |
| `FinishRuntimeUIFramePointer` | — | all 6 | — | direct scene controller |
| `InputRouter::ApplyInteractionTransitionCleanup` | — | `{runtimeTools, interaction, camera, attachedCamera}` | — | direct scene controller |
| `InputRouter::ApplyInteractionTransition` | — | `{interaction}` plus cleanup delegation | — | direct scene controller |
| `InputRouter::SetWorldInteractionOwner` | — | `{interaction}` plus cleanup delegation | — | direct scene controller |
| `InputRouter::RecordModeAction` | — | `{camera, runtimeTools, interaction, attachedCamera}` | — | — |
| `InputRouter::RouteRuntimePointer` | — | `{runtimeTools, attachedCamera, interaction, camera}` | — | direct assets and scene controller |
| `InputRouter::ApplyCameraMode` | — | `{camera, interaction, runtimeTools, attachedCamera}` | — | direct scene controller |
| `InputRouter::CycleCameraMode` | — | `{camera, attachedCamera}` plus `ApplyCameraMode` delegation | — | direct scene controller |
| `InputRouter::HandleUnfocusedFrame` | — | `{interaction, runtimeTools, attachedCamera, camera, operatorUi}` | — | direct scene controller |
| `InputRouter::DispatchCaptureActions` | — | `{camera, attachedCamera, operatorUi}` | — | direct diagnostics and scene controller |
| `InputRouter::DispatchAfterUiDismiss` | — | `{camera, attachedCamera, runtimeTools, operatorUi}` | — | direct diagnostics, scene controller, overlays |
| `TickInteractionAutomationBeforeInput` | — | `{camera, inputRouter, interaction, runtimeTools, operatorUi}` | `{config, timers, sceneController}` | direct window |
| `TickInteractionAutomationAfterRender` | — | `{runtimeTools, interaction, inputRouter, camera, operatorUi}` | — | direct scene controller |
| `ApplyUIStressAction` | — | — | `{overlays, sceneController, timers, simulation}` | direct UI, render backend, renderer |
| `ApplyGraphicsStressAction` | — | `{camera, operatorUi, runtimeTools}` | `{launchOptions, config, overlays, sceneController, timers, simulation}` | direct assets, render defaults, renderer |
| `RunUIStressActions` | `{diagnosticsRuntime, window, profiler}` | all 6 | `{timers, sceneController, config, simulation, launchOptions}` | direct render backend and renderer |
| `RuntimeValidationHarness::ExecuteGraphicsStressFrame` | `{window, diagnosticsRuntime, assets, workerPool}` | all 6 | all 7 | all 4 |
| `OperatorEditorFrameComposer::Render` | `{diagnosticsRuntime, assets, workerPool, window, profiler}` | `{inputRouter, operatorUi, runtimeTools, camera}` | `{config, launchOptions, timers, overlays, sceneController}` | direct renderer |

## Removed View Edges

- `Run::TickPhysics` and `Run::AfterPhysicsStep` no longer accept frame views;
  the composition owner invokes their fixed post-step dependencies directly.
- `CaptureReplayPostStep` receives the four concrete replay-capture inputs it
  reads instead of interaction and scene capability slices.
- `InputRouter::RouteEditorPointer` receives assets, runtime tools,
  interaction, and the scene controller directly.
- Host-view one-field uses in UI sampling and automation became direct window
  parameters.
- Scene-view one- or two-field uses in UI finishing, automation-after-render,
  and input-router helpers became direct owner parameters.
- Presentation-view one- or two-field uses in operator composition and UI
  stress became direct renderer/backend parameters.

## Root-View Retention Proof

The final source still reads every root field at least once:

- Host: `applicationExit`, `diagnosticsRuntime`, `assets`, `workerPool`,
  `window`, `profiler`.
- Interaction: `inputRouter`, `interaction`, `attachedCamera`, `operatorUi`,
  `runtimeTools`, `camera`.
- Scene: `config`, `launchOptions`, `startup`, `timers`, `overlays`,
  `simulation`, `sceneController`.
- Presentation: `renderDefaults`, `validationHarness`, `renderBackendView`,
  `renderer`.

Deleting any retained field would move a real multi-owner frame operation back
to `Run` or create a second context spelling. The retained root views remain
non-owning, stack-only, synchronous borrow maps; OF4 removed narrow uses rather
than replacing them with another broad bag.
