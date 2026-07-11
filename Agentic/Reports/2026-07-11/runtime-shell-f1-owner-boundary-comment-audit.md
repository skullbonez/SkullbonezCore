# Runtime Shell F1 Owner-Boundary Comment Audit

Scope: source-bearing files changed by the F1 owner-boundary slice that deletes
`RunSubsystemState`, removes remaining forwarding surfaces, and promotes camera,
stress, automation, live-style, mouse-pickup, scene-completion, and render-resource
authority to concrete owners.

Checklist: 37 checked, 0 deferred, 0 unchecked.

- [x] `SkullbonezSource/Runtime/CaptureController.cpp`
- [x] `SkullbonezSource/Runtime/CaptureController.h`
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp`
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h`
- [x] `SkullbonezSource/Runtime/Editor/RunMousePickupTools.cpp`
- [x] `SkullbonezSource/Runtime/GraphicsStressController.h`
- [x] `SkullbonezSource/Runtime/Input.cpp`
- [x] `SkullbonezSource/Runtime/InputFrame.cpp`
- [x] `SkullbonezSource/Runtime/InputFrame.h`
- [x] `SkullbonezSource/Runtime/InputFrameExecution.cpp`
- [x] `SkullbonezSource/Runtime/InteractionAutomationController.cpp`
- [x] `SkullbonezSource/Runtime/InteractionAutomationController.h`
- [x] `SkullbonezSource/Runtime/LiveStyleController.cpp`
- [x] `SkullbonezSource/Runtime/LiveStyleController.h`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderer.h`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderHost.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRuntime.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRuntimeOwnerViews.h`
- [x] `SkullbonezSource/Runtime/Replay/RunReplayProbes.cpp`
- [x] `SkullbonezSource/Runtime/Run.cpp`
- [x] `SkullbonezSource/Runtime/Run.h`
- [x] `SkullbonezSource/Runtime/RunCameraState.cpp`
- [x] `SkullbonezSource/Runtime/RunCameraState.h`
- [x] `SkullbonezSource/Runtime/RunDemoDirector.h`
- [x] `SkullbonezSource/Runtime/RunFrame.cpp`
- [x] `SkullbonezSource/Runtime/RunRender.cpp`
- [x] `SkullbonezSource/Runtime/RunTimerState.h`
- [x] `SkullbonezSource/Runtime/RuntimeStressController.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeStressController.h`
- [x] `SkullbonezSource/Runtime/Scene/RunScene.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneController.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneController.h`
- [x] `SkullbonezSource/Runtime/Tools/RuntimeTools.h`
- [x] `SkullbonezSource/World/Terrain.cpp`
- [x] `tools/check_replay_prediction_determinism.py`
- [x] `tools/validate_project_filters.py`

Audit notes:

- New controller files carry complete learning headers and keep synchronous
  borrow lifetime and failure propagation beside the relevant boundaries.
- Stale compatibility wording was removed from diagnostics and scene-controller
  headers; the comments now describe the concrete owners and value-returning
  scene-completion contract.
- Mouse-pickup, camera-control, renderer-resource, and scene-gate mutations have
  nearby ownership, lifetime, hazard, or invariant comments where stale handles,
  backend resources, or automation results are non-obvious.
- No wording requires human approval. Repository validation is recorded in the
  owning plan after the commit-bound gates complete.
