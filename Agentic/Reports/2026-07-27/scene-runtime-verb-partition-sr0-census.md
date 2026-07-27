# Scene Runtime Verb Partition Consolidation — SR0 Census

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan: `scene-runtime-verb-partition-consolidation`
Phase: SR0

## Result

Every operation in the eight `SceneRuntime*` verb units has one destination.
No operation remains a free function over a context or shared authority bag.
The census does not justify a new transaction: load/reset/UI sequencing belongs
to the existing GV2 `SceneLoadTransaction`, and generated-control sequencing
already belongs to the existing GV3 `SceneGeneratedControlTransaction`.

Destination keys are **(a)** method on a concrete owner, **(b)** phase/private
operation of an existing GV transaction, and **(d)** domain-named value or
genuinely pure operation. No operation was ruled to destination (c).

## Operation Rulings

### `SceneRuntimeCoordinator`

| Operation | State / owner and ordering | Destination |
|---|---|---|
| `SceneController::ResetCurrentScene` | `SceneController`; select a valid current entry before marking manual reset | **(a)** retain in `SceneController.Navigation.cpp` |
| `SceneController::AdvanceScene`, `PerfPass` | `SceneController::m_perfPass` and queue; first performance pass reloads current, later passes advance | **(a)** retain in `SceneController.Navigation.cpp` |
| `SubmitSceneUIRequests` | `SceneController` request ring; preserve reset/default/demo/save/create/select order | **(a)** `SceneController::SubmitUIRequests`; rename result `SceneUICommandSubmissionResult` |
| `SceneLoadRequest::{None,AcceptedWithoutLoad,Load,HasLoad}` | Authority-free request value with mutually exclusive no-load/load states | **(d)** `SceneLoadRequest.h` |

The unit disappears.

### `SceneRuntimeCreate`

| Operation | State / owner and ordering | Destination |
|---|---|---|
| `CreateSceneFromUI` | Scene file and `SceneController` queue; sanitize → make directory → choose unused path → write complete file → append queue; failures publish no queue entry | **(a)** `SceneController::CreateScene` in `SceneController.Creation.cpp` |
| `IsSceneNameChar`, `SanitizeSceneFileName`, `NormalizeScenePathForCreate`, `UniqueScenePath`, `WriteStarterSceneFile` | Private creation/file mechanics supporting the owner method | **(a)** anonymous helpers beside `SceneController::CreateScene` |

The unit disappears.

### `SceneRuntimeDefaults`

| Operation | State / owner and ordering | Destination |
|---|---|---|
| `SaveRenderDefaults`, `SaveSkyDefaults` | `engine.cfg`; sole caller/request owner is `RenderDefaultsStore`; load/version-check before rewrite and preserve unrelated lines | **(a)** private `RenderDefaultsStore::PersistOrdinary` and `PersistCinematic` in `RenderDefaultsStore.Persistence.cpp` |
| `ConfigLineMatchesKey`, `ReplaceConfigLine`, `EraseConfigLines`, `EraseConfigLinesWithPrefix`, `OrdinaryConfigInsertIndex`, `CinematicConfigInsertIndex`, `AppendMissingOrdinaryConfigLines`, `AppendMissingCinematicConfigLines`, `LoadConfigLines`, `WriteConfigLines`, `StampCurrentConfigVersion` | Authority-free persistence mechanics | **(a)** anonymous helpers in the owner unit |

The unit disappears.

### `SceneRuntimeGeneratedControls`

| Operation | State / owner and ordering | Destination |
|---|---|---|
| Phase cursor operations | GV3; only `Idle → DrainAndReset → Repopulate → PublishFollowUps → Complete` | **(b)** retain |
| Four factories and private constructor | GV3 request state; select exactly one request kind | **(b)** retain |
| `ResolveRequest` | GV3 counts; UI override wins, balls are preserved and boxes trimmed at capacity | **(b)** retain |
| `DrainAndReset`, `RecordDrainResult`, `MutationAllowedAfterDrain` | GV3 borrows simulation/scene/tools; no mutation after failed GPU drain | **(b)** retain |
| `Repopulate` | GV3 borrows simulation/scene; runs only after successful drain | **(b)** retain |
| `PublishFollowUps`, `RecordFollowUps` | GV3 result flags; follow-ups only after active-scene rebuild | **(b)** retain |
| `AdvanceOrFatal`, `Execute`, `Phase`, diagnostic helper | GV3 owns ordering and diagnostics | **(b)** retain |

The implementation is unchanged structurally. The unit becomes
`SceneGeneratedControlTransaction.{h,cpp}` and
`SceneRuntimeGeneratedControlAction` becomes
`SceneGeneratedControlAction`.

### `SceneRuntimeLoad`

| Operation | State / owner and ordering | Destination |
|---|---|---|
| `RefreshSceneBrowserList` plus path/name helpers | `UI::SceneNavigationModel::browser`; populate paths/names before stable `namePtrs` | **(a)** `SceneNavigationModel::RefreshBrowserList` in `SceneNavigationModel.Browser.cpp` |
| `CurrentSceneBrowserIndex` plus normalization helper | Navigation browser queried against `SceneController`; exact normalized current-path match | **(a)** `SceneNavigationModel::CurrentSceneBrowserIndex` |
| `PrepareSceneRuntimeLoad` plus reset-policy helpers | GV2 load attempt; validate/derive policy before mutation, failed GPU drain prevents load mutation | **(b)** private preparation in `SceneLoadTransaction::Load` |
| `CommitSceneRuntimeLoad` | `SceneController` plus navigation output; interactive/override decision precedes `BeginLoad`, selection follows it | **(b)** private GV2 Load-phase commit |
| `SceneRuntimeLoadBeginResult` | One-attempt GV2 intermediate | **(b)** private `SceneLoadBeginResult` |

The unit disappears. GV2 keeps its existing
`Idle → Load → RuntimeReactions → Presentation → Complete` order.

### `SceneRuntimeReset`

| Operation | State / owner and ordering | Destination |
|---|---|---|
| `CaptureSceneRuntimeResetSnapshot` | GV2 snapshot copied from scene/world/render/debug/UI/camera owners; capture before `BeginLoad` mutation | **(b)** GV2 private `CaptureResetSnapshot` |
| `RestoreSceneRuntimeResetSnapshot` | Same owners; restore after population, clamp camera row, honor exit suppression | **(b)** GV2 private `RestoreResetSnapshot` |
| `ClearSceneRuntimeUIOverrides` | UI navigation overrides; preservation arbitrates capture/restore versus clear, never both | **(b)** GV2 private clear operation |
| `SceneRuntimeResetSnapshot` | One-attempt GV2 intermediate | **(b)** private `SceneResetPreservationSnapshot` |

The unit disappears. This is already a GV2 invariant and does not warrant a
second transaction.

### `SceneRuntimeStyle`

| Operation | State / owner and ordering | Destination |
|---|---|---|
| `ApplyCinematicSceneOverrides` | Explicit value target; masked fields override and unmasked fields inherit | **(d)** `SceneCinematicPolicy` |
| both `ActiveSceneCinematicConfig` overloads | Pure owner selection | **(d)** `SceneCinematicPolicy` |
| `IsSceneCinematicRenderingEnabled` | Pure precedence policy | **(d)** `SceneCinematicPolicy` |
| `ApplyCinematicModeFromBrowserIndex` | `SceneController` owns session/world; clear launch override first, `-1` restores defaults/materials, invalid/load failure makes no later mutations, success commits config/session/materials then selection | **(a)** `SceneController::ApplyCinematicBrowserStyle` in `SceneController.Style.cpp` |
| `ApplyLiveStyleScene` | Same owner; launch override → materials → config → scene flags → browser deselection | **(a)** `SceneController::ApplyLiveStyle` |
| `ApplyDemoHeroStyleOverride` | Same owner; eligibility/load succeeds before live-style application | **(a)** `SceneController::ApplyDemoHeroStyle` |
| path/cinematic classification and load-failure helpers | Pure policy/diagnostics | **(d)** private to `SceneCinematicPolicy` |
| broad-target, material-target, reset/apply-material helpers | Private world mutation; reset before authored overrides and broad targets exclude simple ragdoll parts | **(a)** private beside `SceneController` style methods |

The unit splits into `SceneController.Style.cpp` and
`SceneCinematicPolicy.{h,cpp}`. No seven-reference free operation survives.

### `SceneRuntimeUiOptions`

| Operation | State / owner and ordering | Destination |
|---|---|---|
| `PrepareSceneUiOptions` | GV2 output plus diagnostics/debug; diagnostics apply during Load and visible UI is staged | **(b)** private GV2 Load preparation |
| `ApplySceneUiActivation` | `UI::InGameUI`; Presentation runs after RuntimeReactions, preserve-state gates visible UI while stress still applies | **(b)** GV2 `ApplyPresentationOutputs` |
| `SceneUiActivation` | Produced in GV2 Load and consumed once in Presentation | **(b)** private transaction output |

The unit disappears.

## Final Topology Ruling

| Current unit | Outcome |
|---|---|
| `SceneRuntimeCoordinator` | dissolve into `SceneController.Navigation.cpp` and `SceneLoadRequest.h` |
| `SceneRuntimeCreate` | dissolve into `SceneController.Creation.cpp` |
| `SceneRuntimeDefaults` | dissolve into `RenderDefaultsStore.Persistence.cpp` |
| `SceneRuntimeGeneratedControls` | rename to `SceneGeneratedControlTransaction` |
| `SceneRuntimeLoad` | dissolve into `SceneNavigationModel.Browser.cpp` and existing GV2 implementation |
| `SceneRuntimeReset` | dissolve into existing GV2 implementation |
| `SceneRuntimeStyle` | split into `SceneController.Style.cpp` and `SceneCinematicPolicy.{h,cpp}` |
| `SceneRuntimeUiOptions` | dissolve into existing GV2 implementation |

`class SceneRuntime` is not one of the eight verb partitions. Its queue,
session, lifecycle packet, and object-identity cursor form a cohesive state
owner used by `SceneController`. SR2 must either give it an owner/domain name or
fold it into `SceneController`; it is not authorized to survive unchanged.

SR1 will move browser, controller, defaults, and GV2 clusters in that order,
deleting each emptied unit and reconciling project/filter rows in the same
source commit. No new aggregate, context, callback, service bag, virtual
dispatch, render interface, or transaction is authorized.
