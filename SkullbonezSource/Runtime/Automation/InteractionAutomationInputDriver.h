/*
File: SkullbonezSource/Runtime/Automation/InteractionAutomationInputDriver.h
Purpose:
  Owns the synthetic mouse, keyboard, and focus snapshot used by CLI automation.

Summary:
  Legacy script actions mutate one bounded device-state owner and use scheduled
  release deadlines. Recorded manifests instead publish a complete captured
  device frame, resolving semantic anchors before normalized pointer fallback.
  Both paths produce one Input::AutomationState for normal input routing.

Glossary:
  Synthetic device frame: Script-owned mouse, key, and focus values presented
    to the normal input bridge for one runtime frame.
  Release frame: First scene frame on which a held synthetic button/key becomes
    up again.

Invariants:
  - This owner never calls scene, UI, replay, or tool business APIs.
  - Focus loss is script-controlled; desktop foreground state cannot affect a
    deterministic automation launch.
  - Raw mouse deltas remain unscaled; normalized absolute coordinates map to
    the target client area's inclusive pixel domain.

Related:
  - SkullbonezSource/Runtime/App/InteractionAutomationApplication.cpp
  - SkullbonezSource/Runtime/Input/Input.h
*/
#pragma once

#include "../../Core/PlatformWin32.h"
#include "../Input/Input.h"
#include "InteractionAutomationRecorder.h"
#include "RecordedCursorFrame.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace SkullbonezCore::Runtime
{
inline RecordedCursorFrame BuildRecordedFramePublication( const RecordedInputFrame& frame,
                                                          SkullbonezCore::Hardware::Input::AutomationState& inputState,
                                                          int targetWidth, int targetHeight, bool publishedRealTurn,
                                                          const POINT* semanticPosition = nullptr )
{
    inputState = {};
    inputState.enabled = true;
    inputState.overrideAppFocused = true;
    inputState.appFocused = frame.appFocused;
    inputState.hasMouseClientPosition = frame.hasPointer;

    // Invariant: an anchor may relocate a real pointer, but cannot fabricate
    // availability for a malformed pointerless recorded turn.
    if ( frame.hasPointer && semanticPosition )
    {
        inputState.mouseClientPosition = *semanticPosition;
    }
    else if ( frame.hasPointer )
    {
        const int targetMaxX = (std::max)( 0, targetWidth - 1 );
        const int targetMaxY = (std::max)( 0, targetHeight - 1 );
        inputState.mouseClientPosition.x = static_cast<LONG>(
            std::lround( std::clamp( frame.normalizedX, 0.0f, 1.0f ) * static_cast<float>( targetMaxX ) ) );
        inputState.mouseClientPosition.y = static_cast<LONG>(
            std::lround( std::clamp( frame.normalizedY, 0.0f, 1.0f ) * static_cast<float>( targetMaxY ) ) );
    }

    inputState.leftMouseDown = frame.leftDown;
    inputState.rightMouseDown = frame.rightDown;
    inputState.middleMouseDown = frame.middleDown;
    inputState.mouseWheelDelta = frame.wheelDelta;
    inputState.rawMouseDeltaX = frame.rawMouseX;
    inputState.rawMouseDeltaY = frame.rawMouseY;
    inputState.keyWords = frame.keyWords;

    RecordedCursorFrame cursor;
    cursor.clientX = inputState.hasMouseClientPosition ? inputState.mouseClientPosition.x : 0;
    cursor.clientY = inputState.hasMouseClientPosition ? inputState.mouseClientPosition.y : 0;
    cursor.publishedRealTurn = publishedRealTurn;
    cursor.pointerResolved = inputState.hasMouseClientPosition;
    cursor.recordedAppFocused = inputState.appFocused;
    return cursor;
}

class InteractionAutomationInputDriver
{
  public:
    void Reset();
    void AdvanceReleases( int frame );
    void MoveMouse( POINT position );
    void ScrollMouse( int wheelDelta );
    void PressMouse( bool rightButton, int frame, int holdFrames );
    void PressKey( int virtualKey, bool controlDown, int frame, int holdFrames = 1 );
    void LoseFocus( int frameCount );
    void PublishFrame();

    // Publishes a complete recorded device frame. semanticPosition, when
    // supplied, wins over normalized absolute coordinates for this turn.
    RecordedCursorFrame PublishRecordedFrame( const RecordedInputFrame& frame, int targetWidth, int targetHeight,
                                              bool publishedRealTurn, const POINT* semanticPosition = nullptr );

  private:
    POINT m_mouseClientPosition = {};
    bool m_hasMouseClientPosition = false;
    bool m_leftMouseDown = false;
    bool m_rightMouseDown = false;
    bool m_middleMouseDown = false;
    int m_mouseWheelDelta = 0;
    int m_keyVirtualKey = 0;
    bool m_keyDown = false;
    bool m_controlDown = false;
    std::array<uint64_t, 4> m_keyWords = {};
    std::array<int, 256> m_releaseKeyFrames = {};
    int m_releaseLeftFrame = -1;
    int m_releaseRightFrame = -1;
    int m_releaseKeyFrame = -1;
    int m_unfocusedInputFrames = 0;
};
} // namespace SkullbonezCore::Runtime
