/*
File: SkullbonezSource/Runtime/App/ReplayPredictionDrawing.cpp
Purpose:
  Draws immutable replay prediction and retained-path publication as overlay geometry.

Summary:
  ReplayPrediction publishes frame, trajectory, marker, and reveal values before
  rendering. This unit consumes those const values and appends fixed-capacity
  retained ribbon chunks as published prefixes grow. The deterministic fidelity
  lane retains the frame-local builder as an independent visual oracle.

Glossary:
  Frame-local ribbon: Screen-space-width overlay stroke emitted through
    EditorTracer for the deterministic visual oracle.
  Draw quota: Frame-local cap for ordinary replay ribbon segments.
  Retained range chunk: Small stable slice appended when one trajectory outgrows
    its current compact GPU range.
  Frame-local prediction draw: Full visible trajectory submission rebuilt only
    by the deterministic fidelity oracle.

Invariants:
  - Drawing receives const prediction and presentation values only.
  - Drawing never starts, advances, cancels, or completes prediction work.
  - Quota exhaustion records dropped logical segments without allocating.
  - Product prediction geometry appends only newly revealed segments; an
    unchanged publication exits before scanning trajectory records.
  - Deterministic fidelity bypasses retained state so golden comparison remains
    an independent frame-local oracle.
  - Held velocity preview traverses only the selected display stride and does
    not mutate retained non-selected geometry.

Related:
  - ReplayPrediction.h
  - ReplayPredictionDrawing.h
  - ReplayPredictionPresentation.h
  - Agentic/Reference/engine-glossary.md
*/
#include "ReplayPredictionDrawing.h"
#include "../Replay/ReplayAuthoring.h"
#include "../Prediction/ReplayPrediction.h"
#include "ReplayPredictionPresentation.h"
#include "ReplayPredictionComposition.h"
#include "../Prediction/ReplayPredictionPublicationOperations.h"
#include "../Replay/ReplayPresentationSubmission.h"
#include "../Replay/ReplayVisualPacket.h"
#include "../Editor/EditorTools.h"
#include "../Tools/RuntimeTools.h"
#include "../Scene/SceneEntityStore.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsTimestep.h"
#include "../../Core/Config.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
namespace Physics = SkullbonezCore::Physics;
using namespace SkullbonezCore::Runtime::ReplayOverlay;
using namespace SkullbonezCore::Runtime::ReplayPresentationSubmissionOperations;
using namespace SkullbonezCore::Runtime::ReplayPredictionPublicationOperations;
using namespace SkullbonezCore::Runtime::ReplayPredictionSchedulingOperations;
using SkullbonezCore::Math::Vector::Vector3;

namespace
{
constexpr double REPLAY_PREDICTION_MAX_WORK_MILLISECONDS = 5.0;
constexpr std::size_t REPLAY_PATH_MAX_FUTURE_NODES = REPLAY_VISUAL_FUTURE_NODE_CAPACITY;
constexpr std::size_t REPLAY_RIBBON_SEGMENTS_PER_PATH_SEGMENT = 1;
constexpr float REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ = 0.0001f;

// Units: simulation distance per second. This presentation-only tuning point
// puts ordinary launched bodies near the middle of the heat ramp while leaving
// high-energy impacts visibly red.
constexpr float REPLAY_PATH_VELOCITY_HEAT_MAX_SPEED = 80.0f;
constexpr uint16_t REPLAY_TRAJECTORY_COMMITTED_BRANCH = 0;
constexpr uint16_t REPLAY_TRAJECTORY_BUILD_BRANCH = 1;

bool TryResolveReplayAuthoredPathColor( const SceneEntityStore& entities, Physics::PhysicsSceneObjectId bodyId, float& r,
                                        float& g, float& b )
{
    const int entityIndex = entities.FindBySceneObjectId( bodyId );
    const SceneEntityRecord* entity = entities.TryGet( entityIndex );

    if ( !entity )
    {
        return false;
    }

    r = entity->renderMaterial.baseColor[0];
    g = entity->renderMaterial.baseColor[1];
    b = entity->renderMaterial.baseColor[2];
    return true;
}

struct ReplayRibbonDrawQuota
{
    // Counts internal ribbon records, not logical trajectory lines. The tracer
    // merges legacy two-style inputs into one record per path segment.
    std::size_t remainingRibbonSegments = 0;
};

ReplayRibbonDrawQuota BeginReplayRibbonDrawQuota( const EditorTracer& tracer )
{
    ReplayRibbonDrawQuota quota;
    quota.remainingRibbonSegments = tracer.ReplayPathRibbonSegmentCapacityRemaining();
    return quota;
}

bool TryReserveReplayPathRibbonSegment( ReplayRibbonDrawQuota* quota )
{
    if ( !quota )
    {
        return true;
    }

    if ( quota->remainingRibbonSegments < REPLAY_RIBBON_SEGMENTS_PER_PATH_SEGMENT )
    {
        quota->remainingRibbonSegments = 0;
        return false;
    }

    quota->remainingRibbonSegments -= REPLAY_RIBBON_SEGMENTS_PER_PATH_SEGMENT;
    return true;
}

// Invariant: traversal continues after quota exhaustion. Every later logical
// segment is cheap to inspect and must be counted in its lane even though no
// vertex payload is emitted. Ordinary and baseline paths share this accounting
// contract; only their final tracer record shapes differ.
bool TryAccountReplayPathSegment( EditorTracer& tracer, ReplayRibbonDrawQuota* quota,
                                  SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane )
{
    if ( tracer.ReplayPathRibbonSegmentCapacityRemaining() < REPLAY_RIBBON_SEGMENTS_PER_PATH_SEGMENT )
    {
        if ( quota )
        {
            quota->remainingRibbonSegments = 0;
        }

        tracer.RecordReplayRibbonDroppedSegments( lane );
        return false;
    }

    if ( !TryReserveReplayPathRibbonSegment( quota ) )
    {
        tracer.RecordReplayRibbonDroppedSegments( lane );
        return false;
    }

    return true;
}

void AddOrAccountReplayPathSegment( EditorTracer& tracer, ReplayRibbonDrawQuota* quota, const Vector3& start,
                                    const Vector3& end, float r, float g, float b,
                                    SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane, float emphasis = 0.0f )
{
    if ( !TryAccountReplayPathSegment( tracer, quota, lane ) )
    {
        return;
    }

    tracer.AddReplayPathSegment( start, end, r, g, b, lane, emphasis );
}

void AddOrAccountReplayBaselinePathSegment( EditorTracer& tracer, ReplayRibbonDrawQuota* quota, const Vector3& start,
                                            const Vector3& end, float r, float g, float b )
{
    if ( !TryAccountReplayPathSegment( tracer, quota, SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::BaselineRoot ) )
    {
        return;
    }

    tracer.AddReplayBaselinePathSegment( start, end, r, g, b );
}

ReplayTrajectoryRecordKey ReplayTrajectoryKey( Physics::PhysicsSceneObjectId bodyId, ReplayTrajectoryLane lane,
                                               uint16_t branchOrdinal )
{
    ReplayTrajectoryRecordKey key;
    key.bodyId = bodyId;
    key.lane = lane;
    key.branchOrdinal = branchOrdinal;
    return key;
}

uint16_t ReplayPredictionChildTrajectoryBranch( std::size_t nodeIndex, bool usingBuildFrames )
{
    const std::size_t branchBase = usingBuildFrames ? REPLAY_PATH_MAX_FUTURE_NODES : 0u;
    return static_cast<uint16_t>(
        (std::min)( branchBase + nodeIndex, static_cast<std::size_t>( ( std::numeric_limits<uint16_t>::max )() ) ) );
}

float ReplayPathFrameT( ReplayFrameIndex frame, ReplayFrameIndex start, ReplayFrameIndex end )
{
    if ( end <= start || frame <= start )
    {
        return 0.0f;
    }

    if ( frame >= end )
    {
        return 1.0f;
    }

    const double numerator = static_cast<double>( frame - start );
    const double denominator = static_cast<double>( end - start );
    return static_cast<float>( std::clamp( numerator / denominator, 0.0, 1.0 ) );
}

float ReplayColorLerp( float a, float b, float t )
{
    return a + ( b - a ) * std::clamp( t, 0.0f, 1.0f );
}

void ReplayDepthPalette( int depth, float& r, float& g, float& b )
{
    // Concept: child depth is encoded as hue first and brightness second. This
    // keeps grandchildren readable even when many branches overlap in the same
    // prediction horizon.
    switch ( std::clamp( depth - 1, 0, 4 ) )
    {
    case 0:
        r = 1.00f;
        g = 0.58f;
        b = 0.18f;
        break;
    case 1:
        r = 0.76f;
        g = 0.92f;
        b = 0.24f;
        break;
    case 2:
        r = 0.26f;
        g = 0.88f;
        b = 0.96f;
        break;
    case 3:
        r = 0.54f;
        g = 0.62f;
        b = 1.00f;
        break;
    default:
        r = 0.96f;
        g = 0.46f;
        b = 0.76f;
        break;
    }

    const float depthDim = std::clamp( static_cast<float>( depth - 1 ) * 0.055f, 0.0f, 0.28f );
    r = std::clamp( r * ( 1.0f - depthDim ) + 0.18f * depthDim, 0.0f, 1.0f );
    g = std::clamp( g * ( 1.0f - depthDim ) + 0.20f * depthDim, 0.0f, 1.0f );
    b = std::clamp( b * ( 1.0f - depthDim ) + 0.24f * depthDim, 0.0f, 1.0f );
}

void ReplayLaneFlatColor( ReplayTrajectoryLane lane, float& r, float& g, float& b )
{
    // Concept: lane-flat is intentionally categorical. A segment's position in
    // time or its body speed cannot change its lane identity.
    switch ( lane )
    {
    case ReplayTrajectoryLane::PastRoot:
        r = 0.96f;
        g = 0.34f;
        b = 0.50f;
        break;
    case ReplayTrajectoryLane::FutureRoot:
        r = 0.46f;
        g = 0.96f;
        b = 0.88f;
        break;
    case ReplayTrajectoryLane::FutureChildIncoming:
        r = 1.00f;
        g = 0.66f;
        b = 0.20f;
        break;
    case ReplayTrajectoryLane::FutureChildOutgoing:
        r = 0.58f;
        g = 0.68f;
        b = 1.00f;
        break;
    case ReplayTrajectoryLane::RetainedTrail:
        r = 0.96f;
        g = 0.78f;
        b = 0.34f;
        break;
    case ReplayTrajectoryLane::BaselineRoot:
        r = 0.34f;
        g = 0.82f;
        b = 0.95f;
        break;
    }
}

void ReplayVelocityHeatColor( float speed, float& r, float& g, float& b )
{
    const float heat = std::clamp( speed / REPLAY_PATH_VELOCITY_HEAT_MAX_SPEED, 0.0f, 1.0f );

    if ( heat < 1.0f / 3.0f )
    {
        const float t = heat * 3.0f;
        r = 0.08f;
        g = ReplayColorLerp( 0.28f, 0.95f, t );
        b = 1.00f;
        return;
    }

    if ( heat < 2.0f / 3.0f )
    {
        const float t = ( heat - 1.0f / 3.0f ) * 3.0f;
        r = ReplayColorLerp( 0.08f, 1.00f, t );
        g = 0.95f;
        b = ReplayColorLerp( 1.00f, 0.10f, t );
        return;
    }

    const float t = ( heat - 2.0f / 3.0f ) * 3.0f;
    r = 1.00f;
    g = ReplayColorLerp( 0.95f, 0.12f, t );
    b = 0.10f;
}

void ReplayTimeGradientColor( float pathT, float& r, float& g, float& b )
{
    const float horizonT = std::clamp( pathT, 0.0f, 1.0f );
    r = ReplayColorLerp( 0.28f, 0.94f, horizonT );
    g = ReplayColorLerp( 1.00f, 0.34f, horizonT );
    b = ReplayColorLerp( 0.78f, 1.00f, horizonT );
}

void ReplayHueColor( Physics::PhysicsSceneObjectId bodyId, float& r, float& g, float& b )
{
    // Why: multiplying stable object identity by the golden-ratio conjugate
    // spreads adjacent ids around the hue wheel without retained palette state.
    constexpr double goldenRatioConjugate = 0.6180339887498948482;
    const double hue = std::fmod( static_cast<double>( bodyId.value ) * goldenRatioConjugate, 1.0 );
    const float sector = static_cast<float>( hue * 6.0 );
    const int sectorIndex = static_cast<int>( std::floor( sector ) ) % 6;
    const float fraction = sector - std::floor( sector );
    constexpr float saturation = 0.72f;
    constexpr float value = 1.00f;
    constexpr float low = value * ( 1.0f - saturation );
    const float falling = value * ( 1.0f - saturation * fraction );
    const float rising = value * ( 1.0f - saturation * ( 1.0f - fraction ) );

    switch ( sectorIndex )
    {
    case 0:
        r = value;
        g = rising;
        b = low;
        break;
    case 1:
        r = falling;
        g = value;
        b = low;
        break;
    case 2:
        r = low;
        g = value;
        b = rising;
        break;
    case 3:
        r = low;
        g = falling;
        b = value;
        break;
    case 4:
        r = rising;
        g = low;
        b = value;
        break;
    default:
        r = value;
        g = low;
        b = falling;
        break;
    }
}

void ResolveReplayPathColor( ReplayPathColorMode mode, ReplayTrajectoryLane lane, Physics::PhysicsSceneObjectId bodyId,
                             int causalDepth, float pathT, float speed, float& r, float& g, float& b )
{
    // Invariant: this resolver consumes only values already available at draw
    // time. It cannot allocate, mutate captured trajectories, or affect replay
    // simulation/determinism.
    switch ( mode )
    {
    case ReplayPathColorMode::VelocityHeat:
        ReplayVelocityHeatColor( speed, r, g, b );
        break;
    case ReplayPathColorMode::TimeGradient:
        ReplayTimeGradientColor( pathT, r, g, b );
        break;
    case ReplayPathColorMode::PerObjectHue:
        ReplayHueColor( bodyId, r, g, b );
        break;
    case ReplayPathColorMode::CausalDepth:
        ReplayDepthPalette( causalDepth, r, g, b );
        break;
    case ReplayPathColorMode::LaneFlat:
    default:
        ReplayLaneFlatColor( lane, r, g, b );
        break;
    }
}

float ReplayTrajectorySegmentSpeed( const ReplayTrajectoryPoint& previous, const ReplayTrajectoryPoint& current )
{
    // Units: simulation distance per second. Stored path points deliberately do
    // not retain velocity, so color derives it from the fixed-timestep frame
    // delta without changing trajectory storage or allocating draw-time state.
    if ( current.frameIndex <= previous.frameIndex )
    {
        return 0.0f;
    }

    const float elapsedSeconds = static_cast<float>( current.frameIndex - previous.frameIndex ) * PHYSICS_FIXED_DT;
    return std::sqrt( VectorMagSquared( current.position - previous.position ) ) / elapsedSeconds;
}


uint64_t ReplayRetainedRangeIdentity( const ReplayTrajectoryRecordKey& key, bool retainedTrail,
                                      uint32_t chunkOrdinal ) noexcept
{
    const uint64_t body = static_cast<uint64_t>( key.bodyId.value );
    const uint64_t lane = static_cast<uint64_t>( key.lane ) << 32u;
    const uint64_t branch = static_cast<uint64_t>( key.branchOrdinal ) << 40u;
    const uint64_t presentation = retainedTrail ? ( uint64_t { 1 } << 63u ) : 0u;
    uint64_t identity = body | lane | branch | presentation;
    identity ^= static_cast<uint64_t>( chunkOrdinal + 1u ) + 0x9E3779B97F4A7C15ull + ( identity << 6u ) + ( identity >> 2u );
    return identity;
}


struct ReplayPredictionDrawFrameWindow
{
    ReplayFrameIndex lastFrame = 0;
    ReplayFrameIndex revealFrame = 0;
    std::size_t sampleStride = 1;
};

std::size_t ReplayTrajectoryPublishedPointCount( const ReplayTrajectoryRecord& record );
const ReplayTrajectoryRecord* ReplayTrajectoryRecordForDraw( std::span<const ReplayTrajectoryRecord> records,
                                                             Physics::PhysicsSceneObjectId id, ReplayTrajectoryLane lane,
                                                             uint16_t branchOrdinal );
const ColliderRecord* ReplayColliderRecordForModelIndex( const ColliderStore* colliderStore, int modelIndex );


ReplayPredictionDrawFrameWindow PublishedReplayPredictionDrawFrameWindow( const ReplayPredictionPresentationView& prediction,
                                                                          std::span<const RunReplayPredictionFrame> frames,
                                                                          std::size_t frameCount )
{
    ReplayPredictionDrawFrameWindow window;
    frameCount = (std::min)( frameCount, frames.size() );

    if ( frameCount == 0 )
    {
        return window;
    }

    window.lastFrame = frames[frameCount - 1].frameIndex;
    window.revealFrame = (std::min)( window.lastFrame, prediction.timeline.revealFrame );
    window.sampleStride = ReplayPredictionPathStrideForSampleCount( frameCount );
    return window;
}

void DrawReplayPredictionBaselineSnapshot( const ReplayPredictionPresentationView& prediction, ReplayPathColorMode colorMode,
                                           const ColliderStore& colliderStore, EditorTracer& tracer,
                                           ReplayRibbonDrawQuota& ribbonQuota )
{
    if ( !prediction.baseline.valid )
    {
        return;
    }

    if ( const ReplayTrajectoryRecord* record = ReplayTrajectoryRecordForDraw( prediction.trajectory.records,
                                                                               prediction.baseline.rootId,
                                                                               ReplayTrajectoryLane::BaselineRoot,
                                                                               REPLAY_TRAJECTORY_COMMITTED_BRANCH ) )
    {
        const std::size_t pointCount = ReplayTrajectoryPublishedPointCount( *record );

        if ( pointCount >= 2u )
        {
            const ReplayTrajectoryPoint* previous = nullptr;
            const ReplayFrameIndex firstFrame = record->points.front().frameIndex;
            const ReplayFrameIndex lastFrame = record->points[pointCount - 1u].frameIndex;

            for ( std::size_t i = 0; i < pointCount; ++i )
            {
                const ReplayTrajectoryPoint& point = record->points[i];

                if ( previous &&
                     VectorMagSquared( point.position - previous->position ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
                {
                    float r = 1.0f;
                    float g = 1.0f;
                    float b = 1.0f;
                    ResolveReplayPathColor( colorMode, ReplayTrajectoryLane::BaselineRoot, record->key.bodyId, record->depth,
                                            ReplayPathFrameT( point.frameIndex, firstFrame, lastFrame ),
                                            ReplayTrajectorySegmentSpeed( *previous, point ), r, g, b );

                    AddOrAccountReplayBaselinePathSegment( tracer, &ribbonQuota, previous->position, point.position, r, g,
                                                           b );
                }

                previous = &point;
            }
        }
    }

    for ( const ReplayPredictionBaselineBodyPose& pose : prediction.baseline.bodyPoses )
    {
        const ColliderRecord* collider = ReplayColliderRecordForModelIndex( &colliderStore, pose.modelRow.value );

        if ( !collider )
        {
            continue;
        }

        if ( pose.hasEntryPose )
        {
            tracer.AddReplayBaselineEntryMarker( pose.entryPosition, pose.entryOrientation, collider->shape );
        }

        if ( pose.hasRestPose )
        {
            tracer.AddReplayBaselineRestMarker( pose.restPosition, pose.restOrientation, collider->shape );
        }
    }
}

bool ShouldDrawReplayPathFrame( ReplayFrameIndex frameIndex, std::size_t stride )
{
    return stride <= 1 || ( frameIndex % static_cast<ReplayFrameIndex>( stride ) ) == 0;
}

std::size_t ReplayTrajectoryPublishedPointCount( const ReplayTrajectoryRecord& record )
{
    return (std::min)( record.publishedPointCount, record.points.size() );
}

constexpr std::size_t REPLAY_RETAINED_RANGE_CHUNK_SEGMENTS = 8u;

bool EnsureReplayRetainedRangeChunk( ReplayPredictionRetainedGeometry& drawList, ReplayPredictionDrawRecordCursor& cursor,
                                     const ReplayTrajectoryRecord& record, std::size_t canonicalRecordIndex,
                                     bool retainedTrail )
{
    if ( drawList.RangeCapacityRemaining( cursor.retainedRangeIndex ) > 0u )
    {
        return true;
    }

    const bool priority = retainedTrail;
    const std::size_t laneRemaining = priority ? drawList.PriorityCapacityRemaining() : drawList.OrdinaryCapacityRemaining();

    const std::size_t chunkCapacity = (std::min)( REPLAY_RETAINED_RANGE_CHUNK_SEGMENTS, laneRemaining );

    if ( chunkCapacity == 0u )
    {
        return false;
    }

    const uint32_t chunkOrdinal = cursor.retainedRangeChunkCount;
    const uint64_t drawOrder = ( priority ? ( uint64_t { 1 } << 63u ) : 0u ) |
                               ( static_cast<uint64_t>( canonicalRecordIndex ) << 32u ) |
                               static_cast<uint64_t>( chunkOrdinal );

    const std::size_t continuationRange = cursor.retainedRangeIndex;
    const std::size_t rangeIndex = drawList.BeginRange( ReplayRetainedRangeIdentity( record.key, retainedTrail,
                                                                                     chunkOrdinal ),
                                                        record.version, priority, chunkCapacity, drawOrder,
                                                        continuationRange );

    if ( rangeIndex >= PREDICTION_TRAJECTORY_RANGE_CAPACITY )
    {
        return false;
    }

    cursor.retainedRangeIndex = rangeIndex;
    ++cursor.retainedRangeChunkCount;
    return true;
}

const ReplayTrajectoryRecord* ReplayTrajectoryRecordForDraw( std::span<const ReplayTrajectoryRecord> records,
                                                             Physics::PhysicsSceneObjectId id, ReplayTrajectoryLane lane,
                                                             uint16_t branchOrdinal )
{
    if ( id.value == 0 )
    {
        return nullptr;
    }

    const ReplayTrajectoryRecordKey key = ReplayTrajectoryKey( id, lane, branchOrdinal );

    for ( const ReplayTrajectoryRecord& record : records )
    {
        if ( record.key == key )
        {
            return &record;
        }
    }

    return nullptr;
}

template <typename ColorForFrame>
void DrawReplayTrajectoryRecordSegments( const ReplayTrajectoryRecord& record, std::size_t pointCount,
                                         ReplayFrameIndex rangeStart, ReplayFrameIndex rangeEnd,
                                         ReplayFrameIndex forcedFrame, std::size_t sampleStride, EditorTracer& tracer,
                                         ReplayRibbonDrawQuota& ribbonQuota,
                                         SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane,
                                         ColorForFrame colorForFrame, float emphasis = 0.0f )
{
    pointCount = (std::min)( pointCount, record.points.size() );

    if ( pointCount < 2 || rangeEnd < rangeStart )
    {
        return;
    }

    const ReplayTrajectoryPoint* previous = nullptr;

    for ( std::size_t i = 0; i < pointCount; ++i )
    {

        const ReplayTrajectoryPoint& point = record.points[i];

        if ( point.frameIndex < rangeStart )
        {
            continue;
        }

        if ( point.frameIndex > rangeEnd )
        {
            break;
        }

        const bool endpointFrame = point.frameIndex == rangeStart || point.frameIndex == rangeEnd ||
                                   point.frameIndex == forcedFrame || i == 0u || i + 1u == pointCount;

        if ( !endpointFrame && !ShouldDrawReplayPathFrame( point.frameIndex, sampleStride ) )
        {
            continue;
        }

        if ( previous && VectorMagSquared( point.position - previous->position ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
        {
            float r = 1.0f;
            float g = 1.0f;
            float b = 1.0f;
            colorForFrame( *previous, point, r, g, b );
            AddOrAccountReplayPathSegment( tracer, &ribbonQuota, previous->position, point.position, r, g, b, lane,
                                           emphasis );
        }

        previous = &point;
    }
}

const ColliderRecord* ReplayColliderRecordForModelIndex( const ColliderStore* colliderStore, int modelIndex )
{
    if ( !colliderStore )
    {
        return nullptr;
    }

    // Why: retained prediction markers store historical model-index samples, not
    // live body handles. Use this only for presentation fallback; store-edit
    // paths resolve through PhysicsBodyHandle before reading collider rows.
    const PhysicsColliderHandle colliderHandle = colliderStore->HandleForModelIndex( modelIndex );
    const ColliderRecord* collider = colliderStore->RecordForHandle( colliderHandle );

    if ( !collider || colliderStore->ModelIndexForHandle( colliderHandle ) != modelIndex )
    {
        return nullptr;
    }

    return collider;
}

std::size_t ReplayRetainedMarkerTrailStrideForFrameCount( std::size_t frameCount )
{
    constexpr std::size_t retainedTrailMaxSegments = 96;

    if ( frameCount <= retainedTrailMaxSegments )
    {
        return 1;
    }

    return ( frameCount + retainedTrailMaxSegments - 1 ) / retainedTrailMaxSegments;
}

const ReplayTrajectoryRecord* FindReplayPredictionMarkerTrailRecord( const ReplayPredictionPresentationView& prediction,
                                                                     Physics::PhysicsSceneObjectId id,
                                                                     bool usingBuildFrames )
{
    const uint16_t branchBase = usingBuildFrames ? static_cast<uint16_t>( REPLAY_PATH_MAX_FUTURE_NODES ) : 0u;
    const uint16_t branchEnd = static_cast<uint16_t>( branchBase + static_cast<uint16_t>( REPLAY_PATH_MAX_FUTURE_NODES ) );

    for ( const ReplayTrajectoryRecord& record : prediction.trajectory.records )
    {
        if ( record.key.bodyId.value == id.value && record.key.lane == ReplayTrajectoryLane::FutureChildOutgoing &&
             record.key.branchOrdinal >= branchBase && record.key.branchOrdinal < branchEnd )
        {
            return &record;
        }
    }

    return nullptr;
}

void DrawReplayPredictionRetainedMarkerTrailFromStore( const ReplayPredictionPresentationView& prediction,
                                                       const ReplayPredictionRetainedMarker& marker,
                                                       ReplayPathColorMode colorMode, bool usingBuildFrames,
                                                       ReplayFrameIndex revealFrame, ReplayFrameIndex lastFrame,
                                                       EditorTracer& tracer )
{
    const ReplayTrajectoryRecord* record = FindReplayPredictionMarkerTrailRecord( prediction, marker.id, usingBuildFrames );

    if ( !record )
    {
        return;
    }

    const std::size_t pointCount = ReplayTrajectoryPublishedPointCount( *record );

    if ( pointCount < 2 )
    {
        return;
    }

    const std::size_t sampleStride = ReplayRetainedMarkerTrailStrideForFrameCount( pointCount );
    const ReplayTrajectoryPoint* previous = nullptr;

    for ( std::size_t i = 0; i < pointCount; ++i )
    {
        const ReplayTrajectoryPoint& point = record->points[i];

        if ( point.frameIndex > revealFrame )
        {
            break;
        }

        const bool endpointFrame = point.frameIndex == revealFrame || point.frameIndex == lastFrame || i + 1u == pointCount;

        if ( !endpointFrame && !ShouldDrawReplayPathFrame( point.frameIndex, sampleStride ) )
        {
            continue;
        }

        if ( previous && VectorMagSquared( point.position - previous->position ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
        {
            const float t = ReplayPathFrameT( point.frameIndex, 0, lastFrame );
            float r = 0.82f;
            float g = 0.82f;
            float b = 0.88f;
            ResolveReplayPathColor( colorMode, ReplayTrajectoryLane::RetainedTrail, marker.id, record->depth, t,
                                    ReplayTrajectorySegmentSpeed( *previous, point ), r, g, b );

            tracer.AddReplayCausalTrailSegment( previous->position, point.position, r, g, b );
        }

        previous = &point;
    }
}

void DrawReplayPredictionRetainedMarkers( const ReplayPredictionPresentationView& prediction, ReplayPathColorMode colorMode,
                                          bool usingBuildFrames, ReplayFrameIndex revealFrame, ReplayFrameIndex lastFrame,
                                          const ColliderStore& colliderStore, EditorTracer& tracer )
{
    // Invariant: marker emission is bounded by SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS and independent
    // of the visualizer budget. Lines may degrade under load; already-revealed
    // yellow/grey boxes must not.
    for ( std::size_t i = 0; i < prediction.markers.retainedMarkers.size(); ++i )
    {
        const ReplayPredictionRetainedMarker& marker = prediction.markers.retainedMarkers[i];
        const ColliderRecord* collider = ReplayColliderRecordForModelIndex( &colliderStore, marker.modelRow.value );

        if ( !collider )
        {
            continue;
        }

        DrawReplayPredictionRetainedMarkerTrailFromStore( prediction, marker, colorMode, usingBuildFrames, revealFrame,
                                                          lastFrame, tracer );

        if ( marker.hasEntryPose )
        {
            tracer.AddReplayCausalEntryMarker( marker.entryPosition, marker.entryOrientation, collider->shape );
        }

        if ( marker.hasRestPose )
        {
            tracer.AddReplayCausalRestMarker( marker.restPosition, marker.restOrientation, collider->shape );
        }
        else if ( marker.hasHorizonPose )
        {
            tracer.AddReplayCausalHorizonMarker( marker.horizonPosition, marker.horizonOrientation, collider->shape );
        }
    }
}

uint16_t ReplayPredictionDrawBranch( bool usingBuildFrames )
{
    return usingBuildFrames ? REPLAY_TRAJECTORY_BUILD_BRANCH : REPLAY_TRAJECTORY_COMMITTED_BRANCH;
}

void DrawReplayPredictionRootTrajectoryFromStore( const ReplayPredictionPresentationView& prediction,
                                                  Physics::PhysicsSceneObjectId rootId, ReplayPathColorMode colorMode,
                                                  const SceneEntityStore& entities, bool usingBuildFrames,
                                                  ReplayFrameIndex lastFrame, ReplayFrameIndex revealFrame,
                                                  std::size_t sampleStride, EditorTracer& tracer,
                                                  ReplayRibbonDrawQuota& ribbonQuota )
{
    const ReplayTrajectoryRecord* record = ReplayTrajectoryRecordForDraw( prediction.trajectory.records, rootId,
                                                                          ReplayTrajectoryLane::FutureRoot,
                                                                          ReplayPredictionDrawBranch( usingBuildFrames ) );

    if ( !record )
    {
        return;
    }

    const std::size_t pointCount = usingBuildFrames ? prediction.timeline.frames.size()
                                                    : ReplayTrajectoryPublishedPointCount( *record );

    DrawReplayTrajectoryRecordSegments(
        *record, pointCount, 0, revealFrame, revealFrame, sampleStride, tracer, ribbonQuota,
        SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureRoot,
        [&]( const ReplayTrajectoryPoint& previous, const ReplayTrajectoryPoint& point, float& r, float& g, float& b )
        {
            if ( !ReplayPredictionUsesAuthoredBodyColor( prediction.pathPresentation, ReplayTrajectoryLane::FutureRoot ) ||
                 !TryResolveReplayAuthoredPathColor( entities, rootId, r, g, b ) )
            {
                ResolveReplayPathColor( colorMode, ReplayTrajectoryLane::FutureRoot, rootId, record->depth,
                                        ReplayPathFrameT( point.frameIndex, 0, lastFrame ),
                                        ReplayTrajectorySegmentSpeed( previous, point ), r, g, b );
            }
        },
        1.0f );
}

void DrawReplayPredictionSmallSceneBodyTrajectories( std::span<const RunReplayPredictionFrame> frames,
                                                     std::size_t frameCount, Physics::PhysicsSceneObjectId selectedId,
                                                     ReplayPathColorMode colorMode, const SceneEntityStore& entities,
                                                     ReplayFrameIndex revealFrame, std::size_t requestedStride,
                                                     EditorTracer& tracer, ReplayRibbonDrawQuota& ribbonQuota )
{
    constexpr std::size_t MAX_ALL_BODY_PREDICTION_COUNT = 8u;
    frameCount = (std::min)( frameCount, frames.size() );

    if ( frameCount < 2u || frames[0].bodies.size() < 2u || frames[0].bodies.size() > MAX_ALL_BODY_PREDICTION_COUNT )
    {
        return;
    }

    const std::size_t auxiliaryBodyCount = frames[0].bodies.size() - 1u;
    const std::size_t logicalSegmentsRemaining = ribbonQuota.remainingRibbonSegments /
                                                 REPLAY_RIBBON_SEGMENTS_PER_PATH_SEGMENT;

    const std::size_t segmentsPerBody = (std::max)( std::size_t { 1 }, logicalSegmentsRemaining / auxiliaryBodyCount );

    // Why: all-body chaos paths share the existing fixed ribbon quota. Increase
    // sample stride as bodies/horizon grow instead of increasing runtime storage.
    const std::size_t quotaStride = ( frameCount + segmentsPerBody - 1u ) / segmentsPerBody;
    const std::size_t sampleStride = (std::max)( requestedStride, quotaStride );
    const ReplayFrameIndex lastFrame = frames[frameCount - 1u].frameIndex;

    for ( std::size_t bodyIndex = 0; bodyIndex < frames[0].bodies.size(); ++bodyIndex )
    {
        const RunReplayPredictionBodySample& seedBody = frames[0].bodies[bodyIndex];

        if ( seedBody.id.value == 0 || seedBody.id.value == selectedId.value )
        {
            continue;
        }

        bool hasPrevious = false;
        Vector3 previous = SkullbonezCore::Math::Vector::ZERO_VECTOR;

        for ( std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            const RunReplayPredictionFrame& frame = frames[frameIndex];

            if ( frame.frameIndex > revealFrame )
            {
                break;
            }

            const bool endpoint = frameIndex == 0u || frameIndex + 1u == frameCount || frame.frameIndex == revealFrame;

            if ( !endpoint && !ShouldDrawReplayPathFrame( frame.frameIndex, sampleStride ) )
            {
                if ( sampleStride > requestedStride && ShouldDrawReplayPathFrame( frame.frameIndex, requestedStride ) )
                {
                    // The adaptive quota deliberately merges this logical
                    // segment into a longer ribbon. Count the omission in the
                    // same lane the all-body preview would have emitted.
                    tracer.RecordReplayRibbonDroppedSegments(
                        SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureRoot );
                }

                continue;
            }

            const RunReplayPredictionBodySample* body = FindReplayPredictionBodyByIdWithHint( frame, seedBody.id,
                                                                                              seedBody.modelRow.value );

            if ( !body )
            {
                continue;
            }

            if ( hasPrevious && VectorMagSquared( body->position - previous ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
            {
                float r = 1.0f;
                float g = 1.0f;
                float b = 1.0f;

                if ( !TryResolveReplayAuthoredPathColor( entities, body->id, r, g, b ) )
                {
                    ResolveReplayPathColor( colorMode, ReplayTrajectoryLane::FutureRoot, body->id, 0,
                                            ReplayPathFrameT( frame.frameIndex, 0, lastFrame ),
                                            std::sqrt( VectorMagSquared( body->linearVelocity ) ), r, g, b );
                }

                AddOrAccountReplayPathSegment( tracer, &ribbonQuota, previous, body->position, r, g, b,
                                               SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureRoot );
            }

            previous = body->position;
            hasPrevious = true;
        }
    }
}

void DrawReplayPredictionChildTrajectoryRecord( const ReplayPredictionPresentationView& prediction,
                                                const RunReplayPathTraceNode& node, std::size_t nodeIndex,
                                                ReplayPathColorMode colorMode, bool usingBuildFrames,
                                                ReplayTrajectoryLane lane, ReplayFrameIndex revealFrame,
                                                ReplayFrameIndex lastFrame, std::size_t sampleStride, EditorTracer& tracer,
                                                ReplayRibbonDrawQuota& ribbonQuota )
{
    const ReplayTrajectoryRecord*
        record = ReplayTrajectoryRecordForDraw( prediction.trajectory.records, node.id, lane,
                                                ReplayPredictionChildTrajectoryBranch( nodeIndex, usingBuildFrames ) );

    if ( !record )
    {
        return;
    }

    if ( lane == ReplayTrajectoryLane::FutureChildIncoming )
    {
        const ReplayFrameIndex endFrame = (std::min)( revealFrame, node.firstFrame );
        DrawReplayTrajectoryRecordSegments( *record, ReplayTrajectoryPublishedPointCount( *record ), 0, endFrame, endFrame,
                                            sampleStride, tracer, ribbonQuota,
                                            SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureChildIncoming,
                                            [&]( const ReplayTrajectoryPoint& previous, const ReplayTrajectoryPoint& point,
                                                 float& r, float& g, float& b )
                                            {
                                                ResolveReplayPathColor( colorMode, ReplayTrajectoryLane::FutureChildIncoming,
                                                                        node.id, node.depth,
                                                                        ReplayPathFrameT( point.frameIndex, 0,
                                                                                          node.firstFrame ),
                                                                        ReplayTrajectorySegmentSpeed( previous, point ), r,
                                                                        g, b );
                                            } );

        return;
    }

    const std::size_t pointCount = ReplayTrajectoryPublishedPointCount( *record );

    if ( pointCount < 2 || revealFrame <= node.firstFrame )
    {
        return;
    }

    const ReplayTrajectoryPoint* previous = nullptr;

    for ( std::size_t i = 0; i < pointCount; ++i )
    {

        const ReplayTrajectoryPoint& point = record->points[i];

        if ( point.frameIndex < node.firstFrame )
        {
            previous = &point;
            continue;
        }

        if ( point.frameIndex > revealFrame )
        {
            break;
        }

        if ( point.frameIndex == node.firstFrame )
        {
            continue;
        }

        const bool endpointFrame = point.frameIndex == revealFrame || point.frameIndex == lastFrame || i + 1u == pointCount;

        if ( !endpointFrame && !ShouldDrawReplayPathFrame( point.frameIndex, sampleStride ) )
        {
            continue;
        }

        if ( previous && VectorMagSquared( point.position - previous->position ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
        {
            const float t = ReplayPathFrameT( point.frameIndex, node.firstFrame, lastFrame );
            float r = 0.5f;
            float g = 0.5f;
            float b = 0.56f;
            ResolveReplayPathColor( colorMode, ReplayTrajectoryLane::FutureChildOutgoing, node.id, node.depth, t,
                                    ReplayTrajectorySegmentSpeed( *previous, point ), r, g, b );

            AddOrAccountReplayPathSegment( tracer, &ribbonQuota, previous->position, point.position, r, g, b,
                                           SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureChildOutgoing );
        }

        previous = &point;
    }
}

void DrawReplayPredictionChildTrajectoriesFromStore( const ReplayPredictionPresentationView& prediction,
                                                     ReplayPathColorMode colorMode, bool usingBuildFrames,
                                                     ReplayFrameIndex revealFrame, ReplayFrameIndex lastFrame,
                                                     std::size_t sampleStride, EditorTracer& tracer,
                                                     ReplayRibbonDrawQuota& ribbonQuota )
{
    const std::size_t nodeCount = (std::min)( prediction.topology.futureNodes.size(), REPLAY_PATH_MAX_FUTURE_NODES );

    for ( std::size_t i = 0; i < nodeCount; ++i )
    {
        const RunReplayPathTraceNode& node = prediction.topology.futureNodes[i];
        DrawReplayPredictionChildTrajectoryRecord( prediction, node, i, colorMode, usingBuildFrames,
                                                   ReplayTrajectoryLane::FutureChildIncoming, revealFrame, lastFrame,
                                                   sampleStride, tracer, ribbonQuota );

        DrawReplayPredictionChildTrajectoryRecord( prediction, node, i, colorMode, usingBuildFrames,
                                                   ReplayTrajectoryLane::FutureChildOutgoing, revealFrame, lastFrame,
                                                   sampleStride, tracer, ribbonQuota );
    }
}

void DrawReplayPastRootTrajectoryFromStore( const ReplayPredictionPresentationView& prediction,
                                            Physics::PhysicsSceneObjectId rootId, ReplayPathColorMode colorMode,
                                            ReplayFrameIndex presentFrame, EditorTracer& tracer,
                                            ReplayRibbonDrawQuota& ribbonQuota )
{
    const ReplayTrajectoryRecord* record = ReplayTrajectoryRecordForDraw( prediction.trajectory.records, rootId,
                                                                          ReplayTrajectoryLane::PastRoot,
                                                                          REPLAY_TRAJECTORY_COMMITTED_BRANCH );

    if ( !record )
    {
        return;
    }

    const std::size_t pointCount = ReplayTrajectoryPublishedPointCount( *record );

    if ( pointCount < 2 )
    {
        return;
    }

    const ReplayFrameIndex firstFrame = record->points[0].frameIndex;
    const ReplayFrameIndex lastFrame = record->points[pointCount - 1u].frameIndex;
    const ReplayFrameIndex clampedPresent = std::clamp( presentFrame, firstFrame, lastFrame );
    const std::size_t sampleStride = ReplayPredictionPathStrideForSampleCount( pointCount );

    // Concept: a single PastRoot store record contains the retained solver
    // window. Draw-time presentFrame only recolors the already-published prefix
    // into "history" and "recorded future" halves; it never rebuilds samples.
    DrawReplayTrajectoryRecordSegments(
        *record, pointCount, firstFrame, clampedPresent, clampedPresent, sampleStride, tracer, ribbonQuota,
        SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::PastRoot,
        [&]( const ReplayTrajectoryPoint& previous, const ReplayTrajectoryPoint& point, float& r, float& g, float& b )
        {
            ResolveReplayPathColor( colorMode, ReplayTrajectoryLane::PastRoot, rootId, record->depth,
                                    ReplayPathFrameT( point.frameIndex, firstFrame, clampedPresent ),
                                    ReplayTrajectorySegmentSpeed( previous, point ), r, g, b );
        },
        1.0f );

    DrawReplayTrajectoryRecordSegments(
        *record, pointCount, clampedPresent, lastFrame, lastFrame, sampleStride, tracer, ribbonQuota,
        SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureRoot,
        [&]( const ReplayTrajectoryPoint& previous, const ReplayTrajectoryPoint& point, float& r, float& g, float& b )
        {
            ResolveReplayPathColor( colorMode, ReplayTrajectoryLane::FutureRoot, rootId, record->depth,
                                    ReplayPathFrameT( point.frameIndex, clampedPresent, lastFrame ),
                                    ReplayTrajectorySegmentSpeed( previous, point ), r, g, b );
        },
        1.0f );
}

void DrawReplayPredictionRagdollTorsoTrails( std::span<const RunReplayPredictionFrame> frames, std::size_t frameCount,
                                             ReplayPathColorMode colorMode, ReplayFrameIndex revealFrame,
                                             const SceneEntityStore& collection, EditorTracer& tracer,
                                             ReplayRibbonDrawQuota& ribbonQuota )
{
    const int modelCount = collection.Count();
    frameCount = (std::min)( frameCount, frames.size() );

    if ( frameCount < 2 || modelCount <= 0 )
    {
        return;
    }

    const ReplayFrameIndex lastFrame = frames[frameCount - 1].frameIndex;
    const std::size_t sampleStride = ReplayPredictionPathStrideForSampleCount( frameCount );

    for ( int modelIndex = 0; modelIndex < modelCount; ++modelIndex )
    {
        const SceneEntityRecord* entity = collection.TryGet( modelIndex );

        if ( !entity || entity->behaviorGroup.kind != SceneBehaviorGroupKind::SimpleRagdoll ||
             entity->behaviorGroup.partIndex != 0 )
        {
            continue;
        }

        bool hasPrevious = false;
        Vector3 previous = SkullbonezCore::Math::Vector::ZERO_VECTOR;

        for ( std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            const RunReplayPredictionFrame& frame = frames[frameIndex];

            if ( frame.frameIndex > revealFrame )
            {
                break;
            }

            // Why: the reveal-edge frame must always draw, or trail tips would
            // advance in visible stride-sized jumps instead of growing smoothly.
            if ( frame.frameIndex != lastFrame && frame.frameIndex != revealFrame &&
                 !ShouldDrawReplayPathFrame( frame.frameIndex, sampleStride ) )
            {
                continue;
            }

            const RunReplayPredictionBodySample* body = FindReplayPredictionBodyByModelIndex( frame, modelIndex );

            if ( !body )
            {
                continue;
            }

            if ( hasPrevious && VectorMagSquared( body->position - previous ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
            {
                const float t = ReplayPathFrameT( frame.frameIndex, 0, lastFrame );
                float r = 1.0f;
                float g = 1.0f;
                float b = 1.0f;
                ResolveReplayPathColor( colorMode, ReplayTrajectoryLane::FutureChildOutgoing, body->id, 1, t,
                                        std::sqrt( VectorMagSquared( body->linearVelocity ) ), r, g, b );

                AddOrAccountReplayPathSegment( tracer, &ribbonQuota, previous, body->position, r, g, b,
                                               SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::AuxiliaryTrail );
            }

            previous = body->position;
            hasPrevious = true;
        }
    }
}

void DrawReplayPredictionAffectedBodyTrails( std::span<const RunReplayPredictionFrame> frames, std::size_t frameCount,
                                             ReplayPathColorMode colorMode, ReplayFrameIndex revealFrame,
                                             Physics::PhysicsSceneObjectId rootId, int rootModelIndex,
                                             std::span<const RunReplayPathTraceNode> futureNodes,
                                             const SceneEntityStore& collection, EditorTracer& tracer,
                                             ReplayRibbonDrawQuota& ribbonQuota )
{
    std::array<ReplayPredictionAffectedBodyTrail, REPLAY_PATH_MAX_FUTURE_NODES> trails = {};
    std::array<ReplayPredictionSceneEntityFact, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> sceneFacts = {};
    const ReplayPredictionSceneView scene = BuildReplayPredictionSceneView( collection, sceneFacts );
    const std::size_t trailCount = BuildReplayPredictionAffectedBodyTrails( frames, frameCount, revealFrame, rootId,
                                                                            rootModelIndex, futureNodes, scene, trails );

    frameCount = (std::min)( frameCount, frames.size() );

    if ( trailCount == 0 )
    {
        return;
    }

    const ReplayFrameIndex lastFrame = frames[frameCount - 1].frameIndex;
    const std::size_t sampleStride = ReplayPredictionPathStrideForSampleCount( frameCount );

    for ( std::size_t trailIndex = 0; trailIndex < trailCount; ++trailIndex )
    {
        ReplayPredictionAffectedBodyTrail& trail = trails[trailIndex];

        for ( std::size_t frameSlot = trail.firstFrameSlot + 1; frameSlot < frameCount; ++frameSlot )
        {
            if ( frames[frameSlot].frameIndex > revealFrame )
            {
                break;
            }

            const RunReplayPredictionFrame& frame = frames[frameSlot];

            if ( frame.frameIndex != lastFrame && frame.frameIndex != revealFrame &&
                 !ShouldDrawReplayPathFrame( frame.frameIndex, sampleStride ) )
            {
                continue;
            }

            const RunReplayPredictionBodySample* body = FindReplayPredictionBodyByIdWithHint( frame, trail.id,
                                                                                              trail.modelRow.value );

            if ( !body )
            {
                continue;
            }

            if ( VectorMagSquared( body->position - trail.previous ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
            {
                const float t = ReplayPathFrameT( frame.frameIndex, trail.firstFrame, lastFrame );
                float r = 1.0f;
                float g = 0.65f;
                float b = 0.18f;
                ResolveReplayPathColor( colorMode, ReplayTrajectoryLane::FutureChildOutgoing, trail.id, trail.causalDepth, t,
                                        std::sqrt( VectorMagSquared( body->linearVelocity ) ), r, g, b );

                AddOrAccountReplayPathSegment( tracer, &ribbonQuota, trail.previous, body->position, r, g, b,
                                               SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::AuxiliaryTrail );
            }

            if ( ReplayPredictionBodyHasVisibleLinearMotion( *body ) )
            {
                trail.lastMotionFrame = frame.frameIndex;
            }

            trail.previous = body->position;
            trail.modelRow.value = body->modelRow.value;
        }
    }
}

bool DrawReplayPredictionOverlay( const RunReplayPathVisualizerState& pathVisualizer,
                                  const ReplayPredictionPresentationView& prediction, SkullbonezCore::Core::Profiler*,
                                  const SceneEntityStore& modelCollection, const ColliderStore& colliderStore,
                                  EditorTracer& tracer, ReplayRibbonDrawQuota& ribbonQuota )
{
    const bool usingBuildFrames = prediction.timeline.usingBuildFrames;
    const std::span<const RunReplayPredictionFrame> activePredictionFrames = prediction.timeline.frames;
    const std::size_t activePredictionFrameCount = activePredictionFrames.size();

    if ( activePredictionFrameCount < 2 )
    {
        return false;
    }

    // Concept: every pass below draws only frames at or before the reveal
    // cursor. That single clamp is what turns a finished prediction buffer into
    // an unfolding animation: the root line grows first, and each child starts
    // drawing when the cursor passes the frame where its cause happened.
    const ReplayPredictionDrawFrameWindow
        drawWindow = PublishedReplayPredictionDrawFrameWindow( prediction, activePredictionFrames,
                                                               activePredictionFrameCount );

    DrawReplayPredictionBaselineSnapshot( prediction, pathVisualizer.colorMode, colliderStore, tracer, ribbonQuota );

    if ( !pathVisualizer.hasTarget || pathVisualizer.targetId.value == 0 )
    {
        if ( prediction.topology.ragdollVisualsEnabled )
        {
            DrawReplayPredictionRagdollTorsoTrails( activePredictionFrames, activePredictionFrameCount,
                                                    pathVisualizer.colorMode, drawWindow.revealFrame, modelCollection,
                                                    tracer, ribbonQuota );
        }

        return true;
    }

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/DrawRoot" );
        DrawReplayPredictionRootTrajectoryFromStore( prediction, pathVisualizer.targetId, pathVisualizer.colorMode,
                                                     modelCollection, usingBuildFrames, drawWindow.lastFrame,
                                                     drawWindow.revealFrame, drawWindow.sampleStride, tracer, ribbonQuota );

        DrawReplayPredictionSmallSceneBodyTrajectories( activePredictionFrames, activePredictionFrameCount,
                                                        pathVisualizer.targetId, pathVisualizer.colorMode, modelCollection,
                                                        drawWindow.revealFrame, drawWindow.sampleStride, tracer,
                                                        ribbonQuota );
    }

    // Invariant: publication proves the future tree internally coherent, while
    // this submission seam still rejects a stale visualizer selection for a
    // different stable root id.
    const bool drawFutureTree = !ReplayPredictionPathPresentationShowsAllBodies( prediction.pathPresentation ) &&
                                prediction.topology.treeReady &&
                                prediction.topology.targetId.value == pathVisualizer.targetId.value;

    if ( drawFutureTree )
    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/DrawChildren" );
        DrawReplayPredictionChildTrajectoriesFromStore( prediction, pathVisualizer.colorMode, usingBuildFrames,
                                                        drawWindow.revealFrame, drawWindow.lastFrame,
                                                        drawWindow.sampleStride, tracer, ribbonQuota );
    }

    if ( !ReplayPredictionPathPresentationShowsAllBodies( prediction.pathPresentation ) )
    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/DrawAffectedBodies" );
        DrawReplayPredictionAffectedBodyTrails( activePredictionFrames, activePredictionFrameCount, pathVisualizer.colorMode,
                                                drawWindow.revealFrame, pathVisualizer.targetId,
                                                pathVisualizer.targetModelRow.value, prediction.topology.futureNodes,
                                                modelCollection, tracer, ribbonQuota );
    }

    if ( prediction.topology.ragdollVisualsEnabled )
    {
        DrawReplayPredictionRagdollTorsoTrails( activePredictionFrames, activePredictionFrameCount, pathVisualizer.colorMode,
                                                drawWindow.revealFrame, modelCollection, tracer, ribbonQuota );
    }

    if ( !ReplayPredictionPathPresentationShowsAllBodies( prediction.pathPresentation ) )
    {
        DrawReplayPredictionRetainedMarkers( prediction, pathVisualizer.colorMode, usingBuildFrames, drawWindow.revealFrame,
                                             drawWindow.lastFrame, colliderStore, tracer );
    }

    return true;
}

void DrawReplayPredictionVisualizer( const RunReplayPathVisualizerState& pathVisualizer,
                                     const ReplayPredictionPresentationView& prediction,
                                     SkullbonezCore::Core::Profiler* profiler, PhysicsEngine& physicsEngine,
                                     const SceneEntityStore& entities, EditorTracer& tracer,
                                     ReplayRibbonDrawQuota& ribbonQuota )
{
    PROFILE_SCOPED( "Frame/Replay/PathVisualizer/Prediction" );
    const ColliderStore& colliderStore = PhysicsEngine::ReadColliders( physicsEngine );
    DrawReplayPredictionOverlay( pathVisualizer, prediction, profiler, entities, colliderStore, tracer, ribbonQuota );
}

} // namespace

namespace SkullbonezCore::Runtime::ReplayOverlay
{
ReplayPredictionRetainedGeometry::ReplayPredictionRetainedGeometry()
    : m_records( std::make_unique<float[]>( PREDICTION_TRAJECTORY_RECORD_FLOAT_CAPACITY ) )
{
}


bool ReplayPredictionRetainedGeometry::SetAppearance( const Core::ReplayTrajectoryAppearanceConfig& appearance )
{
    const auto boundedStyle = []( float width, float alpha, float edgeFeather )
    {
        return RibbonStyle { std::clamp( width, 1.0f, 6.0f ), std::clamp( alpha, 0.05f, 1.0f ),
                             std::clamp( edgeFeather, 0.25f, 1.25f ), 0.0f };
    };

    const RibbonStyle path = boundedStyle( appearance.futureWidth, appearance.futureAlpha, appearance.futureEdgeFeather );

    const RibbonStyle causal = boundedStyle( appearance.causalWidth, appearance.causalAlpha, appearance.causalEdgeFeather );

    const RibbonStyle baseline = boundedStyle( appearance.baselineWidth, appearance.baselineAlpha,
                                               appearance.baselineEdgeFeather );

    const float selectedEmphasis = std::clamp( appearance.selectedEmphasis, 0.0f, 1.0f );
    const auto sameStyle = []( const RibbonStyle& lhs, const RibbonStyle& rhs )
    {
        return lhs.width == rhs.width && lhs.alpha == rhs.alpha && lhs.edgeFeather == rhs.edgeFeather &&
               lhs.emphasis == rhs.emphasis;
    };

    if ( m_appearanceInitialized && sameStyle( path, m_pathStyle ) && sameStyle( causal, m_causalStyle ) &&
         sameStyle( baseline, m_baselineStyle ) && selectedEmphasis == m_selectedEmphasis )
    {
        return false;
    }

    m_pathStyle = path;
    m_causalStyle = causal;
    m_baselineStyle = baseline;
    m_selectedEmphasis = selectedEmphasis;
    m_appearanceInitialized = true;
    return true;
}


void ReplayPredictionRetainedGeometry::Clear() noexcept
{
    m_rangeCount = 0;
    m_ordinaryRecordCapacityUsed = 0;
    m_priorityRecordCapacityUsed = 0;
    m_ordinaryRecordCount = 0;
    m_priorityRecordCount = 0;
    m_stats = {};
}


void ReplayPredictionRetainedGeometry::PublishToPacket( ReplayVisualPacket& packet )
{
    std::copy_n( m_ranges.begin(), m_rangeCount, m_drawRanges.begin() );
    std::sort( m_drawRanges.begin(), m_drawRanges.begin() + m_rangeCount,
               []( const Rendering::RetainedGeometryRangeToken& lhs, const Rendering::RetainedGeometryRangeToken& rhs )
               { return lhs.drawOrder < rhs.drawOrder; } );

    packet.retainedPredictionCompactRibbonRecords = std::span<const float>( m_records.get(),
                                                                            PREDICTION_TRAJECTORY_RECORD_FLOAT_CAPACITY );

    packet.retainedPredictionRibbonRanges = std::span<const Rendering::RetainedGeometryRangeToken>( m_drawRanges.data(),
                                                                                                    m_rangeCount );
}


std::size_t ReplayPredictionRetainedGeometry::BeginRange( uint64_t identity, uint32_t sourceVersion, bool priority,
                                                          std::size_t recordCapacity, uint64_t drawOrder,
                                                          std::size_t continuationRange )
{
    if ( recordCapacity == 0u || m_rangeCount >= PREDICTION_TRAJECTORY_RANGE_CAPACITY )
    {
        return ( std::numeric_limits<std::size_t>::max )();
    }

    const std::size_t laneCapacity = priority ? PREDICTION_TRAJECTORY_PRIORITY_RECORD_CAPACITY
                                              : PREDICTION_TRAJECTORY_ORDINARY_RECORD_CAPACITY;

    std::size_t& laneUsed = priority ? m_priorityRecordCapacityUsed : m_ordinaryRecordCapacityUsed;

    if ( recordCapacity > laneCapacity - laneUsed )
    {
        return ( std::numeric_limits<std::size_t>::max )();
    }

    const std::size_t rangeIndex = m_rangeCount++;
    Rendering::RetainedGeometryRangeToken& range = m_ranges[rangeIndex];
    range = {};
    range.identity = identity;
    range.drawOrder = drawOrder;
    range.sourceVersion = sourceVersion;
    range.cacheSlot = static_cast<uint32_t>( rangeIndex );
    range.continuationRange = static_cast<uint32_t>( continuationRange );
    range.firstRecord = static_cast<uint32_t>( laneUsed +
                                               ( priority ? PREDICTION_TRAJECTORY_ORDINARY_RECORD_CAPACITY : 0u ) );

    range.recordCapacity = static_cast<uint32_t>( recordCapacity );
    range.lane = priority ? Rendering::RetainedGeometryLane::Priority : Rendering::RetainedGeometryLane::Ordinary;
    laneUsed += recordCapacity;
    return rangeIndex;
}


std::size_t ReplayPredictionRetainedGeometry::RangeCapacityRemaining( std::size_t rangeIndex ) const noexcept
{
    if ( rangeIndex >= m_rangeCount )
    {
        return 0u;
    }

    const Rendering::RetainedGeometryRangeToken& range = m_ranges[rangeIndex];
    return range.recordCapacity - range.recordCount;
}


std::size_t ReplayPredictionRetainedGeometry::OrdinaryCapacityRemaining() const noexcept
{
    return PREDICTION_TRAJECTORY_ORDINARY_RECORD_CAPACITY - m_ordinaryRecordCapacityUsed;
}


std::size_t ReplayPredictionRetainedGeometry::PriorityCapacityRemaining() const noexcept
{
    return PREDICTION_TRAJECTORY_PRIORITY_RECORD_CAPACITY - m_priorityRecordCapacityUsed;
}


std::size_t ReplayPredictionRetainedGeometry::OrdinaryCountRemaining() const noexcept
{
    return PREDICTION_TRAJECTORY_ORDINARY_RECORD_CAPACITY - m_ordinaryRecordCount;
}


std::size_t ReplayPredictionRetainedGeometry::PriorityCountRemaining() const noexcept
{
    return PREDICTION_TRAJECTORY_PRIORITY_RECORD_CAPACITY - m_priorityRecordCount;
}


void ReplayPredictionRetainedGeometry::RecordDropped( SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane )
{
    const std::size_t laneIndex = static_cast<std::size_t>( lane );

    if ( laneIndex < SkullbonezCore::Core::MAIN_MEMORY_REPLAY_TRAJECTORY_LANE_COUNT )
    {
        ++m_stats.droppedSegments[laneIndex];
    }
}


bool ReplayPredictionRetainedGeometry::EmitRecord( std::size_t rangeIndex, const Vector3& start, const Vector3& end, float r,
                                                   float g, float b, const RibbonStyle& style,
                                                   SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane )
{
    if ( rangeIndex >= m_rangeCount )
    {
        RecordDropped( lane );
        return false;
    }

    Rendering::RetainedGeometryRangeToken& range = m_ranges[rangeIndex];

    if ( range.recordCount >= range.recordCapacity )
    {
        RecordDropped( lane );
        return false;
    }

    const std::size_t laneIndex = static_cast<std::size_t>( lane );

    if ( laneIndex < SkullbonezCore::Core::MAIN_MEMORY_REPLAY_TRAJECTORY_LANE_COUNT )
    {
        ++m_stats.emittedSegments[laneIndex];
    }

    const ReplayPredictionRetainedRecord record =
        { start, end, style.width, r, g, b, style.alpha, style.edgeFeather, style.emphasis, start, end };
    std::span<float> records( m_records.get(), PREDICTION_TRAJECTORY_RECORD_FLOAT_CAPACITY );

    const bool appended = range.recordCount == 0u && range.continuationRange < m_rangeCount
                              ? AppendPredictionRetainedContinuation( records, m_ranges[range.continuationRange], range,
                                                                      record, TOLERANCE * TOLERANCE )
                              : AppendPredictionRetainedRecord( records, range, record, TOLERANCE * TOLERANCE );

    if ( !appended )
    {
        RecordDropped( lane );
        return false;
    }

    if ( range.lane == Rendering::RetainedGeometryLane::Priority )
    {
        ++m_priorityRecordCount;
    }
    else
    {
        ++m_ordinaryRecordCount;
    }

    ++m_revision;
    return true;
}


void ReplayPredictionRetainedGeometry::AddPathSegment( std::size_t rangeIndex, const Vector3& start, const Vector3& end,
                                                       float r, float g, float b,
                                                       SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane,
                                                       float emphasis )
{
    RibbonStyle style = m_pathStyle;
    style.emphasis = std::clamp( emphasis, 0.0f, 1.0f ) * m_selectedEmphasis;
    (void)EmitRecord( rangeIndex, start, end, r, g, b, style, lane );
}


void ReplayPredictionRetainedGeometry::AddCausalTrailSegment( std::size_t rangeIndex, const Vector3& start,
                                                              const Vector3& end, float r, float g, float b )
{
    (void)EmitRecord( rangeIndex, start, end, r, g, b, m_causalStyle,
                      SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::RetainedTrail );
}


void ReplayPredictionRetainedGeometry::AddBaselinePathSegment( std::size_t rangeIndex, const Vector3& start,
                                                               const Vector3& end, float r, float g, float b, float opacity )
{
    RibbonStyle style = m_baselineStyle;
    style.alpha *= std::clamp( opacity, 0.0f, 1.0f );
    (void)EmitRecord( rangeIndex, start, end, r, g, b, style,
                      SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::BaselineRoot );
}

void AppendReplayVelocityDragPreview( const ReplayPredictionPresentationView& prediction,
                                      const RunReplayPathVisualizerState& pathVisualizer,
                                      const ReplayPredictionDrawListState& state, EditorTracer& tracer )
{
    const ReplayVelocityDragPreviewView& preview = prediction.dragPreview;

    if ( !preview.active || preview.targetId.value == 0 || preview.targetId.value != pathVisualizer.targetId.value )
    {
        return;
    }

    const ReplayTrajectoryRecord* record = ReplayTrajectoryRecordForDraw( prediction.trajectory.records, preview.targetId,
                                                                          ReplayTrajectoryLane::FutureRoot,
                                                                          REPLAY_TRAJECTORY_COMMITTED_BRANCH );

    if ( !record )
    {
        return;
    }

    const std::size_t publishedCount = ReplayTrajectoryPublishedPointCount( *record );

    if ( publishedCount < 2u )
    {
        return;
    }

    // Concept: this is a first-order visual estimate, not another simulation.
    // It bends the selected committed polyline by delta-v times elapsed time,
    // performs no allocation, and leaves every other retained lane untouched.
    const ReplayFrameIndex lastFrame = record->points[publishedCount - 1u].frameIndex;
    const std::size_t sampleStride = (std::max)( state.sampleStride, std::size_t { 1 } );
    std::size_t segmentBudget = tracer.ReplayPathRibbonSegmentCapacityRemaining();
    Vector3 previousPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    const ReplayTrajectoryPoint* previousPoint = nullptr;

    std::size_t pointIndex = 0;

    while ( pointIndex < publishedCount )
    {
        const ReplayTrajectoryPoint& point = record->points[pointIndex];
        const bool finalVisiblePoint = pointIndex + 1u == publishedCount;

        const float elapsedSeconds = point.frameIndex > record->firstFrame
                                         ? static_cast<float>( point.frameIndex - record->firstFrame ) * PHYSICS_FIXED_DT
                                         : 0.0f;

        const Vector3 previewPosition = point.position + preview.velocityDelta * elapsedSeconds;

        if ( previousPoint && VectorMagSquared( previewPosition - previousPosition ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
        {
            if ( segmentBudget == 0u )
            {
                return;
            }

            float r = 1.0f;
            float g = 1.0f;
            float b = 1.0f;
            ResolveReplayPathColor( pathVisualizer.colorMode, ReplayTrajectoryLane::FutureRoot, record->key.bodyId,
                                    record->depth, ReplayPathFrameT( point.frameIndex, record->firstFrame, lastFrame ),
                                    ReplayTrajectorySegmentSpeed( *previousPoint, point ), r, g, b );

            tracer.AddReplayPathSegment( previousPosition, previewPosition, r, g, b,
                                         SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureRoot, 1.0f );

            --segmentBudget;
        }

        previousPoint = &point;
        previousPosition = previewPosition;

        if ( finalVisiblePoint )
        {
            break;
        }

        pointIndex = (std::min)( pointIndex + sampleStride, publishedCount - 1u );
    }
}


void AppendReplayPredictionRetainedEvidence( const ReplayPredictionPresentationView& prediction,
                                             const RunReplayPathVisualizerState& pathVisualizer,
                                             const ColliderStore& colliderStore,
                                             ReplayPredictionRetainedGeometry& retainedGeometry,
                                             EditorTracer& retainedMarkers, ReplayPredictionDrawListState& state,
                                             bool reset )
{
    const uint16_t activeChildBranchBase = prediction.timeline.usingBuildFrames
                                               ? static_cast<uint16_t>( REPLAY_PATH_MAX_FUTURE_NODES )
                                               : 0u;
    const uint16_t activeChildBranchEnd = static_cast<uint16_t>( activeChildBranchBase +
                                                                 static_cast<uint16_t>( REPLAY_PATH_MAX_FUTURE_NODES ) );

    // Concept: a retained marker trail is a second command stream over the
    // same outgoing child record. It deliberately uses a denser 96-segment
    // policy and priority storage, so consuming the ordinary child cursor would
    // silently delete causal evidence as the reveal advances.
    const std::size_t previousRetainedTrailCursorCount = reset ? 0u : state.retainedTrailCursorCount;
    state.retainedTrailCursorCount = (std::min)( prediction.markers.retainedMarkers.size(),
                                                 ReplayPredictionDrawListState::MAX_RECORD_CURSORS );

    if ( reset || previousRetainedTrailCursorCount < state.retainedTrailCursorCount )
    {
        for ( std::size_t markerIndex = previousRetainedTrailCursorCount; markerIndex < state.retainedTrailCursorCount;
              ++markerIndex )
        {
            const ReplayPredictionRetainedMarker& marker = prediction.markers.retainedMarkers[markerIndex];
            ReplayPredictionDrawRecordCursor& cursor = state.retainedTrailCursors[markerIndex];

            for ( std::size_t recordIndex = 0; recordIndex < prediction.trajectory.records.size(); ++recordIndex )
            {
                const ReplayTrajectoryRecord& record = prediction.trajectory.records[recordIndex];

                if ( record.key.bodyId.value == marker.id.value &&
                     record.key.lane == ReplayTrajectoryLane::FutureChildOutgoing &&
                     record.key.branchOrdinal >= activeChildBranchBase && record.key.branchOrdinal < activeChildBranchEnd )
                {
                    cursor.key = record.key;
                    cursor.recordVersion = record.version;
                    cursor.sourceRecordIndex = recordIndex;
                    break;
                }
            }
        }
    }

    for ( std::size_t markerIndex = 0; markerIndex < state.retainedTrailCursorCount; ++markerIndex )
    {
        ReplayPredictionDrawRecordCursor& cursor = state.retainedTrailCursors[markerIndex];

        if ( cursor.recordVersion == 0 || cursor.sourceRecordIndex >= prediction.trajectory.records.size() )
        {
            continue;
        }

        const ReplayTrajectoryRecord& record = prediction.trajectory.records[cursor.sourceRecordIndex];

        if ( record.key != cursor.key || record.version != cursor.recordVersion )
        {
            // Record replacement is already a reset condition for the primary
            // cursor bank. Keep this defensive seam from reading a stale index.
            continue;
        }

        const std::size_t publishedCount = ReplayTrajectoryPublishedPointCount( record );
        const std::size_t markerStride = ReplayRetainedMarkerTrailStrideForFrameCount( publishedCount );
        std::size_t pointIndex = ReplayPredictionFirstUnconsumedPoint( cursor.consumedPointCount );

        for ( ; pointIndex < publishedCount; ++pointIndex )
        {
            const ReplayTrajectoryPoint& point = record.points[pointIndex];

            if ( point.frameIndex > prediction.timeline.revealFrame )
            {
                break;
            }

            const bool finalPoint = pointIndex + 1u == publishedCount && prediction.timeline.complete;

            if ( !finalPoint && !ShouldDrawReplayPathFrame( point.frameIndex, markerStride ) )
            {
                continue;
            }

            const ReplayTrajectoryPoint& previous = record.points[cursor.lastSelectedPointIndex];

            if ( VectorMagSquared( point.position - previous.position ) <= REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
            {
                cursor.lastSelectedPointIndex = pointIndex;
                continue;
            }

            float r = 0.82f;
            float g = 0.82f;
            float b = 0.88f;
            const ReplayFrameIndex lastFrame = prediction.timeline.frames.empty()
                                                   ? point.frameIndex
                                                   : prediction.timeline.frames.back().frameIndex;

            ResolveReplayPathColor( pathVisualizer.colorMode, ReplayTrajectoryLane::RetainedTrail, record.key.bodyId,
                                    record.depth, ReplayPathFrameT( point.frameIndex, 0, lastFrame ),
                                    ReplayTrajectorySegmentSpeed( previous, point ), r, g, b );

            if ( EnsureReplayRetainedRangeChunk( retainedGeometry, cursor, record, markerIndex, true ) )
            {
                retainedGeometry.AddCausalTrailSegment( cursor.retainedRangeIndex, previous.position, point.position, r, g,
                                                        b );
            }
            else
            {
                retainedGeometry.RecordDroppedSegment( SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::RetainedTrail );
            }

            cursor.lastSelectedPointIndex = pointIndex;
        }

        cursor.consumedPointCount = pointIndex;
    }

    if ( reset )
    {
        // Marker topology is immutable for one draw-list generation, so these
        // shape commands are appended once and never revisited on stable frames.
        for ( const ReplayPredictionBaselineBodyPose& pose : prediction.baseline.bodyPoses )
        {
            const ColliderRecord* collider = ReplayColliderRecordForModelIndex( &colliderStore, pose.modelRow.value );

            if ( !collider )
            {
                continue;
            }

            if ( pose.hasEntryPose )
            {
                retainedMarkers.AddReplayBaselineEntryMarker( pose.entryPosition, pose.entryOrientation, collider->shape );
            }

            if ( pose.hasRestPose )
            {
                retainedMarkers.AddReplayBaselineRestMarker( pose.restPosition, pose.restOrientation, collider->shape );
            }
        }

        state.baselinePoseCount = prediction.baseline.bodyPoses.size();
    }

    const bool finalReveal = prediction.timeline.complete && !prediction.timeline.frames.empty() &&
                             prediction.timeline.revealFrame >= prediction.timeline.frames.back().frameIndex;

    for ( std::size_t markerIndex = 0; markerIndex < state.retainedTrailCursorCount; ++markerIndex )
    {
        const ReplayPredictionRetainedMarker& marker = prediction.markers.retainedMarkers[markerIndex];
        ReplayPredictionDrawRecordCursor& cursor = state.retainedTrailCursors[markerIndex];
        const ColliderRecord* collider = ReplayColliderRecordForModelIndex( &colliderStore, marker.modelRow.value );

        if ( !collider )
        {
            continue;
        }

        if ( marker.hasEntryPose && !cursor.entryMarkerAppended )
        {
            retainedMarkers.AddReplayCausalEntryMarker( marker.entryPosition, marker.entryOrientation, collider->shape );

            cursor.entryMarkerAppended = true;
        }

        if ( marker.hasRestPose && !cursor.endMarkerAppended )
        {
            retainedMarkers.AddReplayCausalRestMarker( marker.restPosition, marker.restOrientation, collider->shape );
            cursor.endMarkerAppended = true;
        }
        else if ( finalReveal && marker.hasHorizonPose && !cursor.endMarkerAppended )
        {
            retainedMarkers.AddReplayCausalHorizonMarker( marker.horizonPosition, marker.horizonOrientation,
                                                          collider->shape );

            cursor.endMarkerAppended = true;
        }
    }
}

ReplayPredictionDrawListUpdate
UpdateReplayPredictionDrawList( const ReplayPredictionPresentationView& prediction,
                                const RunReplayPathVisualizerState& pathVisualizer, const SceneEntityStore& entities,
                                const ColliderStore& colliderStore, ReplayPredictionRetainedGeometry& retainedGeometry,
                                EditorTracer& retainedMarkers, ReplayPredictionDrawListState& state )
{
    ReplayPredictionDrawListUpdate update;
    const uint64_t geometryRevisionBefore = retainedGeometry.Revision();
    const uint64_t markerRevisionBefore = retainedMarkers.ReplayGeometryRevision();
    const bool hasPrediction = prediction.controls.enabled && prediction.timeline.frames.size() >= 2u &&
                               !prediction.trajectory.records.empty();

    if ( !hasPrediction )
    {
        if ( state.valid )
        {
            retainedGeometry.Clear();
            retainedMarkers.Clear();
            state.Reset();
            update.reset = true;
        }

        update.stable = true;
        return update;
    }

    // Invariant: publication growth cannot change path density. The full
    // horizon fixes the stride before the first retained chunk is emitted, so
    // later worker prefixes append instead of invalidating earlier geometry.
    const std::size_t horizonFrameCapacity = static_cast<std::size_t>(
                                                 std::ceil( prediction.controls.horizonSeconds /
                                                            static_cast<double>( PHYSICS_FIXED_DT ) ) ) +
                                             1u;

    const std::size_t sampleStride = ReplayPredictionPathStrideForSampleCount(
        (std::max)( prediction.timeline.frames.size(), horizonFrameCapacity ) );

    bool reset = !state.valid || state.generation != prediction.timeline.generation ||
                 state.targetId.value != pathVisualizer.targetId.value || state.colorMode != pathVisualizer.colorMode ||
                 state.velocityPreviewActive != prediction.dragPreview.active ||
                 state.velocityPreviewTargetId.value != prediction.dragPreview.targetId.value ||
                 state.usingBuildFrames != prediction.timeline.usingBuildFrames ||
                 state.pathPresentation != prediction.pathPresentation ||
                 state.recordCursorCount > prediction.trajectory.records.size() ||
                 state.retainedMarkerCount > prediction.markers.retainedMarkers.size() ||
                 state.baselinePoseCount != prediction.baseline.bodyPoses.size() || state.sampleStride != sampleStride;

    const bool publicationUnchanged = IsReplayPredictionDrawListPublicationStable( reset, state.trajectoryPublicationVersion,
                                                                                   state.revealFrame,
                                                                                   prediction.trajectory.publicationVersion,
                                                                                   prediction.timeline.revealFrame );

    if ( publicationUnchanged )
    {
        update.stable = true;
        return update;
    }

    if ( !reset && state.saturated )
    {
        // The bounded list already owns its complete drawable prefix. Later
        // publication cannot add a command, so advance tokens without scanning
        // the 800+ source records.
        state.revealFrame = prediction.timeline.revealFrame;
        state.trajectoryPublicationVersion = prediction.trajectory.publicationVersion;
        update.stable = true;
        return update;
    }

    if ( !reset )
    {
        for ( std::size_t index = 0; index < state.recordCursorCount; ++index )
        {
            const ReplayTrajectoryRecord& record = prediction.trajectory.records[index];
            const ReplayPredictionDrawRecordCursor& cursor = state.recordCursors[index];

            if ( cursor.key != record.key || cursor.recordVersion != record.version ||
                 cursor.consumedPointCount > ReplayTrajectoryPublishedPointCount( record ) )
            {
                reset = true;
                break;
            }
        }
    }

    if ( prediction.trajectory.records.size() > ReplayPredictionDrawListState::MAX_RECORD_CURSORS )
    {
        // The prediction store is bounded well below this presentation limit.
        // Returning an empty list keeps this defensive path allocation-free.
        retainedGeometry.Clear();
        retainedMarkers.Clear();
        state.Reset();
        update.reset = true;
        return update;
    }

    if ( reset )
    {
        retainedGeometry.Clear();
        retainedMarkers.Clear();
        state.Reset();
        state.recordCursorCount = prediction.trajectory.records.size();
        state.targetId = pathVisualizer.targetId;
        state.velocityPreviewTargetId = prediction.dragPreview.targetId;
        state.generation = prediction.timeline.generation;
        state.topologyVersion = prediction.topology.version;
        state.trajectoryBuildTopologyVersion = prediction.trajectory.topologyVersion;
        state.colorMode = pathVisualizer.colorMode;
        state.usingBuildFrames = prediction.timeline.usingBuildFrames;
        state.pathPresentation = prediction.pathPresentation;
        state.velocityPreviewActive = prediction.dragPreview.active;
        state.sampleStride = sampleStride;
        state.valid = true;
        update.reset = true;
    }
    else
    {
        // Record and marker vectors publish append-only prefixes. Grow cursor
        // banks in place so discovering a new causal body never rebuilds old
        // commands; replacement of an existing record is still caught above.
        state.recordCursorCount = prediction.trajectory.records.size();
    }

    const uint16_t activeRootBranch = ReplayPredictionDrawBranch( prediction.timeline.usingBuildFrames );
    const uint16_t activeChildBranchBase = prediction.timeline.usingBuildFrames
                                               ? static_cast<uint16_t>( REPLAY_PATH_MAX_FUTURE_NODES )
                                               : 0u;

    const uint16_t activeChildBranchEnd = static_cast<uint16_t>( activeChildBranchBase +
                                                                 static_cast<uint16_t>( REPLAY_PATH_MAX_FUTURE_NODES ) );

    for ( std::size_t recordIndex = 0; recordIndex < prediction.trajectory.records.size(); ++recordIndex )
    {
        const ReplayTrajectoryRecord& record = prediction.trajectory.records[recordIndex];
        ReplayPredictionDrawRecordCursor& cursor = state.recordCursors[recordIndex];
        const bool initializeCursor = cursor.recordVersion == 0;

        if ( initializeCursor )
        {
            cursor.key = record.key;
            cursor.recordVersion = record.version;
            cursor.sourceRecordIndex = recordIndex;
            cursor.usesAuthoredColor = ReplayPredictionUsesAuthoredBodyColor( prediction.pathPresentation,
                                                                              record.key.lane ) &&
                                       TryResolveReplayAuthoredPathColor( entities, record.key.bodyId, cursor.authoredColorR,
                                                                          cursor.authoredColorG, cursor.authoredColorB );
        }

        const bool previewReplacesRoot = prediction.dragPreview.active &&
                                         prediction.dragPreview.targetId.value == record.key.bodyId.value;

        const bool rootLane = !previewReplacesRoot && record.key.lane == ReplayTrajectoryLane::FutureRoot &&
                              record.key.branchOrdinal == activeRootBranch &&
                              record.key.bodyId.value == pathVisualizer.targetId.value;

        const bool allBodyLane = ReplayPredictionDrawsAllBodyRecord( prediction.pathPresentation, record.key,
                                                                     activeRootBranch, pathVisualizer.targetId );

        const bool childLane = ReplayPredictionDrawsCausalChildRecord( prediction.pathPresentation, record.key,
                                                                       activeChildBranchBase, activeChildBranchEnd );

        const bool baselineLane = prediction.baseline.valid && record.key.lane == ReplayTrajectoryLane::BaselineRoot &&
                                  record.key.branchOrdinal == REPLAY_TRAJECTORY_COMMITTED_BRANCH &&
                                  record.key.bodyId.value == prediction.baseline.rootId.value;

        if ( !rootLane && !allBodyLane && !childLane && !baselineLane )
        {
            cursor.consumedPointCount = ReplayTrajectoryPublishedPointCount( record );
            continue;
        }

        const std::size_t publishedCount = ReplayTrajectoryPublishedPointCount( record );

        if ( publishedCount < 2u )
        {
            cursor.consumedPointCount = publishedCount;
            continue;
        }

        if ( initializeCursor && record.key.lane == ReplayTrajectoryLane::FutureChildOutgoing )
        {
            const std::size_t nodeIndex = static_cast<std::size_t>( record.key.branchOrdinal - activeChildBranchBase );

            if ( nodeIndex < prediction.topology.futureNodes.size() )
            {
                const ReplayFrameIndex firstFrame = prediction.topology.futureNodes[nodeIndex].firstFrame;
                std::size_t firstOutgoingPoint = 0;

                while ( firstOutgoingPoint < publishedCount && record.points[firstOutgoingPoint].frameIndex < firstFrame )
                {
                    cursor.lastSelectedPointIndex = firstOutgoingPoint;
                    ++firstOutgoingPoint;
                }

                if ( firstOutgoingPoint < publishedCount && record.points[firstOutgoingPoint].frameIndex == firstFrame )
                {
                    ++firstOutgoingPoint;
                }

                cursor.consumedPointCount = firstOutgoingPoint;
            }
        }

        std::size_t pointIndex = ReplayPredictionFirstUnconsumedPoint( cursor.consumedPointCount );

        for ( ; pointIndex < publishedCount; ++pointIndex )
        {
            const ReplayTrajectoryPoint& point = record.points[pointIndex];

            if ( !baselineLane && point.frameIndex > prediction.timeline.revealFrame )
            {
                break;
            }

            const bool finalPoint = pointIndex + 1u == publishedCount && prediction.timeline.complete;
            const std::size_t recordStride = baselineLane ? 1u : sampleStride;

            if ( !baselineLane && !finalPoint && !ShouldDrawReplayPathFrame( point.frameIndex, recordStride ) )
            {
                continue;
            }

            const ReplayTrajectoryPoint& previous = record.points[cursor.lastSelectedPointIndex];

            if ( VectorMagSquared( point.position - previous.position ) <= REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
            {
                cursor.lastSelectedPointIndex = pointIndex;
                continue;
            }

            float r = 1.0f;
            float g = 1.0f;
            float b = 1.0f;
            const ReplayFrameIndex lastFrame = prediction.timeline.frames.empty()
                                                   ? point.frameIndex
                                                   : prediction.timeline.frames.back().frameIndex;

            if ( cursor.usesAuthoredColor )
            {
                r = cursor.authoredColorR;
                g = cursor.authoredColorG;
                b = cursor.authoredColorB;
            }
            else
            {
                ResolveReplayPathColor( pathVisualizer.colorMode, record.key.lane, record.key.bodyId, record.depth,
                                        ReplayPathFrameT( point.frameIndex, record.firstFrame, lastFrame ),
                                        ReplayTrajectorySegmentSpeed( previous, point ), r, g, b );
            }

            if ( baselineLane )
            {
                if ( EnsureReplayRetainedRangeChunk( retainedGeometry, cursor, record, recordIndex, false ) )
                {
                    retainedGeometry.AddBaselinePathSegment( cursor.retainedRangeIndex, previous.position, point.position, r,
                                                             g, b );
                }
                else
                {
                    retainedGeometry.RecordDroppedSegment(
                        SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::BaselineRoot );
                }
            }
            else
            {
                SkullbonezCore::Core::MainMemoryReplayTrajectoryLane
                    diagnosticLane = SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureRoot;

                if ( record.key.lane == ReplayTrajectoryLane::FutureChildIncoming )
                {
                    diagnosticLane = SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureChildIncoming;
                }
                else if ( record.key.lane == ReplayTrajectoryLane::FutureChildOutgoing )
                {
                    diagnosticLane = SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureChildOutgoing;
                }

                const float emphasis = rootLane && record.key.bodyId.value == pathVisualizer.targetId.value ? 1.0f : 0.0f;

                if ( EnsureReplayRetainedRangeChunk( retainedGeometry, cursor, record, recordIndex, false ) )
                {
                    retainedGeometry.AddPathSegment( cursor.retainedRangeIndex, previous.position, point.position, r, g, b,
                                                     diagnosticLane, emphasis );
                }
                else
                {
                    retainedGeometry.RecordDroppedSegment( diagnosticLane );
                }
            }

            cursor.lastSelectedPointIndex = pointIndex;
        }

        cursor.consumedPointCount = pointIndex;
    }

    AppendReplayPredictionRetainedEvidence( prediction, pathVisualizer, colliderStore, retainedGeometry, retainedMarkers,
                                            state, reset );
    state.revealFrame = prediction.timeline.revealFrame;
    state.topologyVersion = prediction.topology.version;
    state.trajectoryBuildTopologyVersion = prediction.trajectory.topologyVersion;
    state.retainedMarkerCount = prediction.markers.retainedMarkers.size();
    state.trajectoryPublicationVersion = prediction.trajectory.publicationVersion;
    state.ordinaryRibbonCapacityRemaining = retainedGeometry.OrdinaryCountRemaining();
    state.priorityRibbonCapacityRemaining = retainedGeometry.PriorityCountRemaining();
    state.saturated = state.ordinaryRibbonCapacityRemaining == 0u && state.priorityRibbonCapacityRemaining == 0u;
    update.appended = retainedGeometry.Revision() != geometryRevisionBefore ||
                      retainedMarkers.ReplayGeometryRevision() != markerRevisionBefore;

    return update;
}

void AppendReplayPredictionProvisionalTails( const ReplayPredictionPresentationView& prediction,
                                             const RunReplayPathVisualizerState& pathVisualizer,
                                             const ReplayPredictionDrawListState& state, const ColliderStore& colliderStore,
                                             EditorTracer& tracer )
{
    if ( !state.valid || prediction.timeline.frames.empty() )
    {
        return;
    }

    AppendReplayVelocityDragPreview( prediction, pathVisualizer, state, tracer );

    if ( prediction.timeline.complete && prediction.timeline.revealFrame >= prediction.timeline.frames.back().frameIndex )
    {
        return;
    }

    const uint16_t activeRootBranch = ReplayPredictionDrawBranch( prediction.timeline.usingBuildFrames );
    const uint16_t activeChildBranchBase = prediction.timeline.usingBuildFrames
                                               ? static_cast<uint16_t>( REPLAY_PATH_MAX_FUTURE_NODES )
                                               : 0u;

    const uint16_t activeChildBranchEnd = static_cast<uint16_t>( activeChildBranchBase +
                                                                 static_cast<uint16_t>( REPLAY_PATH_MAX_FUTURE_NODES ) );

    const ReplayFrameIndex lastFrame = prediction.timeline.frames.back().frameIndex;
    std::size_t ordinaryTailBudget = (std::min)( state.ordinaryRibbonCapacityRemaining,
                                                 tracer.ReplayPathRibbonSegmentCapacityRemaining() );

    std::size_t priorityTailBudget = (std::min)( state.priorityRibbonCapacityRemaining,
                                                 tracer.ReplayPriorityRibbonSegmentCapacityRemaining() );

    for ( std::size_t recordIndex = 0; recordIndex < state.recordCursorCount; ++recordIndex )
    {
        const ReplayPredictionDrawRecordCursor& cursor = state.recordCursors[recordIndex];

        if ( cursor.recordVersion == 0 || cursor.sourceRecordIndex >= prediction.trajectory.records.size() )
        {
            continue;
        }

        const ReplayTrajectoryRecord& record = prediction.trajectory.records[cursor.sourceRecordIndex];
        const bool previewReplacesRoot = prediction.dragPreview.active &&
                                         prediction.dragPreview.targetId.value == record.key.bodyId.value;

        const bool rootLane = !previewReplacesRoot && record.key.lane == ReplayTrajectoryLane::FutureRoot &&
                              record.key.branchOrdinal == activeRootBranch &&
                              record.key.bodyId.value == pathVisualizer.targetId.value;

        const bool allBodyLane = ReplayPredictionDrawsAllBodyRecord( prediction.pathPresentation, record.key,
                                                                     activeRootBranch, pathVisualizer.targetId );

        const bool childLane = ReplayPredictionDrawsCausalChildRecord( prediction.pathPresentation, record.key,
                                                                       activeChildBranchBase, activeChildBranchEnd );

        if ( !rootLane && !allBodyLane && !childLane )
        {
            continue;
        }

        if ( record.key.lane == ReplayTrajectoryLane::FutureChildOutgoing )
        {
            const std::size_t nodeIndex = static_cast<std::size_t>( record.key.branchOrdinal - activeChildBranchBase );

            if ( nodeIndex >= prediction.topology.futureNodes.size() ||
                 prediction.timeline.revealFrame <= prediction.topology.futureNodes[nodeIndex].firstFrame )
            {
                continue;
            }
        }

        const std::size_t publishedCount = ReplayTrajectoryPublishedPointCount( record );

        if ( publishedCount < 2u || cursor.lastSelectedPointIndex >= publishedCount )
        {
            continue;
        }

        std::size_t tailIndex = cursor.lastSelectedPointIndex;

        while ( tailIndex + 1u < publishedCount &&
                record.points[tailIndex + 1u].frameIndex <= prediction.timeline.revealFrame )
        {
            ++tailIndex;
        }

        if ( tailIndex == cursor.lastSelectedPointIndex )
        {
            continue;
        }

        const ReplayTrajectoryPoint& previous = record.points[cursor.lastSelectedPointIndex];
        const ReplayTrajectoryPoint& point = record.points[tailIndex];

        if ( VectorMagSquared( point.position - previous.position ) <= REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
        {
            continue;
        }

        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;

        if ( cursor.usesAuthoredColor )
        {
            r = cursor.authoredColorR;
            g = cursor.authoredColorG;
            b = cursor.authoredColorB;
        }
        else
        {
            ResolveReplayPathColor( pathVisualizer.colorMode, record.key.lane, record.key.bodyId, record.depth,
                                    ReplayPathFrameT( point.frameIndex, record.firstFrame, lastFrame ),
                                    ReplayTrajectorySegmentSpeed( previous, point ), r, g, b );
        }

        SkullbonezCore::Core::MainMemoryReplayTrajectoryLane
            diagnosticLane = SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureRoot;

        if ( record.key.lane == ReplayTrajectoryLane::FutureChildIncoming )
        {
            diagnosticLane = SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureChildIncoming;
        }
        else if ( record.key.lane == ReplayTrajectoryLane::FutureChildOutgoing )
        {
            diagnosticLane = SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureChildOutgoing;
        }

        const float emphasis = rootLane && record.key.bodyId.value == pathVisualizer.targetId.value ? 1.0f : 0.0f;

        if ( ordinaryTailBudget > 0u )
        {
            tracer.AddReplayPathSegment( previous.position, point.position, r, g, b, diagnosticLane, emphasis );
            --ordinaryTailBudget;
        }
    }

    for ( std::size_t markerIndex = 0; markerIndex < state.retainedTrailCursorCount; ++markerIndex )
    {
        const ReplayPredictionDrawRecordCursor& cursor = state.retainedTrailCursors[markerIndex];

        if ( cursor.recordVersion == 0 || cursor.sourceRecordIndex >= prediction.trajectory.records.size() )
        {
            continue;
        }

        const ReplayTrajectoryRecord& record = prediction.trajectory.records[cursor.sourceRecordIndex];
        const std::size_t publishedCount = ReplayTrajectoryPublishedPointCount( record );

        if ( publishedCount < 2u || cursor.lastSelectedPointIndex >= publishedCount )
        {
            continue;
        }

        std::size_t tailIndex = cursor.lastSelectedPointIndex;

        while ( tailIndex + 1u < publishedCount &&
                record.points[tailIndex + 1u].frameIndex <= prediction.timeline.revealFrame )
        {
            ++tailIndex;
        }

        if ( tailIndex == cursor.lastSelectedPointIndex )
        {
            continue;
        }

        const ReplayTrajectoryPoint& previous = record.points[cursor.lastSelectedPointIndex];
        const ReplayTrajectoryPoint& point = record.points[tailIndex];

        if ( VectorMagSquared( point.position - previous.position ) <= REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
        {
            continue;
        }

        float r = 0.82f;
        float g = 0.82f;
        float b = 0.88f;
        ResolveReplayPathColor( pathVisualizer.colorMode, ReplayTrajectoryLane::RetainedTrail, record.key.bodyId,
                                record.depth, ReplayPathFrameT( point.frameIndex, 0, lastFrame ),
                                ReplayTrajectorySegmentSpeed( previous, point ), r, g, b );

        if ( priorityTailBudget > 0u )
        {
            tracer.AddReplayCausalTrailSegment( previous.position, point.position, r, g, b );
            --priorityTailBudget;
        }
    }

    // Horizon boxes move with the revealed endpoint and therefore are not
    // append-only geometry. Keep only this bounded marker tail frame-local;
    // entry and rest/final-horizon boxes live in the retained command list.
    for ( const ReplayPredictionRetainedMarker& marker : prediction.markers.retainedMarkers )
    {
        if ( marker.hasRestPose || !marker.hasHorizonPose )
        {
            continue;
        }

        const ColliderRecord* collider = ReplayColliderRecordForModelIndex( &colliderStore, marker.modelRow.value );

        if ( collider )
        {
            tracer.AddReplayCausalHorizonMarker( marker.horizonPosition, marker.horizonOrientation, collider->shape );
        }
    }
}


ReplayPathVisualizerRenderResult RenderReplayPathVisualizer( const ReplayPredictionPresentationView& prediction,
                                                             const RunReplayPathVisualizerState& pathVisualizer,
                                                             PhysicsEngine& physics, const SceneEntityStore& entities,
                                                             EditorTracer& tracer, Core::Profiler* profiler,
                                                             ReplayFrameIndex presentFrame, bool hasPresentSample,
                                                             bool drawPredictionOverlay )
{
    ReplayPathVisualizerRenderResult result;
    PROFILE_SCOPED( "Frame/Replay/PathVisualizer" );

    // Concept: this marker owns replay presentation budgeting.
    //
    // Prediction has already published during frame update. Visible trajectory
    // drawing spends a fixed ribbon quota here so completed segments do not
    // flicker under transient render load.
    const auto visualizerStart = std::chrono::steady_clock::now();
    ReplayRibbonDrawQuota ribbonQuota = BeginReplayRibbonDrawQuota( tracer );

    if ( drawPredictionOverlay )
    {
        DrawReplayPredictionVisualizer( pathVisualizer, prediction, profiler, physics, entities, tracer, ribbonQuota );
    }

    if ( !pathVisualizer.hasTarget || !pathVisualizer.pastPathVisible )
    {
        return result;
    }

    const bool deterministicFidelityReveal = prediction.timeline.deterministicRevealEnabled &&
                                             prediction.timeline.complete && !prediction.controls.building;

    // Invariant: the frame-exact fidelity lane pins presentation scheduling.
    // A wall-clock overrun may defer retained-cache work during interactive
    // play, but it must not delete the striker trail and target marker from an
    // otherwise identical compared ReplayFrameIndex.
    if ( !deterministicFidelityReveal &&
         ReplayPredictionBudgetExpired( visualizerStart, REPLAY_PREDICTION_MAX_WORK_MILLISECONDS ) )
    {
        result.retainedRefreshBudgetExpired = true;
        return result;
    }

    if ( !hasPresentSample )
    {
        return result;
    }

    const PhysicsBodyStore& bodyStore = Physics::PhysicsEngine::ReadBodies( physics );
    const ColliderStore& colliderStore = Physics::PhysicsEngine::ReadColliders( physics );

    for ( const RunReplayPathTarget& target : pathVisualizer.targets )
    {
        if ( target.id.value == 0 )
        {
            continue;
        }

        PROFILE_SCOPED( "Frame/Replay/PathVisualizer/RetainedTarget" );

        if ( target.id.value == pathVisualizer.targetId.value )
        {
            PROFILE_SCOPED( "Frame/Replay/PathVisualizer/RetainedTarget/DrawRoot" );
            DrawReplayPastRootTrajectoryFromStore( prediction, target.id, pathVisualizer.colorMode, presentFrame, tracer,
                                                   ribbonQuota );
        }

        {
            PROFILE_SCOPED( "Frame/Replay/PathVisualizer/RetainedTarget/DrawMarker" );
            ModelRowHint targetHint;
            targetHint.value = target.modelRow.value;
            int markerIndex = -1;
            const bool markerResolved = TryResolveReplayBodyModelIndex( bodyStore, target.id, targetHint, bodyStore.Count(),
                                                                        markerIndex );

            if ( markerResolved )
            {
                TryAddReplayTargetMarkerFromStores( tracer, bodyStore, colliderStore, markerIndex );
            }
        }
    }

    return result;
}
} // namespace SkullbonezCore::Runtime::ReplayOverlay
void ReplayPredictionPresentation::RenderPathVisualizer( const ReplayPredictionPresentationView& prediction,
                                                         const RunReplayPathVisualizerState& path,
                                                         const ReplaySolverFrameSample* presentSample,
                                                         PhysicsEngine& physics, const SceneEntityStore& entities,
                                                         EditorTracer& tracer, bool drawPredictionOverlay )
{
    tracer.ClearReplayTrajectoryStats();
    const ReplayFrameIndex presentFrame = prediction.controls.generationPermitted && presentSample
                                              ? presentSample->frameIndex
                                              : prediction.timeline.sourceFrame;

    const SkullbonezCore::Runtime::ReplayOverlay::ReplayPathVisualizerRenderResult
        result = SkullbonezCore::Runtime::ReplayOverlay::RenderReplayPathVisualizer( prediction, path, physics, entities,
                                                                                     tracer, m_profiler, presentFrame,
                                                                                     presentSample != nullptr,
                                                                                     drawPredictionOverlay );

    if ( result.retainedRefreshBudgetExpired )
    {
        RecordTrajectoryBudgetExpiry( SkullbonezCore::Core::MainMemoryReplayBudgetPass::RetainedRefresh );
    }

    RecordTrajectoryFrameStats( tracer.ReplayTrajectoryStats() );
}
