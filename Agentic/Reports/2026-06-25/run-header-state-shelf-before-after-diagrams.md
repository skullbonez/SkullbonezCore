# Run Header State Shelf Before/After Diagrams

Date: 2026-06-25

Comparison points:

- Before: `2fc1cdf87b1c`, the branch state before this follow-up header split.
- After: the working tree prepared for the next commit on `nightrunner-24th-june-refactor`.

This is a narrow follow-up to the larger runtime decomposition. It does not claim
that `Run` stopped being the composition root. The real improvement is that
`Run.h` no longer carries every state aggregate, render-pass resource bundle,
debug replay probe, CLI flag, and scene-browser list inline in the same header.

## One-Screen Delta

```mermaid
flowchart LR
    subgraph Before["Before: Run.h as state catalogue"]
        BRun["Run.h\n859 lines"]
        BRun --> BFacade["Run facade and methods"]
        BRun --> BRuntimeState["runtime state structs\nsettings, timers, camera, debug"]
        BRun --> BRenderResources["render pass resource structs\nreflection, sky, shadow, tonemap"]
        BRun --> BReplayProbes["debug replay probe structs"]
        BRun --> BLooseFields["loose Run members\nm_cmd*, scene browser vectors,\nUI overrides, latches, replay mismatch counters"]
    end

    subgraph After["After: named state shelves"]
        ARun["Run.h\n590 lines"]
        ARun --> AFacade["Run facade and methods"]
        ARun --> AState["RunState.h\n233 lines"]
        ARun --> AReplay["RunReplayProbeState.h\n46 lines"]
        ARun --> ARender["RuntimeRenderResources.h\n102 lines"]

        AState --> ALaunch["RunLaunchOptions"]
        AState --> ABrowser["RunSceneBrowserState"]
        AState --> AOverrides["RunSceneUIOverrideState"]
        AState --> AStartup["RunStartupState"]
        AState --> ALatches["RunInputLatchState"]
        AState --> AMismatch["RunReplayMismatchState"]
        ARender --> APassResources["RunRenderPassResources"]
    end

    BRun -. split .-> ARun

    classDef before fill:#ffe6e6,stroke:#a33,stroke-width:2px;
    classDef after fill:#e7f7ea,stroke:#2e7d32,stroke-width:2px;
    classDef shelf fill:#e8f4ff,stroke:#2867a8,stroke-width:2px;
    class BRun,BFacade,BRuntimeState,BRenderResources,BReplayProbes,BLooseFields before;
    class ARun,AFacade after;
    class AState,AReplay,ARender,ALaunch,ABrowser,AOverrides,AStartup,ALatches,AMismatch,APassResources shelf;
```

## Before Class Shape

```mermaid
classDiagram
    direction LR

    class RunHeader_Before {
        Run facade and lifecycle methods
        TornadoVisualSettings
        RunRuntimeSettings
        RunTimerState
        ReflectionPassResources
        SkyPassResources
        CinematicScenePassResources
        VolumetricLightPassResources
        TonemapPassResources
        FullscreenPassResources
        ShadowPassResources
        RunRenderPassResources
        RunSubsystemState
        RunCameraState
        RunLiveStyleControlState
        OverlayMode
        RunDebugState
        RunReplayScrubProbeState
        RunReplayRestoreProbeState
        RunReplaySaveProbeState
        m_cmdTimeScaleOverride
        m_cmdFixedStep
        m_cmdSeedOverride
        m_cmdNoWater
        m_cmdNoSleep
        m_cmdTornado*
        m_cmdCinematic*
        m_cmdPhysicsDebug*
        m_sceneBrowserPaths
        m_sceneBrowserNames
        m_sceneBrowserNamePtrs
        m_selectedCineModeSceneIndex
        m_UITimeScaleOverride
        m_UIModelCountOverride
        m_startupGameModelCapacity
        m_startupWorkerThreads
        m_leftSceneCycleWasDown
        m_lastEscapeTapTime
        m_solverReplayMismatch*
    }

    class RuntimeFiles_Before {
        Run.cpp
        RunFrame.cpp
        RunInput.cpp
        RunRender.cpp
        RunScene.cpp
        RunStress.cpp
        RunUiTextPass.cpp
        LauncherTools.cpp
    }

    RuntimeFiles_Before --> RunHeader_Before : include and depend on inline state layout
```

The pre-change problem was semantic density. `Run.h` mixed public launch
surface, composition-root ownership, render resource bundles, debug-only replay
probe data, launch policy, UI overrides, input latches, and scene-browser lists.
That made the header hard to scan and made unrelated runtime files appear to
share one flat bag of state.

## After Class Shape

```mermaid
classDiagram
    direction LR

    class Run_After {
        SceneController m_sceneController
        SceneRuntimeCoordinator m_sceneCoordinator
        RunSceneBrowserState m_sceneBrowser
        RunInputLatchState m_inputLatches
        RunLaunchOptions m_launchOptions
        RunStartupState m_startup
        RunSceneUIOverrideState m_sceneUIOverrides
        DiagnosticsRuntime m_diagnosticsRuntime
        RunRuntimeSettings m_runtimeSettings
        RunTimerState m_timers
        RunSubsystemState m_systems
        RunCameraState m_camera
        RunReplayMismatchState m_solverReplayMismatch
        RuntimeRenderHost m_renderHost
        RuntimeRenderer m_renderer
    }

    class RunState_h {
        TornadoVisualSettings
        RunRuntimeSettings
        RunTimerState
        RunSubsystemState
        RunCameraState
        RunLiveStyleControlState
        OverlayMode
        RunDebugState
        RunLaunchOptions
        RunSceneBrowserState
        RunSceneUIOverrideState
        RunStartupState
        RunInputLatchState
        RunReplayMismatchState
    }

    class RuntimeRenderResources_h {
        ReflectionPassResources
        SkyPassResources
        CinematicScenePassResources
        VolumetricLightPassResources
        TonemapPassResources
        FullscreenPassResources
        ShadowPassResources
        RunRenderPassResources
    }

    class RunReplayProbeState_h {
        RunReplayScrubProbeState
        RunReplayRestoreProbeState
        RunReplaySaveProbeState
    }

    class RuntimeRenderHost {
        RunSceneBrowserState& m_sceneBrowser
        RunRuntimeSettings& m_runtimeSettings
        callbacks into Run
    }

    Run_After --> RunState_h : owns grouped state types
    Run_After --> RunReplayProbeState_h : debug-only probe state
    RunState_h --> RuntimeRenderResources_h : RunSubsystemState.renderPasses
    RuntimeRenderHost --> RunState_h : borrows scene browser aggregate
```

## Architecture Flow

```mermaid
flowchart TB
    CLI["CLI / launch arguments"] --> LaunchOptions["RunLaunchOptions"]
    SceneUI["Scene tab overrides"] --> SceneOverrides["RunSceneUIOverrideState"]
    SceneBrowser["Scene discovery and cine selection"] --> SceneBrowserState["RunSceneBrowserState"]
    Input["cross-frame key state"] --> InputLatches["RunInputLatchState"]
    ReplayCheck["solver replay mismatch reporting"] --> ReplayMismatch["RunReplayMismatchState"]
    DebugBuild["debug replay probes"] --> ProbeState["RunReplayProbeState.h"]
    RenderPasses["render pass backend resources"] --> RenderResources["RuntimeRenderResources.h"]

    LaunchOptions --> Run["Run composition root"]
    SceneOverrides --> Run
    SceneBrowserState --> Run
    InputLatches --> Run
    ReplayMismatch --> Run
    ProbeState --> Run
    RenderResources --> Systems["RunSubsystemState"]
    Systems --> Run

    Run --> RenderHost["RuntimeRenderHost"]
    RenderHost -. borrows .-> SceneBrowserState
    Run --> Renderer["RuntimeRenderer"]

    classDef state fill:#e8f4ff,stroke:#2867a8,stroke-width:2px;
    classDef root fill:#e7f7ea,stroke:#2e7d32,stroke-width:2px;
    classDef bridge fill:#fff4cc,stroke:#8a6d00,stroke-dasharray: 4 3;
    class LaunchOptions,SceneOverrides,SceneBrowserState,InputLatches,ReplayMismatch,ProbeState,RenderResources,Systems state;
    class Run,Renderer root;
    class RenderHost bridge;
```

## What Actually Changed

| Area | Before | After |
|------|--------|-------|
| Header shape | `Run.h` contained 859 lines, including the facade, state structs, render resource structs, debug replay probe structs, and many loose state fields. | `Run.h` is 590 lines and delegates state declarations to `RunState.h`, `RunReplayProbeState.h`, and `RuntimeRenderResources.h`. |
| Render resources | Per-pass resource bundles lived in the generic runtime header even though they describe render-owned backend objects. | Render resource bundles live in `Runtime/Render/RuntimeRenderResources.h`; `RunSubsystemState` keeps the single `RunRenderPassResources renderPasses` aggregate. |
| CLI/session flags | Many `m_cmd*` booleans and values were private `Run` fields with shared naming but no aggregate owner. | They are grouped under `RunLaunchOptions m_launchOptions`, so launch policy is one named shelf. |
| Scene browser | Scene browser paths, display names, pointer cache, and selected cine index were separate fields. | They are grouped under `RunSceneBrowserState m_sceneBrowser`; `RuntimeRenderHost` now borrows that aggregate instead of two loose pointers. |
| UI scene overrides | Scene-tab numeric overrides were separate `m_UI*Override` fields. | They are grouped under `RunSceneUIOverrideState m_sceneUIOverrides`. |
| Startup defaults | Startup capacity and worker-thread defaults were loose fields. | They are grouped under `RunStartupState m_startup`. |
| Input latches | Cross-frame scene cycling and escape-tap latch state were loose fields. | They are grouped under `RunInputLatchState m_inputLatches`. |
| Replay mismatch throttling | The report count and suppression flag were loose fields. | They are grouped under `RunReplayMismatchState m_solverReplayMismatch`. |
| Debug replay probes | Debug-only scrub, restore, and save probe structs lived in `Run.h`. | They live in `RunReplayProbeState.h`. |
| Project metadata | New headers were invisible to Visual Studio filters until added. | The `.vcxproj`, `.filters`, and project-filter validator classify the new headers. |

## Honest Remaining Boundaries

- `Run.h` is still a large composition-root header. It now reads more like a
  shell plus member list, but it still owns top-level lifecycle, scene, render,
  UI, tools, diagnostics, physics, replay, and world references.
- `RunState.h` is a staging boundary, not a final home for every state object.
  Some structs can move again when narrower owners appear.
- `RuntimeRenderHost` remains a broad bridge from render code back to runtime
  state. This change narrows one bridge member by borrowing `RunSceneBrowserState`,
  but it does not eliminate the host pattern.
- The refactor is mostly semantic and mechanical. It should reduce friction for
  future extraction work, but it intentionally avoids behavior changes.

## Next Pull-Out Candidates

```mermaid
flowchart LR
    RunState["RunState.h"] --> Launch["RunLaunchOptions\ncandidate: RunLaunchOptions.h"]
    RunState --> Browser["RunSceneBrowserState\ncandidate: SceneBrowserState.h"]
    RunState --> Debug["RunDebugState and OverlayMode\ncandidate: RuntimeDebugState.h"]
    RunState --> RuntimeSettings["RunRuntimeSettings\ncandidate: RuntimeSettings.h"]
    RunState --> LiveStyle["RunLiveStyleControlState\ncandidate: LiveStyleControlState.h"]
    RuntimeRenderHost["RuntimeRenderHost"] --> HostBindings["RuntimeRenderHostBindings\ncandidate: smaller service structs"]

    classDef candidate fill:#fff4cc,stroke:#8a6d00,stroke-width:2px;
    class Launch,Browser,Debug,RuntimeSettings,LiveStyle,HostBindings candidate;
```

The next useful split is probably not another blind header shuffle. The best
next step would be to extract one behavior owner with its state, such as scene
browser selection or launch option application, then let the header split follow
that owner.
