# SceneController Round-2 S6 Comment Audit

Date: 2026-07-18
Scope: every source-bearing file changed by the S6 ownership-review remediation
Guide: `Agentic/Reference/comment-style-guide.md`
Result: 62 checked, 0 deferred, 0 unchecked

Each checked file has a complete learning header and was inspected for nearby
ownership, lifetime, invariant, hazard, and validation-sensitive comments. The
remediation added local lifetime comments at the single-`SceneWorld` editor,
replay-restore, replay-probe, automation-report, UI-text, runtime-tuning,
renderer-construction, launcher-repro, style, and navigation seams. Diagnostics
now explains its value-only entity-count and direct-physics boundaries; input
documents its scalar camera-capability facts. Attached-camera and camera-state
operations document why they derive camera and terrain subowners from one
borrowed `SceneWorld`; Director playback now documents the same single-world
rule for its paired pose and style mutations. Stale collection/controller
wording was updated where authority moved.

## Checklist

- [x] `SkullbonezSource/Runtime/AttachedCameraController.cpp`
- [x] `SkullbonezSource/Runtime/AttachedCameraController.h`
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp`
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h`
- [x] `SkullbonezSource/Runtime/Editor/EditorOverlayTools.h`
- [x] `SkullbonezSource/Runtime/Editor/EditorTools.cpp`
- [x] `SkullbonezSource/Runtime/Editor/EditorTools.h`
- [x] `SkullbonezSource/Runtime/Editor/LauncherTools.cpp`
- [x] `SkullbonezSource/Runtime/Editor/RunEditorGizmoTools.cpp`
- [x] `SkullbonezSource/Runtime/Editor/RunEditorHistory.cpp`
- [x] `SkullbonezSource/Runtime/Editor/RunEditorObjectPlacement.cpp`
- [x] `SkullbonezSource/Runtime/Editor/RunEditorOverlayTools.cpp`
- [x] `SkullbonezSource/Runtime/Editor/RunEditorTools.cpp`
- [x] `SkullbonezSource/Runtime/Editor/RunMousePickupTools.cpp`
- [x] `SkullbonezSource/Runtime/InputFrame.cpp`
- [x] `SkullbonezSource/Runtime/InputFrame.h`
- [x] `SkullbonezSource/Runtime/InputFrameExecution.cpp`
- [x] `SkullbonezSource/Runtime/InteractionAutomationController.cpp`
- [x] `SkullbonezSource/Runtime/InteractionAutomationReportWriter.cpp`
- [x] `SkullbonezSource/Runtime/InteractionAutomationReportWriter.h`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderer.h`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderHost.h`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayCoordination.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRestoreService.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayValidation.Internal.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayValidation.Probes.cpp`
- [x] `SkullbonezSource/Runtime/Replay/ReplayValidation.cpp`
- [x] `SkullbonezSource/Runtime/Run.cpp`
- [x] `SkullbonezSource/Runtime/RunCameraState.cpp`
- [x] `SkullbonezSource/Runtime/RunCameraState.h`
- [x] `SkullbonezSource/Runtime/RunDemoDirector.cpp`
- [x] `SkullbonezSource/Runtime/RunDemoDirector.h`
- [x] `SkullbonezSource/Runtime/RunFrame.cpp`
- [x] `SkullbonezSource/Runtime/RunInput.cpp`
- [x] `SkullbonezSource/Runtime/RunRender.cpp`
- [x] `SkullbonezSource/Runtime/RunUiTextPass.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeDiagnostics.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeDiagnostics.h`
- [x] `SkullbonezSource/Runtime/RuntimeStressController.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeTuning.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeTuning.h`
- [x] `SkullbonezSource/Runtime/RuntimeViewModel.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeViewModel.h`
- [x] `SkullbonezSource/Runtime/Scene/RunScene.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneController.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneControllerState.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneNavigationModel.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRequestExecution.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRequestQueue.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRequestQueue.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntime.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeCreate.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeCreate.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeLoad.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeLoad.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeStyle.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneRuntimeStyle.h`
- [x] `SkullbonezSource/Runtime/Tools/RuntimeTools.cpp`
- [x] `SkullbonezSource/Runtime/Tools/RuntimeTools.h`
- [x] `SkullbonezTests/TestOwnerRequestQueues.cpp`

## Validation Classification

This audit accompanies behavior and ownership changes, so the S6 mapped code
gates remain required. It is not a comment-only validation exemption.
