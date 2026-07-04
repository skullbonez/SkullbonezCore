/*
File: SkullbonezSource/Runtime/Replay/RunReplayPredictionHelpers.inl
Purpose:
  Owns replay path, future-node, and prediction sample helper logic used by replay tools.

Mental model:
  This file is included inside RunReplayTools.cpp's anonymous namespace. Retained solver
  samples and prediction samples share body-id lookup, path-budget, and contact-tree helpers.

Glossary:
  Future node: Body discovered by following retained or predicted contacts away from a target.
  Prediction frame: Temporary replay frame captured while fast-forwarding live physics.
  Body record: Physics-owned row holding pose, velocity, mass, inertia, and
    fixed/dynamic state for one replay body.

Invariants:
  - Prediction helpers must honor the shared replay visualizer time budget.
  - This file must only be included from RunReplayTools.cpp inside the anonymous namespace.
  - Prediction backups and frame samples read simulation state from body
    records; GameModel remains only for presentation-owned timers.

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

bool ReplayPredictionMutationReserveSpent( const std::chrono::steady_clock::time_point& start,
                                           double budgetMilliseconds )
{
    if ( budgetMilliseconds <= REPLAY_PREDICTION_MUTATION_RESERVE_MILLISECONDS )
    {
        return ReplayPredictionBudgetExpired( start, budgetMilliseconds );
    }
    return ReplayPredictionBudgetExpired( start, budgetMilliseconds - REPLAY_PREDICTION_MUTATION_RESERVE_MILLISECONDS );
}

// Concept: future-node building is an incremental cache.
//
// Prediction can hold thousands of future frames. Clearing and rebuilding the
// contact tree every render frame makes the path visualizer scale with the full
// horizon. These cursors let each frame continue where the last frame stopped.
void ClearReplayPredictionFutureNodeCache( RunReplayPredictionState& prediction )
{
    prediction.futureNodes.clear();
    prediction.futureNodeBuildScratch.clear();
    prediction.futureNodesBuiltFrameCount = 0;
    prediction.futureNodesBuiltContactIndex = 0;
    prediction.futureNodesBuiltTargetId = ReplayBodyId{};
    prediction.futureNodesBuiltRagdollVisuals = prediction.ragdollVisualsEnabled;
    prediction.futureNodesBuiltFromBuildFrames = false;
    prediction.futureNodesCacheValid = false;
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

bool ReplayModelIndexIsRagdollPart( const std::vector<GameModel>& models, int modelIndex )
{
    return modelIndex >= 0 && modelIndex < static_cast<int>( models.size() ) &&
           ReplayModelIsRagdollPart( models[static_cast<std::size_t>( modelIndex )] );
}

int ReplayRagdollTorsoModelIndexForPart( const std::vector<GameModel>& models, int modelIndex )
{
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( models.size() ) )
    {
        return modelIndex;
    }

    const GameModel& model = models[static_cast<std::size_t>( modelIndex )];
    if ( model.GetRuntimeCollectionKind() != GameModelCollectionKind::SimpleRagdoll )
    {
        return modelIndex;
    }

    const int rootModelIndex = model.GetRuntimeCollectionRootModelIndex();
    if ( rootModelIndex >= 0 && rootModelIndex < static_cast<int>( models.size() ) &&
         models[static_cast<std::size_t>( rootModelIndex )].GetRuntimeCollectionKind() ==
             GameModelCollectionKind::SimpleRagdoll )
    {
        return rootModelIndex;
    }

    return modelIndex;
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
    const std::vector<GameModel>* models = nullptr;
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

        const bool ragdollA = context.models && ReplayModelIndexIsRagdollPart( *context.models, contact.bodyA );
        const bool ragdollB = context.models && ReplayModelIndexIsRagdollPart( *context.models, contact.bodyB );
        const int modelIndexA =
            context.models ? ReplayRagdollTorsoModelIndexForPart( *context.models, contact.bodyA ) : contact.bodyA;
        const int modelIndexB =
            context.models ? ReplayRagdollTorsoModelIndexForPart( *context.models, contact.bodyB ) : contact.bodyB;
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
    bool hasIncomingPrevious = false;
    bool hasPrevious = false;
    bool markerDrawn = false;
    Vector3 incomingPrevious = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 previous = SkullbonezCore::Math::Vector::ZERO_VECTOR;
};

struct ReplayPathChildDrawContext
{
    RunEditorTracer* tracer = nullptr;
    const std::vector<GameModel>* models = nullptr;
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

float ReplayFutureMarkerRadiusForModelIndex( const std::vector<GameModel>* models, int modelIndex )
{
    if ( models && modelIndex >= 0 && modelIndex < static_cast<int>( models->size() ) )
    {
        return EditorModelRadius( ( *models )[static_cast<std::size_t>( modelIndex )] ) * 1.18f;
    }
    return 1.25f;
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
                                             const std::vector<GameModel>& models,
                                             RunEditorTracer& tracer,
                                             const std::chrono::steady_clock::time_point& budgetStart,
                                             double budgetMilliseconds )
{
    if ( frames.size() < 2 || models.empty() )
    {
        return;
    }

    const ReplayFrameIndex lastFrame = frames.back().frameIndex;
    const std::size_t sampleStride = ReplayPathStrideForSampleCount( frames.size() );
    for ( int modelIndex = 0; modelIndex < static_cast<int>( models.size() ); ++modelIndex )
    {
        if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
        {
            return;
        }

        if ( !ReplayModelIsRagdollTorso( models[static_cast<std::size_t>( modelIndex )] ) )
        {
            continue;
        }

        bool hasPrevious = false;
        Vector3 previous = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        std::size_t ordinal = 0;
        for ( const RunReplayPredictionFrame& frame : frames )
        {
            if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
            {
                return;
            }

            const std::size_t currentOrdinal = ordinal++;
            if ( frame.frameIndex != lastFrame && !ShouldDrawReplayPathSample( currentOrdinal, sampleStride ) )
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
            if ( !drawState.markerDrawn )
            {
                const float radius = ReplayFutureMarkerRadiusForModelIndex( context.models, body->modelIndex );
                context.tracer->AddReplayFutureTargetMarker( body->position, radius, drawState.node.depth );
                drawState.markerDrawn = true;
            }
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
            drawState.previous = body->position;
            drawState.hasPrevious = true;
        }
    }
}

void AddReplayFutureContactMarkers( const RunReplayPathVisualizerState& visualizer,
                                    RunEditorTracer& tracer,
                                    const std::chrono::steady_clock::time_point& budgetStart,
                                    double budgetMilliseconds )
{
    for ( const RunReplayPathTraceNode& node : visualizer.futureNodes )
    {
        if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
        {
            return;
        }

        float r = 0.58f;
        float g = 0.62f;
        float b = 0.70f;
        if ( node.depth <= 1 )
        {
            r = 0.72f;
            g = 0.78f;
            b = 0.86f;
        }
        tracer.AddReplayContactMarker( node.contactPoint, node.contactNormal, r, g, b );
    }
}

struct ReplayPredictionFutureContext
{
    RunReplayPredictionState* prediction = nullptr;
    std::vector<RunReplayPathTraceNode>* nodes = nullptr;
    const std::vector<GameModel>* models = nullptr;
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
        context.nodes ? *context.nodes : context.prediction->futureNodeBuildScratch;
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

bool ReplayPredictionFutureNodeExists( const std::vector<RunReplayPathTraceNode>& nodes, ReplayBodyId id )
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

void AddReplayPredictionFutureNode( ReplayPredictionFutureContext& context,
                                    ReplayBodyId parentId,
                                    int parentModelIndex,
                                    ReplayBodyId id,
                                    int modelIndex,
                                    ReplayFrameIndex firstFrame,
                                    const Vector3& contactPoint,
                                    const Vector3& contactNormal,
                                    int depth )
{
    if ( id.value == 0 || id.value == context.rootId.value || !context.nodes ||
         ReplayPredictionFutureNodeExists( *context.nodes, id ) ||
         context.nodes->size() >= REPLAY_PATH_MAX_FUTURE_NODES )
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
        const bool ragdollA = context.models && ReplayModelIndexIsRagdollPart( *context.models, contact.bodyA );
        const bool ragdollB = context.models && ReplayModelIndexIsRagdollPart( *context.models, contact.bodyB );
        const int modelIndexA =
            context.models ? ReplayRagdollTorsoModelIndexForPart( *context.models, contact.bodyA ) : contact.bodyA;
        const int modelIndexB =
            context.models ? ReplayRagdollTorsoModelIndexForPart( *context.models, contact.bodyB ) : contact.bodyB;
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
                                           depthA + 1 );
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
                                           depthB + 1 );
        }
        outNextContactIndex = contactIndex + 1;
    }
    outNextContactIndex = 0;
    return true;
}

void UpdateReplayPredictionFutureNodeCache( RunReplayPredictionState& prediction,
                                            const std::vector<RunReplayPredictionFrame>& frames,
                                            bool usingBuildFrames,
                                            const std::vector<GameModel>& models,
                                            ReplayBodyId rootId,
                                            const std::chrono::steady_clock::time_point& budgetStart,
                                            double budgetMilliseconds )
{
    const bool completingBuildFrames = !usingBuildFrames && prediction.futureNodesBuiltFromBuildFrames &&
                                       prediction.futureNodesBuiltFrameCount <= frames.size();
    const bool sourceMismatch =
        prediction.futureNodesBuiltFromBuildFrames != usingBuildFrames && !completingBuildFrames;
    // Invariant: these inputs define the meaning of the cached tree. Any change
    // means old future nodes may point at the wrong root or include the wrong
    // ragdoll aggregation policy.
    const bool cacheMismatch = !prediction.futureNodesCacheValid ||
                               prediction.futureNodesBuiltTargetId.value != rootId.value ||
                               prediction.futureNodesBuiltRagdollVisuals != prediction.ragdollVisualsEnabled ||
                               sourceMismatch || prediction.futureNodesBuiltFrameCount > frames.size();
    if ( cacheMismatch )
    {
        ClearReplayPredictionFutureNodeCache( prediction );
        prediction.futureNodesBuiltTargetId = rootId;
        prediction.futureNodesBuiltRagdollVisuals = prediction.ragdollVisualsEnabled;
        prediction.futureNodesBuiltFromBuildFrames = usingBuildFrames;
        prediction.futureNodesCacheValid = rootId.value != 0;
    }
    else if ( completingBuildFrames )
    {
        prediction.futureNodesBuiltFromBuildFrames = false;
    }

    if ( rootId.value == 0 || frames.empty() || !prediction.futureNodesCacheValid )
    {
        return;
    }

    auto publishScratch = [&]()
    {
        // Why: the renderer reads futureNodes only after this builder returns.
        // Copying the scratch prefix here lets cause/effect paths grow over
        // frames without exposing a vector while it is being mutated.
        prediction.futureNodes = prediction.futureNodeBuildScratch;
    };

    if ( prediction.futureNodeBuildScratch.size() >= REPLAY_PATH_MAX_FUTURE_NODES )
    {
        prediction.futureNodesBuiltFrameCount = frames.size();
        prediction.futureNodesBuiltContactIndex = 0;
        publishScratch();
        return;
    }

    ReplayPredictionFutureContext futureContext;
    futureContext.prediction = &prediction;
    futureContext.nodes = &prediction.futureNodeBuildScratch;
    futureContext.models = &models;
    futureContext.rootId = rootId;
    futureContext.includeRagdollVisuals = prediction.ragdollVisualsEnabled;

    while ( prediction.futureNodesBuiltFrameCount < frames.size() )
    {
        const std::size_t frameIndex = prediction.futureNodesBuiltFrameCount;
        std::size_t nextContactIndex = prediction.futureNodesBuiltContactIndex;
        if ( !BuildReplayPredictionFutureNodes( frames[frameIndex],
                                                futureContext,
                                                prediction.futureNodesBuiltContactIndex,
                                                budgetStart,
                                                budgetMilliseconds,
                                                nextContactIndex ) )
        {
            prediction.futureNodesBuiltContactIndex = nextContactIndex;
            publishScratch();
            return;
        }
        prediction.futureNodesBuiltContactIndex = 0;
        ++prediction.futureNodesBuiltFrameCount;

        if ( prediction.futureNodeBuildScratch.size() >= REPLAY_PATH_MAX_FUTURE_NODES )
        {
            prediction.futureNodesBuiltFrameCount = frames.size();
            prediction.futureNodesBuiltContactIndex = 0;
            break;
        }

        if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
        {
            publishScratch();
            return;
        }
    }

    publishScratch();
}

bool CaptureReplayPredictionBodyState( SkullbonezCore::GameObjects::GameModelCollection& modelCollection,
                                       SkullbonezCore::Threading::WorkerPool& workerPool,
                                       std::vector<RunReplayPredictionBodyBackup>& outBodies )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/CaptureBodyState" );
    const int modelCount = modelCollection.GetModelCount();
    const std::vector<PhysicsBodyRecord>& bodyRecords = modelCollection.GetPhysicsEngine().BodyStore().Records();
    if ( static_cast<int>( bodyRecords.size() ) < modelCount )
    {
        return false;
    }

    outBodies.clear();
    outBodies.resize( static_cast<std::size_t>( modelCount ) );

    const auto captureBody = [&]( int i )
    {
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
    // backups remains serial because it mutates live GameModel state.
    if ( modelCount >= REPLAY_PREDICTION_PARALLEL_BODY_MIN )
    {
        workerPool.ParallelFor( 0,
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


bool ApplyReplayPredictionBodyState( SkullbonezCore::GameObjects::GameModelCollection& modelCollection,
                                     const std::vector<RunReplayPredictionBodyBackup>& bodies )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/ApplyBodyState" );
    if ( bodies.size() != static_cast<std::size_t>( modelCollection.GetModelCount() ) )
    {
        return false;
    }

    for ( const RunReplayPredictionBodyBackup& backup : bodies )
    {
        if ( !modelCollection.TryRestoreReplayPredictionBodyState( backup.modelIndex,
                                                                   backup.id.value,
                                                                   backup.fixed,
                                                                   backup.position,
                                                                   backup.orientation,
                                                                   backup.linearVelocity,
                                                                   backup.angularVelocity,
                                                                   backup.mass,
                                                                   backup.inverseMass,
                                                                   backup.rotationalInertia,
                                                                   backup.inverseRotationalInertia,
                                                                   backup.fixedContactHighlightSeconds ) )
        {
            return false;
        }
    }
    return true;
}


void CaptureReplayPredictionFrame( ReplayRuntime& replayRuntime,
                                   SkullbonezCore::GameObjects::GameModelCollection& modelCollection,
                                   SkullbonezCore::Threading::WorkerPool& workerPool,
                                   ReplayFrameIndex frameIndex )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/CaptureSample" );
    const int modelCount = modelCollection.GetModelCount();
    const std::vector<PhysicsBodyRecord>& bodyRecords = modelCollection.GetPhysicsEngine().BodyStore().Records();
    if ( static_cast<int>( bodyRecords.size() ) < modelCount )
    {
        return;
    }

    RunReplayPredictionFrame frame;
    frame.frameIndex = frameIndex;
    frame.simulationSeconds = replayRuntime.Prediction().sourceSimulationSeconds +
                              static_cast<double>( frameIndex ) * static_cast<double>( PHYSICS_FIXED_DT );
    frame.tornadoSystemElapsedSeconds = modelCollection.GetTornadoSystemElapsedSeconds();
    frame.bodies.resize( static_cast<std::size_t>( modelCount ) );

    const auto captureBody = [&]( int i )
    {
        const PhysicsBodyRecord& source = bodyRecords[static_cast<std::size_t>( i )];
        RunReplayPredictionBodySample body;
        body.id.value = source.replayBodyId;
        body.modelIndex = i;
        body.position = source.position;
        body.orientation = source.orientation;
        frame.bodies[static_cast<std::size_t>( i )] = body;
    };

    // Invariant: capture reads the store rows advanced by the prediction step.
    // A replay-only GameModel writeback would copy every temporary pose just so
    // this loop could read the same values back into prediction samples.
    if ( modelCount >= REPLAY_PREDICTION_PARALLEL_BODY_MIN )
    {
        workerPool.ParallelFor( 0,
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
    frame.debugContacts = modelCollection.GetPhysicsDebugContacts();
    replayRuntime.Prediction().buildFrames.push_back( std::move( frame ) );
}
