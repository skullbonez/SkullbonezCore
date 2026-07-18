# SceneController Round-2 Census And Proposed Split Map

Date: 2026-07-18
Branch: `nightrunner-17th-july`
Source inspected: `000598ea34de55c66b878063d5a298331da9c6ff`
Plan: `Agentic/Plans/TODO/scene-controller-decomposition-round-2.md`
Status: S0 complete; the 2026-07-18 owner directive to complete every pending
plan delegates plan-local design choices, and the orchestrator ratified the
decisions below under that direction.

## Current Physical Surface

`SceneController.h` is 417 lines. Its public section contains 98 declarations
across 90 method names (including two Debug-only declarations and mutable/const
overloads). The class directly owns nine data members and thirteen private
helper declarations. The prior round removed the in-class `.inl`, UI-owned
browser state, and automation-gate storage, but the surviving class still mixes
scene lifecycle policy with scene-lifetime world topology and several
consumer-specific edges.

## Complete Public-Method Census

Mutable/const overloads are marked `x2`; constructors are also `x2`. These rows
account for all 98 declarations in `SceneController.h:198-380`.

| Classification | Current declarations | Round-2 destination |
|---|---|---|
| Lifecycle construction/state | `SceneController` (x2), `State` (x2), `Runtime` (x2) | Keep on `SceneController`; it constructs/composes the world owner and owns active-run/queue lifecycle state. |
| Lifecycle frame policy | `EnterInteractiveRun`, `CanAutomationQuit`, `MarkInteractiveRunComplete`, `ToggleCrossScenePause`, `CrossScenePauseLocked`, `AdvanceFrame` | Keep on `SceneController`. |
| Queue/lifecycle navigation | `HasEntry`, `HasCurrentEntry`, `CurrentPath`, `PathAt`, `QueueSize`, `CurrentIndex`, `NextIndex`, `Queue`, `BeginLoad`, `RecordLifecycleEvent`, `MarkManualReset`, `FindNormalizedPath`, `FindGeneratedDemo`, `Append`, `CurrentQueueIsCinematicDeck`, `AdjacentQueueIndex`, `ResetCurrentScene`, `AdvanceScene`, `PerfPass` | Keep on `SceneController`; these operate its queue, request, perf-pass, and lifecycle receipts. |
| Load/request transaction | `Load`, `ExecutePending`, `SaveCurrentDefaults`, `SubmitLoadBrowserIndex`, `SubmitLoadDemoScene`, `SubmitResetCurrentScene`, `SubmitCreateScene`, `SubmitSaveCurrentDefaults`, `TakePendingRequests`, `PendingRequestCount` | Keep sequencing/submission on `SceneController`; narrow participant values at S5. |
| World creation/topology | `ApplyRuntimeConfig`, `TryCreateSceneEntity`, `DestroySceneEntity`, `Clear`, `SceneEntityCount`, `RepairPhysicsBodyAndColliderTopology`, `TrimForReplayRestore`, `ReleaseAttachedFixedTreeParts` | Move to `SceneWorld`; no controller relay remains. |
| World presentation snapshot | `BeginPhysicsStepPresentationCapture`, `CompletePhysicsStepPresentationCapture`, `PrepareRenderInstances`, `TryGetModelPosition`, `TryGetPresentationPose`, `CanTrimPresentationRowsForSceneRestore`, `TrimPresentationRowsForSceneRestore`, `MutableRenderInstances`, `RenderInstances`, `TryQueueReplayRenderPoseOverride`, `RenderPresentationRecords`, `GetRenderInstanceStore`, `NotifyFixedContact`, `TickContactHighlights` | Move to `SceneWorld`; S2 replaces fixed-contact notification with a bounded post-step output before the move. |
| World physics/replay | `CaptureReplaySolverWorldSnapshot`, `RestoreReplaySolverWorldSnapshot`, `BodyStore`, `Colliders`, `GetSceneKineticEnergy`, `BeginCollisionVisualFrame`, `EndCollisionVisualFrame`, `StepPhysics` | Move to `SceneWorld`; `StepPhysics` is a world operation and does not remain as a controller relay. |
| World owner access | `Entities` (x2), `Cameras` (x2), `World` (x2), `Terrain` (x2), `Physics` (x2) | Replace with one mutable/const `SceneWorld` owner boundary; callers then borrow the specific concrete owner they require. No duplicate compatibility accessors remain on `SceneController`. |
| Relocate: navigation policy | `LoadSceneFromBrowserIndex`, `LoadDemoSceneFromUI`, `AdjacentCinematicModeBrowserIndex`, `LoadAdjacentSceneFromBrowser` | Move decision logic to UI-owned `SceneNavigationModel`; submit only value `SceneLoadRequest` records to lifecycle. |
| Relocate: input/audio | `ApplyWaterHeightControl`, `NotifyAudioContact` | Interaction maps keys to a typed water-height command. Audio consumes bounded post-step contact outputs. |
| Relocate: grouping/ragdoll | `GroupKindAt`, `GroupRootObjectIdAt`, `GroupPartIndexAt`, `IsSimpleRagdollPart`, `IsSimpleRagdollTorso`, `RagdollRootModelIndexForPart`, `TryFindSimpleRagdollPart`, `GatherGroupMemberIndices` | Move to `SceneEntityStore`, which owns behavior-group rows; callers use the store directly. |
| Relocate: diagnostics | `CollectMemoryStats`, Debug-only `TryGetPhysicsDiagnosticsModelName`, Debug-only `FillPhysicsDiagnosticsNames` | Group/name queries move to `SceneEntityStore`; aggregate memory accounting becomes a diagnostics-boundary free operation over const entity, physics, and render-store views. |

The target removes 51 declarations from the lifecycle surface before any load-
surface consolidation. The exact S6 public count is evidence, not a ratchet;
the accepted target remains fewer than 60 declarations and zero consumer-policy
relays.

## Complete Member And Private-Helper Census

| Member/helper | Current responsibility | Round-2 destination |
|---|---|---|
| `m_runtime` | Queue, active-run state, and lifecycle receipts | Keep on `SceneController`. |
| `m_requests` | Fixed deferred scene request ring | Keep on `SceneController`. |
| `m_perfPass` | Two-pass lifecycle/performance index | Keep on `SceneController`. |
| `m_crossScenePauseLocked` | Operator cross-scene flow lock | Keep on `SceneController`. |
| `m_entities` | Stable identity, authored metadata, and behavior groups | Move to `SceneWorld`. |
| `m_cameras` | Scene-lifetime camera slots | Move to `SceneWorld`. |
| `m_world` | Gravity, fluid, and terrain-bound settings | Move to `SceneWorld`. |
| `m_terrain` | Replaceable terrain owner | Move to `SceneWorld`. |
| `m_physics` | Scene-lifetime physics engine | Move to `SceneWorld`. |
| `m_renderInstanceStore` | Dense render/presentation snapshot | Move to `SceneWorld`. |
| `m_activeGameModelCapacity` | Shared entity/physics/render capacity invariant | Move to `SceneWorld`. |
| `ReserveForActiveGameModelCapacity`, `RefreshPhysicsBodyStoreFromAuthoredDescriptors`, `RepairPhysicsBodyTopology`, `RefreshRenderInstances`, `AssertSceneCreationTopology` | Cross-store capacity, repair, and snapshot operations | Move with their data to `SceneWorld`. |
| `BehaviorGroupAt`, `ResolveBehaviorGroupRootModelIndex`, `FixedTreeReleaseRootForModelIndex`, `SceneEntities` (x2) | Behavior-group/store lookup | Move to `SceneEntityStore` or delete after direct store use. |
| `BuildFixedTreeReleaseRootsForReload`, `BuildDiagnosticNamesForReload` | Cold derived rows built from entity metadata | Move to `SceneEntityStore`; return bounded/value rows to physics/diagnostics consumers. |

No target member is a backpointer, service bag, callback pack, or mutable alias
to `SceneController`. `SceneWorld` owns only the scene-lifetime topology whose
counts and replacement lifetime are one invariant.

## Ratified Owner Decisions

1. Reuse feature branch `nightrunner-17th-july`.
2. Name the concrete world owner `SceneWorld`. It is a domain noun and denotes
   the replaceable scene-lifetime entity/physics/camera/terrain/world/render
   unit; it is not a generic state or context bag.
3. Move `StepPhysics` to `SceneWorld`. The lifecycle controller may order a
   frame, but it must not relay the world operation after extraction.
4. Put behavior-group and Debug name queries on `SceneEntityStore`. Put the
   cross-owner `MainMemoryGameObjectStats` assembly at a diagnostics boundary
   that accepts const store/physics/render views; do not give the entity store
   sibling-owner access.

These decisions release S1 source work.

## Load Participant Census And Post-Split Target

The current four structs carry 22 borrowed concrete owners: policy 6, host 4,
interaction 6, and presentation 6. `RunScene.cpp:522-544` aliases every one;
`SceneRequestExecution.cpp:50-142` republishes all four participant values.

| Current participant | Current fields | Post-split target |
|---|---|---|
| `SceneLoadPolicyInputs` | config, launch options, default cinematic config, startup, assets, worker pool | Keep all six; each controls authored/generated population or capacity. |
| `SceneLoadHostParticipants` | window, timers, diagnostics, simulation | Keep timers, diagnostics, and simulation. Return title text as an activation output for `Window` to apply. |
| `SceneLoadInteractionParticipants` | input router, interaction, camera, attached camera, runtime tools, operator UI | Keep input router, interaction, camera, attached camera, and runtime tools. Replace the complete UI owner with value navigation inputs plus value UI activation outputs. |
| `SceneLoadPresentationParticipants` | contact audio, replay, overlays, validation harness, render backend view, renderer | Keep replay, overlays, render backend view, and renderer. Audio consumes the bounded lifecycle/contact output from S2; validation consumes a value scene-gate configuration rather than participating as a mutable owner. |

The target public borrow surface is 18 concrete owners (6 + 3 + 5 + 4), down
from 22. Window title, UI visibility/navigation, audio reset/contact, and
validation-gate effects cross explicit value outputs at their consumer
boundaries. Those values must preserve the current synchronous order; they are
not callbacks and are never retained by the scene owners.

## Evidence And Gate

- CodeGraph index was current at the inspected tip.
- The header census was mechanically counted from the public/private sections
  and reconciled against every declaration and data member above.
- S0 is documentation-only; no repository validation is required.
- The ratified branch and split map release S1.
