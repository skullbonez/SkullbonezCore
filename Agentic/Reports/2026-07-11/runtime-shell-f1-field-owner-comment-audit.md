# Runtime Shell F1 Field-Owner Comment Audit

Scope: source-bearing files changed while moving replay capture scratch,
cinematic defaults, the frame-local UI view model, perf-pass state, and the
cross-scene pause policy out of `Run`/`RunDebugState` and into their domain owners.

Checklist: 22 checked, 0 deferred, 0 unchecked.

- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp`
- [x] `SkullbonezSource/Runtime/InputFrame.cpp`
- [x] `SkullbonezSource/Runtime/InputFrame.h`
- [x] `SkullbonezSource/Runtime/InputFrameExecution.cpp`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderHost.h`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h`
- [x] `SkullbonezSource/Runtime/RenderDefaultsStore.cpp`
- [x] `SkullbonezSource/Runtime/RenderDefaultsStore.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRuntime.h`
- [x] `SkullbonezSource/Runtime/Run.cpp`
- [x] `SkullbonezSource/Runtime/Run.h`
- [x] `SkullbonezSource/Runtime/RunDebugState.h`
- [x] `SkullbonezSource/Runtime/RunFrame.cpp`
- [x] `SkullbonezSource/Runtime/RunUiTextPass.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeStressController.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeStressController.h`
- [x] `SkullbonezSource/Runtime/Scene/RunScene.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneController.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneController.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneRequestExecution.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeCoordinator.cpp`

Audit notes:

- Replay scratch lifetime and reuse are documented beside the owning recorder state.
- Render-default comments distinguish the immutable startup baseline from deferred save intent.
- Cross-scene pause comments now identify `SceneController` as the persistent policy owner;
  `RunDebugState` documents presentation-only authority.
- Perf-pass and UI-view values are described at their scene/frame boundaries and no
  stale comment claims that the application shell owns them.
