/*
File: SkullbonezSource/Runtime/Planning/ReplayInterceptReadout.h
Purpose:
  Owns the bounded closest-approach scan over published Replay prediction rows.

Summary:
  A dedicated Legacy pick selects one durable target id. The readout then scans
  only newly published future frames for the prediction root/target minimum and
  publishes a small value packet for world markers and overlay text.

Glossary:
  Closest approach: Prediction frame with the smallest ship-to-target distance.
  Published prefix: Contiguous prediction rows that are safe for frame readers.
  Topology version: Generation tag for the prediction node set and row order.

Invariants:
  - The scan never reads beyond the caller's published frame span.
  - Equal distances keep the earlier frame, making tie-breaking deterministic.
  - A generation, topology, frame-bank, identity, or radius change resets the
    cursor before any new rows are consumed.
  - The owner allocates no storage and retains no borrowed frame span.

Related:
  - ReplayPredictionView.h publishes the frame prefix.
  - ReplayRuntime.cpp composes selection, scan, and rendering.
  - SkullbonezTests/TestReplayInterceptReadout.cpp
*/
#pragma once

#include "../Prediction/ReplayPredictionView.h"
#include "../../Physics/PhysicsTimestep.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace SkullbonezCore::Runtime
{
// Why: the discrete prediction frames and contact solver can leave a touching
// pair a few thousandths outside the authored radius sum. Contact is still a
// proximity intercept, so classification admits one solver-slop-width.
inline constexpr float REPLAY_INTERCEPT_CONTACT_SLOP = 0.005f;

struct ReplayInterceptView
{
    bool valid = false;
    bool intercept = false;
    Physics::PhysicsSceneObjectId shipId;
    Physics::PhysicsSceneObjectId targetId;
    ReplayFrameIndex closestFrame = 0;
    float missDistance = 0.0f;
    float relativeSpeed = 0.0f;
    float etaSeconds = 0.0f;
    Math::Vector::Vector3 shipPosition = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 targetPosition = Math::Vector::ZERO_VECTOR;
    uint32_t topologyVersion = 0;
};

struct ReplayInterceptUpdateInput
{
    std::span<const RunReplayPredictionFrame> frames;
    Physics::PhysicsSceneObjectId shipId;
    Physics::PhysicsSceneObjectId targetId;
    float shipRadius = 0.0f;
    float targetRadius = 0.0f;
    uint32_t generation = 0;
    uint32_t topologyVersion = 0;
    bool usingBuildFrames = false;
    bool enabled = false;
};

class ReplayInterceptReadout
{
  public:
    // Selection retains durable scene identity; the model row is a repairable
    // hint that the Runtime composition boundary refreshes after topology work.
    void SetTarget( Physics::PhysicsSceneObjectId id, Physics::ModelRowHint modelRow ) noexcept;
    void ClearTarget() noexcept;
    bool HasTarget() const noexcept;
    Physics::PhysicsSceneObjectId TargetId() const noexcept;
    Physics::ModelRowHint TargetModelRow() const noexcept;
    const ReplayInterceptView& View() const noexcept;
    // Lifetime: the frame prefix is borrowed only for this synchronous scan.
    // Callers may replace or release its backing bank as soon as Update returns.
    void Update( const ReplayInterceptUpdateInput& input ) noexcept;

  private:
    void ResetScan() noexcept;

    Physics::PhysicsSceneObjectId m_targetId;
    Physics::ModelRowHint m_targetModelRow;
    Physics::PhysicsSceneObjectId m_scanShipId;
    Physics::PhysicsSceneObjectId m_scanTargetId;
    float m_scanShipRadius = 0.0f;
    float m_scanTargetRadius = 0.0f;
    uint32_t m_scanGeneration = 0;
    uint32_t m_scanTopologyVersion = 0;
    std::size_t m_scannedFrameCount = 0;
    bool m_scanUsingBuildFrames = false;
    bool m_scanKeyValid = false;
    ReplayInterceptView m_view;
};
} // namespace SkullbonezCore::Runtime
