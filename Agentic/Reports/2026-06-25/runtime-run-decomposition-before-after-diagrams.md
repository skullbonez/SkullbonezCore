# Runtime Run Decomposition Before/After Diagrams

Date: 2026-06-25

Comparison points:

- Before: `de9940c0402a77291019efd33b657ebe6ea43ef7`, the parent of the first runtime decomposition commit.
- After: `0fba04ffb28c3fd2f1168860dadccbe7ecc3f2c5`, the final Phase 8 boundary-lock commit on `nightrunner-24th-june-refactor`.

This report is intentionally architecture-focused. It shows ownership and control
boundaries that changed, and it calls out remaining compatibility bridges so the
diagrams do not overstate the refactor.

## Executive Delta

Before the refactor, `Run` was the practical owner of most runtime decisions and
state: render pass classes and objects, replay recorders, editor/tool transient
state, scene population helpers, diagnostics entry points, and physics-facing
coordination. Several specialized systems existed, but the runtime still routed
major behavior through `Run` fields and helper methods.

After the refactor, `Run` is much closer to an application shell. It owns top
level subsystem objects and tick order, while ownership of render passes,
replay state, runtime tools, diagnostics state, scene lifecycle decisions, and
physics implementation access moved behind named owners.

The main remaining limitation is not hidden: `Run` still performs broad
composition, still owns some cross-cutting runtime state, and
`RuntimeRenderHost` remains a broad bridge from render passes to runtime state.

## Before Class Ownership

```mermaid
classDiagram
    direction LR

    class Run_Before {
        +SceneController m_sceneController
        +ReplayRecorder m_replay
        +ReplaySolverRecorder m_solverReplay
        +ReplayEventRecorder m_replayEvents
        +ReplayBranchInfo m_replayBranch
        +RunRayCastTestState m_rayCastTest
        +RunMousePickupState m_mousePickup
        +RunEditorPlacementState m_editor
        +RunEditorTracer m_editorTracer
        +LauncherLaser m_launcherLaser
        +GameModelCollection m_cGameModelCollection
        +WorldEnvironment m_cWorldEnvironment
        +FullscreenQuadPass m_fullscreenQuadPass
        +SkyPass m_skyPass
        +SceneTargetPass m_sceneTargetPass
        +ShadowPass m_shadowPass
        +ReflectionPass m_reflectionPass
        +ObjectPass m_objectPass
        +TerrainPass m_terrainPass
        +WaterPass m_waterPass
        +TornadoVisualPass m_tornadoVisualPass
        +DebugOverlayPass m_debugOverlayPass
        +VolumetricPass m_volumetricPass
        +TonemapPass m_tonemapPass
        +UiTextPass m_uiTextPass
        +DrawPrimitives()
        +SetUpGameModels()
        +SetUpSolverObjects()
        +SetUpCamerasFromScene()
        +SetUpGameModelsFromScene()
        +SetUpRequiredContactsFromScene()
        +SetUpRequiredBroadphaseXCellsFromScene()
    }

    class RenderPasses_Before {
    }
    note for RenderPasses_Before "Nested in Run.h\nConstructors took Run&\nPass order scheduled by Run::DrawPrimitives"

    class ReplayState_Before {
        +presentationRecorder
        +solverRecorder
        +eventRecorder
        +branchProvenance
        +scrubState
        +predictionState
        +causeTreeState
        +velocityEditState
    }

    class ToolState_Before {
        +launcherRayTestLines
        +launcherLaserVisuals
        +mousePickupState
        +editorPlacementState
        +editorTracerLines
    }

    class SceneSetup_Before {
        +generatedModelSetupWrappers
        +solverObjectSetupWrappers
        +authoredCameraSetupWrappers
        +authoredModelSetupWrappers
        +requiredContactAndBroadphaseGates
    }

    class GameModelCollection_Before {
        +bodyModelRenderCompatibilityStorage
        +physicsDiagnosticsFriends
        +solverFriends
        +sceneSnapshotFriend
    }

    class PhysicsInternals_Before {
        +PhysicsDiagnosticsSink
        +PersistentContactSolver
        +SleepIslandSystem
        +PhysicsWorld
        +PhysicsScene
    }

    Run_Before *-- RenderPasses_Before : owns types and instances
    Run_Before *-- ReplayState_Before : owns recorder and UI state
    Run_Before *-- ToolState_Before : owns transient tools
    Run_Before *-- SceneSetup_Before : owns setup helpers
    Run_Before *-- GameModelCollection_Before : coordinates models and physics
    GameModelCollection_Before ..> PhysicsInternals_Before : friend access leaks
```

## After Class Ownership

```mermaid
classDiagram
    direction LR

    class Run_After {
        +SceneController m_sceneController
        +SceneRuntimeCoordinator m_sceneCoordinator
        +DiagnosticsRuntime m_diagnosticsRuntime
        +ReplayRuntime m_replayRuntime
        +RuntimeTools m_runtimeTools
        +RuntimeRenderHost m_renderHost
        +RuntimeRenderer m_renderer
        +GameModelCollection m_cGameModelCollection
        +WorldEnvironment m_cWorldEnvironment
        +BindEngineContext()
        +LoadScene()
        +Render()
        +DrawPrimitives()
    }

    class RuntimeRenderer {
        +EnsureFrameResources()
        +RenderFrame()
        +ReleaseBackendOwnedResources()
        +RenderUiText()
        -FullscreenQuadPass
        -SkyPass
        -SceneTargetPass
        -ShadowPass
        -ReflectionPass
        -ObjectPass
        -TerrainPass
        -WaterPass
        -TornadoVisualPass
        -DebugOverlayPass
        -VolumetricPass
        -TonemapPass
        -UiTextPass
    }

    class RuntimeRenderHost {
    }
    note for RuntimeRenderHost "Borrowed runtime references\nCallback bridge to Run behavior\nNo ownership"

    class SceneRuntimeCoordinator {
        +LoadSceneFromBrowserIndex()
        +LoadDemoSceneFromUI()
        +ApplyAdjacentCinematicMode()
        +LoadAdjacentSceneFromBrowser()
        +ResetCurrentScene()
        +AdvanceScene()
    }

    class SceneSetupHelpers {
    }
    note for SceneSetupHelpers "SceneGeneratedSetup\nSceneAuthoredSetup\nExplicit context structs"

    class ReplayRuntime {
        +Presentation()
        +Solver()
        +Events()
        +Branch()
        +ConfigureRecording()
        +CaptureFrame()
        +ApplyPresentationSampleForRender()
        +ApplySolverSampleForRender()
        +ApplyPredictionFrameForRender()
        +RestoreRenderPose()
        +BuildPredictionGhostDrawRequests()
        -ReplayRecorder m_presentation
        -ReplaySolverRecorder m_solver
        -ReplayEventRecorder m_events
        -ReplayBranchInfo m_branch
        -scrubPredictionState
        -renderPoseBackups
        -ghostDrawRequests
    }

    class RuntimeTools {
        +RayCastTest()
        +Laser()
        +MousePickup()
        +Editor()
        +EditorTracer()
        -RunRayCastTestState
        -LauncherLaser
        -RunMousePickupState
        -RunEditorPlacementState
        -RunEditorTracer
    }

    class DiagnosticsRuntime {
        +Capture()
        +Diagnostics()
        +PerfLog()
        +TickPerfLog()
        +PhysicsDiagnostics()
        +UIStress()
        -CaptureController
        -DiagnosticsController
        -UIStressState
    }

    class GameModelCollection_After {
        +GetPhysicsEngine()
        +GetPhysicsBodyStore()
        +GetColliderStore()
        +GetRenderInstanceStore()
        +GetPhysicsDebugContacts()
    }
    note for GameModelCollection_After "Compatibility model facade\nOwns PhysicsEngine\nExposes named physics stores/views\nNo solver diagnostics friend leaks"

    class PhysicsEngine {
        +Step()
        +WakeBody()
        +ApplyBodyImpulse()
        +SetPendingBodyImpulse()
        +CaptureReplaySolverSnapshot()
        +RestoreReplaySolverSnapshot()
        +GetDiagnosticsView()
        -PhysicsScene m_scene
    }

    class BoundaryValidation {
    }
    note for BoundaryValidation "validate_runtime_boundaries.bat\ncheck_runtime_boundaries.py\nwired into validate_fast\nwired into validate_full\nselectable as runtime-boundaries"

    class PhysicsInternals_After {
        +PhysicsScene
        +PhysicsWorld
        +solverInternals
        +diagnosticsView
    }

    Run_After *-- RuntimeRenderer : owns subsystem
    RuntimeRenderer --> RuntimeRenderHost : borrows services
    Run_After *-- SceneRuntimeCoordinator : owns lifecycle coordinator
    Run_After ..> SceneSetupHelpers : calls explicit setup contexts
    Run_After *-- ReplayRuntime : owns replay subsystem
    Run_After *-- RuntimeTools : owns tool subsystem
    Run_After *-- DiagnosticsRuntime : owns diagnostics subsystem
    Run_After *-- GameModelCollection_After : owns model facade
    GameModelCollection_After *-- PhysicsEngine : owns physics facade
    PhysicsEngine *-- PhysicsInternals_After : owns implementation
    BoundaryValidation ..> Run_After : prevents ownership regression
```

## Before Architecture

```mermaid
flowchart LR
    UserInput["Input / UI / CLI"] --> Run["Run monolith"]
    SceneFiles["Scene files"] --> Run
    Validation["Validation scripts"] --> Run

    Run --> RenderScheduler["Run::DrawPrimitives"]
    RenderScheduler --> Passes["Nested render pass objects in Run"]
    Passes --> DX12["DX12 backend"]

    Run --> SceneSetup["Run scene setup helpers"]
    SceneSetup --> Models["GameModelCollection"]

    Run --> ReplayRecorders["Replay recorders and branch state"]
    ReplayRecorders --> ReplayExport["Replay save/export"]

    Run --> ToolTransient["Launcher / manipulator / editor transient state"]
    ToolTransient --> RenderDebug["Editor tracer and launcher visuals"]

    Run --> Diagnostics["Capture, perf, SkullScope entry points"]
    Run --> PhysicsCalls["Direct physics-facing coordination"]
    PhysicsCalls --> Models
    Models -. friend access .-> SolverInternals["Diagnostics, solver, and sleep internals"]

    Run --> UI["InGameUI"]
    Run --> World["WorldEnvironment"]

    classDef center fill:#ffe6e6,stroke:#a33,stroke-width:2px;
    classDef owned fill:#fff4cc,stroke:#8a6d00;
    class Run center;
    class RenderScheduler,Passes,SceneSetup,ReplayRecorders,ToolTransient,Diagnostics,PhysicsCalls owned;
```

The pre-refactor problem was not only file size. The problem was that several
subsystems were not true owners. They were data or helper islands whose state
and scheduling still converged in `Run`.

## After Architecture

```mermaid
flowchart LR
    UserInput["Input / UI / CLI"] --> Run["Run application shell"]
    SceneFiles["Scene files"] --> Run
    Validation["Validation scripts"] --> BoundaryGate["Runtime boundary gate"]
    BoundaryGate --> Run

    Run --> Renderer["RuntimeRenderer"]
    Renderer --> RenderHost["RuntimeRenderHost bridge"]
    RenderHost -. borrows .-> RuntimeState["Run-owned cross-cutting state"]
    Renderer --> Passes["Render pass objects"]
    Passes --> DX12["DX12 backend"]

    Run --> SceneCoordinator["SceneRuntimeCoordinator"]
    SceneCoordinator --> SceneController["SceneController"]
    Run --> SceneSetup["SceneGeneratedSetup / SceneAuthoredSetup"]
    SceneSetup --> Models["GameModelCollection"]

    Run --> Replay["ReplayRuntime"]
    Replay --> ReplayRecorders["Presentation / solver / event recorders"]
    Replay --> ReplayRenderState["render pose overrides and ghost requests"]
    Replay --> ReplayExport["Replay save/export delegation"]

    Run --> Tools["RuntimeTools"]
    Tools --> ToolState["launcher, manipulator, editor, tracer state"]

    Run --> DiagnosticsRuntime["DiagnosticsRuntime"]
    DiagnosticsRuntime --> Capture["CaptureController"]
    DiagnosticsRuntime --> Diagnostics["DiagnosticsController / perf / SkullScope / UI stress"]

    Models --> PhysicsEngine["PhysicsEngine facade"]
    PhysicsEngine --> PhysicsScene["PhysicsScene"]
    PhysicsScene --> PhysicsWorld["PhysicsWorld and solver internals"]

    Run --> UI["InGameUI"]
    Run --> World["WorldEnvironment"]

    classDef shell fill:#e8f4ff,stroke:#2867a8,stroke-width:2px;
    classDef owner fill:#e7f7ea,stroke:#2e7d32,stroke-width:2px;
    classDef bridge fill:#fff4cc,stroke:#8a6d00,stroke-dasharray: 4 3;
    class Run shell;
    class Renderer,SceneCoordinator,Replay,Tools,DiagnosticsRuntime,PhysicsEngine owner;
    class RenderHost,RuntimeState bridge;
```

The post-refactor architecture has named ownership points. `Run` still composes
the engine, but the major state blocks now have subsystem names that appear in
stack traces, headers, and validation guardrails.

## Control Flow Comparison

```mermaid
sequenceDiagram
    participant BeforeRun as Before: Run
    participant BeforePasses as Before: nested passes
    participant BeforeModels as Before: GameModelCollection
    participant BeforeReplay as Before: replay fields

    BeforeRun->>BeforeRun: gather input, scene, replay, tools, diagnostics
    BeforeRun->>BeforeReplay: mutate recorder/scrub/prediction state directly
    BeforeRun->>BeforeModels: refresh physics/model/render stores
    BeforeRun->>BeforePasses: ensure pass resources in Run::DrawPrimitives
    BeforeRun->>BeforePasses: render pass order from Run
```

```mermaid
sequenceDiagram
    participant Run as After: Run
    participant Replay as ReplayRuntime
    participant Physics as PhysicsEngine
    participant Renderer as RuntimeRenderer
    participant Host as RuntimeRenderHost
    participant Tools as RuntimeTools
    participant Diag as DiagnosticsRuntime

    Run->>Replay: capture/apply replay-owned state
    Run->>Tools: read/update tool-owned state
    Run->>Diag: route capture, perf, and diagnostics
    Run->>Physics: step and query through facade
    Run->>Renderer: RenderFrame(RuntimeRenderInputs)
    Renderer->>Host: borrow named runtime services
    Renderer->>Renderer: execute owned pass order
    Run->>Replay: restore replay render pose
```

## What Actually Improved

| Area | Before | After |
|------|--------|-------|
| Render ownership | `Run.h` declared and stored pass classes; `Run::DrawPrimitives()` scheduled the frame graph. | `RuntimeRenderer` declares the pass owner and owns frame pass order. `Run::DrawPrimitives()` is a compatibility wrapper around `RuntimeRenderer::RenderFrame()`. |
| Render dependencies | Pass constructors took `Run&`, hiding every dependency behind the monolith. | Passes use `RuntimeRenderHost` and `RuntimeRenderInputs`. This names dependencies, though the host is still broad. |
| Scene lifecycle | Scene advance/reset/load decisions and scene setup helpers were concentrated in `Run`. | `SceneRuntimeCoordinator` owns lifecycle decisions; generated/authored setup helpers use explicit context structs. `Run::LoadScene()` still coordinates side effects. |
| Physics boundary | Scene, tools, replay, and diagnostics touched physics through collection-centered compatibility paths and friend readers. | `PhysicsEngine` is the runtime-facing facade over `PhysicsScene`; diagnostics use `PhysicsWorld::DiagnosticsView`; obsolete collection friend leaks were removed. |
| Replay ownership | Presentation/solver/event recorders, branch state, scrub state, prediction state, render-pose backups, and ghost data lived on or around `Run`. | `ReplayRuntime` owns recorders, branch state, replay UI/prediction state, render-pose backup/restore, capture flow, event stamping, export delegation, and ghost draw requests. |
| Runtime tools | Launcher laser, ray-test state, mouse pickup, editor placement, and tracer state lived directly on `Run`. | `RuntimeTools` owns those state blocks and exposes accessors while behavior continues migrating. |
| Diagnostics | Capture, perf, SkullScope, replay probes, and UI stress entry points were reached through `Run` and static helpers. | `DiagnosticsRuntime` owns capture/diagnostics controllers, perf log state, physics diagnostics state, and UI stress state. |
| Regression protection | Nothing automatically stopped `Run.h` from regaining extracted state. | `tools\validate_runtime_boundaries.bat` checks for render pass classes, replay recorder fields, tool transient fields, scene setup helper declarations, and stored `Run` pointers/references in runtime subsystem headers. |

## Honest Remaining Boundaries

- `Run` is still the application composition root. It still owns process
  lifetime, top-level tick order, CLI/UI overrides, scene browser path lists,
  `GameModelCollection`, `WorldEnvironment`, `InGameUI`, and several
  cross-subsystem coordination methods.
- `RuntimeRenderHost` is still intentionally broad. It made render dependencies
  explicit and moved pass ownership out of `Run`, but it still bridges render
  passes back to runtime state and callbacks.
- `GameModelCollection` still owns the `PhysicsEngine` facade and remains a
  compatibility layer over model, body, collider, and render-instance stores.
  The physics implementation is better isolated, not fully independent of the
  collection.
- Some replay and tool behavior still executes in `Run` methods. The important
  change is that large replay/tool state blocks and several replay operations
  now have subsystem owners.
- Scene loading is improved but not fully inverted. `SceneRuntimeCoordinator`
  owns lifecycle decisions and setup helpers are extracted, while
  `Run::LoadScene()` still performs broad side effects across UI, renderer,
  physics, replay, and diagnostics.

## One-Screen Read

```mermaid
flowchart TB
    subgraph Before["Before: Run as ownership sink"]
        BRun["Run"]
        BRun --> BRender["render pass classes and objects"]
        BRun --> BReplay["replay recorders and replay UI state"]
        BRun --> BTools["launcher, mouse pickup, editor, tracer"]
        BRun --> BScene["scene lifecycle and setup helpers"]
        BRun --> BDiag["capture, perf, SkullScope, UI stress"]
        BRun --> BPhysics["physics coordination through model collection"]
    end

    subgraph After["After: Run as composition root"]
        ARun["Run"]
        ARun --> ARenderer["RuntimeRenderer"]
        ARun --> AReplay["ReplayRuntime"]
        ARun --> ATools["RuntimeTools"]
        ARun --> AScene["SceneRuntimeCoordinator and setup helpers"]
        ARun --> ADiag["DiagnosticsRuntime"]
        ARun --> AModels["GameModelCollection"]
        AModels --> APhysics["PhysicsEngine"]
        ARenderer -.-> AHost["RuntimeRenderHost bridge"]
    end

    BRun -. refactor .-> ARun

    classDef before fill:#ffe6e6,stroke:#a33,stroke-width:2px;
    classDef after fill:#e7f7ea,stroke:#2e7d32,stroke-width:2px;
    classDef bridge fill:#fff4cc,stroke:#8a6d00,stroke-dasharray: 4 3;
    class BRun,BRender,BReplay,BTools,BScene,BDiag,BPhysics before;
    class ARun,ARenderer,AReplay,ATools,AScene,ADiag,AModels,APhysics after;
    class AHost bridge;
```
