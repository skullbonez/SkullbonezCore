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
    case VK_F5:
        return "F5";
    case VK_F6:
        return "F6";
    case VK_F9:
        return "F9";
    case VK_F10:
        return "F10";
    case VK_F11:
        return "F11";
    case VK_SPACE:
        return "Space";
    case VK_ESCAPE:
        return "Escape";
    case VK_RETURN:
        return "Return";
    case VK_BACK:
        return "Backspace";
    case VK_DELETE:
        return "Delete";
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
    m_actions.fill( {} );
    m_previousKeys.fill( 0u );
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

    if ( !m_isRecording || screenWidth <= 0 || screenHeight <= 0 )
    {
        return;
    }

    const int relativeFrame = currentFrame - m_startFrame;
    const RuntimePointerEvent& pointer = inputRouter.RuntimeSnapshot().pointer;
    const RuntimeMouseEdges& mouse = inputRouter.UiSnapshot().mouse;
    const InputKeySnapshot& keys = inputRouter.DeviceFrame().keys;
    const std::span<const uint64_t> keyWords = keys.Words();

    const float normX = std::clamp( static_cast<float>( pointer.clientX ) / static_cast<float>( screenWidth ), 0.0f, 1.0f );
    const float normY = std::clamp( static_cast<float>( pointer.clientY ) / static_cast<float>( screenHeight ), 0.0f, 1.0f );

    // 1. Mouse Button Down / Click
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

    // 2. Mouse Wheel Scroll
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

    // 3. Keyboard Presses
    for ( int vk = 0; vk < InputKeySnapshot::VIRTUAL_KEY_COUNT; ++vk )
    {
        const std::size_t word = static_cast<std::size_t>( vk ) / 64u;
        const uint64_t bit = uint64_t { 1 } << ( static_cast<unsigned int>( vk ) & 63u );
        const bool isDown = keys.IsDown( vk );
        const bool wasDown = ( m_previousKeys[word] & bit ) != 0u;

        if ( isDown && !wasDown && vk != VK_F8 ) // F8 is reserved for recorder toggle
        {
            if ( const char* keyName = VirtualKeyToString( vk ) )
            {
                RecordedInteractionAction keyAction;
                keyAction.frame = relativeFrame;
                keyAction.kind = RecordedActionKind::PressKey;
                keyAction.virtualKey = vk;
                strncpy_s( keyAction.keyName, sizeof( keyAction.keyName ), keyName, _TRUNCATE );
                (void)AppendAction( keyAction );
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
