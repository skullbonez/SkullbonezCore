/*
File: ReplayTimelinePackets.h
Purpose:
  Publishes replay track identity, scrubber facts, and pure normalized-track conversions.

Summary:
  Timeline owns mutable cursor state. Runtime input, Presentation, and
  validation consume these values and pure conversions without including the
  ReplayScrubber owner.

Glossary:
  Live edge: Newest retained replay sample.
  Present marker: Solver-track position separating retained past from predicted future.

Invariants:
  - ReplayScrubberView is a value copy and cannot move the retained cursor.
  - Track conversions clamp their normalized results to the valid zero-to-one range.

Related:
  - ReplayScrubber.h
  - ReplayOverlaySurface.h
*/
#pragma once

#include <algorithm>
#include <cmath>

namespace SkullbonezCore::Runtime
{
inline constexpr float REPLAY_SCRUBBER_LIVE_THRESHOLD = 0.995f;
inline constexpr float REPLAY_SCRUBBER_PRESENT_EPSILON = 0.0035f;

namespace ReplayScrubberOperations
{
inline bool ReplayTimelineHasFuture( float presentT ) noexcept
{
    return presentT < REPLAY_SCRUBBER_LIVE_THRESHOLD;
}

inline bool ReplayAtPresentTrackPosition( float position, float presentT ) noexcept
{

    if ( !ReplayTimelineHasFuture( presentT ) )
    {
        return position >= REPLAY_SCRUBBER_LIVE_THRESHOLD;
    }

    return std::fabs( position - presentT ) <= REPLAY_SCRUBBER_PRESENT_EPSILON;
}

inline bool ReplayTrackPositionIsFuture( float position, float presentT ) noexcept
{
    return ReplayTimelineHasFuture( presentT ) && position > presentT + REPLAY_SCRUBBER_PRESENT_EPSILON;
}

inline float ReplaySolverNormalizedFromTrack( float position, float presentT ) noexcept
{

    if ( !ReplayTimelineHasFuture( presentT ) )
    {
        return std::clamp( position, 0.0f, 1.0f );
    }

    return std::clamp( position / (std::max)( presentT, 0.0001f ), 0.0f, 1.0f );
}

inline float ReplayPredictionNormalizedFromTrack( float position, float presentT ) noexcept
{

    if ( !ReplayTimelineHasFuture( presentT ) )
    {
        return 0.0f;
    }

    return std::clamp( ( position - presentT ) / ( 1.0f - presentT ), 0.0f, 1.0f );
}
} // namespace ReplayScrubberOperations

enum class RunReplayTrack
{
    Presentation,
    Solver
};

struct ReplayScrubberView
{
    bool visible = false;
    bool historicalSamplePaused = false;
    bool liveAdvanceHeld = false;
    bool restoreConsumedThisFrame = false;
    RunReplayTrack activeTrack = RunReplayTrack::Solver;
    RunReplayTrack saveMessageTrack = RunReplayTrack::Solver;
    float position = 1.0f;
    float presentationPosition = 1.0f;
    float solverPosition = 1.0f;
    int mouseX = 0;
    int mouseY = 0;
    double visibleUntil = 0.0;
    float visibleAlpha = 0.0f;
    double saveMessageUntil = 0.0;
    char saveMessage[96] = {};
};
} // namespace SkullbonezCore::Runtime
