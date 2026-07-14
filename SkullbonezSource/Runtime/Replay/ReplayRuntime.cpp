/*
File: SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp
Purpose:
  Sequences replay owners across recording, workspace, restore, prediction, and probes.

Summary:
  ReplayRuntime is the composition boundary between concrete replay owners. The
  application shell supplies value commands and explicit synchronous owners;
  this file orders workspace input, transactional restore, prediction,
  artifact, publication, and validation behavior.

Glossary:
  Branch: Child replay timeline created from a restored source frame.
  Body store: Physics-owned live body records used for pose and velocity
    authority while legacy object-record mirrors are retired.
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
  - Full owner-state accessors are private. External render, input, UI, and
    validation code receives only named read-only publications or commands.
  - Published spans and references are frame-local and must not survive the
    next replay update.
  - Solver hash-log paths derive from the presentation path so paired artifacts
    stay beside each other.
  - Scene and branch reset edges wait for prediction workers before clearing
    replay-owned scratch.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
  - SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h
*/
#include "ReplayRuntime.h"
#include "../../Assets/AssetKeys.h"
#include "ReplayOverlayLayout.h"
#include "ReplayRetainedMemory.h"
#include "ReplayRestoreService.h"
#include "ReplayRuntimeOwnerViews.h"
#include "ReplayV2Artifact.h"
#include "ReplayPredictionArchive.h"
#include "../Diagnostics/DiagnosticsRuntime.h"
#include "../Allocation/RuntimeAllocationTracker.h"
#include "../RuntimeFileWriter.h"
#include "../InputRouter.h"
#include "../RuntimeInteractionCommands.h"
#include "../Tools/RuntimeTools.h"
#include "../../Core/AmortizedTask.h"
#include "../../Core/Profiler.h"
#include "../Scene/SceneController.h"
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

namespace SkullbonezCore::Runtime
{

namespace
{
constexpr uint32_t REPLAY_WORLD_OVERRIDE_GRAVITY_CHANGED = 1u;
constexpr uint32_t REPLAY_WORLD_OVERRIDE_FLUID_HEIGHT_CHANGED = 2u;
constexpr uint32_t REPLAY_WORLD_OVERRIDE_FLUID_DENSITY_CHANGED = 4u;
constexpr uint32_t REPLAY_LAUNCHER_FIRE_PROJECTILE = 1u;
constexpr uint32_t REPLAY_EDITOR_PLACE_FIXED = 1u;
constexpr uint32_t REPLAY_EDITOR_PLACE_TERRAIN_ALIGN = 2u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_TRANSLATE = 1u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_ROTATE = 2u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_SCALE = 4u;
constexpr uint64_t REPLAY_EVENT_FNV_OFFSET = 14695981039346656037ull;
constexpr uint64_t REPLAY_EVENT_FNV_PRIME = 1099511628211ull;
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

constexpr double REPLAY_PREDICTION_MAX_WORK_MILLISECONDS = 5.0;

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

void AddPredictionFrameCategoryBytes( SkullbonezCore::Core::MainMemoryReplayCategoryBytes& categories,
                                      const RunReplayPredictionFrame& frame )
{
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        categories,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionFrameBodies,
        VectorCapacityBytes( frame.bodies ) );
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        categories,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionDebugContacts,
        VectorCapacityBytes( frame.debugContacts ) );
}

uint64_t PredictionEngineMemoryBytes( const PhysicsEngine& engine )
{
    // Why: sizeof the prediction state only counts the unique_ptr. The private
    // prediction engine owns physics stores and solver scratch that must remain
    // visible in the replay memory overlay.
    uint64_t bytes = static_cast<uint64_t>( sizeof( engine ) );
    bytes += engine.CollectPhysicsWorldMemoryBytes();
    bytes += engine.CollectDebugAndBroadphaseMemoryBytes();
    bytes += static_cast<uint64_t>( SkullbonezCore::Physics::PhysicsEngine::ReadBodies( engine ).RecordCapacity() ) *
             sizeof( PhysicsBodyRecord );
    bytes += static_cast<uint64_t>( SkullbonezCore::Physics::PhysicsEngine::ReadColliders( engine ).RecordCapacity() ) *
             sizeof( ColliderRecord );
    return bytes;
}

bool ReplayRuntimeModelIsRagdollPart( std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords,
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


bool ReplayRuntimeQueueRenderPoseOverride( Rendering::RenderInstanceStore& renderInstances,
                                           const PhysicsBodyStore& bodyStore,
                                           const ColliderStore& colliderStore,
                                           ReplayBodyId replayBodyId,
                                           const Vector3& position,
                                           const Math::Orientation::Quaternion& orientation )
{
    const PhysicsBodyHandle body = bodyStore.HandleForReplayBodyId( replayBodyId.value );
    const PhysicsBodyRecord* record = bodyStore.RecordForHandle( body );
    const int modelIndex = bodyStore.ModelIndexForHandle( body );
    if ( !record || record->replayBodyId != replayBodyId.value || modelIndex < 0 )
    {
        return false;
    }
    return renderInstances.OverridePose( modelIndex, replayBodyId.value, position, orientation, colliderStore );
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
// pairing to stay off legacy object record pose and shape mirrors.
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

    const auto colliders = colliderStore.Records();
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
// ReplayPrediction writes in-progress worker output to branch 1 so presentation can draw
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
                                                  prediction.simulation.targetModelRow.value );
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
                                          ReplayFrameIndex frameIndex,
                                          const Math::Vector::Vector3& position )
{
    if ( !store.TryAppendPoint( record, { frameIndex, position } ) )
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
    Physics::ModelRowHint targetModelRow;
    ReplayFrameIndex firstFrame = 0;
    bool hasSample = false;
    bool ok = true;
};

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


void ReplayPrediction::ClearFutureNodeCache()
{
    m_state.futureNodeCache.futureNodes.clear();
    m_state.futureNodeCache.futureNodeBuildScratch.clear();
    m_state.futureNodeCache.futureNodesBuiltFrameCount = 0;
    m_state.futureNodeCache.futureNodesBuiltContactIndex = 0;
    m_state.futureNodeCache.futureNodesBuiltTargetId = ReplayBodyId{};
    m_state.futureNodeCache.futureNodesBuiltRagdollVisuals = m_state.ragdollVisualsEnabled;
    m_state.futureNodeCache.futureNodesBuiltFromBuildFrames = false;
    m_state.futureNodeCache.futureNodesCacheValid = false;
    m_state.futureNodeCache.retainedMarkerCount = 0;
    m_state.trajectoryBuild.childFrameCount = 0;
    m_state.trajectoryBuild.builtNodeCount = 0;
}


void ReplayPrediction::WaitForJobIdle()
{
    WaitForReplayPredictionWorkerIdle( m_state );
}


bool ReplayPrediction::PromoteBuildPrefixToCommitted()
{
    if ( !m_state.BuildPrefixShouldBePresented() )
    {
        return false;
    }
    WaitForJobIdle();
    const std::size_t promotedFrameCount = m_state.PublishedBuildFrameCount();
    if ( promotedFrameCount < 2u || promotedFrameCount > m_state.build.buildFrames.size() )
    {
        return false;
    }

    // Hazard: this is the Play-button ownership transfer. The worker has
    // released buildFrames before the visible prefix becomes committed state.
    m_state.build.workerTask.reset();
    m_state.build.building = false;
    m_state.build.complete = true;
    m_state.simulation.frames.swap( m_state.build.buildFrames );
    m_state.simulation.frames.resize( promotedFrameCount );
    m_state.ResetBuildFramePublication();
    if ( !RebuildReplayRuntimePredictionCommittedRootTrajectory( m_state ) )
    {
        return false;
    }
    m_state.simulation.predictionEngineReady = false;
    m_state.simulation.predictionBodies.clear();
    m_state.simulation.predictionWorld = ReplaySolverWorldSnapshot();
    return true;
}


void ReplayPrediction::CancelJob( bool clearSamples )
{
    WaitForJobIdle();
    m_state.build.workerTask.reset();
    m_state.build.building = false;
    m_state.build.complete = false;
    m_state.build.buildMode = ReplayPredictionBuildMode::Undecided;
    m_state.build.pendingLatestRestart = false;
    m_state.simulation.targetModelRow.value = -1;
    m_state.build.nextTick = 1;
    m_state.build.targetTickCount = 0;
    m_state.simulation.predictionEngineReady = false;
    m_state.simulation.predictionBodies.clear();
    m_state.simulation.predictionWorld = ReplaySolverWorldSnapshot();
    // Runtime allocation policy: cancellation invalidates publication but keeps
    // the double-buffered frame payloads warm for the next replay rebuild.
    m_state.ResetBuildFramePublication();
    m_state.trajectoryBuild = RunReplayPredictionTrajectoryBuildState{};
    if ( clearSamples )
    {
        m_state.build.supersededRestartCount = 0;
        m_state.build.latestRestartBeginCount = 0;
        m_state.simulation.measuredTicksPerMs.store( 0.0, std::memory_order_release );
        m_state.simulation.probeElapsedMs = 0.0;
        m_state.simulation.probeTicksCompleted = 0;
        m_state.simulation.calibratedModelCount = -1;
        m_state.simulation.frames.clear();
        m_state.trajectoryStore.Clear();
        ClearFutureNodeCache();
    }
}


void ReplayPrediction::ClearCache()
{
    CancelJob( true );
    m_state.simulation.targetId = ReplayBodyId{};
    m_state.simulation.sourceFrameIndex = 0;
    m_state.simulation.sourceSolverHash = 0;
    m_state.simulation.sourceSimulationSeconds = 0.0;
    m_state.build.lastBuildTime = 0.0;
    m_state.trajectoryBuild = RunReplayPredictionTrajectoryBuildState{};
    m_state.trajectoryStore.Clear();
    m_state.baseline = ReplayPredictionBaselineSnapshot{};
}


ReplayRuntime::ReplayRuntime() = default;


ReplayFrameIntentResult ReplayRuntime::ApplyFrameIntent( const ReplayFrameIntent& intent )
{
    ReplayFrameIntentResult result;
    if ( intent.setScrubberVisibility )
    {
        m_scrubberOwner.SetVisible( intent.scrubberVisible, intent.scrubberNow, intent.scrubberHoldSeconds );
    }
    if ( intent.setPredictionEnabled )
    {
        m_predictionOwner.SetEnabled( intent.predictionEnabled );
    }
    if ( intent.setPredictionHorizon )
    {
        m_predictionOwner.SetHorizonSeconds( intent.predictionHorizonSeconds );
    }
    if ( intent.prepareVelocityMutationBaseline )
    {
        result.velocityMutationBaselinePrepared = m_predictionOwner.PrepareVelocityMutationBaseline();
    }
    if ( intent.commitVelocityMutation )
    {
        m_predictionOwner.CommitVelocityMutation();
    }
    if ( intent.clearVelocityEditInputState )
    {
        m_authoring.ClearVelocityEditInputState();
    }
    if ( intent.queryDeterministicRevealReady )
    {
        result.deterministicRevealReady = m_predictionOwner.ReadyForDeterministicReveal();
    }
    if ( intent.armDeterministicReveal )
    {
        m_predictionOwner.ArmDeterministicReveal( intent.revealFrame, intent.resetPresentedRevealFrame );
    }
    if ( intent.applyPredictionRevealRate )
    {
        m_predictionOwner.SetRevealRatePreservingCursor( intent.predictionRevealRate );
    }
    if ( intent.setPathTarget &&
         m_visualPresentation.SetPathTarget( intent.pathTargetId, intent.pathTargetModelRow, intent.pathTargetName ) )
    {
        m_predictionOwner.ClearCache();
        m_predictionOwner.MarkDirty();
    }
    return result;
}


ReplaySceneTimelineResetInput DescribeReplaySceneTimeline( const SceneController& sceneController,
                                                           const RunSceneState& scene,
                                                           int gameModelCapacity,
                                                           uint32_t generatedObjectTypeOverride )
{
    const std::string* scenePath = sceneController.CurrentPath();
    const char* sceneLabel = scenePath && !scenePath->empty() ? scenePath->c_str() : "generated";
    ReplaySceneTimelineResetInput replayReset;
    replayReset.sceneLabel = sceneLabel;
    replayReset.isSceneMode = scene.isSceneMode;
    replayReset.modelCount = scene.modelCount;
    replayReset.solverBallCount = scene.solverBallCount;
    replayReset.solverBoxCount = scene.solverBoxCount;
    replayReset.rngSeed = scene.rngSeed;
    replayReset.gameModelCapacity = gameModelCapacity;
    replayReset.generatedObjectTypeOverride = generatedObjectTypeOverride;
    replayReset.hasUiModelCountOverride = sceneController.UIOverrides().modelCountOverride >= 0;
    replayReset.hasUiSolverCountOverride = sceneController.UIOverrides().solverBallCountOverride >= 0 ||
                                           sceneController.UIOverrides().solverBoxCountOverride >= 0;
    return replayReset;
}


bool ReplayRuntime::RestoreSolverSampleAsLive( const ReplayRestoreTransaction& transaction,
                                               const ReplaySolverFrameSample& sample,
                                               char* outReason,
                                               std::size_t reasonSize )
{
    auto writeReason = [outReason, reasonSize]( const char* message )
    {
        if ( outReason && reasonSize > 0 )
        {
            strncpy_s( outReason, reasonSize, message ? message : "restore failed", _TRUNCATE );
        }
    };

    ReplaySolverFrameSample liveBackup;
    if ( !ReplayRestoreService::CaptureCurrentSolverSample( transaction.sampleOwners, sample, liveBackup ) )
    {
        writeReason( "failed to capture live replay backup" );
        return false;
    }

    char applyReason[128] = {};
    if ( !ReplayRestoreService::ApplySolverSampleState( transaction.sampleOwners,
                                                        sample,
                                                        applyReason,
                                                        sizeof( applyReason ) ) )
    {
        writeReason( applyReason[0] != '\0' ? applyReason : "restore apply failed" );
        return false;
    }

    uint64_t restoredSolverHash = 0;
    uint64_t restoredPresentationHash = 0;
    std::size_t restoredBodyCount = 0;
    const bool hashCaptured = ReplayRestoreService::CaptureCurrentSolverHash( transaction.sampleOwners,
                                                                              sample,
                                                                              restoredSolverHash,
                                                                              restoredPresentationHash,
                                                                              restoredBodyCount );
    const bool hashMatched = hashCaptured && restoredSolverHash == sample.solverHash;
    bool fallbackRestored = false;
    if ( !hashMatched )
    {
        char fallbackReason[128] = {};
        fallbackRestored = ReplayRestoreService::ApplySolverSampleState( transaction.sampleOwners,
                                                                         liveBackup,
                                                                         fallbackReason,
                                                                         sizeof( fallbackReason ) );
    }

#ifdef _DEBUG
    transaction.diagnostics.LogReplayRestoreProbe( transaction.sampleOwners.scene,
                                                   sample,
                                                   restoredSolverHash,
                                                   restoredPresentationHash,
                                                   restoredBodyCount,
                                                   hashCaptured,
                                                   hashMatched,
                                                   !hashMatched,
                                                   fallbackRestored );
#endif

    // Hazard: a recoverable restore failure may return only after the live
    // backup was reapplied. Continuing from a half-restored solver would make
    // later physics output nondeterministic, so rollback failure is Lane F.
    if ( !hashMatched && !fallbackRestored )
    {
        SB_FATAL( "Runtime/ReplayRestore",
                  "Replay restore verification failed and the live backup could not be restored" );
    }
    if ( !hashCaptured )
    {
        writeReason( "restore hash capture failed" );
        return false;
    }
    if ( !hashMatched )
    {
        writeReason( fallbackRestored ? "restore hash mismatch; live state restored"
                                      : "restore hash mismatch; fallback unavailable" );
        return false;
    }

    const uint32_t parentBranchId =
        m_authoring.BeginRestoredBranch( sample.branch, sample.frameIndex, sample.solverHash );
    ReplaySceneTimelineResetInput reset = transaction.timelineReset;
    reset.preserveBranchMetadata = true;
    ResetSceneTimeline( reset, transaction.timelineOwners );
    RecordEvent( ReplayEventKind::BranchRestore,
                 0,
                 0,
                 static_cast<int32_t>( parentBranchId ),
                 sample.sceneFrame,
                 0,
                 0,
                 sample.solverHash,
                 "hash-verified solver restore" );
    writeReason( "restored hash match" );
    return true;
}


void ReplayRuntime::AppendOverlayTrace( PhysicsEngine& physics,
                                        const SceneEntityStore& entities,
                                        RunEditorTracer& tracer,
                                        const ReplayOverlayBuildInput& input )
{
    const ReplayPredictionPresentationView prediction = m_predictionOwner.PresentationView();
    const ReplaySolverFrameSample* currentSolverSample = CurrentSolverScrubSample();
    const ReplaySolverFrameSample* presentSample = currentSolverSample;
    if ( !presentSample )
    {
        presentSample = m_timeline.Solver().LatestSample();
    }
    m_visualPresentation.RenderPathVisualizer( prediction, presentSample, physics, entities, tracer );
    const PhysicsBodyStore& bodyStore = Physics::PhysicsEngine::ReadBodies( physics );
    const ColliderStore& colliderStore = Physics::PhysicsEngine::ReadColliders( physics );
    m_visualPresentation.RenderCauseFocusOverlay( m_authoring.CauseTree(),
                                                  prediction,
                                                  currentSolverSample,
                                                  bodyStore,
                                                  colliderStore,
                                                  entities,
                                                  tracer );
    const RunReplayPathVisualizerState& path = m_visualPresentation.PathVisualizer();
    m_authoring.AppendVelocityEditOverlay( path.targetId,
                                           path.targetModelRow,
                                           physics,
                                           input.editorModeEnabled,
                                           input.gesture,
                                           tracer );
}


const ReplayRecorder& ReplayRuntime::Presentation() const
{
    return m_timeline.Presentation();
}

const ReplaySolverRecorder& ReplayRuntime::Solver() const
{
    return m_timeline.Solver();
}

const ReplayEventRecorder& ReplayRuntime::Events() const
{
    return m_timeline.Events();
}

const ReplayBranchInfo& ReplayRuntime::Branch() const
{
    return m_authoring.Branch();
}

const RunLoadedReplayPresentationState& ReplayRuntime::LoadedPresentation() const
{
    return m_timeline.LoadedPresentation();
}

ReplayScrubberView ReplayRuntime::ScrubberView() const noexcept
{
    return m_scrubberOwner.View();
}


ReplayInputView ReplayRuntime::BuildInputView() const noexcept
{
    const ReplayScrubberView scrubber = m_scrubberOwner.View();
    const RunReplayCameraState camera = m_visualPresentation.CameraView();
    const RunReplayPathVisualizerState& path = m_visualPresentation.PathVisualizer();

    ReplayInputView view;
    view.activeInteraction = HasActiveInteractionState();
    view.inspectionCameraActive = camera.active;
    view.inspectionActive = camera.active || scrubber.historicalSamplePaused || scrubber.liveAdvanceHeld;
    view.restoreConsumedThisFrame = scrubber.restoreConsumedThisFrame;
    view.scrubPaused = scrubber.historicalSamplePaused;
    view.liveAdvanceHeld = scrubber.liveAdvanceHeld;
    view.velocityEditEnabled = m_authoring.VelocityEdit().enabled;
    view.predictionEnabled = m_predictionOwner.State().enabled;
    view.captureEnabled = m_timeline.Presentation().IsEnabled() || m_timeline.Solver().IsEnabled();
    view.hasPathTarget = path.hasTarget;
    view.hasCameraFocus = camera.focusKind != RunReplayCameraFocusKind::None;
    view.restoreCameraMode = camera.restoreCameraMode;
    view.pathTargetModelRow = path.hasTarget ? path.targetModelRow.value : -1;
    view.solverPresentTrackPosition = SolverPresentTrackPosition();
    view.predictionRevealAvailable = m_predictionOwner.RevealProgress01( view.predictionRevealProgress );
    return view;
}


ReplayAutomationView ReplayRuntime::BuildAutomationView() const
{
    return { m_predictionOwner.State(),
             m_visualPresentation.PathVisualizer(),
             m_timeline.Presentation(),
             m_timeline.Solver(),
             m_timeline.Events(),
             ActivePredictionFrames(),
             m_scrubberOwner.View(),
             m_timeline.Solver().GetStats(),
             m_timeline.Solver().LatestSample(),
             CurrentSolverScrubSample(),
             CurrentPredictionScrubFrame(),
             m_visualPresentation.PublishedVisualPacketView(),
             m_visualPresentation.TrajectorySubmissionProbeSnapshot(),
             CollectMemoryStats(),
             BuildInputView(),
             TrackPosition( RunReplayTrack::Solver ),
             SolverPresentTrackPosition() };
}


ReplayOverlay::ReplayOverlayStateView
ReplayRuntime::BuildOverlayStateView( bool editorModeEnabled,
                                      bool uiVisible,
                                      bool uiMinimized,
                                      RuntimeInteractionGestureKind gesture ) const
{
    const ReplayScrubberView scrubber = m_scrubberOwner.View();
    const bool loadedPresentation = HasLoadedPresentation();
    const RunReplayTrack overlayTrack = loadedPresentation ? RunReplayTrack::Presentation : RunReplayTrack::Solver;
    const float overlayTrackPosition = TrackPosition( overlayTrack );
    const float solverPresentTrackPosition = SolverPresentTrackPosition();
    const bool futureSelected =
        !loadedPresentation && ReplayTrackPositionIsFuture( overlayTrackPosition, solverPresentTrackPosition );
    const ReplayPresentationSample* selectedPresentation =
        loadedPresentation ? LoadedPresentationSampleAtNormalized( overlayTrackPosition ) : nullptr;
    const ReplayPresentationSample* latestPresentation =
        loadedPresentation ? LoadedPresentationLatestSample() : nullptr;
    const ReplaySolverFrameSample* selectedSolver =
        ( loadedPresentation || futureSelected )
            ? nullptr
            : m_timeline.Solver().SampleAtNormalized(
                  ReplaySolverNormalizedFromTrack( overlayTrackPosition, solverPresentTrackPosition ) );
    const ReplaySolverFrameSample* latestSolver = loadedPresentation ? nullptr : m_timeline.Solver().LatestSample();

    return { scrubber,
             m_predictionOwner.PresentationView(),
             m_visualPresentation.PathVisualizer(),
             m_authoring.VelocityEdit(),
             m_authoring.CauseTree(),
             m_timeline.Solver().GetStats(),
             selectedPresentation,
             latestPresentation,
             selectedSolver,
             latestSolver,
             futureSelected ? CurrentPredictionScrubFrame() : nullptr,
             CurrentScrubSample(),
             CurrentSolverScrubSample(),
             solverPresentTrackPosition,
             loadedPresentation,
             ActivePredictionFrames().size() >= 2 || m_predictionOwner.State().BuildPrefixShouldBePresented(),
             ShouldRenderScrubber( editorModeEnabled, uiVisible, uiMinimized, gesture ) };
}


ReplayRenderSelectionView ReplayRuntime::BuildRenderSelectionView() const
{
    const ReplayPresentationSample* presentationSample = CurrentScrubSample();
    return { presentationSample,
             presentationSample ? nullptr : CurrentSolverScrubSample(),
             CurrentPredictionScrubFrame() };
}

ReplayRenderFrameView
ReplayRuntime::PrepareRenderFrame( Rendering::RenderInstanceStore& renderInstances,
                                   std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords,
                                   PhysicsEngine& physics,
                                   const SceneEntityStore& entities,
                                   RuntimeTools& runtimeTools,
                                   RunEditorTracer& tracer,
                                   int modelCount,
                                   bool editorModeEnabled,
                                   const RuntimeInteractionGesture& gesture,
                                   int sceneFrame,
                                   bool collisionVisualizer,
                                   bool debugTransparentBodyPass,
                                   const Math::Vector::Vector3& cameraTranslation,
                                   const Math::Vector::Vector3& cameraUp,
                                   uint64_t replayReserveGrowthEvents )
{
    const ReplayRenderSelectionView selection = BuildRenderSelectionView();
    const RunReplayPredictionFrame* predictionFrame = selection.predictionFrame;
    const ReplayPresentationSample* presentationSample = selection.presentationSample;
    const ReplaySolverFrameSample* solverSample = selection.solverSample;
    const ReplayPredictionPresentationView prediction = m_predictionOwner.PresentationView();

    {
        Allocation::RuntimeAllocationScope replayAllocationScope( Allocation::RuntimeAllocationPhase::Replay );
        if ( predictionFrame )
        {
            m_visualPresentation.ApplyPredictionFrameForRender( renderInstances,
                                                                PhysicsEngine::ReadBodies( physics ),
                                                                PhysicsEngine::ReadColliders( physics ),
                                                                *predictionFrame );
        }
        else if ( presentationSample )
        {
            m_visualPresentation.ApplyPresentationSampleForRender( renderInstances,
                                                                   PhysicsEngine::ReadBodies( physics ),
                                                                   PhysicsEngine::ReadColliders( physics ),
                                                                   *presentationSample );
        }
        else if ( solverSample )
        {
            m_visualPresentation.ApplySolverSampleForRender( renderInstances,
                                                             PhysicsEngine::ReadBodies( physics ),
                                                             PhysicsEngine::ReadColliders( physics ),
                                                             *solverSample );
            if ( !m_visualPresentation.HasLauncherVisualBackup() )
            {
                m_visualPresentation.StoreLauncherVisualBackupFrom( runtimeTools );
                runtimeTools.RestoreReplayLauncherVisualSample( solverSample->launcherVisual );
            }
        }
    }

    AppendOverlayTrace( physics, entities, tracer, ReplayOverlayBuildInput{ editorModeEnabled, gesture, sceneFrame } );
    (void)m_visualPresentation.BuildPredictionGhostDrawRequests( prediction,
                                                                 presentationRecords,
                                                                 PhysicsEngine::ReadBodies( physics ) );
    ReplayVisualPacket packet = tracer.BuildReplayVisualPacket( cameraTranslation, cameraUp );
    m_visualPresentation.PublishVisualPacket( packet,
                                              prediction,
                                              m_timeline.Solver().LatestSample(),
                                              replayReserveGrowthEvents );

    const ReplayInputView input = BuildInputView();
    bool focusFadeActive = false;
    if ( !input.predictionEnabled && !collisionVisualizer && !debugTransparentBodyPass )
    {
        Allocation::RuntimeAllocationScope replayAllocationScope( Allocation::RuntimeAllocationPhase::Replay );
        const std::span<const RunReplayPathTraceNode> focusNodes =
            prediction.enabled
                ? prediction.futureNodes
                : std::span<const RunReplayPathTraceNode>( m_visualPresentation.PathVisualizer().futureNodes );
        focusFadeActive =
            m_visualPresentation.BuildFocusModelMask( PhysicsEngine::ReadBodies( physics ), modelCount, focusNodes );
    }

    return { presentationSample,
             solverSample,
             ( presentationSample || solverSample ) ? nullptr : predictionFrame,
             &m_visualPresentation.PublishedVisualPacketView(),
             focusFadeActive ? &m_visualPresentation.FocusModelMaskView() : nullptr,
             input.predictionEnabled,
             input.liveAdvanceHeld,
             focusFadeActive };
}

void ReplayRuntime::CompleteRenderFrame( bool submissionRendered,
                                         int sceneFrame,
                                         uint64_t replayReserveGrowthEvents,
                                         RuntimeTools& runtimeTools )
{
    if ( submissionRendered )
    {
        m_visualPresentation.RecordTrajectorySubmissionFrame(
            m_visualPresentation.PublishedVisualPacketView().submission,
            sceneFrame,
            replayReserveGrowthEvents );
    }
    CancelRenderFrame( runtimeTools );
}

void ReplayRuntime::CancelRenderFrame( RuntimeTools& runtimeTools )
{
    if ( m_visualPresentation.HasLauncherVisualBackup() )
    {
        m_visualPresentation.RestoreAndClearLauncherVisualBackup( runtimeTools );
    }
}

ReplayVisualPacket ReplayRuntime::BuildVisualProjectionForValidation(
    PhysicsEngine& physics,
    const SceneEntityStore& entities,
    std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords,
    const PhysicsBodyStore& bodyStore,
    RuntimeTools& runtimeTools,
    const Math::Vector::Vector3& cameraEye,
    const Math::Vector::Vector3& cameraUp,
    uint64_t replayReserveGrowthEvents )
{
    RunEditorTracer& tracer = runtimeTools.EditorTracer();
    AppendOverlayTrace(
        physics,
        entities,
        tracer,
        ReplayOverlayBuildInput{ runtimeTools.Editor().editorModeEnabled, RuntimeInteractionGesture{}, 0 } );
    const ReplayPredictionPresentationView prediction = m_predictionOwner.PresentationView();
    (void)m_visualPresentation.BuildPredictionGhostDrawRequests( prediction, presentationRecords, bodyStore );
    ReplayVisualPacket packet = tracer.BuildReplayVisualPacket( cameraEye, cameraUp );
    m_visualPresentation.PublishVisualPacket( packet,
                                              prediction,
                                              m_timeline.Solver().LatestSample(),
                                              replayReserveGrowthEvents );
    return m_visualPresentation.PublishedVisualPacketView();
}

RunCameraMode ReplayRuntime::ReplayRestoreCameraMode() const noexcept
{
    return m_visualPresentation.CameraView().restoreCameraMode;
}

bool ReplayRuntime::ReplayCameraActive() const noexcept
{
    return m_visualPresentation.CameraView().active;
}

const RunReplayPathVisualizerState& ReplayRuntime::PathVisualizer() const
{
    return m_visualPresentation.PathVisualizer();
}

const RunReplayPredictionState& ReplayRuntime::Prediction() const
{
    return m_predictionOwner.State();
}

ReplayPredictionPresentationView ReplayRuntime::PredictionPresentationView() const
{
    return m_predictionOwner.PresentationView();
}


std::span<const RunReplayPredictionFrame> ReplayRuntime::ActivePredictionFrames() const
{
    return m_predictionOwner.ActiveFrames();
}

void ReplayRuntime::ApplyAuthoringPredictionRequest()
{
    const ReplayAuthoringPredictionRequest request = m_authoring.TakePredictionRequest();
    m_predictionOwner.ApplyAuthoringRequest( request.enablePrediction,
                                             request.refreshPrediction,
                                             ReplayOverlay::REPLAY_PREDICTION_MIN_SECONDS,
                                             ReplayOverlay::REPLAY_PREDICTION_MAX_SECONDS );
}

void ReplayRuntime::EnterOfflinePredictionVerification()
{
    // Invariant: this is a one-way terminal capability transition for a CLI
    // validation process. It does not clear the frozen prediction because the
    // caller immediately replaces it from RVPD, and no render frame follows.
    m_predictionOwner.EnterOfflineVerification();
    m_visualPresentation.ResetTrajectoryVisualStats();
}


bool ReplayRuntime::LoadPredictionArchiveForVerification( std::span<const uint8_t> bytes,
                                                          char* outReason,
                                                          std::size_t reasonSize )
{
    RunReplayPathVisualizerState archivePath;
    if ( !m_predictionOwner.LoadArchive( bytes, archivePath, outReason, reasonSize ) )
    {
        return false;
    }
    m_visualPresentation.ApplyArchivePathState( archivePath );
    return true;
}


bool ReplayRuntime::BuildPredictionArchiveForValidation( std::vector<uint8_t>& outBytes ) const
{
    return m_predictionOwner.BuildArchive( m_visualPresentation.PathVisualizer(), outBytes );
}


void ReplayRuntime::ResetPredictionPresentationVerification()
{
    m_visualPresentation.ResetTrajectoryVisualStats();
    m_predictionOwner.ResetVerificationMarkers();
}


void ReplayRuntime::ClearPathVisualizerState()
{
    m_visualPresentation.ClearPathState();
    m_authoring.ResetCauseTreeRows();
    m_predictionOwner.ClearCache();
    m_predictionOwner.MarkDirty();
}

bool ReplayRuntime::SetPathTarget( const char* name, int modelIndex, const PhysicsBodyStore& bodyStore )
{
    if ( !m_visualPresentation.SetPathTarget( name, modelIndex, bodyStore ) )
    {
        return false;
    }
    m_predictionOwner.ClearCache();
    m_predictionOwner.MarkDirty();
    return true;
}


ReplayPathPickResult
ReplayRuntime::ApplyPathPick( const ReplayPathPickInput& input,
                              const SceneEntityStore& entities,
                              const PhysicsBodyStore& bodyStore,
                              const ColliderStore& colliderStore,
                              std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords )
{
    const ReplayPathPickResult result = m_visualPresentation.TryPickPathTarget( input,
                                                                                entities,
                                                                                bodyStore,
                                                                                colliderStore,
                                                                                presentationRecords,
                                                                                CurrentSolverScrubSample() );
    if ( result.picked )
    {
        m_predictionOwner.ClearCache();
        m_predictionOwner.MarkDirty();
    }
    else if ( result.exitInspectionCamera )
    {
        ClearCameraFocusForRestore();
        ClearPathVisualizerState();
    }
    return result;
}


bool ReplayRuntime::RouteWorldPointer( const ReplayWorldPointerInput& input,
                                       const SceneEntityStore& entities,
                                       const Physics::PhysicsBodyStore& bodyStore,
                                       const Physics::ColliderStore& colliderStore,
                                       std::span<const Rendering::RenderInstancePresentationRecord> presentation,
                                       Environment::CameraCollection* cameras,
                                       Geometry::Terrain* terrain,
                                       RunCameraState& camera,
                                       RuntimeInteractionController& interaction,
                                       InputRouter& inputRouter )
{
    if ( !input.leftPressed || input.suppressWorldAction || input.editorMode || input.uiWantsNativeCursor ||
         ( !input.controlDown && input.launcherMode ) )
    {
        return false;
    }

    const ReplayPathPickResult pickResult =
        ApplyPathPick( input.pick, entities, bodyStore, colliderStore, presentation );
    if ( pickResult.exitInspectionCamera )
    {
        ExitInspectionCamera( cameras,
                              terrain,
                              camera,
                              input.restoreCameraMode,
                              input.attachedCameraFollow,
                              input.directorGrabbed,
                              interaction,
                              inputRouter );
    }
    return true;
}

bool ReplayRuntime::BeginToolGesture( RuntimeInteractionController& interaction,
                                      RuntimeInteractionGestureKind kind,
                                      WorldInteractionOwner owner,
                                      RuntimePointerButton button,
                                      int startX,
                                      int startY,
                                      PhysicsBodyHandle body,
                                      int axis,
                                      bool angular )
{
    interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                     owner,
                                                     InteractionExitReason::BeginGesture );
    RuntimeInteractionGesture gesture;
    gesture.kind = kind;
    gesture.button = button;
    gesture.startX = startX;
    gesture.startY = startY;
    gesture.body = body;
    gesture.axis = axis;
    gesture.angular = angular;
    RuntimeGestureCommand command;
    command.gesture = gesture;
    RuntimeGestureEvent event;
    return interaction.ApplyGestureCommand( command, event );
}

void ReplayRuntime::EndToolGesture( RuntimeInteractionController& interaction, RuntimeInteractionGestureKind kind )
{
    if ( interaction.Gesture().kind == kind )
    {
        RuntimeGestureCommand command;
        command.action = RuntimeGestureCommandAction::End;
        command.gesture.kind = kind;
        command.reason = InteractionExitReason::EndGesture;
        RuntimeGestureEvent event;
        (void)interaction.ApplyGestureCommand( command, event );
    }
}

void ReplayRuntime::CancelToolGesture( RuntimeInteractionController& interaction )
{
    switch ( interaction.Gesture().kind )
    {
    case RuntimeInteractionGestureKind::ReplayScrubDrag:
    case RuntimeInteractionGestureKind::ReplayVelocityDrag:
    case RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag:
    case RuntimeInteractionGestureKind::ReplayCauseTreeDrag:
    {
        RuntimeGestureCommand command;
        command.action = RuntimeGestureCommandAction::End;
        command.gesture.kind = interaction.Gesture().kind;
        command.reason = InteractionExitReason::EndGesture;
        RuntimeGestureEvent event;
        (void)interaction.ApplyGestureCommand( command, event );
        break;
    }
    default:
        break;
    }
}

void ReplayRuntime::CancelToolDragState( RuntimeInteractionController& interaction, InputRouter& inputRouter )
{
    const RuntimeInteractionGestureKind gesture = interaction.Gesture().kind;
    const bool ownsReplayCapture = interaction.PointerCapture() == RuntimePointerCaptureOwner::ToolGesture &&
                                   ( gesture == RuntimeInteractionGestureKind::ReplayScrubDrag ||
                                     gesture == RuntimeInteractionGestureKind::ReplayVelocityDrag ||
                                     gesture == RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag ||
                                     gesture == RuntimeInteractionGestureKind::ReplayCauseTreeDrag );
    CancelToolGesture( interaction );
    if ( ownsReplayCapture )
    {
        inputRouter.ReleaseNativeCapture();
    }
}

bool ReplayRuntime::HasActiveInteractionState() const
{
    const ReplayScrubberView scrubber = m_scrubberOwner.View();
    const RunReplayCameraState camera = m_visualPresentation.CameraView();
    return camera.active || camera.focusKind != RunReplayCameraFocusKind::None || scrubber.historicalSamplePaused ||
           scrubber.liveAdvanceHeld || m_visualPresentation.PathVisualizer().hasTarget ||
           !m_visualPresentation.PathVisualizer().targets.empty() || m_predictionOwner.State().enabled ||
           m_predictionOwner.State().build.building || m_authoring.VelocityEdit().enabled ||
           m_authoring.CauseTree().selectedRow >= 0 || !m_authoring.CauseTree().rows.empty();
}


bool ReplayRuntime::ApplyInteractionExit( const ReplayInteractionExitInput& input,
                                          Environment::CameraCollection* cameras,
                                          Geometry::Terrain* terrain,
                                          RunCameraState& camera,
                                          RuntimeInteractionController& interaction,
                                          InputRouter& inputRouter )
{
    if ( !input.leavingReplayWorkspace || ( !HasActiveInteractionState() && !input.previousOwnerWasReplay ) )
    {
        return false;
    }

    if ( ClearInteractionForRuntimeTransition( interaction, inputRouter ) )
    {
        ExitInspectionCamera( cameras,
                              terrain,
                              camera,
                              input.normalizedRestoreMode,
                              input.attachedFollow,
                              input.directorGrabbed,
                              interaction,
                              inputRouter );
    }
    return true;
}


void ReplayRuntime::ApplyInputFocusLoss( Environment::CameraCollection* cameras,
                                         Geometry::Terrain* terrain,
                                         RunCameraState& camera,
                                         RunCameraMode normalizedRestoreMode,
                                         bool attachedFollow,
                                         bool directorGrabbed,
                                         RuntimeInteractionController& interaction,
                                         InputRouter& inputRouter )
{
    CancelToolDragState( interaction, inputRouter );
    if ( ResetScrubberState() )
    {
        ExitInspectionCamera( cameras,
                              terrain,
                              camera,
                              normalizedRestoreMode,
                              attachedFollow,
                              directorGrabbed,
                              interaction,
                              inputRouter );
    }
    m_authoring.ClearVelocityEditInputState();
}


void ReplayRuntime::ClearInteractionForSceneLoad( const ReplaySceneTimelineResetOwners& owners )
{
    const RuntimeInteractionTransition transition =
        owners.interaction.ResetForScene( InteractionExitReason::LoadScene );
    const bool previousOwnerWasReplay = transition.previousOwner == WorldInteractionOwner::ReplayScrub ||
                                        transition.previousOwner == WorldInteractionOwner::ReplayVelocityEdit ||
                                        transition.previousOwner == WorldInteractionOwner::ReplayPrediction ||
                                        transition.previousOwner == WorldInteractionOwner::ReplayBranchTarget ||
                                        transition.previousOwner == WorldInteractionOwner::ReplayCauseTree;
    if ( !HasActiveInteractionState() && !previousOwnerWasReplay )
    {
        return;
    }
    if ( ClearInteractionForRuntimeTransition( owners.interaction, owners.inputRouter ) )
    {
        ExitInspectionCamera( owners.cameras,
                              owners.terrain,
                              owners.camera,
                              owners.normalizedRestoreMode,
                              owners.attachedFollow,
                              owners.directorGrabbed,
                              owners.interaction,
                              owners.inputRouter );
    }
}

bool ReplayRuntime::ClearInteractionForRuntimeTransition( RuntimeInteractionController& interaction,
                                                          InputRouter& inputRouter )
{
    CancelToolDragState( interaction, inputRouter );
    SetLiveAdvanceHeld( false );
    const bool exitInspectionCamera = ResetScrubberState() || m_visualPresentation.CameraView().active;
    SetAllTrackPositions( 1.0f );
    m_scrubberOwner.HideSurface();
    ClearCameraFocusForRestore();
    ClearPathVisualizerState();
    m_predictionOwner.DisableAndClearCache();
    m_authoring.ResetVelocityEdit();
    m_authoring.ResetCauseTreeRows();
    return exitInspectionCamera;
}

const RunReplayCauseTreeState& ReplayRuntime::CauseTree() const
{
    return m_authoring.CauseTree();
}

const RunReplayVelocityEditState& ReplayRuntime::VelocityEdit() const
{
    return m_authoring.VelocityEdit();
}

bool ReplayRuntime::SetVelocityEditEnabled( bool enabled )
{
    PROFILE_SCOPED( "Frame/Replay/VelocityEdit/Toggle" );
    if ( !m_authoring.SetVelocityEditEnabled( enabled ) )
    {
        return false;
    }
    ApplyAuthoringPredictionRequest();
    return true;
}

ReplayKeyboardVelocityEditResult
ReplayRuntime::ApplyKeyboardVelocityEdit( const ReplayKeyboardVelocityEditInput& input )
{
    ReplayKeyboardVelocityEditResult result;
    if ( input.toggleAllowed && input.altDown && !m_authoring.VelocityEdit().keyboardAltWasDown )
    {
        const bool enableVelocityEdit = !m_authoring.VelocityEdit().enabled;
        if ( SetVelocityEditEnabled( enableVelocityEdit ) )
        {
            result.cancelToolDrag = true;
            if ( enableVelocityEdit )
            {
                result.enterInteractive = true;
                if ( SetLiveAdvanceHeld( true ) )
                {
                    result.cameraAction = ShouldUseInspectionCamera()
                                              ? ReplayKeyboardVelocityEditCameraAction::EnterInspection
                                              : ReplayKeyboardVelocityEditCameraAction::ExitInspection;
                }
                result.setWorldOwner = true;
                result.worldOwner = WorldInteractionOwner::ReplayVelocityEdit;
            }
            else if ( input.currentWorldOwner == WorldInteractionOwner::ReplayVelocityEdit )
            {
                result.setWorldOwner = true;
                result.worldOwner = WorldInteractionOwner::ReplayScrub;
            }
        }
        m_scrubberOwner.KeepVisible( input.now, ReplayOverlay::REPLAY_SCRUBBER_VISIBLE_SECONDS );
    }
    m_authoring.ObserveVelocityEditAltKey( input.altDown );
    return result;
}

float ReplayRuntime::TrackPosition( RunReplayTrack track ) const
{
    return m_scrubberOwner.TrackPosition( track );
}

void ReplayRuntime::SetTrackPosition( RunReplayTrack track, float position )
{
    m_scrubberOwner.SetTrackPosition( track, position );
}

void ReplayRuntime::PinSolverScrubberToPresent()
{
    m_scrubberOwner.SetTrackPosition( RunReplayTrack::Solver, SolverPresentTrackPosition() );
    if ( m_scrubberOwner.View().activeTrack == RunReplayTrack::Solver )
    {
        m_scrubberOwner.SetHistoricalSamplePaused( false );
    }
}

void ReplayRuntime::SetAllTrackPositions( float position )
{
    m_scrubberOwner.SetAllTrackPositions( position );
}

bool ReplayRuntime::ResetScrubberState()
{
    return m_scrubberOwner.ResetState( m_visualPresentation.CameraView().active );
}


ReplayScrubberInputFrame
ReplayRuntime::BeginReplayScrubberInputFrame( bool leftPressed, bool leftReleased, bool restoreDown )
{
    return m_scrubberOwner.BeginInputFrame( leftPressed, leftReleased, restoreDown );
}


ReplayScrubberUnavailableResult ReplayRuntime::ResetUnavailableScrubberSurface( bool loadedPresentation )
{
    return m_scrubberOwner.ResetUnavailableSurface( loadedPresentation, m_visualPresentation.CameraView().active );
}


void ReplayRuntime::ClearCauseTreeFocusSelection()
{
    ClearCameraFocusForRestore();
    ClearPathVisualizerState();
}


bool ReplayRuntime::SetLiveAdvanceHeld( bool held )
{
    if ( !m_scrubberOwner.SetLiveAdvanceHeld( held ) )
    {
        if ( !held )
        {
            m_visualPresentation.SetCameraPauseOwnership( false );
        }
        return false;
    }

    if ( !held )
    {
        m_visualPresentation.SetCameraPauseOwnership( false );
    }
    return true;
}

bool ReplayRuntime::LiveAdvanceHeld() const
{
    return m_scrubberOwner.LiveAdvanceHeld();
}

bool ReplayRuntime::HasPathVisualizerTarget() const
{
    return m_visualPresentation.PathVisualizer().hasTarget;
}

bool ReplayRuntime::HasCameraFocus() const
{
    return m_visualPresentation.CameraView().focusKind != RunReplayCameraFocusKind::None;
}

bool ReplayRuntime::VelocityEditActive() const
{
    return m_authoring.VelocityEdit().enabled;
}

float ReplayRuntime::SolverPresentTrackPosition() const
{
    return ReplayRuntimeScrubberPresentTrackPosition( m_timeline.Solver().GetStats(), m_predictionOwner.State() );
}

bool ReplayRuntime::ShouldRenderScrubber( bool editorModeEnabled,
                                          bool uiVisible,
                                          bool uiMinimized,
                                          RuntimeInteractionGestureKind gesture ) const
{
    if ( editorModeEnabled || !uiVisible || !uiMinimized )
    {
        return false;
    }

    const bool loadedPresentation = HasLoadedPresentation();
    const ReplayRecorderStats solverReplayStats = m_timeline.Solver().GetStats();
    const bool solverReplayEnabled = solverReplayStats.enabled;
    const ReplayScrubberView scrubber = m_scrubberOwner.View();
    // Why: visibility is about whether a replay control surface is armed, not
    // whether enough retained frames exist to enable scrub/prediction tools.
    return ( loadedPresentation || solverReplayEnabled ) &&
           ( scrubber.visible || gesture == RuntimeInteractionGestureKind::ReplayScrubDrag ||
             gesture == RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag || scrubber.historicalSamplePaused ||
             scrubber.liveAdvanceHeld );
}

bool ReplayRuntime::ShouldUseInspectionCamera() const
{
    const ReplayScrubberView scrubber = m_scrubberOwner.View();
    return scrubber.historicalSamplePaused || scrubber.liveAdvanceHeld ||
           m_visualPresentation.CameraView().focusKind != RunReplayCameraFocusKind::None;
}

bool ReplayRuntime::InspectionActive() const
{
    const ReplayScrubberView scrubber = m_scrubberOwner.View();
    return m_visualPresentation.CameraView().active || scrubber.historicalSamplePaused || scrubber.liveAdvanceHeld;
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
    m_predictionOwner.DisableForLiveAdvance();
    m_authoring.ResetVelocityEdit();
    m_scrubberOwner.ArmLoadedPresentation( normalized, now, ReplayOverlay::REPLAY_SCRUBBER_VISIBLE_SECONDS );
    return true;
}

void ReplayRuntime::ClearCameraFocusForRestore()
{
    const bool ownedSimulationPause = m_visualPresentation.ClearCameraFocus();
    m_authoring.ClearCauseTreeFocus();

    const ReplayScrubberView scrubber = m_scrubberOwner.View();
    if ( ownedSimulationPause && scrubber.liveAdvanceHeld && !scrubber.historicalSamplePaused )
    {
        m_scrubberOwner.SetLiveAdvanceHeld( false );
    }
}

ReplayRecordingConfigResult ReplayRuntime::ConfigureRecording( bool enabled,
                                                               int retentionSeconds,
                                                               const char* hashLogPath,
                                                               int runtimeBodyCapacity )
{
    m_visualPresentation.ReserveLauncherVisualCaptureBuffers();
    const ReplayRecordingConfigResult result =
        m_timeline.ConfigureRecording( enabled, retentionSeconds, hashLogPath, runtimeBodyCapacity );
    if ( result.presentationConfig.enabled )
    {
        // Runtime allocation policy: presentation buffers reserve during replay
        // setup, before steady gameplay begins.
        m_authoring.ReserveCauseTreeRows( REPLAY_CAUSE_TREE_ROW_CAPACITY );
        m_visualPresentation.ReserveRecordingBuffers();
    }
    return result;
}

bool ReplayRuntime::ApplyMemoryPolicyRequest( const ReplayMemoryPolicyRequest& request )
{
    const ReplayMemoryPolicyApplyResult result = m_timeline.ApplyMemoryPolicyRequest( request );
    if ( result.recordersReset )
    {
        ResetScrubberState();
        SetAllTrackPositions( 1.0f );
    }
    return result.changed;
}

ReplayShutdownReport ReplayRuntime::FinishShutdown()
{
    m_timeline.FlushHashLogs();
    ReplayShutdownReport report;
    report.presentation = m_timeline.Presentation().GetStats();
    report.solver = m_timeline.Solver().GetStats();
    return report;
}

ReplaySceneTimelineResetResult ReplayRuntime::BeginSceneTimelineReset( const ReplaySceneTimelineResetInput& input )
{
    ReplaySceneTimelineResetResult result;
    // Hazard: scene reset can rebuild live model, body, and collider storage.
    // Prediction workers hold only replay-owned values, but cancellation still
    // waits here before old private-engine snapshots are cleared or replaced.
    m_predictionOwner.CancelJob( true );
    if ( SceneTimelineResetClearsBranch( input ) )
    {
        m_authoring.ResetBranch();
    }
    if ( m_scrubberOwner.LiveAdvanceHeld() )
    {
        SetLiveAdvanceHeld( false );
    }
    if ( ResetScrubberState() )
    {
        result.exitInspectionCamera = true;
    }
    return result;
}


ReplaySceneTimelineResetResult ReplayRuntime::FinishSceneTimelineReset( const ReplaySceneTimelineResetInput& input )
{
    ReplaySceneTimelineResetResult result;
    m_timeline.ClearLoadedPresentation();
    ClearCameraFocusForRestore();
    result.exitInspectionCamera = true;
    ClearPathVisualizerState();
    m_authoring.ResetVelocityEdit();
    if ( !m_timeline.Presentation().IsEnabled() )
    {
        return result;
    }

    const char* sceneLabel = input.sceneLabel && input.sceneLabel[0] != '\0' ? input.sceneLabel : "generated";
    m_timeline.Reset( sceneLabel );
    RecordEvent( ReplayEventKind::TimelineStart, 0, 0, 0, 0, 0, 0, 0, sceneLabel );
    result.timelineStarted = true;
    // Why: mismatch diagnostics are scoped to the active replay timeline so a
    // noisy prior scene does not suppress the first useful report in this scene.
    m_timeline.ResetCaptureMismatchDiagnostics();

    if ( SceneTimelineRecordsGeneratedConfig( input ) )
    {
        const uint32_t flags = SceneTimelineGeneratedConfigFlags( input );

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


ReplayFrameIndex ReplayRuntime::NextEventFrameIndex() const
{
    const ReplayRecorderStats solverStats = m_timeline.Solver().GetStats();
    if ( solverStats.enabled )
    {
        return solverStats.nextFrameIndex;
    }

    const ReplayRecorderStats presentationStats = m_timeline.Presentation().GetStats();
    return presentationStats.nextFrameIndex;
}


void ReplayRuntime::ApplyPastTrajectoryUpdate( const ReplayPastTrajectoryUpdate& update )
{
    if ( !update.apply )
    {
        return;
    }
    m_visualPresentation.ApplyPastTrajectoryUpdate( update.targetId,
                                                    update.firstFrame,
                                                    update.builtThroughFrame,
                                                    update.totalFramesEvicted,
                                                    update.fullRebuildCount,
                                                    update.incrementalTrimCount,
                                                    update.valid,
                                                    update.targetModelRow,
                                                    update.targetModelRowRepaired );
}

ReplayPastTrajectoryUpdate
ReplayPrediction::RefreshPastTrajectoryStore( const ReplaySolverRecorder& solver,
                                              const RunReplayPathVisualizerState& pathVisualizer )
{
    ReplayPastTrajectoryUpdate update;
    if ( !pathVisualizer.hasTarget || pathVisualizer.targetId.value == 0 )
    {
        update.apply = true;
        return update;
    }

    const ReplayRecorderStats stats = solver.GetStats();
    if ( !stats.enabled || stats.sampleCount == 0 || stats.nextFrameIndex == 0 )
    {
        update.apply = true;
        return update;
    }

    const ReplayFrameIndex oldestFrame = ReplayOldestFrameFromStats( stats );
    const ReplayFrameIndex newestFrame = stats.nextFrameIndex - 1u;
    const bool needsRebuild = !pathVisualizer.pastTrajectory.valid ||
                              pathVisualizer.pastTrajectory.targetId.value != pathVisualizer.targetId.value ||
                              pathVisualizer.pastTrajectory.totalFramesEvicted != stats.totalFramesEvicted ||
                              pathVisualizer.pastTrajectory.firstFrame != oldestFrame ||
                              pathVisualizer.pastTrajectory.builtThroughFrame < newestFrame;
    if ( !needsRebuild )
    {
        return update;
    }

    const int frameNumber = static_cast<int>( (std::min)( newestFrame, static_cast<ReplayFrameIndex>( INT_MAX ) ) );
    ReplayTrajectoryRecord* record = BeginReplayPastRootTrajectoryRecord( m_state.trajectoryStore,
                                                                          pathVisualizer.targetId,
                                                                          stats.sampleCount,
                                                                          frameNumber );
    if ( !record )
    {
        update.apply = true;
        return update;
    }

    ReplayPastRootRebuildContext rebuild;
    rebuild.store = &m_state.trajectoryStore;
    rebuild.record = record;
    const bool traversalOk = solver.ForEachBodyPositionChronological(
        pathVisualizer.targetId,
        [&]( ReplayFrameIndex frameIndex, Physics::ModelRowHint modelRow, const Math::Vector::Vector3& position )
        {
            if ( !rebuild.ok )
            {
                return;
            }
            rebuild.ok = AppendReplayPastRootTrajectoryPoint( *rebuild.store, *rebuild.record, frameIndex, position );
            if ( rebuild.ok )
            {
                if ( !rebuild.hasSample )
                {
                    rebuild.firstFrame = frameIndex;
                    rebuild.hasSample = true;
                }
                rebuild.targetModelRow = modelRow;
            }
        } );
    if ( !traversalOk || !rebuild.ok || !rebuild.hasSample )
    {
        update.apply = true;
        return update;
    }

    record->firstFrame = rebuild.firstFrame;
    update.targetId = pathVisualizer.targetId;
    update.firstFrame = oldestFrame;
    update.builtThroughFrame = newestFrame;
    update.totalFramesEvicted = stats.totalFramesEvicted;
    update.fullRebuildCount = pathVisualizer.pastTrajectory.fullRebuildCount + 1u;
    update.incrementalTrimCount = pathVisualizer.pastTrajectory.incrementalTrimCount;
    update.targetModelRow = rebuild.targetModelRow;
    update.apply = true;
    update.targetModelRowRepaired = true;
    update.valid = true;
    return update;
}


void ReplayRuntime::RefreshPastTrajectoryStoreFromSolverSamples()
{
    ApplyPastTrajectoryUpdate(
        m_predictionOwner.RefreshPastTrajectoryStore( m_timeline.Solver(), m_visualPresentation.PathVisualizer() ) );
}

// Concept: capture mismatch diagnostics compare the newest paired presentation
// and solver samples after ReplayTimeline records the current frame.
//
// Why: the throttle and paired recorder state belong to one owner, so capture
// callers never receive mutable recorder access merely to validate alignment.
void ReplayPrediction::AppendPastTrajectorySample( const ReplayRecorderStats& solverStats,
                                                   const RunReplayPathVisualizerState& pathVisualizer,
                                                   const ReplaySolverFrameSample& sample,
                                                   ReplayPastTrajectoryUpdate& update )
{
    if ( !pathVisualizer.hasTarget || pathVisualizer.targetId.value == 0 || !pathVisualizer.pastTrajectory.valid ||
         pathVisualizer.pastTrajectory.targetId.value != pathVisualizer.targetId.value )
    {
        return;
    }

    ReplayTrajectoryRecord* record =
        m_state.trajectoryStore.FindRecord( ReplayPastRootTrajectoryKey( pathVisualizer.targetId ) );
    if ( !record )
    {
        update.apply = true;
        update.valid = false;
        return;
    }

    update.targetId = pathVisualizer.pastTrajectory.targetId;
    update.firstFrame = pathVisualizer.pastTrajectory.firstFrame;
    update.builtThroughFrame = pathVisualizer.pastTrajectory.builtThroughFrame;
    update.totalFramesEvicted = pathVisualizer.pastTrajectory.totalFramesEvicted;
    update.fullRebuildCount = pathVisualizer.pastTrajectory.fullRebuildCount;
    update.incrementalTrimCount = pathVisualizer.pastTrajectory.incrementalTrimCount;
    update.valid = pathVisualizer.pastTrajectory.valid;

    const ReplayFrameIndex oldestFrame = ReplayOldestFrameFromStats( solverStats );
    if ( pathVisualizer.pastTrajectory.totalFramesEvicted != solverStats.totalFramesEvicted ||
         pathVisualizer.pastTrajectory.firstFrame != oldestFrame )
    {
        // Why: ring eviction advances every live capture once retention is
        // full. Slide the already-published record in place; rebuilding compact
        // solver history here would reconstruct every world snapshot and would
        // also replace the record version that prevents path flicker.
        m_state.trajectoryStore.TrimPublishedPointsBeforeFrame( *record, oldestFrame );
        update.firstFrame = oldestFrame;
        update.totalFramesEvicted = solverStats.totalFramesEvicted;
        ++update.incrementalTrimCount;
        update.apply = true;
    }
    if ( sample.frameIndex <= pathVisualizer.pastTrajectory.builtThroughFrame )
    {
        return;
    }

    const ReplaySolverBodySample* body =
        FindReplayBodyByIdWithHint( sample, pathVisualizer.targetId, pathVisualizer.targetModelRow.value );
    if ( !body )
    {
        // The frame was inspected even when the selected body no longer exists;
        // do not trigger a full historical rebuild on the next render pass.
        update.builtThroughFrame = sample.frameIndex;
        update.apply = true;
        return;
    }

    if ( !AppendReplayPastRootTrajectoryPoint( m_state.trajectoryStore, *record, sample.frameIndex, body->position ) )
    {
        update.valid = false;
        update.apply = true;
        return;
    }

    update.targetModelRow = body->modelRow;
    update.targetModelRowRepaired = true;
    update.builtThroughFrame = sample.frameIndex;
    update.apply = true;
}


void ReplayRuntime::AppendSolverTrajectorySampleToStore( const ReplaySolverFrameSample& sample )
{
    ReplayPastTrajectoryUpdate update;
    m_predictionOwner.AppendPastTrajectorySample( m_timeline.Solver().GetStats(),
                                                  m_visualPresentation.PathVisualizer(),
                                                  sample,
                                                  update );
    ApplyPastTrajectoryUpdate( update );
}

void ReplayRuntime::CaptureFrame( ReplayCaptureInput input, RuntimeTools& runtimeTools )
{
    // Invariant: presentation, solver, and event timelines share the same
    // branch and event cursor for this frame. Save/export code depends on that
    // alignment when it pairs visual frames with restore checkpoints.
    m_visualPresentation.PopulateLauncherVisualCapture( input, runtimeTools );
    input.branch = m_authoring.Branch();
    const ReplayTimelineCaptureResult result = m_timeline.CaptureFrame( input );
    if ( result.solverSample )
    {
        AppendSolverTrajectorySampleToStore( *result.solverSample );
    }
}

// Concept: render replay poses are temporary render-instance overrides.
//
// Scrubbing should affect only the pixels drawn for this frame. The helpers
// below apply replay or prediction poses to a freshly prepared render-instance
// snapshot; live physics rows and authored presentation metadata are not
// mutated and therefore need no restore.
bool ReplayPresentation::ApplyPresentationSampleForRender( Rendering::RenderInstanceStore& renderInstances,
                                                           const PhysicsBodyStore& bodyStore,
                                                           const ColliderStore& colliderStore,
                                                           const ReplayPresentationSample& sample )
{
    const int modelCount = renderInstances.Count();
    if ( !PrepareRenderPoseBodyMatch( modelCount ) )
    {
        return false;
    }
    bool queuedAny = false;

    for ( const ReplayBodyPresentationSample& body : sample.bodies )
    {
        int resolvedModelIndex = -1;
        if ( !ReplayRuntimeResolveReplayBody( bodyStore, body.id, -1, modelCount, resolvedModelIndex ) )
        {
            continue;
        }

        Math::Orientation::Quaternion orientation( body.orientation[0],
                                                   body.orientation[1],
                                                   body.orientation[2],
                                                   body.orientation[3] );
        orientation.Normalise();
        if ( ReplayRuntimeQueueRenderPoseOverride( renderInstances,
                                                   bodyStore,
                                                   colliderStore,
                                                   body.id,
                                                   body.position,
                                                   orientation ) )
        {
            MarkRenderPoseBodyMatched( resolvedModelIndex );
            queuedAny = true;
        }
    }

    const Math::Vector::Vector3 hiddenReplayPosition( 0.0f, -100000.0f, 0.0f );
    for ( int i = 0; i < modelCount; ++i )
    {
        const std::size_t bodyIndex = static_cast<std::size_t>( i );
        if ( RenderPoseBodyMatched( static_cast<int>( bodyIndex ) ) )
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
        ReplayBodyId replayBodyId{ bodyRecord->replayBodyId };
        if ( ReplayRuntimeQueueRenderPoseOverride( renderInstances,
                                                   bodyStore,
                                                   colliderStore,
                                                   replayBodyId,
                                                   hiddenReplayPosition,
                                                   Math::Orientation::IDENTITY_QUATERNION ) )
        {
            queuedAny = true;
        }
    }
    return queuedAny;
}

bool ReplayPresentation::ApplySolverSampleForRender( Rendering::RenderInstanceStore& renderInstances,
                                                     const PhysicsBodyStore& bodyStore,
                                                     const ColliderStore& colliderStore,
                                                     const ReplaySolverFrameSample& sample )
{
    const int modelCount = renderInstances.Count();
    if ( !PrepareRenderPoseBodyMatch( modelCount ) )
    {
        return false;
    }
    bool queuedAny = false;

    for ( const ReplaySolverBodySample& body : sample.bodies )
    {
        int resolvedModelIndex = -1;
        if ( !ReplayRuntimeResolveReplayBody( bodyStore, body.id, -1, modelCount, resolvedModelIndex ) )
        {
            continue;
        }

        Math::Orientation::Quaternion orientation( body.orientation[0],
                                                   body.orientation[1],
                                                   body.orientation[2],
                                                   body.orientation[3] );
        orientation.Normalise();
        if ( ReplayRuntimeQueueRenderPoseOverride( renderInstances,
                                                   bodyStore,
                                                   colliderStore,
                                                   body.id,
                                                   body.position,
                                                   orientation ) )
        {
            MarkRenderPoseBodyMatched( resolvedModelIndex );
            queuedAny = true;
        }
    }

    const Math::Vector::Vector3 hiddenReplayPosition( 0.0f, -100000.0f, 0.0f );
    for ( int i = 0; i < modelCount; ++i )
    {
        const std::size_t bodyIndex = static_cast<std::size_t>( i );
        if ( RenderPoseBodyMatched( static_cast<int>( bodyIndex ) ) )
        {
            continue;
        }

        const PhysicsBodyRecord* bodyRecord = ReplayRuntimeBodyRecordForModelIndex( bodyStore, i );
        if ( !bodyRecord )
        {
            continue;
        }

        ReplayBodyId replayBodyId{ bodyRecord->replayBodyId };
        if ( ReplayRuntimeQueueRenderPoseOverride( renderInstances,
                                                   bodyStore,
                                                   colliderStore,
                                                   replayBodyId,
                                                   hiddenReplayPosition,
                                                   Math::Orientation::IDENTITY_QUATERNION ) )
        {
            queuedAny = true;
        }
    }
    return queuedAny;
}

bool ReplayPresentation::ApplyPredictionFrameForRender( Rendering::RenderInstanceStore& renderInstances,
                                                        const PhysicsBodyStore& bodyStore,
                                                        const ColliderStore& colliderStore,
                                                        const RunReplayPredictionFrame& frame )
{
    const int modelCount = renderInstances.Count();
    if ( !PrepareRenderPoseBodyMatch( modelCount ) )
    {
        return false;
    }
    bool queuedAny = false;

    for ( const RunReplayPredictionBodySample& body : frame.bodies )
    {
        int resolvedModelIndex = -1;
        if ( !ReplayRuntimeResolveReplayBody( bodyStore,
                                              body.id,
                                              body.modelRow.value,
                                              modelCount,
                                              resolvedModelIndex ) )
        {
            continue;
        }

        Math::Orientation::Quaternion orientation = body.orientation;
        orientation.Normalise();
        if ( ReplayRuntimeQueueRenderPoseOverride( renderInstances,
                                                   bodyStore,
                                                   colliderStore,
                                                   body.id,
                                                   body.position,
                                                   orientation ) )
        {
            MarkRenderPoseBodyMatched( resolvedModelIndex );
            queuedAny = true;
        }
    }

    const Math::Vector::Vector3 hiddenReplayPosition( 0.0f, -100000.0f, 0.0f );
    for ( int i = 0; i < modelCount; ++i )
    {
        const std::size_t bodyIndex = static_cast<std::size_t>( i );
        if ( RenderPoseBodyMatched( static_cast<int>( bodyIndex ) ) )
        {
            continue;
        }

        const PhysicsBodyRecord* bodyRecord = ReplayRuntimeBodyRecordForModelIndex( bodyStore, i );
        if ( !bodyRecord )
        {
            continue;
        }

        ReplayBodyId replayBodyId{ bodyRecord->replayBodyId };
        if ( ReplayRuntimeQueueRenderPoseOverride( renderInstances,
                                                   bodyStore,
                                                   colliderStore,
                                                   replayBodyId,
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
    return m_timeline.LoadedPresentation().enabled && m_timeline.LoadedPresentation().samples.size() >= 2;
}


const ReplayPresentationSample* ReplayRuntime::LoadedPresentationSampleAtNormalized( float normalized ) const
{
    if ( !HasLoadedPresentation() )
    {
        return nullptr;
    }

    return ReplayRuntimeLoadedPresentationSampleAtNormalized( m_timeline.LoadedPresentation().samples, normalized );
}


const ReplayPresentationSample* ReplayRuntime::LoadedPresentationLatestSample() const
{
    return HasLoadedPresentation() ? &m_timeline.LoadedPresentation().samples.back() : nullptr;
}


bool ReplayRuntime::IsScrubPaused() const
{
    const ReplayScrubberView scrubber = m_scrubberOwner.View();
    if ( !scrubber.historicalSamplePaused )
    {
        return false;
    }

    if ( scrubber.activeTrack == RunReplayTrack::Presentation && HasLoadedPresentation() )
    {
        return LoadedPresentationSampleAtNormalized( TrackPosition( RunReplayTrack::Presentation ) ) != nullptr;
    }

    const float position = TrackPosition( scrubber.activeTrack );
    const float presentT = scrubber.activeTrack == RunReplayTrack::Solver ? SolverPresentTrackPosition() : 1.0f;
    if ( ReplayAtPresentTrackPosition( position, presentT ) )
    {
        return false;
    }

    if ( scrubber.activeTrack == RunReplayTrack::Presentation )
    {
        return m_timeline.Presentation().IsEnabled() &&
               m_timeline.Presentation().SampleAtNormalized( position ) != nullptr;
    }

    if ( ReplayTrackPositionIsFuture( position, presentT ) )
    {
        return CurrentPredictionScrubFrame() != nullptr;
    }

    return m_timeline.Solver().IsEnabled() &&
           m_timeline.Solver().SampleAtNormalized( ReplaySolverNormalizedFromTrack( position, presentT ) ) != nullptr;
}


const ReplayPresentationSample* ReplayRuntime::CurrentScrubSample() const
{
    // Concept: a scrub sample is available only when the active track is paused
    // away from live time. Live presentation should continue drawing the live
    // scene instead of borrowing old retained samples.
    const ReplayScrubberView scrubber = m_scrubberOwner.View();
    if ( scrubber.activeTrack != RunReplayTrack::Presentation )
    {
        return nullptr;
    }

    if ( HasLoadedPresentation() )
    {
        return scrubber.historicalSamplePaused
                   ? LoadedPresentationSampleAtNormalized( TrackPosition( RunReplayTrack::Presentation ) )
                   : nullptr;
    }

    if ( !IsScrubPaused() )
    {
        return nullptr;
    }

    return m_timeline.Presentation().SampleAtNormalized( TrackPosition( RunReplayTrack::Presentation ) );
}


const ReplaySolverFrameSample* ReplayRuntime::CurrentSolverScrubSample() const
{
    if ( m_scrubberOwner.View().activeTrack != RunReplayTrack::Solver || !IsScrubPaused() )
    {
        return nullptr;
    }

    const float position = TrackPosition( RunReplayTrack::Solver );
    const float presentT = SolverPresentTrackPosition();
    if ( ReplayTrackPositionIsFuture( position, presentT ) )
    {
        return nullptr;
    }

    return m_timeline.Solver().SampleAtNormalized( ReplaySolverNormalizedFromTrack( position, presentT ) );
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
        ReplayRuntimeTimelinePredictionFrames( m_predictionOwner.State(), frameCount );
    const ReplayScrubberView scrubber = m_scrubberOwner.View();
    if ( scrubber.activeTrack != RunReplayTrack::Solver || !scrubber.historicalSamplePaused || frameCount < 2 )
    {
        return nullptr;
    }

    const float position = TrackPosition( RunReplayTrack::Solver );
    const float presentT = SolverPresentTrackPosition();
    if ( !ReplayTrackPositionIsFuture( position, presentT ) )
    {
        return nullptr;
    }

    const float predictionT = ReplayPredictionNormalizedFromTrack( position, presentT );
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

    const std::span<const RunReplayPredictionFrame> activePredictionFrames = ActivePredictionFrames();
    if ( m_predictionOwner.State().enabled && !activePredictionFrames.empty() &&
         m_predictionOwner.State().simulation.targetId.value == m_visualPresentation.PathVisualizer().targetId.value )
    {
        if ( const RunReplayPredictionBodySample* body =
                 FindReplayPredictionBodyById( activePredictionFrames.front(), id ) )
        {
            outPosition = body->position;
            if ( outRadius )
            {
                const PhysicsBodyHandle liveBody = bodyStore.HandleForReplayBodyId( id.value, body->modelRow.value );
                const PhysicsBodyRecord* liveBodyRecord = bodyStore.RecordForHandle( liveBody );
                *outRadius =
                    liveBodyRecord
                        ? ReplayRuntimeColliderRadiusForBody( colliderStore, *liveBodyRecord, body->modelRow.value )
                        : ReplayRuntimeColliderRadiusForModelIndex( colliderStore, body->modelRow.value );
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
                const PhysicsBodyHandle liveBody = bodyStore.HandleForReplayBodyId( id.value, body->modelRow.value );
                const PhysicsBodyRecord* liveBodyRecord = bodyStore.RecordForHandle( liveBody );
                *outRadius =
                    liveBodyRecord
                        ? ReplayRuntimeColliderRadiusForBody( colliderStore, *liveBodyRecord, body->modelRow.value )
                        : ReplayRuntimeColliderRadiusForModelIndex( colliderStore, body->modelRow.value );
            }
            return true;
        }
    }

    const auto bodies = bodyStore.Records();
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


bool ReplayRuntime::BuildCauseTreeRows(
    std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords,
    const PhysicsBodyStore& bodyStore )
{
    PROFILE_SCOPED( "Frame/Replay/CauseTree/BuildRows" );
    m_authoring.BeginCauseTreeRowBuild();

    if ( !m_visualPresentation.PathVisualizer().hasTarget || m_visualPresentation.PathVisualizer().targetId.value == 0 )
    {
        return false;
    }

    // Why: ActivePredictionFrames() waits for a coherent full buffer, while the
    // prediction overlay exposes a populated build prefix so long jobs are
    // visible immediately. The cause tree must use the same readiness rule.
    const bool predictionPrefixVisible = ActivePredictionFrames().size() >= 2 ||
                                         m_predictionOwner.State().HasPublishedBuildFramePrefix() ||
                                         !m_predictionOwner.State().futureNodeCache.futureNodes.empty();
    const bool usePrediction =
        m_predictionOwner.State().enabled && predictionPrefixVisible &&
        m_predictionOwner.State().simulation.targetId.value == m_visualPresentation.PathVisualizer().targetId.value;
    const std::vector<RunReplayPathTraceNode>& nodes = usePrediction
                                                           ? m_predictionOwner.State().futureNodeCache.futureNodes
                                                           : m_visualPresentation.PathVisualizer().futureNodes;
    const ReplaySolverFrameSample* solverSample = CurrentSolverScrubSample();
    const std::size_t solverContactCount =
        solverSample ? solverSample->worldSnapshot.persistentContacts.size() : static_cast<std::size_t>( 0 );
    const std::size_t estimatedRows = 1 + nodes.size() + solverContactCount * 3;
    if ( !m_authoring.CauseTreeRowCapacityCovers( estimatedRows ) )
    {
        // Hazard: this path runs from input/render. If a future scene exceeds
        // the preallocated explanation budget, hide the overlay for the frame
        // instead of growing row storage on the hot path.
        m_authoring.SetCauseTreeSelectedRow( -1 );
        return false;
    }
    bool rowOverflow = false;
    auto appendCauseTreeRow = [&]( const RunReplayCauseTreeRow& row ) -> bool
    {
        if ( !m_authoring.AppendCauseTreeRow( row ) )
        {
            rowOverflow = true;
            return false;
        }
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
                return sampleBody->modelRow.value;
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
                contactRow.modelRow.value = bodyRow.modelRow.value;
                contactRow.counterpartModelRow.value = node.parentModelRow.value;
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

        if ( !solverSample || bodyRow.modelRow.value < 0 )
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
            if ( !ReplayContactHasModelIndex( contact, bodyRow.modelRow.value ) )
            {
                continue;
            }
            const int otherModelIndex = ReplayContactOtherModelIndex( contact, bodyRow.modelRow.value );
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
                if ( !ReplayContactHasModelIndex( contact, bodyRow.modelRow.value ) )
                {
                    continue;
                }
                const int otherModelIndex = ReplayContactOtherModelIndex( contact, bodyRow.modelRow.value );
                const bool terrain = contact.isTerrain || otherModelIndex < 0;
                if ( otherModelIndex != group.otherModelIndex || terrain != group.terrain )
                {
                    continue;
                }
                const Vector3 point = ReplayContactPoint( *solverSample, contact );
                centroid += point;
                normalSum += ReplayContactNormalForModel( contact, bodyRow.modelRow.value );
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
            manifoldRow.modelRow.value = bodyRow.modelRow.value;
            manifoldRow.counterpartModelRow.value = group.otherModelIndex;
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
                if ( !ReplayContactHasModelIndex( contact, bodyRow.modelRow.value ) )
                {
                    continue;
                }
                const int otherModelIndex = ReplayContactOtherModelIndex( contact, bodyRow.modelRow.value );
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
                solverRow.modelRow.value = bodyRow.modelRow.value;
                solverRow.counterpartModelRow.value = group.otherModelIndex;
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
                solverRow.normal = ReplayContactNormalForModel( contact, bodyRow.modelRow.value );
                solverRow.impulse = ReplayContactImpulseForModel( contact, bodyRow.modelRow.value );
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
        row.modelRow.value = modelIndexForId( id, modelIndex );
        row.prediction = usePrediction;
        writeName( id, row.modelRow.value, fallbackName, row.name, sizeof( row.name ) );
        if ( usePrediction && firstFrame > 0 )
        {
            sprintf_s( row.detail,
                       sizeof( row.detail ),
                       "first affected frame %llu",
                       static_cast<unsigned long long>( firstFrame ) );
        }
        else if ( row.modelRow.value >= 0 && solverSample )
        {
            if ( const ReplaySolverBodySample* body = FindReplayBodyByModelIndex( *solverSample, row.modelRow.value ) )
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
        appendSolverRowsForBody( row );
        return !rowOverflow;
    };

    if ( !addBodyRow( m_visualPresentation.PathVisualizer().targetId,
                      ReplayBodyId{},
                      0,
                      0,
                      m_visualPresentation.PathVisualizer().targetModelRow.value,
                      m_visualPresentation.PathVisualizer().targetName ) )
    {
        m_authoring.FailCauseTreeRowBuild();
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
                             modelIndexForId( node.id, node.modelRow.value ),
                             nullptr ) )
            {
                self( self, node.id, depth + 1 );
            }
        }
    };
    addChildren( addChildren, m_visualPresentation.PathVisualizer().targetId, 1 );
    if ( rowOverflow )
    {
        m_authoring.FailCauseTreeRowBuild();
        return false;
    }

    m_authoring.SetCauseTreeSelectedRow( -1 );
    const RunReplayCauseTreeState& causeTree = m_authoring.CauseTree();
    const RunReplayCameraState camera = m_visualPresentation.CameraView();
    if ( camera.focusKind != RunReplayCameraFocusKind::None )
    {
        for ( int i = 0; i < static_cast<int>( causeTree.rows.size() ); ++i )
        {
            const RunReplayCauseTreeRow& row = causeTree.rows[static_cast<std::size_t>( i )];
            if ( row.kind != camera.focusRowKind || row.id.value != camera.focusedId.value ||
                 row.modelRow.value != camera.focusModelRow.value || row.terrain != camera.focusTerrain )
            {
                continue;
            }
            if ( row.kind == RunReplayCauseTreeRowKind::Body ||
                 ( row.counterpartId.value == camera.counterpartId.value &&
                   row.counterpartModelRow.value == camera.focusCounterpartModelRow.value &&
                   ( row.kind != RunReplayCauseTreeRowKind::SolverRow ||
                     ( row.featureId == camera.focusFeatureId &&
                       row.solverRowIndex == camera.focusSolverRowIndex ) ) ) )
            {
                m_authoring.SetCauseTreeSelectedRow( i );
                m_visualPresentation.SetCameraFocusedRow( i );
                break;
            }
        }
    }
    if ( m_authoring.CauseTree().selectedRow >= static_cast<int>( m_authoring.CauseTree().rows.size() ) )
    {
        m_authoring.SetCauseTreeSelectedRow( -1 );
    }
    return !m_authoring.CauseTree().rows.empty();
}


bool ReplayPresentation::BuildPredictionGhostDrawRequests(
    const ReplayPredictionPresentationView& prediction,
    std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords,
    const PhysicsBodyStore& bodyStore )
{
    ClearPredictionGhostDrawRequests();
    const std::span<const RunReplayPredictionFrame> frames = prediction.frames;
    const bool drawLivePrediction = prediction.enabled && prediction.ragdollVisualsEnabled && frames.size() >= 2;
    const bool drawBaseline = prediction.baselineValid && prediction.baselineComparisonActive &&
                              prediction.ragdollVisualsEnabled && !prediction.baselineBodyPoses.empty();

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
    const std::size_t baselineRequestCapacity = drawBaseline ? prediction.baselineBodyPoses.size() : 0u;
    if ( !CanAppendPredictionGhostDrawRequests( liveRequestCapacity + baselineRequestCapacity ) )
    {
        return false;
    }

    if ( drawBaseline )
    {
        for ( const ReplayPredictionBaselineBodyPose& pose : prediction.baselineBodyPoses )
        {
            if ( !pose.hasRestPose || pose.modelRow.value < 0 ||
                 pose.modelRow.value >= static_cast<int>( presentationRecords.size() ) ||
                 !ReplayRuntimeModelIsRagdollPart( presentationRecords, pose.modelRow.value ) )
            {
                continue;
            }

            ReplayPredictionGhostDrawRequest request;
            request.modelRow.value = pose.modelRow.value;
            request.position = pose.restPosition;
            request.orientation = pose.restOrientation;
            request.orientation.Normalise();
            request.alpha = 0.075f;
            request.tintR = 0.28f;
            request.tintG = 0.76f;
            request.tintB = 1.0f;
            request.tintStrength = 0.82f;
            AppendPredictionGhostDrawRequest( request );
        }
    }

    if ( !drawLivePrediction )
    {
        return HasPredictionGhostDrawRequests();
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
                                                  body.modelRow.value,
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
            request.modelRow.value = resolvedModelIndex;
            request.position = body.position;
            request.orientation = body.orientation;
            request.orientation.Normalise();
            request.alpha = alpha;
            AppendPredictionGhostDrawRequest( request );
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
    return HasPredictionGhostDrawRequests();
}

void ReplayPresentation::PublishVisualPacket( ReplayVisualPacket packet,
                                              const ReplayPredictionPresentationView& prediction,
                                              const ReplaySolverFrameSample* latestSolver,
                                              uint64_t replayReserveGrowthEvents )
{
    packet.header.sourceFrame = prediction.sourceFrame;
    packet.header.revealFrame = prediction.revealFrame;
    packet.header.targetId = m_pathVisualizer.targetId;
    packet.header.branchId = latestSolver ? latestSolver->branch.branchId : 0u;
    packet.header.eventCursor = latestSolver ? latestSolver->eventCursor : 0u;
    packet.header.topologyVersion = prediction.topologyVersion;
    packet.header.publishedFrameCount = static_cast<uint32_t>( prediction.frames.size() );
    packet.header.futureNodeCount = static_cast<uint32_t>( prediction.futureNodes.size() );
    const std::span<const ReplayPredictionGhostDrawRequest> ghostRequests = PredictionGhostDrawRequestsView();
    packet.header.ghostRequestCount = static_cast<uint32_t>( ghostRequests.size() );
    packet.header.replayReserveGrowthEvents = replayReserveGrowthEvents;
    packet.header.predictionEnabled = prediction.enabled;
    packet.header.predictionBuilding = prediction.building;
    packet.header.predictionComplete = prediction.complete;
    packet.trajectoryRecords = prediction.trajectoryRecords;
    packet.futureNodes = prediction.futureNodes;
    packet.retainedMarkers = prediction.retainedMarkers;
    packet.ghostRequests = ghostRequests;
    packet.trajectoryDiagnostics = TrajectoryVisualStatsSnapshot();
    PublishVisualPacket( packet );
}

SkullbonezCore::Core::MainMemoryReplayStats ReplayRuntime::CollectMemoryStats() const
{
    SkullbonezCore::Core::MainMemoryReplayStats stats;
    const ReplayRecorderStats presentationStats = m_timeline.Presentation().GetStats();
    const ReplayRecorderStats solverStats = m_timeline.Solver().GetStats();
    const ReplayEventRecorderStats eventStats = m_timeline.Events().GetStats();

    // Concept: the broad replay totals are sums of the same category table
    // exposed to diagnostics. That keeps UI totals and memory-dump evidence in
    // lockstep when later stages move storage between owners.
    m_timeline.CollectMemoryCategoryBytes( stats.categoryBytes );
    stats.presentationBytes = SkullbonezCore::Core::MainMemoryReplayCategoryRangeBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PresentationOwner,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverOwner );
    stats.solverBytes = SkullbonezCore::Core::MainMemoryReplayCategoryRangeBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverOwner,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::EventsOwner );
    stats.eventsBytes = SkullbonezCore::Core::MainMemoryReplayCategoryRangeBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::EventsOwner,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::LoadedOwner );
    stats.presentationSamples = presentationStats.sampleCount;
    stats.solverSamples = solverStats.sampleCount;
    stats.eventSamples = eventStats.eventCount;
    stats.memoryPreset = static_cast<int>( m_timeline.MemoryPolicy().preset );
    stats.requestedRetentionSeconds = m_timeline.MemoryPolicy().requestedRetentionSeconds;
    stats.requestedBudgetMiB = m_timeline.MemoryPolicy().requestedBudgetMiB;
    stats.presentationRetentionSeconds = m_timeline.MemoryPolicy().presentationRetentionSeconds;
    stats.solverRetentionSeconds = m_timeline.MemoryPolicy().solverRetentionSeconds;
    stats.memoryBudgetClamped = m_timeline.MemoryPolicy().budgetClamped;
    stats.solverWindowReduced = m_timeline.MemoryPolicy().solverWindowReduced;
    // The policy table is stable and fixed-size; diagnostics never discovers
    // replay owners by scanning recent-event text or allocating a report map.
    for ( std::size_t index = 0; index < REPLAY_GROWTH_OWNER_POLICIES.size(); ++index )
    {
        const ReplayGrowthOwnerPolicy& policy = REPLAY_GROWTH_OWNER_POLICIES[index];
        SkullbonezCore::Core::MainMemoryReplayStats::GrowthOwner& growth = stats.growthOwners[index];
        growth.ownerName = policy.ownerName;
        growth.hardBytes = policy.hardBytes;
        growth.measuredHighWaterBytes = policy.measuredHighWaterBytes;
        Runtime::Allocation::RuntimeReserveOwnerStatsView ownerStats = {};
        growth.registered =
            Runtime::Allocation::RuntimeReserveAllocator::CopyOwnerStatsByName( policy.ownerName, ownerStats );
        if ( growth.registered )
        {
            growth.allocatorHighWaterBytes = ownerStats.highWaterBytes;
            growth.replayGrowths = ownerStats.replayGrowths;
            growth.failedGrowths = ownerStats.failedGrowths;
            growth.reportedHighWaterCapacity = ownerStats.highWaterCapacity;
            growth.lastGrowthFrame = ownerStats.lastGrowthFrame;
        }
    }

    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::LoadedOwner,
        static_cast<uint64_t>( sizeof( m_timeline.LoadedPresentation() ) ) );
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::LoadedSampleRecords,
        VectorCapacityBytes( m_timeline.LoadedPresentation().samples ) );
    for ( const ReplayPresentationSample& sample : m_timeline.LoadedPresentation().samples )
    {
        SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
            stats.categoryBytes,
            SkullbonezCore::Core::MainMemoryReplayByteCategory::LoadedBodies,
            PresentationSampleMemoryBytes( sample ) );
    }
    stats.loadedReplayBytes = SkullbonezCore::Core::MainMemoryReplayCategoryRangeBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::LoadedOwner,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionOwner );
    stats.loadedReplaySamples = m_timeline.LoadedPresentation().samples.size();

    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionOwner,
        static_cast<uint64_t>( sizeof( m_predictionOwner.State() ) ) );
    if ( m_predictionOwner.State().simulation.predictionEngine )
    {
        SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
            stats.categoryBytes,
            SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionEngine,
            PredictionEngineMemoryBytes( *m_predictionOwner.State().simulation.predictionEngine ) );
    }
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionWorldState,
        SolverWorldSnapshotMemoryBytes( m_predictionOwner.State().simulation.predictionWorld ) );
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionBodyState,
        VectorCapacityBytes( m_predictionOwner.State().simulation.predictionBodies ) );
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionFrameRecords,
        VectorCapacityBytes( m_predictionOwner.State().simulation.frames ) +
            VectorCapacityBytes( m_predictionOwner.State().build.buildFrames ) );
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionFutureTree,
        VectorCapacityBytes( m_predictionOwner.State().futureNodeCache.futureNodes ) +
            VectorCapacityBytes( m_predictionOwner.State().futureNodeCache.futureNodeBuildScratch ) );
    for ( const RunReplayPredictionFrame& frame : m_predictionOwner.State().simulation.frames )
    {
        AddPredictionFrameCategoryBytes( stats.categoryBytes, frame );
    }
    for ( const RunReplayPredictionFrame& frame : m_predictionOwner.State().build.buildFrames )
    {
        AddPredictionFrameCategoryBytes( stats.categoryBytes, frame );
    }
    stats.predictionBytes = SkullbonezCore::Core::MainMemoryReplayCategoryRangeBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionOwner,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PathOwner );
    stats.predictionFrames =
        m_predictionOwner.State().simulation.frames.size() + m_predictionOwner.State().build.buildFrames.size();

    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PathOwner,
        static_cast<uint64_t>( sizeof( m_visualPresentation.PathVisualizer() ) + sizeof( m_authoring.CauseTree() ) ) );
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PathFutureNodes,
        VectorCapacityBytes( m_visualPresentation.PathVisualizer().futureNodes ) );
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PathTargets,
        VectorCapacityBytes( m_visualPresentation.PathVisualizer().targets ) );
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PathCauseRows,
        VectorCapacityBytes( m_authoring.CauseTree().rows ) );
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::TrajectoryStore,
        m_predictionOwner.State().trajectoryStore.CapacityBytes() );
    stats.pathAndCauseBytes = SkullbonezCore::Core::MainMemoryReplayCategoryRangeBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PathOwner,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::RenderGhostRequests );
    stats.pathNodes = m_visualPresentation.PathVisualizer().futureNodes.size() +
                      m_predictionOwner.State().futureNodeCache.futureNodes.size();
    stats.causeRows = m_authoring.CauseTree().rows.size();

    const ReplayPresentationMemoryStats visualMemory = m_visualPresentation.CollectMemoryStats();
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::RenderGhostRequests,
        visualMemory.ghostRequestCapacityBytes );
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::RenderFocusMask,
        visualMemory.focusModelMaskCapacityBytes );
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::RenderLauncherBackup,
        visualMemory.launcherVisualBytes );
    stats.renderScratchBytes = SkullbonezCore::Core::MainMemoryReplayCategoryRangeBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::RenderGhostRequests,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::TrajectoryStore );
    stats.ghostRequests = visualMemory.ghostRequestCount;
    stats.trajectory = visualMemory.trajectory;
    stats.trajectory.storeBytes = SkullbonezCore::Core::MainMemoryReplayCategoryByte(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::TrajectoryStore );
    stats.trajectory.recordCount = static_cast<uint64_t>( m_predictionOwner.State().trajectoryStore.RecordCount() );
    stats.trajectory.pointCount = static_cast<uint64_t>( m_predictionOwner.State().trajectoryStore.PointCount() );
    stats.trajectory.versionChurn =
        m_predictionOwner.State().trajectoryStore.nextVersion > 0u
            ? static_cast<uint64_t>( m_predictionOwner.State().trajectoryStore.nextVersion - 1u )
            : 0u;
    for ( const ReplayTrajectoryRecord& record : m_predictionOwner.State().trajectoryStore.records )
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


ReplayHudStatus ReplayRuntime::BuildHudStatus( bool includeMemoryStats ) const
{
    ReplayHudStatus status;
    const ReplayMemoryPolicy& policy = m_timeline.MemoryPolicy();
    status.memoryPreset = static_cast<int>( policy.preset );
    status.requestedRetentionSeconds = policy.requestedRetentionSeconds;
    status.requestedBudgetMiB = policy.requestedBudgetMiB;
    status.presentationRetentionSeconds = policy.presentationRetentionSeconds;
    status.solverRetentionSeconds = policy.solverRetentionSeconds;
    status.memoryBudgetClamped = policy.budgetClamped;
    status.solverWindowReduced = policy.solverWindowReduced;
    status.divergenceUnits = m_predictionOwner.State().baseline.divergenceUnits;
    status.divergenceValid = m_predictionOwner.State().baseline.divergenceValid;
    if ( includeMemoryStats )
    {
        status.memoryStats = CollectMemoryStats();
        status.memoryStatsValid = true;
    }
    return status;
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
    if ( !m_timeline.Events().IsEnabled() )
    {
        return;
    }

    ReplayEventInput input;
    input.frameIndex = frameIndex;
    input.branch = m_authoring.Branch();
    input.kind = kind;
    input.flags = flags;
    input.value0 = value0;
    input.value1 = value1;
    input.value2 = value2;
    input.value3 = value3;
    input.data0 = data0;
    input.text = text;
    m_timeline.RecordEvent( input );
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

bool ReplayRuntime::SavePresentationWithSolverHashes( const char* path,
                                                      ReplayV2SaveResult* result,
                                                      std::span<const ReplayVisualArchiveSample> visualPackets,
                                                      std::span<const uint8_t> visualPredictionState ) const
{
    std::vector<uint8_t> fallbackPredictionState;
    if ( !visualPackets.empty() && visualPredictionState.empty() &&
         !m_predictionOwner.BuildArchive( m_visualPresentation.PathVisualizer(), fallbackPredictionState ) )
    {
        return false;
    }
    const std::span<const uint8_t> predictionState =
        !visualPredictionState.empty() ? visualPredictionState : std::span<const uint8_t>( fallbackPredictionState );
    return ReplayV2Artifact::SavePresentationWithSolverHashes( m_timeline.Presentation(),
                                                               m_timeline.Solver(),
                                                               m_timeline.Events(),
                                                               visualPackets,
                                                               predictionState,
                                                               path,
                                                               result );
}

bool ReplayRuntime::SavePresentationFromScrubber( double now )
{
    // Invariant: the owner advances the process-local sequence and publishes
    // success only after the binary v2 writer completes.
    char path[256] = {};
    bool saved = false;
    if ( m_timeline.NextPresentationSavePath( path, sizeof( path ) ) )
    {
        saved = SavePresentationWithSolverHashes( path );
    }

    char message[96] = {};
    if ( saved )
    {
        const char* fileName = std::strrchr( path, '\\' );
        if ( !fileName )
        {
            fileName = std::strrchr( path, '/' );
        }
        fileName = fileName ? fileName + 1 : path;
        sprintf_s( message, sizeof( message ), "SAVED %s", fileName );
    }
    else
    {
        sprintf_s( message, sizeof( message ), "REPLAY SAVE FAILED" );
    }
    m_scrubberOwner.PublishFeedback( RunReplayTrack::Presentation, message, now, 2.5 );
    m_scrubberOwner.KeepVisible( now, ReplayOverlay::REPLAY_SCRUBBER_VISIBLE_SECONDS );
    return saved;
}

bool ReplayRuntime::LoadPresentationArtifact( const char* path,
                                              bool activateScrubber,
                                              double now,
                                              InputRouter& inputRouter,
                                              RuntimeInteractionController& interaction,
                                              Environment::CameraCollection* cameras,
                                              Geometry::Terrain* terrain,
                                              RunCameraState& camera,
                                              RunMousePickupState& mousePickup,
                                              RunCameraMode normalizedCurrentMode,
                                              RunCameraMode normalizedRestoreMode,
                                              bool attachedFollow,
                                              bool directorGrabbed )
{
    if ( !path || path[0] == '\0' )
    {
        return false;
    }
    std::vector<ReplayPresentationSample> samples;
    ReplayV2LoadResult result;
    if ( !ReplayV2Artifact::LoadPresentation( path, samples, &result ) || samples.size() < 2 )
    {
        return false;
    }

    m_timeline.InstallLoadedPresentation( path,
                                          samples,
                                          result.bodyDictionaryCount,
                                          result.fileBytes,
                                          result.firstFrame,
                                          result.lastFrame );

    if ( activateScrubber )
    {
        SetLiveAdvanceHeld( false );
        CancelToolDragState( interaction, inputRouter );
        ClearCameraFocusForRestore();
        ExitInspectionCamera( cameras,
                              terrain,
                              camera,
                              normalizedRestoreMode,
                              attachedFollow,
                              directorGrabbed,
                              interaction,
                              inputRouter );
        ArmLoadedPresentationScrubber( 0.25f, now );
        interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                         WorldInteractionOwner::ReplayScrub,
                                                         InteractionExitReason::EnterReplay );
        if ( ShouldUseInspectionCamera() )
        {
            EnterInspectionCamera( cameras, camera, normalizedCurrentMode, interaction, inputRouter, mousePickup );
        }
    }

    printf( "[replay] Loaded v2 presentation artifact: path=%s samples=%llu bodies=%llu first_frame=%llu "
            "last_frame=%llu bytes=%llu\n",
            m_timeline.LoadedPresentation().path,
            static_cast<unsigned long long>( m_timeline.LoadedPresentation().samples.size() ),
            static_cast<unsigned long long>( m_timeline.LoadedPresentation().bodyDictionaryCount ),
            static_cast<unsigned long long>( m_timeline.LoadedPresentation().firstFrame ),
            static_cast<unsigned long long>( m_timeline.LoadedPresentation().lastFrame ),
            static_cast<unsigned long long>( m_timeline.LoadedPresentation().fileBytes ) );
    return true;
}
void ReplayRuntime::UpdatePrediction( PhysicsEngine& physics,
                                      const SceneEntityStore& entities,
                                      const SkullbonezCore::Core::EngineConfig& config,
                                      const Physics::PhysicsWorldForces& worldForces,
                                      Threading::WorkerPool& workerPool,
                                      bool scenePhysicsEnabled,
                                      double simulationTimeSinceLastStart,
                                      double simulationTotalTime )
{
    // Concept: the composition root samples owner values, then prediction
    // advances without a ReplayRuntime reach-back. Its value-only result is
    // applied after the worker/publication transition returns.
    const RunReplayPathVisualizerState& path = m_visualPresentation.PathVisualizer();
    const ReplayScrubberView scrubber = m_scrubberOwner.View();
    ReplayPredictionUpdateResult result;
    m_predictionOwner.UpdateFrame( physics,
                                   entities,
                                   config,
                                   worldForces,
                                   workerPool,
                                   m_timeline.Solver().LatestSample(),
                                   path.targetId,
                                   path.targetModelRow,
                                   path.hasTarget,
                                   scrubber.liveAdvanceHeld,
                                   scrubber.historicalSamplePaused,
                                   m_scrubberOwner.TrackPosition( RunReplayTrack::Solver ),
                                   SolverPresentTrackPosition(),
                                   scenePhysicsEnabled,
                                   simulationTimeSinceLastStart,
                                   simulationTotalTime,
                                   REPLAY_PREDICTION_MAX_WORK_MILLISECONDS,
                                   result );
    ApplyPredictionUpdateResult( result );
    PreparePredictionPresentation( physics, entities );
}


void ReplayRuntime::ApplyPredictionUpdateResult( const ReplayPredictionUpdateResult& result )
{
    if ( result.targetModelRowRepaired )
    {
        m_visualPresentation.SetPathTargetModelRow( result.repairedTargetModelRow );
    }
    if ( result.pinSolverScrubberToPresent )
    {
        PinSolverScrubberToPresent();
    }
    for ( std::size_t passIndex = 0; passIndex < result.budgetExpiries.size(); ++passIndex )
    {
        for ( uint32_t count = 0; count < result.budgetExpiries[passIndex]; ++count )
        {
            m_visualPresentation.RecordTrajectoryBudgetExpiry(
                static_cast<SkullbonezCore::Core::MainMemoryReplayBudgetPass>( passIndex ) );
        }
    }
    for ( std::size_t causeIndex = 0; causeIndex < result.rebuildCauses.size(); ++causeIndex )
    {
        for ( uint32_t count = 0; count < result.rebuildCauses[causeIndex]; ++count )
        {
            m_visualPresentation.RecordTrajectoryRebuildCause(
                static_cast<SkullbonezCore::Core::MainMemoryReplayRebuildCause>( causeIndex ) );
        }
    }
}


void ReplayRuntime::PreparePredictionPresentation( PhysicsEngine& physics, const SceneEntityStore& entities )
{
    // Why: live frames and CPU-only archive projection share this publication
    // command. Keeping it separate from drawing prevents validation from
    // becoming a privileged back door into the prediction owner's state.
    const ColliderStore& colliderStore = PhysicsEngine::ReadColliders( physics );
    const RunReplayPathVisualizerState& path = m_visualPresentation.PathVisualizer();
    ReplayPredictionUpdateResult result;
    m_predictionOwner.PreparePresentation( entities,
                                           colliderStore,
                                           path.targetId,
                                           path.targetModelRow,
                                           path.hasTarget,
                                           REPLAY_PREDICTION_MAX_WORK_MILLISECONDS,
                                           result );
    ApplyPredictionUpdateResult( result );
    if ( m_predictionOwner.PresentationView().generationPermitted )
    {
        ApplyPastTrajectoryUpdate(
            m_predictionOwner.RefreshPastTrajectoryStore( m_timeline.Solver(),
                                                          m_visualPresentation.PathVisualizer() ) );
    }
    m_visualPresentation.PreparePathDrawing( PhysicsEngine::ReadBodies( physics ) );
}


} // namespace SkullbonezCore::Runtime
