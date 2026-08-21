/*
File: SkullbonezSource/Runtime/Automation/InteractionAutomationRecorder.cpp
Purpose:
  Implements the resolution-independent human interaction test recorder.

Summary:
  Tracks input events and translates screen interactions into semantic UI control
  identifiers and normalized viewport coordinates. Outputs structured JSON that can
  be executed deterministically across diverse display sizes.

Invariants:
  - Fixed-capacity action storage prevents allocation during frame recording.
  - Output coordinates map to [0, 1] relative to the active presentation surface.
  - Serialization runs synchronously upon completion and flushes to disk cleanly.

Related:
  - SkullbonezSource/Runtime/Automation/InteractionAutomationRecorder.h
  - SkullbonezSource/Runtime/Automation/InteractionAutomationController.h
  - SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h
  - SkullbonezSource/Runtime/Planning/ReplayCauseInspection.h
*/
#include "InteractionAutomationRecorder.h"
#include "../Input/InputRouter.h"
#include "../Interaction/RuntimeInteractionController.h"
#include "../Replay/ReplayOverlayLayout.h"
#include "../Replay/ReplayOverlaySurface.h"
#include "../Planning/ReplayCauseInspection.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::ReplayOverlay;

namespace
{
constexpr bool PointInsideRect( const SkullbonezCore::UI::UIRect& r, int px, int py ) noexcept
{
    return px >= r.x && px <= r.x + r.w && py >= r.y && py <= r.y + r.h;
}

const char* VirtualKeyToString( int virtualKey ) noexcept
{
    switch ( virtualKey )
    {
    case VK_F1:
        return "F1";
    case VK_F2:
        return "F2";
    case VK_F3:
        return "F3";
    case VK_F4:
        return "F4";
    case VK_F5:
        return "F5";
    case VK_F6:
        return "F6";
    case VK_F7:
        return "F7";
    case VK_F8:
        return "F8";
    case VK_F9:
        return "F9";
    case VK_F10:
        return "F10";
    case VK_F11:
        return "F11";
    case VK_F12:
        return "F12";
    case VK_SPACE:
        return "Space";
    case VK_ESCAPE:
        return "Escape";
    case VK_RETURN:
        return "Return";
    case VK_TAB:
        return "Tab";
    case VK_BACK:
        return "Backspace";
    case VK_DELETE:
        return "Delete";
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
        return "Shift";
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
        return "Control";
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
        return "Alt";
    case VK_UP:
        return "Up";
    case VK_DOWN:
        return "Down";
    case VK_LEFT:
        return "Left";
    case VK_RIGHT:
        return "Right";
    case VK_OEM_COMMA:
        return "Comma";
    case VK_OEM_3:
        return "Tilde";
    default:
        break;
    }

    if ( virtualKey >= 'A' && virtualKey <= 'Z' )
    {
        static char charBuf[2] = { 0, 0 };
        charBuf[0] = static_cast<char>( virtualKey );
        return charBuf;
    }

    if ( virtualKey >= '0' && virtualKey <= '9' )
    {
        static char digitBuf[2] = { 0, 0 };
        digitBuf[0] = static_cast<char>( virtualKey );
        return digitBuf;
    }

    return nullptr;
}
} // namespace

void InteractionAutomationRecorder::StartRecording( const char* outputPath, const char* scenePath )
{
    m_isRecording = true;
    m_actionCount = 0;
    m_startFrame = 0;
    m_recordingTurn = 0;
    m_previousPointerX = -1;
    m_previousPointerY = -1;
    m_actions.fill( {} );
    m_previousKeys.fill( 0u );
    m_keyDownFrame.fill( -1 );
    m_previousLeftDown = false;
    m_previousRightDown = false;
    m_dragStartFrame = -1;

    if ( outputPath && outputPath[0] != '\0' )
    {
        strncpy_s( m_outputPath, sizeof( m_outputPath ), outputPath, _TRUNCATE );
    }
    else
    {
        strncpy_s( m_outputPath, sizeof( m_outputPath ), "TestScenarios/recorded_interactive_test.json", _TRUNCATE );
    }

    if ( scenePath && scenePath[0] != '\0' )
    {
        strncpy_s( m_scenePath, sizeof( m_scenePath ), scenePath, _TRUNCATE );
    }
    else
    {
        m_scenePath[0] = '\0';
    }

    printf( "[recorder] Started recording to: %s\n", m_outputPath );
}

void InteractionAutomationRecorder::StopRecording()
{
    if ( !m_isRecording )
    {
        return;
    }

    // Flush any keys that were still held down when recording stopped
    for ( int vk = 0; vk < 256; ++vk )
    {
        if ( m_keyDownFrame[vk] >= 0 && vk != VK_F8 )
        {
            if ( const char* keyName = VirtualKeyToString( vk ) )
            {
                RecordedInteractionAction keyAction;
                keyAction.frame = m_keyDownFrame[vk];
                keyAction.kind = RecordedActionKind::PressKey;
                keyAction.virtualKey = vk;
                keyAction.holdFrames = (std::max)( 1, m_recordingTurn - m_keyDownFrame[vk] );
                strncpy_s( keyAction.keyName, sizeof( keyAction.keyName ), keyName, _TRUNCATE );
                (void)AppendAction( keyAction );
            }

            m_keyDownFrame[vk] = -1;
        }
    }

    // Ensure strictly monotonic action frame order
    std::stable_sort( m_actions.begin(), m_actions.begin() + m_actionCount,
                      []( const RecordedInteractionAction& a, const RecordedInteractionAction& b )
                      { return a.frame < b.frame; } );

    m_isRecording = false;
    printf( "[recorder] Stopped recording. Total actions captured: %zu\n", m_actionCount );
    (void)SaveToFile();
}

void InteractionAutomationRecorder::ToggleRecording( const char* defaultPath, int currentFrame,
                                                     const char* defaultScenePath )
{
    if ( m_isRecording )
    {
        StopRecording();
    }
    else
    {
        StartRecording( defaultPath, defaultScenePath );
        m_startFrame = currentFrame;
    }
}

bool InteractionAutomationRecorder::AppendAction( const RecordedInteractionAction& action )
{
    if ( m_actionCount >= MAX_RECORDED_ACTIONS )
    {
        return false;
    }

    m_actions[m_actionCount++] = action;
    return true;
}

void InteractionAutomationRecorder::RecordFrame( int currentFrame, int screenWidth, int screenHeight,
                                                 const InputRouter& inputRouter,
                                                 const RuntimeInteractionController& interaction,
                                                 const RunReplayCauseTreeState& causeTree,
                                                 const ReplayCauseInspectionView& causeInspection )
{
    (void)interaction;
    (void)currentFrame;

    if ( !m_isRecording || screenWidth <= 0 || screenHeight <= 0 )
    {
        return;
    }

    const int relativeFrame = m_recordingTurn++;
    const RuntimePointerEvent& pointer = inputRouter.RuntimeSnapshot().pointer;
    const RuntimeMouseEdges& mouse = inputRouter.UiSnapshot().mouse;
    const InputKeySnapshot& keys = inputRouter.DeviceFrame().keys;
    const std::span<const uint64_t> keyWords = keys.Words();

    const float normX = std::clamp( static_cast<float>( pointer.clientX ) / static_cast<float>( screenWidth ), 0.0f, 1.0f );
    const float normY = std::clamp( static_cast<float>( pointer.clientY ) / static_cast<float>( screenHeight ), 0.0f, 1.0f );

    // 1. Mouse Movement / Camera Look Detection
    if ( m_previousPointerX >= 0 && ( pointer.clientX != m_previousPointerX || pointer.clientY != m_previousPointerY ) )
    {
        RecordedInteractionAction move;
        move.frame = relativeFrame;
        move.kind = RecordedActionKind::MoveMouse;
        move.pixelX = pointer.clientX;
        move.pixelY = pointer.clientY;
        move.normalizedX = normX;
        move.normalizedY = normY;
        (void)AppendAction( move );
    }

    m_previousPointerX = pointer.clientX;
    m_previousPointerY = pointer.clientY;

    // 2. Mouse Button Down / Click / Drag
    if ( mouse.leftPressed || mouse.rightPressed )
    {
        RecordedInteractionAction click;
        click.frame = relativeFrame;
        click.pixelX = pointer.clientX;
        click.pixelY = pointer.clientY;
        click.normalizedX = normX;
        click.normalizedY = normY;
        click.isRightButton = mouse.rightPressed;

        // Check Semantic Controls inside Replay Cause Tree / Inspector
        bool isSemanticControl = false;
        const ReplayCauseInspectorLayout inspectorLayout = BuildReplayCauseInspectorLayout( causeInspection, causeTree,
                                                                                            screenWidth, screenHeight,
                                                                                            causeInspection.drawerProgress );

        if ( causeInspection.detailVisible )
        {
            if ( PointInsideRect( inspectorLayout.drawerClose, pointer.clientX, pointer.clientY ) )
            {
                click.kind = RecordedActionKind::ClickReplayControl;
                strncpy_s( click.semanticControl, sizeof( click.semanticControl ), "causeCloseDrawer", _TRUNCATE );
                isSemanticControl = true;
            }
            else if ( PointInsideRect( inspectorLayout.rawCopy, pointer.clientX, pointer.clientY ) )
            {
                click.kind = RecordedActionKind::ClickReplayControl;
                strncpy_s( click.semanticControl, sizeof( click.semanticControl ), "causeCopyRawRecord", _TRUNCATE );
                isSemanticControl = true;
            }
            else
            {
                for ( std::size_t tab = 0; tab < inspectorLayout.tabs.size(); ++tab )
                {
                    if ( PointInsideRect( inspectorLayout.tabs[tab], pointer.clientX, pointer.clientY ) )
                    {
                        click.kind = RecordedActionKind::ClickReplayControl;
                        const char* tabNames[] = { "causeTabSummary", "causeTabRawRecord", "causeTabIterations" };
                        strncpy_s( click.semanticControl, sizeof( click.semanticControl ), tabNames[tab], _TRUNCATE );
                        isSemanticControl = true;
                        break;
                    }
                }
            }
        }

        if ( !isSemanticControl && causeTree.hasWindowPlacement )
        {
            const UI::UIRect funnelRect = ReplayCauseWindowFilterFunnelRect( causeTree );
            const UI::UIRect fieldRect = ReplayCauseWindowFilterFieldRect( causeTree );
            const UI::UIRect chipAll = ReplayCauseWindowFilterChipRect( causeTree, RunReplayCauseTreeFilter::All );
            const UI::UIRect chipPred = ReplayCauseWindowFilterChipRect( causeTree, RunReplayCauseTreeFilter::Prediction );
            const UI::UIRect chipCont = ReplayCauseWindowFilterChipRect( causeTree, RunReplayCauseTreeFilter::Contacts );

            if ( PointInsideRect( funnelRect, pointer.clientX, pointer.clientY ) )
            {
                click.kind = RecordedActionKind::ClickReplayControl;
                strncpy_s( click.semanticControl, sizeof( click.semanticControl ), "causeFilterFunnel", _TRUNCATE );
                isSemanticControl = true;
            }
            else if ( PointInsideRect( fieldRect, pointer.clientX, pointer.clientY ) )
            {
                click.kind = RecordedActionKind::ClickReplayControl;
                strncpy_s( click.semanticControl, sizeof( click.semanticControl ), "causeFilterField", _TRUNCATE );
                isSemanticControl = true;
            }
            else if ( PointInsideRect( chipAll, pointer.clientX, pointer.clientY ) )
            {
                click.kind = RecordedActionKind::ClickReplayControl;
                strncpy_s( click.semanticControl, sizeof( click.semanticControl ), "causeFilterAll", _TRUNCATE );
                isSemanticControl = true;
            }
            else if ( PointInsideRect( chipPred, pointer.clientX, pointer.clientY ) )
            {
                click.kind = RecordedActionKind::ClickReplayControl;
                strncpy_s( click.semanticControl, sizeof( click.semanticControl ), "causeFilterPrediction", _TRUNCATE );
                isSemanticControl = true;
            }
            else if ( PointInsideRect( chipCont, pointer.clientX, pointer.clientY ) )
            {
                click.kind = RecordedActionKind::ClickReplayControl;
                strncpy_s( click.semanticControl, sizeof( click.semanticControl ), "causeFilterContacts", _TRUNCATE );
                isSemanticControl = true;
            }
        }

        if ( !isSemanticControl )
        {
            click.kind = RecordedActionKind::ClickPoint;
        }

        (void)AppendAction( click );
    }

    // 3. Mouse Wheel Scroll
    if ( inputRouter.DeviceFrame().wheelDelta != 0 )
    {
        RecordedInteractionAction scroll;
        scroll.frame = relativeFrame;
        scroll.kind = RecordedActionKind::ScrollPoint;
        scroll.pixelX = pointer.clientX;
        scroll.pixelY = pointer.clientY;
        scroll.normalizedX = normX;
        scroll.normalizedY = normY;
        scroll.wheelDelta = inputRouter.DeviceFrame().wheelDelta;
        (void)AppendAction( scroll );
    }

    // 4. Keyboard Holds & Continuous Movement (WASD, Space, Shift, etc.)
    for ( int vk = 0; vk < InputKeySnapshot::VIRTUAL_KEY_COUNT; ++vk )
    {
        const std::size_t word = static_cast<std::size_t>( vk ) / 64u;
        const uint64_t bit = uint64_t { 1 } << ( static_cast<unsigned int>( vk ) & 63u );
        const bool isDown = keys.IsDown( vk );
        const bool wasDown = ( m_previousKeys[word] & bit ) != 0u;

        if ( isDown && !wasDown && vk != VK_F8 ) // F8 is reserved for recorder toggle
        {
            m_keyDownFrame[vk] = relativeFrame;
        }
        else if ( !isDown && wasDown && vk != VK_F8 )
        {
            if ( m_keyDownFrame[vk] >= 0 )
            {
                if ( const char* keyName = VirtualKeyToString( vk ) )
                {
                    RecordedInteractionAction keyAction;
                    keyAction.frame = m_keyDownFrame[vk];
                    keyAction.kind = RecordedActionKind::PressKey;
                    keyAction.virtualKey = vk;
                    keyAction.holdFrames = (std::max)( 1, relativeFrame - m_keyDownFrame[vk] );
                    strncpy_s( keyAction.keyName, sizeof( keyAction.keyName ), keyName, _TRUNCATE );
                    (void)AppendAction( keyAction );
                }

                m_keyDownFrame[vk] = -1;
            }
        }
    }

    // Save previous state for edge detection
    for ( std::size_t i = 0; i < 4 && i < keyWords.size(); ++i )
    {
        m_previousKeys[i] = keyWords[i];
    }

    m_previousLeftDown = mouse.leftDown;
    m_previousRightDown = mouse.rightDown;
}

bool InteractionAutomationRecorder::SaveToFile()
{
    if ( m_outputPath[0] == '\0' )
    {
        return false;
    }

    std::ofstream out( m_outputPath );

    if ( !out.is_open() )
    {
        printf( "[recorder] Error: failed to open output file %s\n", m_outputPath );
        return false;
    }

    out << "{\n";

    if ( m_scenePath[0] != '\0' )
    {
        out << "  \"scene\": \"" << m_scenePath << "\",\n";
    }

    out << "  \"actions\": [\n";

    for ( std::size_t i = 0; i < m_actionCount; ++i )
    {
        const RecordedInteractionAction& a = m_actions[i];
        out << "    { \"frame\": " << a.frame;

        switch ( a.kind )
        {
        case RecordedActionKind::ClickReplayControl:
            out << ", \"clickReplayControl\": \"" << a.semanticControl << "\"";
            break;
        case RecordedActionKind::ClickPoint:
            out << ", \"clickPoint\": [" << a.pixelX << ", " << a.pixelY << "]";
            out << ", \"normalizedPoint\": [" << a.normalizedX << ", " << a.normalizedY << "]";

            if ( a.isRightButton )
            {
                out << ", \"button\": \"right\"";
            }

            if ( a.holdFrames > 1 )
            {
                out << ", \"holdFrames\": " << a.holdFrames;
            }

            break;
        case RecordedActionKind::MoveMouse:
            out << ", \"moveMouse\": [" << a.pixelX << ", " << a.pixelY << "]";
            out << ", \"normalizedPoint\": [" << a.normalizedX << ", " << a.normalizedY << "]";
            break;
        case RecordedActionKind::ScrollPoint:
            out << ", \"scrollPoint\": [" << a.pixelX << ", " << a.pixelY << ", " << a.wheelDelta << "]";
            break;
        case RecordedActionKind::PressKey:
            out << ", \"pressKey\": \"" << a.keyName << "\"";

            if ( a.holdFrames > 1 )
            {
                out << ", \"holdFrames\": " << a.holdFrames;
            }

            break;
        case RecordedActionKind::SelectReplayCauseRow:
            out << ", \"selectReplayCauseRow\": " << a.selectedRow;
            break;
        case RecordedActionKind::ScrubReplaySolverTrack:
            out << ", \"scrubReplaySolverTrack\": " << a.scrubFraction;
            break;
        default:
            break;
        }

        out << " }" << ( i + 1 < m_actionCount ? ",\n" : "\n" );
    }

    out << "  ]\n";
    out << "}\n";
    out.close();

    printf( "[recorder] Successfully saved %zu actions to %s\n", m_actionCount, m_outputPath );
    return true;
}
