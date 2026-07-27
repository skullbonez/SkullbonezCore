# Runtime Frame View Retirement FV0 Census

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan phase: `runtime-frame-view-retirement` FV0
Measured source: `a5dc7b84`

## Ruling

The ratified endpoint remains concrete operands. No frame transaction, service
bag, callback pack, `Run&` operand, inheritance, or virtual dispatch is needed.
The current one-site `Run::Execute` schedule already owns order by construction.

FV0 corrected two stale measurements in the originating plan:

- The four root views now expose 23 required owner references plus one optional
  shader-development capability. RB2 replaced the old backend view with required
  renderer/capture owners and the explicit optional shader capability.
- The live `Run::Execute` surface has twelve named phase helpers only when the
  Automation-only before-input phase is counted and the six direct/nested
  helpers are counted separately. FV0 therefore records both sets instead of
  silently omitting conditional or nested work.

## Root Capability Key

| Group | Current fields |
|---|---|
| Host (H) | `applicationExit`, `diagnosticsRuntime`, `assets`, `workerPool`, `window`, `profiler` |
| Interaction (I) | `inputRouter`, `interaction`, `attachedCamera`, `operatorUi`, `runtimeTools`, `camera` |
| Scene (S) | `config`, `launchOptions`, `startup`, `timers`, `overlays`, `simulation`, `sceneController` |
| Presentation (P) | `renderDefaults`, `validationHarness`, `renderer`, `backbufferCapture`; optional `shaderDevelopment` |

“Post-removal arity” below is the honest naïve signature: unique owner/capability
operands actually read by that operation plus its existing value operands. It
does not count a view as one and does not hide delegated reads.

## Twelve `Run::Execute` Phase Helpers

| Phase | Root owners/capabilities actually used | Direct non-view owners | Values | Post-removal arity |
|---|---|---|---:|---:|
| `BeginFrameDiagnosticsPhase` | P: `renderer` | — | 0 | 1 |
| `RunAutomationBeforeInputPhase` | H: `applicationExit`, `window`; I: `inputRouter`, `interaction`, `operatorUi`, `runtimeTools`, `camera`; S: `config`, `timers`, `sceneController`; P: `renderer` | `interactionAutomation`, `replayRuntime`, `imguiEditor` | 0 | 14 |
| `RunInputPhase` / `ProcessInputFrame` | all 23 required roots plus optional `shaderDevelopment` | `replayRuntime`, `imguiEditor`, `interactionAutomation` | automation result pointer | 28 |
| `RunSimulationPhase` | H: `diagnosticsRuntime`, `workerPool`; I: `camera`; S: `config`, `timers`, `overlays`, `sceneController`; P: `validationHarness` | `replayRuntime`, `interactionAutomation` | seconds, proceed policy | 12 |
| `PrepareRenderPhase` / graphics stress | all H; all I; all S; P: `renderDefaults`, `validationHarness`, `renderer` | `replayRuntime` | legacy-UI flag, simulation result | 25 |
| `PublishRenderModelsPhase` | H: `workerPool`; S: `config`, `sceneController` | — | 0 | 3 |
| `RenderWorldPhase` | H: `profiler`; P: `renderer` | — | model view, alpha | 4 |
| `RenderOperatorUiPhase` | all H; all I; S: `config`, `launchOptions`, `timers`, `overlays`, `sceneController`; P: `renderer` | `replayRuntime`, `imguiEditor`, `tracyClientOwner` | model view, presentation facts | 23 |
| `RunPostDrawDiagnosticsPhase` | H: `applicationExit`, `diagnosticsRuntime`, `profiler`; I: `inputRouter`, `interaction`, `operatorUi`, `runtimeTools`, `camera`; S: `sceneController`; P: `validationHarness`, `renderer`, `backbufferCapture` | `interactionAutomation`, `replayRuntime`, `imguiEditor` | legacy-UI flag | 16 |
| `FinishFrameWorkPhase` | H: `profiler`; S: `timers` | — | proceed policy | 3 |
| `PresentFramePhase` | H: `applicationExit`, `profiler`; S: `timers`; P: `renderer` | — | 0 | 4 |
| `CompleteFramePhase` | H: `diagnosticsRuntime`; S: `timers`, `sceneController` | — | proceed policy | 4 |

The five wide rows are not permission to keep a view. Their decomposition is
binding for FV1/FV2:

- Automation before input splits into result production (12 operands) and result
  application (at most 10).
- Input splits along the existing capture/routing, Legacy UI begin, UI command,
  scene-load, shader-reload, and pointer-finalization boundaries. Scene-load
  execution retains the existing maximum: `Load` 12, runtime reactions 10, and
  presentation outputs 7.
- Graphics stress splits into frame planning, scene-load execution (12/10/7),
  render/descriptor churn, and the four-operand pipeline-sync phase. The current
  all-surface `ExecuteGraphicsStressFrame` wrapper does not survive.
- Operator UI splits into UI-text fact selection, editor/world command
  composition, Legacy submission, ImGui submission, and replay-overlay work.
  `OperatorEditorFrameComposer::Render` is included in this split rather than
  receiving renamed views.
- Post-draw work splits into render/capture diagnostics and Automation result
  application; each side is at or below 11 operands.

## Six Direct/Nested Helpers Named By The Plan

| Helper | Owners reached directly today | Values | Naïve arity | Binding decomposition |
|---|---|---:|---:|---|
| `TickPhysics` | `assets`, `camera`, `config`, `diagnosticsRuntime`, `inputRouter`, `interaction`, `launchOptions`, `operatorUi`, `profiler`, `renderDefaults`, `replayRuntime`, `runtimeTools`, `sceneController`, `simulation`, `workerPool` | seconds, capture pin, proceed policy | 18 | simulation planning; fixed-step execution; post-step hook; presentation/director update |
| `UpdateLogic` | `assets`, `attachedCamera`, `camera`, `config`, `inputRouter`, `launchOptions`, `operatorUi`, `renderDefaults`, `replayRuntime`, `runtimeTools`, `sceneController` | simulation dt, camera dt, alpha | 14 | camera controls (8); director playback (9); fluid adjustment (3) |
| `AfterPhysicsStep` | `applicationExit`, `assets`, `attachedCamera`, `camera`, `config`, `diagnosticsRuntime`, `inputRouter`, `interaction`, `launchOptions`, `operatorUi`, `overlays`, `profiler`, `replayRuntime`, `runtimeTools`, `sceneController` | 0 | 15 | restore/capture hook (7); Debug probe input (at most 10); probe-result application (4) |
| `TickScreenshots` | `applicationExit`, `assets`, `attachedCamera`, `backbufferCapture`, `camera`, `config`, `diagnosticsRuntime`, `inputRouter`, `interaction`, `launchOptions`, `operatorUi`, `overlays`, `profiler`, `renderDefaults`, `renderer`, `replayRuntime`, `runtimeTools`, `sceneController`, `startup`, `timers`, `validationHarness`, `window`, `workerPool` | proceed policy | 24 | capture evaluation (8); scene-load `Load`/reactions/presentation (12/10/7); automation conclusion (5) |
| `TickAutoCycle` | `applicationExit`, `backbufferCapture`, `camera`, `diagnosticsRuntime`, `renderer`, `sceneController` | proceed policy | 7 | none required |
| `TickSceneAdvance` | `assets`, `attachedCamera`, `camera`, `config`, `diagnosticsRuntime`, `inputRouter`, `interaction`, `launchOptions`, `operatorUi`, `overlays`, `renderDefaults`, `renderer`, `replayRuntime`, `runtimeTools`, `sceneController`, `startup`, `timers`, `validationHarness`, `window`, `workerPool` | proceed policy | 21 | advance decision (8); scene-load `Load`/reactions/presentation (12/10/7); completion application (4) |

`TickScreenshots` has 23 owners plus its value operand, hence arity 24. The
table deliberately treats `Renderer()` and `BackbufferCapture()` as their
concrete owners instead of concealing them as zero-argument member access.

## Complete View Blast Radius

FV2 must update more than the two files named by the originating review. Current
source has view-bearing declarations or definitions in:

- `Runtime/App/Run.*`
- `Runtime/App/InputFrame*`
- `Runtime/App/InputRouter.Interactions.cpp` and `Runtime/Input/InputRouter.h`
- `Runtime/Automation/InteractionAutomationController.*`
- `Runtime/Capture/RuntimeStressController.*`
- `Runtime/UI/OperatorEditorFrameComposer.*`
- `Runtime/RuntimeFrameViews.h`

This is 21 helper consumers in addition to the four root constructors, matching
the prior OF4 consumer census after accounting for the RB2 presentation-field
change. FV1 begins with the six direct helpers above; FV2 then converts every
remaining view-bearing row and deletes the four root types.

## Acceptance

- All twelve top-level phase helpers and all six plan-named direct helpers have
  a current owner/value census.
- The five wide top-level rows and five wide direct helpers have named
  decompositions whose individual operations are at or below 12 parameters.
- No aggregate is proposed to shorten a signature.
- The current maximum of 28 is an admitted pre-implementation defect, not a
  retained endpoint.

FV1 is unblocked.
