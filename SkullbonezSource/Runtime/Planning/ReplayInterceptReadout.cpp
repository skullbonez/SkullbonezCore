/*
File: SkullbonezSource/Runtime/Planning/ReplayInterceptReadout.cpp
Purpose:
  Implements the incremental Replay prediction closest-approach scan.

Summary:
  The owner treats each presentation span as an immutable published prefix.
  It resumes at the first unseen frame while a stable build key is active and
  restarts from frame zero whenever prediction identity or topology changes.

Glossary:
  Scan key: Values that identify one coherent prediction frame bank.
  Arg-min: Frame index that owns the smallest observed distance.

Invariants:
  - Missing ship or target rows are skipped without advancing a false minimum.
  - Intercept classification includes the engine's 0.005 contact slop and
    accepts distance at or below the two collider radii plus that slop.
  - ETA uses the prediction-local frame index and physics fixed-step duration.

Related:
  - ReplayInterceptReadout.h
  - ReplayPredictionPublication.h
*/
#include "ReplayInterceptReadout.h"

#include <cmath>

namespace SkullbonezCore::Runtime
{
namespace
{
const RunReplayPredictionBodySample* FindPredictionBody( const RunReplayPredictionFrame& frame,
                                                         Physics::PhysicsSceneObjectId id ) noexcept
{

    for ( const RunReplayPredictionBodySample& body : frame.bodies )
    {

        if ( body.id.value == id.value )
        {
            return &body;
        }
    }

    return nullptr;
}

float MagnitudeSquared( const Math::Vector::Vector3& value ) noexcept
{
    return Dot( value, value );
}
} // namespace


void ReplayInterceptReadout::SetTarget( Physics::PhysicsSceneObjectId id, Physics::ModelRowHint modelRow ) noexcept
{

    if ( id.value == m_targetId.value && modelRow.value == m_targetModelRow.value )
    {
        return;
    }

    m_targetId = id;
    m_targetModelRow = modelRow;
    ResetScan();
}


void ReplayInterceptReadout::ClearTarget() noexcept
{
    m_targetId = {};
    m_targetModelRow = {};
    ResetScan();
}


bool ReplayInterceptReadout::HasTarget() const noexcept
{
    return m_targetId.value != 0;
}


Physics::PhysicsSceneObjectId ReplayInterceptReadout::TargetId() const noexcept
{
    return m_targetId;
}


Physics::ModelRowHint ReplayInterceptReadout::TargetModelRow() const noexcept
{
    return m_targetModelRow;
}


const ReplayInterceptView& ReplayInterceptReadout::View() const noexcept
{
    return m_view;
}


void ReplayInterceptReadout::ResetScan() noexcept
{
    m_scanShipId = {};
    m_scanTargetId = {};
    m_scanShipRadius = 0.0f;
    m_scanTargetRadius = 0.0f;
    m_scanGeneration = 0;
    m_scanTopologyVersion = 0;
    m_scannedFrameCount = 0;
    m_scanUsingBuildFrames = false;
    m_scanKeyValid = false;
    m_view = {};
}


void ReplayInterceptReadout::Update( const ReplayInterceptUpdateInput& input ) noexcept
{

    if ( !input.enabled || input.shipId.value == 0 || input.targetId.value == 0 ||
         input.shipId.value == input.targetId.value || input.frames.empty() || input.shipRadius <= 0.0f ||
         input.targetRadius <= 0.0f )
    {
        ResetScan();
        return;
    }

    const bool keyChanged = !m_scanKeyValid || m_scanShipId.value != input.shipId.value ||
                            m_scanTargetId.value != input.targetId.value || m_scanGeneration != input.generation ||
                            m_scanTopologyVersion != input.topologyVersion ||
                            m_scanUsingBuildFrames != input.usingBuildFrames || m_scanShipRadius != input.shipRadius ||
                            m_scanTargetRadius != input.targetRadius || input.frames.size() < m_scannedFrameCount;

    if ( keyChanged )
    {
        ResetScan();
        m_scanShipId = input.shipId;
        m_scanTargetId = input.targetId;
        m_scanShipRadius = input.shipRadius;
        m_scanTargetRadius = input.targetRadius;
        m_scanGeneration = input.generation;
        m_scanTopologyVersion = input.topologyVersion;
        m_scanUsingBuildFrames = input.usingBuildFrames;
        m_scanKeyValid = true;
    }

    float bestDistanceSquared = m_view.valid ? m_view.missDistance * m_view.missDistance : 0.0f;

    for ( std::size_t frameSlot = m_scannedFrameCount; frameSlot < input.frames.size(); ++frameSlot )
    {
        const RunReplayPredictionFrame& frame = input.frames[frameSlot];
        const RunReplayPredictionBodySample* ship = FindPredictionBody( frame, input.shipId );
        const RunReplayPredictionBodySample* target = FindPredictionBody( frame, input.targetId );

        if ( !ship || !target )
        {
            continue;
        }

        const Math::Vector::Vector3 separation = ship->position - target->position;
        const float distanceSquared = MagnitudeSquared( separation );

        // Invariant: strict comparison preserves the first frame when two
        // samples have exactly the same miss distance.

        if ( m_view.valid && distanceSquared >= bestDistanceSquared )
        {
            continue;
        }

        const Math::Vector::Vector3 relativeVelocity = ship->linearVelocity - target->linearVelocity;
        bestDistanceSquared = distanceSquared;
        m_view.valid = true;
        m_view.shipId = input.shipId;
        m_view.targetId = input.targetId;
        m_view.closestFrame = frame.frameIndex;
        m_view.missDistance = std::sqrt( distanceSquared );
        m_view.relativeSpeed = std::sqrt( MagnitudeSquared( relativeVelocity ) );
        m_view.etaSeconds = static_cast<float>( frame.frameIndex ) * PHYSICS_FIXED_DT;
        m_view.shipPosition = ship->position;
        m_view.targetPosition = target->position;
        m_view.topologyVersion = input.topologyVersion;
        m_view.intercept = m_view.missDistance <= input.shipRadius + input.targetRadius + REPLAY_INTERCEPT_CONTACT_SLOP;
    }

    m_scannedFrameCount = input.frames.size();
}
} // namespace SkullbonezCore::Runtime
