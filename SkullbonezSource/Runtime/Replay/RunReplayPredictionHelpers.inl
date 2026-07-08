/*
File: SkullbonezSource/Runtime/Replay/RunReplayPredictionHelpers.inl
Purpose:
  Owns replay path, future-node, and prediction sample helper logic used by replay tools.

Mental model:
  This file is included inside RunReplayTools.cpp's anonymous namespace. Retained solver
  samples and prediction samples share body-id lookup, path-budget, and contact-tree helpers.

Glossary:
  Causal markers: Yellow outline fixed at a body's in-place prediction-start
    pose plus a grey outline drawn only at its final resting pose when the
    completed prediction ends at rest — the two-box story of each body. Once
    shown, neither box ever leaves until the prediction is rebuilt.
  Future node: Body discovered by following predicted contact records or, while
    those records are still sparse, by pose divergence away from a target.
  Prediction frame: Replay frame captured from the private prediction engine.
  Body record: Physics-owned row holding pose, velocity, mass, inertia, and
    fixed/dynamic state for one replay body.
  Scene-object group: Collection-owned metadata that folds ragdoll parts to
    their presentation root for replay path visualization.

Invariants:
  - Prediction helpers must honor the shared replay visualizer time budget.
  - This file must only be included from RunReplayTools.cpp inside the anonymous namespace.
  - Prediction backups and frame samples read simulation state from body records;
    ragdoll grouping comes from GameModelCollection, while GameModel remains
    only for presentation-owned timers.

Related:
  - SkullbonezSource/Runtime/Replay/RunReplayTools.cpp
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
  - Agentic/Reference/comment-style-guide.md
*/

double ReplayPredictionElapsedMilliseconds( const std::chrono::steady_clock::time_point& start )
{
    return std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - start ).count();
}

bool ReplayPredictionBudgetExpired( const std::chrono::steady_clock::time_point& start, double budgetMilliseconds )
{
    return budgetMilliseconds > 0.0 && ReplayPredictionElapsedMilliseconds( start ) >= budgetMilliseconds;
}

double ReplayPredictionRemainingMilliseconds( const std::chrono::steady_clock::time_point& start,
                                              double budgetMilliseconds )
{
    if ( budgetMilliseconds <= 0.0 )
    {
        return 0.0;
    }
    return (std::max)( 0.0, budgetMilliseconds - ReplayPredictionElapsedMilliseconds( start ) );
}

double ReplayPredictionRevealSecondsPerSecond( const RunReplayPredictionState& prediction )
{
    // Why: authored shot-list data is allowed to be imperfect. Non-positive
    // rates fall back to real-time pacing instead of freezing the reveal cursor
    // or dividing by zero while the prediction build catches up.
    return prediction.revealClock.secondsPerSecond > 0.0 ? prediction.revealClock.secondsPerSecond : 1.0;
}

// Concept: reveal cursor — the wall-clock playhead of the causal-unfold animation.
//
// Every prediction draw pass clamps to the frame this returns, so the pace of
// the visible tree comes from real time, not from how fast the build job
// happened to finish. While the job is still building, the cursor also clamps
// to the populated prefix and re-anchors at that edge, so a slow build paces
// the unfold without banking "reveal debt" that would snap the animation
// forward the moment the job completes.
// Invariant: the cursor is MONOTONIC per prediction. It plays 0 -> horizon
// exactly once and then holds there, so every revealed line and causal box
// stays on screen; only a rebuild (new future) resets it via the anchor.
ReplayFrameIndex ReplayPredictionRevealFrameIndex( RunReplayPredictionState& prediction,
                                                   ReplayFrameIndex lastAvailableFrame )
{
    const auto now = std::chrono::steady_clock::now();
    if ( !prediction.revealClock.anchorValid )
    {
        prediction.revealClock.anchor = now;
        prediction.revealClock.anchorValid = true;
        return 0;
    }

    const double availableSeconds = static_cast<double>( lastAvailableFrame ) * PHYSICS_FIXED_DT;
    const double elapsedSeconds =
        (std::max)( 0.0, std::chrono::duration<double>( now - prediction.revealClock.anchor ).count() );
    const double revealSecondsPerSecond = ReplayPredictionRevealSecondsPerSecond( prediction );
    double revealSeconds = elapsedSeconds * revealSecondsPerSecond;
    if ( prediction.build.building && revealSeconds > availableSeconds )
    {
        revealSeconds = availableSeconds;
        prediction.revealClock.anchor =
            now - std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                      std::chrono::duration<double>( availableSeconds / revealSecondsPerSecond ) );
    }

    const double revealFrame = revealSeconds / static_cast<double>( PHYSICS_FIXED_DT );
    return (std::min)( lastAvailableFrame, static_cast<ReplayFrameIndex>( revealFrame ) );
}

constexpr const char* REPLAY_PREDICTION_RESERVE_OWNER = "replay_prediction_working_set";
constexpr int REPLAY_PREDICTION_FRAME_CAPACITY =
    static_cast<int>( REPLAY_PREDICTION_MAX_SECONDS / PHYSICS_FIXED_DT ) + 2;
constexpr int REPLAY_PREDICTION_PATH_BUDGET = 100;
constexpr int REPLAY_PREDICTION_RESERVE_HARD_BYTES = 256 * 1024 * 1024;
constexpr std::size_t REPLAY_PREDICTION_DEBUG_CONTACT_INITIAL_MIN = 512u;
constexpr std::size_t REPLAY_PREDICTION_DEBUG_CONTACT_INITIAL_MAX = 2048u;
constexpr std::size_t REPLAY_PREDICTION_DEBUG_CONTACT_GROWTH_CHUNK = 4096u;
// Runtime allocation policy: prediction scratch can grow as the user explores
// larger retained paths. The registered hard cap is a real byte ceiling, not a
// theoretical element-count product; growth count is telemetry so interactive
// replay does not trip a per-run count fuse.
constexpr int REPLAY_PREDICTION_RESERVE_GROWTH_LIMIT = RuntimeAllocation::RUNTIME_RESERVE_REPLAY_GROWTH_LIMIT_UNBOUNDED;

RuntimeAllocation::RuntimeReserveOwnerHandle ReplayPredictionReserveOwner()
{
    static const RuntimeAllocation::RuntimeReserveOwnerHandle owner =
        RuntimeAllocation::RuntimeReserveAllocator::RegisterOwner(
            { REPLAY_PREDICTION_RESERVE_OWNER,
              RuntimeAllocation::RuntimeReserveSubsystem::Replay,
              RuntimeAllocation::RuntimeReservePhase::Replay,
              0,
              REPLAY_PREDICTION_RESERVE_HARD_BYTES,
              REPLAY_PREDICTION_RESERVE_GROWTH_LIMIT,
              true,
              "replay prediction supports large retained path visualization under a hard byte budget" } );
    return owner;
}

template <typename T> bool ReplayPredictionCapacityBytes( std::size_t capacity, uint64_t& outBytes )
{
    constexpr uint64_t elementBytes = static_cast<uint64_t>( sizeof( T ) );
    const uint64_t maxCapacity = ( std::numeric_limits<uint64_t>::max )() / elementBytes;
    if ( capacity > maxCapacity )
    {
        return false;
    }
    outBytes = static_cast<uint64_t>( capacity ) * elementBytes;
    return true;
}

template <typename T>
bool ReplayPredictionFramePayloadBytes( std::size_t frameCount, std::size_t capacityPerFrame, uint64_t& outBytes )
{
    uint64_t bytesPerFrame = 0;
    if ( !ReplayPredictionCapacityBytes<T>( capacityPerFrame, bytesPerFrame ) )
    {
        return false;
    }
    const uint64_t maxValue = ( std::numeric_limits<uint64_t>::max )();
    const uint64_t maxFrameCount = bytesPerFrame > 0 ? maxValue / bytesPerFrame : maxValue;
    if ( frameCount > maxFrameCount )
    {
        return false;
    }
    outBytes = static_cast<uint64_t>( frameCount ) * bytesPerFrame;
    return true;
}

std::size_t RoundUpReplayPredictionCapacity( std::size_t requestedCapacity, std::size_t chunk )
{
    if ( chunk == 0 || requestedCapacity == 0 )
    {
        return requestedCapacity;
    }
    const std::size_t remainder = requestedCapacity % chunk;
    return remainder == 0 ? requestedCapacity : requestedCapacity + ( chunk - remainder );
}

std::size_t ReplayPredictionInitialDebugContactCapacity( int modelCount )
{
    const std::size_t modelScaled = static_cast<std::size_t>( (std::max)( modelCount, 1 ) ) * 8u;
    return std::clamp( modelScaled,
                       REPLAY_PREDICTION_DEBUG_CONTACT_INITIAL_MIN,
                       REPLAY_PREDICTION_DEBUG_CONTACT_INITIAL_MAX );
}

std::size_t ReplayPredictionNextDebugContactCapacity( std::size_t currentCapacity, std::size_t requiredCapacity )
{
    const std::size_t chunked =
        RoundUpReplayPredictionCapacity( requiredCapacity, REPLAY_PREDICTION_DEBUG_CONTACT_GROWTH_CHUNK );
    const std::size_t doubled =
        currentCapacity > 0 ? currentCapacity * 2u : REPLAY_PREDICTION_DEBUG_CONTACT_INITIAL_MIN;
    return (std::max)( chunked, doubled );
}

int ReplayPredictionEngineReserveBytes( const PhysicsEngine& engine )
{
    // Why: seeding the private engine copies several physics-owned vectors.
    // Estimate the live working set before requesting the replay growth scope so
    // those copy allocations are approved under one bounded prediction owner.
    uint64_t bytes = static_cast<uint64_t>( sizeof( PhysicsEngine ) );
    bytes += engine.CollectPhysicsWorldMemoryBytes();
    bytes += engine.CollectDebugAndBroadphaseMemoryBytes();
    bytes += static_cast<uint64_t>( engine.BodyStore().Records().capacity() ) * sizeof( PhysicsBodyRecord );
    bytes += static_cast<uint64_t>( engine.Colliders().Records().capacity() ) * sizeof( ColliderRecord );
    bytes += static_cast<uint64_t>( engine.RenderInstances().Records().capacity() ) *
             sizeof( SkullbonezCore::Rendering::RenderInstanceRecord );
    bytes += static_cast<uint64_t>( engine.RenderInstances().PresentationRecords().capacity() ) *
             sizeof( SkullbonezCore::Rendering::RenderInstancePresentationRecord );
    if ( bytes == 0 || bytes > static_cast<uint64_t>( REPLAY_PREDICTION_RESERVE_HARD_BYTES ) ||
         bytes > static_cast<uint64_t>( ( std::numeric_limits<int>::max )() ) )
    {
        return 0;
    }
    return static_cast<int>( bytes );
}

template <typename T>
bool ReserveReplayPredictionVector( std::vector<T>& values,
                                    std::size_t requestedCapacity,
                                    int frameNumber,
                                    const char* targetName )
{
    if ( requestedCapacity <= values.capacity() )
    {
        return true;
    }
    uint64_t oldBytes = 0;
    uint64_t requestedBytes = 0;
    if ( !ReplayPredictionCapacityBytes<T>( values.capacity(), oldBytes ) ||
         !ReplayPredictionCapacityBytes<T>( requestedCapacity, requestedBytes ) ||
         requestedBytes > static_cast<uint64_t>( REPLAY_PREDICTION_RESERVE_HARD_BYTES ) )
    {
        return false;
    }

    const RuntimeAllocation::RuntimeReserveOwnerHandle owner = ReplayPredictionReserveOwner();
    const RuntimeAllocation::RuntimeReserveGrowthRequest request = { REPLAY_PREDICTION_RESERVE_OWNER,
                                                                     targetName,
                                                                     RuntimeAllocation::RuntimeReservePhase::Replay,
                                                                     frameNumber,
                                                                     static_cast<int>( oldBytes ),
                                                                     static_cast<int>( requestedBytes ),
                                                                     1 };
    const RuntimeAllocation::RuntimeReserveGrowthResult result =
        RuntimeAllocation::RuntimeReserveAllocator::RequestGrowth( owner, request );
    if ( !result.granted )
    {
        return false;
    }

    RuntimeAllocation::RuntimeAllocationScope replayAllocationScope(
        RuntimeAllocation::RuntimeAllocationPhase::Replay );
    RuntimeAllocation::RuntimeReserveOwnerScope ownerScope( owner );
    RuntimeAllocation::RuntimeReserveGrowthScope growthScope( owner,
                                                              RuntimeAllocation::RuntimeReservePhase::Replay,
                                                              result );
    values.reserve( requestedCapacity );
    return requestedCapacity <= values.capacity();
}

template <typename T>
bool ReserveReplayPredictionFramePayloadVectors( std::vector<RunReplayPredictionFrame>& frames,
                                                 std::size_t requestedFrameCount,
                                                 std::size_t requestedCapacityPerFrame,
                                                 int frameNumber,
                                                 const char* targetName,
                                                 std::vector<T> RunReplayPredictionFrame::* member )
{
    // Runtime allocation policy: prediction captures many future frames. Batch
    // the per-frame payload reserves under one replay approval so validation
    // sees one setup event instead of one growth request per future frame.
    if ( requestedCapacityPerFrame == 0 )
    {
        return true;
    }

    uint64_t oldBytes = 0;
    for ( std::size_t i = 0; i < requestedFrameCount; ++i )
    {
        uint64_t frameBytes = 0;
        if ( !ReplayPredictionCapacityBytes<T>( ( frames[i].*member ).capacity(), frameBytes ) ||
             oldBytes > ( std::numeric_limits<uint64_t>::max )() - frameBytes )
        {
            return false;
        }
        oldBytes += frameBytes;
    }
    uint64_t requestedBytes = 0;
    if ( !ReplayPredictionFramePayloadBytes<T>( requestedFrameCount, requestedCapacityPerFrame, requestedBytes ) )
    {
        return false;
    }
    if ( requestedBytes <= oldBytes )
    {
        return true;
    }
    if ( requestedBytes > static_cast<uint64_t>( REPLAY_PREDICTION_RESERVE_HARD_BYTES ) )
    {
        return false;
    }

    const RuntimeAllocation::RuntimeReserveOwnerHandle owner = ReplayPredictionReserveOwner();
    const RuntimeAllocation::RuntimeReserveGrowthRequest request = { REPLAY_PREDICTION_RESERVE_OWNER,
                                                                     targetName,
                                                                     RuntimeAllocation::RuntimeReservePhase::Replay,
                                                                     frameNumber,
                                                                     static_cast<int>( oldBytes ),
                                                                     static_cast<int>( requestedBytes ),
                                                                     1 };
    const RuntimeAllocation::RuntimeReserveGrowthResult result =
        RuntimeAllocation::RuntimeReserveAllocator::RequestGrowth( owner, request );
    if ( !result.granted )
    {
        return false;
    }

    RuntimeAllocation::RuntimeAllocationScope replayAllocationScope(
        RuntimeAllocation::RuntimeAllocationPhase::Replay );
    RuntimeAllocation::RuntimeReserveOwnerScope ownerScope( owner );
    RuntimeAllocation::RuntimeReserveGrowthScope growthScope( owner,
                                                              RuntimeAllocation::RuntimeReservePhase::Replay,
                                                              result );
    for ( std::size_t i = 0; i < requestedFrameCount; ++i )
    {
        ( frames[i].*member ).reserve( requestedCapacityPerFrame );
    }
    return true;
}

// Concept: future-node building is an incremental cache.
//
// Prediction can hold thousands of future frames. Clearing and rebuilding the
// future-impact tree every render frame makes the path visualizer scale with the
// full horizon. These cursors let each frame continue where the last frame stopped.
void ClearReplayPredictionFutureNodeCache( RunReplayPredictionState& prediction )
{
    prediction.futureNodeCache.futureNodes.clear();
    prediction.futureNodeCache.futureNodeBuildScratch.clear();
    prediction.futureNodeCache.futureNodesBuiltFrameCount = 0;
    prediction.futureNodeCache.futureNodesBuiltContactIndex = 0;
    prediction.futureNodeCache.futureNodesBuiltTargetId = ReplayBodyId{};
    prediction.futureNodeCache.futureNodesBuiltRagdollVisuals = prediction.ragdollVisualsEnabled;
    prediction.futureNodeCache.futureNodesBuiltFromBuildFrames = false;
    prediction.futureNodeCache.futureNodesCacheValid = false;
    prediction.futureNodeCache.retainedMarkerCount = 0;
}


const ReplaySolverBodySample* FindReplayBodyById( const ReplaySolverFrameSample& sample, ReplayBodyId id )
{
    for ( const ReplaySolverBodySample& body : sample.bodies )
    {
        if ( body.id.value == id.value )
        {
            return &body;
        }
    }
    return nullptr;
}

ReplayBodyId ReplayBodyIdForModelIndex( const ReplaySolverFrameSample& sample, int modelIndex )
{
    ReplayBodyId id;
    if ( modelIndex < 0 )
    {
        return id;
    }

    if ( modelIndex < static_cast<int>( sample.bodies.size() ) )
    {
        const ReplaySolverBodySample& body = sample.bodies[static_cast<std::size_t>( modelIndex )];
        if ( body.modelIndex == modelIndex )
        {
            return body.id;
        }
    }

    for ( const ReplaySolverBodySample& body : sample.bodies )
    {
        if ( body.modelIndex == modelIndex )
        {
            return body.id;
        }
    }
    return id;
}

const RunReplayPredictionBodySample* FindReplayPredictionBodyById( const RunReplayPredictionFrame& frame,
                                                                   ReplayBodyId id )
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

const RunReplayPredictionBodySample* FindReplayPredictionBodyByModelIndex( const RunReplayPredictionFrame& frame,
                                                                           int modelIndex )
{
    if ( modelIndex < 0 )
    {
        return nullptr;
    }

    if ( modelIndex < static_cast<int>( frame.bodies.size() ) )
    {
        const RunReplayPredictionBodySample& body = frame.bodies[static_cast<std::size_t>( modelIndex )];
        if ( body.modelIndex == modelIndex )
        {
            return &body;
        }
    }

    for ( const RunReplayPredictionBodySample& body : frame.bodies )
    {
        if ( body.modelIndex == modelIndex )
        {
            return &body;
        }
    }
    return nullptr;
}

const ReplaySolverBodySample* FindReplayBodyByModelIndex( const ReplaySolverFrameSample& sample, int modelIndex );

// Why: modelIndex is a cache hint, not identity. The replay id check protects
// against stale hints after body lists are rebuilt or ragdoll parts are folded
// to their collection root.
const ReplaySolverBodySample*
FindReplayBodyByIdWithHint( const ReplaySolverFrameSample& sample, ReplayBodyId id, int modelIndex )
{
    if ( const ReplaySolverBodySample* body = FindReplayBodyByModelIndex( sample, modelIndex ) )
    {
        if ( body->id.value == id.value )
        {
            return body;
        }
    }
    return FindReplayBodyById( sample, id );
}

ReplayBodyId ReplayPredictionBodyIdForModelIndex( const RunReplayPredictionFrame& frame, int modelIndex )
{
    ReplayBodyId id;
    if ( modelIndex < 0 )
    {
        return id;
    }

    if ( const RunReplayPredictionBodySample* body = FindReplayPredictionBodyByModelIndex( frame, modelIndex ) )
    {
        return body->id;
    }
    return id;
}

bool ReplayModelIndexIsRagdollPart( const SkullbonezCore::GameObjects::GameModelCollection& collection, int modelIndex )
{
    return ReplayModelIsRagdollPart( collection, modelIndex );
}

int ReplayRagdollTorsoModelIndexForPart( const SkullbonezCore::GameObjects::GameModelCollection& collection,
                                         int modelIndex )
{
    return collection.RagdollRootModelIndexForPart( modelIndex );
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

Quaternion ReplaySolverBodyOrientation( const ReplaySolverBodySample& body )
{
    Quaternion orientation( body.orientation[0], body.orientation[1], body.orientation[2], body.orientation[3] );
    orientation.Normalise();
    return orientation;
}

const ReplaySolverBodySample* FindReplayBodyByModelIndex( const ReplaySolverFrameSample& sample, int modelIndex )
{
    if ( modelIndex >= 0 && modelIndex < static_cast<int>( sample.bodies.size() ) )
    {
        const ReplaySolverBodySample& body = sample.bodies[static_cast<std::size_t>( modelIndex )];
        if ( body.modelIndex == modelIndex )
        {
            return &body;
        }
    }

    for ( const ReplaySolverBodySample& body : sample.bodies )
    {
        if ( body.modelIndex == modelIndex )
        {
            return &body;
        }
    }
    return nullptr;
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

bool ReplayPredictionBodyHasVisibleLinearMotion( const RunReplayPredictionBodySample& body )
{
    return VectorMagSquared( body.linearVelocity ) >= REPLAY_PREDICTION_CHILD_LINEAR_SPEED_SQ;
}

// Concept: rest is decided by how the story ends, never by a momentary pause.
//
// A body has a resting pose only when the COMPLETED prediction ends with it
// visibly still and it has not drifted across the final grace window. Bodies
// still moving at the horizon end return false: they get a travel line and no
// grey box, because any resting pose we could draw for them would be a guess.
// Invariant: callers must pass a completed frame buffer; a growing build
// prefix has no authoritative final frame.
bool ReplayPredictionBodyRestingPose( const std::vector<RunReplayPredictionFrame>& frames,
                                      std::size_t frameCount,
                                      ReplayBodyId id,
                                      int modelIndexHint,
                                      Vector3& outPosition,
                                      Quaternion& outOrientation )
{
    frameCount = (std::min)( frameCount, frames.size() );
    if ( frameCount < 2 || id.value == 0 )
    {
        return false;
    }

    const RunReplayPredictionBodySample* finalBody =
        FindReplayPredictionBodyByIdWithHint( frames[frameCount - 1], id, modelIndexHint );
    if ( !finalBody || ReplayPredictionBodyHasVisibleLinearMotion( *finalBody ) )
    {
        return false;
    }

    const std::size_t graceSlots =
        (std::min)( static_cast<std::size_t>( REPLAY_PREDICTION_REST_GRACE_FRAMES ), frameCount - 1 );
    const RunReplayPredictionBodySample* graceBody =
        FindReplayPredictionBodyByIdWithHint( frames[frameCount - 1 - graceSlots], id, modelIndexHint );
    if ( !graceBody || ReplayPredictionBodyHasVisibleLinearMotion( *graceBody ) ||
         VectorMagSquared( finalBody->position - graceBody->position ) > REPLAY_PREDICTION_REST_POSITION_EPSILON_SQ )
    {
        return false;
    }

    outPosition = finalBody->position;
    outOrientation = finalBody->orientation;
    return true;
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

std::size_t ReplayPathStrideForSampleCount( std::size_t sampleCount )
{
    if ( sampleCount <= REPLAY_PATH_MAX_SEGMENTS )
    {
        return 1;
    }
    return ( sampleCount + REPLAY_PATH_MAX_SEGMENTS - 1 ) / REPLAY_PATH_MAX_SEGMENTS;
}

void ClearReplayPredictionBaseline( ReplayPredictionBaselineSnapshot& baseline )
{
    baseline.valid = false;
    baseline.comparisonActive = false;
    baseline.rootId = ReplayBodyId{};
    baseline.rootModelIndex = -1;
    baseline.lastFrame = 0;
    baseline.rootPolyline.clear();
    baseline.bodyPoses.clear();
    baseline.divergenceValid = false;
    baseline.divergenceUnits = 0.0f;
}

// Concept: baseline capture freezes the old committed future before a velocity
// edit. It keeps a bounded root path plus completed entry/rest poses so the
// renderer can contrast "what would have happened" against the nudged rebuild.
bool CaptureReplayPredictionBaselineSnapshot( RunReplayPredictionState& prediction,
                                              const std::vector<RunReplayPredictionFrame>& frames,
                                              std::size_t frameCount,
                                              ReplayBodyId rootId,
                                              int rootModelIndex )
{
    frameCount = (std::min)( frameCount, frames.size() );
    ClearReplayPredictionBaseline( prediction.baseline );
    if ( frameCount < 2 || rootId.value == 0 )
    {
        return false;
    }

    const RunReplayPredictionFrame& firstFrame = frames.front();
    const RunReplayPredictionFrame& lastFrame = frames[frameCount - 1];
    const std::size_t bodyCapacity =
        (std::min)( static_cast<std::size_t>( MAX_GAME_MODELS ), firstFrame.bodies.size() );
    const int reserveFrame = static_cast<int>( lastFrame.frameIndex );
    if ( !ReserveReplayPredictionVector( prediction.baseline.rootPolyline,
                                         REPLAY_PREDICTION_BASELINE_ROOT_POINT_CAPACITY,
                                         reserveFrame,
                                         "ReplayPredictionBaselineRootPoint[]" ) ||
         !ReserveReplayPredictionVector( prediction.baseline.bodyPoses,
                                         bodyCapacity,
                                         reserveFrame,
                                         "ReplayPredictionBaselineBodyPose[]" ) )
    {
        ClearReplayPredictionBaseline( prediction.baseline );
        return false;
    }

    prediction.baseline.rootId = rootId;
    prediction.baseline.rootModelIndex = rootModelIndex;
    prediction.baseline.lastFrame = lastFrame.frameIndex;

    const std::size_t rootStride = frameCount <= REPLAY_PREDICTION_BASELINE_ROOT_POINT_CAPACITY
                                       ? 1u
                                       : ( frameCount + REPLAY_PREDICTION_BASELINE_ROOT_POINT_CAPACITY - 1u ) /
                                             REPLAY_PREDICTION_BASELINE_ROOT_POINT_CAPACITY;
    std::size_t ordinal = 0;
    for ( std::size_t frameSlot = 0; frameSlot < frameCount; ++frameSlot )
    {
        const RunReplayPredictionFrame& frame = frames[frameSlot];
        const bool endpointFrame = frameSlot == 0 || frameSlot + 1 == frameCount;
        const std::size_t currentOrdinal = ordinal++;
        if ( !endpointFrame && rootStride > 1u && ( currentOrdinal % rootStride ) != 0u )
        {
            continue;
        }

        const RunReplayPredictionBodySample* body =
            FindReplayPredictionBodyByIdWithHint( frame, rootId, rootModelIndex );
        if ( !body )
        {
            continue;
        }

        ReplayPredictionBaselineRootPoint point;
        point.frameIndex = frame.frameIndex;
        point.position = body->position;
        if ( prediction.baseline.rootPolyline.size() < REPLAY_PREDICTION_BASELINE_ROOT_POINT_CAPACITY )
        {
            prediction.baseline.rootPolyline.push_back( point );
        }
        else if ( endpointFrame && !prediction.baseline.rootPolyline.empty() )
        {
            prediction.baseline.rootPolyline.back() = point;
        }
    }

    for ( const RunReplayPredictionBodySample& body : firstFrame.bodies )
    {
        if ( prediction.baseline.bodyPoses.size() >= bodyCapacity || body.id.value == 0 )
        {
            break;
        }

        Vector3 restPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        Quaternion restOrientation = IDENTITY_QUATERNION;
        if ( !ReplayPredictionBodyRestingPose( frames,
                                               frameCount,
                                               body.id,
                                               body.modelIndex,
                                               restPosition,
                                               restOrientation ) )
        {
            continue;
        }

        ReplayPredictionBaselineBodyPose pose;
        pose.id = body.id;
        pose.modelIndex = body.modelIndex;
        pose.hasEntryPose = true;
        pose.hasRestPose = true;
        pose.entryPosition = body.position;
        pose.entryOrientation = body.orientation;
        pose.entryOrientation.Normalise();
        pose.restPosition = restPosition;
        pose.restOrientation = restOrientation;
        pose.restOrientation.Normalise();
        prediction.baseline.bodyPoses.push_back( pose );
    }

    prediction.baseline.valid = prediction.baseline.rootPolyline.size() >= 2 || !prediction.baseline.bodyPoses.empty();
    prediction.baseline.comparisonActive = prediction.baseline.valid;
    return prediction.baseline.valid;
}

// Concept: divergence is a demo-facing separation metric, not physics authority.
// It sums how far matched bodies' resting endpoints moved between the cold
// baseline and the rebuilt prediction.
void UpdateReplayPredictionBaselineDivergence( RunReplayPredictionState& prediction,
                                               const std::vector<RunReplayPredictionFrame>& frames,
                                               std::size_t frameCount )
{
    ReplayPredictionBaselineSnapshot& baseline = prediction.baseline;
    baseline.divergenceValid = false;
    baseline.divergenceUnits = 0.0f;
    frameCount = (std::min)( frameCount, frames.size() );
    if ( !baseline.valid || frameCount < 2 || baseline.bodyPoses.empty() )
    {
        return;
    }

    float divergence = 0.0f;
    int matchedBodies = 0;
    for ( const ReplayPredictionBaselineBodyPose& baselinePose : baseline.bodyPoses )
    {
        if ( !baselinePose.hasRestPose || baselinePose.id.value == 0 )
        {
            continue;
        }

        Vector3 restPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        Quaternion restOrientation = IDENTITY_QUATERNION;
        if ( !ReplayPredictionBodyRestingPose( frames,
                                               frameCount,
                                               baselinePose.id,
                                               baselinePose.modelIndex,
                                               restPosition,
                                               restOrientation ) )
        {
            continue;
        }

        divergence += VectorMag( restPosition - baselinePose.restPosition );
        ++matchedBodies;
    }

    baseline.divergenceUnits = divergence;
    baseline.divergenceValid = matchedBodies > 0;
}

const ColliderRecord* ReplayColliderRecordForModelIndex( const ColliderStore* colliderStore, int modelIndex );

// Concept: cold baseline drawing deliberately reuses the smooth replay ribbon
// path. It should read as the old future's ghost, never as jaggy debug wire.
void DrawReplayPredictionBaselineSnapshot( const ReplayPredictionBaselineSnapshot& baseline,
                                           const ColliderStore& colliderStore,
                                           RunEditorTracer& tracer )
{
    if ( !baseline.valid )
    {
        return;
    }

    for ( std::size_t i = 1; i < baseline.rootPolyline.size(); ++i )
    {
        const Vector3& previous = baseline.rootPolyline[i - 1].position;
        const Vector3& current = baseline.rootPolyline[i].position;
        if ( VectorMagSquared( current - previous ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
        {
            tracer.AddReplayBaselinePathSegment( previous, current );
        }
    }

    for ( const ReplayPredictionBaselineBodyPose& pose : baseline.bodyPoses )
    {
        const ColliderRecord* collider = ReplayColliderRecordForModelIndex( &colliderStore, pose.modelIndex );
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

struct ReplayPathBoundsContext
{
    bool hasSample = false;
    ReplayFrameIndex firstFrame = 0;
    ReplayFrameIndex lastFrame = 0;
};

void CaptureReplayPathBounds( const ReplaySolverFrameSample& sample, void* userData )
{
    ReplayPathBoundsContext& context = *static_cast<ReplayPathBoundsContext*>( userData );
    if ( !context.hasSample )
    {
        context.hasSample = true;
        context.firstFrame = sample.frameIndex;
    }
    context.lastFrame = sample.frameIndex;
}

struct ReplayPathFutureContext
{
    RunReplayPathVisualizerState* visualizer = nullptr;
    const SkullbonezCore::GameObjects::GameModelCollection* collection = nullptr;
    const std::chrono::steady_clock::time_point* budgetStart = nullptr;
    ReplayBodyId rootId;
    ReplayFrameIndex presentFrame = 0;
    double budgetMilliseconds = 0.0;
    bool includeRagdollVisuals = true;
    bool budgetExpired = false;
};

// Why: the recorder visitor API cannot early-out. The callback records budget
// expiry in the context and turns later visits into cheap no-ops.
bool ReplayPathContextBudgetExpired( ReplayPathFutureContext& context )
{
    if ( context.budgetStart && ReplayPredictionBudgetExpired( *context.budgetStart, context.budgetMilliseconds ) )
    {
        context.budgetExpired = true;
    }
    return context.budgetExpired;
}

bool TryGetReplayFutureDepth( const ReplayPathFutureContext& context,
                              ReplayBodyId id,
                              ReplayFrameIndex frame,
                              int& outDepth )
{
    if ( id.value == 0 )
    {
        return false;
    }
    if ( id.value == context.rootId.value )
    {
        outDepth = 0;
        return frame >= context.presentFrame;
    }

    for ( const RunReplayPathTraceNode& node : context.visualizer->futureNodes )
    {
        if ( node.id.value == id.value && frame >= node.firstFrame )
        {
            outDepth = node.depth;
            return true;
        }
    }
    return false;
}

bool ReplayFutureNodeExists( const RunReplayPathVisualizerState& visualizer, ReplayBodyId id )
{
    for ( const RunReplayPathTraceNode& node : visualizer.futureNodes )
    {
        if ( node.id.value == id.value )
        {
            return true;
        }
    }
    return false;
}

RunReplayPathTarget* FindReplayPathTarget( RunReplayPathVisualizerState& visualizer, ReplayBodyId id )
{
    for ( RunReplayPathTarget& target : visualizer.targets )
    {
        if ( target.id.value == id.value )
        {
            return &target;
        }
    }
    return nullptr;
}

void ApplyPrimaryReplayPathTarget( RunReplayPathVisualizerState& visualizer,
                                   ReplayBodyId id,
                                   int modelIndex,
                                   const char* name )
{
    visualizer.hasTarget = id.value != 0;
    visualizer.targetId = id;
    visualizer.targetModelIndex = modelIndex;
    visualizer.targetName[0] = '\0';
    if ( name && name[0] != '\0' )
    {
        strncpy_s( visualizer.targetName, sizeof( visualizer.targetName ), name, _TRUNCATE );
    }
}

void AddReplayFutureNode( ReplayPathFutureContext& context,
                          ReplayBodyId parentId,
                          int parentModelIndex,
                          ReplayBodyId id,
                          int modelIndex,
                          ReplayFrameIndex firstFrame,
                          const Vector3& contactPoint,
                          const Vector3& contactNormal,
                          int depth )
{
    if ( id.value == 0 || id.value == context.rootId.value || ReplayFutureNodeExists( *context.visualizer, id ) ||
         context.visualizer->futureNodes.size() >= REPLAY_PATH_MAX_FUTURE_NODES )
    {
        return;
    }

    RunReplayPathTraceNode node;
    node.id = id;
    node.parentId = parentId;
    node.modelIndex = modelIndex;
    node.parentModelIndex = parentModelIndex;
    node.firstFrame = firstFrame;
    node.contactPoint = contactPoint;
    node.contactNormal = contactNormal;
    node.depth = depth;
    context.visualizer->futureNodes.push_back( node );
}

void BuildReplayFutureNodes( const ReplaySolverFrameSample& sample, void* userData )
{
    ReplayPathFutureContext& context = *static_cast<ReplayPathFutureContext*>( userData );
    if ( !context.visualizer || sample.frameIndex < context.presentFrame || ReplayPathContextBudgetExpired( context ) )
    {
        return;
    }

    for ( const PhysicsDebugContact& contact : sample.worldSnapshot.debugContacts )
    {
        if ( ReplayPathContextBudgetExpired( context ) )
        {
            return;
        }

        const bool ragdollA = context.collection && ReplayModelIndexIsRagdollPart( *context.collection, contact.bodyA );
        const bool ragdollB = context.collection && ReplayModelIndexIsRagdollPart( *context.collection, contact.bodyB );
        const int modelIndexA = context.collection
                                    ? ReplayRagdollTorsoModelIndexForPart( *context.collection, contact.bodyA )
                                    : contact.bodyA;
        const int modelIndexB = context.collection
                                    ? ReplayRagdollTorsoModelIndexForPart( *context.collection, contact.bodyB )
                                    : contact.bodyB;
        const ReplayBodyId idA = ReplayBodyIdForModelIndex( sample, modelIndexA );
        const ReplayBodyId idB = ReplayBodyIdForModelIndex( sample, modelIndexB );
        int depthA = -1;
        int depthB = -1;
        const bool activeA = TryGetReplayFutureDepth( context, idA, sample.frameIndex, depthA );
        const bool activeB = TryGetReplayFutureDepth( context, idB, sample.frameIndex, depthB );
        if ( activeA && !activeB && ( context.includeRagdollVisuals || !ragdollB ) )
        {
            AddReplayFutureNode( context,
                                 idA,
                                 modelIndexA,
                                 idB,
                                 modelIndexB,
                                 sample.frameIndex,
                                 contact.point,
                                 contact.normal,
                                 depthA + 1 );
        }
        else if ( activeB && !activeA && ( context.includeRagdollVisuals || !ragdollA ) )
        {
            AddReplayFutureNode( context,
                                 idB,
                                 modelIndexB,
                                 idA,
                                 modelIndexA,
                                 sample.frameIndex,
                                 contact.point,
                                 contact.normal * -1.0f,
                                 depthB + 1 );
        }
    }
}

bool ShouldDrawReplayPathSample( std::size_t ordinal, std::size_t stride )
{
    return stride <= 1 || ( ordinal % stride ) == 0;
}

struct ReplayPathRootDrawContext
{
    RunEditorTracer* tracer = nullptr;
    const std::chrono::steady_clock::time_point* budgetStart = nullptr;
    ReplayBodyId rootId;
    ReplayFrameIndex firstFrame = 0;
    ReplayFrameIndex presentFrame = 0;
    ReplayFrameIndex lastFrame = 0;
    double budgetMilliseconds = 0.0;
    std::size_t sampleOrdinal = 0;
    std::size_t sampleStride = 1;
    bool budgetExpired = false;
    bool hasPastPrevious = false;
    bool hasFuturePrevious = false;
    Vector3 pastPrevious = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 futurePrevious = SkullbonezCore::Math::Vector::ZERO_VECTOR;
};

// Why: retained replay path drawing shares the same deadline as prediction so
// the parent Frame/Replay/PathVisualizer marker is the true budget boundary.
bool ReplayPathRootDrawBudgetExpired( ReplayPathRootDrawContext& context )
{
    if ( context.budgetStart && ReplayPredictionBudgetExpired( *context.budgetStart, context.budgetMilliseconds ) )
    {
        context.budgetExpired = true;
    }
    return context.budgetExpired;
}

void DrawReplayRootPath( const ReplaySolverFrameSample& sample, void* userData )
{
    ReplayPathRootDrawContext& context = *static_cast<ReplayPathRootDrawContext*>( userData );
    if ( ReplayPathRootDrawBudgetExpired( context ) )
    {
        return;
    }

    const std::size_t ordinal = context.sampleOrdinal++;
    if ( sample.frameIndex != context.presentFrame && sample.frameIndex != context.lastFrame &&
         !ShouldDrawReplayPathSample( ordinal, context.sampleStride ) )
    {
        return;
    }

    const ReplaySolverBodySample* body = FindReplayBodyById( sample, context.rootId );
    if ( !body )
    {
        return;
    }

    if ( sample.frameIndex <= context.presentFrame )
    {
        if ( context.hasPastPrevious &&
             VectorMagSquared( body->position - context.pastPrevious ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
        {
            const float t = ReplayPathFrameT( sample.frameIndex, context.firstFrame, context.presentFrame );
            context.tracer->AddReplayPathSegment( context.pastPrevious, body->position, 1.0f, t, t );
        }
        context.pastPrevious = body->position;
        context.hasPastPrevious = true;
    }

    if ( sample.frameIndex >= context.presentFrame )
    {
        if ( context.hasFuturePrevious &&
             VectorMagSquared( body->position - context.futurePrevious ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
        {
            const float t = ReplayPathFrameT( sample.frameIndex, context.presentFrame, context.lastFrame );
            context.tracer->AddReplayPathSegment( context.futurePrevious, body->position, 1.0f - t, 1.0f, 1.0f - t );
        }
        context.futurePrevious = body->position;
        context.hasFuturePrevious = true;
    }
}

struct ReplayPathChildDrawState
{
    RunReplayPathTraceNode node;
    bool active = false;
    bool hasIncomingPrevious = false;
    bool hasPrevious = false;
    bool hasMarkerPose = false;
    int markerModelIndex = -1;
    Vector3 incomingPrevious = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 previous = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 markerPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Quaternion markerOrientation = IDENTITY_QUATERNION;
    // Concept: the two-box causal story. Entry is the body's IN-PLACE pose
    // from prediction frame 0 — the wall exactly as the live scene knows it.
    // It is drawn yellow the moment the body visibly moves and never slides.
    // lastMotionFrame times when the grey resting box may pop in.
    bool hasEntryPose = false;
    int entryModelIndex = -1;
    ReplayFrameIndex lastMotionFrame = 0;
    Vector3 entryPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Quaternion entryOrientation = IDENTITY_QUATERNION;
};

struct ReplayPathChildDrawContext
{
    RunEditorTracer* tracer = nullptr;
    const ColliderStore* colliderStore = nullptr;
    const std::chrono::steady_clock::time_point* budgetStart = nullptr;
    std::array<ReplayPathChildDrawState, REPLAY_PATH_MAX_FUTURE_NODES> nodes = {};
    std::size_t nodeCount = 0;
    ReplayFrameIndex presentFrame = 0;
    ReplayFrameIndex lastFrame = 0;
    double budgetMilliseconds = 0.0;
    std::size_t sampleOrdinal = 0;
    std::size_t sampleStride = 1;
    bool budgetExpired = false;
};

// Why: child paths can multiply retained sample count by future-node count. The
// budget check keeps that product from dominating a render frame.
bool ReplayPathChildDrawBudgetExpired( ReplayPathChildDrawContext& context )
{
    if ( context.budgetStart && ReplayPredictionBudgetExpired( *context.budgetStart, context.budgetMilliseconds ) )
    {
        context.budgetExpired = true;
    }
    return context.budgetExpired;
}

// Why: downstream replay markers should show the collider's real authored
// shape, not the broadphase radius used for cheap collision culling.
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

ReplayPredictionRetainedMarker*
FindOrAddReplayPredictionRetainedMarker( RunReplayPredictionState& prediction, ReplayBodyId id, int modelIndex )
{
    if ( id.value == 0 )
    {
        return nullptr;
    }
    for ( std::size_t i = 0; i < prediction.futureNodeCache.retainedMarkerCount; ++i )
    {
        ReplayPredictionRetainedMarker& marker = prediction.futureNodeCache.retainedMarkers[i];
        if ( marker.id.value == id.value )
        {
            if ( modelIndex >= 0 )
            {
                marker.modelIndex = modelIndex;
            }
            return &marker;
        }
    }
    if ( prediction.futureNodeCache.retainedMarkerCount >= prediction.futureNodeCache.retainedMarkers.size() )
    {
        return nullptr;
    }

    ReplayPredictionRetainedMarker& marker =
        prediction.futureNodeCache.retainedMarkers[prediction.futureNodeCache.retainedMarkerCount++];
    marker = ReplayPredictionRetainedMarker{};
    marker.id = id;
    marker.modelIndex = modelIndex;
    return &marker;
}

void RetainReplayPredictionEntryMarker( RunReplayPredictionState& prediction,
                                        ReplayBodyId id,
                                        int modelIndex,
                                        const Vector3& position,
                                        Quaternion orientation )
{
    if ( ReplayPredictionRetainedMarker* marker =
             FindOrAddReplayPredictionRetainedMarker( prediction, id, modelIndex ) )
    {
        marker->hasEntryPose = true;
        marker->entryPosition = position;
        marker->entryOrientation = orientation;
        marker->entryOrientation.Normalise();
    }
}

void RetainReplayPredictionRestMarker( RunReplayPredictionState& prediction,
                                       ReplayBodyId id,
                                       int modelIndex,
                                       const Vector3& position,
                                       Quaternion orientation )
{
    if ( ReplayPredictionRetainedMarker* marker =
             FindOrAddReplayPredictionRetainedMarker( prediction, id, modelIndex ) )
    {
        marker->hasRestPose = true;
        marker->hasHorizonPose = false;
        marker->restPosition = position;
        marker->restOrientation = orientation;
        marker->restOrientation.Normalise();
    }
}

void RetainReplayPredictionHorizonMarker( RunReplayPredictionState& prediction,
                                          ReplayBodyId id,
                                          int modelIndex,
                                          const Vector3& position,
                                          Quaternion orientation )
{
    if ( ReplayPredictionRetainedMarker* marker =
             FindOrAddReplayPredictionRetainedMarker( prediction, id, modelIndex ) )
    {
        if ( marker->hasRestPose )
        {
            return;
        }
        marker->hasHorizonPose = true;
        marker->horizonPosition = position;
        marker->horizonOrientation = orientation;
        marker->horizonOrientation.Normalise();
    }
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

void ReplayRetainedMarkerTrailColor( std::size_t trailOrdinal,
                                     float t,
                                     bool horizonGhost,
                                     float& r,
                                     float& g,
                                     float& b )
{
    const float laneOffset = std::clamp( static_cast<float>( trailOrdinal % 8u ) * 0.016f, 0.0f, 0.10f );
    if ( horizonGhost )
    {
        r = std::clamp( 0.42f + t * 0.20f + laneOffset, 0.38f, 0.74f );
        g = std::clamp( 0.78f + t * 0.16f, 0.72f, 1.0f );
        b = std::clamp( 0.95f + laneOffset * 0.30f, 0.86f, 1.0f );
        return;
    }

    r = std::clamp( 0.86f - t * 0.28f - laneOffset, 0.50f, 0.92f );
    g = std::clamp( 0.84f - t * 0.20f - laneOffset, 0.50f, 0.90f );
    b = std::clamp( 0.90f - t * 0.10f, 0.62f, 0.96f );
}

void DrawReplayPredictionRetainedMarkerTrail( const ReplayPredictionRetainedMarker& marker,
                                              const std::vector<RunReplayPredictionFrame>& frames,
                                              std::size_t frameCount,
                                              ReplayFrameIndex revealFrame,
                                              std::size_t trailOrdinal,
                                              RunEditorTracer& tracer )
{
    frameCount = (std::min)( frameCount, frames.size() );
    if ( !marker.hasEntryPose || marker.id.value == 0 || frameCount < 2 )
    {
        return;
    }

    const ReplayFrameIndex lastFrame = frames[frameCount - 1].frameIndex;
    const std::size_t sampleStride = ReplayRetainedMarkerTrailStrideForFrameCount( frameCount );
    bool hasPrevious = true;
    Vector3 previous = marker.entryPosition;
    const bool horizonGhost = marker.hasHorizonPose && !marker.hasRestPose;

    for ( std::size_t frameSlot = 1; frameSlot < frameCount; ++frameSlot )
    {
        const RunReplayPredictionFrame& frame = frames[frameSlot];
        if ( frame.frameIndex > revealFrame )
        {
            break;
        }

        // Invariant: retained marker trails are sampled polylines, never a
        // direct entry-to-rest chord. Always include the visible reveal edge and
        // completed horizon endpoint, then thin the interior samples.
        const bool endpointFrame = frame.frameIndex == revealFrame || frame.frameIndex == lastFrame;
        if ( !endpointFrame && ( frameSlot % sampleStride ) != 0 )
        {
            continue;
        }

        const RunReplayPredictionBodySample* body =
            FindReplayPredictionBodyByIdWithHint( frame, marker.id, marker.modelIndex );
        if ( !body )
        {
            continue;
        }

        if ( hasPrevious && VectorMagSquared( body->position - previous ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
        {
            const float t = ReplayPathFrameT( frame.frameIndex, 0, lastFrame );
            float r = 0.82f;
            float g = 0.82f;
            float b = 0.88f;
            ReplayRetainedMarkerTrailColor( trailOrdinal, t, horizonGhost, r, g, b );
            tracer.AddReplayCausalTrailSegment( previous, body->position, r, g, b );
        }
        previous = body->position;
        hasPrevious = true;
    }
}

void DrawReplayPredictionRetainedMarkers( const RunReplayPredictionState& prediction,
                                          const std::vector<RunReplayPredictionFrame>& frames,
                                          std::size_t frameCount,
                                          ReplayFrameIndex revealFrame,
                                          const ColliderStore& colliderStore,
                                          RunEditorTracer& tracer )
{
    // Invariant: marker emission is bounded by MAX_GAME_MODELS and independent
    // of the visualizer budget. Lines may degrade under load; already-revealed
    // yellow/grey boxes must not.
    for ( std::size_t i = 0; i < prediction.futureNodeCache.retainedMarkerCount; ++i )
    {
        const ReplayPredictionRetainedMarker& marker = prediction.futureNodeCache.retainedMarkers[i];
        const ColliderRecord* collider = ReplayColliderRecordForModelIndex( &colliderStore, marker.modelIndex );
        if ( !collider )
        {
            continue;
        }
        DrawReplayPredictionRetainedMarkerTrail( marker, frames, frameCount, revealFrame, i, tracer );
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

void RetainReplayPredictionEndStateMarkers( RunReplayPredictionState& prediction,
                                            ReplayFrameIndex revealFrame,
                                            const std::vector<RunReplayPredictionFrame>& completeFrames,
                                            std::size_t completeFrameCount )
{
    completeFrameCount = (std::min)( completeFrameCount, completeFrames.size() );
    if ( completeFrameCount < 2 || revealFrame < completeFrames[completeFrameCount - 1].frameIndex )
    {
        return;
    }

    // Why: late in the 200-brick wall prediction, ownership can move from the
    // affected-body fallback into the future-node tree faster than the budgeted
    // line scan can rediscover every brick. The stable end state is cheap to
    // prove from the final and grace frames. Resting bodies get grey boxes;
    // bodies still moving when the event horizon ends get a ghost endpoint.
    for ( std::size_t i = 0; i < prediction.futureNodeCache.retainedMarkerCount; ++i )
    {
        ReplayPredictionRetainedMarker& marker = prediction.futureNodeCache.retainedMarkers[i];
        if ( !marker.hasEntryPose || marker.hasRestPose || marker.hasHorizonPose )
        {
            continue;
        }

        Vector3 restPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        Quaternion restOrientation = IDENTITY_QUATERNION;
        if ( ReplayPredictionBodyRestingPose( completeFrames,
                                              completeFrameCount,
                                              marker.id,
                                              marker.modelIndex,
                                              restPosition,
                                              restOrientation ) )
        {
            RetainReplayPredictionRestMarker( prediction, marker.id, marker.modelIndex, restPosition, restOrientation );
            continue;
        }

        const RunReplayPredictionBodySample* finalBody =
            FindReplayPredictionBodyByIdWithHint( completeFrames[completeFrameCount - 1],
                                                  marker.id,
                                                  marker.modelIndex );
        if ( !finalBody )
        {
            continue;
        }
        if ( VectorMagSquared( finalBody->position - marker.entryPosition ) <= REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ &&
             !ReplayPredictionBodyHasVisibleLinearMotion( *finalBody ) )
        {
            continue;
        }

        RetainReplayPredictionHorizonMarker( prediction,
                                             marker.id,
                                             finalBody->modelIndex,
                                             finalBody->position,
                                             finalBody->orientation );
    }
}

void CaptureReplayChildMarkerPose( ReplayPathChildDrawState& drawState,
                                   const Vector3& position,
                                   const Quaternion& orientation,
                                   int modelIndex )
{
    drawState.markerPosition = position;
    drawState.markerOrientation = orientation;
    drawState.markerModelIndex = modelIndex;
    drawState.hasMarkerPose = true;
}

void DrawReplayChildFinalMarkers( ReplayPathChildDrawContext& context )
{
    // Why: downstream body markers summarize where each transferred body ends
    // up in the visible prefix. Stamping contact-time poses made boxes look cut
    // short while their gray future trails continued past the outline.
    for ( std::size_t i = 0; i < context.nodeCount; ++i )
    {
        if ( ReplayPathChildDrawBudgetExpired( context ) )
        {
            return;
        }

        const ReplayPathChildDrawState& drawState = context.nodes[i];
        if ( !drawState.hasMarkerPose )
        {
            continue;
        }

        if ( const ColliderRecord* collider =
                 ReplayColliderRecordForModelIndex( context.colliderStore, drawState.markerModelIndex ) )
        {
            context.tracer->AddReplayFutureTargetMarker( drawState.markerPosition,
                                                         drawState.markerOrientation,
                                                         collider->shape,
                                                         drawState.node.depth );
        }
    }
}

// Concept: causal markers are the two-box story of each affected body.
//
// Yellow is fixed at the body's last still pose before it visibly moved — for
// a wall brick, its perfect-formation pose. Grey pops in ONLY at the body's
// final resting pose, and only when the completed prediction actually ends
// with it at rest; a body still moving at the horizon end gets a travel line
// and nothing else. Neither box ever slides.
void DrawReplayPredictionCausalMarkers( RunReplayPredictionState& prediction,
                                        ReplayPathChildDrawContext& context,
                                        ReplayFrameIndex revealFrame,
                                        const std::vector<RunReplayPredictionFrame>* completeFrames,
                                        std::size_t completeFrameCount )
{
    for ( std::size_t i = 0; i < context.nodeCount; ++i )
    {
        const ReplayPathChildDrawState& drawState = context.nodes[i];
        if ( drawState.hasEntryPose )
        {
            RetainReplayPredictionEntryMarker( prediction,
                                               drawState.node.id,
                                               drawState.entryModelIndex,
                                               drawState.entryPosition,
                                               drawState.entryOrientation );
        }

        // Why: completeFrames is null while the job is still building — a
        // growing prefix has no authoritative ending, so no grey box may
        // exist yet. The reveal timing check keeps the grey pop causal: it
        // appears only after the cursor has watched the body stop.
        if ( !drawState.active || !completeFrames )
        {
            continue;
        }
        if ( revealFrame < drawState.lastMotionFrame + REPLAY_PREDICTION_REST_GRACE_FRAMES )
        {
            continue;
        }
        Vector3 restPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        Quaternion restOrientation = IDENTITY_QUATERNION;
        if ( !ReplayPredictionBodyRestingPose( *completeFrames,
                                               completeFrameCount,
                                               drawState.node.id,
                                               drawState.node.modelIndex,
                                               restPosition,
                                               restOrientation ) )
        {
            continue;
        }
        RetainReplayPredictionRestMarker( prediction,
                                          drawState.node.id,
                                          drawState.node.modelIndex,
                                          restPosition,
                                          restOrientation );
    }
}

void ReplayChildIncomingColor( int depth, float t, float& r, float& g, float& b )
{
    const float depthFade = std::clamp( static_cast<float>( depth - 1 ) * 0.10f, 0.0f, 0.36f );
    r = std::clamp( 0.96f - depthFade * 0.55f, 0.44f, 1.0f );
    g = std::clamp( 0.48f + t * 0.34f - depthFade * 0.36f, 0.28f, 0.88f );
    b = std::clamp( 0.16f + t * 0.20f - depthFade * 0.18f, 0.10f, 0.52f );
}

void ReplayChildFutureColor( int depth, float t, float& r, float& g, float& b )
{
    const float depthFade = std::clamp( static_cast<float>( depth - 1 ) * 0.08f, 0.0f, 0.30f );
    const float shade = std::clamp( 0.48f + t * 0.28f - depthFade, 0.25f, 0.78f );
    r = shade;
    g = shade;
    b = shade + 0.06f;
}

void DrawReplayPredictionRagdollTorsoTrails( const std::vector<RunReplayPredictionFrame>& frames,
                                             std::size_t frameCount,
                                             ReplayFrameIndex revealFrame,
                                             const SkullbonezCore::GameObjects::GameModelCollection& collection,
                                             RunEditorTracer& tracer,
                                             const std::chrono::steady_clock::time_point& budgetStart,
                                             double budgetMilliseconds )
{
    const int modelCount = collection.SceneEntityCount();
    frameCount = (std::min)( frameCount, frames.size() );
    if ( frameCount < 2 || modelCount <= 0 )
    {
        return;
    }

    const ReplayFrameIndex lastFrame = frames[frameCount - 1].frameIndex;
    const std::size_t sampleStride = ReplayPathStrideForSampleCount( frameCount );
    for ( int modelIndex = 0; modelIndex < modelCount; ++modelIndex )
    {
        if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
        {
            return;
        }

        if ( !ReplayModelIsRagdollTorso( collection, modelIndex ) )
        {
            continue;
        }

        bool hasPrevious = false;
        Vector3 previous = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        std::size_t ordinal = 0;
        for ( std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            const RunReplayPredictionFrame& frame = frames[frameIndex];
            if ( frame.frameIndex > revealFrame )
            {
                break;
            }
            if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
            {
                return;
            }

            // Why: the reveal-edge frame must always draw, or trail tips would
            // advance in visible stride-sized jumps instead of growing smoothly.
            const std::size_t currentOrdinal = ordinal++;
            if ( frame.frameIndex != lastFrame && frame.frameIndex != revealFrame &&
                 !ShouldDrawReplayPathSample( currentOrdinal, sampleStride ) )
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
                tracer.AddReplayPathSegment( previous, body->position, 0.50f + 0.28f * ( 1.0f - t ), 0.96f, 0.92f );
            }
            previous = body->position;
            hasPrevious = true;
        }
    }
}

struct ReplayPredictionAffectedBodyTrail
{
    ReplayBodyId id;
    int modelIndex = -1;
    std::size_t firstFrameSlot = 0;
    ReplayFrameIndex firstFrame = 0;
    // Concept: same two-box causal story as ReplayPathChildDrawState. Entry is
    // the body's in-place pose from prediction frame 0 (yellow, fixed);
    // lastMotionFrame times when the grey resting box may pop in. The grey
    // pose itself always comes from the completed buffer's final frame.
    ReplayFrameIndex lastMotionFrame = 0;
    Vector3 previous = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 entryPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Quaternion entryOrientation = IDENTITY_QUATERNION;
};

bool ReplayPredictionIdInFutureNodes( const std::vector<RunReplayPathTraceNode>& nodes, ReplayBodyId id )
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

void ReplayAffectedBodyTrailColor( std::size_t trailOrdinal, float t, float& r, float& g, float& b )
{
    const float laneOffset = std::clamp( static_cast<float>( trailOrdinal % 6u ) * 0.025f, 0.0f, 0.125f );
    r = std::clamp( 1.00f - t * 0.32f - laneOffset * 0.40f, 0.55f, 1.00f );
    g = std::clamp( 0.58f + t * 0.28f + laneOffset, 0.48f, 0.94f );
    b = std::clamp( 0.14f + t * 0.42f + laneOffset * 0.50f, 0.10f, 0.72f );
}

void DrawReplayPredictionAffectedBodyTrails( const std::vector<RunReplayPredictionFrame>& frames,
                                             std::size_t frameCount,
                                             RunReplayPredictionState& prediction,
                                             ReplayFrameIndex revealFrame,
                                             bool bufferComplete,
                                             ReplayBodyId rootId,
                                             int rootModelIndex,
                                             const std::vector<RunReplayPathTraceNode>& futureNodes,
                                             const SkullbonezCore::GameObjects::GameModelCollection& collection,
                                             const ColliderStore& colliderStore,
                                             RunEditorTracer& tracer,
                                             const std::chrono::steady_clock::time_point& budgetStart,
                                             double budgetMilliseconds )
{
    frameCount = (std::min)( frameCount, frames.size() );
    if ( frameCount < 2 || rootId.value == 0 )
    {
        return;
    }

    // Concept: affected-body trails are visual evidence, not contact authority.
    //
    // The future-node cache feeds both the cause window and child path renderer.
    // This pass exists only as a visual fallback while that cache has not yet
    // published a body; it skips ids already represented by either contact- or
    // motion-derived nodes.
    std::array<ReplayPredictionAffectedBodyTrail, REPLAY_PATH_MAX_FUTURE_NODES> trails = {};
    std::size_t trailCount = 0;
    // Why: budget exhaustion may stop SCANNING, never marker drawing. Bailing
    // out of the whole pass made yellow boxes flicker under load; instead the
    // scan stops early and whatever trails exist still get their markers.
    bool scanBudgetExhausted = false;
    const RunReplayPredictionFrame& firstFrame = frames.front();
    for ( const RunReplayPredictionBodySample& initialBody : firstFrame.bodies )
    {
        if ( scanBudgetExhausted || trailCount >= REPLAY_PATH_MAX_FUTURE_NODES )
        {
            break;
        }
        if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
        {
            scanBudgetExhausted = true;
            break;
        }
        if ( initialBody.id.value == 0 || initialBody.id.value == rootId.value ||
             initialBody.modelIndex == rootModelIndex ||
             ReplayPredictionIdInFutureNodes( futureNodes, initialBody.id ) )
        {
            continue;
        }
        if ( ReplayModelIndexIsRagdollPart( collection, initialBody.modelIndex ) )
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
            if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
            {
                scanBudgetExhausted = true;
                break;
            }

            const RunReplayPredictionBodySample* body =
                FindReplayPredictionBodyByIdWithHint( frames[frameSlot], initialBody.id, initialBody.modelIndex );
            if ( !body )
            {
                continue;
            }
            if ( !ReplayPredictionBodyHasVisibleLinearMotion( *body ) )
            {
                continue;
            }

            // Why: entry is the body's IN-PLACE pose from prediction frame 0 —
            // the wall exactly as the live scene knows it. Never a sampled
            // pose from after the impulse arrived.
            ReplayPredictionAffectedBodyTrail& trail = trails[trailCount++];
            trail.id = initialBody.id;
            trail.modelIndex = body->modelIndex;
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

    if ( trailCount == 0 )
    {
        return;
    }

    const ReplayFrameIndex lastFrame = frames[frameCount - 1].frameIndex;
    const std::size_t sampleStride = ReplayPathStrideForSampleCount( frameCount );
    for ( std::size_t trailIndex = 0; trailIndex < trailCount && !scanBudgetExhausted; ++trailIndex )
    {
        ReplayPredictionAffectedBodyTrail& trail = trails[trailIndex];
        for ( std::size_t frameSlot = trail.firstFrameSlot + 1; frameSlot < frameCount; ++frameSlot )
        {
            if ( frames[frameSlot].frameIndex > revealFrame )
            {
                break;
            }
            if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
            {
                scanBudgetExhausted = true;
                break;
            }

            const RunReplayPredictionFrame& frame = frames[frameSlot];
            if ( frame.frameIndex != lastFrame && frame.frameIndex != revealFrame &&
                 !ShouldDrawReplayPathSample( frameSlot, sampleStride ) )
            {
                continue;
            }

            const RunReplayPredictionBodySample* body =
                FindReplayPredictionBodyByIdWithHint( frame, trail.id, trail.modelIndex );
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
                ReplayAffectedBodyTrailColor( trailIndex, t, r, g, b );
                tracer.AddReplayPathSegment( trail.previous, body->position, r, g, b );
            }

            if ( ReplayPredictionBodyHasVisibleLinearMotion( *body ) )
            {
                trail.lastMotionFrame = frame.frameIndex;
            }
            trail.previous = body->position;
            trail.modelIndex = body->modelIndex;
        }
    }

    // Why: no budget check here — marker emission is bounded and cheap, and
    // "once rendered, a causal box never leaves" outranks the budget. Only the
    // frame scans above may be cut short.
    for ( std::size_t trailIndex = 0; trailIndex < trailCount; ++trailIndex )
    {
        const ReplayPredictionAffectedBodyTrail& trail = trails[trailIndex];
        if ( !ReplayColliderRecordForModelIndex( &colliderStore, trail.modelIndex ) )
        {
            continue;
        }

        RetainReplayPredictionEntryMarker( prediction,
                                           trail.id,
                                           trail.modelIndex,
                                           trail.entryPosition,
                                           trail.entryOrientation );
        // Why: grey exists only for stories that end at rest inside the
        // completed horizon — see DrawReplayPredictionCausalMarkers.
        if ( !bufferComplete || revealFrame < trail.lastMotionFrame + REPLAY_PREDICTION_REST_GRACE_FRAMES )
        {
            continue;
        }
        Vector3 restPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        Quaternion restOrientation = IDENTITY_QUATERNION;
        if ( ReplayPredictionBodyRestingPose( frames,
                                              frameCount,
                                              trail.id,
                                              trail.modelIndex,
                                              restPosition,
                                              restOrientation ) )
        {
            RetainReplayPredictionRestMarker( prediction, trail.id, trail.modelIndex, restPosition, restOrientation );
        }
    }
}

void DrawReplayChildPaths( const ReplaySolverFrameSample& sample, void* userData )
{
    ReplayPathChildDrawContext& context = *static_cast<ReplayPathChildDrawContext*>( userData );
    if ( ReplayPathChildDrawBudgetExpired( context ) )
    {
        return;
    }

    const std::size_t ordinal = context.sampleOrdinal++;
    bool importantChildFrame = sample.frameIndex == context.presentFrame;
    for ( std::size_t i = 0; i < context.nodeCount; ++i )
    {
        if ( sample.frameIndex == context.nodes[i].node.firstFrame )
        {
            importantChildFrame = true;
            break;
        }
    }
    const bool skipSample =
        sample.frameIndex < context.presentFrame || ( sample.frameIndex != context.lastFrame && !importantChildFrame &&
                                                      !ShouldDrawReplayPathSample( ordinal, context.sampleStride ) );
    if ( skipSample )
    {
        return;
    }

    for ( std::size_t i = 0; i < context.nodeCount; ++i )
    {
        if ( ReplayPathChildDrawBudgetExpired( context ) )
        {
            return;
        }

        ReplayPathChildDrawState& drawState = context.nodes[i];
        const ReplaySolverBodySample* body =
            FindReplayBodyByIdWithHint( sample, drawState.node.id, drawState.node.modelIndex );
        if ( !body )
        {
            continue;
        }

        if ( sample.frameIndex <= drawState.node.firstFrame )
        {
            if ( drawState.hasIncomingPrevious &&
                 VectorMagSquared( body->position - drawState.incomingPrevious ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
            {
                const float t = ReplayPathFrameT( sample.frameIndex, context.presentFrame, drawState.node.firstFrame );
                float r = 0.92f;
                float g = 0.54f;
                float b = 0.18f;
                ReplayChildIncomingColor( drawState.node.depth, t, r, g, b );
                context.tracer->AddReplayPathSegment( drawState.incomingPrevious, body->position, r, g, b );
            }
            drawState.incomingPrevious = body->position;
            drawState.hasIncomingPrevious = true;
        }

        if ( sample.frameIndex >= drawState.node.firstFrame && drawState.hasPrevious &&
             VectorMagSquared( body->position - drawState.previous ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
        {
            const float t = ReplayPathFrameT( sample.frameIndex, drawState.node.firstFrame, context.lastFrame );
            float r = 0.5f;
            float g = 0.5f;
            float b = 0.56f;
            ReplayChildFutureColor( drawState.node.depth, t, r, g, b );
            context.tracer->AddReplayPathSegment( drawState.previous, body->position, r, g, b );
        }
        if ( sample.frameIndex >= drawState.node.firstFrame )
        {
            CaptureReplayChildMarkerPose( drawState,
                                          body->position,
                                          ReplaySolverBodyOrientation( *body ),
                                          body->modelIndex );
            drawState.previous = body->position;
            drawState.hasPrevious = true;
        }
    }
}

struct ReplayPredictionFutureContext
{
    RunReplayPredictionState* prediction = nullptr;
    std::vector<RunReplayPathTraceNode>* nodes = nullptr;
    const SkullbonezCore::GameObjects::GameModelCollection* collection = nullptr;
    ReplayBodyId rootId;
    bool includeRagdollVisuals = true;
};

bool TryGetReplayPredictionFutureDepth( const ReplayPredictionFutureContext& context,
                                        ReplayBodyId id,
                                        ReplayFrameIndex frame,
                                        int& outDepth )
{
    if ( id.value == 0 )
    {
        return false;
    }
    if ( id.value == context.rootId.value )
    {
        outDepth = 0;
        return true;
    }

    const std::vector<RunReplayPathTraceNode>& nodes =
        context.nodes ? *context.nodes : context.prediction->futureNodeCache.futureNodeBuildScratch;
    for ( const RunReplayPathTraceNode& node : nodes )
    {
        if ( node.id.value == id.value && frame >= node.firstFrame )
        {
            outDepth = node.depth;
            return true;
        }
    }
    return false;
}

RunReplayPathTraceNode* FindReplayPredictionFutureNode( std::vector<RunReplayPathTraceNode>& nodes, ReplayBodyId id )
{
    for ( RunReplayPathTraceNode& node : nodes )
    {
        if ( node.id.value == id.value )
        {
            return &node;
        }
    }
    return nullptr;
}

void AddReplayPredictionFutureNode( ReplayPredictionFutureContext& context,
                                    ReplayBodyId parentId,
                                    int parentModelIndex,
                                    ReplayBodyId id,
                                    int modelIndex,
                                    ReplayFrameIndex firstFrame,
                                    const Vector3& contactPoint,
                                    const Vector3& contactNormal,
                                    int depth,
                                    bool contactDerived )
{
    if ( id.value == 0 || id.value == context.rootId.value || !context.nodes )
    {
        return;
    }

    if ( RunReplayPathTraceNode* existing = FindReplayPredictionFutureNode( *context.nodes, id ) )
    {
        if ( contactDerived && !existing->contactDerived )
        {
            existing->parentId = parentId;
            existing->parentModelIndex = parentModelIndex;
            existing->modelIndex = modelIndex;
            existing->firstFrame = firstFrame;
            existing->contactPoint = contactPoint;
            existing->contactNormal = contactNormal;
            existing->depth = depth;
            existing->contactDerived = true;
        }
        return;
    }
    if ( context.nodes->size() >= REPLAY_PATH_MAX_FUTURE_NODES )
    {
        return;
    }

    RunReplayPathTraceNode node;
    node.id = id;
    node.parentId = parentId;
    node.modelIndex = modelIndex;
    node.parentModelIndex = parentModelIndex;
    node.firstFrame = firstFrame;
    node.contactPoint = contactPoint;
    node.contactNormal = contactNormal;
    node.depth = depth;
    node.contactDerived = contactDerived;
    context.nodes->push_back( node );
}

bool BuildReplayPredictionFutureNodes( const RunReplayPredictionFrame& frame,
                                       ReplayPredictionFutureContext& context,
                                       std::size_t startContactIndex,
                                       const std::chrono::steady_clock::time_point& budgetStart,
                                       double budgetMilliseconds,
                                       std::size_t& outNextContactIndex )
{
    outNextContactIndex = (std::min)( startContactIndex, frame.debugContacts.size() );
    for ( std::size_t contactIndex = outNextContactIndex; contactIndex < frame.debugContacts.size(); ++contactIndex )
    {
        // Invariant: if the deadline lands in a contact-heavy frame, report the
        // next contact index instead of advancing the frame cursor. The next
        // render frame resumes inside this same prediction frame.
        if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
        {
            return false;
        }

        const PhysicsDebugContact& contact = frame.debugContacts[contactIndex];
        const bool ragdollA = context.collection && ReplayModelIndexIsRagdollPart( *context.collection, contact.bodyA );
        const bool ragdollB = context.collection && ReplayModelIndexIsRagdollPart( *context.collection, contact.bodyB );
        const int modelIndexA = context.collection
                                    ? ReplayRagdollTorsoModelIndexForPart( *context.collection, contact.bodyA )
                                    : contact.bodyA;
        const int modelIndexB = context.collection
                                    ? ReplayRagdollTorsoModelIndexForPart( *context.collection, contact.bodyB )
                                    : contact.bodyB;
        const ReplayBodyId idA = ReplayPredictionBodyIdForModelIndex( frame, modelIndexA );
        const ReplayBodyId idB = ReplayPredictionBodyIdForModelIndex( frame, modelIndexB );
        int depthA = -1;
        int depthB = -1;
        const bool activeA = TryGetReplayPredictionFutureDepth( context, idA, frame.frameIndex, depthA );
        const bool activeB = TryGetReplayPredictionFutureDepth( context, idB, frame.frameIndex, depthB );
        if ( activeA && !activeB && ( context.includeRagdollVisuals || !ragdollB ) )
        {
            AddReplayPredictionFutureNode( context,
                                           idA,
                                           modelIndexA,
                                           idB,
                                           modelIndexB,
                                           frame.frameIndex,
                                           contact.point,
                                           contact.normal,
                                           depthA + 1,
                                           true );
        }
        else if ( activeB && !activeA && ( context.includeRagdollVisuals || !ragdollA ) )
        {
            AddReplayPredictionFutureNode( context,
                                           idB,
                                           modelIndexB,
                                           idA,
                                           modelIndexA,
                                           frame.frameIndex,
                                           contact.point,
                                           contact.normal * -1.0f,
                                           depthB + 1,
                                           true );
        }
        outNextContactIndex = contactIndex + 1;
    }
    outNextContactIndex = 0;
    return true;
}

bool BuildReplayPredictionAffectedFutureNodes( const std::vector<RunReplayPredictionFrame>& frames,
                                               std::size_t frameCount,
                                               ReplayPredictionFutureContext& context,
                                               const std::chrono::steady_clock::time_point& budgetStart,
                                               double budgetMilliseconds )
{
    frameCount = (std::min)( frameCount, frames.size() );
    if ( frameCount < 2 || context.rootId.value == 0 || !context.nodes )
    {
        return true;
    }

    const RunReplayPredictionFrame& firstFrame = frames.front();
    const RunReplayPredictionBodySample* rootBody = FindReplayPredictionBodyById( firstFrame, context.rootId );
    const int rootModelIndex = rootBody ? rootBody->modelIndex : -1;

    // Concept: motion-derived future nodes populate the cause window while the
    // contact graph is still sparse. A later contact-derived node replaces this
    // fallback, so the tree becomes more causal as solver contact data arrives.
    for ( const RunReplayPredictionBodySample& initialBody : firstFrame.bodies )
    {
        if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
        {
            return false;
        }
        if ( context.nodes->size() >= REPLAY_PATH_MAX_FUTURE_NODES )
        {
            return true;
        }
        if ( initialBody.id.value == 0 || initialBody.id.value == context.rootId.value ||
             ( rootModelIndex >= 0 && initialBody.modelIndex == rootModelIndex ) )
        {
            continue;
        }
        if ( context.collection && ReplayModelIndexIsRagdollPart( *context.collection, initialBody.modelIndex ) )
        {
            continue;
        }

        for ( std::size_t frameSlot = 1; frameSlot < frameCount; ++frameSlot )
        {
            if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
            {
                return false;
            }

            const RunReplayPredictionBodySample* body =
                FindReplayPredictionBodyByIdWithHint( frames[frameSlot], initialBody.id, initialBody.modelIndex );
            if ( !body )
            {
                continue;
            }

            if ( !ReplayPredictionBodyHasVisibleLinearMotion( *body ) )
            {
                continue;
            }

            AddReplayPredictionFutureNode( context,
                                           context.rootId,
                                           rootModelIndex,
                                           initialBody.id,
                                           body->modelIndex,
                                           frames[frameSlot].frameIndex,
                                           body->position,
                                           ReplayNormalizeOr( body->linearVelocity, Vector3( 0.0f, 1.0f, 0.0f ) ),
                                           1,
                                           false );
            break;
        }
    }
    return true;
}

void UpdateReplayPredictionFutureNodeCache( RunReplayPredictionState& prediction,
                                            const std::vector<RunReplayPredictionFrame>& frames,
                                            std::size_t frameCount,
                                            bool usingBuildFrames,
                                            const SkullbonezCore::GameObjects::GameModelCollection& collection,
                                            ReplayBodyId rootId,
                                            const std::chrono::steady_clock::time_point& budgetStart,
                                            double budgetMilliseconds )
{
    // Invariant: frameCount is the populated prefix of frames. buildFrames is
    // pre-sized for the whole prediction horizon, so using frames.size() while
    // building would scan empty rows and mark the future-node cache complete
    // before contacts have been captured.
    frameCount = (std::min)( frameCount, frames.size() );
    const bool completingBuildFrames = !usingBuildFrames &&
                                       prediction.futureNodeCache.futureNodesBuiltFromBuildFrames &&
                                       prediction.futureNodeCache.futureNodesBuiltFrameCount <= frameCount;
    const bool sourceMismatch =
        prediction.futureNodeCache.futureNodesBuiltFromBuildFrames != usingBuildFrames && !completingBuildFrames;
    // Invariant: these inputs define the meaning of the cached tree. Any change
    // means old future nodes may point at the wrong root or include the wrong
    // ragdoll aggregation policy.
    const bool cacheMismatch =
        !prediction.futureNodeCache.futureNodesCacheValid ||
        prediction.futureNodeCache.futureNodesBuiltTargetId.value != rootId.value ||
        prediction.futureNodeCache.futureNodesBuiltRagdollVisuals != prediction.ragdollVisualsEnabled ||
        sourceMismatch || prediction.futureNodeCache.futureNodesBuiltFrameCount > frameCount;
    if ( cacheMismatch )
    {
        ClearReplayPredictionFutureNodeCache( prediction );
        prediction.futureNodeCache.futureNodesBuiltTargetId = rootId;
        prediction.futureNodeCache.futureNodesBuiltRagdollVisuals = prediction.ragdollVisualsEnabled;
        prediction.futureNodeCache.futureNodesBuiltFromBuildFrames = usingBuildFrames;
        prediction.futureNodeCache.futureNodesCacheValid = rootId.value != 0;
    }
    else if ( completingBuildFrames )
    {
        prediction.futureNodeCache.futureNodesBuiltFromBuildFrames = false;
    }

    if ( rootId.value == 0 || frameCount == 0 || !prediction.futureNodeCache.futureNodesCacheValid )
    {
        return;
    }

    auto publishScratch = [&]()
    {
        // Why: the renderer reads futureNodes only after this builder returns.
        // Copying the scratch prefix here lets cause/effect paths grow over
        // frames without exposing a vector while it is being mutated.
        prediction.futureNodeCache.futureNodes = prediction.futureNodeCache.futureNodeBuildScratch;
    };

    if ( prediction.futureNodeCache.futureNodeBuildScratch.size() >= REPLAY_PATH_MAX_FUTURE_NODES )
    {
        prediction.futureNodeCache.futureNodesBuiltFrameCount = frameCount;
        prediction.futureNodeCache.futureNodesBuiltContactIndex = 0;
        publishScratch();
        return;
    }

    ReplayPredictionFutureContext futureContext;
    futureContext.prediction = &prediction;
    futureContext.nodes = &prediction.futureNodeCache.futureNodeBuildScratch;
    futureContext.collection = &collection;
    futureContext.rootId = rootId;
    futureContext.includeRagdollVisuals = prediction.ragdollVisualsEnabled;

    while ( prediction.futureNodeCache.futureNodesBuiltFrameCount < frameCount )
    {
        const std::size_t frameIndex = prediction.futureNodeCache.futureNodesBuiltFrameCount;
        std::size_t nextContactIndex = prediction.futureNodeCache.futureNodesBuiltContactIndex;
        if ( !BuildReplayPredictionFutureNodes( frames[frameIndex],
                                                futureContext,
                                                prediction.futureNodeCache.futureNodesBuiltContactIndex,
                                                budgetStart,
                                                budgetMilliseconds,
                                                nextContactIndex ) )
        {
            prediction.futureNodeCache.futureNodesBuiltContactIndex = nextContactIndex;
            publishScratch();
            return;
        }
        prediction.futureNodeCache.futureNodesBuiltContactIndex = 0;
        ++prediction.futureNodeCache.futureNodesBuiltFrameCount;

        if ( prediction.futureNodeCache.futureNodeBuildScratch.size() >= REPLAY_PATH_MAX_FUTURE_NODES )
        {
            prediction.futureNodeCache.futureNodesBuiltFrameCount = frameCount;
            prediction.futureNodeCache.futureNodesBuiltContactIndex = 0;
            break;
        }

        if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
        {
            publishScratch();
            return;
        }
    }

    if ( prediction.futureNodeCache.futureNodeBuildScratch.size() < REPLAY_PATH_MAX_FUTURE_NODES &&
         !BuildReplayPredictionAffectedFutureNodes( frames,
                                                    frameCount,
                                                    futureContext,
                                                    budgetStart,
                                                    budgetMilliseconds ) )
    {
        publishScratch();
        return;
    }

    publishScratch();
}

bool CaptureReplayPredictionBodyState( SkullbonezCore::GameObjects::GameModelCollection& modelCollection,
                                       const PhysicsBodyStore& bodyStore,
                                       SkullbonezCore::Threading::WorkerPool& workerPool,
                                       std::vector<RunReplayPredictionBodyBackup>& outBodies )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/CaptureBodyState" );
    const int modelCount = modelCollection.SceneEntityCount();
    const auto& bodyRecords = bodyStore.Records();
    if ( static_cast<int>( bodyRecords.size() ) < modelCount )
    {
        return false;
    }

    outBodies.clear();
    if ( !ReserveReplayPredictionVector( outBodies,
                                         static_cast<std::size_t>( modelCount ),
                                         0,
                                         "RunReplayPredictionBodyBackup[]" ) )
    {
        return false;
    }
    outBodies.resize( static_cast<std::size_t>( modelCount ) );

    const auto captureBody = [&]( int i )
    {
        RuntimeAllocation::RuntimeAllocationScope replayAllocationScope(
            RuntimeAllocation::RuntimeAllocationPhase::Replay );
        const GameModel* model = modelCollection.TryGetModel( i );
        if ( !model )
        {
            return;
        }

        const PhysicsBodyRecord& body = bodyRecords[static_cast<std::size_t>( i )];
        RunReplayPredictionBodyBackup backup;
        backup.id.value = body.replayBodyId;
        backup.modelIndex = i;
        backup.position = body.position;
        backup.orientation = body.orientation;
        backup.linearVelocity = body.linearVelocity;
        backup.angularVelocity = body.angularVelocity;
        backup.mass = body.mass;
        backup.inverseMass = body.invMass;
        backup.rotationalInertia = body.rotationalInertia;
        backup.inverseRotationalInertia = body.invRotationalInertia;
        backup.fixedContactHighlightSeconds = model->GetFixedContactHighlightSeconds();
        backup.fixed = body.isFixed;
        outBodies[static_cast<std::size_t>( i )] = backup;
    };

    // Invariant: this loop reads authoritative body records and one
    // presentation timer, then writes one output slot per body. Applying
    // backups remains serial because it mutates physics body state.
    if ( modelCount >= REPLAY_PREDICTION_PARALLEL_BODY_MIN )
    {
        workerPool.ParallelForNoAlloc( 0,
                                       modelCount,
                                       captureBody,
                                       REPLAY_PREDICTION_PARALLEL_BODY_MIN,
                                       "Frame/Replay/Prediction/CaptureBodyState/WorkerBodies",
                                       REPLAY_PREDICTION_CAPTURE_BODY_WORKER_HASH );
    }
    else
    {
        for ( int i = 0; i < modelCount; ++i )
        {
            captureBody( i );
        }
    }
    return true;
}


bool ApplyReplayPredictionBodyState( PhysicsEngine& physicsEngine,
                                     const std::vector<RunReplayPredictionBodyBackup>& bodies )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/ApplyBodyState" );
    const PhysicsBodyStore& bodyStore = physicsEngine.BodyStore();
    if ( bodies.size() != static_cast<std::size_t>( bodyStore.Count() ) )
    {
        return false;
    }

    for ( const RunReplayPredictionBodyBackup& backup : bodies )
    {
        const PhysicsBodyHandle bodyHandle = bodyStore.HandleForModelIndex( backup.modelIndex );
        const PhysicsBodyRecord* bodyRecord = bodyStore.RecordForHandle( bodyHandle );
        if ( !bodyRecord || bodyStore.ModelIndexForHandle( bodyHandle ) != backup.modelIndex ||
             bodyRecord->replayBodyId != backup.id.value )
        {
            return false;
        }
        if ( !physicsEngine.RestoreReplayBodyState( bodyHandle,
                                                    backup.id.value,
                                                    backup.fixed,
                                                    backup.position,
                                                    backup.orientation,
                                                    backup.linearVelocity,
                                                    backup.angularVelocity,
                                                    backup.mass,
                                                    backup.inverseMass,
                                                    backup.rotationalInertia,
                                                    backup.inverseRotationalInertia ) )
        {
            return false;
        }
    }
    return true;
}


bool SeedReplayPredictionEngine( RunReplayPredictionState& prediction,
                                 const PhysicsEngine& liveEngine,
                                 const EngineConfig& config,
                                 const PhysicsWorldForces& worldForces,
                                 int modelCount )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/SeedPrivateEngine" );
    const RuntimeAllocation::RuntimeReserveOwnerHandle owner = ReplayPredictionReserveOwner();
    const int requestedBytes = ReplayPredictionEngineReserveBytes( liveEngine );
    if ( requestedBytes <= 0 )
    {
        return false;
    }
    const int currentBytes =
        prediction.predictionEngine ? ReplayPredictionEngineReserveBytes( *prediction.predictionEngine ) : 0;
    if ( prediction.predictionEngine && currentBytes <= 0 )
    {
        return false;
    }

    RuntimeAllocation::RuntimeReserveGrowthResult result = {};
    if ( requestedBytes > currentBytes )
    {
        // Why: the private engine is retained across prediction rebuilds. Only
        // real capacity increases should consume replay growth events; same-size
        // reseeds just reuse the previous bounded reservation.
        const RuntimeAllocation::RuntimeReserveGrowthRequest request = { REPLAY_PREDICTION_RESERVE_OWNER,
                                                                         "RunReplayPredictionState::predictionEngine",
                                                                         RuntimeAllocation::RuntimeReservePhase::Replay,
                                                                         0,
                                                                         currentBytes,
                                                                         requestedBytes,
                                                                         1 };
        result = RuntimeAllocation::RuntimeReserveAllocator::RequestGrowth( owner, request );
        if ( !result.granted )
        {
            return false;
        }
    }

    RuntimeAllocation::RuntimeAllocationScope replayAllocationScope(
        RuntimeAllocation::RuntimeAllocationPhase::Replay );
    RuntimeAllocation::RuntimeReserveOwnerScope ownerScope( owner );
    RuntimeAllocation::RuntimeReserveGrowthScope growthScope( owner,
                                                              RuntimeAllocation::RuntimeReservePhase::Replay,
                                                              result );
    prediction.predictionEngineReady = false;
    if ( !prediction.predictionEngine )
    {
        prediction.predictionEngine = std::make_unique<PhysicsEngine>();
    }

    // Invariant: seeding starts from the live facade's topology and cold policy,
    // then restores the captured prediction values into the private engine. The
    // live engine is never passed to prediction stepping after this point.
    PhysicsEngine& predictionEngine = *prediction.predictionEngine;
    predictionEngine = liveEngine;
    predictionEngine.ApplyRuntimeConfig( config );
    prediction.predictionWorldForces = worldForces;
    if ( !ApplyReplayPredictionBodyState( predictionEngine, prediction.predictionBodies ) ||
         !predictionEngine.RestoreReplaySolverSnapshot( prediction.predictionWorld, modelCount ) )
    {
        return false;
    }
    prediction.predictionEngineReady = true;
    return true;
}


bool CaptureReplayPredictionFrame( ReplayRuntime& replayRuntime,
                                   SkullbonezCore::GameObjects::GameModelCollection& modelCollection,
                                   const PhysicsEngine& physicsEngine,
                                   SkullbonezCore::Threading::WorkerPool& workerPool,
                                   ReplayFrameIndex frameIndex )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/CaptureSample" );
    const int modelCount = modelCollection.SceneEntityCount();
    const PhysicsBodyStore& bodyStore = physicsEngine.BodyStore();
    const auto& bodyRecords = bodyStore.Records();
    if ( static_cast<int>( bodyRecords.size() ) < modelCount )
    {
        return false;
    }

    RunReplayPredictionState& prediction = replayRuntime.Prediction();
    const std::size_t frameSlot = static_cast<std::size_t>( frameIndex );
    if ( frameSlot >= prediction.build.buildFrames.size() )
    {
        return false;
    }

    RunReplayPredictionFrame& frame = prediction.build.buildFrames[frameSlot];
    frame.frameIndex = frameIndex;
    frame.simulationSeconds = prediction.sourceSimulationSeconds +
                              static_cast<double>( frameIndex ) * static_cast<double>( PHYSICS_FIXED_DT );
    frame.tornadoSystemElapsedSeconds = physicsEngine.GetTornadoSystemElapsedSeconds();
    if ( static_cast<std::size_t>( modelCount ) > frame.bodies.capacity() )
    {
        return false;
    }
    frame.bodies.resize( static_cast<std::size_t>( modelCount ) );

    const auto captureBody = [&]( int i )
    {
        RuntimeAllocation::RuntimeAllocationScope replayAllocationScope(
            RuntimeAllocation::RuntimeAllocationPhase::Replay );
        const PhysicsBodyRecord& source = bodyRecords[static_cast<std::size_t>( i )];
        RunReplayPredictionBodySample body;
        body.id.value = source.replayBodyId;
        body.modelIndex = i;
        body.position = source.position;
        body.orientation = source.orientation;
        body.linearVelocity = source.linearVelocity;
        frame.bodies[static_cast<std::size_t>( i )] = body;
    };

    // Invariant: capture reads the store rows advanced by the prediction step.
    // A replay-only GameModel writeback would copy every temporary pose just so
    // this loop could read the same values back into prediction samples.
    if ( modelCount >= REPLAY_PREDICTION_PARALLEL_BODY_MIN )
    {
        workerPool.ParallelForNoAlloc( 0,
                                       modelCount,
                                       captureBody,
                                       REPLAY_PREDICTION_PARALLEL_BODY_MIN,
                                       "Frame/Replay/Prediction/CaptureSample/WorkerBodies",
                                       REPLAY_PREDICTION_CAPTURE_SAMPLE_WORKER_HASH );
    }
    else
    {
        for ( int i = 0; i < modelCount; ++i )
        {
            captureBody( i );
        }
    }
    const std::vector<PhysicsDebugContact>& debugContacts = physicsEngine.GetPhysicsDebugContacts();
    if ( debugContacts.size() > frame.debugContacts.capacity() )
    {
        // Why: debug contacts feed the optional future-impact tree; the root
        // trajectory line only needs body samples. If a dense contact frame asks
        // for more replay scratch, batch the reserve across every prediction
        // frame so the byte cap covers the whole debug-contact payload set. If
        // the replay reserve refuses, keep the frame and drop contacts rather
        // than cancelling prediction.
        const std::size_t requestedDebugContactCapacity =
            ReplayPredictionNextDebugContactCapacity( frame.debugContacts.capacity(), debugContacts.size() );
        if ( !ReserveReplayPredictionFramePayloadVectors( prediction.build.buildFrames,
                                                          prediction.build.buildFrames.size(),
                                                          requestedDebugContactCapacity,
                                                          static_cast<int>( frameIndex ),
                                                          "RunReplayPredictionFrame::debugContacts",
                                                          &RunReplayPredictionFrame::debugContacts ) )
        {
            frame.debugContacts.clear();
            prediction.PublishBuildFrameSlot( frameSlot );
            return true;
        }
    }
    frame.debugContacts = debugContacts;
    prediction.PublishBuildFrameSlot( frameSlot );
    return true;
}
