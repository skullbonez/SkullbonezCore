/*
File: SkullbonezSource/Runtime/App/SceneLoadApplication.h
Purpose:
  Declares App-owned application of detached scene-load results.

Summary:
  SceneLoadTransaction publishes immutable load facts and a phase cursor. App
  applies those facts synchronously to runtime siblings, Replay composition,
  the native host, operator UI, rendering device, and validation owners.

Invariants:
  - Scene retains no pointer or reference to any reaction or presentation owner.
  - Runtime reactions complete before presentation begins.
  - The detached result remains owned by the stack-scoped transaction.

Related:
  - SkullbonezSource/Runtime/Scene/SceneLoadTransaction.h
  - SkullbonezSource/Runtime/App/InputFrameExecution.cpp
  - SkullbonezSource/Runtime/App/RunFrame.cpp
*/
#pragma once

namespace SkullbonezCore
{
namespace Rendering
{
class Dx12RenderDevice;
}
namespace UI
{
class InGameUI;
}
namespace Runtime
{
class AttachedCameraController;
struct CameraControlState;
class InputRouter;
class ReplayRuntime;
class RuntimeInteractionController;
class RuntimeOverlayDiagnostics;
class RuntimeTools;
class RuntimeValidationHarness;
class SceneController;
class SceneLoadTransaction;
class Window;
struct RunLaunchOptions;

void ApplySceneLoadRuntimeReactions( SceneLoadTransaction& transaction, const RunLaunchOptions& launchOptions,
                                     RuntimeOverlayDiagnostics& overlays, SceneController& sceneController,
                                     InputRouter& inputRouter, RuntimeInteractionController& interaction,
                                     CameraControlState& camera, AttachedCameraController& attachedCamera,
                                     RuntimeTools& runtimeTools, ReplayRuntime& replayRuntime );

void ApplySceneLoadPresentation( SceneLoadTransaction& transaction, Window& window, UI::InGameUI& operatorUi,
                                 RuntimeValidationHarness& validationHarness, const RunLaunchOptions& launchOptions,
                                 Rendering::Dx12RenderDevice* renderDevice, bool rendererVsyncEnabled,
                                 SceneController& sceneController );
} // namespace Runtime
} // namespace SkullbonezCore
