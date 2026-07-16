/*
File: SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp
Purpose:
  Draws immutable replay prediction and retained-path publication as overlay geometry.

Mental model:
  ReplayPrediction publishes frame, trajectory, marker, and reveal values before
  rendering. This unit consumes those const values and emits fixed-capacity tracer
  ribbons and markers without scheduling work or mutating replay owners.

Glossary:
  Replay ribbon: Screen-space-width overlay stroke emitted through RunEditorTracer.
  Draw quota: Frame-local cap for ordinary replay ribbon segments.
  Published prefix: Contiguous prediction frames released by the worker for readers.

Invariants:
  - Drawing receives const prediction and presentation values only.
  - Drawing never starts, advances, cancels, or completes prediction work.
  - Quota exhaustion records dropped logical segments without allocating.

Related:
  - ReplayPrediction.h
  - ReplayOverlayRenderer.h
  - ReplayPresentation.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "ReplayOverlayRenderer.h"
#include "ReplayAuthoring.h"
#include "ReplayPrediction.h"
#include "ReplayPresentation.h"
#include "../Editor/EditorTools.h"
#include "../Tools/RuntimeTools.h"
#include "../Scene/SceneEntityStore.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsTimestep.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Runtime::ReplayOverlay;
using SkullbonezCore::Math::Vector::Vector3;

namespace
{
constexpr double REPLAY_PREDICTION_MAX_WORK_MILLISECONDS = 5.0;
constexpr std::size_t REPLAY_PATH_MAX_FUTURE_NODES = REPLAY_VISUAL_FUTURE_NODE_CAPACITY;
constexpr std::size_t REPLAY_PATH_MAX_SEGMENTS = 260;
constexpr std::size_t REPLAY_RIBBON_SEGMENTS_PER_PATH_SEGMENT = 1;
constexpr float REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ = 0.0001f;
constexpr float REPLAY_PREDICTION_CHILD_LINEAR_SPEED_SQ = 8.0f * 8.0f;
// Units: simulation distance per second. This presentation-only tuning point
// puts ordinary launched bodies near the middle of the heat ramp while leaving
// high-energy impacts visibly red.
constexpr float REPLAY_PATH_VELOCITY_HEAT_MAX_SPEED = 80.0f;
// Why: selection emphasis is presentation-only and fixed. Keeping one bounded
// value here makes the root-path privilege explicit while every sibling lane
// continues through the tracer's zero-emphasis default.
constexpr float REPLAY_SELECTED_PATH_EMPHASIS = 0.75f;
constexpr uint16_t REPLAY_TRAJECTORY_COMMITTED_BRANCH = 0;
constexpr uint16_t REPLAY_TRAJECTORY_BUILD_BRANCH = 1;
bool TryResolveReplayBodyModelIndex( const PhysicsBodyStore& bodyStore,
                                     ReplayBodyId id,
                                     int modelIndexHint,
                                     int modelCount,
                                     int& outModelIndex )
{
    if ( id.value == 0 )
    {
        return false;
    }

    const PhysicsBodyHandle body = bodyStore.HandleForReplayBodyId( id.value, modelIndexHint );
    const int modelIndex = bodyStore.ModelIndexForHandle( body );
    if ( modelIndex < 0 || modelIndex >= modelCount )
    {
        return false;
    }

    outModelIndex = modelIndex;
    return true;
}


bool TryResolveReplayBodyModelIndex( const PhysicsBodyStore& bodyStore,
                                     ReplayBodyId id,
                                     ModelRowHint& hint,
                                     int modelCount,
                                     int& outModelIndex )
{
    // Why: retained replay UI state still carries modelIndex integers until the
    // fable-06 conversion rows are complete. Naming the cache as ModelRowHint
    // keeps stable replay identity in ReplayBodyId while this resolver heals or
    // invalidates the dense-row guess.
    if ( !TryResolveReplayBodyModelIndex( bodyStore, id, hint.value, modelCount, outModelIndex ) )
    {
        hint.value = -1;
        return false;
    }

    hint.value = outModelIndex;
    return true;
}


bool TryAddReplayTargetMarkerFromStores( RunEditorTracer& tracer,
                                         const PhysicsBodyStore& bodyStore,
                                         const ColliderStore& colliderStore,
                                         int modelIndex )
{
    const PhysicsBodyHandle bodyHandle = bodyStore.HandleForModelIndex( modelIndex );
    const PhysicsColliderHandle colliderHandle = colliderStore.HandleForBodyHandle( bodyHandle );
    const PhysicsBodyRecord* body = bodyStore.RecordForHandle( bodyHandle );
    const ColliderRecord* collider = colliderStore.RecordForHandle( colliderHandle );
    if ( !body || !collider || bodyStore.ModelIndexForHandle( bodyHandle ) != modelIndex ||
         colliderStore.ModelIndexForHandle( colliderHandle ) != modelIndex || collider->body != bodyHandle )
    {
        return false;
    }

    // Invariant: replay target identity resolves through body handles before
    // markers read store rows. This avoids scanning the legacy object record mirror just
    // to recover a stable ReplayBodyId that PhysicsBodyStore already owns.
    const float radius = (std::max)( 1.0f, (std::max)( body->boundingRadius, collider->boundingRadius ) ) * 1.18f;
    tracer.AddReplayTargetMarker( body->position, body->orientation, collider->shape, radius );
    return true;
}

struct ReplayRibbonDrawQuota
{
    // Counts internal ribbon records, not logical trajectory lines. The tracer
    // merges legacy two-style inputs into one record per path segment.
    std::size_t remainingRibbonSegments = 0;
};

ReplayRibbonDrawQuota BeginReplayRibbonDrawQuota( const RunEditorTracer& tracer )
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
bool TryAccountReplayPathSegment( RunEditorTracer& tracer,
                                  ReplayRibbonDrawQuota* quota,
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

void AddOrAccountReplayPathSegment( RunEditorTracer& tracer,
                                    ReplayRibbonDrawQuota* quota,
                                    const Vector3& start,
                                    const Vector3& end,
                                    float r,
                                    float g,
                                    float b,
                                    SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane,
                                    float emphasis = 0.0f )
{
    if ( !TryAccountReplayPathSegment( tracer, quota, lane ) )
    {
        return;
    }

    tracer.AddReplayPathSegment( start, end, r, g, b, lane, emphasis );
}

void AddOrAccountReplayBaselinePathSegment( RunEditorTracer& tracer,
                                            ReplayRibbonDrawQuota* quota,
                                            const Vector3& start,
                                            const Vector3& end,
                                            float r,
                                            float g,
                                            float b )
{
    if ( !TryAccountReplayPathSegment( tracer,
                                       quota,
                                       SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::BaselineRoot ) )
    {
        return;
    }

    tracer.AddReplayBaselinePathSegment( start, end, r, g, b );
}

double ReplayPredictionElapsedMilliseconds( const std::chrono::steady_clock::time_point& start )
{
    return std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - start ).count();
}

bool ReplayPredictionBudgetExpired( const std::chrono::steady_clock::time_point& start, double budgetMilliseconds )
{
    return budgetMilliseconds > 0.0 && ReplayPredictionElapsedMilliseconds( start ) >= budgetMilliseconds;
}

template <typename FrameSample, typename BodySample>
const BodySample* FindReplayBodyByIdInSample( const FrameSample& sample, ReplayBodyId id )
{
    for ( const BodySample& body : sample.bodies )
    {
        if ( body.id.value == id.value )
        {
            return &body;
        }
    }
    return nullptr;
}

template <typename FrameSample, typename BodySample, bool AllowNegativeModelIndex>
const BodySample* FindReplayBodyByModelIndexInSample( const FrameSample& sample, int modelIndex )
{
    if constexpr ( !AllowNegativeModelIndex )
    {
        if ( modelIndex < 0 )
        {
            return nullptr;
        }
    }
    if ( modelIndex >= 0 && modelIndex < static_cast<int>( sample.bodies.size() ) )
    {
        const BodySample& body = sample.bodies[static_cast<std::size_t>( modelIndex )];
        if ( body.modelRow.value == modelIndex )
        {
            return &body;
        }
    }
    for ( const BodySample& body : sample.bodies )
    {
        if ( body.modelRow.value == modelIndex )
        {
            return &body;
        }
    }
    return nullptr;
}

const RunReplayPredictionBodySample* FindReplayPredictionBodyById( const RunReplayPredictionFrame& frame,
                                                                   ReplayBodyId id )
{
    return FindReplayBodyByIdInSample<RunReplayPredictionFrame, RunReplayPredictionBodySample>( frame, id );
}

const RunReplayPredictionBodySample* FindReplayPredictionBodyByModelIndex( const RunReplayPredictionFrame& frame,
                                                                           int modelIndex )
{
    return FindReplayBodyByModelIndexInSample<RunReplayPredictionFrame, RunReplayPredictionBodySample, false>(
        frame,
        modelIndex );
}

bool ReplayModelIndexIsRagdollPart( const SceneEntityStore& entities, int modelIndex )
{
    // Hazard: physics debug contacts use -1 for terrain/world counterparts.
    // That sentinel is not a scene row and must never reach group metadata.
    if ( modelIndex < 0 || modelIndex >= entities.Count() )
    {
        return false;
    }
    const SceneEntityRecord* entity = entities.TryGet( modelIndex );
    return entity && entity->behaviorGroup.kind == SceneBehaviorGroupKind::SimpleRagdoll;
}

const RunReplayPredictionBodySample*
FindReplayPredictionBodyByIdWithHint( const RunReplayPredictionFrame& frame, ReplayBodyId id, int modelIndex )
{
    if ( const RunReplayPredictionBodySample* body = FindReplayPredictionBodyByModelIndex( frame, modelIndex ) )
    {
        if ( body->id.value == id.value )
        {
            return body;
        }
    }
    return FindReplayPredictionBodyById( frame, id );
}

ReplayTrajectoryRecordKey ReplayTrajectoryKey( ReplayBodyId bodyId, ReplayTrajectoryLane lane, uint16_t branchOrdinal )
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

bool ReplayPredictionFutureTreeReadyForDraw( const ReplayPredictionPresentationView& prediction,
                                             ReplayBodyId rootId,
                                             bool usingBuildFrames,
                                             std::size_t frameCount )
{
    const std::size_t nodeCount = (std::min)( prediction.futureNodes.size(), REPLAY_PATH_MAX_FUTURE_NODES );
    return nodeCount > 0 && prediction.futureNodesCacheValid && prediction.topologyVersion != 0 &&
           prediction.trajectoryBuildValid && prediction.trajectoryBuildRootId.value == rootId.value &&
           prediction.trajectoryBuildUsingBuildFrames == usingBuildFrames &&
           prediction.trajectoryBuildTopologyVersion == prediction.topologyVersion &&
           prediction.trajectoryBuiltNodeCount == nodeCount && prediction.trajectoryChildFrameCount >= frameCount;
}

bool ReplayPredictionBodyHasVisibleLinearMotion( const RunReplayPredictionBodySample& body )
{
    return VectorMagSquared( body.linearVelocity ) >= REPLAY_PREDICTION_CHILD_LINEAR_SPEED_SQ;
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

void ReplayHueColor( ReplayBodyId bodyId, float& r, float& g, float& b )
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

void ResolveReplayPathColor( ReplayPathColorMode mode,
                             ReplayTrajectoryLane lane,
                             ReplayBodyId bodyId,
                             int causalDepth,
                             float pathT,
                             float speed,
                             float& r,
                             float& g,
                             float& b )
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

std::size_t ReplayPathStrideForSampleCount( std::size_t sampleCount )
{
    if ( sampleCount <= REPLAY_PATH_MAX_SEGMENTS )
    {
        return 1;
    }
    return ( sampleCount + REPLAY_PATH_MAX_SEGMENTS - 1 ) / REPLAY_PATH_MAX_SEGMENTS;
}

struct ReplayPredictionDrawFrameWindow
{
    ReplayFrameIndex lastFrame = 0;
    ReplayFrameIndex revealFrame = 0;
    std::size_t sampleStride = 1;
};

std::size_t ReplayTrajectoryPublishedPointCount( const ReplayTrajectoryRecord& record );
const ReplayTrajectoryRecord* ReplayTrajectoryRecordForDraw( std::span<const ReplayTrajectoryRecord> records,
                                                             ReplayBodyId id,
                                                             ReplayTrajectoryLane lane,
                                                             uint16_t branchOrdinal );
const ColliderRecord* ReplayColliderRecordForModelIndex( const ColliderStore* colliderStore, int modelIndex );


ReplayPredictionDrawFrameWindow
PublishedReplayPredictionDrawFrameWindow( const ReplayPredictionPresentationView& prediction,
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
    window.revealFrame = (std::min)( window.lastFrame, prediction.revealFrame );
    window.sampleStride = ReplayPathStrideForSampleCount( frameCount );
    return window;
}

void DrawReplayPredictionBaselineSnapshot( const ReplayPredictionPresentationView& prediction,
                                           ReplayPathColorMode colorMode,
                                           const ColliderStore& colliderStore,
                                           RunEditorTracer& tracer,
                                           ReplayRibbonDrawQuota& ribbonQuota )
{
    if ( !prediction.baselineValid )
    {
        return;
    }

    if ( const ReplayTrajectoryRecord* record = ReplayTrajectoryRecordForDraw( prediction.trajectoryRecords,
                                                                               prediction.baselineRootId,
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
                    ResolveReplayPathColor( colorMode,
                                            ReplayTrajectoryLane::BaselineRoot,
                                            record->key.bodyId,
                                            record->depth,
                                            ReplayPathFrameT( point.frameIndex, firstFrame, lastFrame ),
                                            ReplayTrajectorySegmentSpeed( *previous, point ),
                                            r,
                                            g,
                                            b );
                    AddOrAccountReplayBaselinePathSegment( tracer,
                                                           &ribbonQuota,
                                                           previous->position,
                                                           point.position,
                                                           r,
                                                           g,
                                                           b );
                }
                previous = &point;
            }
        }
    }

    for ( const ReplayPredictionBaselineBodyPose& pose : prediction.baselineBodyPoses )
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

const ReplayTrajectoryRecord* ReplayTrajectoryRecordForDraw( std::span<const ReplayTrajectoryRecord> records,
                                                             ReplayBodyId id,
                                                             ReplayTrajectoryLane lane,
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
void DrawReplayTrajectoryRecordSegments( const ReplayTrajectoryRecord& record,
                                         std::size_t pointCount,
                                         ReplayFrameIndex rangeStart,
                                         ReplayFrameIndex rangeEnd,
                                         ReplayFrameIndex forcedFrame,
                                         std::size_t sampleStride,
                                         RunEditorTracer& tracer,
                                         ReplayRibbonDrawQuota& ribbonQuota,
                                         SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane,
                                         ColorForFrame colorForFrame,
                                         float emphasis = 0.0f )
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
            AddOrAccountReplayPathSegment( tracer,
                                           &ribbonQuota,
                                           previous->position,
                                           point.position,
                                           r,
                                           g,
                                           b,
                                           lane,
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
                                                                     ReplayBodyId id,
                                                                     bool usingBuildFrames )
{
    const uint16_t branchBase = usingBuildFrames ? static_cast<uint16_t>( REPLAY_PATH_MAX_FUTURE_NODES ) : 0u;
    const uint16_t branchEnd =
        static_cast<uint16_t>( branchBase + static_cast<uint16_t>( REPLAY_PATH_MAX_FUTURE_NODES ) );
    for ( const ReplayTrajectoryRecord& record : prediction.trajectoryRecords )
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
                                                       ReplayPathColorMode colorMode,
                                                       bool usingBuildFrames,
                                                       ReplayFrameIndex revealFrame,
                                                       ReplayFrameIndex lastFrame,
                                                       RunEditorTracer& tracer )
{
    const ReplayTrajectoryRecord* record =
        FindReplayPredictionMarkerTrailRecord( prediction, marker.id, usingBuildFrames );
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
        const bool endpointFrame =
            point.frameIndex == revealFrame || point.frameIndex == lastFrame || i + 1u == pointCount;
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
            ResolveReplayPathColor( colorMode,
                                    ReplayTrajectoryLane::RetainedTrail,
                                    marker.id,
                                    record->depth,
                                    t,
                                    ReplayTrajectorySegmentSpeed( *previous, point ),
                                    r,
                                    g,
                                    b );
            tracer.AddReplayCausalTrailSegment( previous->position, point.position, r, g, b );
        }
        previous = &point;
    }
}

void DrawReplayPredictionRetainedMarkers( const ReplayPredictionPresentationView& prediction,
                                          ReplayPathColorMode colorMode,
                                          bool usingBuildFrames,
                                          ReplayFrameIndex revealFrame,
                                          ReplayFrameIndex lastFrame,
                                          const ColliderStore& colliderStore,
                                          RunEditorTracer& tracer )
{
    // Invariant: marker emission is bounded by SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS and independent
    // of the visualizer budget. Lines may degrade under load; already-revealed
    // yellow/grey boxes must not.
    for ( std::size_t i = 0; i < prediction.retainedMarkers.size(); ++i )
    {
        const ReplayPredictionRetainedMarker& marker = prediction.retainedMarkers[i];
        const ColliderRecord* collider = ReplayColliderRecordForModelIndex( &colliderStore, marker.modelRow.value );
        if ( !collider )
        {
            continue;
        }
        DrawReplayPredictionRetainedMarkerTrailFromStore( prediction,
                                                          marker,
                                                          colorMode,
                                                          usingBuildFrames,
                                                          revealFrame,
                                                          lastFrame,
                                                          tracer );
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
                                                  ReplayBodyId rootId,
                                                  ReplayPathColorMode colorMode,
                                                  bool usingBuildFrames,
                                                  ReplayFrameIndex lastFrame,
                                                  ReplayFrameIndex revealFrame,
                                                  std::size_t sampleStride,
                                                  RunEditorTracer& tracer,
                                                  ReplayRibbonDrawQuota& ribbonQuota )
{
    const ReplayTrajectoryRecord* record =
        ReplayTrajectoryRecordForDraw( prediction.trajectoryRecords,
                                       rootId,
                                       ReplayTrajectoryLane::FutureRoot,
                                       ReplayPredictionDrawBranch( usingBuildFrames ) );
    if ( !record )
    {
        return;
    }

    const std::size_t pointCount =
        usingBuildFrames ? prediction.frames.size() : ReplayTrajectoryPublishedPointCount( *record );
    DrawReplayTrajectoryRecordSegments(
        *record,
        pointCount,
        0,
        revealFrame,
        revealFrame,
        sampleStride,
        tracer,
        ribbonQuota,
        SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureRoot,
        [&]( const ReplayTrajectoryPoint& previous, const ReplayTrajectoryPoint& point, float& r, float& g, float& b )
        {
            ResolveReplayPathColor( colorMode,
                                    ReplayTrajectoryLane::FutureRoot,
                                    rootId,
                                    record->depth,
                                    ReplayPathFrameT( point.frameIndex, 0, lastFrame ),
                                    ReplayTrajectorySegmentSpeed( previous, point ),
                                    r,
                                    g,
                                    b );
        },
        REPLAY_SELECTED_PATH_EMPHASIS );
}

void DrawReplayPredictionSmallSceneBodyTrajectories( std::span<const RunReplayPredictionFrame> frames,
                                                     std::size_t frameCount,
                                                     ReplayBodyId selectedId,
                                                     ReplayPathColorMode colorMode,
                                                     ReplayFrameIndex revealFrame,
                                                     std::size_t requestedStride,
                                                     RunEditorTracer& tracer,
                                                     ReplayRibbonDrawQuota& ribbonQuota )
{
    constexpr std::size_t MAX_ALL_BODY_PREDICTION_COUNT = 8u;
    frameCount = (std::min)( frameCount, frames.size() );
    if ( frameCount < 2u || frames[0].bodies.size() < 2u || frames[0].bodies.size() > MAX_ALL_BODY_PREDICTION_COUNT )
    {
        return;
    }

    const std::size_t auxiliaryBodyCount = frames[0].bodies.size() - 1u;
    const std::size_t logicalSegmentsRemaining =
        ribbonQuota.remainingRibbonSegments / REPLAY_RIBBON_SEGMENTS_PER_PATH_SEGMENT;
    const std::size_t segmentsPerBody = (std::max)( std::size_t{ 1 }, logicalSegmentsRemaining / auxiliaryBodyCount );
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
            const RunReplayPredictionBodySample* body =
                FindReplayPredictionBodyByIdWithHint( frame, seedBody.id, seedBody.modelRow.value );
            if ( !body )
            {
                continue;
            }
            if ( hasPrevious && VectorMagSquared( body->position - previous ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
            {
                float r = 1.0f;
                float g = 1.0f;
                float b = 1.0f;
                ResolveReplayPathColor( colorMode,
                                        ReplayTrajectoryLane::FutureRoot,
                                        body->id,
                                        0,
                                        ReplayPathFrameT( frame.frameIndex, 0, lastFrame ),
                                        std::sqrt( VectorMagSquared( body->linearVelocity ) ),
                                        r,
                                        g,
                                        b );
                AddOrAccountReplayPathSegment( tracer,
                                               &ribbonQuota,
                                               previous,
                                               body->position,
                                               r,
                                               g,
                                               b,
                                               SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureRoot );
            }
            previous = body->position;
            hasPrevious = true;
        }
    }
}

void DrawReplayPredictionChildTrajectoryRecord( const ReplayPredictionPresentationView& prediction,
                                                const RunReplayPathTraceNode& node,
                                                std::size_t nodeIndex,
                                                ReplayPathColorMode colorMode,
                                                bool usingBuildFrames,
                                                ReplayTrajectoryLane lane,
                                                ReplayFrameIndex revealFrame,
                                                ReplayFrameIndex lastFrame,
                                                std::size_t sampleStride,
                                                RunEditorTracer& tracer,
                                                ReplayRibbonDrawQuota& ribbonQuota )
{
    const ReplayTrajectoryRecord* record =
        ReplayTrajectoryRecordForDraw( prediction.trajectoryRecords,
                                       node.id,
                                       lane,
                                       ReplayPredictionChildTrajectoryBranch( nodeIndex, usingBuildFrames ) );
    if ( !record )
    {
        return;
    }

    if ( lane == ReplayTrajectoryLane::FutureChildIncoming )
    {
        const ReplayFrameIndex endFrame = (std::min)( revealFrame, node.firstFrame );
        DrawReplayTrajectoryRecordSegments( *record,
                                            ReplayTrajectoryPublishedPointCount( *record ),
                                            0,
                                            endFrame,
                                            endFrame,
                                            sampleStride,
                                            tracer,
                                            ribbonQuota,
                                            SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureChildIncoming,
                                            [&]( const ReplayTrajectoryPoint& previous,
                                                 const ReplayTrajectoryPoint& point,
                                                 float& r,
                                                 float& g,
                                                 float& b )
                                            {
                                                ResolveReplayPathColor(
                                                    colorMode,
                                                    ReplayTrajectoryLane::FutureChildIncoming,
                                                    node.id,
                                                    node.depth,
                                                    ReplayPathFrameT( point.frameIndex, 0, node.firstFrame ),
                                                    ReplayTrajectorySegmentSpeed( previous, point ),
                                                    r,
                                                    g,
                                                    b );
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
        const bool endpointFrame =
            point.frameIndex == revealFrame || point.frameIndex == lastFrame || i + 1u == pointCount;
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
            ResolveReplayPathColor( colorMode,
                                    ReplayTrajectoryLane::FutureChildOutgoing,
                                    node.id,
                                    node.depth,
                                    t,
                                    ReplayTrajectorySegmentSpeed( *previous, point ),
                                    r,
                                    g,
                                    b );
            AddOrAccountReplayPathSegment( tracer,
                                           &ribbonQuota,
                                           previous->position,
                                           point.position,
                                           r,
                                           g,
                                           b,
                                           SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureChildOutgoing );
        }
        previous = &point;
    }
}

void DrawReplayPredictionChildTrajectoriesFromStore( const ReplayPredictionPresentationView& prediction,
                                                     ReplayPathColorMode colorMode,
                                                     bool usingBuildFrames,
                                                     ReplayFrameIndex revealFrame,
                                                     ReplayFrameIndex lastFrame,
                                                     std::size_t sampleStride,
                                                     RunEditorTracer& tracer,
                                                     ReplayRibbonDrawQuota& ribbonQuota )
{
    const std::size_t nodeCount = (std::min)( prediction.futureNodes.size(), REPLAY_PATH_MAX_FUTURE_NODES );
    for ( std::size_t i = 0; i < nodeCount; ++i )
    {
        const RunReplayPathTraceNode& node = prediction.futureNodes[i];
        DrawReplayPredictionChildTrajectoryRecord( prediction,
                                                   node,
                                                   i,
                                                   colorMode,
                                                   usingBuildFrames,
                                                   ReplayTrajectoryLane::FutureChildIncoming,
                                                   revealFrame,
                                                   lastFrame,
                                                   sampleStride,
                                                   tracer,
                                                   ribbonQuota );
        DrawReplayPredictionChildTrajectoryRecord( prediction,
                                                   node,
                                                   i,
                                                   colorMode,
                                                   usingBuildFrames,
                                                   ReplayTrajectoryLane::FutureChildOutgoing,
                                                   revealFrame,
                                                   lastFrame,
                                                   sampleStride,
                                                   tracer,
                                                   ribbonQuota );
    }
}

void DrawReplayPastRootTrajectoryFromStore( const ReplayPredictionPresentationView& prediction,
                                            ReplayBodyId rootId,
                                            ReplayPathColorMode colorMode,
                                            ReplayFrameIndex presentFrame,
                                            RunEditorTracer& tracer,
                                            ReplayRibbonDrawQuota& ribbonQuota )
{
    const ReplayTrajectoryRecord* record = ReplayTrajectoryRecordForDraw( prediction.trajectoryRecords,
                                                                          rootId,
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
    const std::size_t sampleStride = ReplayPathStrideForSampleCount( pointCount );
    // Concept: a single PastRoot store record contains the retained solver
    // window. Draw-time presentFrame only recolors the already-published prefix
    // into "history" and "recorded future" halves; it never rebuilds samples.
    DrawReplayTrajectoryRecordSegments(
        *record,
        pointCount,
        firstFrame,
        clampedPresent,
        clampedPresent,
        sampleStride,
        tracer,
        ribbonQuota,
        SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::PastRoot,
        [&]( const ReplayTrajectoryPoint& previous, const ReplayTrajectoryPoint& point, float& r, float& g, float& b )
        {
            ResolveReplayPathColor( colorMode,
                                    ReplayTrajectoryLane::PastRoot,
                                    rootId,
                                    record->depth,
                                    ReplayPathFrameT( point.frameIndex, firstFrame, clampedPresent ),
                                    ReplayTrajectorySegmentSpeed( previous, point ),
                                    r,
                                    g,
                                    b );
        },
        REPLAY_SELECTED_PATH_EMPHASIS );
    DrawReplayTrajectoryRecordSegments(
        *record,
        pointCount,
        clampedPresent,
        lastFrame,
        lastFrame,
        sampleStride,
        tracer,
        ribbonQuota,
        SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureRoot,
        [&]( const ReplayTrajectoryPoint& previous, const ReplayTrajectoryPoint& point, float& r, float& g, float& b )
        {
            ResolveReplayPathColor( colorMode,
                                    ReplayTrajectoryLane::FutureRoot,
                                    rootId,
                                    record->depth,
                                    ReplayPathFrameT( point.frameIndex, clampedPresent, lastFrame ),
                                    ReplayTrajectorySegmentSpeed( previous, point ),
                                    r,
                                    g,
                                    b );
        },
        REPLAY_SELECTED_PATH_EMPHASIS );
}

void DrawReplayPredictionRagdollTorsoTrails( std::span<const RunReplayPredictionFrame> frames,
                                             std::size_t frameCount,
                                             ReplayPathColorMode colorMode,
                                             ReplayFrameIndex revealFrame,
                                             const SceneEntityStore& collection,
                                             RunEditorTracer& tracer,
                                             ReplayRibbonDrawQuota& ribbonQuota )
{
    const int modelCount = collection.Count();
    frameCount = (std::min)( frameCount, frames.size() );
    if ( frameCount < 2 || modelCount <= 0 )
    {
        return;
    }

    const ReplayFrameIndex lastFrame = frames[frameCount - 1].frameIndex;
    const std::size_t sampleStride = ReplayPathStrideForSampleCount( frameCount );
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
                ResolveReplayPathColor( colorMode,
                                        ReplayTrajectoryLane::FutureChildOutgoing,
                                        body->id,
                                        1,
                                        t,
                                        std::sqrt( VectorMagSquared( body->linearVelocity ) ),
                                        r,
                                        g,
                                        b );
                AddOrAccountReplayPathSegment( tracer,
                                               &ribbonQuota,
                                               previous,
                                               body->position,
                                               r,
                                               g,
                                               b,
                                               SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::AuxiliaryTrail );
            }
            previous = body->position;
            hasPrevious = true;
        }
    }
}

struct ReplayPredictionAffectedBodyTrail
{
    ReplayBodyId id;
    ModelRowHint modelRow;
    std::size_t firstFrameSlot = 0;
    ReplayFrameIndex firstFrame = 0;
    int causalDepth = 1;
    // Concept: same two-box causal story as ReplayPathChildDrawState. Entry is
    // the body's in-place pose from prediction frame 0 (yellow, fixed);
    // lastMotionFrame times when the grey resting box may pop in. The grey
    // pose itself always comes from the completed buffer's final frame.
    ReplayFrameIndex lastMotionFrame = 0;
    Vector3 previous = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 entryPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Quaternion entryOrientation = IDENTITY_QUATERNION;
};

bool ReplayPredictionIdInFutureNodes( std::span<const RunReplayPathTraceNode> nodes, ReplayBodyId id )
{
    for ( const RunReplayPathTraceNode& node : nodes )
    {
        if ( node.id.value == id.value )
        {
            return true;
        }
    }
    return false;
}

std::size_t BuildReplayPredictionAffectedBodyTrails(
    std::span<const RunReplayPredictionFrame> frames,
    std::size_t frameCount,
    ReplayFrameIndex revealFrame,
    ReplayBodyId rootId,
    int rootModelIndex,
    std::span<const RunReplayPathTraceNode> futureNodes,
    const SceneEntityStore& collection,
    std::array<ReplayPredictionAffectedBodyTrail, REPLAY_PATH_MAX_FUTURE_NODES>& trails )
{
    frameCount = (std::min)( frameCount, frames.size() );
    if ( frameCount < 2 || rootId.value == 0 )
    {
        return 0;
    }

    // Concept: affected-body trails are visual evidence, not contact authority.
    //
    // The future-node cache feeds both the cause window and child path renderer.
    // This pass exists only as a visual fallback while that cache has not yet
    // published a body; it skips ids already represented by either contact- or
    // motion-derived nodes.
    std::size_t trailCount = 0;
    const RunReplayPredictionFrame& firstFrame = frames.front();
    for ( const RunReplayPredictionBodySample& initialBody : firstFrame.bodies )
    {
        if ( trailCount >= REPLAY_PATH_MAX_FUTURE_NODES )
        {
            break;
        }
        if ( initialBody.id.value == 0 || initialBody.id.value == rootId.value ||
             initialBody.modelRow.value == rootModelIndex ||
             ReplayPredictionIdInFutureNodes( futureNodes, initialBody.id ) )
        {
            continue;
        }
        if ( ReplayModelIndexIsRagdollPart( collection, initialBody.modelRow.value ) )
        {
            continue;
        }

        for ( std::size_t frameSlot = 1; frameSlot < frameCount; ++frameSlot )
        {
            // Why: a body whose first movement lies past the reveal cursor is
            // not part of the story yet. Skipping it here keeps its trail and
            // outline from pre-spawning ahead of the causal unfold.
            if ( frames[frameSlot].frameIndex > revealFrame )
            {
                break;
            }

            const RunReplayPredictionBodySample* body =
                FindReplayPredictionBodyByIdWithHint( frames[frameSlot], initialBody.id, initialBody.modelRow.value );
            if ( !body )
            {
                continue;
            }
            if ( !ReplayPredictionBodyHasVisibleLinearMotion( *body ) )
            {
                continue;
            }

            // Why: entry is the body's IN-PLACE pose from prediction frame 0 â€”
            // the wall exactly as the live scene knows it. Never a sampled
            // pose from after the impulse arrived.
            ReplayPredictionAffectedBodyTrail& trail = trails[trailCount++];
            trail.id = initialBody.id;
            trail.modelRow.value = body->modelRow.value;
            trail.firstFrameSlot = frameSlot;
            trail.firstFrame = frames[frameSlot].frameIndex;
            trail.lastMotionFrame = frames[frameSlot].frameIndex;
            trail.previous = initialBody.position;
            trail.entryPosition = initialBody.position;
            trail.entryOrientation = initialBody.orientation;
            trail.entryOrientation.Normalise();
            break;
        }
    }

    return trailCount;
}

void DrawReplayPredictionAffectedBodyTrails( std::span<const RunReplayPredictionFrame> frames,
                                             std::size_t frameCount,
                                             ReplayPathColorMode colorMode,
                                             ReplayFrameIndex revealFrame,
                                             ReplayBodyId rootId,
                                             int rootModelIndex,
                                             std::span<const RunReplayPathTraceNode> futureNodes,
                                             const SceneEntityStore& collection,
                                             RunEditorTracer& tracer,
                                             ReplayRibbonDrawQuota& ribbonQuota )
{
    std::array<ReplayPredictionAffectedBodyTrail, REPLAY_PATH_MAX_FUTURE_NODES> trails = {};
    const std::size_t trailCount = BuildReplayPredictionAffectedBodyTrails( frames,
                                                                            frameCount,
                                                                            revealFrame,
                                                                            rootId,
                                                                            rootModelIndex,
                                                                            futureNodes,
                                                                            collection,
                                                                            trails );
    frameCount = (std::min)( frameCount, frames.size() );
    if ( trailCount == 0 )
    {
        return;
    }

    const ReplayFrameIndex lastFrame = frames[frameCount - 1].frameIndex;
    const std::size_t sampleStride = ReplayPathStrideForSampleCount( frameCount );
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

            const RunReplayPredictionBodySample* body =
                FindReplayPredictionBodyByIdWithHint( frame, trail.id, trail.modelRow.value );
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
                ResolveReplayPathColor( colorMode,
                                        ReplayTrajectoryLane::FutureChildOutgoing,
                                        trail.id,
                                        trail.causalDepth,
                                        t,
                                        std::sqrt( VectorMagSquared( body->linearVelocity ) ),
                                        r,
                                        g,
                                        b );
                AddOrAccountReplayPathSegment( tracer,
                                               &ribbonQuota,
                                               trail.previous,
                                               body->position,
                                               r,
                                               g,
                                               b,
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
                                  const ReplayPredictionPresentationView& prediction,
                                  const SceneEntityStore& modelCollection,
                                  const ColliderStore& colliderStore,
                                  RunEditorTracer& tracer,
                                  ReplayRibbonDrawQuota& ribbonQuota )
{
    const bool usingBuildFrames = prediction.usingBuildFrames;
    const std::span<const RunReplayPredictionFrame> activePredictionFrames = prediction.frames;
    const std::size_t activePredictionFrameCount = activePredictionFrames.size();
    if ( activePredictionFrameCount < 2 )
    {
        return false;
    }

    // Concept: every pass below draws only frames at or before the reveal
    // cursor. That single clamp is what turns a finished prediction buffer into
    // an unfolding animation: the root line grows first, and each child starts
    // drawing when the cursor passes the frame where its cause happened.
    const ReplayPredictionDrawFrameWindow drawWindow =
        PublishedReplayPredictionDrawFrameWindow( prediction, activePredictionFrames, activePredictionFrameCount );
    DrawReplayPredictionBaselineSnapshot( prediction, pathVisualizer.colorMode, colliderStore, tracer, ribbonQuota );

    if ( !pathVisualizer.hasTarget || pathVisualizer.targetId.value == 0 )
    {
        if ( prediction.ragdollVisualsEnabled )
        {
            DrawReplayPredictionRagdollTorsoTrails( activePredictionFrames,
                                                    activePredictionFrameCount,
                                                    pathVisualizer.colorMode,
                                                    drawWindow.revealFrame,
                                                    modelCollection,
                                                    tracer,
                                                    ribbonQuota );
        }
        return true;
    }

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/DrawRoot" );
        DrawReplayPredictionRootTrajectoryFromStore( prediction,
                                                     pathVisualizer.targetId,
                                                     pathVisualizer.colorMode,
                                                     usingBuildFrames,
                                                     drawWindow.lastFrame,
                                                     drawWindow.revealFrame,
                                                     drawWindow.sampleStride,
                                                     tracer,
                                                     ribbonQuota );
        DrawReplayPredictionSmallSceneBodyTrajectories( activePredictionFrames,
                                                        activePredictionFrameCount,
                                                        pathVisualizer.targetId,
                                                        pathVisualizer.colorMode,
                                                        drawWindow.revealFrame,
                                                        drawWindow.sampleStride,
                                                        tracer,
                                                        ribbonQuota );
    }
    const bool drawFutureTree = ReplayPredictionFutureTreeReadyForDraw( prediction,
                                                                        pathVisualizer.targetId,
                                                                        usingBuildFrames,
                                                                        activePredictionFrameCount );
    if ( drawFutureTree )
    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/DrawChildren" );
        DrawReplayPredictionChildTrajectoriesFromStore( prediction,
                                                        pathVisualizer.colorMode,
                                                        usingBuildFrames,
                                                        drawWindow.revealFrame,
                                                        drawWindow.lastFrame,
                                                        drawWindow.sampleStride,
                                                        tracer,
                                                        ribbonQuota );
    }

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/DrawAffectedBodies" );
        DrawReplayPredictionAffectedBodyTrails( activePredictionFrames,
                                                activePredictionFrameCount,
                                                pathVisualizer.colorMode,
                                                drawWindow.revealFrame,
                                                pathVisualizer.targetId,
                                                pathVisualizer.targetModelRow.value,
                                                prediction.futureNodes,
                                                modelCollection,
                                                tracer,
                                                ribbonQuota );
    }

    if ( prediction.ragdollVisualsEnabled )
    {
        DrawReplayPredictionRagdollTorsoTrails( activePredictionFrames,
                                                activePredictionFrameCount,
                                                pathVisualizer.colorMode,
                                                drawWindow.revealFrame,
                                                modelCollection,
                                                tracer,
                                                ribbonQuota );
    }
    DrawReplayPredictionRetainedMarkers( prediction,
                                         pathVisualizer.colorMode,
                                         usingBuildFrames,
                                         drawWindow.revealFrame,
                                         drawWindow.lastFrame,
                                         colliderStore,
                                         tracer );
    return true;
}

void DrawReplayPredictionVisualizer( const RunReplayPathVisualizerState& pathVisualizer,
                                     const ReplayPredictionPresentationView& prediction,
                                     PhysicsEngine& physicsEngine,
                                     const SceneEntityStore& entities,
                                     RunEditorTracer& tracer,
                                     ReplayRibbonDrawQuota& ribbonQuota )
{
    PROFILE_SCOPED( "Frame/Replay/PathVisualizer/Prediction" );
    const ColliderStore& colliderStore = PhysicsEngine::ReadColliders( physicsEngine );
    DrawReplayPredictionOverlay( pathVisualizer, prediction, entities, colliderStore, tracer, ribbonQuota );
}

const ReplaySolverBodySample* FindReplayBodyById( const ReplaySolverFrameSample& sample, ReplayBodyId id )
{
    return FindReplayBodyByIdInSample<ReplaySolverFrameSample, ReplaySolverBodySample>( sample, id );
}

const ReplaySolverBodySample* FindReplayBodyByModelIndex( const ReplaySolverFrameSample& sample, int modelIndex )
{
    return FindReplayBodyByModelIndexInSample<ReplaySolverFrameSample, ReplaySolverBodySample, true>( sample,
                                                                                                      modelIndex );
}

int ReplayRagdollTorsoModelIndexForPart( const SceneEntityStore& entities, int modelIndex )
{
    const SceneEntityRecord* entity = entities.TryGet( modelIndex );
    if ( !entity || entity->behaviorGroup.kind != SceneBehaviorGroupKind::SimpleRagdoll )
    {
        return modelIndex;
    }
    const int rootRow = entities.FindBySceneObjectId( entity->behaviorGroup.rootObjectId );
    return rootRow >= 0 ? rootRow : modelIndex;
}

Vector3 ReplayNormalizeOr( Vector3 value, const Vector3& fallback )
{
    const float magSq = VectorMagSquared( value );
    if ( magSq <= TOLERANCE * TOLERANCE )
    {
        return fallback;
    }
    value /= sqrtf( magSq );
    return value;
}

bool ReplayContactHasModelIndex( const ReplaySolverPersistentContactSample& contact, int modelIndex )
{
    return modelIndex >= 0 && ( contact.bodyA == modelIndex || contact.bodyB == modelIndex );
}

int ReplayContactOtherModelIndex( const ReplaySolverPersistentContactSample& contact, int modelIndex )
{
    if ( contact.bodyA == modelIndex )
    {
        return contact.bodyB;
    }
    if ( contact.bodyB == modelIndex )
    {
        return contact.bodyA;
    }
    return -1;
}

Vector3 ReplayContactPoint( const ReplaySolverFrameSample& sample, const ReplaySolverPersistentContactSample& contact )
{
    if ( const ReplaySolverBodySample* bodyA = FindReplayBodyByModelIndex( sample, contact.bodyA ) )
    {
        return bodyA->position + contact.rA;
    }
    if ( const ReplaySolverBodySample* bodyB = FindReplayBodyByModelIndex( sample, contact.bodyB ) )
    {
        return bodyB->position + contact.rB;
    }
    return SkullbonezCore::Math::Vector::ZERO_VECTOR;
}

Vector3 ReplayContactNormalForModel( const ReplaySolverPersistentContactSample& contact, int modelIndex )
{
    Vector3 normal = contact.normal;
    if ( contact.isTerrain && VectorMagSquared( contact.terrainNormal ) > TOLERANCE * TOLERANCE )
    {
        normal = contact.terrainNormal;
    }
    if ( contact.bodyB == modelIndex && !contact.isTerrain )
    {
        normal = normal * -1.0f;
    }
    return ReplayNormalizeOr( normal, Vector3( 0.0f, 1.0f, 0.0f ) );
}

} // namespace

namespace SkullbonezCore::Runtime::ReplayOverlay
{
ReplayPathVisualizerRenderResult RenderReplayPathVisualizer( const ReplayPathVisualizerRenderContext& context )
{
    ReplayPathVisualizerRenderResult result;
    PROFILE_SCOPED( "Frame/Replay/PathVisualizer" );
    // Concept: this marker owns replay presentation budgeting.
    //
    // Prediction has already published during frame update. Visible trajectory
    // drawing spends a fixed ribbon quota here so completed segments do not
    // flicker under transient render load.
    const auto visualizerStart = std::chrono::steady_clock::now();
    ReplayRibbonDrawQuota ribbonQuota = BeginReplayRibbonDrawQuota( context.tracer );
    DrawReplayPredictionVisualizer( context.pathVisualizer,
                                    context.prediction,
                                    context.physics,
                                    context.entities,
                                    context.tracer,
                                    ribbonQuota );
    const ReplayPredictionPresentationView& prediction = context.prediction;
    const RunReplayPathVisualizerState& pathVisualizer = context.pathVisualizer;
    if ( !prediction.enabled && prediction.frames.size() >= 2 && pathVisualizer.hasTarget &&
         !pathVisualizer.pastPathVisible && prediction.targetId.value == pathVisualizer.targetId.value )
    {
        // Why: Play disables prediction but keeps the committed path preview;
        // when the user has hidden the past lane, do not refresh retained store
        // data from the advancing live timeline behind that frozen preview.
        return result;
    }
    const bool deterministicFidelityReveal =
        prediction.deterministicRevealEnabled && prediction.complete && !prediction.building;
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

    if ( !pathVisualizer.hasTarget )
    {
        return result;
    }

    if ( !pathVisualizer.pastPathVisible )
    {
        // Why: hiding the past lane must stop both drawing and the retained
        // node report. Prediction keeps its separate future-node cache.
        return result;
    }

    if ( !context.hasPresentSample )
    {
        return result;
    }

    const PhysicsBodyStore& bodyStore = Physics::PhysicsEngine::ReadBodies( context.physics );
    const ColliderStore& colliderStore = Physics::PhysicsEngine::ReadColliders( context.physics );
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
            DrawReplayPastRootTrajectoryFromStore( prediction,
                                                   target.id,
                                                   pathVisualizer.colorMode,
                                                   context.presentFrame,
                                                   context.tracer,
                                                   ribbonQuota );
        }

        {
            PROFILE_SCOPED( "Frame/Replay/PathVisualizer/RetainedTarget/DrawMarker" );
            ModelRowHint targetHint;
            targetHint.value = target.modelRow.value;
            int markerIndex = -1;
            const bool markerResolved =
                TryResolveReplayBodyModelIndex( bodyStore, target.id, targetHint, bodyStore.Count(), markerIndex );
            if ( markerResolved )
            {
                TryAddReplayTargetMarkerFromStores( context.tracer, bodyStore, colliderStore, markerIndex );
            }
        }
    }
    return result;
}
} // namespace SkullbonezCore::Runtime::ReplayOverlay
void ReplayPresentation::RenderPathVisualizer( const ReplayPredictionPresentationView& prediction,
                                               const ReplaySolverFrameSample* presentSample,
                                               PhysicsEngine& physics,
                                               const SceneEntityStore& entities,
                                               RunEditorTracer& tracer )
{
    tracer.ClearReplayTrajectoryStats();
    const ReplayFrameIndex presentFrame =
        prediction.generationPermitted && presentSample ? presentSample->frameIndex : prediction.sourceFrame;
    const SkullbonezCore::Runtime::ReplayOverlay::ReplayPathVisualizerRenderContext
        context{ prediction, m_pathVisualizer, physics, entities, tracer, presentFrame, presentSample != nullptr };
    const SkullbonezCore::Runtime::ReplayOverlay::ReplayPathVisualizerRenderResult result =
        SkullbonezCore::Runtime::ReplayOverlay::RenderReplayPathVisualizer( context );
    if ( result.retainedRefreshBudgetExpired )
    {
        RecordTrajectoryBudgetExpiry( SkullbonezCore::Core::MainMemoryReplayBudgetPass::RetainedRefresh );
    }
    RecordTrajectoryFrameStats( tracer.ReplayTrajectoryStats() );
}


void ReplayPresentation::RenderCauseFocusOverlay( const RunReplayCauseTreeState& causeTree,
                                                  const ReplayPredictionPresentationView& prediction,
                                                  const ReplaySolverFrameSample* currentSolverSample,
                                                  const PhysicsBodyStore& bodyStore,
                                                  const ColliderStore& colliderStore,
                                                  const SceneEntityStore& entities,
                                                  RunEditorTracer& tracer )
{
    const RunReplayCameraState camera = CameraView();
    if ( camera.focusKind == RunReplayCameraFocusKind::None )
    {
        return;
    }

    if ( camera.focusKind == RunReplayCameraFocusKind::Body )
    {
        ModelRowHint focusHint;
        focusHint.value = camera.focusModelRow.value;
        int focusedModelIndex = -1;
        if ( TryResolveReplayBodyModelIndex( bodyStore,
                                             camera.focusedId,
                                             focusHint,
                                             bodyStore.Count(),
                                             focusedModelIndex ) )
        {
            TryAddReplayTargetMarkerFromStores( tracer, bodyStore, colliderStore, focusedModelIndex );
            return;
        }
    }

    if ( camera.focusKind == RunReplayCameraFocusKind::Manifold ||
         camera.focusKind == RunReplayCameraFocusKind::PredictionContact ||
         camera.focusKind == RunReplayCameraFocusKind::PredictionMotion )
    {
        if ( camera.focusKind == RunReplayCameraFocusKind::Manifold )
        {
            const ReplaySolverFrameSample* sample = currentSolverSample;
            if ( sample )
            {
                const ReplaySolverBodySample* focusedBody = FindReplayBodyById( *sample, camera.focusedId );
                const ReplaySolverBodySample* counterpartBody = FindReplayBodyById( *sample, camera.counterpartId );
                if ( focusedBody )
                {
                    bool drewContact = false;
                    for ( const ReplaySolverPersistentContactSample& contact :
                          sample->worldSnapshot.persistentContacts )
                    {
                        if ( !ReplayContactHasModelIndex( contact, focusedBody->modelRow.value ) )
                        {
                            continue;
                        }
                        const int otherModelIndex =
                            ReplayContactOtherModelIndex( contact, focusedBody->modelRow.value );
                        const bool terrain = contact.isTerrain || otherModelIndex < 0;
                        if ( camera.focusTerrain != terrain )
                        {
                            continue;
                        }
                        if ( !terrain && ( !counterpartBody || counterpartBody->modelRow.value != otherModelIndex ) )
                        {
                            continue;
                        }
                        tracer.AddReplayContactMarker(
                            ReplayContactPoint( *sample, contact ),
                            ReplayContactNormalForModel( contact, focusedBody->modelRow.value ),
                            0.1f,
                            0.95f,
                            1.0f );
                        drewContact = true;
                    }
                    if ( drewContact )
                    {
                        return;
                    }
                }
            }
        }
        else if ( camera.focusKind == RunReplayCameraFocusKind::PredictionContact )
        {
            ReplayFrameIndex focusFrame = 0;
            int focusedModelIndex = camera.focusModelRow.value;
            int counterpartModelIndex = camera.focusCounterpartModelRow.value;

            if ( causeTree.selectedRow >= 0 && causeTree.selectedRow < static_cast<int>( causeTree.rows.size() ) )
            {
                const RunReplayCauseTreeRow& row = causeTree.rows[static_cast<std::size_t>( causeTree.selectedRow )];
                if ( row.kind == RunReplayCauseTreeRowKind::PredictionContact &&
                     row.id.value == camera.focusedId.value )
                {
                    focusFrame = row.firstFrame;
                    focusedModelIndex = row.modelRow.value;
                    counterpartModelIndex = row.counterpartModelRow.value;
                }
            }
            else if ( camera.focusContactIndex >= 0 &&
                      camera.focusContactIndex < static_cast<int>( prediction.futureNodes.size() ) )
            {
                const RunReplayPathTraceNode& node =
                    prediction.futureNodes[static_cast<std::size_t>( camera.focusContactIndex )];
                if ( node.id.value == camera.focusedId.value && node.contactDerived )
                {
                    focusFrame = node.firstFrame;
                    focusedModelIndex = node.modelRow.value;
                    counterpartModelIndex = node.parentModelRow.value;
                }
            }

            bool drewPredictionManifold = false;
            for ( const RunReplayPredictionFrame& frame : prediction.frames )
            {
                if ( frame.frameIndex != focusFrame )
                {
                    continue;
                }

                // Why: prediction contacts are selected from the future-node
                // tree, but the full manifold lives in the frame's debug
                // contacts. Match by the selected child/parent body pair so the
                // manifold marker remains visible while that collision row is
                // selected.
                for ( const PhysicsDebugContact& contact : frame.debugContacts )
                {
                    const int contactModelA = ReplayRagdollTorsoModelIndexForPart( entities, contact.bodyA );
                    const int contactModelB = contact.bodyB >= 0
                                                  ? ReplayRagdollTorsoModelIndexForPart( entities, contact.bodyB )
                                                  : contact.bodyB;
                    const bool selectedPairAB = contactModelA == focusedModelIndex &&
                                                ( counterpartModelIndex < 0 || contactModelB == counterpartModelIndex );
                    const bool selectedPairBA = contactModelB == focusedModelIndex &&
                                                ( counterpartModelIndex < 0 || contactModelA == counterpartModelIndex );
                    if ( !selectedPairAB && !selectedPairBA )
                    {
                        continue;
                    }

                    Vector3 normal = contact.normal;
                    if ( selectedPairBA && contactModelB >= 0 )
                    {
                        normal = normal * -1.0f;
                    }
                    tracer.AddReplayContactMarker( contact.point,
                                                   ReplayNormalizeOr( normal, Vector3( 0.0f, 1.0f, 0.0f ) ),
                                                   0.1f,
                                                   0.95f,
                                                   1.0f );
                    drewPredictionManifold = true;
                }
                break;
            }
            if ( drewPredictionManifold )
            {
                return;
            }
        }
        tracer.AddReplayContactMarker( camera.targetPoint, camera.targetNormal, 0.1f, 0.95f, 1.0f );
        return;
    }

    if ( camera.focusKind == RunReplayCameraFocusKind::SolverRow )
    {
        tracer.AddReplayContactMarker( camera.targetPoint, camera.targetNormal, 0.2f, 0.85f, 1.0f );
        tracer.AddReplayImpulseVector( camera.targetPoint, camera.impulseVector, 1.0f, 0.32f, 0.12f );
    }
}
