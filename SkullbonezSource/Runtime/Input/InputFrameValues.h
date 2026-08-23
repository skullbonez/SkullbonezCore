/*
File: SkullbonezSource/Runtime/Input/InputFrameValues.h
Purpose:
  Publishes immutable values shared during one routed input turn.

Summary:
  Input owns device snapshots and frame-local routing facts. App sequencing may
  combine them with domain owners, but lower consumers and tests need no App
  header to copy pointer, key, camera-mode, or UI-capture values.

Invariants:
  - Values retain no runtime owner pointer or callback.
  - The selected operator surface is fixed for the complete input turn.
  - A pointer override replaces client coordinates but not button edges.

Related:
  - SkullbonezSource/Runtime/Input/InputRouter.h
  - SkullbonezSource/Runtime/App/InputFrame.h
*/
#pragma once

#include "../Camera/RuntimeCameraMode.h"
#include "InputRouter.h"
#include "../../UI/OperatorEditorExchange.h"
#include "../../UI/UIInput.h"

namespace SkullbonezCore::Runtime
{
struct RuntimeInputFrameFacts
{
    RunCameraMode replayCurrentCameraMode = RunCameraMode::Inspect;
    RunCameraMode replayRestoreCameraMode = RunCameraMode::Inspect;
    uint32_t cameraModeEnabledMask = 0u;
    bool suppressWorldActionThisFrame = false;
    int sceneObjectCapacity = 0;
    UiInputCaptureIntent externalUiCapture;
    UI::OperatorEditorCommandQueues externalEditorCommands;
    bool gameUiActive = true;
    int requestedReplayCauseRow = -1;
};

inline UI::InputControl::UIInputSnapshot BuildUIInputSnapshot( const DeviceInputFrame& frame, const RuntimeMouseEdges& mouse,
                                                               UI::InputControl::UIPointerOverride pointerOverride )
{
    UI::InputControl::UIInputSnapshot snapshot;
    snapshot.keyWords = frame.keys.Words();
    snapshot.wheelDelta = frame.wheelDelta;

    if ( pointerOverride.enabled )
    {
        snapshot.mouseX = pointerOverride.x;
        snapshot.mouseY = pointerOverride.y;
    }
    else if ( frame.hasClientPosition )
    {
        snapshot.mouseX = frame.clientX;
        snapshot.mouseY = frame.clientY;
    }

    snapshot.leftDown = mouse.leftDown;
    snapshot.leftPressed = mouse.leftPressed;
    snapshot.leftReleased = mouse.leftReleased;
    return snapshot;
}

struct KeyboardContextFacts
{
    bool keyboardUnblocked = false;
    bool scene = false;
    bool flyCamera = false;
    bool launcher = false;
    bool attachedCamera = false;
    bool director = false;
    bool directorAuthoring = false;
    bool editor = false;
    bool replayRestoreNotConsumed = false;
    bool uiNotInteracted = false;
};
} // namespace SkullbonezCore::Runtime
