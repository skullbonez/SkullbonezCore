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
  - A startup-failure report may publish before the first frame and retains the
    earlier process diagnostic as its result.

Related:
  - SkullbonezSource/Runtime/App/InteractionAutomationApplication.cpp
  - SkullbonezSource/Runtime/Automation/InteractionAutomationController.h
*/
#pragma once

#include "../Automation/InteractionAutomationController.h"
#include "../../Core/SbResult.h"

#include <algorithm>
#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
class SbDiagnosticStore;
}
namespace Rendering
{
class Dx12BackbufferCapture;
struct RenderSceneSnapshot;
} // namespace Rendering
namespace UI
{
class InGameUI;
}
namespace Runtime
{
class ApplicationExitState;
class CaptureController;
class EditorToolsOwner;
class InteractionAutomationRecorder;
class InputRouter;
class RuntimeInteractionController;
class RuntimeTools;
class SceneController;
class Window;
struct CameraControlState;
struct ContinuousOrbitalForecastView;

using InteractionRecordingBoundaryOperation = void ( * )( void* context );
using InteractionAutomationReportExitOperation = SkullbonezCore::Core::SbResult ( * )(
    void* context, InteractionAutomationRunStatus& status );

// Production and focused tests share the exact structural-admission boundaries
// and stable authored-order sort used by the first automation turn.
inline bool AdmitInteractionAutomationScriptRoot( InteractionAutomationController& state, bool rootIsObject )
{
    if ( rootIsObject )
    {
        return true;
    }

    state.status.Fail( "interaction script root must be an object" );
    return false;
}

inline bool AdmitInteractionRecordingBaselineContainers( InteractionAutomationController& state, bool cameraIsObject,
                                                          bool interactionIsObject, bool toolsIsObject,
                                                          bool uiIsObject, bool replayIsObject,
                                                          bool causeInspectionIsObject )
{
    if ( cameraIsObject && interactionIsObject && toolsIsObject && uiIsObject && replayIsObject &&
         causeInspectionIsObject )
    {
        return true;
    }

    state.status.Fail( "recorded manifest baseline state is incomplete or invalid" );
    return false;
}

inline bool AdmitInteractionAutomationPressKeyOptions( bool pressKeyIsString, bool controlTypeIsValid,
                                                        bool holdFramesTypeIsValid, std::string& outError )
{
    if ( pressKeyIsString && controlTypeIsValid && holdFramesTypeIsValid )
    {
        return true;
    }

    outError = "pressKey requires a string key, optional boolean control, and optional integer holdFrames";
    return false;
}

inline void SortInteractionAutomationActions( std::vector<RunInteractionAutomationAction>& actions )
{
    // Invariant: frame grouping never changes the authored order within a turn;
    // order-dependent commands therefore replay exactly as serialized.
    std::stable_sort( actions.begin(), actions.end(),
                      []( const RunInteractionAutomationAction& lhs,
                          const RunInteractionAutomationAction& rhs ) { return lhs.frame < rhs.frame; } );
}

// Finalizes the required report without replacing an earlier process failure.
// Run supplies the concrete owner-composition operation; tests can exercise the
// same precedence boundary with the writer's atomic publication seam.
inline SkullbonezCore::Core::SbResult ResolveInteractionAutomationReportForExit(
    InteractionAutomationController& state, const SkullbonezCore::Core::SbResult& processStatus,
    InteractionAutomationReportExitOperation writeReport, void* writeContext )
{
    if ( !state.enabled || state.reportWriter.Written() )
    {
        return processStatus;
    }

    if ( !processStatus.Ok() && !state.status.failed )
    {
        // Lifetime: InteractionAutomationRunStatus copies the diagnostic text;
        // the report never retains the process result's diagnostic lease.
        state.status.Fail( processStatus.ErrorMessage() );
    }

    if ( !writeReport )
    {
        return processStatus.Ok()
                   ? state.resultDiagnostics.Failure( "InteractionAutomation",
                                                      "required interaction report operation is unavailable" )
                   : processStatus;
    }

    const SkullbonezCore::Core::SbResult reportStatus = writeReport( writeContext, state.status );
    return processStatus.Ok() ? reportStatus : processStatus;
}

// Captures an armed baseline, then converts an active recorder's final save
// result into the process-owned exit state before Run::Execute returns.
SkullbonezCore::Core::SbResult ResolveRunExitAfterInteractionRecording(
    InteractionAutomationRecorder& recorder, SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
    ApplicationExitState& applicationExit, int messageExitCode,
    InteractionRecordingBoundaryOperation captureArmedBoundary, void* captureContext );

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
InteractionAutomationDevelopmentUiApplyResult
ApplyInteractionAutomationDevelopmentUiCommands( const InteractionAutomationController& state,
                                                 const InteractionAutomationFrameResult& frame, Window& window,
                                                 DevelopmentTools::ImGuiEditorOwner& editor );
#endif

InteractionAutomationFrameResult TickInteractionAutomationBeforeInput( InteractionAutomationController& state, Window& window, const SkullbonezCore::Core::EngineConfig& config,
                                                                       SceneController& scene, const RuntimeFrameMetricsSnapshot& timers, CameraControlState& camera, InputRouter& inputRouter,
                                                                       RuntimeInteractionController& interaction, EditorToolsOwner& editorTools, RuntimeTools& runtimeTools,
                                                                       SkullbonezCore::UI::InGameUI& ui, const ReplayAutomationView& replayView,
                                                                       const Rendering::RenderSceneSnapshot& renderSnapshot );

InteractionAutomationFrameResult TickInteractionAutomationAfterRender( InteractionAutomationController& state, EditorToolsOwner& editorTools, RuntimeTools& runtimeTools,
                                                                       RuntimeInteractionController& interaction, InputRouter& inputRouter, CameraControlState& camera,
                                                                       SkullbonezCore::UI::InGameUI& ui, SceneController& scene, const ReplayAutomationView& replayView,
                                                                       const InteractionAutomationDevelopmentUiView& developmentUiView, const ContinuousOrbitalForecastView& forecastView,
                                                                       const Rendering::RenderSceneSnapshot& renderSnapshot, CaptureController& capture,
                                                                       Rendering::Dx12BackbufferCapture& backbufferCapture );
} // namespace Runtime
} // namespace SkullbonezCore
