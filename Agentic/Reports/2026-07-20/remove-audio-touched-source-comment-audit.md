# Remove-audio touched-source comment audit

Date: 2026-07-20
Guide: `Agentic/Reference/comment-style-guide.md`
Audit skill: `Agentic/Skills/comment-style-audit/skill.md`

This is the required touched-file audit for the audio-subsystem removal. Each
surviving edited source-bearing file was checked for its learning header and
for stale or newly misleading local ownership, lifetime, invariant, and hazard
comments. Deleted source files were reviewed through the deletion diff and do
not require surviving headers.

Result: **64 checked, 0 deferred, 0 unchecked**.

## Core, physics, rendering, and scene schema

- [x] `SkullbonezSource/Core/Config.cpp`
- [x] `SkullbonezSource/Core/Config.h`
- [x] `SkullbonezSource/Physics/ColliderStore.h`
- [x] `SkullbonezSource/Physics/PhysicsDebugData.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h`
- [x] `SkullbonezSource/Rendering/RenderInstanceRenderer.cpp`
- [x] `SkullbonezSource/Rendering/RenderInstanceStore.cpp`
- [x] `SkullbonezSource/Rendering/RenderInstanceStore.h`
- [x] `SkullbonezSource/Rendering/RenderMaterial.h`
- [x] `SkullbonezSource/Scene/AuthoredScene.h`
- [x] `SkullbonezSource/Scene/AuthoredSceneParserSchema.h`

## Runtime

- [x] `SkullbonezSource/Runtime/AttachedCameraController.cpp`
- [x] `SkullbonezSource/Runtime/AttachedCameraController.h`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.cpp`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.h`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.h`
- [x] `SkullbonezSource/Runtime/Init.cpp`
- [x] `SkullbonezSource/Runtime/InputController.cpp`
- [x] `SkullbonezSource/Runtime/InputController.h`
- [x] `SkullbonezSource/Runtime/InputFrame.cpp`
- [x] `SkullbonezSource/Runtime/InputFrame.h`
- [x] `SkullbonezSource/Runtime/InputFrameExecution.cpp`
- [x] `SkullbonezSource/Runtime/OperatorCommandApplier.cpp`
- [x] `SkullbonezSource/Runtime/OperatorCommandApplier.h`
- [x] `SkullbonezSource/Runtime/Render/RenderPresentationSettings.h`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderer.h`
- [x] `SkullbonezSource/Runtime/Run.cpp`
- [x] `SkullbonezSource/Runtime/Run.h`
- [x] `SkullbonezSource/Runtime/RunFrame.cpp`
- [x] `SkullbonezSource/Runtime/RunLaunchOptions.h`
- [x] `SkullbonezSource/Runtime/RunTimerState.h`
- [x] `SkullbonezSource/Runtime/RuntimeDiagnostics.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeDiagnostics.h`
- [x] `SkullbonezSource/Runtime/RuntimeFrameViews.h`
- [x] `SkullbonezSource/Runtime/RuntimeStressController.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeStressController.h`
- [x] `SkullbonezSource/Runtime/RuntimeViewModel.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeViewModel.h`
- [x] `SkullbonezSource/Runtime/Scene/RunScene.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneController.h`
- [x] `SkullbonezSource/Runtime/Startup/StartupCommandLine.cpp`
- [x] `SkullbonezSource/Runtime/Startup/StartupCommandLine.h`
- [x] `SkullbonezSource/Runtime/Startup/StartupLaunchResolution.cpp`
- [x] `SkullbonezSource/Runtime/Startup/StartupProbeHarnesses.cpp`
- [x] `SkullbonezSource/Runtime/Startup/StartupProbeHarnesses.h`
- [x] `SkullbonezSource/Runtime/UiTextPass.cpp`

## UI exchange and legacy UI

- [x] `SkullbonezSource/UI/OperatorEditorExchange.cpp`
- [x] `SkullbonezSource/UI/OperatorEditorExchange.h`
- [x] `SkullbonezSource/UI/UI.cpp`
- [x] `SkullbonezSource/UI/UI.h`
- [x] `SkullbonezSource/UI/UICommands.h`
- [x] `SkullbonezSource/UI/UIFrameComposition.cpp`
- [x] `SkullbonezSource/UI/UIWindowInteractionOwner.cpp`
- [x] `SkullbonezSource/UI/UIWindowInteractionOwner.h`

## Tests and tools

- [x] `SkullbonezTests/TestConfig.cpp`
- [x] `SkullbonezTests/TestOwnerRequestQueues.cpp`
- [x] `SkullbonezTests/TestSceneEntityStore.cpp`
- [x] `SkullbonezTests/TestStartup.cpp`
- [x] `tools/check_physics_query_regression.py`
- [x] `tools/migrate_data_formats.py`
- [x] `tools/physics_query.py`
- [x] `tools/validate_perf.bat`
- [x] `tools/validate_project_filters.py`

## Findings

- Removed stale audio ownership language from physics diagnostics, render
  presentation, contact-material, frame-timer, camera, and scene-load comments.
- Updated the ImGui topology vocabulary from the retired combined render/audio
  panel to the surviving rendering panel.
- The only surviving contact-audio source reference is the explicit config v4
  to v5 deletion migration; its owner, reason, and version boundary are stated
  beside the migration and covered by migration self-tests.
