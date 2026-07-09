/*
File: SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp
Purpose:
  Provides the replay subsystem ownership boundary for legacy Run replay callers.

Mental model:
  ReplayRuntime is mostly an accessor and coordination shell. It keeps recorder,
  loaded-artifact, tool, branch, and camera state in one owned object while Run
  still performs most replay behavior.

Glossary:
  Branch: Child replay timeline created from a restored source frame.
  Body store: Physics-owned live body records used for pose and velocity
    authority while legacy GameModel mirrors are retired.
  Cause tree row: UI row derived from retained solver contacts or prediction
    future nodes.
  Collider store: Physics-owned shape, material, and radius records paired with
    body handles.
  Hash log: Deterministic text stream that lets saved replay output be compared.
  Loaded presentation: Replay artifact data loaded from disk for scrub preview.
  Prediction worker: Amortized task that fills replay-owned prediction build
    frames outside the render thread.
  Ragdoll part: One body inside a multi-body SimpleRagdoll collection.
  Velocity edit: Replay tool state for selecting one path-target body and
    editing its linear or angular velocity vectors.

Invariants:
  - Accessors return owned state; callers must not store references past
    ReplayRuntime lifetime.
  - Solver hash-log paths derive from the presentation path so paired artifacts
    stay beside each other.
  - Scene and branch reset edges wait for prediction workers before clearing
    replay-owned scratch.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
  - SkullbonezSource/Runtime/Replay/ReplayExporter.h
  - SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h
*/
#include "ReplayRuntime.h"
#include "ReplayExporter.h"
#include "ReplayOverlayLayout.h"
#include "ReplayV2Artifact.h"
#include "../../Core/AmortizedTask.h"
#include "../../Core/Profiler.h"
#include "../../GameObjects/GameModelCollection.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsTimestep.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>

namespace SkullbonezCore::Basics
{
namespace
{
constexpr float REPLAY_RUNTIME_SCRUBBER_LIVE_THRESHOLD = 0.995f;
constexpr float REPLAY_RUNTIME_SCRUBBER_PRESENT_EPSILON = 0.0035f;
constexpr uint32_t REPLAY_WORLD_OVERRIDE_GRAVITY_CHANGED = 1u;
constexpr uint32_t REPLAY_WORLD_OVERRIDE_FLUID_HEIGHT_CHANGED = 2u;
constexpr uint32_t REPLAY_WORLD_OVERRIDE_FLUID_DENSITY_CHANGED = 4u;
constexpr uint32_t REPLAY_LAUNCHER_FIRE_PROJECTILE = 1u;
constexpr uint32_t REPLAY_EDITOR_PLACE_FIXED = 1u;
constexpr uint32_t REPLAY_EDITOR_PLACE_TERRAIN_ALIGN = 2u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_TRANSLATE = 1u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_ROTATE = 2u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_SCALE = 4u;
constexpr uint32_t REPLAY_GENERATED_SCENE_EXACT_SOLVER_COUNTS = 1u;
constexpr uint32_t REPLAY_GENERATED_SCENE_UI_MODEL_COUNT = 2u;
constexpr uint32_t REPLAY_GENERATED_SCENE_UI_SOLVER_COUNTS = 4u;
constexpr uint32_t REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT = 8u;
constexpr uint32_t REPLAY_GENERATED_SCENE_OVERRIDE_MASK = 3u << REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT;
constexpr uint64_t REPLAY_EVENT_FNV_OFFSET = 14695981039346656037ull;
constexpr uint64_t REPLAY_EVENT_FNV_PRIME = 1099511628211ull;
constexpr int REPLAY_TRAJECTORY_SUBMISSION_STEADY_FRAME_TARGET = 120;

using Math::Vector::Vector3;
using Math::Vector::VectorMagSquared;
using Physics::ColliderRecord;
using Physics::ColliderStore;
using Physics::PhysicsBodyHandle;
using Physics::PhysicsBodyRecord;
using Physics::PhysicsBodyStore;
using Physics::PhysicsEngine;
using Physics::PhysicsPipelineRecord;
using Physics::PhysicsPipelineStageName;

template <typename T> uint64_t VectorCapacityBytes( const std::vector<T>& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( T ) );
}

uint32_t ReplayRuntimeFloatBits( float value )
{
    uint32_t bits = 0;
    static_assert( sizeof( bits ) == sizeof( value ), "Replay float payloads assume 32-bit floats." );
    std::memcpy( &bits, &value, sizeof( bits ) );
    return bits;
}

int32_t ReplayRuntimeFloatBitsSigned( float value )
{
    const uint32_t bits = ReplayRuntimeFloatBits( value );
    int32_t signedBits = 0;
    std::memcpy( &signedBits, &bits, sizeof( signedBits ) );
    return signedBits;
}

void ReplayRuntimeHashFloat( uint64_t& hash, float value )
{
    const uint32_t bits = ReplayRuntimeFloatBits( value );
    for ( int shift = 0; shift < 32; shift += 8 )
    {
        hash ^= static_cast<uint64_t>( ( bits >> shift ) & 0xFFu );
        hash *= REPLAY_EVENT_FNV_PRIME;
    }
}

void ReplayRuntimeHashInt( uint64_t& hash, int32_t value )
{
    const uint32_t bits = static_cast<uint32_t>( value );
    for ( int shift = 0; shift < 32; shift += 8 )
    {
        hash ^= static_cast<uint64_t>( ( bits >> shift ) & 0xFFu );
        hash *= REPLAY_EVENT_FNV_PRIME;
    }
}

void ReplayRuntimeAppendFloatHex( char*& cursor, std::size_t& remaining, float value )
{
    if ( remaining == 0 )
    {
        return;
    }

    const int written = std::snprintf( cursor, remaining, "%08X", ReplayRuntimeFloatBits( value ) );
    if ( written < 0 )
    {
        cursor[0] = '\0';
        return;
    }
    const std::size_t consumed = (std::min)( static_cast<std::size_t>( written ), remaining > 0 ? remaining - 1 : 0 );
    cursor += consumed;
    remaining -= consumed;
}

void ReplayRuntimeAppendVectorHex( char*& cursor, std::size_t& remaining, const Vector3& value )
{
    ReplayRuntimeAppendFloatHex( cursor, remaining, value.x );
    ReplayRuntimeAppendFloatHex( cursor, remaining, value.y );
    ReplayRuntimeAppendFloatHex( cursor, remaining, value.z );
}

void ReplayRuntimeAppendQuaternionHex( char*& cursor,
                                       std::size_t& remaining,
                                       const Math::Orientation::Quaternion& value )
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
    value.GetComponents( x, y, z, w );
    ReplayRuntimeAppendFloatHex( cursor, remaining, x );
    ReplayRuntimeAppendFloatHex( cursor, remaining, y );
    ReplayRuntimeAppendFloatHex( cursor, remaining, z );
    ReplayRuntimeAppendFloatHex( cursor, remaining, w );
}

const ReplayPresentationSample*
ReplayRuntimeLoadedPresentationSampleAtNormalized( const std::vector<ReplayPresentationSample>& samples,
                                                   float normalized )
{
    if ( samples.empty() )
    {
        return nullptr;
    }

    const float t = std::clamp( normalized, 0.0f, 1.0f );
    const std::size_t maxOffset = samples.size() - 1;
    const std::size_t offset = (std::min)( maxOffset, static_cast<std::size_t>( t * maxOffset + 0.5f ) );
    return &samples[offset];
}

float ReplayRuntimeScrubberRetainedPastSeconds( const ReplayRecorderStats& stats )
{
    if ( !stats.enabled || stats.sampleCount < 2 )
    {
        return PHYSICS_FIXED_DT;
    }
    return static_cast<float>( stats.sampleCount - 1 ) * PHYSICS_FIXED_DT;
}

const std::vector<RunReplayPredictionFrame>&
ReplayRuntimeActivePredictionFrames( const RunReplayPredictionState& prediction )
{
    if ( prediction.BuildFramesAreComplete() )
    {
        return prediction.build.buildFrames;
    }
    return prediction.simulation.frames;
}

const std::vector<RunReplayPredictionFrame>&
ReplayRuntimeTimelinePredictionFrames( const RunReplayPredictionState& prediction, std::size_t& outFrameCount )
{
    if ( prediction.BuildPrefixShouldBePresented() )
    {
        outFrameCount = prediction.PublishedBuildFrameCount();
        return prediction.build.buildFrames;
    }

    const std::vector<RunReplayPredictionFrame>& frames = ReplayRuntimeActivePredictionFrames( prediction );
    outFrameCount = frames.size();
    return frames;
}

float ReplayRuntimePredictionAvailableFutureSeconds( const RunReplayPredictionState& prediction )
{
    std::size_t frameCount = 0;
    const std::vector<RunReplayPredictionFrame>& frames =
        ReplayRuntimeTimelinePredictionFrames( prediction, frameCount );
    if ( frameCount < 2 )
    {
        return 0.0f;
    }
    // Why: prediction.enabled controls whether the future may rebuild, and
    // BuildPrefixShouldBePresented controls whether the in-progress prefix is
    // coherent enough to draw. The scrubber timeline follows that same prefix
    // so the live marker drifts left while prediction unfolds instead of
    // snapping only after the final frame vector swaps in.
    return static_cast<float>( frames[frameCount - 1].frameIndex ) * PHYSICS_FIXED_DT;
}

float ReplayRuntimeScrubberPresentTrackPosition( const ReplayRecorderStats& stats,
                                                 const RunReplayPredictionState& prediction )
{
    const float pastSeconds = (std::max)( PHYSICS_FIXED_DT, ReplayRuntimeScrubberRetainedPastSeconds( stats ) );
    const float futureSeconds = ReplayRuntimePredictionAvailableFutureSeconds( prediction );
    if ( futureSeconds <= PHYSICS_FIXED_DT )
    {
        return 1.0f;
    }
    return std::clamp( pastSeconds / ( pastSeconds + futureSeconds ), 0.05f, 0.995f );
}

uint64_t LauncherVisualMemoryBytes( const ReplayLauncherVisualSample& visual )
{
    return VectorCapacityBytes( visual.rayLines ) + VectorCapacityBytes( visual.laserShots );
}

uint64_t SolverWorldSnapshotMemoryBytes( const ReplaySolverWorldSnapshot& snapshot )
{
    uint64_t bytes = 0;
    bytes += VectorCapacityBytes( snapshot.timeRemaining );
    bytes += VectorCapacityBytes( snapshot.sleepSupportedThisFrame );
    bytes += VectorCapacityBytes( snapshot.sleepInhibitedThisFrame );
    bytes += VectorCapacityBytes( snapshot.sleepState );
    bytes += VectorCapacityBytes( snapshot.sleepCounter );
    bytes += VectorCapacityBytes( snapshot.underwaterSleepLocked );
    bytes += VectorCapacityBytes( snapshot.tornadoCaptureSeconds );
    bytes += VectorCapacityBytes( snapshot.tornadoEjectCooldownSeconds );
    bytes += VectorCapacityBytes( snapshot.collisionVisualContacts );
    bytes += VectorCapacityBytes( snapshot.sleepIslandVisualId );
    bytes += VectorCapacityBytes( snapshot.sleepIslandAssignedVisualId );
    bytes += VectorCapacityBytes( snapshot.sleepSupportEdges );
    bytes += VectorCapacityBytes( snapshot.sleepIslandParent );
    bytes += VectorCapacityBytes( snapshot.sleepIslandRank );
    bytes += VectorCapacityBytes( snapshot.sleepIslandHasAwake );
    bytes += VectorCapacityBytes( snapshot.sleepIslandHasSupportAnchor );
    bytes += VectorCapacityBytes( snapshot.sleepIslandEligible );
    bytes += VectorCapacityBytes( snapshot.sleepIslandCanSleep );
    bytes += VectorCapacityBytes( snapshot.persistentContacts );
    bytes += VectorCapacityBytes( snapshot.persistentContactCache );
    bytes += VectorCapacityBytes( snapshot.persistentContactCounts );
    bytes += VectorCapacityBytes( snapshot.persistentRestingContactCounts );
    bytes += VectorCapacityBytes( snapshot.debugContacts );
    bytes += VectorCapacityBytes( snapshot.pipelineTrace );
    bytes += VectorCapacityBytes( snapshot.collisionCellKeys );
    return bytes;
}

uint64_t PresentationSampleMemoryBytes( const ReplayPresentationSample& sample )
{
    return VectorCapacityBytes( sample.bodies );
}

void AddPredictionFrameCategoryBytes( MainMemoryReplayCategoryBytes& categories, const RunReplayPredictionFrame& frame )
{
    MainMemoryAddReplayCategoryBytes( categories,
                                      MainMemoryReplayByteCategory::PredictionFrameBodies,
                                      VectorCapacityBytes( frame.bodies ) );
    MainMemoryAddReplayCategoryBytes( categories,
                                      MainMemoryReplayByteCategory::PredictionDebugContacts,
                                      VectorCapacityBytes( frame.debugContacts ) );
}

uint64_t PredictionEngineMemoryBytes( const PhysicsEngine& engine )
{
    // Why: sizeof(m_prediction) only counts the unique_ptr. The private
    // prediction engine owns physics stores and solver scratch that must remain
    // visible in the replay memory overlay.
    uint64_t bytes = static_cast<uint64_t>( sizeof( engine ) );
    bytes += engine.CollectPhysicsWorldMemoryBytes();
    bytes += engine.CollectDebugAndBroadphaseMemoryBytes();
    bytes += static_cast<uint64_t>( engine.BodyStore().Records().capacity() ) * sizeof( PhysicsBodyRecord );
    bytes += static_cast<uint64_t>( engine.Colliders().Records().capacity() ) * sizeof( ColliderRecord );
    return bytes;
}

bool ReplayRuntimeModelIsRagdollPart(
    const std::vector<Rendering::RenderInstancePresentationRecord>& presentationRecords,
    int modelIndex )
{
    // SimpleRagdoll children share replay visuals with their collection root.
    // This helper keeps that policy local to replay loading/restoration paths.
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( presentationRecords.size() ) )
    {
        return false;
    }
    return presentationRecords[static_cast<std::size_t>( modelIndex )].simpleRagdollPart;
}


const PhysicsBodyRecord* ReplayRuntimeResolveReplayBody( const PhysicsBodyStore& bodyStore,
                                                         ReplayBodyId id,
                                                         int modelIndexHint,
                                                         int modelCount,
                                                         int& outModelIndex )
{
    outModelIndex = -1;
    if ( id.value == 0 )
    {
        return nullptr;
    }

    // Invariant: replay artifacts carry model indices only as staleable hints.
    // Stable identity is the replay id resolved through the live body handle map.
    const PhysicsBodyHandle body = bodyStore.HandleForReplayBodyId( id.value, modelIndexHint );
    const int modelIndex = bodyStore.ModelIndexForHandle( body );
    const PhysicsBodyRecord* record = bodyStore.RecordForHandle( body );
    if ( !record || record->replayBodyId != id.value || modelIndex < 0 || modelIndex >= modelCount )
    {
        return nullptr;
    }

    outModelIndex = modelIndex;
    return record;
}


const PhysicsBodyRecord* ReplayRuntimeBodyRecordForModelIndex( const PhysicsBodyStore& bodyStore, int modelIndex )
{
    const PhysicsBodyHandle body = bodyStore.HandleForModelIndex( modelIndex );
    const PhysicsBodyRecord* record = bodyStore.RecordForHandle( body );
    if ( !record || bodyStore.ModelIndexForHandle( body ) != modelIndex || record->replayBodyId == 0 )
    {
        return nullptr;
    }
    return record;
}


bool ReplayRuntimeQueueRenderPoseOverride( GameObjects::GameModelCollection& collection,
                                           int modelIndex,
                                           uint32_t replayBodyId,
                                           const Vector3& position,
                                           const Math::Orientation::Quaternion& orientation )
{
    return collection.TryQueueReplayRenderPoseOverride( modelIndex, replayBodyId, position, orientation );
}


bool ReplayRuntimePrepareBodyMatchedMask( std::array<uint8_t, MAX_GAME_MODELS>& mask, int modelCount )
{
    if ( modelCount < 0 || modelCount > MAX_GAME_MODELS )
    {
        return false;
    }

    std::fill( mask.begin(), mask.begin() + modelCount, uint8_t{ 0 } );
    return true;
}


// Concept: replay body lookup is sample-shaped, not subsystem-shaped.
//
// Solver samples and prediction frames both expose body rows keyed by
// ReplayBodyId, while modelIndex remains only a fast hint into each row array.
// Invariant: wrappers keep the old negative-modelIndex behavior for callers
// that still distinguish solver samples from prediction samples.
template <typename FrameSample, typename BodySample>
const BodySample* FindReplayBodyByIdInSample( const FrameSample& sample, ReplayBodyId id );

template <typename FrameSample, typename BodySample, bool AllowNegativeModelIndex>
const BodySample* FindReplayBodyByModelIndexInSample( const FrameSample& sample, int modelIndex );

template <typename FrameSample, typename BodySample, bool AllowNegativeModelIndex>
ReplayBodyId ReplayBodyIdForModelIndexInSample( const FrameSample& sample, int modelIndex );

const ReplaySolverBodySample* FindReplayBodyById( const ReplaySolverFrameSample& sample, ReplayBodyId id )
{
    return FindReplayBodyByIdInSample<ReplaySolverFrameSample, ReplaySolverBodySample>( sample, id );
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

const RunReplayPredictionBodySample* FindReplayPredictionBodyById( const RunReplayPredictionFrame& frame,
                                                                   ReplayBodyId id )
{
    return FindReplayBodyByIdInSample<RunReplayPredictionFrame, RunReplayPredictionBodySample>( frame, id );
}

const RunReplayPredictionBodySample* FindReplayPredictionBodyByModelIndex( const RunReplayPredictionFrame& frame,
                                                                           int modelIndex )
{
    return FindReplayBodyByModelIndexInSample<RunReplayPredictionFrame, RunReplayPredictionBodySample, true>(
        frame,
        modelIndex );
}

const RunReplayPredictionBodySample*
FindReplayPredictionBodyByIdWithHint( const RunReplayPredictionFrame& frame, ReplayBodyId id, int modelIndex )
{
    if ( const RunReplayPredictionBodySample* hinted = FindReplayPredictionBodyByModelIndex( frame, modelIndex ) )
    {
        if ( hinted->id.value == id.value )
        {
            return hinted;
        }
    }
    return FindReplayPredictionBodyById( frame, id );
}

// Concept: cause-tree focus needs a display radius, not exact shape math.
// Replay samples carry model indices, while live fallback can use body-handle
// pairing to stay off GameModel pose and shape mirrors.
float ReplayRuntimeColliderRadius( const ColliderRecord& collider )
{
    return (std::max)( collider.boundingRadius, 1.0f );
}

float ReplayRuntimeColliderRadiusForModelIndex( const ColliderStore& colliderStore, int modelIndex )
{
    // Why: prediction and scrub samples can outlive the exact live body handle.
    // Treat modelIndex as a replay-sample hint for a display radius only; live
    // body-backed callers use ReplayRuntimeColliderRadiusForBody above.
    const Physics::PhysicsColliderHandle colliderHandle = colliderStore.HandleForModelIndex( modelIndex );
    if ( const ColliderRecord* collider = colliderStore.RecordForHandle( colliderHandle ) )
    {
        return ReplayRuntimeColliderRadius( *collider );
    }

    const auto& colliders = colliderStore.Records();
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( colliders.size() ) )
    {
        return 1.0f;
    }
    return ReplayRuntimeColliderRadius( colliders[static_cast<std::size_t>( modelIndex )] );
}

float ReplayRuntimeColliderRadiusForBody( const ColliderStore& colliderStore,
                                          const PhysicsBodyRecord& body,
                                          int fallbackModelIndex )
{
    const Physics::PhysicsColliderHandle colliderHandle = colliderStore.HandleForBodyHandle( body.handle );
    if ( const ColliderRecord* collider = colliderStore.RecordForHandle( colliderHandle ) )
    {
        return ReplayRuntimeColliderRadius( *collider );
    }
    return ReplayRuntimeColliderRadiusForModelIndex( colliderStore, fallbackModelIndex );
}

ReplayBodyId ReplayBodyIdForModelIndex( const ReplaySolverFrameSample& sample, int modelIndex )
{
    return ReplayBodyIdForModelIndexInSample<ReplaySolverFrameSample, ReplaySolverBodySample, false>( sample,
                                                                                                      modelIndex );
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

const ReplaySolverBodySample* FindReplayBodyByModelIndex( const ReplaySolverFrameSample& sample, int modelIndex )
{
    return FindReplayBodyByModelIndexInSample<ReplaySolverFrameSample, ReplaySolverBodySample, true>( sample,
                                                                                                      modelIndex );
}

const ReplaySolverBodySample*
FindReplayBodyByIdWithHint( const ReplaySolverFrameSample& sample, ReplayBodyId id, int modelIndex )
{
    if ( const ReplaySolverBodySample* hinted = FindReplayBodyByModelIndex( sample, modelIndex ) )
    {
        if ( hinted->id.value == id.value )
        {
            return hinted;
        }
    }
    return FindReplayBodyById( sample, id );
}

ReplayFrameIndex ReplayOldestFrameFromStats( const ReplayRecorderStats& stats )
{
    return stats.nextFrameIndex > static_cast<ReplayFrameIndex>( stats.sampleCount )
               ? stats.nextFrameIndex - static_cast<ReplayFrameIndex>( stats.sampleCount )
               : 0;
}

// Concept: the past-root trajectory mirrors the solver recorder window. Rebuild
// handles target changes and ring eviction; capture-time append handles the
// ordinary newest-sample case without re-walking retained history.
ReplayTrajectoryRecordKey ReplayPastRootTrajectoryKey( ReplayBodyId targetId )
{
    ReplayTrajectoryRecordKey key;
    key.bodyId = targetId;
    key.lane = ReplayTrajectoryLane::PastRoot;
    key.branchOrdinal = 0;
    return key;
}

// Concept: committed prediction roots use trajectory branch 0.
//
// RunReplayTools writes in-progress worker output to branch 1 so render can draw
// a published prefix without replacing the old future. Promotion and completion
// republish the accepted root into branch 0, which is the frozen preview branch.
ReplayTrajectoryRecordKey ReplayPredictionCommittedRootTrajectoryKey( ReplayBodyId targetId )
{
    ReplayTrajectoryRecordKey key;
    key.bodyId = targetId;
    key.lane = ReplayTrajectoryLane::FutureRoot;
    key.branchOrdinal = 0;
    return key;
}

ReplayTrajectoryRecord* BeginReplayPredictionCommittedRootTrajectoryRecord( ReplayTrajectoryStore& store,
                                                                            ReplayBodyId targetId,
                                                                            std::size_t pointCapacity )
{
    const ReplayTrajectoryRecordKey key = ReplayPredictionCommittedRootTrajectoryKey( targetId );
    if ( !store.FindRecord( key ) && !store.ReserveRecords( store.RecordCount() + 1u, 0 ) )
    {
        return nullptr;
    }

    ReplayTrajectoryRecord* record = store.BeginReplaceRecord( key, 0, ReplayBodyId{}, 0, 0, false );
    if ( !record || !store.ReserveRecordPoints( *record, pointCapacity, 0 ) )
    {
        return nullptr;
    }
    return record;
}

bool RebuildReplayRuntimePredictionCommittedRootTrajectory( RunReplayPredictionState& prediction )
{
    if ( prediction.simulation.targetId.value == 0 || prediction.simulation.frames.size() < 2u )
    {
        return true;
    }

    ReplayTrajectoryRecord* record =
        BeginReplayPredictionCommittedRootTrajectoryRecord( prediction.trajectoryStore,
                                                            prediction.simulation.targetId,
                                                            prediction.simulation.frames.size() );
    if ( !record )
    {
        prediction.trajectoryBuild.valid = false;
        return false;
    }

    for ( const RunReplayPredictionFrame& frame : prediction.simulation.frames )
    {
        const RunReplayPredictionBodySample* body =
            FindReplayPredictionBodyByIdWithHint( frame,
                                                  prediction.simulation.targetId,
                                                  prediction.simulation.targetModelIndex );
        if ( body && !prediction.trajectoryStore.TryAppendPoint( *record, { frame.frameIndex, body->position } ) )
        {
            prediction.trajectoryBuild.valid = false;
            return false;
        }
    }

    prediction.trajectoryStore.PublishPrefix( *record, record->points.size() );
    prediction.trajectoryBuild.rootId = prediction.simulation.targetId;
    prediction.trajectoryBuild.usingBuildFrames = false;
    prediction.trajectoryBuild.rootFrameCount = record->points.size();
    prediction.trajectoryBuild.childFrameCount = 0;
    prediction.trajectoryBuild.builtNodeCount = 0;
    prediction.trajectoryBuild.topologyVersion = 0;
    prediction.trajectoryBuild.valid = true;
    return true;
}

ReplayTrajectoryRecord* BeginReplayPastRootTrajectoryRecord( ReplayTrajectoryStore& store,
                                                             ReplayBodyId targetId,
                                                             std::size_t pointCapacity,
                                                             int frameNumber )
{
    const ReplayTrajectoryRecordKey key = ReplayPastRootTrajectoryKey( targetId );
    if ( !store.FindRecord( key ) && !store.ReserveRecords( store.RecordCount() + 1u, frameNumber ) )
    {
        return nullptr;
    }

    ReplayTrajectoryRecord* record =
        store.BeginReplaceRecord( key, 0, ReplayBodyId{}, 0, static_cast<ReplayFrameIndex>( frameNumber ), false );
    if ( !record || !store.ReserveRecordPoints( *record, pointCapacity, frameNumber ) )
    {
        return nullptr;
    }
    return record;
}

bool AppendReplayPastRootTrajectoryPoint( ReplayTrajectoryStore& store,
                                          ReplayTrajectoryRecord& record,
                                          const ReplaySolverFrameSample& sample,
                                          const ReplaySolverBodySample& body )
{
    if ( !store.TryAppendPoint( record, { sample.frameIndex, body.position } ) )
    {
        return false;
    }
    store.PublishPrefix( record, record.points.size() );
    return true;
}

struct ReplayPastRootRebuildContext
{
    ReplayTrajectoryStore* store = nullptr;
    ReplayTrajectoryRecord* record = nullptr;
    ReplayBodyId targetId;
    int targetModelIndex = -1;
    ReplayFrameIndex firstFrame = 0;
    ReplayFrameIndex builtThroughFrame = 0;
    bool hasSample = false;
    bool ok = true;
};

void RebuildReplayPastRootTrajectorySample( const ReplaySolverFrameSample& sample, void* userData )
{
    ReplayPastRootRebuildContext* context = static_cast<ReplayPastRootRebuildContext*>( userData );
    if ( !context || !context->ok || !context->store || !context->record )
    {
        return;
    }

    const ReplaySolverBodySample* body =
        FindReplayBodyByIdWithHint( sample, context->targetId, context->targetModelIndex );
    if ( !body )
    {
        return;
    }

    context->ok = AppendReplayPastRootTrajectoryPoint( *context->store, *context->record, sample, *body );
    if ( context->ok )
    {
        if ( !context->hasSample )
        {
            context->firstFrame = sample.frameIndex;
            context->hasSample = true;
        }
        context->builtThroughFrame = sample.frameIndex;
        context->targetModelIndex = body->modelIndex;
    }
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
        if ( body.modelIndex == modelIndex )
        {
            return &body;
        }
    }

    for ( const BodySample& body : sample.bodies )
    {
        if ( body.modelIndex == modelIndex )
        {
            return &body;
        }
    }
    return nullptr;
}

template <typename FrameSample, typename BodySample, bool AllowNegativeModelIndex>
ReplayBodyId ReplayBodyIdForModelIndexInSample( const FrameSample& sample, int modelIndex )
{
    if ( const BodySample* body =
             FindReplayBodyByModelIndexInSample<FrameSample, BodySample, AllowNegativeModelIndex>( sample,
                                                                                                   modelIndex ) )
    {
        return body->id;
    }
    return ReplayBodyId{};
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

Vector3 ReplayContactImpulseForModel( const ReplaySolverPersistentContactSample& contact, int modelIndex )
{
    const Vector3 rowImpulse =
        contact.normal * contact.accN + contact.tangent1 * contact.accT1 + contact.tangent2 * contact.accT2;
    if ( contact.bodyB == modelIndex && !contact.isTerrain )
    {
        return rowImpulse;
    }
    return rowImpulse * -1.0f;
}

int ReplayFindPipelineIndexForContact( const ReplaySolverWorldSnapshot& snapshot,
                                       const ReplaySolverPersistentContactSample& contact )
{
    for ( int i = 0; i < static_cast<int>( snapshot.pipelineTrace.size() ); ++i )
    {
        const PhysicsPipelineRecord& record = snapshot.pipelineTrace[static_cast<std::size_t>( i )];
        if ( record.featureId == contact.featureId &&
             ( ( record.bodyA == contact.bodyA && record.bodyB == contact.bodyB ) ||
               ( record.bodyA == contact.bodyB && record.bodyB == contact.bodyA ) ) )
        {
            return i;
        }
    }
    return -1;
}

std::string SolverReplayHashLogPath( const std::string& presentationPath )
{
    // Keep solver hash logs beside presentation logs so capture artifacts can
    // be copied or deleted as a pair.
    if ( presentationPath.empty() )
    {
        return {};
    }

    const std::size_t slash = presentationPath.find_last_of( "/\\" );
    const std::size_t dot = presentationPath.find_last_of( '.' );
    if ( dot != std::string::npos && ( slash == std::string::npos || dot > slash ) )
    {
        return presentationPath.substr( 0, dot ) + ".solver" + presentationPath.substr( dot );
    }
    return presentationPath + ".solver";
}

void WaitForReplayPredictionWorkerIdle( RunReplayPredictionState& prediction )
{
    while ( prediction.build.workerTask && prediction.build.workerTask->IsInFlight() )
    {
        // Hazard: cancellation is a scene/branch mutation edge. The worker task
        // owns buildFrames and prediction trajectory slots until it drops
        // in-flight, so clearing those arrays before this wait would let render
        // read freed scratch.
        std::this_thread::yield();
    }
}
} // namespace


RunReplayPredictionState::RunReplayPredictionState() = default;


RunReplayPredictionState::~RunReplayPredictionState()
{
    // Hazard: WorkerPool tasks capture this replay state by reference. Destruct
    // only after the in-flight slice has dropped ownership of build scratch.
    WaitForReplayPredictionWorkerIdle( *this );
}


ReplayRuntime::ReplayRuntime()
{
    // Runtime allocation policy: path target selection is a live replay UI
    // action, so it rotates entries within a fixed pre-gameplay vector budget.
    m_pathVisualizer.targets.reserve( REPLAY_PATH_MAX_ROOT_TARGETS );
    // Runtime allocation policy: focus masks are rewritten during replay render
    // passes, so the byte vector owns its full model-capacity storage up front.
    m_focusModelMask.reserve( MAX_GAME_MODELS );
    m_renderPoseBodyMatched.fill( uint8_t{ 0 } );
}


ReplayRecorder& ReplayRuntime::Presentation()
{
    return m_presentation;
}

const ReplayRecorder& ReplayRuntime::Presentation() const
{
    return m_presentation;
}

ReplaySolverRecorder& ReplayRuntime::Solver()
{
    return m_solver;
}

const ReplaySolverRecorder& ReplayRuntime::Solver() const
{
    return m_solver;
}

ReplayEventRecorder& ReplayRuntime::Events()
{
    return m_events;
}

const ReplayEventRecorder& ReplayRuntime::Events() const
{
    return m_events;
}

ReplayBranchInfo& ReplayRuntime::Branch()
{
    return m_branch;
}

const ReplayBranchInfo& ReplayRuntime::Branch() const
{
    return m_branch;
}

RunLoadedReplayPresentationState& ReplayRuntime::LoadedPresentation()
{
    return m_loadedPresentation;
}

const RunLoadedReplayPresentationState& ReplayRuntime::LoadedPresentation() const
{
    return m_loadedPresentation;
}

RunReplayScrubberState& ReplayRuntime::Scrubber()
{
    return m_scrubber;
}

const RunReplayScrubberState& ReplayRuntime::Scrubber() const
{
    return m_scrubber;
}

RunReplayCameraState& ReplayRuntime::Camera()
{
    return m_camera;
}

const RunReplayCameraState& ReplayRuntime::Camera() const
{
    return m_camera;
}

RunReplayPathVisualizerState& ReplayRuntime::PathVisualizer()
{
    return m_pathVisualizer;
}

const RunReplayPathVisualizerState& ReplayRuntime::PathVisualizer() const
{
    return m_pathVisualizer;
}

RunReplayPredictionState& ReplayRuntime::Prediction()
{
    return m_prediction;
}

const RunReplayPredictionState& ReplayRuntime::Prediction() const
{
    return m_prediction;
}

const std::vector<RunReplayPredictionFrame>& ReplayRuntime::ActivePredictionFrames() const
{
    return ReplayRuntimeActivePredictionFrames( m_prediction );
}

void ReplayRuntime::ClearPredictionFutureNodeCache()
{
    m_prediction.futureNodeCache.futureNodes.clear();
    m_prediction.futureNodeCache.futureNodeBuildScratch.clear();
    m_prediction.futureNodeCache.futureNodesBuiltFrameCount = 0;
    m_prediction.futureNodeCache.futureNodesBuiltContactIndex = 0;
    m_prediction.futureNodeCache.futureNodesBuiltTargetId = ReplayBodyId{};
    m_prediction.futureNodeCache.futureNodesBuiltRagdollVisuals = m_prediction.ragdollVisualsEnabled;
    m_prediction.futureNodeCache.futureNodesBuiltFromBuildFrames = false;
    m_prediction.futureNodeCache.futureNodesCacheValid = false;
    m_prediction.futureNodeCache.retainedMarkerCount = 0;
    m_prediction.trajectoryBuild.childFrameCount = 0;
    m_prediction.trajectoryBuild.builtNodeCount = 0;
}

void ReplayRuntime::WaitForPredictionJobIdle()
{
    WaitForReplayPredictionWorkerIdle( m_prediction );
}

bool ReplayRuntime::PromotePredictionBuildPrefixToCommitted()
{
    if ( !m_prediction.BuildPrefixShouldBePresented() )
    {
        return false;
    }

    WaitForReplayPredictionWorkerIdle( m_prediction );
    const std::size_t promotedFrameCount = m_prediction.PublishedBuildFrameCount();
    if ( promotedFrameCount < 2u || promotedFrameCount > m_prediction.build.buildFrames.size() )
    {
        return false;
    }

    // Hazard: this is the Play-button ownership transfer. The worker has
    // released buildFrames, so the frame loop can turn the visible prefix into
    // committed replay samples before clearing private prediction scratch.
    m_prediction.build.workerTask.reset();
    m_prediction.build.building = false;
    m_prediction.build.complete = true;
    m_prediction.simulation.frames.swap( m_prediction.build.buildFrames );
    m_prediction.simulation.frames.resize( promotedFrameCount );
    m_prediction.build.buildFrames.clear();
    m_prediction.ResetBuildFramePublication();
    if ( !RebuildReplayRuntimePredictionCommittedRootTrajectory( m_prediction ) )
    {
        return false;
    }
    m_prediction.simulation.predictionEngineReady = false;
    m_prediction.simulation.predictionBodies.clear();
    m_prediction.simulation.predictionWorld = ReplaySolverWorldSnapshot();
    return true;
}

void ReplayRuntime::CancelPredictionJob( bool clearSamples )
{
    WaitForReplayPredictionWorkerIdle( m_prediction );
    m_prediction.build.workerTask.reset();
    m_prediction.build.building = false;
    m_prediction.build.complete = false;
    m_prediction.simulation.targetModelIndex = -1;
    m_prediction.build.nextTick = 1;
    m_prediction.build.targetTickCount = 0;
    m_prediction.simulation.predictionEngineReady = false;
    m_prediction.simulation.predictionBodies.clear();
    m_prediction.simulation.predictionWorld = ReplaySolverWorldSnapshot();
    m_prediction.build.buildFrames.clear();
    m_prediction.ResetBuildFramePublication();
    m_prediction.trajectoryBuild = RunReplayPredictionTrajectoryBuildState{};
    if ( clearSamples )
    {
        m_prediction.simulation.frames.clear();
        m_prediction.trajectoryStore.Clear();
        ClearPredictionFutureNodeCache();
    }
}

void ReplayRuntime::ClearPredictionCache()
{
    CancelPredictionJob( true );
    m_prediction.simulation.targetId = ReplayBodyId{};
    m_prediction.simulation.sourceFrameIndex = 0;
    m_prediction.simulation.sourceSolverHash = 0;
    m_prediction.simulation.sourceSimulationSeconds = 0.0;
    m_prediction.build.lastBuildTime = 0.0;
    m_prediction.trajectoryBuild = RunReplayPredictionTrajectoryBuildState{};
    m_prediction.trajectoryStore.Clear();
    m_prediction.baseline = ReplayPredictionBaselineSnapshot{};
}

void ReplayRuntime::MarkPredictionDirty()
{
    CancelPredictionJob( false );
    m_prediction.build.dirty = true;
}

void ReplayRuntime::ClearPathVisualizerState()
{
    m_pathVisualizer.hasTarget = false;
    m_pathVisualizer.targetId = ReplayBodyId{};
    m_pathVisualizer.targetModelIndex = -1;
    m_pathVisualizer.targetName[0] = '\0';
    m_pathVisualizer.pastPathHovered = false;
    m_pathVisualizer.futureNodes.clear();
    m_pathVisualizer.targets.clear();
    m_pathVisualizer.pastTrajectory = RunReplayPastTrajectoryBuildState{};
    m_causeTree.rows.clear();
    m_causeTree.hoveredRow = -1;
    m_causeTree.selectedRow = -1;
    m_causeTree.scrollY = 0.0f;
    ClearPredictionCache();
    MarkPredictionDirty();
}

RunReplayCauseTreeState& ReplayRuntime::CauseTree()
{
    return m_causeTree;
}

const RunReplayCauseTreeState& ReplayRuntime::CauseTree() const
{
    return m_causeTree;
}

RunReplayVelocityEditState& ReplayRuntime::VelocityEdit()
{
    return m_velocityEdit;
}

const RunReplayVelocityEditState& ReplayRuntime::VelocityEdit() const
{
    return m_velocityEdit;
}

bool ReplayRuntime::SetVelocityEditEnabled( bool enabled )
{
    if ( m_velocityEdit.enabled == enabled )
    {
        return false;
    }

    PROFILE_SCOPED( "Frame/Replay/VelocityEdit/Toggle" );
    m_velocityEdit.enabled = enabled;
    m_velocityEdit.hotLinearAxis = -1;
    m_velocityEdit.hotAngularAxis = -1;
    m_velocityEdit.activeAxis = -1;

    if ( enabled )
    {
        m_prediction.enabled = true;
        m_prediction.simulation.horizonSeconds = std::clamp( m_prediction.simulation.horizonSeconds,
                                                             ReplayOverlay::REPLAY_PREDICTION_MIN_SECONDS,
                                                             ReplayOverlay::REPLAY_PREDICTION_MAX_SECONDS );
        MarkPredictionDirty();
    }

    return true;
}

void ReplayRuntime::SetVelocityEditAltKeyDown( bool isDown )
{
    m_velocityEdit.keyboardAltWasDown = isDown;
}

float ReplayRuntime::TrackPosition( RunReplayTrack track ) const
{
    return track == RunReplayTrack::Solver ? m_scrubber.solverPosition : m_scrubber.presentationPosition;
}

void ReplayRuntime::SetTrackPosition( RunReplayTrack track, float position )
{
    const float clamped = std::clamp( position, 0.0f, 1.0f );
    if ( track == RunReplayTrack::Solver )
    {
        m_scrubber.solverPosition = clamped;
    }
    else
    {
        m_scrubber.presentationPosition = clamped;
    }

    if ( m_scrubber.activeTrack == track )
    {
        m_scrubber.position = clamped;
    }
}

void ReplayRuntime::SyncActiveTrackPosition()
{
    m_scrubber.position = TrackPosition( m_scrubber.activeTrack );
}

void ReplayRuntime::SetAllTrackPositions( float position )
{
    const float clamped = std::clamp( position, 0.0f, 1.0f );
    m_scrubber.presentationPosition = clamped;
    m_scrubber.solverPosition = clamped;
    m_scrubber.position = clamped;
}

bool ReplayRuntime::ResetScrubberState()
{
    const bool shouldExitInspectionCamera = m_camera.active && !m_scrubber.liveAdvanceHeld;
    const bool leftWasDown = m_scrubber.leftWasDown;
    const bool restoreWasDown = m_scrubber.restoreWasDown;
    const bool restoreConsumedThisFrame = m_scrubber.restoreConsumedThisFrame;
    const bool liveAdvanceHeld = m_scrubber.liveAdvanceHeld;
    const bool pauseRestoreFlyMode = m_scrubber.pauseRestoreFlyMode;
    const bool pauseRestoreLauncherMode = m_scrubber.pauseRestoreLauncherMode;
    m_scrubber = RunReplayScrubberState{};
    m_scrubber.leftWasDown = leftWasDown;
    m_scrubber.restoreWasDown = restoreWasDown;
    m_scrubber.restoreConsumedThisFrame = restoreConsumedThisFrame;
    m_scrubber.liveAdvanceHeld = liveAdvanceHeld;
    m_scrubber.pauseRestoreFlyMode = pauseRestoreFlyMode;
    m_scrubber.pauseRestoreLauncherMode = pauseRestoreLauncherMode;
    return shouldExitInspectionCamera;
}


ReplayRuntime::ScrubberInputFrame ReplayRuntime::BeginScrubberInputFrame( bool leftDown, bool restoreDown )
{
    ScrubberInputFrame frame;
    m_scrubber.restoreConsumedThisFrame = false;
    frame.leftPressed = leftDown && !m_scrubber.leftWasDown;
    frame.leftReleased = !leftDown && m_scrubber.leftWasDown;
    m_scrubber.leftWasDown = leftDown;
    frame.restorePressed = restoreDown && !m_scrubber.restoreWasDown;
    m_scrubber.restoreWasDown = restoreDown;
    return frame;
}


ReplayRuntime::ScrubberUnavailableResult ReplayRuntime::ResetUnavailableScrubberSurface( bool loadedPresentation,
                                                                                         bool leftDown )
{
    ScrubberUnavailableResult result;
    if ( !loadedPresentation )
    {
        result.exitInspectionCamera = ResetScrubberState();
    }
    m_prediction.ui.checkboxHovered = false;
    m_prediction.ui.ragdollVisualsHovered = false;
    m_prediction.ui.decreaseHovered = false;
    m_prediction.ui.increaseHovered = false;
    m_prediction.ui.horizonHovered = false;
    m_prediction.ui.horizonDragging = false;
    m_pathVisualizer.pastPathHovered = false;
    m_velocityEdit.toggleHovered = false;
    m_scrubber.branchHovered = false;
    m_scrubber.loadHovered = false;
    m_scrubber.leftWasDown = leftDown;
    m_scrubber.fadeUpdatedAt = 0.0;
    m_scrubber.visibleAlpha = 0.0f;
    return result;
}


ReplayRuntime::PointerButtonEdges ReplayRuntime::BeginCauseTreeInputFrame( bool leftDown )
{
    PointerButtonEdges edges;
    edges.leftPressed = leftDown && !m_causeTree.leftWasDown;
    edges.leftReleased = !leftDown && m_causeTree.leftWasDown;
    m_causeTree.leftWasDown = leftDown;
    m_causeTree.hoveredRow = -1;
    return edges;
}


void ReplayRuntime::ClearCauseTreeFocusSelection()
{
    ClearCameraFocusForRestore();
    ClearPathVisualizerState();
}


bool ReplayRuntime::SetLiveAdvanceHeld( bool held )
{
    if ( m_scrubber.liveAdvanceHeld == held )
    {
        if ( !held )
        {
            m_camera.ownsSimulationPause = false;
        }
        return false;
    }

    m_scrubber.liveAdvanceHeld = held;
    if ( !held )
    {
        m_camera.ownsSimulationPause = false;
    }
    return true;
}

bool ReplayRuntime::LiveAdvanceHeld() const
{
    return m_scrubber.liveAdvanceHeld;
}

bool ReplayRuntime::HasPathVisualizerTarget() const
{
    return m_pathVisualizer.hasTarget;
}

bool ReplayRuntime::HasCameraFocus() const
{
    return m_camera.focusKind != RunReplayCameraFocusKind::None;
}

bool ReplayRuntime::VelocityEditActive() const
{
    return m_velocityEdit.enabled;
}

float ReplayRuntime::SolverPresentTrackPosition() const
{
    return ReplayRuntimeScrubberPresentTrackPosition( m_solver.GetStats(), m_prediction );
}

bool ReplayRuntime::TimelineHasFuture( float presentT )
{
    return presentT < REPLAY_RUNTIME_SCRUBBER_LIVE_THRESHOLD;
}

bool ReplayRuntime::AtPresentTrackPosition( float position, float presentT )
{
    if ( !TimelineHasFuture( presentT ) )
    {
        return position >= REPLAY_RUNTIME_SCRUBBER_LIVE_THRESHOLD;
    }
    return std::fabs( position - presentT ) <= REPLAY_RUNTIME_SCRUBBER_PRESENT_EPSILON;
}

bool ReplayRuntime::TrackPositionIsFuture( float position, float presentT )
{
    return TimelineHasFuture( presentT ) && position > presentT + REPLAY_RUNTIME_SCRUBBER_PRESENT_EPSILON;
}

float ReplayRuntime::SolverNormalizedFromTrack( float position, float presentT )
{
    if ( !TimelineHasFuture( presentT ) )
    {
        return std::clamp( position, 0.0f, 1.0f );
    }
    return std::clamp( position / (std::max)( presentT, 0.0001f ), 0.0f, 1.0f );
}

float ReplayRuntime::PredictionNormalizedFromTrack( float position, float presentT )
{
    if ( !TimelineHasFuture( presentT ) )
    {
        return 0.0f;
    }
    return std::clamp( ( position - presentT ) / ( 1.0f - presentT ), 0.0f, 1.0f );
}

bool ReplayRuntime::ShouldRenderScrubber( bool editorModeEnabled, bool uiVisible, bool uiMinimized ) const
{
    if ( editorModeEnabled || !uiVisible || !uiMinimized )
    {
        return false;
    }

    const bool loadedPresentation = HasLoadedPresentation();
    const ReplayRecorderStats solverReplayStats = m_solver.GetStats();
    const bool solverReplayEnabled = solverReplayStats.enabled;
    // Why: visibility is about whether a replay control surface is armed, not
    // whether enough retained frames exist to enable scrub/prediction tools.
    return ( loadedPresentation || solverReplayEnabled ) &&
           ( m_scrubber.visible || m_scrubber.dragging || m_scrubber.historicalSamplePaused ||
             m_scrubber.liveAdvanceHeld );
}

bool ReplayRuntime::ShouldUseInspectionCamera() const
{
    return m_scrubber.historicalSamplePaused || m_scrubber.liveAdvanceHeld ||
           m_camera.focusKind != RunReplayCameraFocusKind::None;
}

bool ReplayRuntime::InspectionActive() const
{
    return m_camera.active || m_scrubber.historicalSamplePaused || m_scrubber.liveAdvanceHeld;
}

bool ReplayRuntime::InspectionMouseLookActive( bool rightMouseDown,
                                               bool uiWantsNativeCursor,
                                               bool uiBlocksCameraMouse ) const
{
    return InspectionActive() && rightMouseDown && !uiWantsNativeCursor && !uiBlocksCameraMouse;
}

bool ReplayRuntime::ArmLoadedPresentationScrubber( float normalized, double now )
{
    if ( !HasLoadedPresentation() )
    {
        return false;
    }

    ClearPathVisualizerState();
    m_prediction.enabled = false;
    m_prediction.ui.horizonDragging = false;
    m_velocityEdit = RunReplayVelocityEditState{};
    m_scrubber.activeTrack = RunReplayTrack::Presentation;
    SetTrackPosition( RunReplayTrack::Presentation, normalized );
    m_scrubber.solverPosition = 1.0f;
    m_scrubber.dragging = false;
    m_scrubber.historicalSamplePaused = true;
    m_scrubber.mouseCaptured = false;
    m_scrubber.visible = true;
    m_scrubber.visibleUntil = now + ReplayOverlay::REPLAY_SCRUBBER_VISIBLE_SECONDS;
    return true;
}

void ReplayRuntime::ClearCameraFocusForRestore()
{
    m_camera.focusKind = RunReplayCameraFocusKind::None;
    m_camera.focusedId = ReplayBodyId{};
    m_camera.counterpartId = ReplayBodyId{};
    m_camera.focusedRow = -1;
    m_camera.focusRowKind = RunReplayCauseTreeRowKind::Body;
    m_camera.focusModelIndex = -1;
    m_camera.focusCounterpartModelIndex = -1;
    m_camera.focusContactIndex = -1;
    m_camera.focusSolverRowIndex = -1;
    m_camera.focusFeatureId = 0;
    m_camera.focusTerrain = false;
    m_camera.targetPoint = Math::Vector::ZERO_VECTOR;
    m_camera.targetNormal = Vector3( 0.0f, 1.0f, 0.0f );
    m_camera.impulseVector = Math::Vector::ZERO_VECTOR;
    m_causeTree.focusedId = ReplayBodyId{};
    m_causeTree.selectedRow = -1;

    if ( m_camera.ownsSimulationPause && m_scrubber.liveAdvanceHeld && !m_scrubber.historicalSamplePaused )
    {
        m_scrubber.liveAdvanceHeld = false;
    }
    m_camera.ownsSimulationPause = false;
}

ReplayRuntime::RecordingConfigResult ReplayRuntime::ConfigureRecording( bool enabled,
                                                                        int retentionSeconds,
                                                                        const char* hashLogPath,
                                                                        int runtimeBodyCapacity )
{
    m_recordingConfigured = true;
    m_recordingEnabled = enabled || ( hashLogPath && hashLogPath[0] != '\0' );
    m_recordingRuntimeBodyCapacity = runtimeBodyCapacity;
    m_recordingHashLogPath = hashLogPath ? hashLogPath : "";
    m_memoryPolicy.requestedRetentionSeconds = (std::max)( 1, retentionSeconds );
    m_memoryPolicy = ResolveReplayMemoryPolicy( m_memoryPolicy );

    ReplayRecorderConfig replayConfig;
    replayConfig.enabled = enabled || ( hashLogPath && hashLogPath[0] != '\0' );
    replayConfig.retentionSeconds = m_memoryPolicy.presentationRetentionSeconds;
    replayConfig.checkpointIntervalFrames = 30;
    replayConfig.runtimeBodyCapacity = runtimeBodyCapacity;
    if ( hashLogPath && hashLogPath[0] != '\0' )
    {
        replayConfig.hashLogPath = hashLogPath;
    }

    ReplayRecorderConfig solverReplayConfig = replayConfig;
    solverReplayConfig.retentionSeconds = m_memoryPolicy.solverRetentionSeconds;
    solverReplayConfig.checkpointIntervalFrames = 60;
    solverReplayConfig.hashLogPath = SolverReplayHashLogPath( replayConfig.hashLogPath );

    m_presentation.Configure( replayConfig );
    m_solver.Configure( solverReplayConfig );
    m_events.Configure( replayConfig );
    if ( replayConfig.enabled )
    {
        // Runtime allocation policy: cause rows and prediction ghost requests are
        // replay-only overlay buffers. Reserve them at startup replay
        // configuration, not in the always-constructed runtime, so non-replay perf
        // scenes do not carry 49 MB of dormant visualization capacity.
        m_causeTree.rows.reserve( REPLAY_CAUSE_TREE_ROW_CAPACITY );
        m_predictionGhostDrawRequests.reserve( REPLAY_PREDICTION_GHOST_REQUEST_CAPACITY );
    }

    RecordingConfigResult result;
    result.presentationConfig = replayConfig;
    result.solverConfig = solverReplayConfig;
    result.presentationStats = m_presentation.GetStats();
    result.solverStats = m_solver.GetStats();
    result.eventStats = m_events.GetStats();
    return result;
}

bool ReplayRuntime::ApplyMemoryPolicyRequest( const ReplayMemoryPolicyRequest& request )
{
    ReplayMemoryPolicy nextPolicy = m_memoryPolicy;
    if ( request.presetIndex >= 0 )
    {
        nextPolicy = ReplayMemoryPresetPolicy( ReplayMemoryPresetFromIndex( request.presetIndex ) );
    }
    if ( request.retentionSeconds > 0 )
    {
        nextPolicy.requestedRetentionSeconds = request.retentionSeconds;
    }
    if ( request.budgetMiB > 0 )
    {
        nextPolicy.requestedBudgetMiB = request.budgetMiB;
    }
    nextPolicy = ResolveReplayMemoryPolicy( nextPolicy );
    if ( nextPolicy.preset == m_memoryPolicy.preset &&
         nextPolicy.requestedRetentionSeconds == m_memoryPolicy.requestedRetentionSeconds &&
         nextPolicy.requestedBudgetMiB == m_memoryPolicy.requestedBudgetMiB &&
         nextPolicy.presentationRetentionSeconds == m_memoryPolicy.presentationRetentionSeconds &&
         nextPolicy.solverRetentionSeconds == m_memoryPolicy.solverRetentionSeconds )
    {
        return false;
    }

    m_memoryPolicy = nextPolicy;
    if ( !m_recordingConfigured )
    {
        return true;
    }

    // Hazard: changing retention policy invalidates normalized scrub positions
    // and retained samples, so replay resets the recorders and snaps tracks back
    // to the live edge in one owner-controlled step.
    ConfigureRecording( m_recordingEnabled,
                        m_memoryPolicy.requestedRetentionSeconds,
                        m_recordingHashLogPath.empty() ? nullptr : m_recordingHashLogPath.c_str(),
                        m_recordingRuntimeBodyCapacity );
    ResetScrubberState();
    SetAllTrackPositions( 1.0f );
    return true;
}

const ReplayMemoryPolicy& ReplayRuntime::MemoryPolicy() const
{
    return m_memoryPolicy;
}

void ReplayRuntime::FlushHashLogs()
{
    m_presentation.FlushHashLog();
    m_solver.FlushHashLog();
}

void ReplayRuntime::ResetBranch()
{
    m_branch = ReplayBranchInfo();
}

void ReplayRuntime::ResetTimeline( const char* sceneLabel )
{
    m_presentation.ResetTimeline( sceneLabel );
    m_solver.ResetTimeline( sceneLabel );
    m_events.ResetTimeline( sceneLabel );
}


ReplayRuntime::SceneTimelineResetResult ReplayRuntime::BeginSceneTimelineReset( const SceneTimelineResetInput& input )
{
    SceneTimelineResetResult result;
    // Hazard: scene reset can rebuild live model, body, and collider storage.
    // Prediction workers hold only replay-owned values, but cancellation still
    // waits here before old private-engine snapshots are cleared or replaced.
    CancelPredictionJob( true );
    if ( !input.preserveBranchMetadata )
    {
        ResetBranch();
    }
    if ( m_scrubber.liveAdvanceHeld )
    {
        SetLiveAdvanceHeld( false );
    }
    if ( ResetScrubberState() )
    {
        result.exitInspectionCamera = true;
    }
    return result;
}


ReplayRuntime::SceneTimelineResetResult ReplayRuntime::FinishSceneTimelineReset( const SceneTimelineResetInput& input )
{
    SceneTimelineResetResult result;
    m_loadedPresentation = RunLoadedReplayPresentationState{};
    ClearCameraFocusForRestore();
    result.exitInspectionCamera = true;
    ClearPathVisualizerState();
    m_velocityEdit = RunReplayVelocityEditState{};
    if ( !IsPresentationEnabled() )
    {
        return result;
    }

    const char* sceneLabel = input.sceneLabel && input.sceneLabel[0] != '\0' ? input.sceneLabel : "generated";
    ResetTimeline( sceneLabel );
    RecordEvent( ReplayEventKind::TimelineStart, 0, 0, 0, 0, 0, 0, 0, sceneLabel );
    result.timelineStarted = true;
    // Why: mismatch diagnostics are scoped to the active replay timeline so a
    // noisy prior scene does not suppress the first useful report in this scene.
    m_captureMismatchReports = 0;
    m_captureMismatchSuppressed = false;

    if ( !( input.isSceneMode && input.solverBallCount <= 0 && input.solverBoxCount <= 0 ) )
    {
        uint32_t flags = 0;
        flags |=
            ( input.solverBallCount > 0 || input.solverBoxCount > 0 ) ? REPLAY_GENERATED_SCENE_EXACT_SOLVER_COUNTS : 0u;
        flags |= input.hasUiModelCountOverride ? REPLAY_GENERATED_SCENE_UI_MODEL_COUNT : 0u;
        flags |= input.hasUiSolverCountOverride ? REPLAY_GENERATED_SCENE_UI_SOLVER_COUNTS : 0u;
        flags |= ( input.generatedObjectTypeOverride << REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT ) &
                 REPLAY_GENERATED_SCENE_OVERRIDE_MASK;

        uint64_t hash = REPLAY_EVENT_FNV_OFFSET;
        ReplayRuntimeHashInt( hash, input.modelCount );
        ReplayRuntimeHashInt( hash, input.solverBallCount );
        ReplayRuntimeHashInt( hash, input.solverBoxCount );
        ReplayRuntimeHashInt( hash, static_cast<int32_t>( input.rngSeed ) );
        ReplayRuntimeHashInt( hash, input.gameModelCapacity );
        ReplayRuntimeHashInt( hash, static_cast<int32_t>( input.generatedObjectTypeOverride ) );

        RecordEvent( ReplayEventKind::GeneratedSceneConfig,
                     0,
                     flags,
                     input.modelCount,
                     input.solverBallCount,
                     input.solverBoxCount,
                     static_cast<int32_t>( input.rngSeed ),
                     hash,
                     "generated_scene_config" );
    }
    return result;
}


bool ReplayRuntime::IsPresentationEnabled() const
{
    return m_presentation.IsEnabled();
}

bool ReplayRuntime::IsCaptureEnabled() const
{
    return m_presentation.IsEnabled() || m_solver.IsEnabled();
}

ReplayRecorderStats ReplayRuntime::PresentationStats() const
{
    return m_presentation.GetStats();
}

ReplayRecorderStats ReplayRuntime::SolverStats() const
{
    return m_solver.GetStats();
}

ReplayEventRecorderStats ReplayRuntime::EventStats() const
{
    return m_events.GetStats();
}

ReplayFrameIndex ReplayRuntime::NextEventFrameIndex() const
{
    const ReplayRecorderStats solverStats = m_solver.GetStats();
    if ( solverStats.enabled )
    {
        return solverStats.nextFrameIndex;
    }

    const ReplayRecorderStats presentationStats = m_presentation.GetStats();
    return presentationStats.nextFrameIndex;
}

void ReplayRuntime::RefreshPastTrajectoryStoreFromSolverSamples()
{
    if ( !m_pathVisualizer.hasTarget || m_pathVisualizer.targetId.value == 0 )
    {
        m_pathVisualizer.pastTrajectory = RunReplayPastTrajectoryBuildState{};
        return;
    }

    const ReplayRecorderStats stats = m_solver.GetStats();
    if ( !stats.enabled || stats.sampleCount == 0 || stats.nextFrameIndex == 0 )
    {
        m_pathVisualizer.pastTrajectory = RunReplayPastTrajectoryBuildState{};
        return;
    }

    const ReplayFrameIndex oldestFrame = ReplayOldestFrameFromStats( stats );
    const ReplayFrameIndex newestFrame = stats.nextFrameIndex - 1u;
    const bool needsRebuild = !m_pathVisualizer.pastTrajectory.valid ||
                              m_pathVisualizer.pastTrajectory.targetId.value != m_pathVisualizer.targetId.value ||
                              m_pathVisualizer.pastTrajectory.totalFramesEvicted != stats.totalFramesEvicted ||
                              m_pathVisualizer.pastTrajectory.firstFrame != oldestFrame ||
                              m_pathVisualizer.pastTrajectory.builtThroughFrame < newestFrame;
    if ( !needsRebuild )
    {
        return;
    }

    const int frameNumber = static_cast<int>( (std::min)( newestFrame, static_cast<ReplayFrameIndex>( INT_MAX ) ) );
    ReplayTrajectoryRecord* record = BeginReplayPastRootTrajectoryRecord( m_prediction.trajectoryStore,
                                                                          m_pathVisualizer.targetId,
                                                                          stats.sampleCount,
                                                                          frameNumber );
    if ( !record )
    {
        m_pathVisualizer.pastTrajectory = RunReplayPastTrajectoryBuildState{};
        return;
    }

    ReplayPastRootRebuildContext rebuild;
    rebuild.store = &m_prediction.trajectoryStore;
    rebuild.record = record;
    rebuild.targetId = m_pathVisualizer.targetId;
    rebuild.targetModelIndex = m_pathVisualizer.targetModelIndex;
    m_solver.ForEachSampleChronological( RebuildReplayPastRootTrajectorySample, &rebuild );
    if ( !rebuild.ok || !rebuild.hasSample )
    {
        m_pathVisualizer.pastTrajectory = RunReplayPastTrajectoryBuildState{};
        return;
    }

    record->firstFrame = rebuild.firstFrame;
    m_pathVisualizer.targetModelIndex = rebuild.targetModelIndex;
    m_pathVisualizer.pastTrajectory.targetId = m_pathVisualizer.targetId;
    m_pathVisualizer.pastTrajectory.firstFrame = oldestFrame;
    m_pathVisualizer.pastTrajectory.builtThroughFrame = rebuild.builtThroughFrame;
    m_pathVisualizer.pastTrajectory.totalFramesEvicted = stats.totalFramesEvicted;
    m_pathVisualizer.pastTrajectory.valid = true;
}

// Concept: capture mismatch diagnostics compare the newest paired presentation
// and solver samples after ReplayRuntime records the current frame.
//
// Why: the throttle belongs to the replay timeline owner, so RunFrame can request
// capture without carrying replay-specific diagnostic state.
void ReplayRuntime::ReportLatestCaptureMismatch()
{
    const ReplayPresentationSample* presentation = m_presentation.LatestSample();
    const ReplaySolverFrameSample* solver = m_solver.LatestSample();
    if ( !presentation || !solver )
    {
        return;
    }

    const bool matches = presentation->frameIndex == solver->frameIndex &&
                         presentation->stateHash == solver->presentationHash &&
                         presentation->bodies.size() == solver->bodies.size();
    if ( matches )
    {
        return;
    }

    if ( m_captureMismatchReports < 8 )
    {
        ++m_captureMismatchReports;
        fprintf( stderr,
                 "[replay] Solver/presentation capture mismatch #%u: presentation_frame=%llu solver_frame=%llu "
                 "presentation_hash=0x%016llX solver_presentation_hash=0x%016llX solver_hash=0x%016llX "
                 "presentation_bodies=%llu solver_bodies=%llu\n",
                 m_captureMismatchReports,
                 static_cast<unsigned long long>( presentation->frameIndex ),
                 static_cast<unsigned long long>( solver->frameIndex ),
                 static_cast<unsigned long long>( presentation->stateHash ),
                 static_cast<unsigned long long>( solver->presentationHash ),
                 static_cast<unsigned long long>( solver->solverHash ),
                 static_cast<unsigned long long>( presentation->bodies.size() ),
                 static_cast<unsigned long long>( solver->bodies.size() ) );
    }
    else if ( !m_captureMismatchSuppressed )
    {
        m_captureMismatchSuppressed = true;
        fprintf( stderr,
                 "[replay] Further solver/presentation capture mismatch diagnostics suppressed for this replay "
                 "timeline.\n" );
    }
}

void ReplayRuntime::AppendSolverTrajectorySampleToStore( const ReplaySolverFrameSample& sample )
{
    if ( !m_pathVisualizer.hasTarget || m_pathVisualizer.targetId.value == 0 ||
         !m_pathVisualizer.pastTrajectory.valid ||
         m_pathVisualizer.pastTrajectory.targetId.value != m_pathVisualizer.targetId.value )
    {
        return;
    }

    const ReplayRecorderStats stats = m_solver.GetStats();
    if ( m_pathVisualizer.pastTrajectory.totalFramesEvicted != stats.totalFramesEvicted ||
         m_pathVisualizer.pastTrajectory.firstFrame != ReplayOldestFrameFromStats( stats ) ||
         sample.frameIndex <= m_pathVisualizer.pastTrajectory.builtThroughFrame )
    {
        return;
    }

    ReplayTrajectoryRecord* record =
        m_prediction.trajectoryStore.FindRecord( ReplayPastRootTrajectoryKey( m_pathVisualizer.targetId ) );
    if ( !record )
    {
        m_pathVisualizer.pastTrajectory.valid = false;
        return;
    }

    const ReplaySolverBodySample* body =
        FindReplayBodyByIdWithHint( sample, m_pathVisualizer.targetId, m_pathVisualizer.targetModelIndex );
    if ( !body )
    {
        return;
    }

    if ( !AppendReplayPastRootTrajectoryPoint( m_prediction.trajectoryStore, *record, sample, *body ) )
    {
        m_pathVisualizer.pastTrajectory.valid = false;
        return;
    }

    m_pathVisualizer.targetModelIndex = body->modelIndex;
    m_pathVisualizer.pastTrajectory.builtThroughFrame = sample.frameIndex;
}

void ReplayRuntime::CaptureFrame( ReplayCaptureInput input )
{
    // Invariant: presentation, solver, and event timelines share the same
    // branch and event cursor for this frame. Save/export code depends on that
    // alignment when it pairs visual frames with restore checkpoints.
    input.branch = m_branch;
    input.eventCursor = m_events.GetStats().nextSequence;
    if ( m_solver.IsEnabled() )
    {
        const ReplayFrameIndex expectedSolverFrame = m_solver.GetStats().nextFrameIndex;
        m_solver.CaptureFrame( input );
        const ReplaySolverFrameSample* solverSample = m_solver.LatestSample();
        if ( solverSample && solverSample->frameIndex == expectedSolverFrame )
        {
            // Why: the solver sample contains every presentation-facing body
            // field plus the already computed presentation hash. Reusing it
            // avoids a second per-body/contact pass in the frame tick.
            m_presentation.CaptureFrameFromSolverSample( *solverSample );
            AppendSolverTrajectorySampleToStore( *solverSample );
            ReportLatestCaptureMismatch();
            return;
        }
    }

    m_presentation.CaptureFrame( input );
    ReportLatestCaptureMismatch();
}

// Concept: render replay poses are temporary render-instance overrides.
//
// Scrubbing should affect only the pixels drawn for this frame. The helpers
// below apply replay or prediction poses to a freshly prepared render-instance
// snapshot; live physics rows and authored presentation metadata are not
// mutated and therefore need no restore.
bool ReplayRuntime::ApplyPresentationSampleForRender( GameObjects::GameModelCollection& collection,
                                                      const ReplayPresentationSample& sample )
{
    const PhysicsBodyStore& bodyStore = collection.GetPhysicsEngine().BodyStore();
    const int modelCount = collection.RenderInstances().Count();
    if ( !ReplayRuntimePrepareBodyMatchedMask( m_renderPoseBodyMatched, modelCount ) )
    {
        return false;
    }
    bool queuedAny = false;

    for ( const ReplayBodyPresentationSample& body : sample.bodies )
    {
        int resolvedModelIndex = -1;
        if ( !ReplayRuntimeResolveReplayBody( bodyStore, body.id, body.modelIndex, modelCount, resolvedModelIndex ) )
        {
            continue;
        }

        Math::Orientation::Quaternion orientation( body.orientation[0],
                                                   body.orientation[1],
                                                   body.orientation[2],
                                                   body.orientation[3] );
        orientation.Normalise();
        if ( ReplayRuntimeQueueRenderPoseOverride( collection,
                                                   resolvedModelIndex,
                                                   body.id.value,
                                                   body.position,
                                                   orientation ) )
        {
            m_renderPoseBodyMatched[static_cast<std::size_t>( resolvedModelIndex )] = 1;
            queuedAny = true;
        }
    }

    const Math::Vector::Vector3 hiddenReplayPosition( 0.0f, -100000.0f, 0.0f );
    for ( int i = 0; i < modelCount; ++i )
    {
        const std::size_t bodyIndex = static_cast<std::size_t>( i );
        if ( m_renderPoseBodyMatched[bodyIndex] )
        {
            continue;
        }

        const PhysicsBodyRecord* bodyRecord = ReplayRuntimeBodyRecordForModelIndex( bodyStore, i );
        if ( !bodyRecord )
        {
            continue;
        }

        // Why: loaded artifacts may not contain every live body. Move unmatched
        // bodies out of view instead of letting unrelated live geometry appear
        // inside the scrubbed replay frame.
        if ( ReplayRuntimeQueueRenderPoseOverride( collection,
                                                   i,
                                                   bodyRecord->replayBodyId,
                                                   hiddenReplayPosition,
                                                   Math::Orientation::IDENTITY_QUATERNION ) )
        {
            queuedAny = true;
        }
    }
    return queuedAny;
}

bool ReplayRuntime::ApplySolverSampleForRender( GameObjects::GameModelCollection& collection,
                                                const ReplaySolverFrameSample& sample )
{
    const PhysicsBodyStore& bodyStore = collection.GetPhysicsEngine().BodyStore();
    const int modelCount = collection.RenderInstances().Count();
    if ( !ReplayRuntimePrepareBodyMatchedMask( m_renderPoseBodyMatched, modelCount ) )
    {
        return false;
    }
    bool queuedAny = false;

    for ( const ReplaySolverBodySample& body : sample.bodies )
    {
        int resolvedModelIndex = -1;
        if ( !ReplayRuntimeResolveReplayBody( bodyStore, body.id, body.modelIndex, modelCount, resolvedModelIndex ) )
        {
            continue;
        }

        Math::Orientation::Quaternion orientation( body.orientation[0],
                                                   body.orientation[1],
                                                   body.orientation[2],
                                                   body.orientation[3] );
        orientation.Normalise();
        if ( ReplayRuntimeQueueRenderPoseOverride( collection,
                                                   resolvedModelIndex,
                                                   body.id.value,
                                                   body.position,
                                                   orientation ) )
        {
            m_renderPoseBodyMatched[static_cast<std::size_t>( resolvedModelIndex )] = 1;
            queuedAny = true;
        }
    }

    const Math::Vector::Vector3 hiddenReplayPosition( 0.0f, -100000.0f, 0.0f );
    for ( int i = 0; i < modelCount; ++i )
    {
        const std::size_t bodyIndex = static_cast<std::size_t>( i );
        if ( m_renderPoseBodyMatched[bodyIndex] )
        {
            continue;
        }

        const PhysicsBodyRecord* bodyRecord = ReplayRuntimeBodyRecordForModelIndex( bodyStore, i );
        if ( !bodyRecord )
        {
            continue;
        }

        if ( ReplayRuntimeQueueRenderPoseOverride( collection,
                                                   i,
                                                   bodyRecord->replayBodyId,
                                                   hiddenReplayPosition,
                                                   Math::Orientation::IDENTITY_QUATERNION ) )
        {
            queuedAny = true;
        }
    }
    return queuedAny;
}

bool ReplayRuntime::ApplyPredictionFrameForRender( GameObjects::GameModelCollection& collection,
                                                   const RunReplayPredictionFrame& frame )
{
    const PhysicsBodyStore& bodyStore = collection.GetPhysicsEngine().BodyStore();
    const int modelCount = collection.RenderInstances().Count();
    if ( !ReplayRuntimePrepareBodyMatchedMask( m_renderPoseBodyMatched, modelCount ) )
    {
        return false;
    }
    bool queuedAny = false;

    for ( const RunReplayPredictionBodySample& body : frame.bodies )
    {
        int resolvedModelIndex = -1;
        if ( !ReplayRuntimeResolveReplayBody( bodyStore, body.id, body.modelIndex, modelCount, resolvedModelIndex ) )
        {
            continue;
        }

        Math::Orientation::Quaternion orientation = body.orientation;
        orientation.Normalise();
        if ( ReplayRuntimeQueueRenderPoseOverride( collection,
                                                   resolvedModelIndex,
                                                   body.id.value,
                                                   body.position,
                                                   orientation ) )
        {
            m_renderPoseBodyMatched[static_cast<std::size_t>( resolvedModelIndex )] = 1;
            queuedAny = true;
        }
    }

    const Math::Vector::Vector3 hiddenReplayPosition( 0.0f, -100000.0f, 0.0f );
    for ( int i = 0; i < modelCount; ++i )
    {
        const std::size_t bodyIndex = static_cast<std::size_t>( i );
        if ( m_renderPoseBodyMatched[bodyIndex] )
        {
            continue;
        }

        const PhysicsBodyRecord* bodyRecord = ReplayRuntimeBodyRecordForModelIndex( bodyStore, i );
        if ( !bodyRecord )
        {
            continue;
        }

        if ( ReplayRuntimeQueueRenderPoseOverride( collection,
                                                   i,
                                                   bodyRecord->replayBodyId,
                                                   hiddenReplayPosition,
                                                   Math::Orientation::IDENTITY_QUATERNION ) )
        {
            queuedAny = true;
        }
    }
    return queuedAny;
}

bool ReplayRuntime::HasLoadedPresentation() const
{
    return m_loadedPresentation.enabled && m_loadedPresentation.samples.size() >= 2;
}


const ReplayPresentationSample* ReplayRuntime::LoadedPresentationSampleAtNormalized( float normalized ) const
{
    if ( !HasLoadedPresentation() )
    {
        return nullptr;
    }

    return ReplayRuntimeLoadedPresentationSampleAtNormalized( m_loadedPresentation.samples, normalized );
}


const ReplayPresentationSample* ReplayRuntime::LoadedPresentationLatestSample() const
{
    return HasLoadedPresentation() ? &m_loadedPresentation.samples.back() : nullptr;
}


bool ReplayRuntime::IsScrubPaused() const
{
    if ( !m_scrubber.historicalSamplePaused )
    {
        return false;
    }

    if ( m_scrubber.activeTrack == RunReplayTrack::Presentation && HasLoadedPresentation() )
    {
        return LoadedPresentationSampleAtNormalized( TrackPosition( RunReplayTrack::Presentation ) ) != nullptr;
    }

    const float position = TrackPosition( m_scrubber.activeTrack );
    const float presentT = m_scrubber.activeTrack == RunReplayTrack::Solver ? SolverPresentTrackPosition() : 1.0f;
    if ( AtPresentTrackPosition( position, presentT ) )
    {
        return false;
    }

    if ( m_scrubber.activeTrack == RunReplayTrack::Presentation )
    {
        return m_presentation.IsEnabled() && m_presentation.SampleAtNormalized( position ) != nullptr;
    }

    if ( TrackPositionIsFuture( position, presentT ) )
    {
        return CurrentPredictionScrubFrame() != nullptr;
    }

    return m_solver.IsEnabled() &&
           m_solver.SampleAtNormalized( SolverNormalizedFromTrack( position, presentT ) ) != nullptr;
}


const ReplayPresentationSample* ReplayRuntime::CurrentScrubSample() const
{
    // Concept: a scrub sample is available only when the active track is paused
    // away from live time. Live presentation should continue drawing the live
    // scene instead of borrowing old retained samples.
    if ( m_scrubber.activeTrack != RunReplayTrack::Presentation )
    {
        return nullptr;
    }

    if ( HasLoadedPresentation() )
    {
        return m_scrubber.historicalSamplePaused
                   ? LoadedPresentationSampleAtNormalized( TrackPosition( RunReplayTrack::Presentation ) )
                   : nullptr;
    }

    if ( !IsScrubPaused() )
    {
        return nullptr;
    }

    return m_presentation.SampleAtNormalized( TrackPosition( RunReplayTrack::Presentation ) );
}


const ReplaySolverFrameSample* ReplayRuntime::CurrentSolverScrubSample() const
{
    if ( m_scrubber.activeTrack != RunReplayTrack::Solver || !IsScrubPaused() )
    {
        return nullptr;
    }

    const float position = TrackPosition( RunReplayTrack::Solver );
    const float presentT = SolverPresentTrackPosition();
    if ( TrackPositionIsFuture( position, presentT ) )
    {
        return nullptr;
    }

    return m_solver.SampleAtNormalized( SolverNormalizedFromTrack( position, presentT ) );
}


const RunReplayPredictionFrame* ReplayRuntime::CurrentPredictionScrubFrame() const
{
    // Concept: prediction frames extend the solver track past the present
    // marker. They are not retained history, so only the future side of the
    // normalized track can resolve to a prediction frame. Prediction.enabled is
    // deliberately not checked here: Play can freeze rebuilds while keeping the
    // committed future scrubbable.
    std::size_t frameCount = 0;
    const std::vector<RunReplayPredictionFrame>& frames =
        ReplayRuntimeTimelinePredictionFrames( m_prediction, frameCount );
    if ( m_scrubber.activeTrack != RunReplayTrack::Solver || !m_scrubber.historicalSamplePaused || frameCount < 2 )
    {
        return nullptr;
    }

    const float position = TrackPosition( RunReplayTrack::Solver );
    const float presentT = SolverPresentTrackPosition();
    if ( !TrackPositionIsFuture( position, presentT ) )
    {
        return nullptr;
    }

    const float predictionT = PredictionNormalizedFromTrack( position, presentT );
    const std::size_t frameIndex =
        (std::min)( frameCount - 1,
                    static_cast<std::size_t>( std::round( predictionT * static_cast<float>( frameCount - 1 ) ) ) );
    return &frames[frameIndex];
}


bool ReplayRuntime::ResolveCauseTreeBodyPosition( ReplayBodyId id,
                                                  const PhysicsBodyStore& bodyStore,
                                                  const ColliderStore& colliderStore,
                                                  Vector3& outPosition,
                                                  float* outRadius ) const
{
    if ( id.value == 0 )
    {
        return false;
    }

    if ( outRadius )
    {
        *outRadius = 1.0f;
    }

    const std::vector<RunReplayPredictionFrame>& activePredictionFrames = ActivePredictionFrames();
    if ( m_prediction.enabled && !activePredictionFrames.empty() &&
         m_prediction.simulation.targetId.value == m_pathVisualizer.targetId.value )
    {
        if ( const RunReplayPredictionBodySample* body =
                 FindReplayPredictionBodyById( activePredictionFrames.front(), id ) )
        {
            outPosition = body->position;
            if ( outRadius )
            {
                const PhysicsBodyHandle liveBody = bodyStore.HandleForReplayBodyId( id.value, body->modelIndex );
                const PhysicsBodyRecord* liveBodyRecord = bodyStore.RecordForHandle( liveBody );
                *outRadius =
                    liveBodyRecord
                        ? ReplayRuntimeColliderRadiusForBody( colliderStore, *liveBodyRecord, body->modelIndex )
                        : ReplayRuntimeColliderRadiusForModelIndex( colliderStore, body->modelIndex );
            }
            return true;
        }
    }

    if ( const ReplaySolverFrameSample* sample = CurrentSolverScrubSample() )
    {
        if ( const ReplaySolverBodySample* body = FindReplayBodyById( *sample, id ) )
        {
            outPosition = body->position;
            if ( outRadius )
            {
                const PhysicsBodyHandle liveBody = bodyStore.HandleForReplayBodyId( id.value, body->modelIndex );
                const PhysicsBodyRecord* liveBodyRecord = bodyStore.RecordForHandle( liveBody );
                *outRadius =
                    liveBodyRecord
                        ? ReplayRuntimeColliderRadiusForBody( colliderStore, *liveBodyRecord, body->modelIndex )
                        : ReplayRuntimeColliderRadiusForModelIndex( colliderStore, body->modelIndex );
            }
            return true;
        }
    }

    const auto& bodies = bodyStore.Records();
    for ( int i = 0; i < static_cast<int>( bodies.size() ); ++i )
    {
        const PhysicsBodyRecord& body = bodies[static_cast<std::size_t>( i )];
        if ( body.replayBodyId == id.value )
        {
            outPosition = body.position;
            if ( outRadius )
            {
                const int fallbackModelIndex = bodyStore.ModelIndexForHandle( body.handle );
                *outRadius = ReplayRuntimeColliderRadiusForBody( colliderStore, body, fallbackModelIndex );
            }
            return true;
        }
    }
    return false;
}


PhysicsBodyHandle ReplayRuntime::ResolveVelocityEditBodyHandle( const PhysicsBodyStore& bodyStore ) const
{
    if ( !m_pathVisualizer.hasTarget || m_pathVisualizer.targetId.value == 0 )
    {
        return PhysicsBodyHandle{};
    }

    return bodyStore.HandleForReplayBodyId( m_pathVisualizer.targetId.value, m_pathVisualizer.targetModelIndex );
}


bool ReplayRuntime::BuildCauseTreeRows(
    const std::vector<Rendering::RenderInstancePresentationRecord>& presentationRecords,
    const PhysicsBodyStore& bodyStore )
{
    PROFILE_SCOPED( "Frame/Replay/CauseTree/BuildRows" );
    m_causeTree.rows.clear();

    if ( !m_pathVisualizer.hasTarget || m_pathVisualizer.targetId.value == 0 )
    {
        return false;
    }

    // Why: ActivePredictionFrames() waits for a coherent full buffer, while the
    // prediction overlay exposes a populated build prefix so long jobs are
    // visible immediately. The cause tree must use the same readiness rule.
    const bool predictionPrefixVisible = ActivePredictionFrames().size() >= 2 ||
                                         m_prediction.HasPublishedBuildFramePrefix() ||
                                         !m_prediction.futureNodeCache.futureNodes.empty();
    const bool usePrediction = m_prediction.enabled && predictionPrefixVisible &&
                               m_prediction.simulation.targetId.value == m_pathVisualizer.targetId.value;
    const std::vector<RunReplayPathTraceNode>& nodes =
        usePrediction ? m_prediction.futureNodeCache.futureNodes : m_pathVisualizer.futureNodes;
    const ReplaySolverFrameSample* solverSample = CurrentSolverScrubSample();
    const std::size_t solverContactCount =
        solverSample ? solverSample->worldSnapshot.persistentContacts.size() : static_cast<std::size_t>( 0 );
    const std::size_t estimatedRows = 1 + nodes.size() + solverContactCount * 3;
    if ( estimatedRows > m_causeTree.rows.capacity() )
    {
        // Hazard: this path runs from input/render. If a future scene exceeds
        // the preallocated explanation budget, hide the overlay for the frame
        // instead of growing row storage on the hot path.
        m_causeTree.selectedRow = -1;
        return false;
    }
    bool rowOverflow = false;
    auto appendCauseTreeRow = [&]( const RunReplayCauseTreeRow& row ) -> bool
    {
        if ( m_causeTree.rows.size() >= m_causeTree.rows.capacity() )
        {
            rowOverflow = true;
            return false;
        }
        m_causeTree.rows.push_back( row );
        return true;
    };

    // Invariant: cause-tree rows keep model indices only for UI row selection
    // and solver-artifact contact matching. ReplayBodyId identity resolves
    // through body-store handles first; solver samples are historical fallback.
    auto modelIndexForId = [&]( ReplayBodyId id, int preferredModelIndex ) -> int
    {
        const PhysicsBodyHandle body = bodyStore.HandleForReplayBodyId( id.value, preferredModelIndex );
        const int liveIndex = bodyStore.ModelIndexForHandle( body );
        if ( liveIndex >= 0 )
        {
            return liveIndex;
        }
        if ( solverSample )
        {
            if ( const ReplaySolverBodySample* sampleBody = FindReplayBodyById( *solverSample, id ) )
            {
                return sampleBody->modelIndex;
            }
        }
        return -1;
    };

    auto idForModelIndex = [&]( int modelIndex ) -> ReplayBodyId
    {
        ReplayBodyId id;
        if ( modelIndex < 0 )
        {
            return id;
        }
        if ( solverSample )
        {
            id = ReplayBodyIdForModelIndex( *solverSample, modelIndex );
            if ( id.value != 0 )
            {
                return id;
            }
        }
        if ( const PhysicsBodyRecord* body = bodyStore.RecordForModelIndex( modelIndex ) )
        {
            id.value = body->replayBodyId;
        }
        return id;
    };

    auto writeName =
        [&]( ReplayBodyId id, int modelIndex, const char* fallback, char* out, std::size_t outSize ) -> void
    {
        out[0] = '\0';
        if ( fallback && fallback[0] != '\0' )
        {
            strncpy_s( out, outSize, fallback, _TRUNCATE );
            return;
        }
        if ( modelIndex >= 0 && modelIndex < static_cast<int>( presentationRecords.size() ) )
        {
            const char* modelName = presentationRecords[static_cast<std::size_t>( modelIndex )].displayName;
            if ( modelName && modelName[0] != '\0' )
            {
                strncpy_s( out, outSize, modelName, _TRUNCATE );
                return;
            }
        }
        if ( solverSample )
        {
            if ( const ReplaySolverBodySample* body = FindReplayBodyById( *solverSample, id ) )
            {
                if ( body->name[0] != '\0' )
                {
                    strncpy_s( out, outSize, body->name, _TRUNCATE );
                    return;
                }
            }
        }
        sprintf_s( out, outSize, "body_%u", id.value );
    };

    auto appendSolverRowsForBody = [&]( RunReplayCauseTreeRow bodyRow ) -> void
    {
        if ( usePrediction )
        {
            for ( int i = 0; i < static_cast<int>( nodes.size() ); ++i )
            {
                const RunReplayPathTraceNode& node = nodes[static_cast<std::size_t>( i )];
                if ( node.id.value != bodyRow.id.value )
                {
                    continue;
                }
                RunReplayCauseTreeRow contactRow;
                contactRow.kind = node.contactDerived ? RunReplayCauseTreeRowKind::PredictionContact
                                                      : RunReplayCauseTreeRowKind::PredictionMotion;
                contactRow.id = bodyRow.id;
                contactRow.parentId = node.parentId;
                contactRow.counterpartId = node.parentId;
                contactRow.firstFrame = node.firstFrame;
                contactRow.depth = bodyRow.depth + 1;
                contactRow.modelIndex = bodyRow.modelIndex;
                contactRow.counterpartModelIndex = node.parentModelIndex;
                contactRow.contactIndex = i;
                contactRow.prediction = true;
                contactRow.point = node.contactPoint;
                contactRow.normal = ReplayNormalizeOr( node.contactNormal, Vector3( 0.0f, 1.0f, 0.0f ) );
                if ( node.contactDerived )
                {
                    sprintf_s( contactRow.name, sizeof( contactRow.name ), "Predicted contact" );
                    sprintf_s( contactRow.detail,
                               sizeof( contactRow.detail ),
                               "first frame %llu  normal %.2f %.2f %.2f",
                               static_cast<unsigned long long>( node.firstFrame ),
                               contactRow.normal.x,
                               contactRow.normal.y,
                               contactRow.normal.z );
                }
                else
                {
                    sprintf_s( contactRow.name, sizeof( contactRow.name ), "Predicted movement" );
                    sprintf_s( contactRow.detail,
                               sizeof( contactRow.detail ),
                               "first affected frame %llu  direction %.2f %.2f %.2f",
                               static_cast<unsigned long long>( node.firstFrame ),
                               contactRow.normal.x,
                               contactRow.normal.y,
                               contactRow.normal.z );
                }
                if ( !appendCauseTreeRow( contactRow ) )
                {
                    return;
                }
            }
            return;
        }

        if ( !solverSample || bodyRow.modelIndex < 0 )
        {
            return;
        }

        struct ManifoldGroup
        {
            int otherModelIndex = -1;
            bool terrain = false;
        };
        std::array<ManifoldGroup, REPLAY_CAUSE_TREE_CONTACT_CAPACITY> groups = {};
        std::size_t groupCount = 0;
        for ( const ReplaySolverPersistentContactSample& contact : solverSample->worldSnapshot.persistentContacts )
        {
            if ( !ReplayContactHasModelIndex( contact, bodyRow.modelIndex ) )
            {
                continue;
            }
            const int otherModelIndex = ReplayContactOtherModelIndex( contact, bodyRow.modelIndex );
            const bool terrain = contact.isTerrain || otherModelIndex < 0;
            bool exists = false;
            for ( std::size_t groupIndex = 0; groupIndex < groupCount; ++groupIndex )
            {
                const ManifoldGroup& group = groups[groupIndex];
                if ( group.otherModelIndex == otherModelIndex && group.terrain == terrain )
                {
                    exists = true;
                    break;
                }
            }
            if ( !exists )
            {
                if ( groupCount >= groups.size() )
                {
                    rowOverflow = true;
                    return;
                }
                groups[groupCount] = { otherModelIndex, terrain };
                ++groupCount;
            }
        }

        for ( std::size_t groupIndex = 0; groupIndex < groupCount; ++groupIndex )
        {
            const ManifoldGroup& group = groups[groupIndex];
            Vector3 centroid = SkullbonezCore::Math::Vector::ZERO_VECTOR;
            Vector3 normalSum = SkullbonezCore::Math::Vector::ZERO_VECTOR;
            float maxPenetration = 0.0f;
            int pointCount = 0;
            int firstContactIndex = -1;
            uint32_t firstFeatureId = 0;
            for ( int i = 0; i < static_cast<int>( solverSample->worldSnapshot.persistentContacts.size() ); ++i )
            {
                const ReplaySolverPersistentContactSample& contact =
                    solverSample->worldSnapshot.persistentContacts[static_cast<std::size_t>( i )];
                if ( !ReplayContactHasModelIndex( contact, bodyRow.modelIndex ) )
                {
                    continue;
                }
                const int otherModelIndex = ReplayContactOtherModelIndex( contact, bodyRow.modelIndex );
                const bool terrain = contact.isTerrain || otherModelIndex < 0;
                if ( otherModelIndex != group.otherModelIndex || terrain != group.terrain )
                {
                    continue;
                }
                const Vector3 point = ReplayContactPoint( *solverSample, contact );
                centroid += point;
                normalSum += ReplayContactNormalForModel( contact, bodyRow.modelIndex );
                maxPenetration = (std::max)( maxPenetration, contact.penetration );
                pointCount += 1;
                if ( firstContactIndex < 0 )
                {
                    firstContactIndex = i;
                    firstFeatureId = contact.featureId;
                }
            }
            if ( pointCount <= 0 )
            {
                continue;
            }
            centroid /= static_cast<float>( pointCount );
            const ReplayBodyId otherId = idForModelIndex( group.otherModelIndex );

            char otherName[64] = {};
            if ( group.terrain )
            {
                strncpy_s( otherName, sizeof( otherName ), "terrain", _TRUNCATE );
            }
            else
            {
                writeName( otherId, group.otherModelIndex, nullptr, otherName, sizeof( otherName ) );
            }

            RunReplayCauseTreeRow manifoldRow;
            manifoldRow.kind = RunReplayCauseTreeRowKind::Manifold;
            manifoldRow.id = bodyRow.id;
            manifoldRow.parentId = bodyRow.parentId;
            manifoldRow.counterpartId = otherId;
            manifoldRow.depth = bodyRow.depth + 1;
            manifoldRow.modelIndex = bodyRow.modelIndex;
            manifoldRow.counterpartModelIndex = group.otherModelIndex;
            manifoldRow.contactIndex = firstContactIndex;
            manifoldRow.featureId = static_cast<int>( firstFeatureId );
            manifoldRow.manifoldPointCount = pointCount;
            manifoldRow.penetration = maxPenetration;
            manifoldRow.point = centroid;
            manifoldRow.normal = ReplayNormalizeOr( normalSum, Vector3( 0.0f, 1.0f, 0.0f ) );
            manifoldRow.terrain = group.terrain;
            sprintf_s( manifoldRow.name, sizeof( manifoldRow.name ), "Manifold vs %s", otherName );
            sprintf_s( manifoldRow.detail,
                       sizeof( manifoldRow.detail ),
                       "%d point%s  max pen %.3f",
                       pointCount,
                       pointCount == 1 ? "" : "s",
                       maxPenetration );
            if ( !appendCauseTreeRow( manifoldRow ) )
            {
                return;
            }

            for ( int i = 0; i < static_cast<int>( solverSample->worldSnapshot.persistentContacts.size() ); ++i )
            {
                const ReplaySolverPersistentContactSample& contact =
                    solverSample->worldSnapshot.persistentContacts[static_cast<std::size_t>( i )];
                if ( !ReplayContactHasModelIndex( contact, bodyRow.modelIndex ) )
                {
                    continue;
                }
                const int otherModelIndex = ReplayContactOtherModelIndex( contact, bodyRow.modelIndex );
                const bool terrain = contact.isTerrain || otherModelIndex < 0;
                if ( otherModelIndex != group.otherModelIndex || terrain != group.terrain )
                {
                    continue;
                }

                RunReplayCauseTreeRow solverRow;
                solverRow.kind = RunReplayCauseTreeRowKind::SolverRow;
                solverRow.id = bodyRow.id;
                solverRow.parentId = bodyRow.parentId;
                solverRow.counterpartId = otherId;
                solverRow.depth = bodyRow.depth + 2;
                solverRow.modelIndex = bodyRow.modelIndex;
                solverRow.counterpartModelIndex = group.otherModelIndex;
                solverRow.contactIndex = i;
                solverRow.solverRowIndex = i;
                solverRow.pipelineIndex = ReplayFindPipelineIndexForContact( solverSample->worldSnapshot, contact );
                solverRow.featureId = static_cast<int>( contact.featureId );
                solverRow.manifoldPointCount = contact.manifoldPointCount;
                solverRow.penetration = contact.penetration;
                solverRow.normalImpulse = contact.accN;
                solverRow.tangentImpulse = sqrtf( contact.accT1 * contact.accT1 + contact.accT2 * contact.accT2 );
                solverRow.warmStartImpulse = contact.terrainWarmStart;
                solverRow.bias = contact.bias;
                solverRow.effectiveMass = contact.normalMass;
                solverRow.frictionLimit = contact.frictionLimit;
                solverRow.point = ReplayContactPoint( *solverSample, contact );
                solverRow.normal = ReplayContactNormalForModel( contact, bodyRow.modelIndex );
                solverRow.impulse = ReplayContactImpulseForModel( contact, bodyRow.modelIndex );
                solverRow.terrain = terrain;
                solverRow.warmStarted = contact.warmStarted;
                sprintf_s( solverRow.name, sizeof( solverRow.name ), "Solver row %d", i );
                const char* traceStage = "";
                if ( solverRow.pipelineIndex >= 0 )
                {
                    const PhysicsPipelineRecord& record =
                        solverSample->worldSnapshot.pipelineTrace[static_cast<std::size_t>( solverRow.pipelineIndex )];
                    traceStage = PhysicsPipelineStageName( record.stage );
                }
                sprintf_s( solverRow.detail,
                           sizeof( solverRow.detail ),
                           "feature %u  n %.3f  t %.3f  bias %.3f  mass %.3f  limit %.3f  %s%s%s",
                           contact.featureId,
                           solverRow.normalImpulse,
                           solverRow.tangentImpulse,
                           solverRow.bias,
                           solverRow.effectiveMass,
                           solverRow.frictionLimit,
                           contact.warmStarted ? "warm" : "cold",
                           solverRow.pipelineIndex >= 0 ? "  " : "",
                           traceStage );
                if ( !appendCauseTreeRow( solverRow ) )
                {
                    return;
                }
            }
        }
    };

    auto addBodyRow = [&]( ReplayBodyId id,
                           ReplayBodyId parentId,
                           ReplayFrameIndex firstFrame,
                           int depth,
                           int modelIndex,
                           const char* fallbackName ) -> bool
    {
        if ( id.value == 0 )
        {
            return false;
        }

        RunReplayCauseTreeRow row;
        row.kind = RunReplayCauseTreeRowKind::Body;
        row.id = id;
        row.parentId = parentId;
        row.firstFrame = firstFrame;
        row.depth = depth;
        row.modelIndex = modelIndexForId( id, modelIndex );
        row.prediction = usePrediction;
        writeName( id, row.modelIndex, fallbackName, row.name, sizeof( row.name ) );
        if ( usePrediction && firstFrame > 0 )
        {
            sprintf_s( row.detail,
                       sizeof( row.detail ),
                       "first affected frame %llu",
                       static_cast<unsigned long long>( firstFrame ) );
        }
        else if ( row.modelIndex >= 0 && solverSample )
        {
            if ( const ReplaySolverBodySample* body = FindReplayBodyByModelIndex( *solverSample, row.modelIndex ) )
            {
                sprintf_s( row.detail,
                           sizeof( row.detail ),
                           "contacts %u  max pen %.3f  impulse %.3f",
                           static_cast<unsigned int>( body->contactCount ),
                           body->maxPenetration,
                           body->normalImpulseSum );
            }
        }
        else if ( firstFrame > 0 )
        {
            sprintf_s( row.detail,
                       sizeof( row.detail ),
                       "first affected frame %llu",
                       static_cast<unsigned long long>( firstFrame ) );
        }
        if ( !appendCauseTreeRow( row ) )
        {
            return false;
        }
        appendSolverRowsForBody( m_causeTree.rows.back() );
        return !rowOverflow;
    };

    if ( !addBodyRow( m_pathVisualizer.targetId,
                      ReplayBodyId{},
                      0,
                      0,
                      m_pathVisualizer.targetModelIndex,
                      m_pathVisualizer.targetName ) )
    {
        m_causeTree.rows.clear();
        m_causeTree.selectedRow = -1;
        return false;
    }

    auto addChildren = [&]( auto&& self, ReplayBodyId parentId, int fallbackDepth ) -> void
    {
        for ( const RunReplayPathTraceNode& node : nodes )
        {
            if ( node.parentId.value != parentId.value )
            {
                continue;
            }
            const int depth = node.depth > 0 ? node.depth : fallbackDepth;
            if ( addBodyRow( node.id,
                             parentId,
                             node.firstFrame,
                             depth,
                             modelIndexForId( node.id, node.modelIndex ),
                             nullptr ) )
            {
                self( self, node.id, depth + 1 );
            }
        }
    };
    addChildren( addChildren, m_pathVisualizer.targetId, 1 );
    if ( rowOverflow )
    {
        m_causeTree.rows.clear();
        m_causeTree.selectedRow = -1;
        return false;
    }

    m_causeTree.selectedRow = -1;
    if ( m_camera.focusKind != RunReplayCameraFocusKind::None )
    {
        for ( int i = 0; i < static_cast<int>( m_causeTree.rows.size() ); ++i )
        {
            const RunReplayCauseTreeRow& row = m_causeTree.rows[static_cast<std::size_t>( i )];
            if ( row.kind != m_camera.focusRowKind || row.id.value != m_camera.focusedId.value ||
                 row.modelIndex != m_camera.focusModelIndex || row.terrain != m_camera.focusTerrain )
            {
                continue;
            }
            if ( row.kind == RunReplayCauseTreeRowKind::Body ||
                 ( row.counterpartId.value == m_camera.counterpartId.value &&
                   row.counterpartModelIndex == m_camera.focusCounterpartModelIndex &&
                   ( row.kind != RunReplayCauseTreeRowKind::SolverRow ||
                     ( row.featureId == m_camera.focusFeatureId &&
                       row.solverRowIndex == m_camera.focusSolverRowIndex ) ) ) )
            {
                m_causeTree.selectedRow = i;
                m_camera.focusedRow = i;
                break;
            }
        }
    }
    if ( m_causeTree.selectedRow >= static_cast<int>( m_causeTree.rows.size() ) )
    {
        m_causeTree.selectedRow = -1;
    }
    return !m_causeTree.rows.empty();
}


bool ReplayRuntime::BuildPredictionGhostDrawRequests(
    const std::vector<Rendering::RenderInstancePresentationRecord>& presentationRecords,
    const PhysicsBodyStore& bodyStore )
{
    m_predictionGhostDrawRequests.clear();
    const std::vector<RunReplayPredictionFrame>& frames = ActivePredictionFrames();
    const bool drawLivePrediction = m_prediction.enabled && m_prediction.ragdollVisualsEnabled && frames.size() >= 2;
    const bool drawBaseline = m_prediction.baseline.valid && m_prediction.baseline.comparisonActive &&
                              m_prediction.ragdollVisualsEnabled && !m_prediction.baseline.bodyPoses.empty();

    bool hasRagdollPart = false;
    for ( int i = 0; i < static_cast<int>( presentationRecords.size() ); ++i )
    {
        if ( ReplayRuntimeModelIsRagdollPart( presentationRecords, i ) )
        {
            hasRagdollPart = true;
            break;
        }
    }
    if ( !hasRagdollPart )
    {
        return false;
    }

    const std::size_t liveRequestCapacity =
        drawLivePrediction
            ? (std::min)( frames.size(), REPLAY_PREDICTION_GHOST_MAX_FRAMES + 1 ) * presentationRecords.size()
            : 0u;
    const std::size_t baselineRequestCapacity = drawBaseline ? m_prediction.baseline.bodyPoses.size() : 0u;
    if ( liveRequestCapacity + baselineRequestCapacity > m_predictionGhostDrawRequests.capacity() )
    {
        return false;
    }

    if ( drawBaseline )
    {
        for ( const ReplayPredictionBaselineBodyPose& pose : m_prediction.baseline.bodyPoses )
        {
            if ( !pose.hasRestPose || pose.modelIndex < 0 ||
                 pose.modelIndex >= static_cast<int>( presentationRecords.size() ) ||
                 !ReplayRuntimeModelIsRagdollPart( presentationRecords, pose.modelIndex ) )
            {
                continue;
            }

            ReplayPredictionGhostDrawRequest request;
            request.modelIndex = pose.modelIndex;
            request.position = pose.restPosition;
            request.orientation = pose.restOrientation;
            request.orientation.Normalise();
            request.alpha = 0.075f;
            request.tintR = 0.28f;
            request.tintG = 0.76f;
            request.tintB = 1.0f;
            request.tintStrength = 0.82f;
            m_predictionGhostDrawRequests.push_back( request );
        }
    }

    if ( !drawLivePrediction )
    {
        return !m_predictionGhostDrawRequests.empty();
    }

    const std::size_t lastIndex = frames.size() - 1;
    const std::size_t stride =
        (std::max)( static_cast<std::size_t>( 1 ),
                    ( lastIndex + REPLAY_PREDICTION_GHOST_MAX_FRAMES - 1 ) / REPLAY_PREDICTION_GHOST_MAX_FRAMES );
    const ReplayFrameIndex lastFrame = frames.back().frameIndex;

    auto appendGhostFrame = [&]( std::size_t index )
    {
        const RunReplayPredictionFrame& predictionFrame = frames[index];
        if ( predictionFrame.frameIndex == 0 )
        {
            return;
        }

        const float t =
            lastFrame > 0
                ? std::clamp( static_cast<float>( predictionFrame.frameIndex ) / static_cast<float>( lastFrame ),
                              0.0f,
                              1.0f )
                : 1.0f;
        const float alpha = std::clamp( 0.055f + ( 1.0f - t ) * 0.105f, 0.045f, 0.18f );

        for ( const RunReplayPredictionBodySample& body : predictionFrame.bodies )
        {
            int resolvedModelIndex = -1;
            if ( !ReplayRuntimeResolveReplayBody( bodyStore,
                                                  body.id,
                                                  body.modelIndex,
                                                  static_cast<int>( presentationRecords.size() ),
                                                  resolvedModelIndex ) )
            {
                continue;
            }

            if ( !ReplayRuntimeModelIsRagdollPart( presentationRecords, resolvedModelIndex ) )
            {
                continue;
            }

            ReplayPredictionGhostDrawRequest request;
            request.modelIndex = resolvedModelIndex;
            request.position = body.position;
            request.orientation = body.orientation;
            request.orientation.Normalise();
            request.alpha = alpha;
            m_predictionGhostDrawRequests.push_back( request );
        }
    };

    std::size_t farIndex = lastIndex;
    if ( farIndex % stride != 0 )
    {
        appendGhostFrame( farIndex );
        farIndex = ( farIndex / stride ) * stride;
    }
    for ( std::size_t index = farIndex; index >= stride; index -= stride )
    {
        appendGhostFrame( index );
        if ( index == stride )
        {
            break;
        }
    }
    return !m_predictionGhostDrawRequests.empty();
}

const std::vector<ReplayPredictionGhostDrawRequest>& ReplayRuntime::PredictionGhostDrawRequests() const
{
    return m_predictionGhostDrawRequests;
}


bool ReplayRuntime::BuildFocusModelMask( const PhysicsBodyStore& bodyStore, int modelCount )
{
    PROFILE_SCOPED( "Frame/Replay/FocusMask" );
    if ( !m_pathVisualizer.hasTarget || m_pathVisualizer.targetId.value == 0 || modelCount <= 0 ||
         modelCount > MAX_GAME_MODELS )
    {
        m_focusModelMask.clear();
        return false;
    }

    m_focusModelMask.assign( static_cast<std::size_t>( modelCount ), 0 );
    int markedCount = 0;
    const auto markByReplayId = [&]( ReplayBodyId id, int preferredModelIndex )
    {
        if ( id.value == 0 )
        {
            return;
        }

        const PhysicsBodyHandle body = bodyStore.HandleForReplayBodyId( id.value, preferredModelIndex );
        const int resolvedIndex = bodyStore.ModelIndexForHandle( body );
        if ( resolvedIndex >= 0 && resolvedIndex < modelCount )
        {
            uint8_t& mask = m_focusModelMask[static_cast<std::size_t>( resolvedIndex )];
            if ( mask == 0 )
            {
                mask = 1;
                ++markedCount;
            }
        }
    };

    if ( m_pathVisualizer.targets.empty() )
    {
        markByReplayId( m_pathVisualizer.targetId, m_pathVisualizer.targetModelIndex );
    }
    else
    {
        for ( const RunReplayPathTarget& target : m_pathVisualizer.targets )
        {
            markByReplayId( target.id, target.modelIndex );
        }
    }

    const std::vector<RunReplayPathTraceNode>& futureNodes =
        m_prediction.enabled ? m_prediction.futureNodeCache.futureNodes : m_pathVisualizer.futureNodes;
    for ( const RunReplayPathTraceNode& node : futureNodes )
    {
        markByReplayId( node.id, node.modelIndex );
    }

    if ( markedCount <= 0 || markedCount >= modelCount )
    {
        m_focusModelMask.clear();
        return false;
    }
    return true;
}


std::vector<uint8_t>& ReplayRuntime::FocusModelMask()
{
    return m_focusModelMask;
}

const std::vector<uint8_t>& ReplayRuntime::FocusModelMask() const
{
    return m_focusModelMask;
}

bool ReplayRuntime::HasLauncherVisualBackup() const
{
    return m_launcherVisualBackupActive;
}

void ReplayRuntime::StoreLauncherVisualBackup( const ReplayLauncherVisualSample& sample )
{
    m_launcherVisualBackup = sample;
    m_launcherVisualBackupActive = true;
}

const ReplayLauncherVisualSample& ReplayRuntime::LauncherVisualBackup() const
{
    return m_launcherVisualBackup;
}

void ReplayRuntime::ClearLauncherVisualBackup()
{
    m_launcherVisualBackup = ReplayLauncherVisualSample();
    m_launcherVisualBackupActive = false;
}

void ReplayRuntime::RecordReplayTrajectoryFrameStats( const MainMemoryReplayTrajectoryStats& frameStats )
{
    // Concept: trajectory segment counters are cumulative repro-session
    // evidence. Store bytes remain a current snapshot and are refreshed by
    // CollectMemoryStats once the TrajectoryStore exists.
    for ( std::size_t i = 0; i < MAIN_MEMORY_REPLAY_TRAJECTORY_LANE_COUNT; ++i )
    {
        m_trajectoryVisualStats.emittedSegments[i] += frameStats.emittedSegments[i];
        m_trajectoryVisualStats.droppedSegments[i] += frameStats.droppedSegments[i];
    }
}

void ReplayRuntime::RecordReplayTrajectorySubmissionFrame(
    const MainMemoryReplayTrajectorySubmissionStats& submissionStats,
    int frameNumber,
    uint64_t reserveGrowthEventCount )
{
    if ( !submissionStats.hasGeometry || submissionStats.vertexBytes == 0 || submissionStats.vertexCount == 0 )
    {
        return;
    }

    ++m_trajectorySubmissionProbe.observedFrameCount;
    m_trajectorySubmissionProbe.hasSubmission = true;
    m_trajectorySubmissionProbe.stableWindowTargetFrameCount = REPLAY_TRAJECTORY_SUBMISSION_STEADY_FRAME_TARGET;

    const bool sameSubmittedBytes = m_trajectorySubmissionProbe.stableFrameCount > 0 &&
                                    m_trajectorySubmissionProbe.stableHash == submissionStats.vertexHash &&
                                    m_trajectorySubmissionProbe.vertexBytes == submissionStats.vertexBytes &&
                                    m_trajectorySubmissionProbe.vertexCount == submissionStats.vertexCount &&
                                    m_trajectorySubmissionProbe.segmentCount == submissionStats.segmentCount;
    const bool sameReserveWindow = reserveGrowthEventCount == m_trajectorySubmissionProbe.reserveGrowthEventsAtEnd;

    if ( !sameSubmittedBytes || !sameReserveWindow )
    {
        // Invariant: reveal growth and prediction build reserves are allowed
        // before the steady window. The acceptance proof starts at the first
        // frame whose submitted bytes and replay reserve counter remain fixed.
        m_trajectorySubmissionProbe.stableFrameCount = 1;
        m_trajectorySubmissionProbe.firstFrame = frameNumber;
        m_trajectorySubmissionProbe.stableHash = submissionStats.vertexHash;
        m_trajectorySubmissionProbe.vertexBytes = submissionStats.vertexBytes;
        m_trajectorySubmissionProbe.vertexCount = submissionStats.vertexCount;
        m_trajectorySubmissionProbe.segmentCount = submissionStats.segmentCount;
        m_trajectorySubmissionProbe.reserveGrowthEventsAtStart = reserveGrowthEventCount;
    }
    else
    {
        ++m_trajectorySubmissionProbe.stableFrameCount;
    }

    m_trajectorySubmissionProbe.lastFrame = frameNumber;
    m_trajectorySubmissionProbe.reserveGrowthEventsAtEnd = reserveGrowthEventCount;
    m_trajectorySubmissionProbe.noReserveGrowth =
        m_trajectorySubmissionProbe.reserveGrowthEventsAtStart == m_trajectorySubmissionProbe.reserveGrowthEventsAtEnd;
    m_trajectorySubmissionProbe.stableWindowReady =
        m_trajectorySubmissionProbe.stableFrameCount >= REPLAY_TRAJECTORY_SUBMISSION_STEADY_FRAME_TARGET &&
        m_trajectorySubmissionProbe.noReserveGrowth;
}

const ReplayTrajectorySubmissionProbeStats& ReplayRuntime::ReplayTrajectorySubmissionProbe() const
{
    return m_trajectorySubmissionProbe;
}

void ReplayRuntime::RecordReplayTrajectoryBudgetExpiry( MainMemoryReplayBudgetPass pass )
{
    const std::size_t passIndex = static_cast<std::size_t>( pass );
    if ( passIndex < MAIN_MEMORY_REPLAY_BUDGET_PASS_COUNT )
    {
        ++m_trajectoryVisualStats.budgetExpiries[passIndex];
    }
}

void ReplayRuntime::RecordReplayTrajectoryRebuildCause( MainMemoryReplayRebuildCause cause )
{
    const std::size_t causeIndex = static_cast<std::size_t>( cause );
    if ( causeIndex < MAIN_MEMORY_REPLAY_REBUILD_CAUSE_COUNT )
    {
        ++m_trajectoryVisualStats.rebuildCauses[causeIndex];
    }
}

MainMemoryReplayStats ReplayRuntime::CollectMemoryStats() const
{
    MainMemoryReplayStats stats;
    const ReplayRecorderStats presentationStats = m_presentation.GetStats();
    const ReplayRecorderStats solverStats = m_solver.GetStats();
    const ReplayEventRecorderStats eventStats = m_events.GetStats();

    // Concept: the broad replay totals are sums of the same category table
    // exposed to diagnostics. That keeps UI totals and memory-dump evidence in
    // lockstep when later stages move storage between owners.
    m_presentation.CollectMemoryCategoryBytes( stats.categoryBytes );
    m_solver.CollectMemoryCategoryBytes( stats.categoryBytes );
    m_events.CollectMemoryCategoryBytes( stats.categoryBytes );
    stats.presentationBytes = MainMemoryReplayCategoryRangeBytes( stats.categoryBytes,
                                                                  MainMemoryReplayByteCategory::PresentationOwner,
                                                                  MainMemoryReplayByteCategory::SolverOwner );
    stats.solverBytes = MainMemoryReplayCategoryRangeBytes( stats.categoryBytes,
                                                            MainMemoryReplayByteCategory::SolverOwner,
                                                            MainMemoryReplayByteCategory::EventsOwner );
    stats.eventsBytes = MainMemoryReplayCategoryRangeBytes( stats.categoryBytes,
                                                            MainMemoryReplayByteCategory::EventsOwner,
                                                            MainMemoryReplayByteCategory::LoadedOwner );
    stats.presentationSamples = presentationStats.sampleCount;
    stats.solverSamples = solverStats.sampleCount;
    stats.eventSamples = eventStats.eventCount;
    stats.memoryPreset = static_cast<int>( m_memoryPolicy.preset );
    stats.requestedRetentionSeconds = m_memoryPolicy.requestedRetentionSeconds;
    stats.requestedBudgetMiB = m_memoryPolicy.requestedBudgetMiB;
    stats.presentationRetentionSeconds = m_memoryPolicy.presentationRetentionSeconds;
    stats.solverRetentionSeconds = m_memoryPolicy.solverRetentionSeconds;
    stats.memoryBudgetClamped = m_memoryPolicy.budgetClamped;
    stats.solverWindowReduced = m_memoryPolicy.solverWindowReduced;

    MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                      MainMemoryReplayByteCategory::LoadedOwner,
                                      static_cast<uint64_t>( sizeof( m_loadedPresentation ) ) );
    MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                      MainMemoryReplayByteCategory::LoadedSampleRecords,
                                      VectorCapacityBytes( m_loadedPresentation.samples ) );
    for ( const ReplayPresentationSample& sample : m_loadedPresentation.samples )
    {
        MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                          MainMemoryReplayByteCategory::LoadedBodies,
                                          PresentationSampleMemoryBytes( sample ) );
    }
    stats.loadedReplayBytes = MainMemoryReplayCategoryRangeBytes( stats.categoryBytes,
                                                                  MainMemoryReplayByteCategory::LoadedOwner,
                                                                  MainMemoryReplayByteCategory::PredictionOwner );
    stats.loadedReplaySamples = m_loadedPresentation.samples.size();

    MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                      MainMemoryReplayByteCategory::PredictionOwner,
                                      static_cast<uint64_t>( sizeof( m_prediction ) ) );
    if ( m_prediction.simulation.predictionEngine )
    {
        MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                          MainMemoryReplayByteCategory::PredictionEngine,
                                          PredictionEngineMemoryBytes( *m_prediction.simulation.predictionEngine ) );
    }
    MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                      MainMemoryReplayByteCategory::PredictionWorldState,
                                      SolverWorldSnapshotMemoryBytes( m_prediction.simulation.predictionWorld ) );
    MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                      MainMemoryReplayByteCategory::PredictionBodyState,
                                      VectorCapacityBytes( m_prediction.simulation.predictionBodies ) );
    MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        MainMemoryReplayByteCategory::PredictionFrameRecords,
        VectorCapacityBytes( m_prediction.simulation.frames ) + VectorCapacityBytes( m_prediction.build.buildFrames ) );
    MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                      MainMemoryReplayByteCategory::PredictionFutureTree,
                                      VectorCapacityBytes( m_prediction.futureNodeCache.futureNodes ) +
                                          VectorCapacityBytes( m_prediction.futureNodeCache.futureNodeBuildScratch ) );
    for ( const RunReplayPredictionFrame& frame : m_prediction.simulation.frames )
    {
        AddPredictionFrameCategoryBytes( stats.categoryBytes, frame );
    }
    for ( const RunReplayPredictionFrame& frame : m_prediction.build.buildFrames )
    {
        AddPredictionFrameCategoryBytes( stats.categoryBytes, frame );
    }
    stats.predictionBytes = MainMemoryReplayCategoryRangeBytes( stats.categoryBytes,
                                                                MainMemoryReplayByteCategory::PredictionOwner,
                                                                MainMemoryReplayByteCategory::PathOwner );
    stats.predictionFrames = m_prediction.simulation.frames.size() + m_prediction.build.buildFrames.size();

    MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                      MainMemoryReplayByteCategory::PathOwner,
                                      static_cast<uint64_t>( sizeof( m_pathVisualizer ) + sizeof( m_causeTree ) ) );
    MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                      MainMemoryReplayByteCategory::PathFutureNodes,
                                      VectorCapacityBytes( m_pathVisualizer.futureNodes ) );
    MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                      MainMemoryReplayByteCategory::PathTargets,
                                      VectorCapacityBytes( m_pathVisualizer.targets ) );
    MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                      MainMemoryReplayByteCategory::PathCauseRows,
                                      VectorCapacityBytes( m_causeTree.rows ) );
    MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                      MainMemoryReplayByteCategory::TrajectoryStore,
                                      m_prediction.trajectoryStore.CapacityBytes() );
    stats.pathAndCauseBytes = MainMemoryReplayCategoryRangeBytes( stats.categoryBytes,
                                                                  MainMemoryReplayByteCategory::PathOwner,
                                                                  MainMemoryReplayByteCategory::RenderGhostRequests );
    stats.pathNodes = m_pathVisualizer.futureNodes.size() + m_prediction.futureNodeCache.futureNodes.size();
    stats.causeRows = m_causeTree.rows.size();

    MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                      MainMemoryReplayByteCategory::RenderGhostRequests,
                                      VectorCapacityBytes( m_predictionGhostDrawRequests ) );
    MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                      MainMemoryReplayByteCategory::RenderFocusMask,
                                      VectorCapacityBytes( m_focusModelMask ) );
    MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                      MainMemoryReplayByteCategory::RenderLauncherBackup,
                                      static_cast<uint64_t>( sizeof( m_launcherVisualBackup ) ) +
                                          LauncherVisualMemoryBytes( m_launcherVisualBackup ) );
    stats.renderScratchBytes = MainMemoryReplayCategoryRangeBytes( stats.categoryBytes,
                                                                   MainMemoryReplayByteCategory::RenderGhostRequests,
                                                                   MainMemoryReplayByteCategory::TrajectoryStore );
    stats.ghostRequests = m_predictionGhostDrawRequests.size();
    stats.trajectory = m_trajectoryVisualStats;
    stats.trajectory.storeBytes =
        MainMemoryReplayCategoryByte( stats.categoryBytes, MainMemoryReplayByteCategory::TrajectoryStore );
    stats.trajectory.recordCount = static_cast<uint64_t>( m_prediction.trajectoryStore.RecordCount() );
    stats.trajectory.pointCount = static_cast<uint64_t>( m_prediction.trajectoryStore.PointCount() );
    stats.trajectory.versionChurn = m_prediction.trajectoryStore.nextVersion > 0u
                                        ? static_cast<uint64_t>( m_prediction.trajectoryStore.nextVersion - 1u )
                                        : 0u;
    for ( const ReplayTrajectoryRecord& record : m_prediction.trajectoryStore.records )
    {
        stats.trajectory.publishedPointCount +=
            static_cast<uint64_t>( (std::min)( record.publishedPointCount, record.points.size() ) );
        stats.trajectory.maxRecordVersion = (std::max)( stats.trajectory.maxRecordVersion, record.version );
    }

    stats.totalBytes = stats.presentationBytes + stats.solverBytes + stats.eventsBytes + stats.loadedReplayBytes +
                       stats.predictionBytes + stats.pathAndCauseBytes + stats.renderScratchBytes +
                       stats.trajectory.storeBytes;
    return stats;
}

void ReplayRuntime::RecordEvent( ReplayEventKind kind,
                                 ReplayFrameIndex frameIndex,
                                 uint32_t flags,
                                 int32_t value0,
                                 int32_t value1,
                                 int32_t value2,
                                 int32_t value3,
                                 uint64_t data0,
                                 const char* text )
{
    if ( !m_events.IsEnabled() )
    {
        return;
    }

    ReplayEventInput input;
    input.frameIndex = frameIndex;
    input.branch = m_branch;
    input.kind = kind;
    input.flags = flags;
    input.value0 = value0;
    input.value1 = value1;
    input.value2 = value2;
    input.value3 = value3;
    input.data0 = data0;
    input.text = text;
    m_events.RecordEvent( input );
}

void ReplayRuntime::RecordWorldOverrideEvent( float previousGravity,
                                              float previousFluidHeight,
                                              float previousFluidDensity,
                                              float gravity,
                                              float fluidHeight,
                                              float fluidDensity )
{
    uint32_t flags = 0;
    flags |= previousGravity != gravity ? REPLAY_WORLD_OVERRIDE_GRAVITY_CHANGED : 0u;
    flags |= previousFluidHeight != fluidHeight ? REPLAY_WORLD_OVERRIDE_FLUID_HEIGHT_CHANGED : 0u;
    flags |= previousFluidDensity != fluidDensity ? REPLAY_WORLD_OVERRIDE_FLUID_DENSITY_CHANGED : 0u;
    if ( flags == 0 )
    {
        return;
    }

    uint64_t hash = REPLAY_EVENT_FNV_OFFSET;
    ReplayRuntimeHashFloat( hash, gravity );
    ReplayRuntimeHashFloat( hash, fluidHeight );
    ReplayRuntimeHashFloat( hash, fluidDensity );

    RecordEvent( ReplayEventKind::WorldOverride,
                 NextEventFrameIndex(),
                 flags,
                 ReplayRuntimeFloatBitsSigned( gravity ),
                 ReplayRuntimeFloatBitsSigned( fluidHeight ),
                 ReplayRuntimeFloatBitsSigned( fluidDensity ),
                 0,
                 hash,
                 "world_override" );
}

void ReplayRuntime::RecordLauncherConfigEvent( uint32_t changedFlags, float impulseStrength, float projectileSpeed )
{
    if ( changedFlags == 0 )
    {
        return;
    }

    uint64_t hash = REPLAY_EVENT_FNV_OFFSET;
    ReplayRuntimeHashFloat( hash, impulseStrength );
    ReplayRuntimeHashFloat( hash, projectileSpeed );

    RecordEvent( ReplayEventKind::LauncherConfig,
                 NextEventFrameIndex(),
                 changedFlags,
                 ReplayRuntimeFloatBitsSigned( impulseStrength ),
                 ReplayRuntimeFloatBitsSigned( projectileSpeed ),
                 0,
                 0,
                 hash,
                 "launcher_config" );
}

void ReplayRuntime::RecordLauncherFireEvent( const Vector3& rayOrigin,
                                             const Vector3& rayDirection,
                                             const Vector3& cameraUp,
                                             bool projectile,
                                             float impulseStrength,
                                             float projectileSpeed,
                                             int modelCount )
{
    char payload[96] = {};
    char* cursor = payload;
    std::size_t remaining = sizeof( payload );
    const int prefixWritten = std::snprintf( cursor, remaining, "ray9:" );
    if ( prefixWritten > 0 )
    {
        const std::size_t consumed =
            (std::min)( static_cast<std::size_t>( prefixWritten ), remaining > 0 ? remaining - 1 : 0 );
        cursor += consumed;
        remaining -= consumed;
    }
    ReplayRuntimeAppendVectorHex( cursor, remaining, rayOrigin );
    ReplayRuntimeAppendVectorHex( cursor, remaining, rayDirection );
    ReplayRuntimeAppendVectorHex( cursor, remaining, cameraUp );

    uint64_t hash = REPLAY_EVENT_FNV_OFFSET;
    ReplayRuntimeHashFloat( hash, rayOrigin.x );
    ReplayRuntimeHashFloat( hash, rayOrigin.y );
    ReplayRuntimeHashFloat( hash, rayOrigin.z );
    ReplayRuntimeHashFloat( hash, rayDirection.x );
    ReplayRuntimeHashFloat( hash, rayDirection.y );
    ReplayRuntimeHashFloat( hash, rayDirection.z );
    ReplayRuntimeHashFloat( hash, cameraUp.x );
    ReplayRuntimeHashFloat( hash, cameraUp.y );
    ReplayRuntimeHashFloat( hash, cameraUp.z );

    const uint32_t flags = projectile ? REPLAY_LAUNCHER_FIRE_PROJECTILE : 0u;
    RecordEvent( ReplayEventKind::LauncherFire,
                 NextEventFrameIndex(),
                 flags,
                 projectile ? 1 : 0,
                 ReplayRuntimeFloatBitsSigned( impulseStrength ),
                 ReplayRuntimeFloatBitsSigned( projectileSpeed ),
                 modelCount,
                 hash,
                 payload );
}

void ReplayRuntime::RecordEditorPlaceEvent( int objectType,
                                            bool fixedObject,
                                            bool terrainAlign,
                                            int modelCountBefore,
                                            const Vector3& terrainPoint,
                                            const Vector3& placementScale,
                                            float placementYawRadians )
{
    char payload[80] = {};
    char* cursor = payload;
    std::size_t remaining = sizeof( payload );
    const int prefixWritten = std::snprintf( cursor, remaining, "place7:" );
    if ( prefixWritten > 0 )
    {
        const std::size_t consumed =
            (std::min)( static_cast<std::size_t>( prefixWritten ), remaining > 0 ? remaining - 1 : 0 );
        cursor += consumed;
        remaining -= consumed;
    }
    ReplayRuntimeAppendVectorHex( cursor, remaining, terrainPoint );
    ReplayRuntimeAppendVectorHex( cursor, remaining, placementScale );
    ReplayRuntimeAppendFloatHex( cursor, remaining, placementYawRadians );

    uint64_t hash = REPLAY_EVENT_FNV_OFFSET;
    ReplayRuntimeHashInt( hash, objectType );
    ReplayRuntimeHashInt( hash, fixedObject ? 1 : 0 );
    ReplayRuntimeHashInt( hash, terrainAlign ? 1 : 0 );
    ReplayRuntimeHashInt( hash, modelCountBefore );
    ReplayRuntimeHashFloat( hash, terrainPoint.x );
    ReplayRuntimeHashFloat( hash, terrainPoint.y );
    ReplayRuntimeHashFloat( hash, terrainPoint.z );
    ReplayRuntimeHashFloat( hash, placementScale.x );
    ReplayRuntimeHashFloat( hash, placementScale.y );
    ReplayRuntimeHashFloat( hash, placementScale.z );
    ReplayRuntimeHashFloat( hash, placementYawRadians );

    uint32_t flags = 0;
    flags |= fixedObject ? REPLAY_EDITOR_PLACE_FIXED : 0u;
    flags |= terrainAlign ? REPLAY_EDITOR_PLACE_TERRAIN_ALIGN : 0u;

    RecordEvent( ReplayEventKind::EditorPlace,
                 NextEventFrameIndex(),
                 flags,
                 objectType,
                 fixedObject ? 1 : 0,
                 terrainAlign ? 1 : 0,
                 modelCountBefore,
                 hash,
                 payload );
}

void ReplayRuntime::RecordEditorTransformEvent( int modelIndex,
                                                uint32_t changedFlags,
                                                uint32_t replayBodyId,
                                                const Vector3& position,
                                                const Math::Orientation::Quaternion& orientation,
                                                int modelCount,
                                                int scaleAxis,
                                                float scaleFactor )
{
    changedFlags &= REPLAY_EDITOR_TRANSFORM_TRANSLATE | REPLAY_EDITOR_TRANSFORM_ROTATE | REPLAY_EDITOR_TRANSFORM_SCALE;
    if ( changedFlags == 0 )
    {
        return;
    }
    if ( ( changedFlags & REPLAY_EDITOR_TRANSFORM_SCALE ) == 0 )
    {
        scaleAxis = -1;
        scaleFactor = 1.0f;
    }
    else if ( scaleAxis < 0 || scaleAxis > 2 || !std::isfinite( scaleFactor ) || scaleFactor <= 0.0f )
    {
        return;
    }

    char payload[96] = {};
    char* cursor = payload;
    std::size_t remaining = sizeof( payload );
    const int prefixWritten =
        std::snprintf( cursor, remaining, ( changedFlags & REPLAY_EDITOR_TRANSFORM_SCALE ) ? "xform8:" : "xform7:" );
    if ( prefixWritten > 0 )
    {
        const std::size_t consumed =
            (std::min)( static_cast<std::size_t>( prefixWritten ), remaining > 0 ? remaining - 1 : 0 );
        cursor += consumed;
        remaining -= consumed;
    }
    ReplayRuntimeAppendVectorHex( cursor, remaining, position );
    ReplayRuntimeAppendQuaternionHex( cursor, remaining, orientation );
    if ( changedFlags & REPLAY_EDITOR_TRANSFORM_SCALE )
    {
        ReplayRuntimeAppendFloatHex( cursor, remaining, scaleFactor );
    }

    float qx = 0.0f;
    float qy = 0.0f;
    float qz = 0.0f;
    float qw = 1.0f;
    orientation.GetComponents( qx, qy, qz, qw );

    uint64_t hash = REPLAY_EVENT_FNV_OFFSET;
    ReplayRuntimeHashInt( hash, modelIndex );
    ReplayRuntimeHashInt( hash, static_cast<int32_t>( replayBodyId ) );
    ReplayRuntimeHashInt( hash, modelCount );
    ReplayRuntimeHashInt( hash, static_cast<int32_t>( changedFlags ) );
    ReplayRuntimeHashInt( hash, scaleAxis );
    ReplayRuntimeHashFloat( hash, position.x );
    ReplayRuntimeHashFloat( hash, position.y );
    ReplayRuntimeHashFloat( hash, position.z );
    ReplayRuntimeHashFloat( hash, qx );
    ReplayRuntimeHashFloat( hash, qy );
    ReplayRuntimeHashFloat( hash, qz );
    ReplayRuntimeHashFloat( hash, qw );
    ReplayRuntimeHashFloat( hash, scaleFactor );

    RecordEvent( ReplayEventKind::EditorTransform,
                 NextEventFrameIndex(),
                 changedFlags,
                 modelIndex,
                 static_cast<int32_t>( replayBodyId ),
                 modelCount,
                 scaleAxis,
                 hash,
                 payload );
}

bool ReplayRuntime::SaveSolverReplay( const char* path ) const
{
    return ReplayExporter::Save( m_solver, path );
}

bool ReplayRuntime::SavePresentationWithSolverHashes( const char* path, ReplayV2SaveResult* result ) const
{
    return ReplayV2Artifact::SavePresentationWithSolverHashes( m_presentation, m_solver, m_events, path, result );
}
} // namespace SkullbonezCore::Basics
