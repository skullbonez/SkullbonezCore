/*
File: SkullbonezSource/Runtime/Input/InputController.cpp
Purpose:
  Maintains runtime input-mode state and applies camera mouse-look deltas.

Summary:
  InputController is stateless policy over InputRouter-owned context values and
  CameraControlState. It applies focus, mode, pointer, and camera deltas without
  becoming a second retained input-state owner.

Glossary:
  Mouse look: Camera mode where relative mouse movement rotates the view.
  Runtime command: Normalized input event consumed later by Run.

Invariants:
  - UI/focus blocking policy is resolved before camera/pointer work is applied.
  - RuntimeInputContext does not store semantic keyboard edge memory.

Related:
  - SkullbonezSource/Runtime/Input/InputController.h
  - SkullbonezSource/Runtime/App/InputRouter.Interactions.cpp
  - Agentic/Reference/engine-glossary.md
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


void InputController::BeginFrame( RuntimeInputContext& context, const RuntimeInputModeState& modeState, bool appFocused,
                                  bool uiBlocksKeyboard, bool uiBlocksMouse )
{
    context.BeginFrame( appFocused, uiBlocksKeyboard, uiBlocksMouse );
    context.SetMode( ResolveMode( modeState ), RuntimeInputAction::None,
                     appFocused ? RuntimeInputActionSource::Runtime : RuntimeInputActionSource::FocusLost );
}

void InputController::ApplyModeAction( RuntimeInputContext& context, RuntimeInputMode mode, RuntimeInputAction action,
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


void InputController::ResetUnfocusedInput( CameraControlState& camera )
{
    camera.inputXMove = 0;
    camera.inputYMove = 0;
    camera.inputMoveForward = false;
    camera.inputMoveBackward = false;
    camera.inputMoveLeft = false;
    camera.inputMoveRight = false;
    camera.mouseLookOwnsCursor = false;
    camera.travelSpeedMultiplier = 1.0f;
    camera.hasMouseLookLastClient = false;
    camera.needsMouseLookReset = true;
}

void InputController::ResetMouseLook( CameraControlState& camera )
{
    camera.inputXMove = 0;
    camera.inputYMove = 0;
    camera.hasMouseLookLastClient = false;
    camera.needsMouseLookReset = true;
}

void InputController::SetMouseLookDelta( CameraControlState& camera, long rawX, long rawY )
{
    const long absX = rawX < 0 ? -rawX : rawX;
    const long absY = rawY < 0 ? -rawY : rawY;

    if ( absX > CAMERA_MOUSE_SPIKE_DELTA_PIXELS || absY > CAMERA_MOUSE_SPIKE_DELTA_PIXELS )
    {
        camera.inputXMove = 0;
        camera.inputYMove = 0;
        return;
    }

    camera.inputXMove = std::clamp( rawX, -CAMERA_MOUSE_MAX_DELTA_PIXELS, CAMERA_MOUSE_MAX_DELTA_PIXELS );
    camera.inputYMove = std::clamp( rawY, -CAMERA_MOUSE_MAX_DELTA_PIXELS, CAMERA_MOUSE_MAX_DELTA_PIXELS );
}

RuntimeCameraInputFrameResult InputController::ApplyCameraInputFrame( CameraControlState& camera, bool appFocused,
                                                                      bool cameraMouseLookActive, bool mouseLookOwnsCursor,
                                                                      bool cameraKeyboardControlsActive,
                                                                      const DeviceInputFrame& deviceFrame )
{
    RuntimeCameraInputFrameResult result;
    camera.mouseLookOwnsCursor = mouseLookOwnsCursor;
    camera.travelSpeedMultiplier = deviceFrame.keys.IsDown( VK_SHIFT ) ? 3.0f : 1.0f;

    if ( cameraMouseLookActive )
    {
        // Why: raw mouse input gives stable deltas during native mouse-look, and
        // client-position deltas keep remote-desktop or automation paths usable
        // when raw packets are unavailable.
        if ( !appFocused )
        {
            ResetMouseLook( camera );
        }
        else if ( !mouseLookOwnsCursor )
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
                camera.inputXMove = 0;
                camera.inputYMove = 0;
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
                camera.inputXMove = 0;
                camera.inputYMove = 0;
                camera.mouseLookLastClient = currentClient;
                camera.hasMouseLookLastClient = true;
            }
            else
            {
                SetMouseLookDelta( camera, currentClient.x - camera.mouseLookLastClient.x,
                                   currentClient.y - camera.mouseLookLastClient.y );

                camera.mouseLookLastClient = currentClient;
            }
        }
    }
    else
    {
        ResetMouseLook( camera );
        result.applyCursorOwnership = true;
    }

    if ( cameraKeyboardControlsActive )
    {
        camera.inputMoveForward = deviceFrame.keys.IsDown( 'W' );
        camera.inputMoveLeft = deviceFrame.keys.IsDown( 'A' );
        camera.inputMoveBackward = deviceFrame.keys.IsDown( 'S' );
        camera.inputMoveRight = deviceFrame.keys.IsDown( 'D' );
    }
    else
    {
        ResetMouseLook( camera );
        camera.inputMoveForward = false;
        camera.inputMoveBackward = false;
        camera.inputMoveLeft = false;
        camera.inputMoveRight = false;
    }

    return result;
}


void InputController::ApplyCameraMovement( CameraControlState& camera, Environment::CameraCollection& cameras,
                                           Geometry::Terrain& terrain, const RuntimeCameraMovementInput& input )
{
    const bool hasTravelInput = camera.inputMoveForward || camera.inputMoveBackward || camera.inputMoveLeft ||
                                camera.inputMoveRight;

    if ( !input.attachedOrbitOwnsCamera &&
         ( input.flyControlsActive || camera.mouseLookOwnsCursor || input.editorViewportLookActive || hasTravelInput ) )
    {
        if ( ( !input.editorModeEnabled || input.editorViewportLookActive ) &&
             ( camera.inputXMove != 0 || camera.inputYMove != 0 ) )
        {
            cameras.RotatePrimary( camera.inputXMove * input.mouseMovementQuantity,
                                   camera.inputYMove * input.mouseMovementQuantity );
        }

        const float travelQuantity = input.keyMovementQuantity * camera.travelSpeedMultiplier;

        if ( camera.inputMoveForward )
        {
            cameras.MovePrimary( Environment::Camera::TravelDirection::Forward, travelQuantity );
        }

        if ( camera.inputMoveLeft )
        {
            cameras.MovePrimary( Environment::Camera::TravelDirection::Left, travelQuantity );
        }

        if ( camera.inputMoveBackward )
        {
            cameras.MovePrimary( Environment::Camera::TravelDirection::Backward, travelQuantity );
        }

        if ( camera.inputMoveRight )
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
