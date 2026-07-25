/*
File: SkullbonezSource/Runtime/Input/InputController.cpp
Purpose:
  Maintains runtime input-mode state and applies camera mouse-look deltas.

Summary:
  InputRouter normalizes semantic keyboard edges. This layer retains camera and
  pointer compatibility behavior while later input slices move those paths.

Glossary:
  Input edge: Transition from not pressed to pressed, used for one-shot
  commands.
  Mouse look: Camera mode where relative mouse movement rotates the view.
  Runtime command: Normalized input event consumed later by Run.

Invariants:
  - UI/focus blocking policy is resolved before camera/pointer work is applied.
  - RuntimeInputContext does not store semantic keyboard edge memory.

Related:
  - SkullbonezSource/Runtime/Input/InputController.h
  - SkullbonezSource/Runtime/App/InputRouter.Interactions.cpp
*/
#include "InputController.h"
#include "InputRouter.h"

#include "../Camera/CameraControlState.h"
#include "../Camera/CameraCollection.h"
#include "../../World/Terrain.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace SkullbonezCore
{
namespace Runtime
{
namespace
{
constexpr long CAMERA_MOUSE_MAX_DELTA_PIXELS = 96;
constexpr long CAMERA_MOUSE_SPIKE_DELTA_PIXELS = 320;
} // namespace

void RuntimeInputContext::BeginFrame( bool appFocused, bool uiBlocksKeyboard, bool uiBlocksMouse )
{
    m_appFocused = appFocused;
    m_uiBlocksKeyboard = uiBlocksKeyboard;
    m_uiBlocksMouse = uiBlocksMouse;
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

void InputController::BeginFrame(
    RuntimeInputContext& context,
    const RuntimeInputModeState& modeState,
    bool appFocused,
    bool uiBlocksKeyboard,
    bool uiBlocksMouse
)
{
    context.BeginFrame( appFocused, uiBlocksKeyboard, uiBlocksMouse );
    context.SetMode(
        ResolveMode( modeState ),
        RuntimeInputAction::None,
        appFocused ? RuntimeInputActionSource::Runtime : RuntimeInputActionSource::FocusLost
    );
}

void InputController::ApplyModeAction(
    RuntimeInputContext& context,
    RuntimeInputMode mode,
    RuntimeInputAction action,
    RuntimeInputActionSource source
)
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
    case RuntimeInputAction::ToggleDirectorGrab:
        return "ToggleDirectorGrab";
    case RuntimeInputAction::SetDirectorPhasePose:
        return "SetDirectorPhasePose";
    case RuntimeInputAction::StepDirectorPhase:
        return "StepDirectorPhase";
    case RuntimeInputAction::SaveDirectorShotList:
        return "SaveDirectorShotList";
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
    case RuntimeInputAction::UndoEditor:
        return "UndoEditor";
    case RuntimeInputAction::RedoEditor:
        return "RedoEditor";
    case RuntimeInputAction::DeleteEditorSelection:
        return "DeleteEditorSelection";
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
    case RuntimeInputAction::ToggleMemoryOverlay:
        return "ToggleMemoryOverlay";
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
    case RuntimeInputAction::SetReplayMemoryPolicy:
        return "SetReplayMemoryPolicy";
    case RuntimeInputAction::CycleReplayPathColorMode:
        return "CycleReplayPathColorMode";
    case RuntimeInputAction::ToggleReplayGuideArcs:
        return "ToggleReplayGuideArcs";
    case RuntimeInputAction::ToggleReplayTripPlanner:
        return "ToggleReplayTripPlanner";
    case RuntimeInputAction::ToggleReplayPorkchopPanel:
        return "ToggleReplayPorkchopPanel";
    case RuntimeInputAction::ToggleCinematicRendering:
        return "ToggleCinematicRendering";
    case RuntimeInputAction::SelectCinematicScene:
        return "SelectCinematicScene";
    case RuntimeInputAction::ToggleCinematicFeature:
        return "ToggleCinematicFeature";
    case RuntimeInputAction::ApplyCinematicParam:
        return "ApplyCinematicParam";
    case RuntimeInputAction::ToggleCrossScenePause:
        return "ToggleCrossScenePause";
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

        const int written = std::snprintf(
            out + used,
            outSize - used,
            "%s%s -> %s via %s/%s",
            separator,
            DescribeMode( transition.from ),
            DescribeMode( transition.to ),
            DescribeAction( transition.action ),
            DescribeSource( transition.source )
        );

        if ( written < 0 || static_cast<std::size_t>( written ) >= outSize - used )
        {
            out[outSize - 1] = '\0';
            return;
        }
    }
}

void InputController::ResetUnfocusedInput( CameraControlState& camera )
{
    camera.input = {};
    camera.mouseLookOwnsCursor = false;
    camera.travelSpeedMultiplier = 1.0f;
    camera.hasMouseLookLastClient = false;
    camera.needsMouseLookReset = true;
}

void InputController::ResetMouseLook( CameraControlState& camera )
{
    camera.input.xMove = 0;
    camera.input.yMove = 0;
    camera.hasMouseLookLastClient = false;
    camera.needsMouseLookReset = true;
}

void InputController::SetMouseLookDelta( CameraControlState& camera, long rawX, long rawY )
{
    const long absX = rawX < 0 ? -rawX : rawX;
    const long absY = rawY < 0 ? -rawY : rawY;

    if ( absX > CAMERA_MOUSE_SPIKE_DELTA_PIXELS || absY > CAMERA_MOUSE_SPIKE_DELTA_PIXELS )
    {
        camera.input.xMove = 0;
        camera.input.yMove = 0;
        return;
    }

    camera.input.xMove = std::clamp( rawX, -CAMERA_MOUSE_MAX_DELTA_PIXELS, CAMERA_MOUSE_MAX_DELTA_PIXELS );
    camera.input.yMove = std::clamp( rawY, -CAMERA_MOUSE_MAX_DELTA_PIXELS, CAMERA_MOUSE_MAX_DELTA_PIXELS );
}

RuntimeCameraInputFrameResult
InputController::ApplyCameraInputFrame( CameraControlState& camera, const RuntimeCameraInputFrameContext& context )
{
    RuntimeCameraInputFrameResult result;
    assert( context.deviceFrame && "Camera input requires the immutable device frame" );
    if ( !context.deviceFrame )
    {
        return result;
    }
    const DeviceInputFrame& deviceFrame = *context.deviceFrame;
    camera.mouseLookOwnsCursor = context.mouseLookOwnsCursor;
    camera.travelSpeedMultiplier = deviceFrame.keys.IsDown( VK_SHIFT ) ? 3.0f : 1.0f;
    if ( context.cameraMouseLookActive )
    {
        // Why: raw mouse input gives stable deltas during native mouse-look, and
        // client-position deltas keep remote-desktop or automation paths usable
        // when raw packets are unavailable.
        if ( !context.appFocused )
        {
            ResetMouseLook( camera );
        }
        else if ( !context.mouseLookOwnsCursor )
        {
            result.applyCursorOwnership = true;
            ResetMouseLook( camera );
        }
        else
        {
            if ( !deviceFrame.hasClientPosition )
            {
                ResetMouseLook( camera );
                result.applyCursorOwnership = true;
                return result;
            }
            const POINT currentClient { deviceFrame.clientX, deviceFrame.clientY };

            const bool hasRawDelta = deviceFrame.rawMouseX != 0 || deviceFrame.rawMouseY != 0;

            if ( camera.needsMouseLookReset )
            {
                camera.input.xMove = 0;
                camera.input.yMove = 0;
                camera.mouseLookLastClient = currentClient;
                camera.hasMouseLookLastClient = true;
                camera.needsMouseLookReset = false;
            }
            else if ( hasRawDelta )
            {
                SetMouseLookDelta( camera, deviceFrame.rawMouseX, deviceFrame.rawMouseY );
                camera.mouseLookLastClient = currentClient;
                camera.hasMouseLookLastClient = true;
            }
            else if ( !camera.hasMouseLookLastClient )
            {
                camera.input.xMove = 0;
                camera.input.yMove = 0;
                camera.mouseLookLastClient = currentClient;
                camera.hasMouseLookLastClient = true;
            }
            else
            {
                SetMouseLookDelta(
                    camera,
                    currentClient.x - camera.mouseLookLastClient.x,
                    currentClient.y - camera.mouseLookLastClient.y
                );
                camera.mouseLookLastClient = currentClient;
            }
        }
    }
    else
    {
        ResetMouseLook( camera );
        result.applyCursorOwnership = true;
    }

    if ( context.cameraKeyboardControlsActive )
    {
        camera.input.Set( Hardware::InputState::Up, deviceFrame.keys.IsDown( 'W' ) );
        camera.input.Set( Hardware::InputState::Left, deviceFrame.keys.IsDown( 'A' ) );
        camera.input.Set( Hardware::InputState::Down, deviceFrame.keys.IsDown( 'S' ) );
        camera.input.Set( Hardware::InputState::Right, deviceFrame.keys.IsDown( 'D' ) );
    }
    else
    {
        ResetMouseLook( camera );
        camera.input.Set( Hardware::InputState::Up, false );
        camera.input.Set( Hardware::InputState::Down, false );
        camera.input.Set( Hardware::InputState::Left, false );
        camera.input.Set( Hardware::InputState::Right, false );
    }
    return result;
}


void InputController::ApplyCameraMovement(
    CameraControlState& camera,
    Environment::CameraCollection& cameras,
    Geometry::Terrain& terrain,
    const RuntimeCameraMovementInput& input
)
{
    const bool hasTravelInput =
        camera.input.Get( Hardware::InputState::Up ) || camera.input.Get( Hardware::InputState::Down ) ||
        camera.input.Get( Hardware::InputState::Left ) || camera.input.Get( Hardware::InputState::Right );
    if ( !input.attachedOrbitOwnsCamera &&
         ( input.flyControlsActive || camera.mouseLookOwnsCursor || input.editorViewportLookActive || hasTravelInput ) )
    {
        if ( ( !input.editorModeEnabled || input.editorViewportLookActive ) &&
             ( camera.input.xMove != 0 || camera.input.yMove != 0 ) )
        {
            cameras.RotatePrimary(
                camera.input.xMove * input.mouseMovementQuantity,
                camera.input.yMove * input.mouseMovementQuantity
            );
        }

        const float travelQuantity = input.keyMovementQuantity * camera.travelSpeedMultiplier;
        if ( camera.input.Get( Hardware::InputState::Up ) )
        {
            cameras.MovePrimary( Environment::Camera::TravelDirection::Forward, travelQuantity );
        }
        if ( camera.input.Get( Hardware::InputState::Left ) )
        {
            cameras.MovePrimary( Environment::Camera::TravelDirection::Left, travelQuantity );
        }
        if ( camera.input.Get( Hardware::InputState::Down ) )
        {
            cameras.MovePrimary( Environment::Camera::TravelDirection::Backward, travelQuantity );
        }
        if ( camera.input.Get( Hardware::InputState::Right ) )
        {
            cameras.MovePrimary( Environment::Camera::TravelDirection::Right, travelQuantity );
        }
        cameras.ApplyPrimaryMovementBuffer();
    }

    // Passive generated-demo camera bounds do not own manual or pinned follow views.
    if ( !input.manualControlsActive && !input.editorViewportLookActive && !input.authoredScene )
    {
        const Math::Vector::Vector3 translatedCameraPosition = cameras.GetCameraTranslation();
        const float minY = terrain.GetTerrainHeightAt( translatedCameraPosition.x, translatedCameraPosition.z, true ) +
                           input.minCameraHeight;
        if ( minY > translatedCameraPosition.y )
        {
            cameras.AmmendPrimaryY( minY );
        }
        else if ( translatedCameraPosition.y > input.maxCameraHeight )
        {
            cameras.AmmendPrimaryY( input.maxCameraHeight );
        }
    }
}
} // namespace Runtime
} // namespace SkullbonezCore
