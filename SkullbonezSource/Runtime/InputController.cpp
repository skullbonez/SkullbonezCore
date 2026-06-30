/*
File: SkullbonezSource/Runtime/InputController.cpp
Purpose:
  Converts raw runtime input state into edge-triggered commands and camera deltas.

Mental model:
  This layer is intentionally narrow: it does not apply gameplay commands, it
  only normalizes keyboard/mouse edges for the runtime.

Glossary:
  Input edge: Transition from not pressed to pressed, used for one-shot
  commands.
  Mouse look: Camera mode where relative mouse movement rotates the view.
  Runtime command: Normalized input event consumed later by Run.

Invariants:
  - CaptureActionPress must be called once per frame for each action whose edge
    is needed; the context stores previous down state.
  - UI/focus blocking policy is resolved before commands are emitted so later
    Run code can stay command-oriented.

Related:
  - SkullbonezSource/Runtime/InputController.h
  - SkullbonezSource/Runtime/RunInput.cpp
*/
#include "InputController.h"

#include "RunInternal.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace SkullbonezCore
{
namespace Basics
{
namespace
{
std::size_t ActionIndex( RuntimeInputAction action )
{
    return static_cast<std::size_t>( action );
}

bool IsActionMemoryValid( RuntimeInputAction action )
{
    return action != RuntimeInputAction::None && action != RuntimeInputAction::Count;
}
} // namespace

RuntimeInputContext::RuntimeInputContext()
{
    m_actionDown.fill( false );
}

void RuntimeInputContext::BeginFrame( bool appFocused, bool uiBlocksKeyboard, bool uiBlocksMouse )
{
    m_appFocused = appFocused;
    m_uiBlocksKeyboard = uiBlocksKeyboard;
    m_uiBlocksMouse = uiBlocksMouse;
}

void RuntimeInputContext::ResetEdges()
{
    m_actionDown.fill( false );
    ResetMouseButtons();
}

void RuntimeInputContext::ResetMouseButtons()
{
    SyncMouseButtons( false, false );
}

void RuntimeInputContext::SyncMouseButtons( bool leftDown, bool rightDown )
{
    m_leftMouseWasDown = leftDown;
    m_rightMouseWasDown = rightDown;
}

bool RuntimeInputContext::CaptureActionPress( RuntimeInputAction action, int virtualKey )
{
    // Concept: this is edge detection, not command execution. Run decides what
    // the normalized action means after all UI/focus gates have been applied.
    const bool isDown = Hardware::Input::IsKeyDown( virtualKey );
    if ( !IsActionMemoryValid( action ) )
    {
        return false;
    }

    const std::size_t index = ActionIndex( action );
    const bool wasPressed = isDown && !m_actionDown[index];
    m_actionDown[index] = isDown;
    return wasPressed;
}

void RuntimeInputContext::SetActionDown( RuntimeInputAction action, bool isDown )
{
    if ( !IsActionMemoryValid( action ) )
    {
        return;
    }

    m_actionDown[ActionIndex( action )] = isDown;
}

RuntimeMouseEdges RuntimeInputContext::CaptureMouseButtons( bool leftDown, bool rightDown )
{
    RuntimeMouseEdges edges;
    edges.leftDown = leftDown;
    edges.leftPressed = leftDown && !m_leftMouseWasDown;
    edges.leftReleased = !leftDown && m_leftMouseWasDown;
    edges.rightDown = rightDown;
    edges.rightPressed = rightDown && !m_rightMouseWasDown;
    edges.rightReleased = !rightDown && m_rightMouseWasDown;
    m_leftMouseWasDown = leftDown;
    m_rightMouseWasDown = rightDown;
    return edges;
}

void RuntimeInputContext::SetMode( RuntimeInputMode mode, RuntimeInputAction action, RuntimeInputActionSource source )
{
    if ( mode == m_currentMode )
    {
        return;
    }

    const RuntimeInputTransition transition = { m_currentMode, mode, action, source };
    m_previousMode = m_currentMode;
    m_currentMode = mode;
    m_transitions[m_transitionWriteIndex] = transition;
    m_transitionWriteIndex = ( m_transitionWriteIndex + 1 ) % TRANSITION_HISTORY_COUNT;
    if ( m_transitionCount < TRANSITION_HISTORY_COUNT )
    {
        ++m_transitionCount;
    }
    SyncMouseButtons( Hardware::Input::IsLeftMouseDown(), Hardware::Input::IsRightMouseDown() );
}

RuntimeInputMode RuntimeInputContext::CurrentMode() const
{
    return m_currentMode;
}

RuntimeInputMode RuntimeInputContext::PreviousMode() const
{
    return m_previousMode;
}

bool RuntimeInputContext::AppFocused() const
{
    return m_appFocused;
}

bool RuntimeInputContext::UIBlocksKeyboard() const
{
    return m_uiBlocksKeyboard;
}

bool RuntimeInputContext::UIBlocksMouse() const
{
    return m_uiBlocksMouse;
}

int RuntimeInputContext::TransitionCount() const
{
    return m_transitionCount;
}

RuntimeInputTransition RuntimeInputContext::TransitionAt( int historyIndex ) const
{
    if ( m_transitionCount <= 0 )
    {
        return {};
    }

    historyIndex = std::clamp( historyIndex, 0, m_transitionCount - 1 );
    const int oldestIndex =
        ( m_transitionWriteIndex + TRANSITION_HISTORY_COUNT - m_transitionCount ) % TRANSITION_HISTORY_COUNT;
    const int index = ( oldestIndex + historyIndex ) % TRANSITION_HISTORY_COUNT;
    return m_transitions[index];
}

RuntimeKeyEdge
InputController::CaptureKeyEdge( Hardware::InputState& state, Hardware::InputState::Key memoryKey, int virtualKey )
{
    const bool isDown = Hardware::Input::IsKeyDown( virtualKey );
    const bool wasPressed = isDown && !state.Get( memoryKey );
    state.Set( memoryKey, isDown );
    return { isDown, wasPressed };
}

bool InputController::CaptureKeyPress( bool& wasDown, int virtualKey )
{
    const bool isDown = Hardware::Input::IsKeyDown( virtualKey );
    const bool wasPressed = isDown && !wasDown;
    wasDown = isDown;
    return wasPressed;
}

void InputController::BeginFrame( RuntimeInputContext& context,
                                  const RuntimeInputModeState& modeState,
                                  bool appFocused,
                                  bool uiBlocksKeyboard,
                                  bool uiBlocksMouse )
{
    context.BeginFrame( appFocused, uiBlocksKeyboard, uiBlocksMouse );
    context.SetMode( ResolveMode( modeState ),
                     RuntimeInputAction::None,
                     appFocused ? RuntimeInputActionSource::Runtime : RuntimeInputActionSource::FocusLost );
}

bool InputController::CaptureKeyboardActionPress( RuntimeInputContext& context,
                                                  RuntimeInputAction action,
                                                  int virtualKey )
{
    return context.CaptureActionPress( action, virtualKey );
}

void InputController::ApplyModeAction( RuntimeInputContext& context,
                                       RuntimeInputMode mode,
                                       RuntimeInputAction action,
                                       RuntimeInputActionSource source )
{
    context.SetMode( mode, action, source );
}

RuntimeInputMode InputController::ResolveMode( const RuntimeInputModeState& state )
{
    if ( state.editor )
    {
        if ( state.editorViewportLook )
        {
            return RuntimeInputMode::EditorViewportLook;
        }
        if ( state.editorPlacementScale )
        {
            return RuntimeInputMode::EditorPlaceScale;
        }
        if ( state.editorGizmoDrag )
        {
            if ( state.editorGizmoScale )
            {
                return RuntimeInputMode::EditorGizmoScale;
            }
            if ( state.editorGizmoRotation )
            {
                return RuntimeInputMode::EditorGizmoRotate;
            }
            return RuntimeInputMode::EditorGizmoTranslate;
        }
        if ( state.editorPlacement )
        {
            return RuntimeInputMode::EditorPlace;
        }
        return RuntimeInputMode::EditorGizmo;
    }

    if ( state.manipulator )
    {
        return RuntimeInputMode::Manipulator;
    }
    if ( state.launcher )
    {
        return RuntimeInputMode::Launcher;
    }
    if ( state.flyCamera )
    {
        return RuntimeInputMode::FlyCamera;
    }
    return RuntimeInputMode::Scene;
}

const char* InputController::DescribeMode( RuntimeInputMode mode )
{
    switch ( mode )
    {
    case RuntimeInputMode::Scene:
        return "Scene";
    case RuntimeInputMode::FlyCamera:
        return "Fly Camera";
    case RuntimeInputMode::Launcher:
        return "Launcher";
    case RuntimeInputMode::Manipulator:
        return "Manipulator";
    case RuntimeInputMode::EditorPlace:
        return "Editor Place";
    case RuntimeInputMode::EditorGizmo:
        return "Editor Gizmo";
    case RuntimeInputMode::EditorViewportLook:
        return "Editor Viewport Look";
    case RuntimeInputMode::EditorPlaceScale:
        return "Editor Place Scale";
    case RuntimeInputMode::EditorGizmoTranslate:
        return "Editor Gizmo Translate";
    case RuntimeInputMode::EditorGizmoRotate:
        return "Editor Gizmo Rotate";
    case RuntimeInputMode::EditorGizmoScale:
        return "Editor Gizmo Scale";
    default:
        return "Unknown";
    }
}

const char* InputController::DescribeAction( RuntimeInputAction action )
{
    switch ( action )
    {
    case RuntimeInputAction::None:
        return "None";
    case RuntimeInputAction::ToggleFlyCamera:
        return "ToggleFlyCamera";
    case RuntimeInputAction::ToggleLauncher:
        return "ToggleLauncher";
    case RuntimeInputAction::CycleCameraMode:
        return "CycleCameraMode";
    case RuntimeInputAction::SetCameraMode:
        return "SetCameraMode";
    case RuntimeInputAction::CycleAttachedCameraSubmode:
        return "CycleAttachedCameraSubmode";
    case RuntimeInputAction::ToggleAttachedCameraPin:
        return "ToggleAttachedCameraPin";
    case RuntimeInputAction::ToggleEditor:
        return "ToggleEditor";
    case RuntimeInputAction::ToggleEditorTool:
        return "ToggleEditorTool";
    case RuntimeInputAction::CycleEditorPlacementType:
        return "CycleEditorPlacementType";
    case RuntimeInputAction::ToggleEditorStaticPlacement:
        return "ToggleEditorStaticPlacement";
    case RuntimeInputAction::ToggleEditorTerrainAlign:
        return "ToggleEditorTerrainAlign";
    case RuntimeInputAction::BeginEditorViewportLook:
        return "BeginEditorViewportLook";
    case RuntimeInputAction::EndEditorViewportLook:
        return "EndEditorViewportLook";
    case RuntimeInputAction::BeginEditorPlacementScale:
        return "BeginEditorPlacementScale";
    case RuntimeInputAction::EndEditorPlacementScale:
        return "EndEditorPlacementScale";
    case RuntimeInputAction::BeginEditorGizmoTranslate:
        return "BeginEditorGizmoTranslate";
    case RuntimeInputAction::BeginEditorGizmoRotate:
        return "BeginEditorGizmoRotate";
    case RuntimeInputAction::BeginEditorGizmoScale:
        return "BeginEditorGizmoScale";
    case RuntimeInputAction::EndEditorGizmoDrag:
        return "EndEditorGizmoDrag";
    case RuntimeInputAction::CycleLauncherFireMode:
        return "CycleLauncherFireMode";
    case RuntimeInputAction::FireLauncher:
        return "FireLauncher";
    case RuntimeInputAction::WriteLauncherReproSnapshot:
        return "WriteLauncherReproSnapshot";
    case RuntimeInputAction::ToggleWaterFreeze:
        return "ToggleWaterFreeze";
    case RuntimeInputAction::CycleWaterReflection:
        return "CycleWaterReflection";
    case RuntimeInputAction::ToggleWaterFlat:
        return "ToggleWaterFlat";
    case RuntimeInputAction::ToggleTerrainHidden:
        return "ToggleTerrainHidden";
    case RuntimeInputAction::ToggleWaterHidden:
        return "ToggleWaterHidden";
    case RuntimeInputAction::ToggleCollisionVisualizer:
        return "ToggleCollisionVisualizer";
    case RuntimeInputAction::CyclePhysicsDebugOverlay:
        return "CyclePhysicsDebugOverlay";
    case RuntimeInputAction::ToggleTerrainContactProbe:
        return "ToggleTerrainContactProbe";
    case RuntimeInputAction::StepPhysicsPipelinePrevious:
        return "StepPhysicsPipelinePrevious";
    case RuntimeInputAction::StepPhysicsPipelineNext:
        return "StepPhysicsPipelineNext";
    case RuntimeInputAction::TogglePhysicsDebugTransparent:
        return "TogglePhysicsDebugTransparent";
    case RuntimeInputAction::ReportRendererRuntimeRetired:
        return "ReportRendererRuntimeRetired";
    case RuntimeInputAction::ToggleBroadphaseOverlay:
        return "ToggleBroadphaseOverlay";
    case RuntimeInputAction::ToggleUIVisibility:
        return "ToggleUIVisibility";
    case RuntimeInputAction::TogglePerformanceHistogram:
        return "TogglePerformanceHistogram";
    case RuntimeInputAction::NavigateScenePrevious:
        return "NavigateScenePrevious";
    case RuntimeInputAction::NavigateSceneNext:
        return "NavigateSceneNext";
    case RuntimeInputAction::DismissOrExitUI:
        return "DismissOrExitUI";
    case RuntimeInputAction::SaveSceneSnapshot:
        return "SaveSceneSnapshot";
    case RuntimeInputAction::SaveScreenshot:
        return "SaveScreenshot";
    case RuntimeInputAction::ResetScene:
        return "ResetScene";
    case RuntimeInputAction::ResetSceneFromBackspace:
        return "ResetSceneFromBackspace";
    case RuntimeInputAction::ResetSceneDefaults:
        return "ResetSceneDefaults";
    case RuntimeInputAction::LoadDemoScene:
        return "LoadDemoScene";
    case RuntimeInputAction::SaveSceneDefaults:
        return "SaveSceneDefaults";
    case RuntimeInputAction::CreateScene:
        return "CreateScene";
    case RuntimeInputAction::SelectScene:
        return "SelectScene";
    case RuntimeInputAction::ToggleVsync:
        return "ToggleVsync";
    case RuntimeInputAction::TogglePhysicsSleepPolicy:
        return "TogglePhysicsSleepPolicy";
    case RuntimeInputAction::TogglePhysicsDebugFlags:
        return "TogglePhysicsDebugFlags";
    case RuntimeInputAction::ToggleTornado:
        return "ToggleTornado";
    case RuntimeInputAction::ToggleTornadoVisualShell:
        return "ToggleTornadoVisualShell";
    case RuntimeInputAction::ToggleTornadoFieldVectors:
        return "ToggleTornadoFieldVectors";
    case RuntimeInputAction::ToggleRayCastVisualization:
        return "ToggleRayCastVisualization";
    case RuntimeInputAction::ApplyTornadoSettings:
        return "ApplyTornadoSettings";
    case RuntimeInputAction::ToggleTextOnly:
        return "ToggleTextOnly";
    case RuntimeInputAction::ToggleFixedStep:
        return "ToggleFixedStep";
    case RuntimeInputAction::ToggleShadows:
        return "ToggleShadows";
    case RuntimeInputAction::SetTimeScale:
        return "SetTimeScale";
    case RuntimeInputAction::SetRunSeed:
        return "SetRunSeed";
    case RuntimeInputAction::SetPhysicsDebugAlpha:
        return "SetPhysicsDebugAlpha";
    case RuntimeInputAction::SetPhysicsDebugContactLinger:
        return "SetPhysicsDebugContactLinger";
    case RuntimeInputAction::SetRayCastImpulseStrength:
        return "SetRayCastImpulseStrength";
    case RuntimeInputAction::SetLauncherProjectileSpeed:
        return "SetLauncherProjectileSpeed";
    case RuntimeInputAction::ApplyPhysicsFrictionSettings:
        return "ApplyPhysicsFrictionSettings";
    case RuntimeInputAction::SetModelCount:
        return "SetModelCount";
    case RuntimeInputAction::SetWorkerThreads:
        return "SetWorkerThreads";
    case RuntimeInputAction::SetSolverCounts:
        return "SetSolverCounts";
    case RuntimeInputAction::ToggleWaterReflection:
        return "ToggleWaterReflection";
    case RuntimeInputAction::SetWaterReflectionMode:
        return "SetWaterReflectionMode";
    case RuntimeInputAction::ApplyWorldWaterSettings:
        return "ApplyWorldWaterSettings";
    case RuntimeInputAction::ToggleRenderShadows:
        return "ToggleRenderShadows";
    case RuntimeInputAction::SaveRenderDefaults:
        return "SaveRenderDefaults";
    case RuntimeInputAction::SaveSkyDefaults:
        return "SaveSkyDefaults";
    case RuntimeInputAction::ApplyRenderTuning:
        return "ApplyRenderTuning";
    case RuntimeInputAction::ToggleCinematicRendering:
        return "ToggleCinematicRendering";
    case RuntimeInputAction::SelectCinematicScene:
        return "SelectCinematicScene";
    case RuntimeInputAction::ToggleCinematicFeature:
        return "ToggleCinematicFeature";
    case RuntimeInputAction::ApplyCinematicParam:
        return "ApplyCinematicParam";
    default:
        return "UnknownAction";
    }
}

const char* InputController::DescribeSource( RuntimeInputActionSource source )
{
    switch ( source )
    {
    case RuntimeInputActionSource::Keyboard:
        return "Keyboard";
    case RuntimeInputActionSource::UI:
        return "UI";
    case RuntimeInputActionSource::Mouse:
        return "Mouse";
    case RuntimeInputActionSource::FocusLost:
        return "FocusLost";
    case RuntimeInputActionSource::Runtime:
        return "Runtime";
    default:
        return "UnknownSource";
    }
}

void InputController::DescribeLastTransitions( const RuntimeInputContext& context, char* out, std::size_t outSize )
{
    if ( outSize == 0 )
    {
        return;
    }

    out[0] = '\0';
    const int count = context.TransitionCount();
    for ( int i = 0; i < count; ++i )
    {
        const RuntimeInputTransition transition = context.TransitionAt( i );
        const char* separator = i == 0 ? "" : " | ";
        const std::size_t used = std::strlen( out );
        if ( used + 1 >= outSize )
        {
            return;
        }

        const int written = std::snprintf( out + used,
                                           outSize - used,
                                           "%s%s -> %s via %s/%s",
                                           separator,
                                           DescribeMode( transition.from ),
                                           DescribeMode( transition.to ),
                                           DescribeAction( transition.action ),
                                           DescribeSource( transition.source ) );
        if ( written < 0 || static_cast<std::size_t>( written ) >= outSize - used )
        {
            out[outSize - 1] = '\0';
            return;
        }
    }
}

void InputController::ResetUnfocusedInput( RunCameraState& camera,
                                           bool& leftSceneCycleWasDown,
                                           bool& rightSceneCycleWasDown )
{
    camera.input = {};
    camera.hasMouseLookLastClient = false;
    camera.needsMouseLookReset = true;
    leftSceneCycleWasDown = false;
    rightSceneCycleWasDown = false;
    Hardware::Input::ResetMouseLookDeltas();
    Hardware::Input::ConsumeMouseWheelDelta();
}

void InputController::ResetMouseLook( RunCameraState& camera )
{
    camera.input.xMove = 0;
    camera.input.yMove = 0;
    camera.hasMouseLookLastClient = false;
    camera.needsMouseLookReset = true;
    Hardware::Input::ResetMouseLookDeltas();
}

void InputController::SetMouseLookDelta( RunCameraState& camera, long rawX, long rawY )
{
    const long absX = rawX < 0 ? -rawX : rawX;
    const long absY = rawY < 0 ? -rawY : rawY;

    if ( absX > RunInternal::CAMERA_MOUSE_SPIKE_DELTA_PIXELS || absY > RunInternal::CAMERA_MOUSE_SPIKE_DELTA_PIXELS )
    {
        camera.input.xMove = 0;
        camera.input.yMove = 0;
        return;
    }

    camera.input.xMove =
        std::clamp( rawX, -RunInternal::CAMERA_MOUSE_MAX_DELTA_PIXELS, RunInternal::CAMERA_MOUSE_MAX_DELTA_PIXELS );
    camera.input.yMove =
        std::clamp( rawY, -RunInternal::CAMERA_MOUSE_MAX_DELTA_PIXELS, RunInternal::CAMERA_MOUSE_MAX_DELTA_PIXELS );
}
} // namespace Basics
} // namespace SkullbonezCore
