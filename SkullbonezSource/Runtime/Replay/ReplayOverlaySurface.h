/*
File: ReplayOverlaySurface.h
Purpose:
  Publishes the bounded replay control-surface geometry used by automation input.

Summary:
  Presentation owns pixel layout. Automation receives only stable constants and
  rectangle queries needed to drive the same visible controls as an operator.

Glossary:
  Control surface: Pixel-space replay control geometry shared by drawing and hit testing.
  Scrubber: Timeline control for retained past and predicted future samples.

Invariants:
  - Automation queries the same rectangle functions used by Presentation drawing.
  - Prediction-horizon values remain clamped to Capture-owned limits.

Related:
  - ReplayOverlayLayout.h keeps internal cause-tree and layout implementation details.
  - ReplayCaptureLimits.h owns the future-horizon cap.
*/
#pragma once

#include "ReplayCaptureLimits.h"
#include "ReplayTimelinePackets.h"
#include "../../UI/UIDraw.h"

namespace SkullbonezCore::Runtime::ReplayOverlay
{
inline constexpr double REPLAY_SCRUBBER_VISIBLE_SECONDS = 1.40;
inline constexpr float REPLAY_PREDICTION_MIN_SECONDS = 1.0f;
inline constexpr float REPLAY_PREDICTION_MAX_SECONDS = REPLAY_FUTURE_MAX_SECONDS;

UI::UIRect ReplayScrubberTrackRect( int screenW, int screenH, RunReplayTrack track );
UI::UIRect ReplayScrubberBranchButtonRect( int screenW, int screenH );
UI::UIRect ReplayScrubberPauseButtonRect( int screenW, int screenH );
UI::UIRect ReplayScrubberVelocityEditToggleRect( int screenW, int screenH );
UI::UIRect ReplayScrubberPredictToggleRect( int screenW, int screenH );
UI::UIRect ReplayScrubberPastPathToggleRect( int screenW, int screenH );
} // namespace SkullbonezCore::Runtime::ReplayOverlay
