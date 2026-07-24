/*
File: SkullbonezSource/Runtime/Replay/ReplayTripPlanner.h
Purpose:
  Owns the bounded Lambert-seeded trip-planning state machine and ghost arcs.

Summary:
  UI and automation enqueue small commands. The composition root supplies live
  body values and published prediction frames, then applies any returned
  velocity mutation through the normal Replay velocity-edit boundary.

Glossary:
  Seed: Analytic Lambert departure velocity used as the first candidate burn.
  Correction: First-order velocity adjustment derived from a real prediction miss.
  Ghost arc: Downsampled root path retained from one completed shooting iteration.
  TOF: Requested time of flight from the current live state to the target.

Invariants:
  - The owner stores no prediction, Physics, scene, or callback borrow.
  - Command and ghost storage are fixed-capacity and never allocate.
  - Analytic math proposes candidates; only completed engine predictions decide
    convergence or failure.
  - At most four candidate generations are submitted for one plan.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp
  - SkullbonezSource/Maths/OrbitalMechanics.h
  - SkullbonezTests/TestReplayTripPlanner.cpp
*/
#pragma once

#include "ReplayInterceptReadout.h"
#include "ReplayPredictionView.h"
#include "../../Maths/OrbitalMechanics.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace SkullbonezCore::Runtime
{
inline constexpr std::size_t REPLAY_TRIP_PLANNER_MAX_ITERATIONS = 4u;
inline constexpr std::size_t REPLAY_TRIP_PLANNER_GHOST_POINTS = 256u;
inline constexpr std::size_t REPLAY_TRIP_PLANNER_COMMAND_CAPACITY = 8u;
inline constexpr float REPLAY_TRIP_PLANNER_MIN_TOF_SECONDS = 2.0f;
inline constexpr float REPLAY_TRIP_PLANNER_INTERCEPT_DISTANCE = 3.2f;
inline constexpr float REPLAY_TRIP_PLANNER_CORRECTION_GAIN = 0.8f;

enum class ReplayTripPlannerState : uint8_t
{
    Idle,
    Seeding,
    AwaitingPrediction,
    Correcting,
    Converged,
    Failed
};

enum class ReplayTripPlannerCommandKind : uint8_t
{
    None,
    TogglePanel,
    DecreaseTimeOfFlight,
    IncreaseTimeOfFlight,
    SetTimeOfFlight,
    Plan,
    Commit,
    Cancel
};

struct ReplayTripPlannerCommand
{
    ReplayTripPlannerCommandKind kind = ReplayTripPlannerCommandKind::None;
    float timeOfFlightSeconds = 0.0f;
};

struct ReplayTripPlannerBodyState
{
    Physics::PhysicsSceneObjectId id;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    float mass = 0.0f;
    bool valid = false;
};

struct ReplayTripPlannerLiveInput
{
    ReplayTripPlannerBodyState sun;
    ReplayTripPlannerBodyState ship;
    ReplayTripPlannerBodyState target;
    float gravitationalConstant = 0.0f;
    float predictionHorizonSeconds = 0.0f;
    bool mutualGravityEnabled = false;
    bool targetSelected = false;
    bool liveAdvanceHeld = false;
    const char* targetName = nullptr;
};

struct ReplayTripPlannerPredictionInput
{
    std::span<const RunReplayPredictionFrame> frames;
    ReplayInterceptView intercept;
    Physics::PhysicsSceneObjectId shipId;
    Physics::PhysicsSceneObjectId targetId;
    uint32_t generation = 0;
    bool complete = false;
    bool cancelled = false;
    bool liveAdvanceHeld = false;
    bool targetAvailable = false;
};

struct ReplayTripPlannerVelocityMutation
{
    Physics::PhysicsSceneObjectId bodyId;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    bool requested = false;
    bool prepareBaseline = false;
    bool restoresPrePlanVelocity = false;
};

struct ReplayTripPlannerGhostArc
{
    std::array<Math::Vector::Vector3, REPLAY_TRIP_PLANNER_GHOST_POINTS> points{};
    std::size_t pointCount = 0;
};

struct ReplayTripPlannerView
{
    std::array<ReplayTripPlannerGhostArc, REPLAY_TRIP_PLANNER_MAX_ITERATIONS> ghosts{};
    std::array<float, REPLAY_TRIP_PLANNER_MAX_ITERATIONS> iterationMissDistances{};
    ReplayTripPlannerState state = ReplayTripPlannerState::Idle;
    Physics::PhysicsSceneObjectId shipId;
    Physics::PhysicsSceneObjectId targetId;
    Math::Vector::Vector3 candidateVelocity = Math::Vector::ZERO_VECTOR;
    float timeOfFlightSeconds = 15.9f;
    float missDistance = 0.0f;
    uint32_t iteration = 0;
    std::size_t ghostCount = 0;
    std::size_t iterationMissCount = 0;
    char targetName[32] = {};
    bool visible = false;
    bool available = false;
    bool noSolution = false;
};

class ReplayTripPlanner
{
  public:
    bool QueueCommand( const ReplayTripPlannerCommand& command ) noexcept;
    ReplayTripPlannerVelocityMutation BeginFrame( const ReplayTripPlannerLiveInput& input ) noexcept;
    ReplayTripPlannerVelocityMutation ObservePrediction( const ReplayTripPlannerPredictionInput& input ) noexcept;
    // Returns the original live velocity through the normal Replay mutation
    // boundary. Callers must apply the result before discarding the plan.
    ReplayTripPlannerVelocityMutation CancelActivePlan() noexcept;
    void ConfirmVelocityApplied() noexcept;
    void Abort() noexcept;
    // Scene teardown may discard the old Physics world without rollback.
    void ResetForSceneDiscard() noexcept;
    const ReplayTripPlannerView& View() const noexcept;
    bool RequiresLiveInput() const noexcept;
    bool AwaitingPrediction() const noexcept;

    static Math::Vector::Vector3 FirstOrderCorrection( const Math::Vector::Vector3& velocity,
                                                       const Math::Vector::Vector3& shipAtClosest,
                                                       const Math::Vector::Vector3& targetAtClosest,
                                                       float closestTimeSeconds ) noexcept;

  private:
    ReplayTripPlannerVelocityMutation BeginPlan( const ReplayTripPlannerLiveInput& input ) noexcept;
    void RetainGhost( std::span<const RunReplayPredictionFrame> frames, Physics::PhysicsSceneObjectId shipId ) noexcept;
    void Fail() noexcept;
    void ClearPlanState() noexcept;

    std::array<ReplayTripPlannerCommand, REPLAY_TRIP_PLANNER_COMMAND_CAPACITY> m_commands{};
    std::size_t m_commandCount = 0;
    ReplayTripPlannerView m_view;
    Math::Vector::Vector3 m_prePlanVelocity = Math::Vector::ZERO_VECTOR;
    float m_previousMissDistance = 0.0f;
    uint32_t m_lastObservedGeneration = 0;
    bool m_prePlanVelocityValid = false;
    bool m_hasPreviousMiss = false;
};
} // namespace SkullbonezCore::Runtime
