/*
File: SkullbonezSource/Runtime/App/InteractionAutomationApplication.h
Purpose:
  Declares App-owned interaction-automation composition operations.

Summary:
  Automation owns script state, input publication, and report evidence. App
  synchronously joins that state with concrete lower owners, applies the fixed
  command results, and releases every borrow before the frame phase returns.

Invariants:
  - No concrete lower owner is retained by Automation.
  - Before-input effects complete before normal input routing.
  - After-render capture and report effects observe the submitted frame.

Related:
  - SkullbonezSource/Runtime/App/InteractionAutomationApplication.cpp
  - SkullbonezSource/Runtime/Automation/InteractionAutomationController.h
*/
#pragma once

#include "../Automation/InteractionAutomationController.h"

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
}
namespace Rendering
{
class Dx12BackbufferCapture;
struct RenderSceneSnapshot;
}
namespace UI
{
class InGameUI;
}
namespace Runtime
{
class CaptureController;
class EditorToolsOwner;
class InputRouter;
class RuntimeInteractionController;
class RuntimeTools;
class SceneController;
class Window;
struct CameraControlState;
struct ContinuousOrbitalForecastView;

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
InteractionAutomationDevelopmentUiApplyResult
ApplyInteractionAutomationDevelopmentUiCommands( const InteractionAutomationController& state,
                                                  const InteractionAutomationFrameResult& frame, Window& window,
                                                  DevelopmentTools::ImGuiEditorOwner& editor );
#endif

InteractionAutomationFrameResult
TickInteractionAutomationBeforeInput( InteractionAutomationController& state, Window& window,
                                      const SkullbonezCore::Core::EngineConfig& config, SceneController& scene,
                                      const RuntimeFrameMetricsSnapshot& timers, CameraControlState& camera,
                                      InputRouter& inputRouter, RuntimeInteractionController& interaction,
                                      EditorToolsOwner& editorTools, RuntimeTools& runtimeTools,
                                      SkullbonezCore::UI::InGameUI& ui, const ReplayAutomationView& replayView,
                                      const Rendering::RenderSceneSnapshot& renderSnapshot );

InteractionAutomationFrameResult
TickInteractionAutomationAfterRender( InteractionAutomationController& state, EditorToolsOwner& editorTools,
                                      RuntimeTools& runtimeTools, RuntimeInteractionController& interaction,
                                      InputRouter& inputRouter, CameraControlState& camera,
                                      SkullbonezCore::UI::InGameUI& ui, SceneController& scene,
                                      const ReplayAutomationView& replayView,
                                      const InteractionAutomationDevelopmentUiView& developmentUiView,
                                      const ContinuousOrbitalForecastView& forecastView,
                                      const Rendering::RenderSceneSnapshot& renderSnapshot, CaptureController& capture,
                                      Rendering::Dx12BackbufferCapture& backbufferCapture );
} // namespace Runtime
} // namespace SkullbonezCore
