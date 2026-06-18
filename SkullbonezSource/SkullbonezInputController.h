/*
File: SkullbonezSource/SkullbonezInputController.h
Purpose:
  Provides runtime input edge detection and camera mouse-look input policy.

Mental model:
  Hardware::Input reads device state. InputController turns that state into
  stable per-frame runtime input events and camera deltas.

Glossary:
  Input edge: Transition from not pressed to pressed, used for one-shot
  commands.
  Camera delta: Per-frame mouse movement accumulated before camera update.
  Runtime input event: Frame-local input state consumed by SkullbonezRun.

Related:
  - SkullbonezSource/SkullbonezInputController.cpp
  - SkullbonezSource/SkullbonezRunInput.cpp
*/
#pragma once

#include "SkullbonezInput.h"

namespace SkullbonezCore
{
namespace Basics
{
struct RunCameraState;

struct RuntimeKeyEdge
{
    bool isDown = false;
    bool wasPressed = false;
};

class InputController
{
  public:
    static RuntimeKeyEdge CaptureKeyEdge( Hardware::InputState& state, Hardware::InputState::Key memoryKey, int virtualKey );
    static bool CaptureKeyPress( bool& wasDown, int virtualKey );
    static void ResetUnfocusedInput( RunCameraState& camera, bool& leftSceneCycleWasDown, bool& rightSceneCycleWasDown );
    static void ResetMouseLook( RunCameraState& camera );
    static void SetMouseLookDelta( RunCameraState& camera, long rawX, long rawY );
};
} // namespace Basics
} // namespace SkullbonezCore
