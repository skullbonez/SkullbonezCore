/*
File: OperatorUiPhase.h
Purpose:
  Owns the value-only phase walk for one operator UI frame.

Summary:
  App supplies one detached snapshot shared by GameUI and ImGui. The phase
  owner records composition and GPU submission completion, emits typed process
  commands, and retains no subsystem pointer or callback.

Invariants:
  - The only phase walk is Idle -> Snapshot -> Composed -> Submitted -> CommandsEmitted -> Complete.
  - The stored snapshot and returned commands are values; no owner borrow survives a call.
  - Surface visibility never changes which immutable frame facts either UI consumes.

Related:
  - Runtime/App/OperatorEditorFramePhase.cpp
  - Runtime/RuntimeFrameViews.h
*/
#pragma once

#include "../RuntimeFrameViews.h"

#include <cstdint>

namespace SkullbonezCore::Runtime
{
enum class OperatorUiSurfaceCommand : uint8_t
{
    None,
    ShowGameUi,
    ShowImGui
};

struct OperatorUiFrameSnapshot
{
    RuntimeUiTextFrameFacts uiText;
    RuntimeFrameMetricsSnapshot metrics;
    int viewportWidth = 0;
    int viewportHeight = 0;
    bool secondarySurfaceVisible = false;
};

struct OperatorUiProcessCommands
{
    OperatorUiSurfaceCommand surface = OperatorUiSurfaceCommand::None;
    bool requestTracyStandardCapture = false;
};

struct OperatorUiSubmissionPlan
{
    bool composeGameUi = false;
    bool submitOverlay = false;
    bool submitReplay = false;
    bool finalizeOverlay = false;
};

inline OperatorUiSubmissionPlan ResolveOperatorUiSubmissionPlan( bool textOnly, bool gameUiNeedsTextPass,
                                                                 bool gameUiVisible, bool profilerBars )
{
    if ( textOnly )
    {
        return {};
    }

    return { gameUiNeedsTextPass, !gameUiVisible, true, !gameUiVisible && !profilerBars };
}

class OperatorUiPhaseOwner
{
  public:
    enum class Phase : uint8_t
    {
        Idle,
        Snapshot,
        Composed,
        Submitted,
        CommandsEmitted,
        Complete
    };

    bool Begin( const OperatorUiFrameSnapshot& snapshot )
    {
        if ( !Advance( Phase::Idle, Phase::Snapshot ) )
        {
            return false;
        }
        m_snapshot = snapshot;
        return true;
    }

    bool MarkComposed() { return Advance( Phase::Snapshot, Phase::Composed ); }

    bool RecordGpuSubmission( int gameUiDrawCalls )
    {
        if ( !Advance( Phase::Composed, Phase::Submitted ) )
        {
            return false;
        }
        m_gameUiDrawCalls = gameUiDrawCalls;
        return true;
    }

    bool EmitCommands( const OperatorUiProcessCommands& commands )
    {
        if ( !Advance( Phase::Submitted, Phase::CommandsEmitted ) )
        {
            return false;
        }
        m_commands = commands;
        return true;
    }

    bool Complete() { return Advance( Phase::CommandsEmitted, Phase::Complete ); }
    Phase CurrentPhase() const { return m_phase; }
    const OperatorUiFrameSnapshot& Snapshot() const { return m_snapshot; }
    const OperatorUiProcessCommands& Commands() const { return m_commands; }
    int GameUiDrawCalls() const { return m_gameUiDrawCalls; }

  private:
    bool Advance( Phase expected, Phase next )
    {
        if ( m_phase != expected )
        {
            return false;
        }
        m_phase = next;
        return true;
    }

    Phase m_phase = Phase::Idle;
    OperatorUiFrameSnapshot m_snapshot;
    OperatorUiProcessCommands m_commands;
    int m_gameUiDrawCalls = 0;
};
} // namespace SkullbonezCore::Runtime
