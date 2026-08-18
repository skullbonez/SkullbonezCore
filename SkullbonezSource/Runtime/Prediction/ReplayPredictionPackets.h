/*
File: SkullbonezSource/Runtime/Prediction/ReplayPredictionPackets.h
Purpose:
  Publishes replay prediction command, capability, and presentation policy values.

Summary:
  Callers exchange typed policy decisions without borrowing prediction worker
  state. ReplayPrediction retains the active detail and path-presentation
  choices; these values describe commands, archive capabilities, and required
  transition effects without gaining lifecycle authority.

Invariants:
  - Packet enums carry no scheduling authority.
  - Archive detail capability never overwrites the operator's active preference.
  - Only explicit AllBodiesSpace policy permits independent future roots;
    physics force configuration is not a presentation-policy selector.
  - Explicit enum values are interpreted only inside the current process/report schema.

Related:
  - SkullbonezSource/Runtime/Prediction/ReplayPrediction.h
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionScheduling.h
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionView.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include <cstdint>

namespace SkullbonezCore::Runtime
{
enum class ReplayPredictionBuildMode : uint8_t
{
    Undecided,
    Amortized,
    Instant
};

// Concept: detail mode is the operator's retained preference, while archive
// capability only describes what one captured artifact contains. Loading an
// artifact must never overwrite the active preference.
enum class ReplayPredictionDetailMode : uint8_t
{
    High,
    Low
};

// One policy predicate gates every predicted cause-window consumer. Recorded
// cause rows remain available in Low because the preference owns future-solver
// evidence only; predicted rows require High for both drawing and hit testing.
constexpr bool ReplayPredictionCauseWindowAvailable( ReplayPredictionDetailMode mode, bool predictionRows ) noexcept
{
    return !predictionRows || mode == ReplayPredictionDetailMode::High;
}

struct ReplayPredictionDetailModeCommand
{
    ReplayPredictionDetailMode mode = ReplayPredictionDetailMode::High;
};

enum class ReplayPredictionArchiveDetailCapability : uint8_t
{
    Low,
    High
};

enum class ReplayPredictionGenerationResetReason : uint8_t
{
    Scene,
    Owner,
    PredictToggle
};

// Concept: one mode command expands into explicit effects that App can route to
// the existing prediction and inspection owners. Recorded inspection is absent
// deliberately: a Prediction mode change may clear predicted inspection only.
enum class ReplayPredictionDetailTransitionAction : uint8_t
{
    None = 0,
    RestartGeneration = 1u << 0u,
    ClearPredictionInspection = 1u << 1u,
    ReleaseHighDetailCapacity = 1u << 2u
};

constexpr ReplayPredictionDetailTransitionAction operator|( ReplayPredictionDetailTransitionAction lhs,
                                                            ReplayPredictionDetailTransitionAction rhs ) noexcept
{
    return static_cast<ReplayPredictionDetailTransitionAction>( static_cast<uint8_t>( lhs ) | static_cast<uint8_t>( rhs ) );
}

constexpr bool ReplayPredictionDetailTransitionHas( ReplayPredictionDetailTransitionAction actions,
                                                    ReplayPredictionDetailTransitionAction action ) noexcept
{
    return ( static_cast<uint8_t>( actions ) & static_cast<uint8_t>( action ) ) != 0u;
}

constexpr ReplayPredictionDetailTransitionAction
EvaluateReplayPredictionDetailTransition( ReplayPredictionDetailMode active, ReplayPredictionDetailMode requested ) noexcept
{
    if ( active == requested )
    {
        return ReplayPredictionDetailTransitionAction::None;
    }

    ReplayPredictionDetailTransitionAction actions = ReplayPredictionDetailTransitionAction::RestartGeneration |
                                                     ReplayPredictionDetailTransitionAction::ClearPredictionInspection;

    if ( requested == ReplayPredictionDetailMode::Low )
    {
        actions = actions | ReplayPredictionDetailTransitionAction::ReleaseHighDetailCapacity;
    }

    return actions;
}

constexpr ReplayPredictionDetailMode
ReplayPredictionDetailModeAfterArchiveLoad( ReplayPredictionDetailMode activePreference,
                                            ReplayPredictionArchiveDetailCapability capturedCapability ) noexcept
{
    (void)capturedCapability;
    return activePreference;
}

// Invariant: scene, owner, and Predict resets invalidate one generation, not
// the operator's retained detail preference.
constexpr ReplayPredictionDetailMode
ReplayPredictionDetailModeAfterGenerationReset( ReplayPredictionDetailMode activePreference,
                                                ReplayPredictionGenerationResetReason resetReason ) noexcept
{
    (void)resetReason;
    return activePreference;
}

enum class ReplayPredictionPathPresentation : uint8_t
{
    SelectedCausalTree,
    AllBodiesSpace
};

constexpr bool ReplayPredictionPathPresentationShowsAllBodies( ReplayPredictionPathPresentation presentation ) noexcept
{
    return presentation == ReplayPredictionPathPresentation::AllBodiesSpace;
}
} // namespace SkullbonezCore::Runtime
