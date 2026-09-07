/*
File: OperatorUiPhase.h
Purpose:
  Owns the value-only phase walk for one operator UI frame.

Invariants:
  - The only phase walk is Idle -> Snapshot -> Composed -> Submitted -> Complete.
  - The owner terminates on every repeated, skipped, or backward phase operation.
  - The stored snapshot and submission plan are values; no borrowed reference survives a call.
  - UI visibility never changes the immutable frame facts consumed by GameUI.

Related:
  - Runtime/App/OperatorEditorFramePhase.cpp
  - Runtime/RuntimeFrameViews.h
*/

#pragma once

#include "../../Core/FatalError.h"
#include "../RuntimeFrameViews.h"

#include <cstdint>

namespace SkullbonezCore::Runtime
{

struct OperatorUiFrameSnapshot
{
    RuntimeUiTextFrameFacts uiText;
    RuntimeFrameMetricsSnapshot metrics;
    int viewportWidth = 0;
    int viewportHeight = 0;
};


struct OperatorUiSubmissionPlan
{
    bool composeGameUi = false;
    bool submitOverlay = false;
    bool submitReplay = false;
    bool finalizeOverlay = false;
};

class OperatorUiPhaseOwner
{
  public:
    enum class Phase : uint8_t
    {
        Idle,
        Snapshot,
        Composed,
        Submitted,
        Complete
    };

    // The focused truth-table test exercises the same predicate that guards
    // every public operation; callers cannot choose a recoverable fallback.
    static constexpr bool IsLegalTransition( Phase current, Phase next )
    {
        return ( current == Phase::Idle && next == Phase::Snapshot ) ||
               ( current == Phase::Snapshot && next == Phase::Composed ) ||
               ( current == Phase::Composed && next == Phase::Submitted ) ||
               ( current == Phase::Submitted && next == Phase::Complete );
    }

    void Begin( const OperatorUiFrameSnapshot& snapshot )
    {
        AdvanceOrFatal( Phase::Snapshot, "Begin" );

        m_snapshot = snapshot;
    }

    // Resolves the complete presentation policy before App opens any UI GPU
    // pass. Keeping the decision with the phase owner prevents the composition
    // root from selecting individual renderer operations itself.
    void Compose( bool textOnly, bool gameUiNeedsTextPass, bool gameUiVisible, bool profilerBars )
    {
        AdvanceOrFatal( Phase::Composed, "Compose" );

        if ( !textOnly )
        {
            m_submissionPlan = { gameUiNeedsTextPass, !gameUiVisible, true, !gameUiVisible && !profilerBars };
        }
    }

    void RecordGpuSubmission( int gameUiDrawCalls )
    {
        AdvanceOrFatal( Phase::Submitted, "RecordGpuSubmission" );

        m_gameUiDrawCalls = gameUiDrawCalls;
    }

    void Complete()
    {
        AdvanceOrFatal( Phase::Complete, "Complete" );
    }
    Phase CurrentPhase() const
    {
        return m_phase;
    }
    const OperatorUiFrameSnapshot& Snapshot() const
    {
        return m_snapshot;
    }
    const OperatorUiSubmissionPlan& SubmissionPlan() const
    {
        return m_submissionPlan;
    }
    int GameUiDrawCalls() const
    {
        return m_gameUiDrawCalls;
    }

  private:
    void AdvanceOrFatal( Phase next, const char* operation )
    {
        const Phase current = m_phase;

        if ( !IsLegalTransition( current, next ) )
        {
            SB_FATAL( "Runtime/OperatorUiPhaseOwner", "Illegal phase transition. operation=%s current=%u next=%u", operation,
                      static_cast<unsigned int>( current ), static_cast<unsigned int>( next ) );
        }

        m_phase = next;
    }

    Phase m_phase = Phase::Idle;
    OperatorUiFrameSnapshot m_snapshot;
    OperatorUiSubmissionPlan m_submissionPlan;
    int m_gameUiDrawCalls = 0;
};
} // namespace SkullbonezCore::Runtime
