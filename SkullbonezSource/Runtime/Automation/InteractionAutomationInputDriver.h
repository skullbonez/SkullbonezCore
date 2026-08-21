/*
File: SkullbonezSource/Runtime/Automation/InteractionAutomationInputDriver.h
Purpose:
  Owns the synthetic mouse, keyboard, and focus snapshot used by CLI automation.

Summary:
  Script actions mutate one bounded device-state owner. Runtime sequencing asks
  it to expire holds and publish exactly one synthetic frame through Input.
  Script actions mutate this small device-state owner. Once per frame it
  publishes one `Input::AutomationState`, so normal input routing observes the
  same shape it would receive from physical devices.

Glossary:
  Synthetic device frame: Script-owned mouse, key, and focus values presented
    to the normal input bridge for one runtime frame.
  Release frame: First scene frame on which a held synthetic button/key becomes
    up again.

Invariants:
  - This owner never calls scene, UI, replay, or tool business APIs.
  - Focus loss is script-controlled; desktop foreground state cannot affect a
    deterministic automation launch.

Related:
  - SkullbonezSource/Runtime/Automation/InteractionAutomationController.cpp
  - SkullbonezSource/Runtime/Input/Input.h
*/
#pragma once

#include "../../Core/PlatformWin32.h"

#include <array>
#include <cstdint>

namespace SkullbonezCore::Runtime
{
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

  private:
    POINT m_mouseClientPosition = {};
    bool m_hasMouseClientPosition = false;
    bool m_leftMouseDown = false;
    bool m_rightMouseDown = false;
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
