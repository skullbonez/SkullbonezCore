/*
File: SkullbonezSource/Runtime/Automation/InteractionAutomationInputDriver.cpp
Purpose:
  Publishes deterministic synthetic device frames for CLI interaction probes.

Summary:
  The input driver applies hold/release policy independently of camera, scene,
  replay, UI, and tool owners, then forwards a value snapshot to Input.
  Actions set held state and release deadlines. The frame sequencer first
  expires deadlines, applies this frame's actions, then publishes one immutable
  snapshot to the existing input bridge.

Glossary:
  Input bridge: Process-global validation seam consumed by the normal input
    sampler; it does not bypass runtime input routing.
  Focus override: Explicit script value replacing desktop foreground focus for
    deterministic CLI probes.

Invariants:
  - Release deadlines are evaluated before same-frame actions.
  - `PublishFrame` decrements a focus-loss hold only after publishing it.

Related:
  - SkullbonezSource/Runtime/Automation/InteractionAutomationInputDriver.h
  - SkullbonezSource/Runtime/Input/Input.cpp
*/
#include "InteractionAutomationInputDriver.h"

#include "../Input/Input.h"

using SkullbonezCore::Hardware::Input;

void SkullbonezCore::Runtime::InteractionAutomationInputDriver::Reset()
{
    *this = InteractionAutomationInputDriver {};
    m_releaseKeyFrames.fill( -1 );
    Input::ClearAutomationState();
}

void SkullbonezCore::Runtime::InteractionAutomationInputDriver::AdvanceReleases( int frame )
{
    // Invariant: replay transport can advance the automation frame beyond a
    // scheduled release. The first observed frame at or after the deadline
    // must release the synthetic input instead of leaving it latched forever.
    if ( m_releaseLeftFrame >= 0 && frame >= m_releaseLeftFrame )
    {
        m_leftMouseDown = false;
        m_releaseLeftFrame = -1;
    }

    if ( m_releaseRightFrame >= 0 && frame >= m_releaseRightFrame )
    {
        m_rightMouseDown = false;
        m_releaseRightFrame = -1;
    }

    if ( m_releaseKeyFrame >= 0 && frame >= m_releaseKeyFrame )
    {
        m_keyVirtualKey = 0;
        m_keyDown = false;
        m_controlDown = false;
        m_releaseKeyFrame = -1;
    }

    for ( int vk = 0; vk < 256; ++vk )
    {
        if ( m_releaseKeyFrames[vk] >= 0 && frame >= m_releaseKeyFrames[vk] )
        {
            const std::size_t word = static_cast<std::size_t>( vk ) / 64u;
            m_keyWords[word] &= ~( uint64_t { 1 } << ( static_cast<unsigned int>( vk ) & 63u ) );
            m_releaseKeyFrames[vk] = -1;
        }
    }
}

void SkullbonezCore::Runtime::InteractionAutomationInputDriver::MoveMouse( POINT position )
{
    m_mouseClientPosition = position;
    m_hasMouseClientPosition = true;
}

void SkullbonezCore::Runtime::InteractionAutomationInputDriver::ScrollMouse( int wheelDelta )
{
    m_mouseWheelDelta += wheelDelta;
}

void SkullbonezCore::Runtime::InteractionAutomationInputDriver::PressMouse( bool rightButton, int frame, int holdFrames )
{
    if ( rightButton )
    {
        m_rightMouseDown = true;
        m_releaseRightFrame = frame + holdFrames;
        return;
    }

    m_leftMouseDown = true;
    m_releaseLeftFrame = frame + holdFrames;
}

void SkullbonezCore::Runtime::InteractionAutomationInputDriver::PressKey( int virtualKey, bool controlDown, int frame,
                                                                          int holdFrames )
{
    m_keyVirtualKey = virtualKey;
    m_keyDown = true;
    m_controlDown = controlDown;
    m_releaseKeyFrame = frame + (std::max)( 1, holdFrames );

    if ( virtualKey >= 0 && virtualKey < 256 )
    {
        const std::size_t word = static_cast<std::size_t>( virtualKey ) / 64u;
        m_keyWords[word] |= uint64_t { 1 } << ( static_cast<unsigned int>( virtualKey ) & 63u );
        m_releaseKeyFrames[virtualKey] = frame + (std::max)( 1, holdFrames );
    }

    if ( controlDown )
    {
        const std::size_t word = static_cast<std::size_t>( VK_CONTROL ) / 64u;
        m_keyWords[word] |= uint64_t { 1 } << ( static_cast<unsigned int>( VK_CONTROL ) & 63u );
        m_releaseKeyFrames[VK_CONTROL] = frame + (std::max)( 1, holdFrames );
    }
}

void SkullbonezCore::Runtime::InteractionAutomationInputDriver::LoseFocus( int frameCount )
{
    m_unfocusedInputFrames = frameCount;
}

void SkullbonezCore::Runtime::InteractionAutomationInputDriver::PublishFrame()
{
    Input::AutomationState inputState;
    inputState.enabled = true;

    // Automation owns focus for the synthetic frame. Reading the real desktop
    // foreground window here would make a CLI result depend on operator input.
    inputState.overrideAppFocused = true;
    inputState.appFocused = m_unfocusedInputFrames == 0;
    inputState.hasMouseClientPosition = m_hasMouseClientPosition;
    inputState.mouseClientPosition = m_mouseClientPosition;
    inputState.leftMouseDown = m_leftMouseDown;
    inputState.rightMouseDown = m_rightMouseDown;
    inputState.mouseWheelDelta = m_mouseWheelDelta;
    inputState.keyVirtualKey = m_keyVirtualKey;
    inputState.keyDown = m_keyDown;
    inputState.controlDown = m_controlDown;
    inputState.keyWords = m_keyWords;
    Input::SetAutomationState( inputState );
    m_mouseWheelDelta = 0;

    if ( m_unfocusedInputFrames > 0 )
    {
        --m_unfocusedInputFrames;
    }
}
