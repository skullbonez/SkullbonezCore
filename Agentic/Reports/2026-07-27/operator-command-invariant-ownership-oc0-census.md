# Operator Command Invariant Ownership — OC0 Census

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan: 9, OC0
Result: COMPLETE

## Finding

The ordering invariant is real and spans the whole accepted UI-command turn.
`InputFrame.cpp:865-1246` currently supplies the only executable specification:
seventeen free command entry points, six result records plus one world-change
record, inline Boolean acceptance results, and several owner-specific operations
are interleaved by hand. No type owns that order or the accepted-action ledger.

CA1 already removed the eight context inputs. OC1 must now install one
non-copyable `OperatorCommandTransaction`; parameter reshuffling is not an
authorized outcome.

## Canonical Command Precondition

Before application, legacy fields are normalized into typed bounded queues,
human and injected queues are arbitrated, and committed commands are projected
back into the narrow owner packets:

1. exact duplicate identities and payloads coalesce;
2. the same identity with a conflicting payload returns Lane R;
3. preview edits never reach mutation;
4. only the canonical committed packet reaches the transaction.

This happens at `InputFrame.cpp:649-669`. The transaction does not replace that
UI-boundary arbitration; it owns the later mutation order and acceptance ledger.

## Binding Phase Specification

OC1's phase cursor is:

```text
Idle
  -> DeviceAndMode
  -> PhysicsControl
  -> RuntimePresentation
  -> SimulationPolicy
  -> PhysicsMaterial
  -> WorldPolicy
  -> CinematicPolicy
  -> Complete
```

Every edge is adjacent-only and exactly once. Skipping, repeating, or reversing
an edge is Lane F. Each phase borrows only the concrete owners used during that
call and retains none.

| Phase | Current ordered operations | Existing barriers that remain in place |
|---|---|---|
| `DeviceAndMode` | vsync apply; camera request decode and `InputRouter::ApplyCameraMode` | editor pre-mode, editor mode, editor post-mode, hierarchy/edit actions, scene pause/step |
| `PhysicsControl` | sleep-policy toggle; tornado enable, visual-shell, field-vector, then five value requests | diagnostics overlay precedes sleep; raycast and terrain-probe diagnostics follow tornado |
| `RuntimePresentation` | text-only; fixed-step toggle/reset; terrain, water, shadow, defaults, ordinary tuning, reflection presentation | replay-memory policy follows the phase |
| `SimulationPolicy` | time scale; seed plus RNG-state reset; worker-pool reconfiguration | physics-debug values and launcher tuning follow |
| `PhysicsMaterial` | terrain, object, then rolling friction; apply the completed config once to `SceneWorld` | existing GV3 model-count, solver-ball, then solver-box transactions follow |
| `WorldPolicy` | partial gravity/fluid-height/fluid-density request, applied atomically from one pre-change snapshot | replay receives the returned before/after value |
| `CinematicPolicy` | master rendering toggle; queue cinematic save; mode selection; feature toggle; parameter edit | `SceneController::SubmitUIRequests` remains the terminal operation after `Complete` |

The interleaved owner operations are not absorbed into a new god object.
`OperatorCommandTransaction` owns only phase order, arbitration decisions,
command values, and the acceptance ledger. Existing concrete owners and GV3
retain their mutations.

## Operation And Destination Census

| Current operation | State mutated / decision | OC1–OC2 destination |
|---|---|---|
| `ApplyRenderVsyncUICommand` | `RuntimeRenderer` and optional `Dx12RenderDevice` vsync | `DeviceAndMode` phase |
| `DecodeRunCameraModeUICommand` | validates a value; `InputRouter` later owns mode transition/cleanup | `DeviceAndMode` phase, then call the existing router owner |
| `ApplyPhysicsSleepPolicyUICommand` | `SceneWorld::Physics` sleep policy | `PhysicsControl` phase |
| `ApplyTornadoUICommands` | `SceneWorld::Tornado` enable/visual/field values | `PhysicsControl` phase |
| `ApplyRuntimeTextOnlyUICommand` | `OverlayDebugState::isTextOnly` | `RuntimePresentation` phase |
| `ApplySceneFixedStepUICommand` | session fixed-step policy, then `SimulationSystem::Reset` | `RuntimePresentation` phase |
| `ApplyRuntimePresentationUICommands` | debug presentation, ordinary/cinematic shadow state, render-save intent, ordinary tuning, water-reflection state | `RuntimePresentation` phase |
| `ApplyRunSimulationUICommands` | session/UI time scale, RNG seed/state, engine worker setting and `WorkerPool` | `SimulationPolicy` phase |
| `ApplyPhysicsFrictionUICommands` | engine material config and `SceneWorld` runtime config mirror | `PhysicsMaterial` phase |
| `ApplyWorldWaterUICommands` | `WorldEnvironment`, plus before/after replay value | `WorldPolicy` phase |
| `ApplyCinematicRenderingToggleUICommand` | launch override, active cinematic config, scene override masks | `CinematicPolicy` phase |
| `QueueCinematicSkyDefaultsUICommand` | `RenderDefaultsStore` intent ring | `CinematicPolicy` phase |
| `HasCinematicModeUICommand` / `ApplyCinematicModeUICommand` | mode acceptance, scene style/materials, browser selection | one `CinematicPolicy` arbitration; the unused apply-function Boolean disappears |
| `ApplyCinematicTuningUICommands` | active cinematic config and scene override masks | `CinematicPolicy` phase, feature before parameter |
| `ApplyWorkerThreadCountOverride` | engine worker setting and `WorkerPool` | private `SimulationPolicy` kernel |
| `ApplyOrdinaryRenderUIParam` | ordinary render config | private `RuntimePresentation` kernel |
| cinematic mask helpers and shadow/feature/parameter kernels | cinematic config and paired override masks | private transaction/controller kernels; stress calls the same owner path |
| `ApplyUIWorldOverride` | generalized cold/stress/load world mutation used outside UI | move to a focused `WorldEnvironment` owner operation; transaction calls it |
| `CinematicSkySunDirection` | pure render-facing direction derived from cinematic values | move to `SceneCinematicPolicy`; it is not an operator-command operation |

No new context, bindings, services, callback pack, inheritance, virtual
dispatch, or type erasure is authorized.

## Measured Arbitration And Same-Frame Winners

| Collision | Current result that OC1 must preserve |
|---|---|
| Human and injected command with same identity | exact duplicate coalesces; differing payload rejects the whole canonicalization with Lane R; there is no silent winner |
| Legacy Scene-shadow and Render-shadow toggles | both normalize to one `ToggleShadows` identity. Projection emits only `renderTuning.toggleShadows`; one ordinary-shadow toggle wins. A duplicate pair coalesces |
| Direct water-reflection cycle plus explicit mode | cycle executes first; explicit mode writes both flags second and wins |
| Tornado enable with auto-visual plus explicit visual-shell toggle | tornado/auto-sync executes first; explicit shell toggle executes second and wins the final visual-enabled value |
| Ordinary defaults save plus ordinary tuning | save records intent first, but the final checkpoint samples the final config; later tuning/shadow edits are what get persisted |
| Cinematic defaults save plus mode/tuning | save records intent first, but checkpoint drain samples after mode and tuning; their final values are persisted |
| Cinematic master toggle plus mode selection | toggle applies first. A successful mode selection resets from the baseline/look and wins overlapping rendering/config values; a failed selection leaves the toggle intact but is still action-recorded as accepted input |
| Cinematic mode selection plus feature/parameter tuning | selected look applies first; feature then parameter edits win their touched fields and UI override bits |
| Scene-aware shadow toggle plus cinematic Shadows feature | when a direct packet carries both, the scene-aware toggle runs first and the feature toggle runs later. Production canonical projection normally maps the shared shadow identity to ordinary rendering instead |
| Multiple Water-tab world fields | requested fields clamp together; unspecified fields retain their pre-phase values; one before/after record covers the atomic result |
| Worker reconfiguration versus generated rebuild | worker pool mutates before GV3 model/solver transactions, so rebuild uses the new pool |
| Friction edits versus generated rebuild | final material config is mirrored to `SceneWorld` before GV3, so existing and newly generated bodies see the same policy |
| Any tuning plus a same-frame scene reset/load | scene request is submitted only after transaction completion and executes later; the existing load/reset preservation policy remains the final authority |

## Acceptance Ledger Census

One `OperatorCommandAcceptanceLedger` replaces the scattered records. Every
retained field has a named consumer:

| Current field(s) | Consumer |
|---|---|
| vsync Boolean return | `recordUIAction(ToggleVsync)` |
| camera `accepted`, `mode` | gate and operand for `InputRouter::ApplyCameraMode`, which records `SetCameraMode` internally |
| sleep Boolean return | `recordUIAction(TogglePhysicsSleepPolicy)` |
| tornado three toggle flags and `applySettingsActionCount` | `RecordTornadoToggleUIActions` and `RecordTornadoApplySettingsUIActions` |
| text-only and fixed-step Boolean returns | `recordUIAction(ToggleTextOnly/ToggleFixedStep)` |
| presentation eight non-water flags | `RecordRuntimePresentationUIActions` |
| presentation two water-reflection flags | `RecordRuntimePresentationWaterUIActions` after replay-memory handling |
| run simulation `setTimeScale`, `setRunSeed` | `RecordRunSimulationUIActions` |
| run simulation `setWorkerThreads` | delayed `recordUIAction(SetWorkerThreads)` after the first GV3 model-count action |
| friction `applySettingsActionCount` | `RecordPhysicsFrictionUIActions` |
| world accepted plus `WorldOverrideChange` | replay `BuildWorldOverride` and `recordUIAction(ApplyWorldWaterSettings)` |
| cinematic rendering/save/mode accepts | inline action recording; mode also sets `enterInteractiveScene` |
| cinematic `toggledFeature`, `appliedParam` | `RecordCinematicTuningUIActions` |

No stored result field is unused. The sole deletable acceptance artifact is the
Boolean return of `ApplyCinematicModeUICommand`: its only caller discards it and
uses `HasCinematicModeUICommand` as the accepted-input fact.

The ledger stores values only. It retains no runtime owner and remains available
until the existing delayed action-recording sites have consumed it.

## `RunInternal` Deletion Boundary

Current tracked source contains 71 `RunInternal` rows across 29 files. Only two
rows are the operator-applier namespace declarations; the rest are qualifications
or sibling helper namespaces in App, Editor, Render, Scene, Tools, Automation,
and tests. OC2's zero-row acceptance is therefore a real repository-wide naming
cleanup, not a two-line rename. Owner-specific helper namespaces are flattened
or moved beside their concrete owner; no replacement `*Internal` namespace is
authorized.

This census is the binding OC1 implementation specification.
