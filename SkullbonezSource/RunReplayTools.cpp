/*
File: SkullbonezSource/RunReplayTools.cpp
Purpose:
  Owns replay scrubber, cause tree, path visualization, prediction, and velocity editing tools.

Mental model:
  Input toggles replay modes.
  This file keeps retained solver samples, prediction previews, and replay overlays together.

Glossary:
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "RunInternal.h"
#include "EditorHullAssets.h"
#include "InputController.h"
#include "PhysicsMass.h"
#include "RuntimeFileWriter.h"
#include "WorkerPool.h"
#include "UI/UIInput.h"
#include "UI/UILayout.h"

#include <chrono>
#include <cfloat>
#include <cstddef>
#include <cstring>

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

namespace
{
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

ReplayBodyId ReplayPredictionBodyIdForModelIndex( const RunReplayPredictionFrame& frame, int modelIndex )
{
    ReplayBodyId id;
    if ( modelIndex < 0 )
    {
        return id;
    }

    for ( const RunReplayPredictionBodySample& body : frame.bodies )
    {
        if ( body.modelIndex == modelIndex )
        {
            return body.id;
        }
    }
    return id;
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
    ReplayBodyId rootId;
    ReplayFrameIndex presentFrame = 0;
};

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
                          ReplayBodyId id,
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
    node.firstFrame = firstFrame;
    node.contactPoint = contactPoint;
    node.contactNormal = contactNormal;
    node.depth = depth;
    context.visualizer->futureNodes.push_back( node );
}

void BuildReplayFutureNodes( const ReplaySolverFrameSample& sample, void* userData )
{
    ReplayPathFutureContext& context = *static_cast<ReplayPathFutureContext*>( userData );
    if ( !context.visualizer || sample.frameIndex < context.presentFrame )
    {
        return;
    }

    for ( const PhysicsDebugContact& contact : sample.worldSnapshot.debugContacts )
    {
        const ReplayBodyId idA = ReplayBodyIdForModelIndex( sample, contact.bodyA );
        const ReplayBodyId idB = ReplayBodyIdForModelIndex( sample, contact.bodyB );
        int depthA = -1;
        int depthB = -1;
        const bool activeA = TryGetReplayFutureDepth( context, idA, sample.frameIndex, depthA );
        const bool activeB = TryGetReplayFutureDepth( context, idB, sample.frameIndex, depthB );
        if ( activeA && !activeB )
        {
            AddReplayFutureNode( context, idA, idB, sample.frameIndex, contact.point, contact.normal, depthA + 1 );
        }
        else if ( activeB && !activeA )
        {
            AddReplayFutureNode( context,
                                 idB,
                                 idA,
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
    ReplayBodyId rootId;
    ReplayFrameIndex firstFrame = 0;
    ReplayFrameIndex presentFrame = 0;
    ReplayFrameIndex lastFrame = 0;
    std::size_t sampleOrdinal = 0;
    std::size_t sampleStride = 1;
    bool hasPastPrevious = false;
    bool hasFuturePrevious = false;
    Vector3 pastPrevious = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 futurePrevious = SkullbonezCore::Math::Vector::ZERO_VECTOR;
};

void DrawReplayRootPath( const ReplaySolverFrameSample& sample, void* userData )
{
    ReplayPathRootDrawContext& context = *static_cast<ReplayPathRootDrawContext*>( userData );
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
    std::array<ReplayPathChildDrawState, REPLAY_PATH_MAX_FUTURE_NODES> nodes = {};
    std::size_t nodeCount = 0;
    ReplayFrameIndex presentFrame = 0;
    ReplayFrameIndex lastFrame = 0;
    std::size_t sampleOrdinal = 0;
    std::size_t sampleStride = 1;
};

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

void DrawReplayChildPaths( const ReplaySolverFrameSample& sample, void* userData )
{
    ReplayPathChildDrawContext& context = *static_cast<ReplayPathChildDrawContext*>( userData );
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
        ReplayPathChildDrawState& drawState = context.nodes[i];
        const ReplaySolverBodySample* body = FindReplayBodyById( sample, drawState.node.id );
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

void AddReplayFutureContactMarkers( const RunReplayPathVisualizerState& visualizer, RunEditorTracer& tracer )
{
    for ( const RunReplayPathTraceNode& node : visualizer.futureNodes )
    {
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
    ReplayBodyId rootId;
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
                                    ReplayBodyId id,
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
    node.firstFrame = firstFrame;
    node.contactPoint = contactPoint;
    node.contactNormal = contactNormal;
    node.depth = depth;
    context.prediction->futureNodes.push_back( node );
}

void BuildReplayPredictionFutureNodes( const RunReplayPredictionFrame& frame, ReplayPredictionFutureContext& context )
{
    for ( const PhysicsDebugContact& contact : frame.debugContacts )
    {
        const ReplayBodyId idA = ReplayPredictionBodyIdForModelIndex( frame, contact.bodyA );
        const ReplayBodyId idB = ReplayPredictionBodyIdForModelIndex( frame, contact.bodyB );
        int depthA = -1;
        int depthB = -1;
        const bool activeA = TryGetReplayPredictionFutureDepth( context, idA, frame.frameIndex, depthA );
        const bool activeB = TryGetReplayPredictionFutureDepth( context, idB, frame.frameIndex, depthB );
        if ( activeA && !activeB )
        {
            AddReplayPredictionFutureNode( context,
                                           idA,
                                           idB,
                                           frame.frameIndex,
                                           contact.point,
                                           contact.normal,
                                           depthA + 1 );
        }
        else if ( activeB && !activeA )
        {
            AddReplayPredictionFutureNode( context,
                                           idB,
                                           idA,
                                           frame.frameIndex,
                                           contact.point,
                                           contact.normal * -1.0f,
                                           depthB + 1 );
        }
    }
}


} // namespace

void Run::SetReplaySimulationPaused( bool paused )
{
    if ( m_replayScrubber.simulationPaused == paused )
    {
        return;
    }

    PROFILE_SCOPED( "Frame/Replay/SimulationPause" );

    if ( paused )
    {
        EnterInteractiveSceneRun();
        m_replayScrubber.simulationPaused = true;
        UpdateReplayInspectionCamera();
        return;
    }

    m_replayScrubber.simulationPaused = false;
    UpdateReplayInspectionCamera();
}


void Run::EnterReplayInspectionCamera()
{
    if ( !m_systems.cameras )
    {
        return;
    }

    const bool enteringInspectionCamera = !m_replayScrubber.inspectionCameraActive;
    if ( !m_replayScrubber.inspectionCameraActive )
    {
        m_replayScrubber.inspectionRestoreFlyMode = m_camera.isFlyMode;
        m_replayScrubber.inspectionRestoreLauncherMode = m_camera.isLauncherMode;
        m_replayScrubber.inspectionRestoreCameraHash = m_systems.cameras->GetSelectedCameraName();

        const Vector3 eye = m_systems.cameras->GetRenderCameraTranslation();
        const Vector3 view = m_systems.cameras->GetRenderCameraView();
        m_systems.cameras->CancelTween();
        m_systems.cameras->SelectCamera( CAMERA_FREE, false );
        m_systems.cameras->SetPrimaryPosition( eye );
        m_systems.cameras->SetViewCoordinates( view );
        m_replayScrubber.inspectionCameraActive = true;
    }

    XZBounds unbounded;
    unbounded.m_xMin = -99999.9f;
    unbounded.m_xMax = 99999.9f;
    unbounded.m_zMin = -99999.9f;
    unbounded.m_zMax = 99999.9f;
    m_systems.cameras->SetCameraXZBounds( CAMERA_FREE, unbounded );
    m_camera.cameraTime = 0.0f;
    m_camera.isFlyMode = true;
    m_camera.isLauncherMode = false;
    if ( enteringInspectionCamera )
    {
        Input::SetSystemCursorVisible( true );
        InputController::ResetMouseLook( m_camera );
    }
}


void Run::ExitReplayInspectionCamera()
{
    if ( !m_replayScrubber.inspectionCameraActive )
    {
        return;
    }

    m_replayScrubber.inspectionCameraActive = false;
    m_camera.isLauncherMode = m_replayScrubber.inspectionRestoreLauncherMode;
    m_camera.isFlyMode = m_replayScrubber.inspectionRestoreFlyMode || m_camera.isLauncherMode;
    if ( m_systems.cameras )
    {
        m_systems.cameras->CancelTween();
        m_systems.cameras->SelectCamera( m_replayScrubber.inspectionRestoreCameraHash, false );
        if ( m_systems.terrain )
        {
            const uint32_t activeCam = m_systems.cameras->GetSelectedCameraName();
            if ( m_camera.isFlyMode )
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
    Input::SetSystemCursorVisible( true );
    InputController::ResetMouseLook( m_camera );
}


void Run::UpdateReplayInspectionCamera()
{
    if ( m_replayScrubber.paused || m_replayScrubber.simulationPaused )
    {
        EnterReplayInspectionCamera();
    }
    else
    {
        ExitReplayInspectionCamera();
    }
}


bool Run::TickReplayScrubberInput( HWND hwnd, bool uiBlocksMouse )
{
    PROFILE_SCOPED( "Frame/Replay/ScrubberInput" );
    m_replayScrubber.restoreConsumedThisFrame = false;
    const bool leftDown = Input::IsLeftMouseDown();
    const bool leftPressed = leftDown && !m_replayScrubber.leftWasDown;
    const bool leftReleased = !leftDown && m_replayScrubber.leftWasDown;
    m_replayScrubber.leftWasDown = leftDown;
    const bool restoreDown = Input::IsKeyDown( VK_RETURN );
    const bool restorePressed = restoreDown && !m_replayScrubber.restoreWasDown;
    m_replayScrubber.restoreWasDown = restoreDown;

    const bool scrubberAllowed = !m_editor.editorModeEnabled && m_UI.IsVisible() && m_UI.IsMinimized();
    const ReplayRecorderStats replayStats = m_replay.GetStats();
    const ReplayRecorderStats solverReplayStats = m_solverReplay.GetStats();
    const int screenW = WindowScreenWidth();
    const int screenH = WindowScreenHeight();
    if ( !scrubberAllowed || !replayStats.enabled || !solverReplayStats.enabled || replayStats.sampleCount < 2 ||
         solverReplayStats.sampleCount < 2 || screenW <= 0 || screenH <= 0 )
    {
        if ( m_replayScrubber.mouseCaptured )
        {
            UI::InputControl::EndMouseCapture();
        }
        ResetReplayScrubber();
        m_replayPrediction.checkboxHovered = false;
        m_replayPrediction.decreaseHovered = false;
        m_replayPrediction.increaseHovered = false;
        m_replayPrediction.horizonHovered = false;
        m_replayPrediction.horizonDragging = false;
        m_replayVelocityEdit.toggleHovered = false;
        m_replayScrubber.leftWasDown = leftDown;
        return false;
    }

    const POINT mouse = Input::GetClientMouseCoordinates();
    m_replayScrubber.mouseX = mouse.x;
    m_replayScrubber.mouseY = mouse.y;

    const UI::UIRect hotZone = ReplayScrubberHotZoneRect( screenW, screenH );
    const UI::UIRect panel = ReplayScrubberPanelRect( screenW, screenH );
    const UI::UIRect presentationTrack = ReplayScrubberTrackRect( screenW, screenH, RunReplayTrack::Presentation );
    const UI::UIRect solverTrack = ReplayScrubberTrackRect( screenW, screenH, RunReplayTrack::Solver );
    const UI::UIRect presentationSaveButton =
        ReplayScrubberSaveButtonRect( screenW, screenH, RunReplayTrack::Presentation );
    const UI::UIRect solverSaveButton = ReplayScrubberSaveButtonRect( screenW, screenH, RunReplayTrack::Solver );
    const UI::UIRect pauseButton = ReplayScrubberPauseButtonRect( screenW, screenH );
    const UI::UIRect velocityEditToggle = ReplayScrubberVelocityEditToggleRect( screenW, screenH );
    const UI::UIRect predictControl = ReplayScrubberPredictControlRect( screenW, screenH );
    const UI::UIRect predictToggle = ReplayScrubberPredictToggleRect( screenW, screenH );
    const UI::UIRect predictDecrease = ReplayScrubberPredictDecreaseRect( screenW, screenH );
    const UI::UIRect predictIncrease = ReplayScrubberPredictIncreaseRect( screenW, screenH );
    const UI::UIRect predictHorizon = ReplayScrubberPredictHorizonRect( screenW, screenH );
    const bool inHotZone = hotZone.Contains( mouse.x, mouse.y );
    const bool overPanel = panel.Contains( mouse.x, mouse.y );
    const bool overPresentationSaveButton = presentationSaveButton.Contains( mouse.x, mouse.y );
    const bool overSolverSaveButton = solverSaveButton.Contains( mouse.x, mouse.y );
    const bool overSaveButton = overPresentationSaveButton || overSolverSaveButton;
    const bool overPauseButton = pauseButton.Contains( mouse.x, mouse.y );
    const bool overVelocityEditToggle = velocityEditToggle.Contains( mouse.x, mouse.y );
    const bool overPredictControl = predictControl.Contains( mouse.x, mouse.y );
    const bool overPredictToggle = predictToggle.Contains( mouse.x, mouse.y );
    const bool overPredictUi = overPredictControl || overPredictToggle;
    const bool overPredictDecrease = predictDecrease.Contains( mouse.x, mouse.y );
    const bool overPredictIncrease = predictIncrease.Contains( mouse.x, mouse.y );
    const bool overPredictHorizon = predictHorizon.Contains( mouse.x, mouse.y ) ||
                                    ( predictControl.Contains( mouse.x, mouse.y ) && mouse.x >= predictHorizon.x &&
                                      mouse.x <= predictHorizon.x + predictHorizon.w );
    const bool overSolverRow = solverTrack.Contains( mouse.x, mouse.y ) || overSolverSaveButton ||
                               ( overPanel && mouse.y >= ( presentationTrack.y + solverTrack.y ) * 0.5f );
    const RunReplayTrack hoveredTrack = overSolverRow ? RunReplayTrack::Solver : RunReplayTrack::Presentation;
    const bool canTakeMouse = !uiBlocksMouse || m_replayScrubber.dragging || m_replayPrediction.horizonDragging;
    const double now = m_timers.simulationTimer.GetTotalTime();

    if ( inHotZone || overPanel || overSaveButton || overPauseButton || overVelocityEditToggle || overPredictUi ||
         m_replayScrubber.dragging || m_replayPrediction.horizonDragging || m_replayScrubber.paused ||
         m_replayScrubber.simulationPaused )
    {
        m_replayScrubber.visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
    }
    m_replayScrubber.saveHovered = overSaveButton && ( m_replayScrubber.visibleUntil >= now ||
                                                       m_replayScrubber.dragging || m_replayScrubber.paused );
    m_replayScrubber.saveHoveredTrack = hoveredTrack;
    const bool predictionControlVisible = m_replayScrubber.visibleUntil >= now || m_replayScrubber.dragging ||
                                          m_replayPrediction.horizonDragging || m_replayScrubber.paused ||
                                          m_replayScrubber.simulationPaused;
    m_replayScrubber.pauseHovered = overPauseButton && predictionControlVisible;
    m_replayVelocityEdit.toggleHovered = overVelocityEditToggle && predictionControlVisible;
    m_replayPrediction.checkboxHovered = overPredictToggle && predictionControlVisible;
    m_replayPrediction.decreaseHovered = overPredictDecrease && predictionControlVisible;
    m_replayPrediction.increaseHovered = overPredictIncrease && predictionControlVisible;
    m_replayPrediction.horizonHovered = overPredictHorizon && predictionControlVisible;

    bool consumesMouse =
        canTakeMouse && ( m_replayScrubber.dragging || m_replayPrediction.horizonDragging ||
                          ( m_replayScrubber.visibleUntil >= now && ( inHotZone || overSaveButton || overPauseButton ||
                                                                      overVelocityEditToggle || overPredictUi ) ) );

    if ( restorePressed && m_replayScrubber.paused && m_replayScrubber.activeTrack == RunReplayTrack::Solver )
    {
        EnterInteractiveSceneRun();
        char reason[96] = {};
        const ReplaySolverFrameSample* sample = CurrentReplaySolverScrubSample();
        const bool restored = sample && RestoreReplaySolverSampleAsLive( *sample, reason, sizeof( reason ) );
        m_replayScrubber.restoreConsumedThisFrame = true;
        m_replayScrubber.saveMessageTrack = RunReplayTrack::Solver;
        sprintf_s( m_replayScrubber.saveMessage,
                   sizeof( m_replayScrubber.saveMessage ),
                   restored ? "SOLVER RESTORED" : "RESTORE FAILED" );
        m_replayScrubber.saveMessageUntil = now + 2.5;
        m_replayScrubber.visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayScrubber.visible = true;
        fprintf( stderr,
                 "[replay] Solver restore %s%s%s\n",
                 restored ? "applied" : "failed",
                 reason[0] != '\0' ? ": " : "",
                 reason );
        return true;
    }

    auto changePredictionHorizon = [&]( float deltaSeconds )
    {
        EnterInteractiveSceneRun();
        const float nextSeconds = std::clamp( m_replayPrediction.horizonSeconds + deltaSeconds,
                                              REPLAY_PREDICTION_MIN_SECONDS,
                                              REPLAY_PREDICTION_MAX_SECONDS );
        if ( nextSeconds != m_replayPrediction.horizonSeconds )
        {
            m_replayPrediction.horizonSeconds = nextSeconds;
            MarkReplayPredictionDirty();
        }
        m_replayScrubber.visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayScrubber.visible = true;
        consumesMouse = true;
    };

    auto setPredictionHorizonFromMouse = [&]()
    {
        EnterInteractiveSceneRun();
        const float nextSeconds = ReplayPredictionHorizonFromMouse( mouse.x, predictHorizon );
        if ( nextSeconds != m_replayPrediction.horizonSeconds )
        {
            m_replayPrediction.horizonSeconds = nextSeconds;
            MarkReplayPredictionDirty();
        }
        m_replayScrubber.visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayScrubber.visible = true;
        consumesMouse = true;
    };

    if ( leftPressed && canTakeMouse && overPauseButton && m_replayScrubber.visibleUntil >= now )
    {
        SetReplaySimulationPaused( !m_replayScrubber.simulationPaused );
        m_replayScrubber.visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayScrubber.visible = true;
        consumesMouse = true;
    }
    else if ( leftPressed && canTakeMouse && overVelocityEditToggle && m_replayScrubber.visibleUntil >= now )
    {
        SetReplayVelocityEditEnabled( !m_replayVelocityEdit.enabled );
        m_replayScrubber.visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayScrubber.visible = true;
        consumesMouse = true;
    }
    else if ( leftPressed && canTakeMouse && overPredictHorizon && m_replayScrubber.visibleUntil >= now )
    {
        m_replayPrediction.horizonDragging = true;
        setPredictionHorizonFromMouse();
        if ( !m_replayScrubber.mouseCaptured )
        {
            UI::InputControl::BeginMouseCapture( hwnd );
            m_replayScrubber.mouseCaptured = true;
        }
    }
    else if ( leftPressed && canTakeMouse && overPredictDecrease && m_replayScrubber.visibleUntil >= now )
    {
        changePredictionHorizon( -REPLAY_PREDICTION_STEP_SECONDS );
    }
    else if ( leftPressed && canTakeMouse && overPredictIncrease && m_replayScrubber.visibleUntil >= now )
    {
        changePredictionHorizon( REPLAY_PREDICTION_STEP_SECONDS );
    }
    else if ( leftPressed && canTakeMouse && overPredictToggle && m_replayScrubber.visibleUntil >= now )
    {
        EnterInteractiveSceneRun();
        m_replayPrediction.enabled = !m_replayPrediction.enabled;
        m_replayPrediction.horizonSeconds = std::clamp( m_replayPrediction.horizonSeconds,
                                                        REPLAY_PREDICTION_MIN_SECONDS,
                                                        REPLAY_PREDICTION_MAX_SECONDS );
        if ( !m_replayPrediction.enabled )
        {
            ClearReplayPredictionCache();
        }
        MarkReplayPredictionDirty();
        m_replayScrubber.visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayScrubber.visible = true;
        consumesMouse = true;
    }
    else if ( leftPressed && canTakeMouse && overSaveButton && m_replayScrubber.visibleUntil >= now )
    {
        EnterInteractiveSceneRun();
        m_replayScrubber.activeTrack = hoveredTrack;
        ReplayScrubberSyncActivePosition( m_replayScrubber );
        SaveReplayBufferFromScrubber( hoveredTrack );
        consumesMouse = true;
    }
    else if ( leftPressed && canTakeMouse && !overPauseButton && !overPredictUi &&
              ( inHotZone || overPanel || m_replayScrubber.paused ) )
    {
        EnterInteractiveSceneRun();
        m_replayScrubber.activeTrack = hoveredTrack;
        ReplayScrubberSyncActivePosition( m_replayScrubber );
        m_replayScrubber.dragging = true;
        if ( !m_replayScrubber.mouseCaptured )
        {
            UI::InputControl::BeginMouseCapture( hwnd );
            m_replayScrubber.mouseCaptured = true;
        }
    }

    if ( m_replayScrubber.dragging )
    {
        ReplayScrubberSetTrackPosition(
            m_replayScrubber,
            m_replayScrubber.activeTrack,
            ReplayScrubberPositionFromMouse( mouse.x, screenW, screenH, m_replayScrubber.activeTrack ) );
        if ( m_replayScrubber.position >= REPLAY_SCRUBBER_LIVE_THRESHOLD )
        {
            ReplayScrubberSetTrackPosition( m_replayScrubber, m_replayScrubber.activeTrack, 1.0f );
            m_replayScrubber.paused = false;
        }
        else
        {
            m_replayScrubber.paused = true;
        }

        if ( leftReleased )
        {
            m_replayScrubber.dragging = false;
            if ( m_replayScrubber.mouseCaptured )
            {
                UI::InputControl::EndMouseCapture();
                m_replayScrubber.mouseCaptured = false;
            }
        }
    }
    else if ( m_replayPrediction.horizonDragging )
    {
        setPredictionHorizonFromMouse();
        if ( leftReleased )
        {
            m_replayPrediction.horizonDragging = false;
            if ( m_replayScrubber.mouseCaptured )
            {
                UI::InputControl::EndMouseCapture();
                m_replayScrubber.mouseCaptured = false;
            }
        }
    }
    else if ( !m_replayScrubber.paused )
    {
        ReplayScrubberSetAllTrackPositions( m_replayScrubber, 1.0f );
    }

    m_replayScrubber.visible = m_replayScrubber.dragging || m_replayPrediction.horizonDragging ||
                               m_replayScrubber.paused || m_replayScrubber.simulationPaused ||
                               m_replayScrubber.visibleUntil >= now;
    UpdateReplayInspectionCamera();
    return consumesMouse;
}


void Run::ClearReplayPathVisualizer()
{
    m_replayPathVisualizer.hasTarget = false;
    m_replayPathVisualizer.targetId = ReplayBodyId{};
    m_replayPathVisualizer.targetModelIndex = -1;
    m_replayPathVisualizer.targetName[0] = '\0';
    m_replayPathVisualizer.futureNodes.clear();
    m_replayPathVisualizer.targets.clear();
    ClearReplayPredictionCache();
    MarkReplayPredictionDirty();
}


void Run::MarkReplayPredictionDirty()
{
    CancelReplayPredictionJob( true );
    m_replayPrediction.dirty = true;
}


void Run::ClearReplayPredictionCache()
{
    CancelReplayPredictionJob( true );
    m_replayPrediction.targetId = ReplayBodyId{};
    m_replayPrediction.sourceFrameIndex = 0;
    m_replayPrediction.sourceSolverHash = 0;
    m_replayPrediction.lastBuildTime = 0.0;
}


bool Run::BuildReplayCauseTreeRows()
{
    PROFILE_SCOPED( "Frame/Replay/CauseTree/BuildRows" );
    m_replayCauseTree.rowCount = 0;

    if ( !m_replayPathVisualizer.hasTarget || m_replayPathVisualizer.targetId.value == 0 )
    {
        return false;
    }

    const bool usePrediction = m_replayPrediction.enabled && m_replayPrediction.frames.size() >= 2 &&
                               m_replayPrediction.targetId.value == m_replayPathVisualizer.targetId.value;
    const std::vector<RunReplayPathTraceNode>& nodes =
        usePrediction ? m_replayPrediction.futureNodes : m_replayPathVisualizer.futureNodes;
    const std::vector<GameModel>& models = m_cGameModelCollection.Models();

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

    auto addRow = [&]( ReplayBodyId id,
                       ReplayBodyId parentId,
                       ReplayFrameIndex firstFrame,
                       int depth,
                       int modelIndex,
                       const char* fallbackName ) -> bool
    {
        if ( id.value == 0 || m_replayCauseTree.rowCount >= REPLAY_CAUSE_TREE_MAX_ROWS )
        {
            return false;
        }

        RunReplayCauseTreeRow& row = m_replayCauseTree.rows[static_cast<std::size_t>( m_replayCauseTree.rowCount++ )];
        row = RunReplayCauseTreeRow{};
        row.id = id;
        row.parentId = parentId;
        row.firstFrame = firstFrame;
        row.depth = depth;
        row.modelIndex = modelIndex >= 0 ? modelIndex : modelIndexForId( id );
        row.prediction = usePrediction;
        writeName( id, row.modelIndex, fallbackName, row.name, sizeof( row.name ) );
        return true;
    };

    addRow( m_replayPathVisualizer.targetId,
            ReplayBodyId{},
            0,
            0,
            m_replayPathVisualizer.targetModelIndex,
            m_replayPathVisualizer.targetName );

    auto addChildren = [&]( auto&& self, ReplayBodyId parentId, int fallbackDepth ) -> void
    {
        for ( const RunReplayPathTraceNode& node : nodes )
        {
            if ( node.parentId.value != parentId.value )
            {
                continue;
            }
            const int depth = node.depth > 0 ? node.depth : fallbackDepth;
            if ( addRow( node.id, parentId, node.firstFrame, depth, modelIndexForId( node.id ), nullptr ) )
            {
                self( self, node.id, depth + 1 );
            }
        }
    };
    addChildren( addChildren, m_replayPathVisualizer.targetId, 1 );

    return m_replayCauseTree.rowCount > 0;
}


bool Run::TryResolveReplayCauseTreeBodyPosition( ReplayBodyId id, Vector3& outPosition ) const
{
    if ( id.value == 0 )
    {
        return false;
    }

    if ( m_replayPrediction.enabled && !m_replayPrediction.frames.empty() &&
         m_replayPrediction.targetId.value == m_replayPathVisualizer.targetId.value )
    {
        if ( const RunReplayPredictionBodySample* body =
                 FindReplayPredictionBodyById( m_replayPrediction.frames.front(), id ) )
        {
            outPosition = body->position;
            return true;
        }
    }

    if ( const ReplaySolverFrameSample* sample = CurrentReplaySolverScrubSample() )
    {
        if ( const ReplaySolverBodySample* body = FindReplayBodyById( *sample, id ) )
        {
            outPosition = body->position;
            return true;
        }
    }

    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    for ( const GameModel& model : models )
    {
        if ( model.GetReplayBodyId() == id.value )
        {
            outPosition = model.GetPosition();
            return true;
        }
    }
    return false;
}


bool Run::FocusReplayCauseTreeBody( ReplayBodyId id )
{
    PROFILE_SCOPED( "Frame/Replay/CauseTree/Focus" );
    Vector3 targetPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    if ( !TryResolveReplayCauseTreeBodyPosition( id, targetPosition ) )
    {
        return false;
    }

    EnterInteractiveSceneRun();
    if ( !m_replayScrubber.simulationPaused )
    {
        SetReplaySimulationPaused( true );
    }
    if ( m_systems.cameras )
    {
        m_systems.cameras->CancelTween();
        m_systems.cameras->SetViewCoordinates( targetPosition );
        m_systems.cameras->ResetRelativity();
    }
    m_replayCauseTree.focusedId = id;
    InputController::ResetMouseLook( m_camera );
    Input::SetSystemCursorVisible( true );
    return true;
}


bool Run::TickReplayCauseTreeInput( bool uiBlocksMouse )
{
    PROFILE_SCOPED( "Frame/Replay/CauseTree/Input" );
    const bool leftDown = Input::IsLeftMouseDown();
    const bool leftPressed = leftDown && !m_replayCauseTree.leftWasDown;
    m_replayCauseTree.leftWasDown = leftDown;
    m_replayCauseTree.hoveredRow = -1;

    if ( uiBlocksMouse || m_editor.editorModeEnabled || !m_UI.IsVisible() || !m_UI.IsMinimized() ||
         WindowScreenWidth() <= 0 || WindowScreenHeight() <= 0 || !BuildReplayCauseTreeRows() )
    {
        return false;
    }

    const POINT mouse = Input::GetClientMouseCoordinates();
    const UI::UIRect panel = ReplayCauseTreePanelRect( WindowScreenWidth(), WindowScreenHeight() );
    if ( !panel.Contains( mouse.x, mouse.y ) )
    {
        return false;
    }

    const int visibleRows = (std::min)( m_replayCauseTree.rowCount, ReplayCauseTreeVisibleRowCapacity( panel ) );
    for ( int rowIndex = 0; rowIndex < visibleRows; ++rowIndex )
    {
        const UI::UIRect rowRect = ReplayCauseTreeRowRect( panel, rowIndex );
        if ( rowRect.Contains( mouse.x, mouse.y ) )
        {
            m_replayCauseTree.hoveredRow = rowIndex;
            if ( leftPressed )
            {
                FocusReplayCauseTreeBody( m_replayCauseTree.rows[static_cast<std::size_t>( rowIndex )].id );
            }
            break;
        }
    }

    return true;
}


void Run::SetReplayVelocityEditEnabled( bool enabled )
{
    if ( m_replayVelocityEdit.enabled == enabled )
    {
        return;
    }

    PROFILE_SCOPED( "Frame/Replay/VelocityEdit/Toggle" );
    m_replayVelocityEdit.enabled = enabled;
    m_replayVelocityEdit.hotLinearAxis = -1;
    m_replayVelocityEdit.hotAngularAxis = -1;
    m_replayVelocityEdit.activeAxis = -1;
    m_replayVelocityEdit.dragging = false;
    m_replayVelocityEdit.draggingAngular = false;
    if ( m_replayVelocityEdit.mouseCaptured )
    {
        UI::InputControl::EndMouseCapture();
        m_replayVelocityEdit.mouseCaptured = false;
    }

    if ( enabled )
    {
        EnterInteractiveSceneRun();
        SetReplaySimulationPaused( true );
        m_replayPrediction.enabled = true;
        m_replayPrediction.horizonSeconds = std::clamp( m_replayPrediction.horizonSeconds,
                                                        REPLAY_PREDICTION_MIN_SECONDS,
                                                        REPLAY_PREDICTION_MAX_SECONDS );
        MarkReplayPredictionDirty();
        m_replayScrubber.visibleUntil = m_timers.simulationTimer.GetTotalTime() + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayScrubber.visible = true;
    }
}


int Run::ResolveReplayVelocityEditModelIndex() const
{
    if ( !m_replayPathVisualizer.hasTarget || m_replayPathVisualizer.targetId.value == 0 )
    {
        return -1;
    }

    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    const int cachedIndex = m_replayPathVisualizer.targetModelIndex;
    if ( cachedIndex >= 0 && cachedIndex < static_cast<int>( models.size() ) &&
         models[static_cast<std::size_t>( cachedIndex )].GetReplayBodyId() == m_replayPathVisualizer.targetId.value )
    {
        return cachedIndex;
    }

    for ( int i = 0; i < static_cast<int>( models.size() ); ++i )
    {
        if ( models[static_cast<std::size_t>( i )].GetReplayBodyId() == m_replayPathVisualizer.targetId.value )
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
        m_cGameModelCollection.WakeModel( modelIndex );
    }
    m_cGameModelCollection.InvalidatePhysicsStreams();
    MarkReplayPredictionDirty();
    m_replayScrubber.visibleUntil = m_timers.simulationTimer.GetTotalTime() + REPLAY_SCRUBBER_VISIBLE_SECONDS;
    m_replayScrubber.visible = true;
}


void Run::ApplyReplayVelocityEditDrag( const Vector3& rayOrigin, const Vector3& rayDirection )
{
    const int modelIndex = ResolveReplayVelocityEditModelIndex();
    if ( modelIndex < 0 || modelIndex >= m_cGameModelCollection.GetModelCount() || m_replayVelocityEdit.activeAxis < 0 )
    {
        m_replayVelocityEdit.dragging = false;
        m_replayVelocityEdit.activeAxis = -1;
        return;
    }

    Vector3 linearVelocity = m_replayVelocityEdit.dragStartLinearVelocity;
    Vector3 angularVelocity = m_replayVelocityEdit.dragStartAngularVelocity;
    if ( m_replayVelocityEdit.draggingAngular )
    {
        float currentAngle = 0.0f;
        if ( !TryReplayVelocityAngularRayAngle( m_replayVelocityEdit.activeAxis,
                                                rayOrigin,
                                                rayDirection,
                                                currentAngle ) )
        {
            return;
        }
        const float angleDelta = WrapEditorAngleDelta( currentAngle - m_replayVelocityEdit.dragStartAngle );
        const float component = ReplayVelocityAxisComponent( m_replayVelocityEdit.dragStartAngularVelocity,
                                                             m_replayVelocityEdit.activeAxis ) +
                                angleDelta * ( REPLAY_VELOCITY_EDIT_ANGULAR_MAX / _PI );
        ReplayVelocitySetAxisComponent(
            angularVelocity,
            m_replayVelocityEdit.activeAxis,
            std::clamp( component, -REPLAY_VELOCITY_EDIT_ANGULAR_MAX, REPLAY_VELOCITY_EDIT_ANGULAR_MAX ) );
    }
    else
    {
        float axisT = 0.0f;
        if ( !TryReplayVelocityAxisRayParameter( m_replayVelocityEdit.activeAxis, rayOrigin, rayDirection, axisT ) )
        {
            return;
        }
        const float component = ReplayVelocityAxisComponent( m_replayVelocityEdit.dragStartLinearVelocity,
                                                             m_replayVelocityEdit.activeAxis ) +
                                ( axisT - m_replayVelocityEdit.dragStartAxisT ) * ReplayVelocityLinearUnitsPerWorld();
        ReplayVelocitySetAxisComponent(
            linearVelocity,
            m_replayVelocityEdit.activeAxis,
            std::clamp( component, -REPLAY_VELOCITY_EDIT_LINEAR_MAX, REPLAY_VELOCITY_EDIT_LINEAR_MAX ) );
    }

    ApplyReplayVelocityEditToModel( modelIndex, linearVelocity, angularVelocity );
}


bool Run::TickReplayVelocityEditInput( HWND hwnd, bool uiBlocksMouse )
{
    PROFILE_SCOPED( "Frame/Replay/VelocityEdit/Input" );
    const bool leftDown = Input::IsLeftMouseDown();
    const bool leftPressed = leftDown && !m_replayVelocityEdit.leftWasDown;
    const bool leftReleased = !leftDown && m_replayVelocityEdit.leftWasDown;
    m_replayVelocityEdit.leftWasDown = leftDown;

    if ( !m_replayVelocityEdit.enabled || m_editor.editorModeEnabled || !SceneState().isScenePhysics ||
         WindowScreenWidth() <= 0 || WindowScreenHeight() <= 0 )
    {
        m_replayVelocityEdit.hotLinearAxis = -1;
        m_replayVelocityEdit.hotAngularAxis = -1;
        if ( m_replayVelocityEdit.mouseCaptured && !leftDown )
        {
            UI::InputControl::EndMouseCapture();
            m_replayVelocityEdit.mouseCaptured = false;
        }
        return false;
    }

    Vector3 rayOrigin;
    Vector3 rayDirection;
    if ( !TryBuildMouseWorldRay( rayOrigin, rayDirection ) )
    {
        return m_replayVelocityEdit.dragging;
    }

    if ( m_replayVelocityEdit.dragging )
    {
        if ( leftDown && !uiBlocksMouse )
        {
            ApplyReplayVelocityEditDrag( rayOrigin, rayDirection );
        }
        if ( leftReleased || !leftDown )
        {
            m_replayVelocityEdit.dragging = false;
            m_replayVelocityEdit.draggingAngular = false;
            m_replayVelocityEdit.activeAxis = -1;
            if ( m_replayVelocityEdit.mouseCaptured )
            {
                UI::InputControl::EndMouseCapture();
                m_replayVelocityEdit.mouseCaptured = false;
            }
        }
        return true;
    }

    m_replayVelocityEdit.hotAngularAxis = uiBlocksMouse ? -1 : HitReplayVelocityAngularAxis( rayOrigin, rayDirection );
    m_replayVelocityEdit.hotLinearAxis = ( uiBlocksMouse || m_replayVelocityEdit.hotAngularAxis >= 0 )
                                             ? -1
                                             : HitReplayVelocityLinearAxis( rayOrigin, rayDirection );

    if ( !uiBlocksMouse && leftPressed )
    {
        const int modelIndex = ResolveReplayVelocityEditModelIndex();
        if ( modelIndex >= 0 && modelIndex < m_cGameModelCollection.GetModelCount() )
        {
            const GameModel& model = m_cGameModelCollection.Models()[static_cast<std::size_t>( modelIndex )];
            if ( m_replayVelocityEdit.hotAngularAxis >= 0 )
            {
                float startAngle = 0.0f;
                if ( TryReplayVelocityAngularRayAngle( m_replayVelocityEdit.hotAngularAxis,
                                                       rayOrigin,
                                                       rayDirection,
                                                       startAngle ) )
                {
                    EnterInteractiveSceneRun();
                    SetReplaySimulationPaused( true );
                    m_replayPrediction.enabled = true;
                    m_replayVelocityEdit.dragging = true;
                    m_replayVelocityEdit.draggingAngular = true;
                    m_replayVelocityEdit.activeAxis = m_replayVelocityEdit.hotAngularAxis;
                    m_replayVelocityEdit.dragStartAngle = startAngle;
                    m_replayVelocityEdit.dragStartLinearVelocity = model.GetVelocity();
                    m_replayVelocityEdit.dragStartAngularVelocity = model.GetAngularVelocity();
                    if ( !m_replayVelocityEdit.mouseCaptured )
                    {
                        UI::InputControl::BeginMouseCapture( hwnd );
                        m_replayVelocityEdit.mouseCaptured = true;
                    }
                    return true;
                }
            }
            else if ( m_replayVelocityEdit.hotLinearAxis >= 0 )
            {
                float axisT = 0.0f;
                if ( TryReplayVelocityAxisRayParameter( m_replayVelocityEdit.hotLinearAxis,
                                                        rayOrigin,
                                                        rayDirection,
                                                        axisT ) )
                {
                    EnterInteractiveSceneRun();
                    SetReplaySimulationPaused( true );
                    m_replayPrediction.enabled = true;
                    m_replayVelocityEdit.dragging = true;
                    m_replayVelocityEdit.draggingAngular = false;
                    m_replayVelocityEdit.activeAxis = m_replayVelocityEdit.hotLinearAxis;
                    m_replayVelocityEdit.dragStartAxisT = axisT;
                    m_replayVelocityEdit.dragStartLinearVelocity = model.GetVelocity();
                    m_replayVelocityEdit.dragStartAngularVelocity = model.GetAngularVelocity();
                    if ( !m_replayVelocityEdit.mouseCaptured )
                    {
                        UI::InputControl::BeginMouseCapture( hwnd );
                        m_replayVelocityEdit.mouseCaptured = true;
                    }
                    return true;
                }
            }
        }
    }

    return m_replayVelocityEdit.hotLinearAxis >= 0 || m_replayVelocityEdit.hotAngularAxis >= 0;
}


void Run::RenderReplayVelocityEditOverlay( RunEditorTracer& tracer )
{
    PROFILE_SCOPED( "Frame/Replay/VelocityEdit/Overlay" );
    if ( !m_replayVelocityEdit.enabled || m_editor.editorModeEnabled )
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
                                   m_replayVelocityEdit.hotLinearAxis,
                                   m_replayVelocityEdit.hotAngularAxis,
                                   m_replayVelocityEdit.activeAxis,
                                   m_replayVelocityEdit.draggingAngular );
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
    else if ( TryPickEditorModel( rayOrigin, rayDirection, pickedIndex ) && pickedIndex >= 0 &&
              pickedIndex < m_cGameModelCollection.GetModelCount() )
    {
        const GameModel& model = models[static_cast<std::size_t>( pickedIndex )];
        pickedId.value = model.GetReplayBodyId();
        const char* modelName = model.GetName();
        if ( modelName && modelName[0] != '\0' )
        {
            strncpy_s( pickedName, sizeof( pickedName ), modelName, _TRUNCATE );
        }
    }

    if ( pickedId.value != 0 )
    {
        if ( !additive )
        {
            m_replayPathVisualizer.targets.clear();
        }

        RunReplayPathTarget* target = FindReplayPathTarget( m_replayPathVisualizer, pickedId );
        if ( !target )
        {
            if ( m_replayPathVisualizer.targets.size() >= REPLAY_PATH_MAX_ROOT_TARGETS )
            {
                m_replayPathVisualizer.targets.erase( m_replayPathVisualizer.targets.begin() );
            }
            RunReplayPathTarget nextTarget;
            nextTarget.id = pickedId;
            m_replayPathVisualizer.targets.push_back( nextTarget );
            target = &m_replayPathVisualizer.targets.back();
        }

        target->modelIndex = pickedIndex;
        target->name[0] = '\0';
        if ( pickedName[0] != '\0' )
        {
            strncpy_s( target->name, sizeof( target->name ), pickedName, _TRUNCATE );
        }
        ApplyPrimaryReplayPathTarget( m_replayPathVisualizer, pickedId, pickedIndex, target->name );
        m_replayPathVisualizer.futureNodes.clear();
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
    m_replayPrediction.building = false;
    m_replayPrediction.complete = false;
    m_replayPrediction.targetModelIndex = -1;
    m_replayPrediction.nextTick = 1;
    m_replayPrediction.targetTickCount = 0;
    m_replayPrediction.predictionBodies.clear();
    m_replayPrediction.liveRestoreBodies.clear();
    m_replayPrediction.predictionWorld = ReplaySolverWorldSnapshot();
    m_replayPrediction.liveRestoreWorld = ReplaySolverWorldSnapshot();
    if ( clearSamples )
    {
        m_replayPrediction.frames.clear();
        m_replayPrediction.futureNodes.clear();
    }
}


bool Run::CaptureReplayPredictionBodyState( std::vector<RunReplayPredictionBodyBackup>& outBodies )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/CaptureBodyState" );
    std::vector<GameModel>& models = m_cGameModelCollection.PhysicsModels();
    outBodies.clear();
    outBodies.reserve( models.size() );
    for ( int i = 0; i < static_cast<int>( models.size() ); ++i )
    {
        GameModel& model = models[static_cast<std::size_t>( i )];
        RunReplayPredictionBodyBackup backup;
        backup.id.value = model.GetReplayBodyId();
        backup.modelIndex = i;
        backup.position = model.GetPosition();
        backup.orientation = model.GetOrientation();
        backup.linearVelocity = model.GetVelocity();
        backup.angularVelocity = model.GetAngularVelocity();
        backup.fixedContactHighlightSeconds = model.GetFixedContactHighlightSeconds();
        backup.fixed = model.IsFixed();
        outBodies.push_back( backup );
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
    RunReplayPredictionFrame frame;
    frame.frameIndex = frameIndex;
    frame.bodies.reserve( models.size() );
    for ( int i = 0; i < static_cast<int>( models.size() ); ++i )
    {
        GameModel& model = models[static_cast<std::size_t>( i )];
        RunReplayPredictionBodySample body;
        body.id.value = model.GetReplayBodyId();
        body.modelIndex = i;
        body.position = model.GetPosition();
        frame.bodies.push_back( body );
    }
    frame.debugContacts = m_cGameModelCollection.GetPhysicsDebugContacts();
    m_replayPrediction.frames.push_back( std::move( frame ) );
}


bool Run::BeginReplayPredictionJob( ReplayFrameIndex sourceFrameIndex, uint64_t sourceSolverHash )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/BeginJob" );
    CancelReplayPredictionJob( true );
    m_replayPrediction.targetId = m_replayPathVisualizer.targetId;
    m_replayPrediction.dirty = false;

    if ( !m_replayPrediction.enabled || !m_replayPathVisualizer.hasTarget ||
         m_replayPathVisualizer.targetId.value == 0 || !SceneState().isScenePhysics )
    {
        return false;
    }

    m_replayPrediction.sourceFrameIndex = sourceFrameIndex;
    m_replayPrediction.sourceSolverHash = sourceSolverHash;
    m_replayPrediction.lastBuildTime = m_timers.simulationTimer.GetTotalTime();

    std::vector<GameModel>& models = m_cGameModelCollection.PhysicsModels();
    int targetIndex = -1;
    for ( int i = 0; i < static_cast<int>( models.size() ); ++i )
    {
        if ( models[static_cast<std::size_t>( i )].GetReplayBodyId() == m_replayPathVisualizer.targetId.value )
        {
            targetIndex = i;
            break;
        }
    }
    if ( targetIndex < 0 )
    {
        return false;
    }
    m_replayPrediction.targetModelIndex = targetIndex;
    m_replayPathVisualizer.targetModelIndex = targetIndex;

    m_replayPrediction.horizonSeconds =
        std::clamp( m_replayPrediction.horizonSeconds, REPLAY_PREDICTION_MIN_SECONDS, REPLAY_PREDICTION_MAX_SECONDS );
    const int predictionTicks =
        (std::max)( 1, static_cast<int>( std::ceil( m_replayPrediction.horizonSeconds / PHYSICS_FIXED_DT ) ) );
    m_replayPrediction.targetTickCount = predictionTicks;
    m_replayPrediction.nextTick = 1;
    m_replayPrediction.frames.reserve( static_cast<std::size_t>( predictionTicks + 1 ) );

    if ( !CaptureReplayPredictionBodyState( m_replayPrediction.predictionBodies ) )
    {
        CancelReplayPredictionJob( true );
        return false;
    }

    m_cGameModelCollection.CaptureReplaySolverWorldSnapshot( m_replayPrediction.predictionWorld );
    CaptureReplayPredictionFrame( 0 );
    m_replayPrediction.building = true;

    return !m_replayPrediction.frames.empty();
}


bool Run::StepReplayPredictionJob( double budgetMilliseconds )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/Slice" );
    if ( !m_replayPrediction.building )
    {
        return m_replayPrediction.complete;
    }

    const auto sliceStart = std::chrono::steady_clock::now();
    if ( !CaptureReplayPredictionBodyState( m_replayPrediction.liveRestoreBodies ) )
    {
        CancelReplayPredictionJob( true );
        m_replayPrediction.dirty = true;
        return false;
    }
    m_cGameModelCollection.CaptureReplaySolverWorldSnapshot( m_replayPrediction.liveRestoreWorld );

#ifdef _DEBUG
    const bool previousDiagnosticsSuppressed = m_cGameModelCollection.SetPhysicsDiagnosticsSuppressed( true );
#endif

    bool jobApplied = false;
    bool jobStateCaptured = false;
    bool progressed = false;

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/ApplyJobState" );
        jobApplied = ApplyReplayPredictionBodyState( m_replayPrediction.predictionBodies ) &&
                     m_cGameModelCollection.RestoreReplaySolverWorldSnapshot( m_replayPrediction.predictionWorld );
        m_cGameModelCollection.InvalidatePhysicsStreams();
    }

    if ( jobApplied )
    {
        {
            PROFILE_SCOPED( "Frame/Replay/Prediction/Steps" );
            while ( m_replayPrediction.nextTick <= m_replayPrediction.targetTickCount )
            {
                {
                    PROFILE_SCOPED( "Frame/Replay/Prediction/StepPhysics" );
                    m_cGameModelCollection.RunPhysics( PHYSICS_FIXED_DT );
                }
                CaptureReplayPredictionFrame( static_cast<ReplayFrameIndex>( m_replayPrediction.nextTick ) );
                ++m_replayPrediction.nextTick;
                progressed = true;

                const double elapsedMilliseconds =
                    std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - sliceStart ).count();
                if ( elapsedMilliseconds >= budgetMilliseconds )
                {
                    break;
                }
            }
        }

        {
            PROFILE_SCOPED( "Frame/Replay/Prediction/CaptureJobState" );
            jobStateCaptured = CaptureReplayPredictionBodyState( m_replayPrediction.predictionBodies );
            if ( jobStateCaptured )
            {
                m_cGameModelCollection.CaptureReplaySolverWorldSnapshot( m_replayPrediction.predictionWorld );
            }
        }
    }

#ifdef _DEBUG
    m_cGameModelCollection.SetPhysicsDiagnosticsSuppressed( previousDiagnosticsSuppressed );
#endif

    bool liveRestored = false;
    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/RestoreLive" );
        liveRestored = ApplyReplayPredictionBodyState( m_replayPrediction.liveRestoreBodies ) &&
                       m_cGameModelCollection.RestoreReplaySolverWorldSnapshot( m_replayPrediction.liveRestoreWorld );
        m_cGameModelCollection.InvalidatePhysicsStreams();
    }

    if ( !jobApplied || !jobStateCaptured || !liveRestored )
    {
        CancelReplayPredictionJob( true );
        m_replayPrediction.dirty = true;
        return false;
    }

    if ( m_replayPrediction.nextTick > m_replayPrediction.targetTickCount )
    {
        m_replayPrediction.building = false;
        m_replayPrediction.complete = true;
        m_replayPrediction.lastBuildTime = m_timers.simulationTimer.GetTotalTime();
    }

    return progressed || m_replayPrediction.complete;
}


bool Run::BuildReplayFocusModelMask()
{
    PROFILE_SCOPED( "Frame/Replay/FocusMask" );
    const int modelCount = m_cGameModelCollection.GetModelCount();
    if ( !m_replayPathVisualizer.hasTarget || m_replayPathVisualizer.targetId.value == 0 || modelCount <= 0 )
    {
        m_replayFocusModelMask.clear();
        return false;
    }

    m_replayFocusModelMask.assign( static_cast<std::size_t>( modelCount ), 0 );
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
            uint8_t& mask = m_replayFocusModelMask[static_cast<std::size_t>( resolvedIndex )];
            if ( mask == 0 )
            {
                mask = 1;
                ++markedCount;
            }
        }
    };

    if ( m_replayPathVisualizer.targets.empty() )
    {
        markByReplayId( m_replayPathVisualizer.targetId, m_replayPathVisualizer.targetModelIndex );
    }
    else
    {
        for ( const RunReplayPathTarget& target : m_replayPathVisualizer.targets )
        {
            markByReplayId( target.id, target.modelIndex );
        }
    }

    const std::vector<RunReplayPathTraceNode>& futureNodes =
        m_replayPrediction.enabled ? m_replayPrediction.futureNodes : m_replayPathVisualizer.futureNodes;
    for ( const RunReplayPathTraceNode& node : futureNodes )
    {
        markByReplayId( node.id, -1 );
    }

    if ( markedCount <= 0 || markedCount >= modelCount )
    {
        m_replayFocusModelMask.clear();
        return false;
    }
    return true;
}


void Run::RenderReplayPredictionVisualizer( RunEditorTracer& tracer )
{
    PROFILE_SCOPED( "Frame/Replay/PathVisualizer/Prediction" );
    if ( !m_replayPrediction.enabled || !m_replayPathVisualizer.hasTarget ||
         m_replayPathVisualizer.targetId.value == 0 )
    {
        if ( m_replayPrediction.building )
        {
            CancelReplayPredictionJob( true );
        }
        return;
    }

    const ReplaySolverFrameSample* latest = m_solverReplay.LatestSample();
    const ReplayFrameIndex latestFrame = latest ? latest->frameIndex : 0;
    const uint64_t latestHash = latest ? latest->solverHash : 0;
    const double now = m_timers.simulationTimer.GetTotalTime();
    const bool sourceChanged = m_replayPrediction.targetId.value != m_replayPathVisualizer.targetId.value ||
                               m_replayPrediction.sourceFrameIndex != latestFrame ||
                               m_replayPrediction.sourceSolverHash != latestHash;
    const bool refreshDue = ( now - m_replayPrediction.lastBuildTime ) >= REPLAY_PREDICTION_REFRESH_SECONDS;
    const bool allowAutomaticRefresh = !m_replayScrubber.simulationPaused;
    if ( m_replayPrediction.dirty ||
         ( allowAutomaticRefresh && !m_replayPrediction.building && sourceChanged && refreshDue ) )
    {
        BeginReplayPredictionJob( latestFrame, latestHash );
    }
    if ( m_replayPrediction.building )
    {
        StepReplayPredictionJob( REPLAY_PREDICTION_MAX_WORK_MILLISECONDS );
    }

    if ( m_replayPrediction.frames.size() < 2 )
    {
        return;
    }

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/BuildTree" );
        m_replayPrediction.futureNodes.clear();
        ReplayPredictionFutureContext futureContext;
        futureContext.prediction = &m_replayPrediction;
        futureContext.rootId = m_replayPathVisualizer.targetId;
        for ( const RunReplayPredictionFrame& frame : m_replayPrediction.frames )
        {
            BuildReplayPredictionFutureNodes( frame, futureContext );
        }
    }

    const ReplayFrameIndex lastFrame = m_replayPrediction.frames.back().frameIndex;
    const std::size_t sampleStride = ReplayPathStrideForSampleCount( m_replayPrediction.frames.size() );
    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/DrawRoot" );
        bool hasPrevious = false;
        Vector3 previous = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        std::size_t ordinal = 0;
        for ( const RunReplayPredictionFrame& frame : m_replayPrediction.frames )
        {
            const std::size_t currentOrdinal = ordinal++;
            if ( frame.frameIndex != lastFrame && !ShouldDrawReplayPathSample( currentOrdinal, sampleStride ) )
            {
                continue;
            }
            const RunReplayPredictionBodySample* body =
                FindReplayPredictionBodyById( frame, m_replayPathVisualizer.targetId );
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
        childDraw.nodeCount = (std::min)( m_replayPrediction.futureNodes.size(), REPLAY_PATH_MAX_FUTURE_NODES );
        for ( std::size_t i = 0; i < childDraw.nodeCount; ++i )
        {
            childDraw.nodes[i].node = m_replayPrediction.futureNodes[i];
        }

        std::size_t ordinal = 0;
        for ( const RunReplayPredictionFrame& frame : m_replayPrediction.frames )
        {
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
                ReplayPathChildDrawState& drawState = childDraw.nodes[i];
                const RunReplayPredictionBodySample* body = FindReplayPredictionBodyById( frame, drawState.node.id );
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

        for ( const RunReplayPathTraceNode& node : m_replayPrediction.futureNodes )
        {
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
}


void Run::RenderReplayPathVisualizer( RunEditorTracer& tracer )
{
    PROFILE_SCOPED( "Frame/Replay/PathVisualizer" );
    if ( !m_replayPathVisualizer.hasTarget )
    {
        return;
    }

    RenderReplayPredictionVisualizer( tracer );

    if ( !m_solverReplay.IsEnabled() )
    {
        return;
    }

    if ( m_replayPathVisualizer.targets.empty() && m_replayPathVisualizer.targetId.value != 0 )
    {
        RunReplayPathTarget target;
        target.id = m_replayPathVisualizer.targetId;
        target.modelIndex = m_replayPathVisualizer.targetModelIndex;
        if ( m_replayPathVisualizer.targetName[0] != '\0' )
        {
            strncpy_s( target.name, sizeof( target.name ), m_replayPathVisualizer.targetName, _TRUNCATE );
        }
        m_replayPathVisualizer.targets.push_back( target );
    }

    const ReplaySolverFrameSample* presentSample = CurrentReplaySolverScrubSample();
    if ( !presentSample )
    {
        presentSample = m_solverReplay.LatestSample();
    }
    if ( !presentSample )
    {
        return;
    }

    ReplayPathBoundsContext bounds;
    m_solverReplay.ForEachSampleChronological( CaptureReplayPathBounds, &bounds );
    if ( !bounds.hasSample )
    {
        return;
    }

    const ReplayFrameIndex presentFrame = std::clamp( presentSample->frameIndex, bounds.firstFrame, bounds.lastFrame );
    const ReplayRecorderStats stats = m_solverReplay.GetStats();
    const std::size_t sampleStride = ReplayPathStrideForSampleCount( stats.sampleCount );

    m_replayPathVisualizer.futureNodes.clear();
    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    for ( RunReplayPathTarget& target : m_replayPathVisualizer.targets )
    {
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
            futureContext.rootId = target.id;
            futureContext.presentFrame = presentFrame;
            m_solverReplay.ForEachSampleChronological( BuildReplayFutureNodes, &futureContext );
        }

        {
            PROFILE_SCOPED( "Frame/Replay/PathVisualizer/RetainedTarget/DrawRoot" );
            ReplayPathRootDrawContext rootDraw;
            rootDraw.tracer = &tracer;
            rootDraw.rootId = target.id;
            rootDraw.firstFrame = bounds.firstFrame;
            rootDraw.presentFrame = presentFrame;
            rootDraw.lastFrame = bounds.lastFrame;
            rootDraw.sampleStride = sampleStride;
            m_solverReplay.ForEachSampleChronological( DrawReplayRootPath, &rootDraw );
        }

        ReplayPathChildDrawContext childDraw;
        childDraw.tracer = &tracer;
        childDraw.models = &models;
        childDraw.presentFrame = presentFrame;
        childDraw.lastFrame = bounds.lastFrame;
        childDraw.sampleStride = sampleStride;
        childDraw.nodeCount = (std::min)( targetVisualizer.futureNodes.size(), REPLAY_PATH_MAX_FUTURE_NODES );
        for ( std::size_t i = 0; i < childDraw.nodeCount; ++i )
        {
            childDraw.nodes[i].node = targetVisualizer.futureNodes[i];
        }
        if ( childDraw.nodeCount > 0 )
        {
            PROFILE_SCOPED( "Frame/Replay/PathVisualizer/RetainedTarget/DrawChildren" );
            m_solverReplay.ForEachSampleChronological( DrawReplayChildPaths, &childDraw );
            AddReplayFutureContactMarkers( targetVisualizer, tracer );
        }

        if ( target.id.value == m_replayPathVisualizer.targetId.value )
        {
            m_replayPathVisualizer.futureNodes = targetVisualizer.futureNodes;
        }

        {
            PROFILE_SCOPED( "Frame/Replay/PathVisualizer/RetainedTarget/DrawMarker" );
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
                        if ( target.id.value == m_replayPathVisualizer.targetId.value )
                        {
                            m_replayPathVisualizer.targetModelIndex = i;
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
