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
  Cause tree: Contact, solver, and predicted-motion graph that explains how one
    body influenced others.
  Path visualizer: Overlay that draws past/future body trajectories and contact
    handoffs.
  Replay target marker: Overlay outline/ring drawn around the replay-selected
    body from live body/collider store rows.
  Prediction slice: Time-budgeted replay preview work performed inside a render
    frame.
  Prediction physics tick: Replay-owned fixed step that temporarily advances
    PhysicsBodyStore and then restores the live store snapshot.
  Future node: Body discovered by following contacts or predicted movement
    outward from a selected root body.
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
#include "ReplayOverlayLayout.h"
#include "../RuntimePickService.h"
#include "../Allocation/RuntimeAllocationTracker.h"
#include "../Allocation/RuntimeReserveAllocator.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
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
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

#include <commdlg.h>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::UI::Layout;
using namespace SkullbonezCore::Basics::RunInternal;
using namespace SkullbonezCore::Basics::ReplayOverlay;
using SkullbonezCore::Assets::EDITOR_HULL_ASSET_COUNT;
using SkullbonezCore::Assets::EDITOR_HULL_ASSETS;
using SkullbonezCore::Assets::EditorHullAsset;
using SkullbonezCore::Assets::EditorHullAssetDefaultsToContactRelease;
using SkullbonezCore::Assets::EditorHullAssetPath;
using SkullbonezCore::Assets::EditorHullAssetToken;
namespace RuntimeAllocation = SkullbonezCore::Runtime::Allocation;

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


// Why: retained and predicted replay samples carry model-index hints, but
// shape/radius facts are owned by ColliderStore. Keep overlay and query radii
// on the live store row instead of forcing a GameModel mirror refresh.
bool TryReplayColliderRadiusForModelIndex( const ColliderStore& colliderStore, int modelIndex, float& outRadius )
{
    const PhysicsColliderHandle colliderHandle = colliderStore.HandleForModelIndex( modelIndex );
    const ColliderRecord* collider = colliderStore.RecordForHandle( colliderHandle );
    if ( !collider || colliderStore.ModelIndexForHandle( colliderHandle ) != modelIndex )
    {
        return false;
    }

    outRadius = (std::max)( collider->boundingRadius > 0.0f ? collider->boundingRadius
                                                            : GetShapeBoundingRadius( collider->shape ),
                            1.0f );
    return true;
}


float ReplayColliderRadiusForModelIndex( const ColliderStore& colliderStore, int modelIndex )
{
    float radius = 1.0f;
    TryReplayColliderRadiusForModelIndex( colliderStore, modelIndex, radius );
    return radius;
}


// Why: replay target UI code may still carry a model-index hint for names,
// ragdoll grouping, or draw ordering, but stable replay identity belongs to the
// body store. Keeping id recovery here prevents new GameModel replay-id scans in
// the pick, prediction, and marker paths.
ReplayBodyId ReplayBodyIdForModelIndex( const PhysicsBodyStore& bodyStore, int modelIndex )
{
    ReplayBodyId id;
    if ( const PhysicsBodyRecord* body = bodyStore.RecordForModelIndex( modelIndex ) )
    {
        id.value = body->replayBodyId;
    }
    return id;
}


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


bool TryAddReplayTargetMarkerFromStores( RunEditorTracer& tracer,
                                         const PhysicsBodyStore& bodyStore,
                                         const ColliderStore& colliderStore,
                                         int modelIndex )
{
    const PhysicsBodyHandle bodyHandle = bodyStore.HandleForModelIndex( modelIndex );
    const PhysicsColliderHandle colliderHandle = colliderStore.HandleForModelIndex( modelIndex );
    const PhysicsBodyRecord* body = bodyStore.RecordForHandle( bodyHandle );
    const ColliderRecord* collider = colliderStore.RecordForHandle( colliderHandle );
    if ( !body || !collider || bodyStore.ModelIndexForHandle( bodyHandle ) != modelIndex ||
         colliderStore.ModelIndexForHandle( colliderHandle ) != modelIndex || collider->body != bodyHandle )
    {
        return false;
    }

    // Invariant: replay target identity resolves through body handles before
    // markers read store rows. This avoids scanning the GameModel mirror just
    // to recover a stable ReplayBodyId that PhysicsBodyStore already owns.
    const float radius = (std::max)( 1.0f, (std::max)( body->boundingRadius, collider->boundingRadius ) ) * 1.18f;
    tracer.AddReplayTargetMarker( body->position, body->orientation, collider->shape, radius );
    return true;
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


void StepReplayPredictionPhysicsTick( SkullbonezCore::GameObjects::GameModelCollection& modelCollection,
                                      float fixedDt,
                                      const EngineConfig& config,
                                      const PhysicsWorldForces& worldForces,
                                      SkullbonezCore::Threading::WorkerPool& workerPool )
{
    RuntimeAllocation::RuntimeAllocationScope replayAllocationScope(
        RuntimeAllocation::RuntimeAllocationPhase::Replay );
    const int modelCount = modelCollection.ModelCount();
    // Invariant: prediction steps mutate PhysicsBodyStore, then restore live
    // state from captured store records. Model topology repair remains the
    // owner-side edge before the store-owned step reads body/collider rows.
    modelCollection.RepairPhysicsBodyAndColliderTopology();
    modelCollection.TickContactHighlights( modelCount, fixedDt );

    PhysicsEngine& physicsEngine = modelCollection.GetPhysicsEngine();
    const char* const* diagnosticNames = nullptr;
    int diagnosticNameCount = 0;
#ifdef _DEBUG
    std::vector<const char*> physicsDiagnosticsModelNames;
    if ( physicsEngine.ShouldEmitStepDiagnostics() || physicsEngine.ShouldEmitCollisionTimeDiagnostics() )
    {
        // Lifetime: diagnostics names are borrowed only through the immediate
        // Step call, matching the runtime fixed-step edge.
        modelCollection.FillPhysicsDiagnosticsNames( physicsEngine.BodyStore().Count(), physicsDiagnosticsModelNames );
        diagnosticNames = physicsDiagnosticsModelNames.empty() ? nullptr : physicsDiagnosticsModelNames.data();
        diagnosticNameCount = static_cast<int>( physicsDiagnosticsModelNames.size() );
    }
#endif
    physicsEngine.Step( fixedDt, config, worldForces, workerPool, diagnosticNames, diagnosticNameCount );

    for ( int index : physicsEngine.GetFixedContactHighlightBodies() )
    {
        modelCollection.NotifyFixedContact( index, 0.5f );
    }
    // Invariant: prediction samples read PhysicsBodyStore records directly.
    // Do not project temporary preview poses into GameModel mirrors; the
    // captured restore state owns the live model pose after prediction exits.
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

// Why: the 200-brick prediction scene needs more than the old 100-node cap to
// show the full contact spread instead of clipping the visual explanation.
constexpr std::size_t REPLAY_PATH_MAX_FUTURE_NODES = 240;
constexpr std::size_t REPLAY_PATH_MAX_ROOT_TARGETS = 100;
constexpr std::size_t REPLAY_PATH_MAX_SEGMENTS = 260;
constexpr float REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ = 0.0001f;
// Why: sleeping or contact-propagated bodies can wake without translating. Child
// prediction outlines wait for real linear speed so the wall blooms outward
// only when bricks are actually about to move.
constexpr float REPLAY_PREDICTION_CHILD_LINEAR_SPEED_SQ = 8.0f * 8.0f;

// Invariant: Worker dispatch is only worth it for large body snapshots. Small
// scenes stay serial so replay overlays do not pay thread wakeup cost to copy a
// few kilobytes.
constexpr int REPLAY_PREDICTION_PARALLEL_BODY_MIN = 2048;

// Concept: the prediction overlay is a play-once causal animation, not a
// static plot. A wall-clock reveal cursor sweeps the predicted frames so the
// root line grows first and each child line starts only when its causing frame
// is revealed; after the sweep the finished tree holds until the prediction is
// rebuilt. Rate is predicted seconds revealed per real second; 1.0 means the
// future unfolds at the same pace it would actually happen.
constexpr double REPLAY_PREDICTION_REVEAL_SECONDS_PER_SECOND = 1.0;
// Why: "at rest" for the causal overlay is decided from the END of the
// completed prediction, never from a momentary pause. A body rests only when
// the final frame shows no visible motion and it has not drifted across the
// final grace window; otherwise it has no resting pose and gets no grey box.
constexpr double REPLAY_PREDICTION_REST_GRACE_SECONDS = 0.4;
constexpr ReplayFrameIndex REPLAY_PREDICTION_REST_GRACE_FRAMES =
    static_cast<ReplayFrameIndex>( REPLAY_PREDICTION_REST_GRACE_SECONDS / PHYSICS_FIXED_DT );
constexpr float REPLAY_PREDICTION_REST_POSITION_EPSILON_SQ = 0.5f * 0.5f;

// Hazard: prediction temporarily swaps live model/solver state. Keep a small
// reserve so we do not enter a mutation section after spending the whole visual
// budget and then visibly spike while restoring live state.
constexpr double REPLAY_PREDICTION_MUTATION_RESERVE_MILLISECONDS = 1.0;
constexpr uint32_t REPLAY_PREDICTION_CAPTURE_BODY_WORKER_HASH =
    HashStr( "Frame/Replay/Prediction/CaptureBodyState/WorkerBodies" );
constexpr uint32_t REPLAY_PREDICTION_CAPTURE_SAMPLE_WORKER_HASH =
    HashStr( "Frame/Replay/Prediction/CaptureSample/WorkerBodies" );

#include "RunReplayPredictionHelpers.inl"
#include "RunReplayImportExport.inl"
} // namespace

#include "RunReplayScrubberTools.inl"
#include "RunReplayCauseTreeTools.inl"
#include "RunReplayVelocityEdit.inl"
#include "RunReplayQueryTools.inl"

namespace
{
#include "RunReplayPredictionVisualizer.inl"
} // namespace

void Run::RenderReplayPathVisualizer( RunEditorTracer& tracer )
{
    PROFILE_SCOPED( "Frame/Replay/PathVisualizer" );
    // Concept: this marker owns the replay visualizer frame budget.
    //
    // Prediction, retained solver paths, future-node tree updates, and contact
    // markers all share this deadline. Child functions receive the same start
    // time so profiler nesting cannot hide extra replay work outside the cap.
    const auto visualizerStart = std::chrono::steady_clock::now();
    const auto physicsWorldForces = m_cWorldEnvironment.GetPhysicsWorldForces();
    RenderReplayPredictionVisualizer( m_replayRuntime,
                                      m_cGameModelCollection,
                                      *m_systems.config,
                                      physicsWorldForces,
                                      *m_systems.workerPool,
                                      SceneState().isScenePhysics,
                                      m_timers.simulationTimer.GetTimeSinceLastStart(),
                                      m_timers.simulationTimer.GetTotalTime(),
                                      tracer,
                                      visualizerStart,
                                      REPLAY_PREDICTION_MAX_WORK_MILLISECONDS );
    const RunReplayPredictionState& prediction = m_replayRuntime.Prediction();
    if ( !prediction.enabled && prediction.frames.size() >= 2 && m_replayRuntime.PathVisualizer().hasTarget &&
         prediction.targetId.value == m_replayRuntime.PathVisualizer().targetId.value )
    {
        // Why: Play disables prediction but keeps the committed path preview.
        // Letting the retained visualizer continue here would rebuild child
        // paths from the advancing live timeline and make frozen lines drift.
        return;
    }
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
        if ( !ReserveReplayPredictionVector( m_replayRuntime.PathVisualizer().targets,
                                             REPLAY_PATH_MAX_ROOT_TARGETS,
                                             SceneState().currentFrame,
                                             "RunReplayPathVisualizer::targets" ) )
        {
            return;
        }
        RunReplayPathTarget target;
        target.id = m_replayRuntime.PathVisualizer().targetId;
        target.modelIndex = m_replayRuntime.PathVisualizer().targetModelIndex;
        if ( m_replayRuntime.PathVisualizer().targetName[0] != '\0' )
        {
            strncpy_s( target.name, sizeof( target.name ), m_replayRuntime.PathVisualizer().targetName, _TRUNCATE );
        }
        m_replayRuntime.PathVisualizer().targets.push_back( target );
    }

    const ReplaySolverFrameSample* presentSample = m_replayRuntime.CurrentSolverScrubSample();
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
    const PhysicsBodyStore& bodyStore = m_cGameModelCollection.GetPhysicsEngine().BodyStore();
    const ColliderStore& colliderStore = m_cGameModelCollection.GetPhysicsEngine().Colliders();
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
            futureContext.collection = &m_cGameModelCollection;
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
        childDraw.colliderStore = &colliderStore;
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
            DrawReplayChildFinalMarkers( childDraw );
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

            int markerIndex = -1;
            if ( TryResolveReplayBodyModelIndex( bodyStore,
                                                 target.id,
                                                 target.modelIndex,
                                                 m_cGameModelCollection.GetModelCount(),
                                                 markerIndex ) )
            {
                target.modelIndex = markerIndex;
                if ( target.id.value == m_replayRuntime.PathVisualizer().targetId.value )
                {
                    m_replayRuntime.PathVisualizer().targetModelIndex = markerIndex;
                }
                TryAddReplayTargetMarkerFromStores( tracer, bodyStore, colliderStore, markerIndex );
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
        const PhysicsBodyStore& bodyStore = m_cGameModelCollection.GetPhysicsEngine().BodyStore();
        const ColliderStore& colliderStore = m_cGameModelCollection.GetPhysicsEngine().Colliders();
        int focusedModelIndex = -1;
        if ( TryResolveReplayBodyModelIndex( bodyStore,
                                             m_replayRuntime.Camera().focusedId,
                                             m_replayRuntime.Camera().focusModelIndex,
                                             bodyStore.Count(),
                                             focusedModelIndex ) )
        {
            m_replayRuntime.Camera().focusModelIndex = focusedModelIndex;
            TryAddReplayTargetMarkerFromStores( tracer, bodyStore, colliderStore, focusedModelIndex );
            return;
        }
    }

    if ( m_replayRuntime.Camera().focusKind == RunReplayCameraFocusKind::Manifold ||
         m_replayRuntime.Camera().focusKind == RunReplayCameraFocusKind::PredictionContact ||
         m_replayRuntime.Camera().focusKind == RunReplayCameraFocusKind::PredictionMotion )
    {
        if ( m_replayRuntime.Camera().focusKind == RunReplayCameraFocusKind::Manifold )
        {
            const ReplaySolverFrameSample* sample = m_replayRuntime.CurrentSolverScrubSample();
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
        else if ( m_replayRuntime.Camera().focusKind == RunReplayCameraFocusKind::PredictionContact )
        {
            ReplayFrameIndex focusFrame = 0;
            int focusedModelIndex = m_replayRuntime.Camera().focusModelIndex;
            int counterpartModelIndex = m_replayRuntime.Camera().focusCounterpartModelIndex;

            const RunReplayCauseTreeState& causeTree = m_replayRuntime.CauseTree();
            if ( causeTree.selectedRow >= 0 && causeTree.selectedRow < static_cast<int>( causeTree.rows.size() ) )
            {
                const RunReplayCauseTreeRow& row = causeTree.rows[static_cast<std::size_t>( causeTree.selectedRow )];
                if ( row.kind == RunReplayCauseTreeRowKind::PredictionContact &&
                     row.id.value == m_replayRuntime.Camera().focusedId.value )
                {
                    focusFrame = row.firstFrame;
                    focusedModelIndex = row.modelIndex;
                    counterpartModelIndex = row.counterpartModelIndex;
                }
            }
            else if ( m_replayRuntime.Camera().focusContactIndex >= 0 &&
                      m_replayRuntime.Camera().focusContactIndex <
                          static_cast<int>( m_replayRuntime.Prediction().futureNodes.size() ) )
            {
                const RunReplayPathTraceNode& node =
                    m_replayRuntime.Prediction()
                        .futureNodes[static_cast<std::size_t>( m_replayRuntime.Camera().focusContactIndex )];
                if ( node.id.value == m_replayRuntime.Camera().focusedId.value && node.contactDerived )
                {
                    focusFrame = node.firstFrame;
                    focusedModelIndex = node.modelIndex;
                    counterpartModelIndex = node.parentModelIndex;
                }
            }

            bool drewPredictionManifold = false;
            const std::vector<RunReplayPredictionFrame>& frames = m_replayRuntime.ActivePredictionFrames();
            for ( const RunReplayPredictionFrame& frame : frames )
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
                    const int contactModelA =
                        ReplayRagdollTorsoModelIndexForPart( m_cGameModelCollection, contact.bodyA );
                    const int contactModelB =
                        contact.bodyB >= 0
                            ? ReplayRagdollTorsoModelIndexForPart( m_cGameModelCollection, contact.bodyB )
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
