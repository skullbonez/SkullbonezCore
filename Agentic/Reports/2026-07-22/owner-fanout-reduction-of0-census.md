# Owner Fan-Out Reduction OF0 Census

Date: 2026-07-22
Branch: `nightrunner`
Scope: `SceneController::Load`, `SceneController::ExecutePending`,
`ApplySceneLoadConsumerOutputs`, the four `RuntimeFrame*View` records, and
`RuntimeUiTextFrameFacts`

## Method And Classification

CodeGraph mapped the load/request/frame-view call paths first. Targeted source
reads then confirmed each direct owner field and every load-time operation.
Classification is relative to scene lifecycle:

- **T (transactional):** the load cannot decide, drain, parse, populate, or
  rebuild the new scene without this owner/value during the transaction.
- **R (reactive):** the owner only resets, publishes, or applies consequences of
  a lifecycle generation and can do that at its established frame entry.
- **N (not lifecycle):** a frame-only process owner/value that must not be added
  to the lifecycle protocol.

`SceneController` and its owned `SceneWorld`/`PhysicsEngine` are the transaction
root and are not counted among the 18 borrowed participants.

## Current Load And ExecutePending Graph

The four participant records declare 18 borrowed owners at
`SceneController.h:142-175`; four excluded consumers are applied through values
at `RunScene.cpp:569-600`. `ExecutePending` adds navigation/save/replay work
before or after `Load` at `SceneRequestExecution.cpp:51-164`.

| Current owner/value | Class | Evidence and current operation | Target |
|---|---|---|---|
| `EngineConfig` | T | `RunScene.cpp:613,730,789-790`; supplies capacity, runtime config, authored style/physics policy | Retain in policy inputs |
| `RunLaunchOptions` | T | `RunScene.cpp:614,690,791-794,1162-1276`; launch overrides affect population and activation | Retain in policy inputs |
| default cinematic config | T value | `RunScene.cpp:615,855-864`; baseline used while applying generated/demo style | Retain as const policy value |
| `RunStartupState` | T value | `RunScene.cpp:616,788-790`; startup capacity and worker count seed generated loads | Retain as const policy value |
| `AssetSystem` | T | `RunScene.cpp:617,798-806,875-876`; resolves terrain and authored scene assets | Retain |
| `WorkerPool` | T | `RunScene.cpp:618,790`; applies authored/startup worker setting before population | Retain |
| `RunTimerState` | R | `RunScene.cpp:764,1236,1253,1310-1311`; measurement reset, UI timestamps, activation restart | Observe `AfterSceneCleared`/`AfterSceneActivated`; OF1 pilot |
| `DiagnosticsRuntime` | R | `RunScene.cpp:708,731,902,952-962,1095-1101,1233-1252,1295-1302`; resets and projects the activated scene into diagnostics | Observe lifecycle and read final scene/config at frame entry; remove from load participants |
| `SimulationSystem` | R | `RunScene.cpp:733`; only pacing reset | Observe `AfterSceneCleared` |
| `InputRouter` | R | `RunScene.cpp:741,1139-1144,1320-1324`; clears pickup/cursor/input deltas and participates in replay reset | Observe generation through interaction/frame entry |
| `RuntimeInteractionController` | R | `RunScene.cpp:741,754-757,1129-1130,1320-1323`; clears workspace and selects inspect state | Observe generation; derive scene mode from final packet |
| `RunCameraState` | R | `RunScene.cpp:688,759,1108-1110,1130-1143,1325`; reset plus authored activation values | Observe generation and apply final scene-derived camera snapshot |
| `AttachedCameraState` | R | `RunScene.cpp:742,751,1327`; reset/follow presentation only | Observe generation through `AttachedCameraController` |
| `RuntimeTools` | R | `RunScene.cpp:741,754-760`; clears pickup/editor/history/raycast state; `SceneRequestExecution.cpp:133` marks a successful save clean | Load reset moves to ledger; save-clean remains an explicit completed-save command |
| `ReplayRuntime` | R plus completed-command output | `RunScene.cpp:744-753,827-832,1050,1312-1329`; interaction/timeline reset is reactive, while `SceneRequestExecution.cpp:139-154` records only accepted owner work | Move reset/presentation to ledger; return completed replay commands as typed values to the caller |
| `RuntimeOverlayDiagnostics` | R | `RunScene.cpp:637-638,762`; presentation edit/reset; `SceneRequestExecution.cpp:115` snapshots state for save | Observe generation; save keeps a detached value snapshot; OF1 pilot |
| `RuntimeRenderBackendView` | T subset | `RunScene.cpp:689,805-806,989-1013`; only frame drain and cold resource builder transact. Name/vsync reads at `867,1286,1305-1307` are post-load projections | Replace whole view with exact frame/resource borrows; move renderer-name/vsync publication outside `Load` |
| `RuntimeRenderer` | T | `RunScene.cpp:686,735,909-913,1153-1159,1331-1332`; resource release/style restoration/raytracing warmup | Retain |

### Excluded output consumers

| Owner | Class | Current effect | Target |
|---|---|---|---|
| `Window` | R | Title value applied at `RunScene.cpp:581-584` | Keep title as a synchronous value output |
| `InGameUI` | R | Navigation, browser refresh, activation at `RunScene.cpp:585-596` | Keep navigation/browser/activation value outputs; these are expected survivors |
| `RuntimeValidationHarness` | R | Automation gates and graphics-stress resume at `RunScene.cpp:577-580,597-600` | Observe activated generation and derive gate/stress state; remove covered outputs in OF3 |
| `RunLaunchOptions` | T policy | Read only while applying stress resume at `RunScene.cpp:597-600` | Already retained policy input; no second owner edge |

## Frame-View Reachability Census

The four views are declared at `RuntimeFrameViews.h:84-184`, constructed once in
`RunFrame.cpp:253-276`, and consumed synchronously by input, UI, stress,
automation, and post-step helpers. This table records lifecycle classification,
not the per-helper field diet reserved for OF4.

| View | Field owners | Lifecycle classification |
|---|---|---|
| `RuntimeFrameHostView` | `ApplicationExitState`, `DiagnosticsRuntime`, `AssetSystem`, `WorkerPool`, `Window`, optional `Profiler` | N, R, T, T, R, N |
| `RuntimeFrameInteractionView` | `InputRouter`, `RuntimeInteractionController`, `AttachedCameraController`, `InGameUI`, `RuntimeTools`, `RunCameraState` | all R |
| `RuntimeFrameSceneView` | `EngineConfig`, `RunLaunchOptions`, `RunStartupState`, `RunTimerState`, `RuntimeOverlayDiagnostics`, `SimulationSystem`, `SceneController` | T, T, T, R, R, R, T |
| `RuntimeFramePresentationView` | `RenderDefaultsStore`, `RuntimeValidationHarness`, `RuntimeRenderBackendView`, `RuntimeRenderer` | T policy source, R, T subset, T |

`RuntimeUiTextFrameFacts` at `RuntimeFrameViews.h:189-223` contains no lifecycle
owner: camera mask/label, launcher label, launcher-mode flag, interaction
gesture, presentation alpha/pin, frame seconds, UI-surface flag, and the
frame-local operator editor output view. All are N for lifecycle. OF4 will
convert borrowed facts to copied values where the consumer already copies and
will inline any view reduced to two fields.

## Target Participant Boundary

After OF1-OF3, the scene transaction retains these nine external
owners/borrows, plus the internally owned `SceneController`/`SceneWorld`:

1. `EngineConfig`.
2. `RunLaunchOptions`.
3. Default cinematic policy value.
4. `RunStartupState` policy value.
5. `AssetSystem`.
6. `WorkerPool`.
7. `RuntimeRenderer`.
8. `Dx12FrameOwner` for the mandatory pre-mutation drain.
9. `Dx12ResourceBuilder` for cold terrain/scene GPU resource construction.

There is no host, interaction, or replay owner participant record in the target.
The remaining consumer outputs are typed values for window title, UI
navigation/browser/activation, and accepted replay owner commands. Renderer
name, vsync application, diagnostics configuration, automation gates, and
stress resume move to the corresponding owner's post-activation frame entry.

## Target Lifecycle Packet

`SceneLifecyclePacket` is a fixed value owned by `SceneController`; no callbacks,
subscriber list, owner pointers, or allocation are permitted. OF1 should carry:

| Field | Purpose |
|---|---|
| `uint64_t generation` | Monotonic accepted load-attempt identity; changes once before the first destructive phase |
| `SceneRuntimeLifecycleEvent event` | Existing ordered phase: before unload, after clear, before/after populate, after activation |
| `bool preserveUiState` | Lets UI/overlay owners retain operator presentation only when requested |
| `bool preserveRuntimeState` | Lets camera/replay/tool owners distinguish restore from fresh activation |
| `bool suppressExitOnComplete` | Preserves reset/automation exit semantics without borrowing launch state |
| `bool enterInteractiveRun` | Lets interaction/UI consumers apply the accepted interactive transition once |
| `bool manualReset` | Distinguishes explicit reset from navigation/load for owner diagnostics and replay values |
| `int sceneIndex` | Stable selected index for activation consumers; `-1` when unavailable |
| `bool sceneMode` | Final generated-vs-authored mode, meaningful from `AfterScenePopulate` onward |

Each reactive owner stores its last applied generation/phase and consumes only
at fixed existing frame-order points. A failed load that reaches destructive
work still publishes its generation and last completed phase; a failure before
that boundary publishes no new generation. This is the contract OF1 tests must
pin.

## Automation Decision

`InteractionAutomationController` does **not** join the ledger. Its
Automation-only entry points consume the existing frame views
(`InteractionAutomationController.cpp:2675-2677,3093-3094`) and own no
scene-local state that `Load` resets. Adding a macro-only consumer bit would
make lifecycle shape build-dependent. Automation continues to drive explicit
input/validation commands; the underlying timers, overlays, interaction, and
validation owners observe the same generation as production builds.

## OF0 Result

Current direct load graph: 18 borrowed owners plus four excluded consumers.
Target direct transaction graph: nine external owners/borrows plus the internal
scene root, with reactive resets moved to a fixed value ledger. Documentation
only; no repository validation required.
