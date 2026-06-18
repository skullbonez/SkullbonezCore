/*
File: SkullbonezSource/SkullbonezInputController.cpp
Purpose:
  Converts raw runtime input state into edge-triggered commands and camera deltas.

Mental model:
  This layer is intentionally narrow: it does not apply gameplay commands, it
  only normalizes keyboard/mouse edges for the runtime.

Related:
  - SkullbonezSource/SkullbonezInputController.h
  - SkullbonezSource/SkullbonezRunInput.cpp
*/
#include "SkullbonezInputController.h"

#include "SkullbonezRunInternal.h"

#include <algorithm>

namespace SkullbonezCore
{
namespace Basics
{
RuntimeKeyEdge InputController::CaptureKeyEdge( Hardware::InputState& state,
                                                Hardware::InputState::Key memoryKey,
                                                int virtualKey )
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

    camera.input.xMove = std::clamp( rawX, -RunInternal::CAMERA_MOUSE_MAX_DELTA_PIXELS, RunInternal::CAMERA_MOUSE_MAX_DELTA_PIXELS );
    camera.input.yMove = std::clamp( rawY, -RunInternal::CAMERA_MOUSE_MAX_DELTA_PIXELS, RunInternal::CAMERA_MOUSE_MAX_DELTA_PIXELS );
}
} // namespace Basics
} // namespace SkullbonezCore
