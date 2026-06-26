/*
File: SkullbonezSource/Runtime/Replay/RunReplayTools.cpp
Purpose:
  Owns live replay tools: scrubber input, cause-tree inspection, path
  visualization, prediction previews, and velocity-edit overlays.

Mental model:
  Replay tools read two timelines. Retained solver samples describe what already
  happened; prediction samples temporarily fast-forward the live physics state
  and then restore it. The renderer only receives lightweight overlay geometry.

Glossary:
  Scrubber: UI control that maps mouse position to retained replay frames.
  Cause tree: Contact graph that explains how one body influenced others.
  Path visualizer: Overlay that draws past/future body trajectories and contact
    handoffs.
  Prediction slice: Time-budgeted replay preview work performed inside a render
    frame.
  Future node: Body discovered by following contacts outward from a selected
    root body.
  ReplayBodyId: Stable runtime id used across retained samples even when vector
    indices are only local hints.
  Solver snapshot: Physics cache state that must be restored to make the next
    fixed step reproduce.
  WorkerPool: Persistent engine worker threads used only for large, independent
    fork-join loops.

Invariants:
  - Prediction may mutate live physics state only between a captured restore
    snapshot and a guaranteed restore path.
  - Path visualizer work shares one per-frame budget so replay overlays cannot
    hide frame spikes under child profiler markers.
  - Physics steps stay serial; only read-only body capture is parallelized.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "../RunInternal.h"
#include "../Editor/EditorHullAssets.h"
#include "../InputController.h"
#include "../RuntimePickService.h"
#include "../../Physics/PhysicsMass.h"
#include "../RuntimeFileWriter.h"
#include "../../Core/WorkerPool.h"
#include "../../UI/UIInput.h"
#include "../../UI/UILayout.h"

#include <algorithm>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include <commdlg.h>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::UI::Layout;
using namespace SkullbonezCore::Basics::RunInternal;
using SkullbonezCore::Assets::EDITOR_HULL_ASSET_COUNT;
using SkullbonezCore::Assets::EDITOR_HULL_ASSETS;
using SkullbonezCore::Assets::EditorHullAsset;
using SkullbonezCore::Assets::EditorHullAssetDefaultsToContactRelease;
using SkullbonezCore::Assets::EditorHullAssetPath;
using SkullbonezCore::Assets::EditorHullAssetToken;
using SkullbonezCore::GameObjects::GameModelCollectionKind;

namespace
{
bool IsReplayToolOwner( WorldInteractionOwner owner )
{
    return owner == WorldInteractionOwner::ReplayScrub || owner == WorldInteractionOwner::ReplayVelocityEdit ||
           owner == WorldInteractionOwner::ReplayPrediction || owner == WorldInteractionOwner::ReplayBranchTarget ||
           owner == WorldInteractionOwner::ReplayCauseTree;
}


Vector3 EditorAxisVector( int axis )
{
    switch ( axis )
    {
    case 0:
        return Vector3( 1.0f, 0.0f, 0.0f );
    case 1:
        return Vector3( 0.0f, 1.0f, 0.0f );
    case 2:
        return Vector3( 0.0f, 0.0f, 1.0f );
    default:
        return SkullbonezCore::Math::Vector::ZERO_VECTOR;
    }
}


float EditorModelRadius( const GameModel& model )
{
    return (std::max)( GetShapeBoundingRadius( model.GetCollisionShape() ), 1.0f );
}


float ReplayVelocityLinearBaseLength( float modelRadius )
{
    return (std::max)( 10.0f, modelRadius + 7.0f );
}


float ReplayVelocityLinearVisualAxisT( float modelRadius, float velocityComponent )
{
    const float sign = velocityComponent < 0.0f ? -1.0f : 1.0f;
    const float t = std::clamp( fabsf( velocityComponent ) / REPLAY_VELOCITY_EDIT_LINEAR_MAX, 0.0f, 1.0f );
    return sign * ( ReplayVelocityLinearBaseLength( modelRadius ) + t * REPLAY_VELOCITY_EDIT_LINEAR_EXTRA );
}


float ReplayVelocityLinearUnitsPerWorld()
{
    return REPLAY_VELOCITY_EDIT_LINEAR_MAX / REPLAY_VELOCITY_EDIT_LINEAR_EXTRA;
}


float ReplayVelocityAngularBaseRadius( float modelRadius )
{
    return (std::max)( 11.0f, modelRadius + 6.0f );
}


float ReplayVelocityAngularVisualRadius( float modelRadius, float angularComponent )
{
    const float t = std::clamp( fabsf( angularComponent ) / REPLAY_VELOCITY_EDIT_ANGULAR_MAX, 0.0f, 1.0f );
    return ReplayVelocityAngularBaseRadius( modelRadius ) + t * (std::max)( 5.0f, modelRadius * 0.85f );
}


float ReplayVelocityAxisComponent( const Vector3& value, int axis )
{
    if ( axis == 0 )
    {
        return value.x;
    }
    if ( axis == 1 )
    {
        return value.y;
    }
    return value.z;
}


void ReplayVelocitySetAxisComponent( Vector3& value, int axis, float component )
{
    if ( axis == 0 )
    {
        value.x = component;
    }
    else if ( axis == 1 )
    {
        value.y = component;
    }
    else
    {
        value.z = component;
    }
}


Vector3 EditorRotationRingBasisA( int axis )
{
    switch ( axis )
    {
    case 0:
        return Vector3( 0.0f, 1.0f, 0.0f );
    case 1:
        return Vector3( 0.0f, 0.0f, 1.0f );
    case 2:
        return Vector3( 1.0f, 0.0f, 0.0f );
    default:
        return Vector3( 1.0f, 0.0f, 0.0f );
    }
}


Vector3 EditorRotationRingBasisB( int axis )
{
    switch ( axis )
    {
    case 0:
        return Vector3( 0.0f, 0.0f, 1.0f );
    case 1:
        return Vector3( 1.0f, 0.0f, 0.0f );
    case 2:
        return Vector3( 0.0f, 1.0f, 0.0f );
    default:
        return Vector3( 0.0f, 1.0f, 0.0f );
    }
}


float WrapEditorAngleDelta( float delta )
{
    while ( delta > _PI )
    {
        delta -= 2.0f * _PI;
    }
    while ( delta < -_PI )
    {
        delta += 2.0f * _PI;
    }
    return delta;
}


float DistanceRayToSegmentSquared( const Vector3& rayOrigin,
                                   const Vector3& rayDirection,
                                   const Vector3& segmentA,
                                   const Vector3& segmentB )
{
    const Vector3 segment = segmentB - segmentA;
    const float segmentLenSq = segment * segment;
    if ( segmentLenSq <= TOLERANCE * TOLERANCE )
    {
        const Vector3 toPoint = segmentA - rayOrigin;
        const float rayT = (std::max)( 0.0f, toPoint * rayDirection );
        return VectorMagSquared( rayOrigin + rayDirection * rayT - segmentA );
    }

    const Vector3 w0 = rayOrigin - segmentA;
    const float a = rayDirection * rayDirection;
    const float b = rayDirection * segment;
    const float c = segmentLenSq;
    const float d = rayDirection * w0;
    const float e = segment * w0;
    const float denom = a * c - b * b;

    float rayT = 0.0f;
    float segmentT = 0.0f;
    if ( fabsf( denom ) > 1e-5f )
    {
        rayT = ( b * e - c * d ) / denom;
        segmentT = ( a * e - b * d ) / denom;
    }

    if ( rayT < 0.0f )
    {
        rayT = 0.0f;
        segmentT = std::clamp( e / c, 0.0f, 1.0f );
    }
    else if ( segmentT < 0.0f )
    {
        segmentT = 0.0f;
        rayT = (std::max)( 0.0f, -d / a );
    }
    else if ( segmentT > 1.0f )
    {
        segmentT = 1.0f;
        rayT = (std::max)( 0.0f, ( b - d ) / a );
    }

    const Vector3 rayPoint = rayOrigin + rayDirection * rayT;
    const Vector3 segmentPoint = segmentA + segment * segmentT;
    return VectorMagSquared( rayPoint - segmentPoint );
}


bool IntersectRaySphere( const Vector3& rayOrigin,
                         const Vector3& rayDirection,
                         const Vector3& center,
                         float radius,
                         float& outT )
{
    const Vector3 m = rayOrigin - center;
    const float b = m * rayDirection;
    const float c = ( m * m ) - radius * radius;
    if ( c > 0.0f && b > 0.0f )
    {
        return false;
    }

    const float discriminant = b * b - c;
    if ( discriminant < 0.0f )
    {
        return false;
    }

    outT = -b - sqrtf( discriminant );
    if ( outT < 0.0f )
    {
        outT = 0.0f;
    }
    return true;
}

constexpr std::size_t REPLAY_PATH_MAX_FUTURE_NODES = 64;
constexpr std::size_t REPLAY_PATH_MAX_ROOT_TARGETS = 12;
constexpr std::size_t REPLAY_PATH_MAX_SEGMENTS = 260;
constexpr float REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ = 0.0001f;

// Invariant: Worker dispatch is only worth it for large body snapshots. Small
// scenes stay serial so replay overlays do not pay thread wakeup cost to copy a
// few kilobytes.
constexpr int REPLAY_PREDICTION_PARALLEL_BODY_MIN = 2048;

// Hazard: prediction temporarily swaps live model/solver state. Keep a small
// reserve so we do not enter a mutation section after spending the whole visual
// budget and then visibly spike while restoring live state.
constexpr double REPLAY_PREDICTION_MUTATION_RESERVE_MILLISECONDS = 1.0;
constexpr uint32_t REPLAY_PREDICTION_CAPTURE_BODY_WORKER_HASH =
    HashStr( "Frame/Replay/Prediction/CaptureBodyState/WorkerBodies" );
constexpr uint32_t REPLAY_PREDICTION_CAPTURE_SAMPLE_WORKER_HASH =
    HashStr( "Frame/Replay/Prediction/CaptureSample/WorkerBodies" );

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
    prediction.futureNodesBuiltFrameCount = 0;
    prediction.futureNodesBuiltContactIndex = 0;
    prediction.futureNodesBuiltTargetId = ReplayBodyId{};
    prediction.futureNodesBuiltRagdollVisuals = prediction.ragdollVisualsEnabled;
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

void DrawReplayPredictionRagdollTorsoTrails( const RunReplayPredictionState& prediction,
                                             const std::vector<GameModel>& models,
                                             RunEditorTracer& tracer,
                                             const std::chrono::steady_clock::time_point& budgetStart,
                                             double budgetMilliseconds )
{
    if ( prediction.frames.size() < 2 || models.empty() )
    {
        return;
    }

    const ReplayFrameIndex lastFrame = prediction.frames.back().frameIndex;
    const std::size_t sampleStride = ReplayPathStrideForSampleCount( prediction.frames.size() );
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
        for ( const RunReplayPredictionFrame& frame : prediction.frames )
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

    for ( const RunReplayPathTraceNode& node : context.prediction->futureNodes )
    {
        if ( node.id.value == id.value && frame >= node.firstFrame )
        {
            outDepth = node.depth;
            return true;
        }
    }
    return false;
}

bool ReplayPredictionFutureNodeExists( const RunReplayPredictionState& prediction, ReplayBodyId id )
{
    for ( const RunReplayPathTraceNode& node : prediction.futureNodes )
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
    if ( id.value == 0 || id.value == context.rootId.value ||
         ReplayPredictionFutureNodeExists( *context.prediction, id ) ||
         context.prediction->futureNodes.size() >= REPLAY_PATH_MAX_FUTURE_NODES )
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
    context.prediction->futureNodes.push_back( node );
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
                                            const std::vector<GameModel>& models,
                                            ReplayBodyId rootId,
                                            const std::chrono::steady_clock::time_point& budgetStart,
                                            double budgetMilliseconds )
{
    // Invariant: these inputs define the meaning of the cached tree. Any change
    // means old future nodes may point at the wrong root or include the wrong
    // ragdoll aggregation policy.
    const bool cacheMismatch = !prediction.futureNodesCacheValid ||
                               prediction.futureNodesBuiltTargetId.value != rootId.value ||
                               prediction.futureNodesBuiltRagdollVisuals != prediction.ragdollVisualsEnabled ||
                               prediction.futureNodesBuiltFrameCount > prediction.frames.size();
    if ( cacheMismatch )
    {
        ClearReplayPredictionFutureNodeCache( prediction );
        prediction.futureNodesBuiltTargetId = rootId;
        prediction.futureNodesBuiltRagdollVisuals = prediction.ragdollVisualsEnabled;
        prediction.futureNodesCacheValid = rootId.value != 0;
    }

    if ( rootId.value == 0 || prediction.frames.empty() || !prediction.futureNodesCacheValid )
    {
        return;
    }

    if ( prediction.futureNodes.size() >= REPLAY_PATH_MAX_FUTURE_NODES )
    {
        prediction.futureNodesBuiltFrameCount = prediction.frames.size();
        prediction.futureNodesBuiltContactIndex = 0;
        return;
    }

    ReplayPredictionFutureContext futureContext;
    futureContext.prediction = &prediction;
    futureContext.models = &models;
    futureContext.rootId = rootId;
    futureContext.includeRagdollVisuals = prediction.ragdollVisualsEnabled;

    while ( prediction.futureNodesBuiltFrameCount < prediction.frames.size() )
    {
        const std::size_t frameIndex = prediction.futureNodesBuiltFrameCount;
        std::size_t nextContactIndex = prediction.futureNodesBuiltContactIndex;
        if ( !BuildReplayPredictionFutureNodes( prediction.frames[frameIndex],
                                                futureContext,
                                                prediction.futureNodesBuiltContactIndex,
                                                budgetStart,
                                                budgetMilliseconds,
                                                nextContactIndex ) )
        {
            prediction.futureNodesBuiltContactIndex = nextContactIndex;
            return;
        }
        prediction.futureNodesBuiltContactIndex = 0;
        ++prediction.futureNodesBuiltFrameCount;

        if ( prediction.futureNodes.size() >= REPLAY_PATH_MAX_FUTURE_NODES )
        {
            prediction.futureNodesBuiltFrameCount = prediction.frames.size();
            prediction.futureNodesBuiltContactIndex = 0;
            return;
        }

        if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
        {
            return;
        }
    }
}


} // namespace

void Run::SetReplayLiveAdvanceHeld( bool held )
{
    if ( m_replayRuntime.Scrubber().liveAdvanceHeld == held )
    {
        return;
    }

    // Removing this marker until we can sort out avoiding hitting assert
    // PROFILE_SCOPED( "Frame/Replay/SimulationPause" );

    if ( held )
    {
        EnterInteractiveSceneRun();
        if ( !IsReplayToolOwner( m_interaction.Owner() ) )
        {
            SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayScrub,
                                                                InteractionExitReason::EnterReplay );
        }
        m_replayRuntime.Scrubber().liveAdvanceHeld = true;
        UpdateReplayInspectionCamera();
        return;
    }

    m_replayRuntime.Scrubber().liveAdvanceHeld = false;
    m_replayRuntime.Camera().ownsSimulationPause = false;
    if ( m_replayRuntime.VelocityEdit().enabled )
    {
        SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayVelocityEdit,
                                                            InteractionExitReason::EnterReplay );
    }
    else if ( !m_replayRuntime.Scrubber().historicalSamplePaused &&
              m_replayRuntime.Camera().focusKind == RunReplayCameraFocusKind::None )
    {
        EnterInteractionForCameraMode( m_camera.mode );
    }
    UpdateReplayInspectionCamera();
}


void Run::EnterReplayInspectionCamera()
{
    if ( !m_systems.cameras )
    {
        return;
    }

    const bool enteringInspectionCamera = !m_replayRuntime.Camera().active;
    if ( !m_replayRuntime.Camera().active )
    {
        m_replayRuntime.Camera().restoreCameraMode = NormalizeCameraModeForCurrentScene( m_camera.mode );
        m_replayRuntime.Camera().restoreCameraHash = m_systems.cameras->GetSelectedCameraName();

        auto magnitudeSquared = []( const Vector3& value ) -> float
        { return value.x * value.x + value.y * value.y + value.z * value.z; };

        Vector3 eye = m_systems.cameras->GetRenderCameraTranslation();
        Vector3 view = m_systems.cameras->GetRenderCameraView();
        Vector3 up = m_systems.cameras->GetRenderCameraUp();
        if ( magnitudeSquared( view - eye ) < 0.000001f )
        {
            eye = m_systems.cameras->GetCameraTranslation();
            view = m_systems.cameras->GetCameraView();
            up = m_systems.cameras->GetCameraUp();
        }
        if ( magnitudeSquared( view - eye ) < 0.000001f )
        {
            view = eye + Vector3( 0.0f, 0.0f, 1.0f );
        }
        if ( magnitudeSquared( up ) < 0.000001f )
        {
            up = Vector3( 0.0f, 1.0f, 0.0f );
        }

        m_replayRuntime.Camera().restoreEye = eye;
        m_replayRuntime.Camera().restoreView = view;
        m_replayRuntime.Camera().restoreUp = up;
        m_replayRuntime.Camera().hasRestorePose = true;
        m_systems.cameras->CancelTween();
        m_systems.cameras->SelectCamera( CAMERA_FREE, false );
        m_systems.cameras->SetPrimaryPosition( eye );
        m_systems.cameras->SetViewCoordinates( view );
        m_systems.cameras->SetPrimaryUp( up );
        m_replayRuntime.Camera().active = true;
    }

    XZBounds unbounded;
    unbounded.m_xMin = -99999.9f;
    unbounded.m_xMax = 99999.9f;
    unbounded.m_zMin = -99999.9f;
    unbounded.m_zMax = 99999.9f;
    m_systems.cameras->SetCameraXZBounds( CAMERA_FREE, unbounded );
    m_camera.cameraTime = 0.0f;
    CancelMousePickup();
    if ( !IsReplayToolOwner( m_interaction.Owner() ) )
    {
        SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayScrub,
                                                            InteractionExitReason::EnterReplay );
    }
    SetCameraModeLabelAfterInteractionTransition( RunCameraMode::Inspect );
    if ( enteringInspectionCamera )
    {
        Input::SetSystemCursorVisible( true );
        InputController::ResetMouseLook( m_camera );
    }
}


void Run::ExitReplayInspectionCamera()
{
    if ( !m_replayRuntime.Camera().active )
    {
        return;
    }

    m_replayRuntime.Camera().active = false;
    SetCameraModeLabelAfterInteractionTransition(
        NormalizeCameraModeForCurrentScene( m_replayRuntime.Camera().restoreCameraMode ) );
    if ( m_replayRuntime.VelocityEdit().enabled )
    {
        SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayVelocityEdit,
                                                            InteractionExitReason::EnterReplay );
    }
    else
    {
        EnterInteractionForCameraMode( m_camera.mode );
    }
    if ( m_systems.cameras )
    {
        m_systems.cameras->CancelTween();
        m_systems.cameras->SelectCamera( m_replayRuntime.Camera().restoreCameraHash, false );
        if ( m_replayRuntime.Camera().hasRestorePose )
        {
            m_systems.cameras->SetPrimaryPosition( m_replayRuntime.Camera().restoreEye );
            m_systems.cameras->SetViewCoordinates( m_replayRuntime.Camera().restoreView );
            m_systems.cameras->SetPrimaryUp( m_replayRuntime.Camera().restoreUp );
        }
        if ( m_systems.terrain )
        {
            const uint32_t activeCam = m_systems.cameras->GetSelectedCameraName();
            if ( IsFlyCameraMode() )
            {
                XZBounds unbounded;
                unbounded.m_xMin = -99999.9f;
                unbounded.m_xMax = 99999.9f;
                unbounded.m_zMin = -99999.9f;
                unbounded.m_zMax = 99999.9f;
                m_systems.cameras->SetCameraXZBounds( activeCam, unbounded );
            }
            else
            {
                m_systems.cameras->SetCameraXZBounds( activeCam, m_systems.terrain->GetXZBounds() );
            }
        }
    }
    m_replayRuntime.Camera().focusKind = RunReplayCameraFocusKind::None;
    m_replayRuntime.Camera().focusedRow = -1;
    m_replayRuntime.Camera().hasRestorePose = false;
    m_replayRuntime.Camera().ownsSimulationPause = false;
    m_replayRuntime.Camera().restoreCameraMode = RunCameraMode::Demo;
    Input::SetSystemCursorVisible( true );
    InputController::ResetMouseLook( m_camera );
}


void Run::UpdateReplayInspectionCamera()
{
    if ( m_replayRuntime.Scrubber().historicalSamplePaused || m_replayRuntime.Scrubber().liveAdvanceHeld ||
         m_replayRuntime.Camera().focusKind != RunReplayCameraFocusKind::None )
    {
        EnterReplayInspectionCamera();
    }
    else
    {
        ExitReplayInspectionCamera();
    }
}


bool Run::RestoreReplayScrubberSelectionAsLive( double now,
                                                RunReplayV2TargetRestoreResult* outV2Result,
                                                char* outReason,
                                                std::size_t reasonSize )
{
    if ( outV2Result )
    {
        *outV2Result = RunReplayV2TargetRestoreResult();
    }

    auto writeReason = [outReason, reasonSize]( const char* reason )
    {
        if ( outReason && reasonSize > 0 )
        {
            strncpy_s( outReason, reasonSize, reason ? reason : "restore failed", _TRUNCATE );
        }
    };

    char reason[160] = {};
    bool restored = false;
    RunReplayTrack messageTrack = m_replayRuntime.Scrubber().activeTrack;
    if ( HasLoadedReplayPresentation() && m_replayRuntime.Scrubber().historicalSamplePaused &&
         m_replayRuntime.Scrubber().activeTrack == RunReplayTrack::Presentation )
    {
        EnterInteractiveSceneRun();
        RunReplayV2TargetRestoreResult result;
        const ReplayPresentationSample* selected = CurrentReplayScrubSample();
        const ReplayFrameIndex selectedFrame = selected ? selected->frameIndex : 0;
        restored = selected && RestoreReplayV2ArtifactTargetState( m_replayRuntime.LoadedPresentation().path,
                                                                   selectedFrame,
                                                                   true,
                                                                   result,
                                                                   reason,
                                                                   sizeof( reason ) );
        if ( outV2Result )
        {
            *outV2Result = result;
        }
        messageTrack = RunReplayTrack::Presentation;
        fprintf( stderr,
                 "[replay] V2 file restore %s target_frame=%llu branch_id=%u%s%s\n",
                 restored ? "applied" : "failed",
                 static_cast<unsigned long long>( selectedFrame ),
                 restored ? result.branchId : 0,
                 reason[0] != '\0' ? ": " : "",
                 reason );
    }
    else if ( m_replayRuntime.Scrubber().historicalSamplePaused &&
              m_replayRuntime.Scrubber().activeTrack == RunReplayTrack::Solver )
    {
        EnterInteractiveSceneRun();
        const ReplaySolverFrameSample* sample = CurrentReplaySolverScrubSample();
        restored = sample && RestoreReplaySolverSampleAsLive( *sample, reason, sizeof( reason ) );
        messageTrack = RunReplayTrack::Solver;
        fprintf( stderr,
                 "[replay] Solver restore %s%s%s\n",
                 restored ? "applied" : "failed",
                 reason[0] != '\0' ? ": " : "",
                 reason );
    }
    else
    {
        sprintf_s( reason, sizeof( reason ), "no historical replay branch target selected" );
        fprintf( stderr, "[replay] Branch restore failed: %s\n", reason );
    }

    m_replayRuntime.Scrubber().restoreConsumedThisFrame = true;
    m_replayRuntime.Scrubber().saveMessageTrack = messageTrack;
    sprintf_s( m_replayRuntime.Scrubber().saveMessage,
               sizeof( m_replayRuntime.Scrubber().saveMessage ),
               restored ? ( messageTrack == RunReplayTrack::Presentation ? "V2 FILE BRANCHED" : "SOLVER RESTORED" )
                        : "RESTORE FAILED" );
    m_replayRuntime.Scrubber().saveMessageUntil = now + 2.5;
    m_replayRuntime.Scrubber().visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
    m_replayRuntime.Scrubber().visible = true;
    writeReason( reason );
    return restored;
}

bool Run::PromptLoadReplayPresentationArtifact( HWND hwnd )
{
    char path[MAX_PATH] = {};
    OPENFILENAMEA openFile = {};
    openFile.lStructSize = sizeof( openFile );
    openFile.hwndOwner = hwnd;
    openFile.lpstrFilter = "Skullbonez replay (*.skreplay)\0*.skreplay\0All files (*.*)\0*.*\0";
    openFile.lpstrFile = path;
    openFile.nMaxFile = sizeof( path );
    openFile.lpstrInitialDir = "replays";
    openFile.lpstrTitle = "Load Skullbonez replay v2 artifact";
    openFile.lpstrDefExt = "skreplay";
    openFile.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if ( !GetOpenFileNameA( &openFile ) )
    {
        if ( CommDlgExtendedError() != 0 )
        {
            const double now = m_timers.simulationTimer.GetTotalTime();
            sprintf_s( m_replayRuntime.Scrubber().saveMessage,
                       sizeof( m_replayRuntime.Scrubber().saveMessage ),
                       "REPLAY PICKER FAILED" );
            m_replayRuntime.Scrubber().saveMessageTrack = RunReplayTrack::Presentation;
            m_replayRuntime.Scrubber().saveMessageUntil = now + 2.5;
            m_replayRuntime.Scrubber().visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
            m_replayRuntime.Scrubber().visible = true;
        }
        return false;
    }

    const bool loaded = LoadReplayPresentationArtifact( path, true );
    const double now = m_timers.simulationTimer.GetTotalTime();
    const char* fileName = strrchr( path, '\\' );
    if ( !fileName )
    {
        fileName = strrchr( path, '/' );
    }
    fileName = fileName ? fileName + 1 : path;

    m_replayRuntime.Scrubber().saveMessageTrack = RunReplayTrack::Presentation;
    if ( loaded )
    {
        constexpr int loadedPrefixLength = 7;
        constexpr int loadedFileNameLimit =
            static_cast<int>( sizeof( m_replayRuntime.Scrubber().saveMessage ) ) - loadedPrefixLength - 1;
        sprintf_s( m_replayRuntime.Scrubber().saveMessage,
                   sizeof( m_replayRuntime.Scrubber().saveMessage ),
                   "LOADED %.*s",
                   loadedFileNameLimit,
                   fileName );
    }
    else
    {
        sprintf_s( m_replayRuntime.Scrubber().saveMessage,
                   sizeof( m_replayRuntime.Scrubber().saveMessage ),
                   "REPLAY LOAD FAILED" );
    }
    m_replayRuntime.Scrubber().saveMessageUntil = now + 2.5;
    m_replayRuntime.Scrubber().visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
    m_replayRuntime.Scrubber().visible = true;
    return loaded;
}


bool Run::TickReplayScrubberInput( HWND hwnd, bool uiBlocksMouse )
{
    PROFILE_SCOPED( "Frame/Replay/ScrubberInput" );
    m_replayRuntime.Scrubber().restoreConsumedThisFrame = false;
    const bool leftDown = Input::IsLeftMouseDown();
    const bool leftPressed = leftDown && !m_replayRuntime.Scrubber().leftWasDown;
    const bool leftReleased = !leftDown && m_replayRuntime.Scrubber().leftWasDown;
    m_replayRuntime.Scrubber().leftWasDown = leftDown;
    const bool restoreDown = Input::IsKeyDown( VK_RETURN );
    const bool restorePressed = restoreDown && !m_replayRuntime.Scrubber().restoreWasDown;
    m_replayRuntime.Scrubber().restoreWasDown = restoreDown;

    const bool scrubberAllowed = !m_runtimeTools.Editor().editorModeEnabled && m_UI.IsVisible() && m_UI.IsMinimized();
    const bool loadedPresentation = HasLoadedReplayPresentation();
    const ReplayRecorderStats solverReplayStats = m_replayRuntime.Solver().GetStats();
    const bool solverReplayAvailable = solverReplayStats.enabled && solverReplayStats.sampleCount >= 2;
    const int screenW = WindowScreenWidth();
    const int screenH = WindowScreenHeight();
    if ( !scrubberAllowed || ( !loadedPresentation && !solverReplayAvailable ) || screenW <= 0 || screenH <= 0 )
    {
        CancelReplayToolDragState();
        if ( !loadedPresentation )
        {
            ResetReplayScrubber();
        }
        m_replayRuntime.Prediction().checkboxHovered = false;
        m_replayRuntime.Prediction().ragdollVisualsHovered = false;
        m_replayRuntime.Prediction().decreaseHovered = false;
        m_replayRuntime.Prediction().increaseHovered = false;
        m_replayRuntime.Prediction().horizonHovered = false;
        m_replayRuntime.Prediction().horizonDragging = false;
        m_replayRuntime.VelocityEdit().toggleHovered = false;
        m_replayRuntime.Scrubber().branchHovered = false;
        m_replayRuntime.Scrubber().loadHovered = false;
        m_replayRuntime.Scrubber().leftWasDown = leftDown;
        return false;
    }

    const POINT mouse = Input::GetClientMouseCoordinates();
    m_replayRuntime.Scrubber().mouseX = mouse.x;
    m_replayRuntime.Scrubber().mouseY = mouse.y;

    const UI::UIRect hotZone = ReplayScrubberHotZoneRect( screenW, screenH );
    const UI::UIRect panel = ReplayScrubberPanelRect( screenW, screenH );
    const UI::UIRect solverSaveButton = ReplayScrubberSaveButtonRect( screenW, screenH, RunReplayTrack::Solver );
    const UI::UIRect branchButton = ReplayScrubberBranchButtonRect( screenW, screenH );
    const UI::UIRect pauseButton = ReplayScrubberPauseButtonRect( screenW, screenH );
    const UI::UIRect velocityEditToggle = ReplayScrubberVelocityEditToggleRect( screenW, screenH );
    const UI::UIRect predictControl = ReplayScrubberPredictControlRect( screenW, screenH );
    const UI::UIRect predictToggle = ReplayScrubberPredictToggleRect( screenW, screenH );
    const UI::UIRect predictHorizon = ReplayScrubberPredictHorizonRect( screenW, screenH );
    const UI::UIRect ragdollVisualToggle = ReplayScrubberRagdollVisualToggleRect( screenW, screenH );
    const RunReplayTrack scrubTrack = loadedPresentation ? RunReplayTrack::Presentation : RunReplayTrack::Solver;
    const UI::UIRect replayLoadButton = ReplayScrubberLoadButtonRect( screenW, screenH, scrubTrack );
    const bool solverToolsEnabled = !loadedPresentation && solverReplayAvailable;
    const bool inHotZone = hotZone.Contains( mouse.x, mouse.y );
    const bool overPanel = panel.Contains( mouse.x, mouse.y );
    const bool overSaveButton = solverToolsEnabled && solverSaveButton.Contains( mouse.x, mouse.y );
    const bool overLoadButton = replayLoadButton.Contains( mouse.x, mouse.y );
    const bool branchTargetAvailable =
        m_replayRuntime.Scrubber().historicalSamplePaused &&
        ( ( loadedPresentation && m_replayRuntime.Scrubber().activeTrack == RunReplayTrack::Presentation &&
            CurrentReplayScrubSample() != nullptr ) ||
          ( solverToolsEnabled && m_replayRuntime.Scrubber().activeTrack == RunReplayTrack::Solver &&
            CurrentReplaySolverScrubSample() != nullptr ) );
    const bool overBranchButton = branchButton.Contains( mouse.x, mouse.y );
    const bool overPauseButton = solverToolsEnabled && pauseButton.Contains( mouse.x, mouse.y );
    const bool overVelocityEditToggle = solverToolsEnabled && velocityEditToggle.Contains( mouse.x, mouse.y );
    const bool overPredictControl = solverToolsEnabled && predictControl.Contains( mouse.x, mouse.y );
    const bool overPredictToggle = solverToolsEnabled && predictToggle.Contains( mouse.x, mouse.y );
    const bool overRagdollVisualToggle = solverToolsEnabled && ragdollVisualToggle.Contains( mouse.x, mouse.y );
    const bool overPredictUi = overPredictControl || overPredictToggle || overRagdollVisualToggle;
    const bool overPredictHorizon =
        solverToolsEnabled && ( predictHorizon.Contains( mouse.x, mouse.y ) ||
                                ( predictControl.Contains( mouse.x, mouse.y ) && mouse.x >= predictHorizon.x &&
                                  mouse.x <= predictHorizon.x + predictHorizon.w ) );
    const RunReplayTrack hoveredTrack = scrubTrack;
    const bool canTakeMouse =
        !uiBlocksMouse || m_replayRuntime.Scrubber().dragging || m_replayRuntime.Prediction().horizonDragging;
    const double now = m_timers.simulationTimer.GetTotalTime();

    if ( inHotZone || overPanel || overSaveButton || overLoadButton || overBranchButton || overPauseButton ||
         overVelocityEditToggle || overPredictUi || m_replayRuntime.Scrubber().dragging ||
         m_replayRuntime.Prediction().horizonDragging || m_replayRuntime.Scrubber().historicalSamplePaused ||
         m_replayRuntime.Scrubber().liveAdvanceHeld )
    {
        m_replayRuntime.Scrubber().visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
    }
    m_replayRuntime.Scrubber().saveHovered =
        overSaveButton && ( m_replayRuntime.Scrubber().visibleUntil >= now || m_replayRuntime.Scrubber().dragging ||
                            m_replayRuntime.Scrubber().historicalSamplePaused );
    m_replayRuntime.Scrubber().loadHovered =
        overLoadButton && ( m_replayRuntime.Scrubber().visibleUntil >= now || m_replayRuntime.Scrubber().dragging ||
                            m_replayRuntime.Scrubber().historicalSamplePaused );
    m_replayRuntime.Scrubber().saveHoveredTrack = hoveredTrack;
    const bool branchControlVisible =
        m_replayRuntime.Scrubber().visibleUntil >= now || m_replayRuntime.Scrubber().dragging ||
        m_replayRuntime.Scrubber().historicalSamplePaused || m_replayRuntime.Scrubber().liveAdvanceHeld;
    m_replayRuntime.Scrubber().branchHovered = branchTargetAvailable && overBranchButton && branchControlVisible;
    const bool predictionControlVisible =
        solverToolsEnabled &&
        ( m_replayRuntime.Scrubber().visibleUntil >= now || m_replayRuntime.Scrubber().dragging ||
          m_replayRuntime.Prediction().horizonDragging || m_replayRuntime.Scrubber().historicalSamplePaused ||
          m_replayRuntime.Scrubber().liveAdvanceHeld );
    m_replayRuntime.Scrubber().pauseHovered = solverToolsEnabled && overPauseButton && predictionControlVisible;
    m_replayRuntime.VelocityEdit().toggleHovered =
        solverToolsEnabled && overVelocityEditToggle && predictionControlVisible;
    m_replayRuntime.Prediction().checkboxHovered = solverToolsEnabled && overPredictToggle && predictionControlVisible;
    m_replayRuntime.Prediction().ragdollVisualsHovered =
        solverToolsEnabled && overRagdollVisualToggle && predictionControlVisible;
    m_replayRuntime.Prediction().decreaseHovered = false;
    m_replayRuntime.Prediction().increaseHovered = false;
    m_replayRuntime.Prediction().horizonHovered = solverToolsEnabled && overPredictHorizon && predictionControlVisible;

    bool consumesMouse =
        canTakeMouse && ( m_replayRuntime.Scrubber().dragging || m_replayRuntime.Prediction().horizonDragging ||
                          ( m_replayRuntime.Scrubber().visibleUntil >= now &&
                            ( inHotZone || overPanel || overSaveButton || overBranchButton || overLoadButton ||
                              overPauseButton || overVelocityEditToggle || overPredictUi ) ) );

    if ( branchTargetAvailable &&
         ( restorePressed || ( leftPressed && canTakeMouse && overBranchButton && branchControlVisible ) ) )
    {
        SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayBranchTarget,
                                                            InteractionExitReason::EnterReplay );
        RestoreReplayScrubberSelectionAsLive( now );
        consumesMouse = true;
        return true;
    }

    auto setPredictionHorizonFromMouse = [&]( bool ensureReplayPredictionOwner )
    {
        EnterInteractiveSceneRun();
        if ( ensureReplayPredictionOwner )
        {
            SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayPrediction,
                                                                InteractionExitReason::EnterReplay );
        }
        const float nextSeconds = ReplayPredictionHorizonFromMouse( mouse.x, predictHorizon );
        if ( nextSeconds != m_replayRuntime.Prediction().horizonSeconds )
        {
            m_replayRuntime.Prediction().horizonSeconds = nextSeconds;
            MarkReplayPredictionDirty();
        }
        m_replayRuntime.Scrubber().visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayRuntime.Scrubber().visible = true;
        consumesMouse = true;
    };

    if ( solverToolsEnabled && leftPressed && canTakeMouse && overPauseButton &&
         m_replayRuntime.Scrubber().visibleUntil >= now )
    {
        SetReplayLiveAdvanceHeld( !m_replayRuntime.Scrubber().liveAdvanceHeld );
        m_replayRuntime.Scrubber().visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayRuntime.Scrubber().visible = true;
        consumesMouse = true;
    }
    else if ( solverToolsEnabled && leftPressed && canTakeMouse && overVelocityEditToggle &&
              m_replayRuntime.Scrubber().visibleUntil >= now )
    {
        SetReplayVelocityEditEnabled( !m_replayRuntime.VelocityEdit().enabled );
        m_replayRuntime.Scrubber().visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayRuntime.Scrubber().visible = true;
        consumesMouse = true;
    }
    else if ( solverToolsEnabled && leftPressed && canTakeMouse && overRagdollVisualToggle &&
              m_replayRuntime.Scrubber().visibleUntil >= now )
    {
        m_replayRuntime.Prediction().ragdollVisualsEnabled = !m_replayRuntime.Prediction().ragdollVisualsEnabled;
        ClearReplayPredictionFutureNodeCache( m_replayRuntime.Prediction() );
        m_replayRuntime.Scrubber().visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayRuntime.Scrubber().visible = true;
        consumesMouse = true;
    }
    else if ( solverToolsEnabled && leftPressed && canTakeMouse && overPredictHorizon &&
              m_replayRuntime.Scrubber().visibleUntil >= now )
    {
        m_replayRuntime.Prediction().horizonDragging = true;
        BeginReplayToolGesture( RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag,
                                WorldInteractionOwner::ReplayPrediction,
                                RuntimePointerButton::Left,
                                mouse.x,
                                mouse.y );
        setPredictionHorizonFromMouse( false );
        if ( !m_replayRuntime.Scrubber().mouseCaptured )
        {
            UI::InputControl::BeginMouseCapture( hwnd );
            m_replayRuntime.Scrubber().mouseCaptured = true;
        }
    }
    else if ( solverToolsEnabled && leftPressed && canTakeMouse && overPredictToggle &&
              m_replayRuntime.Scrubber().visibleUntil >= now )
    {
        EnterInteractiveSceneRun();
        const float previousPredictionPresentT =
            ReplayScrubberPresentTrackPosition( solverReplayStats, m_replayRuntime.Prediction() );
        m_replayRuntime.Prediction().enabled = !m_replayRuntime.Prediction().enabled;
        SetWorldInteractionOwnerAfterInteractionTransition( m_replayRuntime.Prediction().enabled
                                                                ? WorldInteractionOwner::ReplayPrediction
                                                                : WorldInteractionOwner::ReplayScrub,
                                                            InteractionExitReason::EnterReplay );
        m_replayRuntime.Prediction().horizonSeconds = std::clamp( m_replayRuntime.Prediction().horizonSeconds,
                                                                  REPLAY_PREDICTION_MIN_SECONDS,
                                                                  REPLAY_PREDICTION_MAX_SECONDS );
        if ( !m_replayRuntime.Prediction().enabled )
        {
            const float currentPosition =
                ReplayScrubberTrackPosition( m_replayRuntime.Scrubber(), RunReplayTrack::Solver );
            if ( ReplayScrubberTrackPositionIsFuture( currentPosition, previousPredictionPresentT ) )
            {
                ReplayScrubberSetTrackPosition( m_replayRuntime.Scrubber(), RunReplayTrack::Solver, 1.0f );
                m_replayRuntime.Scrubber().historicalSamplePaused = false;
            }
            ClearReplayPredictionCache();
        }
        MarkReplayPredictionDirty();
        m_replayRuntime.Scrubber().visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayRuntime.Scrubber().visible = true;
        consumesMouse = true;
    }
    else if ( solverToolsEnabled && leftPressed && canTakeMouse && overSaveButton &&
              m_replayRuntime.Scrubber().visibleUntil >= now )
    {
        EnterInteractiveSceneRun();
        SaveReplayBufferFromScrubber( RunReplayTrack::Presentation );
        consumesMouse = true;
    }
    else if ( leftPressed && canTakeMouse && overLoadButton && m_replayRuntime.Scrubber().visibleUntil >= now )
    {
        PromptLoadReplayPresentationArtifact( hwnd );
        consumesMouse = true;
    }
    else if ( leftPressed && canTakeMouse && !overBranchButton && !overPauseButton && !overPredictUi &&
              !overLoadButton && ( inHotZone || overPanel || m_replayRuntime.Scrubber().historicalSamplePaused ) )
    {
        EnterInteractiveSceneRun();
        BeginReplayToolGesture( RuntimeInteractionGestureKind::ReplayScrubDrag,
                                WorldInteractionOwner::ReplayScrub,
                                RuntimePointerButton::Left,
                                mouse.x,
                                mouse.y );
        m_replayRuntime.Scrubber().activeTrack = scrubTrack;
        ReplayScrubberSyncActivePosition( m_replayRuntime.Scrubber() );
        m_replayRuntime.Scrubber().dragging = true;
        if ( !m_replayRuntime.Scrubber().mouseCaptured )
        {
            UI::InputControl::BeginMouseCapture( hwnd );
            m_replayRuntime.Scrubber().mouseCaptured = true;
        }
    }

    if ( m_replayRuntime.Scrubber().dragging )
    {
        ReplayScrubberSetTrackPosition(
            m_replayRuntime.Scrubber(),
            m_replayRuntime.Scrubber().activeTrack,
            ReplayScrubberPositionFromMouse( mouse.x, screenW, screenH, m_replayRuntime.Scrubber().activeTrack ) );
        if ( loadedPresentation )
        {
            m_replayRuntime.Scrubber().historicalSamplePaused = true;
        }
        else
        {
            const float presentT =
                ReplayScrubberPresentTrackPosition( solverReplayStats, m_replayRuntime.Prediction() );
            if ( ReplayScrubberAtPresentTrackPosition( m_replayRuntime.Scrubber().position, presentT ) )
            {
                ReplayScrubberSetTrackPosition( m_replayRuntime.Scrubber(),
                                                m_replayRuntime.Scrubber().activeTrack,
                                                presentT );
                m_replayRuntime.Scrubber().historicalSamplePaused = false;
            }
            else
            {
                m_replayRuntime.Scrubber().historicalSamplePaused = true;
            }
        }

        if ( leftReleased )
        {
            m_replayRuntime.Scrubber().dragging = false;
            EndReplayToolGesture( RuntimeInteractionGestureKind::ReplayScrubDrag );
            if ( m_replayRuntime.Scrubber().mouseCaptured )
            {
                UI::InputControl::EndMouseCapture();
                m_replayRuntime.Scrubber().mouseCaptured = false;
            }
        }
    }
    else if ( m_replayRuntime.Prediction().horizonDragging )
    {
        setPredictionHorizonFromMouse( false );
        if ( leftReleased )
        {
            m_replayRuntime.Prediction().horizonDragging = false;
            EndReplayToolGesture( RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag );
            if ( m_replayRuntime.Scrubber().mouseCaptured )
            {
                UI::InputControl::EndMouseCapture();
                m_replayRuntime.Scrubber().mouseCaptured = false;
            }
        }
    }
    else if ( !loadedPresentation && !m_replayRuntime.Scrubber().historicalSamplePaused )
    {
        ReplayScrubberSetAllTrackPositions(
            m_replayRuntime.Scrubber(),
            ReplayScrubberPresentTrackPosition( solverReplayStats, m_replayRuntime.Prediction() ) );
    }

    m_replayRuntime.Scrubber().visible =
        m_replayRuntime.Scrubber().dragging || m_replayRuntime.Prediction().horizonDragging ||
        m_replayRuntime.Scrubber().historicalSamplePaused || m_replayRuntime.Scrubber().liveAdvanceHeld ||
        m_replayRuntime.Scrubber().visibleUntil >= now;
    UpdateReplayInspectionCamera();
    return consumesMouse;
}


void Run::ClearReplayPathVisualizer()
{
    ClearReplayCameraFocus( true );
    m_replayRuntime.PathVisualizer().hasTarget = false;
    m_replayRuntime.PathVisualizer().targetId = ReplayBodyId{};
    m_replayRuntime.PathVisualizer().targetModelIndex = -1;
    m_replayRuntime.PathVisualizer().targetName[0] = '\0';
    m_replayRuntime.PathVisualizer().futureNodes.clear();
    m_replayRuntime.PathVisualizer().targets.clear();
    m_replayRuntime.CauseTree().rows.clear();
    m_replayRuntime.CauseTree().hoveredRow = -1;
    m_replayRuntime.CauseTree().selectedRow = -1;
    m_replayRuntime.CauseTree().scrollY = 0.0f;
    ClearReplayPredictionCache();
    MarkReplayPredictionDirty();
}


void Run::MarkReplayPredictionDirty()
{
    CancelReplayPredictionJob( false );
    m_replayRuntime.Prediction().dirty = true;
}


void Run::ClearReplayPredictionCache()
{
    CancelReplayPredictionJob( true );
    m_replayRuntime.Prediction().targetId = ReplayBodyId{};
    m_replayRuntime.Prediction().sourceFrameIndex = 0;
    m_replayRuntime.Prediction().sourceSolverHash = 0;
    m_replayRuntime.Prediction().sourceSimulationSeconds = 0.0;
    m_replayRuntime.Prediction().lastBuildTime = 0.0;
}


bool Run::BuildReplayCauseTreeRows()
{
    PROFILE_SCOPED( "Frame/Replay/CauseTree/BuildRows" );
    m_replayRuntime.CauseTree().rows.clear();

    if ( !m_replayRuntime.PathVisualizer().hasTarget || m_replayRuntime.PathVisualizer().targetId.value == 0 )
    {
        return false;
    }

    const bool usePrediction =
        m_replayRuntime.Prediction().enabled && m_replayRuntime.Prediction().frames.size() >= 2 &&
        m_replayRuntime.Prediction().targetId.value == m_replayRuntime.PathVisualizer().targetId.value;
    const std::vector<RunReplayPathTraceNode>& nodes =
        usePrediction ? m_replayRuntime.Prediction().futureNodes : m_replayRuntime.PathVisualizer().futureNodes;
    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    const ReplaySolverFrameSample* solverSample = CurrentReplaySolverScrubSample();
    const std::size_t solverContactCount =
        solverSample ? solverSample->worldSnapshot.persistentContacts.size() : static_cast<std::size_t>( 0 );
    const std::size_t estimatedRows = 1 + nodes.size() + solverContactCount * 3;
    if ( m_replayRuntime.CauseTree().rows.capacity() < estimatedRows )
    {
        m_replayRuntime.CauseTree().rows.reserve( estimatedRows );
    }

    auto modelIndexForId = [&]( ReplayBodyId id ) -> int
    {
        for ( int i = 0; i < static_cast<int>( models.size() ); ++i )
        {
            if ( models[static_cast<std::size_t>( i )].GetReplayBodyId() == id.value )
            {
                return i;
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
        if ( modelIndex < static_cast<int>( models.size() ) )
        {
            id.value = models[static_cast<std::size_t>( modelIndex )].GetReplayBodyId();
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
        if ( modelIndex >= 0 && modelIndex < static_cast<int>( models.size() ) )
        {
            const char* modelName = models[static_cast<std::size_t>( modelIndex )].GetName();
            if ( modelName && modelName[0] != '\0' )
            {
                strncpy_s( out, outSize, modelName, _TRUNCATE );
                return;
            }
        }
        if ( const ReplaySolverFrameSample* sample = CurrentReplaySolverScrubSample() )
        {
            if ( const ReplaySolverBodySample* body = FindReplayBodyById( *sample, id ) )
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
                contactRow.kind = RunReplayCauseTreeRowKind::PredictionContact;
                contactRow.id = bodyRow.id;
                contactRow.parentId = node.parentId;
                contactRow.firstFrame = node.firstFrame;
                contactRow.depth = bodyRow.depth + 1;
                contactRow.modelIndex = bodyRow.modelIndex;
                contactRow.contactIndex = i;
                contactRow.prediction = true;
                contactRow.point = node.contactPoint;
                contactRow.normal = ReplayNormalizeOr( node.contactNormal, Vector3( 0.0f, 1.0f, 0.0f ) );
                sprintf_s( contactRow.name, sizeof( contactRow.name ), "Predicted contact" );
                sprintf_s( contactRow.detail,
                           sizeof( contactRow.detail ),
                           "first frame %llu  normal %.2f %.2f %.2f",
                           static_cast<unsigned long long>( node.firstFrame ),
                           contactRow.normal.x,
                           contactRow.normal.y,
                           contactRow.normal.z );
                m_replayRuntime.CauseTree().rows.push_back( contactRow );
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
        std::vector<ManifoldGroup> groups;
        groups.reserve( solverSample->worldSnapshot.persistentContacts.size() );
        for ( const ReplaySolverPersistentContactSample& contact : solverSample->worldSnapshot.persistentContacts )
        {
            if ( !ReplayContactHasModelIndex( contact, bodyRow.modelIndex ) )
            {
                continue;
            }
            const int otherModelIndex = ReplayContactOtherModelIndex( contact, bodyRow.modelIndex );
            const bool terrain = contact.isTerrain || otherModelIndex < 0;
            bool exists = false;
            for ( const ManifoldGroup& group : groups )
            {
                if ( group.otherModelIndex == otherModelIndex && group.terrain == terrain )
                {
                    exists = true;
                    break;
                }
            }
            if ( !exists )
            {
                groups.push_back( { otherModelIndex, terrain } );
            }
        }

        for ( const ManifoldGroup& group : groups )
        {
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
            m_replayRuntime.CauseTree().rows.push_back( manifoldRow );

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
                m_replayRuntime.CauseTree().rows.push_back( solverRow );
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
        row.modelIndex = modelIndex >= 0 ? modelIndex : modelIndexForId( id );
        row.prediction = usePrediction;
        writeName( id, row.modelIndex, fallbackName, row.name, sizeof( row.name ) );
        if ( row.modelIndex >= 0 && solverSample )
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
        m_replayRuntime.CauseTree().rows.push_back( row );
        appendSolverRowsForBody( m_replayRuntime.CauseTree().rows.back() );
        return true;
    };

    addBodyRow( m_replayRuntime.PathVisualizer().targetId,
                ReplayBodyId{},
                0,
                0,
                m_replayRuntime.PathVisualizer().targetModelIndex,
                m_replayRuntime.PathVisualizer().targetName );

    auto addChildren = [&]( auto&& self, ReplayBodyId parentId, int fallbackDepth ) -> void
    {
        for ( const RunReplayPathTraceNode& node : nodes )
        {
            if ( node.parentId.value != parentId.value )
            {
                continue;
            }
            const int depth = node.depth > 0 ? node.depth : fallbackDepth;
            if ( addBodyRow( node.id, parentId, node.firstFrame, depth, modelIndexForId( node.id ), nullptr ) )
            {
                self( self, node.id, depth + 1 );
            }
        }
    };
    addChildren( addChildren, m_replayRuntime.PathVisualizer().targetId, 1 );

    m_replayRuntime.CauseTree().selectedRow = -1;
    if ( m_replayRuntime.Camera().focusKind != RunReplayCameraFocusKind::None )
    {
        for ( int i = 0; i < static_cast<int>( m_replayRuntime.CauseTree().rows.size() ); ++i )
        {
            const RunReplayCauseTreeRow& row = m_replayRuntime.CauseTree().rows[static_cast<std::size_t>( i )];
            if ( row.kind != m_replayRuntime.Camera().focusRowKind ||
                 row.id.value != m_replayRuntime.Camera().focusedId.value ||
                 row.modelIndex != m_replayRuntime.Camera().focusModelIndex ||
                 row.terrain != m_replayRuntime.Camera().focusTerrain )
            {
                continue;
            }
            if ( row.kind == RunReplayCauseTreeRowKind::Body ||
                 ( row.counterpartId.value == m_replayRuntime.Camera().counterpartId.value &&
                   row.counterpartModelIndex == m_replayRuntime.Camera().focusCounterpartModelIndex &&
                   ( row.kind != RunReplayCauseTreeRowKind::SolverRow ||
                     ( row.featureId == m_replayRuntime.Camera().focusFeatureId &&
                       row.solverRowIndex == m_replayRuntime.Camera().focusSolverRowIndex ) ) ) )
            {
                m_replayRuntime.CauseTree().selectedRow = i;
                m_replayRuntime.Camera().focusedRow = i;
                break;
            }
        }
    }
    if ( m_replayRuntime.CauseTree().selectedRow >= static_cast<int>( m_replayRuntime.CauseTree().rows.size() ) )
    {
        m_replayRuntime.CauseTree().selectedRow = -1;
    }
    m_replayRuntime.CauseTree().scrollY = std::clamp( m_replayRuntime.CauseTree().scrollY,
                                                      0.0f,
                                                      ReplayCauseWindowMaxScroll( m_replayRuntime.CauseTree() ) );
    return !m_replayRuntime.CauseTree().rows.empty();
}


bool Run::TryResolveReplayCauseTreeBodyPosition( ReplayBodyId id, Vector3& outPosition, float* outRadius ) const
{
    if ( id.value == 0 )
    {
        return false;
    }

    if ( outRadius )
    {
        *outRadius = 1.0f;
    }

    if ( m_replayRuntime.Prediction().enabled && !m_replayRuntime.Prediction().frames.empty() &&
         m_replayRuntime.Prediction().targetId.value == m_replayRuntime.PathVisualizer().targetId.value )
    {
        if ( const RunReplayPredictionBodySample* body =
                 FindReplayPredictionBodyById( m_replayRuntime.Prediction().frames.front(), id ) )
        {
            outPosition = body->position;
            if ( outRadius && body->modelIndex >= 0 &&
                 body->modelIndex < static_cast<int>( m_cGameModelCollection.Models().size() ) )
            {
                *outRadius =
                    EditorModelRadius( m_cGameModelCollection.Models()[static_cast<std::size_t>( body->modelIndex )] );
            }
            return true;
        }
    }

    if ( const ReplaySolverFrameSample* sample = CurrentReplaySolverScrubSample() )
    {
        if ( const ReplaySolverBodySample* body = FindReplayBodyById( *sample, id ) )
        {
            outPosition = body->position;
            if ( outRadius && body->modelIndex >= 0 &&
                 body->modelIndex < static_cast<int>( m_cGameModelCollection.Models().size() ) )
            {
                *outRadius =
                    EditorModelRadius( m_cGameModelCollection.Models()[static_cast<std::size_t>( body->modelIndex )] );
            }
            return true;
        }
    }

    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    for ( const GameModel& model : models )
    {
        if ( model.GetReplayBodyId() == id.value )
        {
            outPosition = model.GetPosition();
            if ( outRadius )
            {
                *outRadius = EditorModelRadius( model );
            }
            return true;
        }
    }
    return false;
}


bool Run::FocusReplayCauseTreeBody( ReplayBodyId id )
{
    RunReplayCauseTreeRow row;
    row.kind = RunReplayCauseTreeRowKind::Body;
    row.id = id;
    ActivateReplayCameraForCauseRow( row, -1 );
    return m_replayRuntime.Camera().focusKind != RunReplayCameraFocusKind::None;
}


void Run::ActivateReplayCameraForCauseRow( const RunReplayCauseTreeRow& row, int rowIndex )
{
    PROFILE_SCOPED( "Frame/Replay/CauseTree/Focus" );
    Vector3 targetPosition = row.point;
    float targetRadius = 2.0f;
    RunReplayCameraFocusKind focusKind = RunReplayCameraFocusKind::Body;
    switch ( row.kind )
    {
    case RunReplayCauseTreeRowKind::Body:
        if ( !TryResolveReplayCauseTreeBodyPosition( row.id, targetPosition, &targetRadius ) )
        {
            return;
        }
        focusKind = RunReplayCameraFocusKind::Body;
        break;
    case RunReplayCauseTreeRowKind::Manifold:
        TryResolveReplayCauseTreeBodyPosition( row.id, targetPosition, &targetRadius );
        targetPosition = row.point;
        targetRadius = (std::max)( targetRadius * 0.55f, 2.0f );
        focusKind = RunReplayCameraFocusKind::Manifold;
        break;
    case RunReplayCauseTreeRowKind::SolverRow:
        TryResolveReplayCauseTreeBodyPosition( row.id, targetPosition, &targetRadius );
        targetPosition = row.point;
        targetRadius = (std::max)( targetRadius * 0.45f, 1.5f );
        focusKind = RunReplayCameraFocusKind::SolverRow;
        break;
    case RunReplayCauseTreeRowKind::PredictionContact:
        TryResolveReplayCauseTreeBodyPosition( row.id, targetPosition, &targetRadius );
        targetPosition = row.point;
        targetRadius = (std::max)( targetRadius * 0.45f, 1.5f );
        focusKind = RunReplayCameraFocusKind::PredictionContact;
        break;
    default:
        return;
    }

    if ( VectorMagSquared( targetPosition ) <= TOLERANCE * TOLERANCE && row.kind != RunReplayCauseTreeRowKind::Body )
    {
        return;
    }

    EnterInteractiveSceneRun();
    const bool hadReplayCameraFocus = m_replayRuntime.Camera().focusKind != RunReplayCameraFocusKind::None;
    if ( !m_replayRuntime.Scrubber().liveAdvanceHeld )
    {
        SetReplayLiveAdvanceHeld( true );
        m_replayRuntime.Camera().ownsSimulationPause = true;
    }
    else if ( !hadReplayCameraFocus )
    {
        m_replayRuntime.Camera().ownsSimulationPause = false;
    }
    EnterReplayInspectionCamera();

    m_replayRuntime.Camera().focusKind = focusKind;
    m_replayRuntime.Camera().focusedId = row.id;
    m_replayRuntime.Camera().counterpartId = row.counterpartId;
    m_replayRuntime.Camera().focusedRow = rowIndex;
    m_replayRuntime.Camera().focusRowKind = row.kind;
    m_replayRuntime.Camera().focusModelIndex = row.modelIndex;
    m_replayRuntime.Camera().focusCounterpartModelIndex = row.counterpartModelIndex;
    m_replayRuntime.Camera().focusContactIndex = row.contactIndex;
    m_replayRuntime.Camera().focusSolverRowIndex = row.solverRowIndex;
    m_replayRuntime.Camera().focusFeatureId = row.featureId;
    m_replayRuntime.Camera().focusTerrain = row.terrain;
    m_replayRuntime.Camera().targetPoint = targetPosition;
    m_replayRuntime.Camera().targetNormal = ReplayNormalizeOr( row.normal, Vector3( 0.0f, 1.0f, 0.0f ) );
    m_replayRuntime.Camera().impulseVector = row.impulse;
    m_replayRuntime.Camera().targetRadius = targetRadius;
    m_replayRuntime.CauseTree().focusedId = row.id;
    m_replayRuntime.CauseTree().selectedRow = rowIndex;

    if ( m_systems.cameras )
    {
        const Vector3 eye = m_systems.cameras->GetRenderCameraTranslation();
        Vector3 direction = ReplayNormalizeOr( eye - targetPosition, Vector3( 0.45f, 0.28f, 0.85f ) );
        direction = ReplayNormalizeOr( direction, Vector3( 0.45f, 0.28f, 0.85f ) );
        const float distance = (std::max)( 12.0f, targetRadius * 5.5f );
        const Vector3 newEye = targetPosition + direction * distance + Vector3( 0.0f, targetRadius * 0.35f, 0.0f );
        m_systems.cameras->CancelTween();
        m_systems.cameras->SetPrimaryPosition( newEye );
        m_systems.cameras->SetViewCoordinates( targetPosition );
        m_systems.cameras->ResetRelativity();
    }
    InputController::ResetMouseLook( m_camera );
    Input::SetSystemCursorVisible( true );
}


void Run::ClearReplayCameraFocus( bool restoreCamera )
{
    m_replayRuntime.Camera().focusKind = RunReplayCameraFocusKind::None;
    m_replayRuntime.Camera().focusedId = ReplayBodyId{};
    m_replayRuntime.Camera().counterpartId = ReplayBodyId{};
    m_replayRuntime.Camera().focusedRow = -1;
    m_replayRuntime.Camera().focusRowKind = RunReplayCauseTreeRowKind::Body;
    m_replayRuntime.Camera().focusModelIndex = -1;
    m_replayRuntime.Camera().focusCounterpartModelIndex = -1;
    m_replayRuntime.Camera().focusContactIndex = -1;
    m_replayRuntime.Camera().focusSolverRowIndex = -1;
    m_replayRuntime.Camera().focusFeatureId = 0;
    m_replayRuntime.Camera().focusTerrain = false;
    m_replayRuntime.Camera().targetPoint = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    m_replayRuntime.Camera().targetNormal = Vector3( 0.0f, 1.0f, 0.0f );
    m_replayRuntime.Camera().impulseVector = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    m_replayRuntime.CauseTree().focusedId = ReplayBodyId{};
    m_replayRuntime.CauseTree().selectedRow = -1;

    if ( restoreCamera )
    {
        if ( m_replayRuntime.Camera().ownsSimulationPause && m_replayRuntime.Scrubber().liveAdvanceHeld &&
             !m_replayRuntime.Scrubber().historicalSamplePaused )
        {
            m_replayRuntime.Scrubber().liveAdvanceHeld = false;
        }
        m_replayRuntime.Camera().ownsSimulationPause = false;
        ExitReplayInspectionCamera();
    }
    else
    {
        UpdateReplayInspectionCamera();
    }
}


bool Run::TickReplayCauseTreeInput( HWND hwnd, bool uiBlocksMouse, int wheelDelta )
{
    PROFILE_SCOPED( "Frame/Replay/CauseTree/Input" );
    const bool leftDown = Input::IsLeftMouseDown();
    const bool leftPressed = leftDown && !m_replayRuntime.CauseTree().leftWasDown;
    const bool leftReleased = !leftDown && m_replayRuntime.CauseTree().leftWasDown;
    m_replayRuntime.CauseTree().leftWasDown = leftDown;
    m_replayRuntime.CauseTree().hoveredRow = -1;

    const int screenW = WindowScreenWidth();
    const int screenH = WindowScreenHeight();
    if ( m_runtimeTools.Editor().editorModeEnabled || screenW <= 0 || screenH <= 0 || !BuildReplayCauseTreeRows() )
    {
        if ( leftReleased &&
             ( m_replayRuntime.CauseTree().draggingWindow || m_replayRuntime.CauseTree().resizingWindow ) )
        {
            UI::InputControl::EndMouseCapture();
            EndReplayToolGesture( RuntimeInteractionGestureKind::ReplayCauseTreeDrag );
            m_replayRuntime.CauseTree().draggingWindow = false;
            m_replayRuntime.CauseTree().resizingWindow = false;
        }
        return false;
    }

    EnsureReplayCauseWindowPlacement( m_replayRuntime.CauseTree(), screenW, screenH );
    const POINT mouse = Input::GetClientMouseCoordinates();
    const UI::UIRect panel = ReplayCauseWindowRect( m_replayRuntime.CauseTree() );
    const UI::UIRect title = ReplayCauseWindowTitleRect( m_replayRuntime.CauseTree() );
    const UI::UIRect content = ReplayCauseWindowContentRect( m_replayRuntime.CauseTree() );
    const UI::UIRect resize = ReplayCauseWindowResizeRect( m_replayRuntime.CauseTree() );

    if ( m_replayRuntime.CauseTree().draggingWindow )
    {
        m_replayRuntime.CauseTree().x = mouse.x - m_replayRuntime.CauseTree().dragOffsetX;
        m_replayRuntime.CauseTree().y = mouse.y - m_replayRuntime.CauseTree().dragOffsetY;
        ClampReplayCauseWindow( m_replayRuntime.CauseTree(), screenW, screenH );
        if ( leftReleased )
        {
            UI::InputControl::EndMouseCapture();
            EndReplayToolGesture( RuntimeInteractionGestureKind::ReplayCauseTreeDrag );
            m_replayRuntime.CauseTree().draggingWindow = false;
        }
        return true;
    }

    if ( m_replayRuntime.CauseTree().resizingWindow )
    {
        m_replayRuntime.CauseTree().width =
            m_replayRuntime.CauseTree().resizeStartWidth + ( mouse.x - m_replayRuntime.CauseTree().resizeStartMouseX );
        m_replayRuntime.CauseTree().height =
            m_replayRuntime.CauseTree().resizeStartHeight + ( mouse.y - m_replayRuntime.CauseTree().resizeStartMouseY );
        ClampReplayCauseWindow( m_replayRuntime.CauseTree(), screenW, screenH );
        if ( leftReleased )
        {
            UI::InputControl::EndMouseCapture();
            EndReplayToolGesture( RuntimeInteractionGestureKind::ReplayCauseTreeDrag );
            m_replayRuntime.CauseTree().resizingWindow = false;
        }
        return true;
    }

    const bool insidePanel = panel.Contains( mouse.x, mouse.y );
    if ( uiBlocksMouse || !insidePanel )
    {
        return false;
    }

    if ( wheelDelta != 0 )
    {
        SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayCauseTree,
                                                            InteractionExitReason::EnterReplay );
        const float wheelRows = static_cast<float>( wheelDelta ) / 120.0f;
        m_replayRuntime.CauseTree().scrollY -= wheelRows * REPLAY_CAUSE_WINDOW_ROW_HEIGHT * 3.0f;
        ClampReplayCauseWindow( m_replayRuntime.CauseTree(), screenW, screenH );
        return true;
    }

    if ( leftPressed && resize.Contains( mouse.x, mouse.y ) )
    {
        BeginReplayToolGesture( RuntimeInteractionGestureKind::ReplayCauseTreeDrag,
                                WorldInteractionOwner::ReplayCauseTree,
                                RuntimePointerButton::Left,
                                mouse.x,
                                mouse.y,
                                -1,
                                1 );
        m_replayRuntime.CauseTree().resizingWindow = true;
        m_replayRuntime.CauseTree().resizeStartMouseX = mouse.x;
        m_replayRuntime.CauseTree().resizeStartMouseY = mouse.y;
        m_replayRuntime.CauseTree().resizeStartWidth = m_replayRuntime.CauseTree().width;
        m_replayRuntime.CauseTree().resizeStartHeight = m_replayRuntime.CauseTree().height;
        UI::InputControl::BeginMouseCapture( hwnd );
        return true;
    }

    if ( leftPressed && title.Contains( mouse.x, mouse.y ) )
    {
        BeginReplayToolGesture( RuntimeInteractionGestureKind::ReplayCauseTreeDrag,
                                WorldInteractionOwner::ReplayCauseTree,
                                RuntimePointerButton::Left,
                                mouse.x,
                                mouse.y,
                                -1,
                                0 );
        m_replayRuntime.CauseTree().draggingWindow = true;
        m_replayRuntime.CauseTree().dragOffsetX = mouse.x - m_replayRuntime.CauseTree().x;
        m_replayRuntime.CauseTree().dragOffsetY = mouse.y - m_replayRuntime.CauseTree().y;
        UI::InputControl::BeginMouseCapture( hwnd );
        return true;
    }

    if ( content.Contains( mouse.x, mouse.y ) )
    {
        const float localY = static_cast<float>( mouse.y ) - content.y + m_replayRuntime.CauseTree().scrollY;
        const int rowIndex = static_cast<int>( floorf( localY / REPLAY_CAUSE_WINDOW_ROW_HEIGHT ) );
        if ( rowIndex >= 0 && rowIndex < static_cast<int>( m_replayRuntime.CauseTree().rows.size() ) )
        {
            m_replayRuntime.CauseTree().hoveredRow = rowIndex;
            if ( leftPressed )
            {
                SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayCauseTree,
                                                                    InteractionExitReason::EnterReplay );
                ActivateReplayCameraForCauseRow( m_replayRuntime.CauseTree().rows[static_cast<std::size_t>( rowIndex )],
                                                 rowIndex );
            }
        }
        else if ( leftPressed )
        {
            ClearReplayPathVisualizer();
        }
    }

    return true;
}


void Run::SetReplayVelocityEditEnabled( bool enabled )
{
    if ( m_replayRuntime.VelocityEdit().enabled == enabled )
    {
        return;
    }

    PROFILE_SCOPED( "Frame/Replay/VelocityEdit/Toggle" );
    m_replayRuntime.VelocityEdit().enabled = enabled;
    m_replayRuntime.VelocityEdit().hotLinearAxis = -1;
    m_replayRuntime.VelocityEdit().hotAngularAxis = -1;
    m_replayRuntime.VelocityEdit().activeAxis = -1;
    CancelReplayToolDragState();

    if ( enabled )
    {
        EnterInteractiveSceneRun();
        SetReplayLiveAdvanceHeld( true );
        SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayVelocityEdit,
                                                            InteractionExitReason::EnterReplay );
        m_replayRuntime.Prediction().enabled = true;
        m_replayRuntime.Prediction().horizonSeconds = std::clamp( m_replayRuntime.Prediction().horizonSeconds,
                                                                  REPLAY_PREDICTION_MIN_SECONDS,
                                                                  REPLAY_PREDICTION_MAX_SECONDS );
        MarkReplayPredictionDirty();
        m_replayRuntime.Scrubber().visibleUntil =
            m_timers.simulationTimer.GetTotalTime() + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayRuntime.Scrubber().visible = true;
    }
    else if ( m_interaction.Owner() == WorldInteractionOwner::ReplayVelocityEdit )
    {
        SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayScrub,
                                                            InteractionExitReason::EnterReplay );
    }
}


int Run::ResolveReplayVelocityEditModelIndex() const
{
    if ( !m_replayRuntime.PathVisualizer().hasTarget || m_replayRuntime.PathVisualizer().targetId.value == 0 )
    {
        return -1;
    }

    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    const int cachedIndex = m_replayRuntime.PathVisualizer().targetModelIndex;
    if ( cachedIndex >= 0 && cachedIndex < static_cast<int>( models.size() ) &&
         models[static_cast<std::size_t>( cachedIndex )].GetReplayBodyId() ==
             m_replayRuntime.PathVisualizer().targetId.value )
    {
        return cachedIndex;
    }

    for ( int i = 0; i < static_cast<int>( models.size() ); ++i )
    {
        if ( models[static_cast<std::size_t>( i )].GetReplayBodyId() ==
             m_replayRuntime.PathVisualizer().targetId.value )
        {
            return i;
        }
    }
    return -1;
}


int Run::HitReplayVelocityLinearAxis( const Vector3& rayOrigin, const Vector3& rayDirection ) const
{
    const int modelIndex = ResolveReplayVelocityEditModelIndex();
    if ( modelIndex < 0 || modelIndex >= m_cGameModelCollection.GetModelCount() )
    {
        return -1;
    }

    const GameModel& model = m_cGameModelCollection.Models()[static_cast<std::size_t>( modelIndex )];
    if ( model.IsFixed() )
    {
        return -1;
    }

    const Vector3 origin = model.GetPosition();
    const float radius = EditorModelRadius( model );
    const float threshold = (std::max)( 1.15f, radius * 0.12f );
    const float thresholdSq = threshold * threshold;
    int bestAxis = -1;
    float bestDistanceSq = FLT_MAX;
    for ( int axis = 0; axis < 3; ++axis )
    {
        const Vector3 axisVector = EditorAxisVector( axis );
        const float component = ReplayVelocityAxisComponent( model.GetVelocity(), axis );
        const Vector3 endpoint = origin + axisVector * ReplayVelocityLinearVisualAxisT( radius, component );
        const float distanceSq = DistanceRayToSegmentSquared( rayOrigin, rayDirection, origin, endpoint );
        if ( distanceSq <= thresholdSq && distanceSq < bestDistanceSq )
        {
            bestDistanceSq = distanceSq;
            bestAxis = axis;
        }
    }
    return bestAxis;
}


int Run::HitReplayVelocityAngularAxis( const Vector3& rayOrigin, const Vector3& rayDirection ) const
{
    const int modelIndex = ResolveReplayVelocityEditModelIndex();
    if ( modelIndex < 0 || modelIndex >= m_cGameModelCollection.GetModelCount() )
    {
        return -1;
    }

    const GameModel& model = m_cGameModelCollection.Models()[static_cast<std::size_t>( modelIndex )];
    if ( model.IsFixed() )
    {
        return -1;
    }

    const Vector3 origin = model.GetPosition();
    const float modelRadius = EditorModelRadius( model );
    int bestAxis = -1;
    float bestDiff = FLT_MAX;
    for ( int axis = 0; axis < 3; ++axis )
    {
        const Vector3 normal = EditorAxisVector( axis );
        const float denom = normal * rayDirection;
        if ( fabsf( denom ) <= 1e-4f )
        {
            continue;
        }

        const float rayT = ( normal * ( origin - rayOrigin ) ) / denom;
        if ( rayT < 0.0f )
        {
            continue;
        }

        const float ringRadius =
            ReplayVelocityAngularVisualRadius( modelRadius,
                                               ReplayVelocityAxisComponent( model.GetAngularVelocity(), axis ) );
        const float threshold = (std::max)( 1.10f, ringRadius * 0.08f );
        const Vector3 hitPoint = rayOrigin + rayDirection * rayT;
        const Vector3 radial = hitPoint - origin;
        const float radialDistance = VectorMag( radial - normal * ( radial * normal ) );
        const float diff = fabsf( radialDistance - ringRadius );
        if ( diff <= threshold && diff < bestDiff )
        {
            bestDiff = diff;
            bestAxis = axis;
        }
    }
    return bestAxis;
}


bool Run::TryReplayVelocityAxisRayParameter( int axis,
                                             const Vector3& rayOrigin,
                                             const Vector3& rayDirection,
                                             float& outAxisT ) const
{
    const int modelIndex = ResolveReplayVelocityEditModelIndex();
    if ( axis < 0 || axis > 2 || modelIndex < 0 || modelIndex >= m_cGameModelCollection.GetModelCount() )
    {
        return false;
    }

    const Vector3 axisOrigin = m_cGameModelCollection.Models()[static_cast<std::size_t>( modelIndex )].GetPosition();
    const Vector3 axisVector = EditorAxisVector( axis );
    const Vector3 w = axisOrigin - rayOrigin;
    const float b = axisVector * rayDirection;
    const float d = axisVector * w;
    const float e = rayDirection * w;
    const float denom = 1.0f - b * b;
    if ( fabsf( denom ) <= 1e-5f )
    {
        return false;
    }

    outAxisT = ( b * e - d ) / denom;
    return true;
}


bool Run::TryReplayVelocityAngularRayAngle( int axis,
                                            const Vector3& rayOrigin,
                                            const Vector3& rayDirection,
                                            float& outAngle ) const
{
    const int modelIndex = ResolveReplayVelocityEditModelIndex();
    if ( axis < 0 || axis > 2 || modelIndex < 0 || modelIndex >= m_cGameModelCollection.GetModelCount() )
    {
        return false;
    }

    const Vector3 origin = m_cGameModelCollection.Models()[static_cast<std::size_t>( modelIndex )].GetPosition();
    const Vector3 normal = EditorAxisVector( axis );
    const float denom = normal * rayDirection;
    if ( fabsf( denom ) <= 1e-4f )
    {
        return false;
    }

    const float rayT = ( normal * ( origin - rayOrigin ) ) / denom;
    if ( rayT < 0.0f )
    {
        return false;
    }

    Vector3 radial = rayOrigin + rayDirection * rayT - origin;
    radial -= normal * ( radial * normal );
    const float radialLenSq = radial * radial;
    if ( radialLenSq <= TOLERANCE * TOLERANCE )
    {
        return false;
    }
    radial = radial * ( 1.0f / sqrtf( radialLenSq ) );

    const Vector3 basisA = EditorRotationRingBasisA( axis );
    const Vector3 basisB = EditorRotationRingBasisB( axis );
    outAngle = atan2f( radial * basisB, radial * basisA );
    return true;
}


void Run::ApplyReplayVelocityEditToModel( int modelIndex,
                                          const Vector3& linearVelocity,
                                          const Vector3& angularVelocity )
{
    PROFILE_SCOPED( "Frame/Replay/VelocityEdit/Apply" );
    if ( modelIndex < 0 || modelIndex >= m_cGameModelCollection.GetModelCount() )
    {
        return;
    }

    Vector3 clampedLinear = linearVelocity;
    Vector3 clampedAngular = angularVelocity;
    clampedLinear.x = std::clamp( clampedLinear.x, -REPLAY_VELOCITY_EDIT_LINEAR_MAX, REPLAY_VELOCITY_EDIT_LINEAR_MAX );
    clampedLinear.y = std::clamp( clampedLinear.y, -REPLAY_VELOCITY_EDIT_LINEAR_MAX, REPLAY_VELOCITY_EDIT_LINEAR_MAX );
    clampedLinear.z = std::clamp( clampedLinear.z, -REPLAY_VELOCITY_EDIT_LINEAR_MAX, REPLAY_VELOCITY_EDIT_LINEAR_MAX );
    clampedAngular.x =
        std::clamp( clampedAngular.x, -REPLAY_VELOCITY_EDIT_ANGULAR_MAX, REPLAY_VELOCITY_EDIT_ANGULAR_MAX );
    clampedAngular.y =
        std::clamp( clampedAngular.y, -REPLAY_VELOCITY_EDIT_ANGULAR_MAX, REPLAY_VELOCITY_EDIT_ANGULAR_MAX );
    clampedAngular.z =
        std::clamp( clampedAngular.z, -REPLAY_VELOCITY_EDIT_ANGULAR_MAX, REPLAY_VELOCITY_EDIT_ANGULAR_MAX );

    GameModel& model = m_cGameModelCollection.GetModelAtIndex( modelIndex );
    if ( model.IsFixed() )
    {
        return;
    }

    model.SetLinearVelocity( clampedLinear );
    model.SetAngularVelocity( clampedAngular );
    if ( VectorMagSquared( clampedLinear ) > TOLERANCE * TOLERANCE ||
         VectorMagSquared( clampedAngular ) > TOLERANCE * TOLERANCE )
    {
        m_cGameModelCollection.GetPhysicsEngine().WakeBody( m_cGameModelCollection, modelIndex );
    }
    m_cGameModelCollection.InvalidatePhysicsStreams();
    MarkReplayPredictionDirty();
    m_replayRuntime.Scrubber().visibleUntil = m_timers.simulationTimer.GetTotalTime() + REPLAY_SCRUBBER_VISIBLE_SECONDS;
    m_replayRuntime.Scrubber().visible = true;
}


void Run::ApplyReplayVelocityEditDrag( const Vector3& rayOrigin, const Vector3& rayDirection )
{
    const int modelIndex = ResolveReplayVelocityEditModelIndex();
    if ( modelIndex < 0 || modelIndex >= m_cGameModelCollection.GetModelCount() ||
         m_replayRuntime.VelocityEdit().activeAxis < 0 )
    {
        EndReplayToolGesture( RuntimeInteractionGestureKind::ReplayVelocityDrag );
        m_replayRuntime.VelocityEdit().dragging = false;
        m_replayRuntime.VelocityEdit().draggingAngular = false;
        m_replayRuntime.VelocityEdit().activeAxis = -1;
        if ( m_replayRuntime.VelocityEdit().mouseCaptured )
        {
            UI::InputControl::EndMouseCapture();
            m_replayRuntime.VelocityEdit().mouseCaptured = false;
        }
        return;
    }

    Vector3 linearVelocity = m_replayRuntime.VelocityEdit().dragStartLinearVelocity;
    Vector3 angularVelocity = m_replayRuntime.VelocityEdit().dragStartAngularVelocity;
    if ( m_replayRuntime.VelocityEdit().draggingAngular )
    {
        float currentAngle = 0.0f;
        if ( !TryReplayVelocityAngularRayAngle( m_replayRuntime.VelocityEdit().activeAxis,
                                                rayOrigin,
                                                rayDirection,
                                                currentAngle ) )
        {
            return;
        }
        const float angleDelta = WrapEditorAngleDelta( currentAngle - m_replayRuntime.VelocityEdit().dragStartAngle );
        const float component = ReplayVelocityAxisComponent( m_replayRuntime.VelocityEdit().dragStartAngularVelocity,
                                                             m_replayRuntime.VelocityEdit().activeAxis ) +
                                angleDelta * ( REPLAY_VELOCITY_EDIT_ANGULAR_MAX / _PI );
        ReplayVelocitySetAxisComponent(
            angularVelocity,
            m_replayRuntime.VelocityEdit().activeAxis,
            std::clamp( component, -REPLAY_VELOCITY_EDIT_ANGULAR_MAX, REPLAY_VELOCITY_EDIT_ANGULAR_MAX ) );
    }
    else
    {
        float axisT = 0.0f;
        if ( !TryReplayVelocityAxisRayParameter( m_replayRuntime.VelocityEdit().activeAxis,
                                                 rayOrigin,
                                                 rayDirection,
                                                 axisT ) )
        {
            return;
        }
        const float component =
            ReplayVelocityAxisComponent( m_replayRuntime.VelocityEdit().dragStartLinearVelocity,
                                         m_replayRuntime.VelocityEdit().activeAxis ) +
            ( axisT - m_replayRuntime.VelocityEdit().dragStartAxisT ) * ReplayVelocityLinearUnitsPerWorld();
        ReplayVelocitySetAxisComponent(
            linearVelocity,
            m_replayRuntime.VelocityEdit().activeAxis,
            std::clamp( component, -REPLAY_VELOCITY_EDIT_LINEAR_MAX, REPLAY_VELOCITY_EDIT_LINEAR_MAX ) );
    }

    ApplyReplayVelocityEditToModel( modelIndex, linearVelocity, angularVelocity );
}


bool Run::TickReplayVelocityEditInput( HWND hwnd, bool uiBlocksMouse )
{
    PROFILE_SCOPED( "Frame/Replay/VelocityEdit/Input" );
    const bool leftDown = Input::IsLeftMouseDown();
    const bool leftPressed = leftDown && !m_replayRuntime.VelocityEdit().leftWasDown;
    const bool leftReleased = !leftDown && m_replayRuntime.VelocityEdit().leftWasDown;
    m_replayRuntime.VelocityEdit().leftWasDown = leftDown;

    if ( !m_replayRuntime.VelocityEdit().enabled || m_runtimeTools.Editor().editorModeEnabled ||
         !SceneState().isScenePhysics || WindowScreenWidth() <= 0 || WindowScreenHeight() <= 0 )
    {
        m_replayRuntime.VelocityEdit().hotLinearAxis = -1;
        m_replayRuntime.VelocityEdit().hotAngularAxis = -1;
        if ( m_replayRuntime.VelocityEdit().dragging )
        {
            EndReplayToolGesture( RuntimeInteractionGestureKind::ReplayVelocityDrag );
            m_replayRuntime.VelocityEdit().dragging = false;
            m_replayRuntime.VelocityEdit().draggingAngular = false;
            m_replayRuntime.VelocityEdit().activeAxis = -1;
        }
        if ( m_replayRuntime.VelocityEdit().mouseCaptured )
        {
            UI::InputControl::EndMouseCapture();
            m_replayRuntime.VelocityEdit().mouseCaptured = false;
        }
        return false;
    }

    Vector3 rayOrigin;
    Vector3 rayDirection;
    if ( !TryBuildMouseWorldRay( rayOrigin, rayDirection ) )
    {
        if ( m_replayRuntime.VelocityEdit().dragging && ( leftReleased || !leftDown ) )
        {
            EndReplayToolGesture( RuntimeInteractionGestureKind::ReplayVelocityDrag );
            m_replayRuntime.VelocityEdit().dragging = false;
            m_replayRuntime.VelocityEdit().draggingAngular = false;
            m_replayRuntime.VelocityEdit().activeAxis = -1;
            if ( m_replayRuntime.VelocityEdit().mouseCaptured )
            {
                UI::InputControl::EndMouseCapture();
                m_replayRuntime.VelocityEdit().mouseCaptured = false;
            }
        }
        return m_replayRuntime.VelocityEdit().dragging;
    }

    if ( m_replayRuntime.VelocityEdit().dragging )
    {
        if ( leftDown && !uiBlocksMouse )
        {
            ApplyReplayVelocityEditDrag( rayOrigin, rayDirection );
        }
        if ( leftReleased || !leftDown )
        {
            EndReplayToolGesture( RuntimeInteractionGestureKind::ReplayVelocityDrag );
            m_replayRuntime.VelocityEdit().dragging = false;
            m_replayRuntime.VelocityEdit().draggingAngular = false;
            m_replayRuntime.VelocityEdit().activeAxis = -1;
            if ( m_replayRuntime.VelocityEdit().mouseCaptured )
            {
                UI::InputControl::EndMouseCapture();
                m_replayRuntime.VelocityEdit().mouseCaptured = false;
            }
        }
        return true;
    }

    m_replayRuntime.VelocityEdit().hotAngularAxis =
        uiBlocksMouse ? -1 : HitReplayVelocityAngularAxis( rayOrigin, rayDirection );
    m_replayRuntime.VelocityEdit().hotLinearAxis =
        ( uiBlocksMouse || m_replayRuntime.VelocityEdit().hotAngularAxis >= 0 )
            ? -1
            : HitReplayVelocityLinearAxis( rayOrigin, rayDirection );

    if ( !uiBlocksMouse && leftPressed )
    {
        const POINT mouse = Input::GetClientMouseCoordinates();
        const int modelIndex = ResolveReplayVelocityEditModelIndex();
        if ( modelIndex >= 0 && modelIndex < m_cGameModelCollection.GetModelCount() )
        {
            const GameModel& model = m_cGameModelCollection.Models()[static_cast<std::size_t>( modelIndex )];
            if ( m_replayRuntime.VelocityEdit().hotAngularAxis >= 0 )
            {
                float startAngle = 0.0f;
                if ( TryReplayVelocityAngularRayAngle( m_replayRuntime.VelocityEdit().hotAngularAxis,
                                                       rayOrigin,
                                                       rayDirection,
                                                       startAngle ) )
                {
                    EnterInteractiveSceneRun();
                    SetReplayLiveAdvanceHeld( true );
                    m_replayRuntime.Prediction().enabled = true;
                    BeginReplayToolGesture( RuntimeInteractionGestureKind::ReplayVelocityDrag,
                                            WorldInteractionOwner::ReplayVelocityEdit,
                                            RuntimePointerButton::Left,
                                            mouse.x,
                                            mouse.y,
                                            modelIndex,
                                            m_replayRuntime.VelocityEdit().hotAngularAxis,
                                            true );
                    m_replayRuntime.VelocityEdit().dragging = true;
                    m_replayRuntime.VelocityEdit().draggingAngular = true;
                    m_replayRuntime.VelocityEdit().activeAxis = m_replayRuntime.VelocityEdit().hotAngularAxis;
                    m_replayRuntime.VelocityEdit().dragStartAngle = startAngle;
                    m_replayRuntime.VelocityEdit().dragStartLinearVelocity = model.GetVelocity();
                    m_replayRuntime.VelocityEdit().dragStartAngularVelocity = model.GetAngularVelocity();
                    if ( !m_replayRuntime.VelocityEdit().mouseCaptured )
                    {
                        UI::InputControl::BeginMouseCapture( hwnd );
                        m_replayRuntime.VelocityEdit().mouseCaptured = true;
                    }
                    return true;
                }
            }
            else if ( m_replayRuntime.VelocityEdit().hotLinearAxis >= 0 )
            {
                float axisT = 0.0f;
                if ( TryReplayVelocityAxisRayParameter( m_replayRuntime.VelocityEdit().hotLinearAxis,
                                                        rayOrigin,
                                                        rayDirection,
                                                        axisT ) )
                {
                    EnterInteractiveSceneRun();
                    SetReplayLiveAdvanceHeld( true );
                    m_replayRuntime.Prediction().enabled = true;
                    BeginReplayToolGesture( RuntimeInteractionGestureKind::ReplayVelocityDrag,
                                            WorldInteractionOwner::ReplayVelocityEdit,
                                            RuntimePointerButton::Left,
                                            mouse.x,
                                            mouse.y,
                                            modelIndex,
                                            m_replayRuntime.VelocityEdit().hotLinearAxis,
                                            false );
                    m_replayRuntime.VelocityEdit().dragging = true;
                    m_replayRuntime.VelocityEdit().draggingAngular = false;
                    m_replayRuntime.VelocityEdit().activeAxis = m_replayRuntime.VelocityEdit().hotLinearAxis;
                    m_replayRuntime.VelocityEdit().dragStartAxisT = axisT;
                    m_replayRuntime.VelocityEdit().dragStartLinearVelocity = model.GetVelocity();
                    m_replayRuntime.VelocityEdit().dragStartAngularVelocity = model.GetAngularVelocity();
                    if ( !m_replayRuntime.VelocityEdit().mouseCaptured )
                    {
                        UI::InputControl::BeginMouseCapture( hwnd );
                        m_replayRuntime.VelocityEdit().mouseCaptured = true;
                    }
                    return true;
                }
            }
        }
    }

    return m_replayRuntime.VelocityEdit().hotLinearAxis >= 0 || m_replayRuntime.VelocityEdit().hotAngularAxis >= 0;
}


void Run::RenderReplayVelocityEditOverlay( RunEditorTracer& tracer )
{
    PROFILE_SCOPED( "Frame/Replay/VelocityEdit/Overlay" );
    if ( !m_replayRuntime.VelocityEdit().enabled || m_runtimeTools.Editor().editorModeEnabled )
    {
        return;
    }

    const int modelIndex = ResolveReplayVelocityEditModelIndex();
    if ( modelIndex < 0 || modelIndex >= m_cGameModelCollection.GetModelCount() )
    {
        return;
    }

    const GameModel& model = m_cGameModelCollection.Models()[static_cast<std::size_t>( modelIndex )];
    if ( model.IsFixed() )
    {
        return;
    }
    tracer.AddReplayVelocityGizmo( model,
                                   m_replayRuntime.VelocityEdit().hotLinearAxis,
                                   m_replayRuntime.VelocityEdit().hotAngularAxis,
                                   m_replayRuntime.VelocityEdit().activeAxis,
                                   m_replayRuntime.VelocityEdit().draggingAngular );
}


bool Run::TryPickReplayPathTargetFromMouse( bool additive, bool clearOnMiss )
{
    Vector3 rayOrigin;
    Vector3 rayDirection;
    if ( !TryBuildMouseWorldRay( rayOrigin, rayDirection ) )
    {
        if ( clearOnMiss )
        {
            ClearReplayPathVisualizer();
        }
        return false;
    }

    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    ReplayBodyId pickedId;
    int pickedIndex = -1;
    char pickedName[64] = {};
    if ( const ReplaySolverFrameSample* sample = CurrentReplaySolverScrubSample() )
    {
        float bestT = FLT_MAX;
        for ( const ReplaySolverBodySample& body : sample->bodies )
        {
            float radius = 1.0f;
            if ( body.modelIndex >= 0 && body.modelIndex < static_cast<int>( models.size() ) )
            {
                radius = EditorModelRadius( models[static_cast<std::size_t>( body.modelIndex )] ) + 1.0f;
            }
            float rayT = 0.0f;
            if ( IntersectRaySphere( rayOrigin, rayDirection, body.position, radius, rayT ) && rayT < bestT )
            {
                bestT = rayT;
                pickedId = body.id;
                pickedIndex = body.modelIndex;
                pickedName[0] = '\0';
                if ( body.name[0] != '\0' )
                {
                    strncpy_s( pickedName, sizeof( pickedName ), body.name, _TRUNCATE );
                }
            }
        }
    }
    else
    {
        RuntimePickRequest request;
        request.purpose = RuntimePickPurpose::ReplayPathTarget;
        request.models = &models;
        request.rayOrigin = rayOrigin;
        request.rayDirection = rayDirection;

        RuntimePickResult result;
        if ( RuntimePickService::TryPickModel( request, result ) && result.modelIndex >= 0 &&
             result.modelIndex < static_cast<int>( models.size() ) )
        {
            pickedIndex = result.modelIndex;
            const GameModel& model = models[static_cast<std::size_t>( pickedIndex )];
            pickedId.value = model.GetReplayBodyId();
            const char* modelName = model.GetName();
            if ( modelName && modelName[0] != '\0' )
            {
                strncpy_s( pickedName, sizeof( pickedName ), modelName, _TRUNCATE );
            }
        }
    }

    if ( pickedIndex >= 0 && pickedIndex < static_cast<int>( models.size() ) )
    {
        const int collectionIndex = ReplayRagdollTorsoModelIndexForPart( models, pickedIndex );
        if ( collectionIndex >= 0 && collectionIndex < static_cast<int>( models.size() ) &&
             collectionIndex != pickedIndex )
        {
            const GameModel& rootModel = models[static_cast<std::size_t>( collectionIndex )];
            pickedIndex = collectionIndex;
            pickedId.value = rootModel.GetReplayBodyId();
            pickedName[0] = '\0';
            const char* rootName = rootModel.GetName();
            if ( rootName && rootName[0] != '\0' )
            {
                strncpy_s( pickedName, sizeof( pickedName ), rootName, _TRUNCATE );
            }
        }
    }

    if ( pickedId.value != 0 )
    {
        if ( !additive )
        {
            m_replayRuntime.PathVisualizer().targets.clear();
        }

        RunReplayPathTarget* target = FindReplayPathTarget( m_replayRuntime.PathVisualizer(), pickedId );
        if ( !target )
        {
            if ( m_replayRuntime.PathVisualizer().targets.size() >= REPLAY_PATH_MAX_ROOT_TARGETS )
            {
                m_replayRuntime.PathVisualizer().targets.erase( m_replayRuntime.PathVisualizer().targets.begin() );
            }
            RunReplayPathTarget nextTarget;
            nextTarget.id = pickedId;
            m_replayRuntime.PathVisualizer().targets.push_back( nextTarget );
            target = &m_replayRuntime.PathVisualizer().targets.back();
        }

        target->modelIndex = pickedIndex;
        target->name[0] = '\0';
        if ( pickedName[0] != '\0' )
        {
            strncpy_s( target->name, sizeof( target->name ), pickedName, _TRUNCATE );
        }
        ApplyPrimaryReplayPathTarget( m_replayRuntime.PathVisualizer(), pickedId, pickedIndex, target->name );
        m_replayRuntime.PathVisualizer().futureNodes.clear();
        ClearReplayPredictionCache();
        MarkReplayPredictionDirty();
        return true;
    }

    if ( clearOnMiss )
    {
        ClearReplayPathVisualizer();
    }
    return false;
}


void Run::CancelReplayPredictionJob( bool clearSamples )
{
    m_replayRuntime.Prediction().building = false;
    m_replayRuntime.Prediction().complete = false;
    m_replayRuntime.Prediction().targetModelIndex = -1;
    m_replayRuntime.Prediction().nextTick = 1;
    m_replayRuntime.Prediction().targetTickCount = 0;
    m_replayRuntime.Prediction().predictionBodies.clear();
    m_replayRuntime.Prediction().liveRestoreBodies.clear();
    m_replayRuntime.Prediction().predictionWorld = ReplaySolverWorldSnapshot();
    m_replayRuntime.Prediction().liveRestoreWorld = ReplaySolverWorldSnapshot();
    m_replayRuntime.Prediction().buildFrames.clear();
    if ( clearSamples )
    {
        m_replayRuntime.Prediction().frames.clear();
        ClearReplayPredictionFutureNodeCache( m_replayRuntime.Prediction() );
    }
}


bool Run::CaptureReplayPredictionBodyState( std::vector<RunReplayPredictionBodyBackup>& outBodies )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/CaptureBodyState" );
    std::vector<GameModel>& models = m_cGameModelCollection.PhysicsModels();
    const int modelCount = static_cast<int>( models.size() );
    outBodies.clear();
    outBodies.resize( static_cast<std::size_t>( modelCount ) );

    const auto captureBody = [&]( int i )
    {
        const GameModel& model = models[static_cast<std::size_t>( i )];
        RunReplayPredictionBodyBackup backup;
        backup.id.value = model.GetReplayBodyId();
        backup.modelIndex = i;
        backup.position = model.GetPosition();
        backup.orientation = model.GetOrientation();
        backup.linearVelocity = model.GetVelocity();
        backup.angularVelocity = model.GetAngularVelocity();
        backup.fixedContactHighlightSeconds = model.GetFixedContactHighlightSeconds();
        backup.fixed = model.IsFixed();
        outBodies[static_cast<std::size_t>( i )] = backup;
    };

    // Invariant: this loop is read-only and writes one output slot per body, so
    // it is deterministic under fork-join. Applying backups remains serial
    // because it mutates live GameModel state.
    if ( modelCount >= REPLAY_PREDICTION_PARALLEL_BODY_MIN )
    {
        SkullbonezCore::Threading::WorkerPool::Instance().ParallelFor(
            0,
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


bool Run::ApplyReplayPredictionBodyState( const std::vector<RunReplayPredictionBodyBackup>& bodies )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/ApplyBodyState" );
    std::vector<GameModel>& models = m_cGameModelCollection.PhysicsModels();
    if ( bodies.size() != models.size() )
    {
        return false;
    }

    for ( const RunReplayPredictionBodyBackup& backup : bodies )
    {
        if ( backup.modelIndex < 0 || backup.modelIndex >= static_cast<int>( models.size() ) )
        {
            return false;
        }

        GameModel& model = models[static_cast<std::size_t>( backup.modelIndex )];
        if ( model.GetReplayBodyId() != backup.id.value )
        {
            return false;
        }

        model.SetFixed( backup.fixed );
        model.SetPosition( backup.position );
        model.SetOrientation( backup.orientation );
        model.SetLinearVelocity( backup.linearVelocity );
        model.SetAngularVelocity( backup.angularVelocity );
        model.SetFixedContactHighlightSeconds( backup.fixedContactHighlightSeconds );
    }
    return true;
}


void Run::CaptureReplayPredictionFrame( ReplayFrameIndex frameIndex )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/CaptureSample" );
    std::vector<GameModel>& models = m_cGameModelCollection.PhysicsModels();
    const int modelCount = static_cast<int>( models.size() );
    RunReplayPredictionFrame frame;
    frame.frameIndex = frameIndex;
    frame.simulationSeconds = m_replayRuntime.Prediction().sourceSimulationSeconds +
                              static_cast<double>( frameIndex ) * static_cast<double>( PHYSICS_FIXED_DT );
    frame.tornadoSystemElapsedSeconds = m_cGameModelCollection.GetTornadoSystemElapsedSeconds();
    frame.bodies.resize( static_cast<std::size_t>( modelCount ) );

    const auto captureBody = [&]( int i )
    {
        const GameModel& model = models[static_cast<std::size_t>( i )];
        RunReplayPredictionBodySample body;
        body.id.value = model.GetReplayBodyId();
        body.modelIndex = i;
        body.position = model.GetPosition();
        body.orientation = model.GetOrientation();
        frame.bodies[static_cast<std::size_t>( i )] = body;
    };

    // Why: a 4000-body prediction frame is hundreds of kilobytes of pose copy.
    // Parallel capture pays off there, but small scenes stay serial by threshold.
    if ( modelCount >= REPLAY_PREDICTION_PARALLEL_BODY_MIN )
    {
        SkullbonezCore::Threading::WorkerPool::Instance().ParallelFor(
            0,
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
    frame.debugContacts = m_cGameModelCollection.GetPhysicsDebugContacts();
    m_replayRuntime.Prediction().buildFrames.push_back( std::move( frame ) );
}


bool Run::BeginReplayPredictionJob( ReplayFrameIndex sourceFrameIndex,
                                    uint64_t sourceSolverHash,
                                    const std::chrono::steady_clock::time_point& budgetStart,
                                    double budgetMilliseconds )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/BeginJob" );
    // Hazard: begin captures the initial prediction snapshot. If setup spends
    // the visualizer slice, leave the prediction dirty so the next frame retries
    // instead of piling tree/draw work onto the same render frame.
    if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
    {
        return false;
    }

    CancelReplayPredictionJob( false );
    m_replayRuntime.Prediction().targetId = m_replayRuntime.PathVisualizer().targetId;
    m_replayRuntime.Prediction().dirty = false;

    if ( !m_replayRuntime.Prediction().enabled || !SceneState().isScenePhysics )
    {
        return false;
    }

    m_replayRuntime.Prediction().sourceFrameIndex = sourceFrameIndex;
    m_replayRuntime.Prediction().sourceSolverHash = sourceSolverHash;
    if ( const ReplaySolverFrameSample* latest = m_replayRuntime.Solver().LatestSample() )
    {
        m_replayRuntime.Prediction().sourceSimulationSeconds = latest->simulationSeconds;
    }
    else
    {
        m_replayRuntime.Prediction().sourceSimulationSeconds = m_timers.simulationTimer.GetTimeSinceLastStart();
    }
    m_replayRuntime.Prediction().lastBuildTime = m_timers.simulationTimer.GetTotalTime();

    if ( m_replayRuntime.PathVisualizer().hasTarget && m_replayRuntime.PathVisualizer().targetId.value != 0 )
    {
        std::vector<GameModel>& models = m_cGameModelCollection.PhysicsModels();
        int targetIndex = -1;
        for ( int i = 0; i < static_cast<int>( models.size() ); ++i )
        {
            if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
            {
                m_replayRuntime.Prediction().dirty = true;
                return false;
            }

            if ( models[static_cast<std::size_t>( i )].GetReplayBodyId() ==
                 m_replayRuntime.PathVisualizer().targetId.value )
            {
                targetIndex = i;
                break;
            }
        }
        if ( targetIndex < 0 )
        {
            return false;
        }
        m_replayRuntime.Prediction().targetModelIndex = targetIndex;
        m_replayRuntime.PathVisualizer().targetModelIndex = targetIndex;
    }

    m_replayRuntime.Prediction().horizonSeconds = std::clamp( m_replayRuntime.Prediction().horizonSeconds,
                                                              REPLAY_PREDICTION_MIN_SECONDS,
                                                              REPLAY_PREDICTION_MAX_SECONDS );
    const int predictionTicks =
        (std::max)( 1,
                    static_cast<int>( std::ceil( m_replayRuntime.Prediction().horizonSeconds / PHYSICS_FIXED_DT ) ) );
    m_replayRuntime.Prediction().targetTickCount = predictionTicks;
    m_replayRuntime.Prediction().nextTick = 1;
    m_replayRuntime.Prediction().buildFrames.reserve( static_cast<std::size_t>( predictionTicks + 1 ) );

    if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
    {
        m_replayRuntime.Prediction().dirty = true;
        return false;
    }

    if ( !CaptureReplayPredictionBodyState( m_replayRuntime.Prediction().predictionBodies ) )
    {
        CancelReplayPredictionJob( true );
        return false;
    }
    if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
    {
        CancelReplayPredictionJob( true );
        m_replayRuntime.Prediction().dirty = true;
        return false;
    }

    m_cGameModelCollection.GetPhysicsEngine().CaptureReplaySolverSnapshot( m_replayRuntime.Prediction().predictionWorld,
                                                                           m_cGameModelCollection.GetModelCount() );
    if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
    {
        CancelReplayPredictionJob( true );
        m_replayRuntime.Prediction().dirty = true;
        return false;
    }

    CaptureReplayPredictionFrame( 0 );
    m_replayRuntime.Prediction().building = true;

    return !m_replayRuntime.Prediction().buildFrames.empty();
}


bool Run::StepReplayPredictionJob( const std::chrono::steady_clock::time_point& budgetStart, double budgetMilliseconds )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/Slice" );
    if ( !m_replayRuntime.Prediction().building )
    {
        return m_replayRuntime.Prediction().complete;
    }

    if ( ReplayPredictionMutationReserveSpent( budgetStart, budgetMilliseconds ) )
    {
        return false;
    }

    // Hazard: everything after liveRestoreBodies/liveRestoreWorld succeeds may
    // swap live state for prediction state. All early exits before RestoreLive
    // must happen before the swap, or after the restore block below.
    if ( !CaptureReplayPredictionBodyState( m_replayRuntime.Prediction().liveRestoreBodies ) )
    {
        CancelReplayPredictionJob( true );
        m_replayRuntime.Prediction().dirty = true;
        return false;
    }
    m_cGameModelCollection.GetPhysicsEngine().CaptureReplaySolverSnapshot(
        m_replayRuntime.Prediction().liveRestoreWorld,
        m_cGameModelCollection.GetModelCount() );
    if ( ReplayPredictionMutationReserveSpent( budgetStart, budgetMilliseconds ) )
    {
        return false;
    }

#ifdef _DEBUG
    const bool previousDiagnosticsSuppressed =
        m_cGameModelCollection.GetPhysicsEngine().SetDiagnosticsSuppressed( true );
#endif

    bool jobApplied = false;
    bool jobStateCaptured = false;
    bool progressed = false;

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/ApplyJobState" );
        jobApplied = ApplyReplayPredictionBodyState( m_replayRuntime.Prediction().predictionBodies ) &&
                     m_cGameModelCollection.GetPhysicsEngine().RestoreReplaySolverSnapshot(
                         m_replayRuntime.Prediction().predictionWorld,
                         m_cGameModelCollection.GetModelCount() );
        m_cGameModelCollection.InvalidatePhysicsStreams();
    }

    if ( jobApplied )
    {
        {
            PROFILE_SCOPED( "Frame/Replay/Prediction/Steps" );
            while ( m_replayRuntime.Prediction().nextTick <= m_replayRuntime.Prediction().targetTickCount )
            {
                if ( ReplayPredictionMutationReserveSpent( budgetStart, budgetMilliseconds ) )
                {
                    break;
                }

                {
                    PROFILE_SCOPED( "Frame/Replay/Prediction/StepPhysics" );
                    m_cGameModelCollection.GetPhysicsEngine().Step( m_cGameModelCollection, PHYSICS_FIXED_DT );
                }
                CaptureReplayPredictionFrame( static_cast<ReplayFrameIndex>( m_replayRuntime.Prediction().nextTick ) );
                ++m_replayRuntime.Prediction().nextTick;
                progressed = true;

                if ( ReplayPredictionMutationReserveSpent( budgetStart, budgetMilliseconds ) )
                {
                    break;
                }
            }
        }

        {
            PROFILE_SCOPED( "Frame/Replay/Prediction/CaptureJobState" );
            jobStateCaptured = CaptureReplayPredictionBodyState( m_replayRuntime.Prediction().predictionBodies );
            if ( jobStateCaptured )
            {
                m_cGameModelCollection.GetPhysicsEngine().CaptureReplaySolverSnapshot(
                    m_replayRuntime.Prediction().predictionWorld,
                    m_cGameModelCollection.GetModelCount() );
            }
        }
    }

#ifdef _DEBUG
    m_cGameModelCollection.GetPhysicsEngine().SetDiagnosticsSuppressed( previousDiagnosticsSuppressed );
#endif

    bool liveRestored = false;
    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/RestoreLive" );
        liveRestored = ApplyReplayPredictionBodyState( m_replayRuntime.Prediction().liveRestoreBodies ) &&
                       m_cGameModelCollection.GetPhysicsEngine().RestoreReplaySolverSnapshot(
                           m_replayRuntime.Prediction().liveRestoreWorld,
                           m_cGameModelCollection.GetModelCount() );
        m_cGameModelCollection.InvalidatePhysicsStreams();
    }

    if ( !jobApplied || !jobStateCaptured || !liveRestored )
    {
        CancelReplayPredictionJob( true );
        m_replayRuntime.Prediction().dirty = true;
        return false;
    }

    if ( m_replayRuntime.Prediction().nextTick > m_replayRuntime.Prediction().targetTickCount )
    {
        m_replayRuntime.Prediction().building = false;
        m_replayRuntime.Prediction().complete = true;
        m_replayRuntime.Prediction().frames.swap( m_replayRuntime.Prediction().buildFrames );
        m_replayRuntime.Prediction().buildFrames.clear();
        ClearReplayPredictionFutureNodeCache( m_replayRuntime.Prediction() );
        m_replayRuntime.Prediction().lastBuildTime = m_timers.simulationTimer.GetTotalTime();
    }

    return progressed || m_replayRuntime.Prediction().complete;
}


bool Run::BuildReplayFocusModelMask()
{
    PROFILE_SCOPED( "Frame/Replay/FocusMask" );
    const int modelCount = m_cGameModelCollection.GetModelCount();
    std::vector<uint8_t>& replayFocusModelMask = m_replayRuntime.FocusModelMask();
    if ( !m_replayRuntime.PathVisualizer().hasTarget || m_replayRuntime.PathVisualizer().targetId.value == 0 ||
         modelCount <= 0 )
    {
        replayFocusModelMask.clear();
        return false;
    }

    replayFocusModelMask.assign( static_cast<std::size_t>( modelCount ), 0 );
    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    int markedCount = 0;
    const auto markByReplayId = [&]( ReplayBodyId id, int preferredModelIndex )
    {
        if ( id.value == 0 )
        {
            return;
        }

        int resolvedIndex = -1;
        if ( preferredModelIndex >= 0 && preferredModelIndex < modelCount &&
             models[static_cast<std::size_t>( preferredModelIndex )].GetReplayBodyId() == id.value )
        {
            resolvedIndex = preferredModelIndex;
        }
        else
        {
            for ( int i = 0; i < modelCount; ++i )
            {
                if ( models[static_cast<std::size_t>( i )].GetReplayBodyId() == id.value )
                {
                    resolvedIndex = i;
                    break;
                }
            }
        }

        if ( resolvedIndex >= 0 )
        {
            uint8_t& mask = replayFocusModelMask[static_cast<std::size_t>( resolvedIndex )];
            if ( mask == 0 )
            {
                mask = 1;
                ++markedCount;
            }
        }
    };

    if ( m_replayRuntime.PathVisualizer().targets.empty() )
    {
        markByReplayId( m_replayRuntime.PathVisualizer().targetId, m_replayRuntime.PathVisualizer().targetModelIndex );
    }
    else
    {
        for ( const RunReplayPathTarget& target : m_replayRuntime.PathVisualizer().targets )
        {
            markByReplayId( target.id, target.modelIndex );
        }
    }

    const std::vector<RunReplayPathTraceNode>& futureNodes = m_replayRuntime.Prediction().enabled
                                                                 ? m_replayRuntime.Prediction().futureNodes
                                                                 : m_replayRuntime.PathVisualizer().futureNodes;
    for ( const RunReplayPathTraceNode& node : futureNodes )
    {
        markByReplayId( node.id, node.modelIndex );
    }

    if ( markedCount <= 0 || markedCount >= modelCount )
    {
        replayFocusModelMask.clear();
        return false;
    }
    return true;
}


void Run::RenderReplayPredictionVisualizer( RunEditorTracer& tracer,
                                            const std::chrono::steady_clock::time_point& budgetStart,
                                            double budgetMilliseconds )
{
    PROFILE_SCOPED( "Frame/Replay/PathVisualizer/Prediction" );
    if ( !m_replayRuntime.Prediction().enabled )
    {
        if ( m_replayRuntime.Prediction().building )
        {
            CancelReplayPredictionJob( true );
        }
        return;
    }

    const ReplaySolverFrameSample* latest = m_replayRuntime.Solver().LatestSample();
    const ReplayFrameIndex latestFrame = latest ? latest->frameIndex : 0;
    const uint64_t latestHash = latest ? latest->solverHash : 0;
    const double now = m_timers.simulationTimer.GetTotalTime();
    const bool sourceChanged =
        m_replayRuntime.Prediction().targetId.value != m_replayRuntime.PathVisualizer().targetId.value ||
        m_replayRuntime.Prediction().sourceFrameIndex != latestFrame ||
        m_replayRuntime.Prediction().sourceSolverHash != latestHash;
    const bool refreshDue = ( now - m_replayRuntime.Prediction().lastBuildTime ) >= REPLAY_PREDICTION_REFRESH_SECONDS;
    const bool allowAutomaticRefresh = !m_replayRuntime.Scrubber().liveAdvanceHeld;
    if ( m_replayRuntime.Prediction().dirty ||
         ( allowAutomaticRefresh && !m_replayRuntime.Prediction().building && sourceChanged && refreshDue ) )
    {
        if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
        {
            return;
        }
        BeginReplayPredictionJob( latestFrame, latestHash, budgetStart, budgetMilliseconds );
        if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
        {
            return;
        }
    }
    if ( m_replayRuntime.Prediction().building )
    {
        const double remainingMilliseconds = ReplayPredictionRemainingMilliseconds( budgetStart, budgetMilliseconds );
        if ( remainingMilliseconds <= 0.0 )
        {
            return;
        }
        StepReplayPredictionJob( budgetStart, budgetMilliseconds );
        if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
        {
            return;
        }
    }

    if ( m_replayRuntime.Prediction().frames.size() < 2 )
    {
        return;
    }

    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    if ( !m_replayRuntime.PathVisualizer().hasTarget || m_replayRuntime.PathVisualizer().targetId.value == 0 )
    {
        ClearReplayPredictionFutureNodeCache( m_replayRuntime.Prediction() );
        if ( m_replayRuntime.Prediction().ragdollVisualsEnabled )
        {
            DrawReplayPredictionRagdollTorsoTrails( m_replayRuntime.Prediction(),
                                                    models,
                                                    tracer,
                                                    budgetStart,
                                                    budgetMilliseconds );
        }
        return;
    }

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/BuildTree" );
        UpdateReplayPredictionFutureNodeCache( m_replayRuntime.Prediction(),
                                               models,
                                               m_replayRuntime.PathVisualizer().targetId,
                                               budgetStart,
                                               budgetMilliseconds );
    }
    if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
    {
        return;
    }

    const ReplayFrameIndex lastFrame = m_replayRuntime.Prediction().frames.back().frameIndex;
    const std::size_t sampleStride = ReplayPathStrideForSampleCount( m_replayRuntime.Prediction().frames.size() );
    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/DrawRoot" );
        bool hasPrevious = false;
        Vector3 previous = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        std::size_t ordinal = 0;
        for ( const RunReplayPredictionFrame& frame : m_replayRuntime.Prediction().frames )
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
            const RunReplayPredictionBodySample* body =
                FindReplayPredictionBodyByIdWithHint( frame,
                                                      m_replayRuntime.PathVisualizer().targetId,
                                                      m_replayRuntime.PathVisualizer().targetModelIndex );
            if ( !body )
            {
                continue;
            }

            if ( hasPrevious && VectorMagSquared( body->position - previous ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
            {
                const float t = ReplayPathFrameT( frame.frameIndex, 0, lastFrame );
                tracer.AddReplayPathSegment( previous, body->position, 1.0f - t * 0.85f, 1.0f, 1.0f - t * 0.72f );
            }
            previous = body->position;
            hasPrevious = true;
        }
    }

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/DrawChildren" );
        ReplayPathChildDrawContext childDraw;
        childDraw.tracer = &tracer;
        childDraw.models = &models;
        childDraw.presentFrame = 0;
        childDraw.lastFrame = lastFrame;
        childDraw.sampleStride = sampleStride;
        childDraw.nodeCount =
            (std::min)( m_replayRuntime.Prediction().futureNodes.size(), REPLAY_PATH_MAX_FUTURE_NODES );
        for ( std::size_t i = 0; i < childDraw.nodeCount; ++i )
        {
            childDraw.nodes[i].node = m_replayRuntime.Prediction().futureNodes[i];
        }

        std::size_t ordinal = 0;
        for ( const RunReplayPredictionFrame& frame : m_replayRuntime.Prediction().frames )
        {
            if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
            {
                return;
            }

            const std::size_t currentOrdinal = ordinal++;
            bool importantChildFrame = frame.frameIndex == 0 || frame.frameIndex == lastFrame;
            for ( std::size_t i = 0; i < childDraw.nodeCount; ++i )
            {
                if ( frame.frameIndex == childDraw.nodes[i].node.firstFrame )
                {
                    importantChildFrame = true;
                    break;
                }
            }
            if ( !importantChildFrame && !ShouldDrawReplayPathSample( currentOrdinal, sampleStride ) )
            {
                continue;
            }

            for ( std::size_t i = 0; i < childDraw.nodeCount; ++i )
            {
                if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
                {
                    return;
                }

                ReplayPathChildDrawState& drawState = childDraw.nodes[i];
                const RunReplayPredictionBodySample* body =
                    FindReplayPredictionBodyByIdWithHint( frame, drawState.node.id, drawState.node.modelIndex );
                if ( !body )
                {
                    continue;
                }

                if ( frame.frameIndex <= drawState.node.firstFrame )
                {
                    if ( !drawState.markerDrawn )
                    {
                        const float radius =
                            ReplayFutureMarkerRadiusForModelIndex( childDraw.models, body->modelIndex );
                        tracer.AddReplayFutureTargetMarker( body->position, radius, drawState.node.depth );
                        drawState.markerDrawn = true;
                    }
                    if ( drawState.hasIncomingPrevious &&
                         VectorMagSquared( body->position - drawState.incomingPrevious ) >
                             REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
                    {
                        const float t = ReplayPathFrameT( frame.frameIndex, 0, drawState.node.firstFrame );
                        float r = 0.92f;
                        float g = 0.54f;
                        float b = 0.18f;
                        ReplayChildIncomingColor( drawState.node.depth, t, r, g, b );
                        tracer.AddReplayPathSegment( drawState.incomingPrevious, body->position, r, g, b );
                    }
                    drawState.incomingPrevious = body->position;
                    drawState.hasIncomingPrevious = true;
                }

                if ( frame.frameIndex >= drawState.node.firstFrame && drawState.hasPrevious &&
                     VectorMagSquared( body->position - drawState.previous ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
                {
                    const float t = ReplayPathFrameT( frame.frameIndex, drawState.node.firstFrame, lastFrame );
                    float r = 0.5f;
                    float g = 0.5f;
                    float b = 0.56f;
                    ReplayChildFutureColor( drawState.node.depth, t, r, g, b );
                    tracer.AddReplayPathSegment( drawState.previous, body->position, r, g, b );
                }
                if ( frame.frameIndex >= drawState.node.firstFrame )
                {
                    drawState.previous = body->position;
                    drawState.hasPrevious = true;
                }
            }
        }

        for ( const RunReplayPathTraceNode& node : m_replayRuntime.Prediction().futureNodes )
        {
            if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
            {
                return;
            }

            float r = 0.58f;
            float g = 0.64f;
            float b = 0.68f;
            if ( node.depth <= 1 )
            {
                r = 0.68f;
                g = 0.78f;
                b = 0.76f;
            }
            tracer.AddReplayContactMarker( node.contactPoint, node.contactNormal, r, g, b );
        }
    }

    if ( m_replayRuntime.Prediction().ragdollVisualsEnabled &&
         !ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
    {
        DrawReplayPredictionRagdollTorsoTrails( m_replayRuntime.Prediction(),
                                                models,
                                                tracer,
                                                budgetStart,
                                                budgetMilliseconds );
    }
}


void Run::RenderReplayPathVisualizer( RunEditorTracer& tracer )
{
    PROFILE_SCOPED( "Frame/Replay/PathVisualizer" );
    // Concept: this marker owns the replay visualizer frame budget.
    //
    // Prediction, retained solver paths, future-node tree updates, and contact
    // markers all share this deadline. Child functions receive the same start
    // time so profiler nesting cannot hide extra replay work outside the cap.
    const auto visualizerStart = std::chrono::steady_clock::now();
    RenderReplayPredictionVisualizer( tracer, visualizerStart, REPLAY_PREDICTION_MAX_WORK_MILLISECONDS );
    if ( ReplayPredictionBudgetExpired( visualizerStart, REPLAY_PREDICTION_MAX_WORK_MILLISECONDS ) )
    {
        return;
    }

    if ( !m_replayRuntime.PathVisualizer().hasTarget )
    {
        return;
    }

    if ( !m_replayRuntime.Solver().IsEnabled() )
    {
        return;
    }

    if ( m_replayRuntime.PathVisualizer().targets.empty() && m_replayRuntime.PathVisualizer().targetId.value != 0 )
    {
        RunReplayPathTarget target;
        target.id = m_replayRuntime.PathVisualizer().targetId;
        target.modelIndex = m_replayRuntime.PathVisualizer().targetModelIndex;
        if ( m_replayRuntime.PathVisualizer().targetName[0] != '\0' )
        {
            strncpy_s( target.name, sizeof( target.name ), m_replayRuntime.PathVisualizer().targetName, _TRUNCATE );
        }
        m_replayRuntime.PathVisualizer().targets.push_back( target );
    }

    const ReplaySolverFrameSample* presentSample = CurrentReplaySolverScrubSample();
    if ( !presentSample )
    {
        presentSample = m_replayRuntime.Solver().LatestSample();
    }
    if ( !presentSample )
    {
        return;
    }

    ReplayPathBoundsContext bounds;
    m_replayRuntime.Solver().ForEachSampleChronological( CaptureReplayPathBounds, &bounds );
    if ( ReplayPredictionBudgetExpired( visualizerStart, REPLAY_PREDICTION_MAX_WORK_MILLISECONDS ) )
    {
        return;
    }
    if ( !bounds.hasSample )
    {
        return;
    }

    const ReplayFrameIndex presentFrame = std::clamp( presentSample->frameIndex, bounds.firstFrame, bounds.lastFrame );
    const ReplayRecorderStats stats = m_replayRuntime.Solver().GetStats();
    const std::size_t sampleStride = ReplayPathStrideForSampleCount( stats.sampleCount );

    m_replayRuntime.PathVisualizer().futureNodes.clear();
    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    for ( RunReplayPathTarget& target : m_replayRuntime.PathVisualizer().targets )
    {
        if ( ReplayPredictionBudgetExpired( visualizerStart, REPLAY_PREDICTION_MAX_WORK_MILLISECONDS ) )
        {
            return;
        }

        if ( target.id.value == 0 )
        {
            continue;
        }

        PROFILE_SCOPED( "Frame/Replay/PathVisualizer/RetainedTarget" );
        RunReplayPathVisualizerState targetVisualizer;
        ApplyPrimaryReplayPathTarget( targetVisualizer, target.id, target.modelIndex, target.name );

        {
            PROFILE_SCOPED( "Frame/Replay/PathVisualizer/RetainedTarget/BuildTree" );
            ReplayPathFutureContext futureContext;
            futureContext.visualizer = &targetVisualizer;
            futureContext.models = &models;
            futureContext.budgetStart = &visualizerStart;
            futureContext.rootId = target.id;
            futureContext.presentFrame = presentFrame;
            futureContext.budgetMilliseconds = REPLAY_PREDICTION_MAX_WORK_MILLISECONDS;
            futureContext.includeRagdollVisuals = m_replayRuntime.Prediction().ragdollVisualsEnabled;
            m_replayRuntime.Solver().ForEachSampleChronological( BuildReplayFutureNodes, &futureContext );
        }
        if ( ReplayPredictionBudgetExpired( visualizerStart, REPLAY_PREDICTION_MAX_WORK_MILLISECONDS ) )
        {
            return;
        }

        {
            PROFILE_SCOPED( "Frame/Replay/PathVisualizer/RetainedTarget/DrawRoot" );
            ReplayPathRootDrawContext rootDraw;
            rootDraw.tracer = &tracer;
            rootDraw.budgetStart = &visualizerStart;
            rootDraw.rootId = target.id;
            rootDraw.firstFrame = bounds.firstFrame;
            rootDraw.presentFrame = presentFrame;
            rootDraw.lastFrame = bounds.lastFrame;
            rootDraw.budgetMilliseconds = REPLAY_PREDICTION_MAX_WORK_MILLISECONDS;
            rootDraw.sampleStride = sampleStride;
            m_replayRuntime.Solver().ForEachSampleChronological( DrawReplayRootPath, &rootDraw );
        }
        if ( ReplayPredictionBudgetExpired( visualizerStart, REPLAY_PREDICTION_MAX_WORK_MILLISECONDS ) )
        {
            return;
        }

        ReplayPathChildDrawContext childDraw;
        childDraw.tracer = &tracer;
        childDraw.models = &models;
        childDraw.budgetStart = &visualizerStart;
        childDraw.presentFrame = presentFrame;
        childDraw.lastFrame = bounds.lastFrame;
        childDraw.budgetMilliseconds = REPLAY_PREDICTION_MAX_WORK_MILLISECONDS;
        childDraw.sampleStride = sampleStride;
        childDraw.nodeCount = (std::min)( targetVisualizer.futureNodes.size(), REPLAY_PATH_MAX_FUTURE_NODES );
        for ( std::size_t i = 0; i < childDraw.nodeCount; ++i )
        {
            childDraw.nodes[i].node = targetVisualizer.futureNodes[i];
        }
        if ( childDraw.nodeCount > 0 )
        {
            PROFILE_SCOPED( "Frame/Replay/PathVisualizer/RetainedTarget/DrawChildren" );
            m_replayRuntime.Solver().ForEachSampleChronological( DrawReplayChildPaths, &childDraw );
            AddReplayFutureContactMarkers( targetVisualizer,
                                           tracer,
                                           visualizerStart,
                                           REPLAY_PREDICTION_MAX_WORK_MILLISECONDS );
        }
        if ( ReplayPredictionBudgetExpired( visualizerStart, REPLAY_PREDICTION_MAX_WORK_MILLISECONDS ) )
        {
            return;
        }

        if ( target.id.value == m_replayRuntime.PathVisualizer().targetId.value )
        {
            m_replayRuntime.PathVisualizer().futureNodes = targetVisualizer.futureNodes;
        }

        {
            PROFILE_SCOPED( "Frame/Replay/PathVisualizer/RetainedTarget/DrawMarker" );
            if ( ReplayPredictionBudgetExpired( visualizerStart, REPLAY_PREDICTION_MAX_WORK_MILLISECONDS ) )
            {
                return;
            }

            int markerIndex = target.modelIndex;
            if ( markerIndex < 0 || markerIndex >= static_cast<int>( models.size() ) ||
                 models[static_cast<std::size_t>( markerIndex )].GetReplayBodyId() != target.id.value )
            {
                markerIndex = -1;
                for ( int i = 0; i < static_cast<int>( models.size() ); ++i )
                {
                    if ( models[static_cast<std::size_t>( i )].GetReplayBodyId() == target.id.value )
                    {
                        markerIndex = i;
                        target.modelIndex = i;
                        if ( target.id.value == m_replayRuntime.PathVisualizer().targetId.value )
                        {
                            m_replayRuntime.PathVisualizer().targetModelIndex = i;
                        }
                        break;
                    }
                }
            }
            if ( markerIndex >= 0 && markerIndex < static_cast<int>( models.size() ) )
            {
                tracer.AddReplayTargetMarker( models[static_cast<std::size_t>( markerIndex )] );
            }
        }
    }
}


void Run::RenderReplayCauseFocusOverlay( RunEditorTracer& tracer )
{
    if ( m_replayRuntime.Camera().focusKind == RunReplayCameraFocusKind::None )
    {
        return;
    }

    if ( m_replayRuntime.Camera().focusKind == RunReplayCameraFocusKind::Body )
    {
        const std::vector<GameModel>& models = m_cGameModelCollection.Models();
        for ( const GameModel& model : models )
        {
            if ( model.GetReplayBodyId() == m_replayRuntime.Camera().focusedId.value )
            {
                tracer.AddReplayTargetMarker( model );
                return;
            }
        }
    }

    if ( m_replayRuntime.Camera().focusKind == RunReplayCameraFocusKind::Manifold ||
         m_replayRuntime.Camera().focusKind == RunReplayCameraFocusKind::PredictionContact )
    {
        if ( m_replayRuntime.Camera().focusKind == RunReplayCameraFocusKind::Manifold )
        {
            const ReplaySolverFrameSample* sample = CurrentReplaySolverScrubSample();
            if ( sample )
            {
                const ReplaySolverBodySample* focusedBody =
                    FindReplayBodyById( *sample, m_replayRuntime.Camera().focusedId );
                const ReplaySolverBodySample* counterpartBody =
                    FindReplayBodyById( *sample, m_replayRuntime.Camera().counterpartId );
                if ( focusedBody )
                {
                    bool drewContact = false;
                    for ( const ReplaySolverPersistentContactSample& contact :
                          sample->worldSnapshot.persistentContacts )
                    {
                        if ( !ReplayContactHasModelIndex( contact, focusedBody->modelIndex ) )
                        {
                            continue;
                        }
                        const int otherModelIndex = ReplayContactOtherModelIndex( contact, focusedBody->modelIndex );
                        const bool terrain = contact.isTerrain || otherModelIndex < 0;
                        if ( m_replayRuntime.Camera().focusTerrain != terrain )
                        {
                            continue;
                        }
                        if ( !terrain && ( !counterpartBody || counterpartBody->modelIndex != otherModelIndex ) )
                        {
                            continue;
                        }
                        tracer.AddReplayContactMarker( ReplayContactPoint( *sample, contact ),
                                                       ReplayContactNormalForModel( contact, focusedBody->modelIndex ),
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
        tracer.AddReplayContactMarker( m_replayRuntime.Camera().targetPoint,
                                       m_replayRuntime.Camera().targetNormal,
                                       0.1f,
                                       0.95f,
                                       1.0f );
        return;
    }

    if ( m_replayRuntime.Camera().focusKind == RunReplayCameraFocusKind::SolverRow )
    {
        tracer.AddReplayContactMarker( m_replayRuntime.Camera().targetPoint,
                                       m_replayRuntime.Camera().targetNormal,
                                       0.2f,
                                       0.85f,
                                       1.0f );
        tracer.AddReplayImpulseVector( m_replayRuntime.Camera().targetPoint,
                                       m_replayRuntime.Camera().impulseVector,
                                       1.0f,
                                       0.32f,
                                       0.12f );
    }
}
