# SceneController Ownership Census And Target Map

Date: 2026-07-17
Branch: `nightrunner-17th-july`
Source inspected: `e284a010d`
Plan: `Agentic/Plans/TODO/scene-controller-ownership-decomposition.md`
Status: T0 complete; the owner ratified the branch and target map on 2026-07-17.

## Physical Surface

`SceneController.h` has 315 lines and textually includes the 132-line
`SceneController.Objects.inl` inside the class at line 146. The logical class
therefore exposes both declaration files as one 447-line surface. Its load
implementation occupies `RunScene.cpp:523-1241`; `ExecutePending` begins at
`SceneRequestExecution.cpp:49` and republishes the same owner set into `Load`.

### Complete member inventory

| Member | Current responsibility | Target |
|---|---|---|
| `m_runtime` | Queue/index, active run state, lifecycle receipts, and currently the automation gate vectors | Keep queue/run/lifecycle state; remove validation gate storage. |
| `m_requests` | Fixed deferred scene request ring | Keep. |
| `m_browser` | Paths, labels, cinematic/concept selection | Move to UI-owned `SceneNavigationModel`. |
| `m_uiOverrides` | Scene-tab model/solver/time-scale override state | Move beside `m_browser` into `SceneNavigationModel`. |
| `m_perfPass` | Two-pass scene navigation/perf index | Keep with lifecycle coordinator. |
| `m_crossScenePauseLocked` | Operator scene-flow lock across loads | Keep; it directly controls scene advancement. |
| `m_entities` | Stable identity/material/asset rows | Keep with scene topology. |
| `m_cameras` | Scene camera slots and active camera state | Keep with scene lifetime. |
| `m_world` | Active gravity/fluid/terrain bounds | Keep with scene lifetime. |
| `m_terrain` | Replaceable terrain owner | Keep with scene lifetime. |
| `m_physics` | Scene-lifetime physics topology | Keep with scene lifetime. |
| `m_renderInstanceStore` (from `.inl`) | Dense current/previous presentation rows paired with scene entities | Keep with cross-store topology coordination. |
| `m_activeGameModelCapacity` (from `.inl`) | Capacity invariant shared by entity/physics/render creation | Keep with cross-store topology coordination. |

### Complete public-method inventory

The methods below cover every public declaration from both physical files.
Overloaded mutable/const accessors are marked `x2`.

| Responsibility | Public methods |
|---|---|
| Construction and owner access | `SceneController` (x2), `State` (x2), `Browser` (x2), `UIOverrides` (x2), `Entities` (x2), `Cameras` (x2), `World` (x2), `Terrain` (x2), `Physics` (x2), `Runtime` (x2) |
| Physics/frame lifecycle | `StepPhysics`, `ApplyWaterHeightControl`, `EnterInteractiveRun`, `CanAutomationQuit`, `MarkInteractiveRunComplete`, `ToggleCrossScenePause`, `CrossScenePauseLocked`, `AdvanceFrame` |
| Queue/navigation | `HasEntry`, `HasCurrentEntry`, `CurrentPath`, `PathAt`, `QueueSize`, `CurrentIndex`, `NextIndex`, `Queue`, `BeginLoad`, `RecordLifecycleEvent`, `MarkManualReset`, `FindNormalizedPath`, `FindGeneratedDemo`, `Append`, `CurrentQueueIsCinematicDeck`, `AdjacentQueueIndex`, `LoadSceneFromBrowserIndex`, `LoadDemoSceneFromUI`, `AdjacentCinematicModeBrowserIndex`, `LoadAdjacentSceneFromBrowser`, `ResetCurrentScene`, `AdvanceScene`, `PerfPass` |
| Cold load/request work | `Load`, `ExecutePending`, `SaveCurrentDefaults`, `SubmitLoadBrowserIndex`, `SubmitLoadDemoScene`, `SubmitResetCurrentScene`, `SubmitCreateScene`, `SubmitSaveCurrentDefaults`, `TakePendingRequests`, `PendingRequestCount`, `TrimForReplayRestore` |
| Automation gates | `RequiredContacts` (x2), `RequiredBroadphaseXCells` (x2), `ClearRequiredAutomationGates`, `UpdateRequiredContacts`, `RequiredContactsComplete`, `UpdateRequiredBroadphaseXCells`, `RequiredBroadphaseXCellsComplete` |
| Cross-store entity/presentation operations from `.inl` | `ApplyRuntimeConfig`, `TryCreateSceneEntity`, `DestroySceneEntity`, `Clear`, `BeginPhysicsStepPresentationCapture`, `CompletePhysicsStepPresentationCapture`, `PrepareRenderInstances`, `TryGetModelPosition`, `TryGetPresentationPose`, `SceneEntityCount`, `GroupKindAt`, `GroupRootObjectIdAt`, `GroupPartIndexAt`, `IsSimpleRagdollPart`, `IsSimpleRagdollTorso`, `RagdollRootModelIndexForPart`, `TryFindSimpleRagdollPart`, `GatherGroupMemberIndices`, `CollectMemoryStats`, `CanTrimPresentationRowsForSceneRestore`, `TrimPresentationRowsForSceneRestore`, `CaptureReplaySolverWorldSnapshot`, `RestoreReplaySolverWorldSnapshot`, `RepairPhysicsBodyAndColliderTopology`, `BodyStore`, `Colliders`, `MutableRenderInstances`, `RenderInstances`, `TryQueueReplayRenderPoseOverride`, `RenderPresentationRecords`, `GetRenderInstanceStore`, `GetSceneKineticEnergy`, `NotifyFixedContact`, `TickContactHighlights`, `NotifyAudioContact`, `ReleaseAttachedFixedTreeParts`, `BeginCollisionVisualFrame`, `EndCollisionVisualFrame`; Debug additionally exposes `TryGetPhysicsDiagnosticsModelName` and `FillPhysicsDiagnosticsNames`. |

The surface mixes four domains: scene lifecycle/navigation, cross-store scene
topology, UI navigation policy, and automation validation. The last two are the
relocated authority that T3/T4 must remove.

## Load Parameter And Phase Census

`Load` has the request plus 22 borrowed owners; `ExecutePending` has the same
22 owners without the request. Current per-phase reads are:

| Load phase and source range | Owners read |
|---|---|
| Preflight/unload (`RunScene.cpp:547-637`) | overlays/debug state, renderer, camera, render backend/device lifecycle, launch policy, diagnostics |
| Clear/reset (`:639-678`) | config, diagnostics, simulation, audio, renderer, tools, input, interaction, attached camera, replay, camera, timers |
| Generated population (`:702-781`) | config, startup capacities, worker pool, launch overrides, assets, render backend, replay, UI overrides, browser state, default cinematic config, window |
| Authored population (`:787-1069`) | assets, config/startup/worker policy, diagnostics, renderer, timers, operator UI, launch overrides, render backend, camera/input/interaction, browser and UI overrides, window |
| Activation/notification (`:1075-1241`) | launch overrides, UI overrides, renderer, diagnostics, operator UI, validation harness, timers, render backend, replay, input, interaction, camera, attached camera |

No individual phase needs the full list. The proposed synchronous borrow
surface is four narrow values, each retained only for the call:

| Participant | Borrowed fields (4-6 each) |
|---|---|
| `SceneLoadPolicyInputs` | config, launch options, default cinematic config, startup state, assets, worker pool |
| `SceneLoadHostParticipants` | window, timers, diagnostics, simulation |
| `SceneLoadInteractionParticipants` | input router, interaction controller, camera, attached camera, runtime tools, operator UI |
| `SceneLoadPresentationParticipants` | contact audio, replay runtime, overlay diagnostics, validation harness, render backend view, runtime renderer |

`Load(request, policy, host, interaction, presentation)` is five parameters;
`ExecutePending(policy, host, interaction, presentation)` is four. No value
contains the old complete dependency graph, none is retained, and every field
has a phase-specific use above.

## Ratified T0 Owner Rulings

1. Ratify branch `nightrunner-17th-july` and the four participant values above.
2. Keep scene queue/run state, request ring, perf/pause lifecycle policy,
   entities, cameras, world, terrain, physics, render rows, and capacity in the
   scene lifecycle/topology owner.
3. Move `RunSceneBrowserState` and `RunSceneUIOverrideState` into a concrete
   `SceneNavigationModel` owned by `UI::InGameUI`. Input/UI code mutates that
   model and emits value-only `SceneLoadRequest` values to `SceneController`.
   Scene load may borrow a snapshot/reference only for synchronous application.
4. Move required-contact and required-broadphase state plus completion logic
   into a concrete `SceneAutomationGateTracker` owned by
   `RuntimeValidationHarness`. It reads immutable physics/store views after the
   physics step and supplies value completion facts to scene advancement.
5. Re-home the `.inl` declarations into `SceneController.h` after T3/T4. The
   entity/physics/render methods remain cohesive cross-store transaction
   operations; no forwarding facade or second context owner is introduced.

## Validation

T0 is documentation-only. No repository validation is required before its
commit. The owner's 2026-07-17 approval ratifies this target map and releases
T1.
