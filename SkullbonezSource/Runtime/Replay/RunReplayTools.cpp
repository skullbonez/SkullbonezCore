/*
File: SkullbonezSource/Runtime/Replay/RunReplayTools.cpp
Purpose:
  Owns live replay tools: scrubber input, cause-tree inspection, path
  visualization, prediction previews, and velocity-edit overlays.

Mental model:
  Replay tools read two timelines. Retained solver samples describe what already
  happened; prediction samples advance a private replay-owned physics engine.
  The renderer only receives lightweight overlay geometry.

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
  Prediction physics tick: Replay-owned fixed step against the private
    prediction engine.
  Future node: Body discovered by following contacts or predicted movement
    outward from a selected root body.
  ReplayBodyId: Stable runtime id used across retained samples even when vector
    indices are only local hints.
  Model row hint: Cached live body row paired with ReplayBodyId; replay tools
    may keep it only as a repairable lookup shortcut.
  Solver snapshot: Physics cache state that must be restored to make the next
    fixed step reproduce.
  WorkerPool: Persistent engine worker threads used only for large, independent
    fork-join loops.

Invariants:
  - Prediction must never write live physics stores; private engine state owns
    all future ticks and samples.
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
#include "ReplayInteractionController.h"
#include "ReplayOverlayLayout.h"
#include "ReplayOverlayRenderer.h"
#include "RunReplayImportExport.h"
#include "../RuntimePickService.h"
#include "../Allocation/RuntimeAllocationTracker.h"
#include "../Allocation/RuntimeReserveAllocator.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsMass.h"
#include "../../Physics/PhysicsTimestep.h"
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


// Why: retained and predicted replay samples carry model-index hints, but
// shape/radius facts are owned by ColliderStore. Keep overlay and query radii
// on the live store row instead of forcing a GameModel mirror refresh.
bool TryReplayColliderRadiusForModelIndex( const ColliderStore& colliderStore, int modelIndex, float& outRadius )
{
    // Why: retained replay rows may only carry a model-index sample. This helper
    // is a display-radius fallback; live target markers resolve collider rows
    // through PhysicsBodyHandle before drawing authored shapes.
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
    // markers read store rows. This avoids scanning the GameModel mirror just
    // to recover a stable ReplayBodyId that PhysicsBodyStore already owns.
    const float radius = (std::max)( 1.0f, (std::max)( body->boundingRadius, collider->boundingRadius ) ) * 1.18f;
    tracer.AddReplayTargetMarker( body->position, body->orientation, collider->shape, radius );
    return true;
}


// Concept: prediction stepping is pure physics. Contact-highlight and
// diagnostics-name presentation belongs to the live engine only; prediction
// samples read the private engine's body records directly.
bool StepPredictionEngineTick( PhysicsEngine& engine,
                               float fixedDt,
                               const EngineConfig& config,
                               const PhysicsWorldForces& worldForces,
                               SkullbonezCore::Threading::WorkerPool& workerPool )
{
    RuntimeAllocation::RuntimeAllocationScope replayAllocationScope(
        RuntimeAllocation::RuntimeAllocationPhase::Replay );
    engine.Step( fixedDt, config, worldForces, workerPool, nullptr, 0, PhysicsDiagnosticsCsvWriter{} );
    return true;
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
// rebuilt. The per-run rate lives on RunReplayPredictionState so authored
// director phases can slow the money-shot unfold without touching physics.
// Why: "at rest" for the causal overlay is decided from the END of the
// completed prediction, never from a momentary pause. A body rests only when
// the final frame shows no visible motion and it has not drifted across the
// final grace window; otherwise it has no resting pose and gets no grey box.
constexpr double REPLAY_PREDICTION_REST_GRACE_SECONDS = 0.4;
constexpr ReplayFrameIndex REPLAY_PREDICTION_REST_GRACE_FRAMES =
    static_cast<ReplayFrameIndex>( REPLAY_PREDICTION_REST_GRACE_SECONDS / PHYSICS_FIXED_DT );
constexpr float REPLAY_PREDICTION_REST_POSITION_EPSILON_SQ = 0.5f * 0.5f;

constexpr uint32_t REPLAY_PREDICTION_CAPTURE_BODY_WORKER_HASH =
    HashStr( "Frame/Replay/Prediction/CaptureBodyState/WorkerBodies" );
constexpr uint32_t REPLAY_PREDICTION_CAPTURE_SAMPLE_WORKER_HASH =
    HashStr( "Frame/Replay/Prediction/CaptureSample/WorkerBodies" );

#include "RunReplayPredictionHelpers.inl"
} // namespace

#include "RunReplayScrubberTools.inl"
#include "RunReplayCauseTreeTools.inl"
#include "RunReplayQueryTools.inl"

namespace
{
#include "RunReplayPredictionVisualizer.inl"
} // namespace

namespace SkullbonezCore::Basics::ReplayOverlay
{
void RenderReplayPathVisualizer( const ReplayPathVisualizerRenderContext& context )
{
    PROFILE_SCOPED( "Frame/Replay/PathVisualizer" );
    // Concept: this marker owns the replay visualizer frame budget.
    //
    // Prediction, retained solver paths, future-node tree updates, and contact
    // markers all share this deadline. Child functions receive the same start
    // time so profiler nesting cannot hide extra replay work outside the cap.
    const auto visualizerStart = std::chrono::steady_clock::now();
    RenderReplayPredictionVisualizer( context.replayRuntime,
                                      context.models,
                                      context.config,
                                      context.worldForces,
                                      context.workerPool,
                                      context.scenePhysicsEnabled,
                                      context.simulationTimeSinceLastStart,
                                      context.simulationTotalTime,
                                      context.tracer,
                                      visualizerStart,
                                      REPLAY_PREDICTION_MAX_WORK_MILLISECONDS );
    const RunReplayPredictionState& prediction = context.replayRuntime.Prediction();
    if ( !prediction.enabled && prediction.simulation.frames.size() >= 2 &&
         context.replayRuntime.PathVisualizer().hasTarget &&
         prediction.simulation.targetId.value == context.replayRuntime.PathVisualizer().targetId.value )
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

    if ( !context.replayRuntime.PathVisualizer().hasTarget )
    {
        return;
    }

    if ( !context.replayRuntime.Solver().IsEnabled() )
    {
        return;
    }

    if ( context.replayRuntime.PathVisualizer().targets.empty() &&
         context.replayRuntime.PathVisualizer().targetId.value != 0 )
    {
        if ( !ReserveReplayPredictionVector( context.replayRuntime.PathVisualizer().targets,
                                             REPLAY_PATH_MAX_ROOT_TARGETS,
                                             context.sceneCurrentFrame,
                                             "RunReplayPathVisualizer::targets" ) )
        {
            return;
        }
        RunReplayPathTarget target;
        target.id = context.replayRuntime.PathVisualizer().targetId;
        target.modelIndex = context.replayRuntime.PathVisualizer().targetModelIndex;
        if ( context.replayRuntime.PathVisualizer().targetName[0] != '\0' )
        {
            strncpy_s( target.name,
                       sizeof( target.name ),
                       context.replayRuntime.PathVisualizer().targetName,
                       _TRUNCATE );
        }
        context.replayRuntime.PathVisualizer().targets.push_back( target );
    }

    const ReplaySolverFrameSample* presentSample = context.replayRuntime.CurrentSolverScrubSample();
    if ( !presentSample )
    {
        presentSample = context.replayRuntime.Solver().LatestSample();
    }
    if ( !presentSample )
    {
        return;
    }

    ReplayPathBoundsContext bounds;
    context.replayRuntime.Solver().ForEachSampleChronological( CaptureReplayPathBounds, &bounds );
    if ( ReplayPredictionBudgetExpired( visualizerStart, REPLAY_PREDICTION_MAX_WORK_MILLISECONDS ) )
    {
        return;
    }
    if ( !bounds.hasSample )
    {
        return;
    }

    const ReplayFrameIndex presentFrame = std::clamp( presentSample->frameIndex, bounds.firstFrame, bounds.lastFrame );
    const ReplayRecorderStats stats = context.replayRuntime.Solver().GetStats();
    const std::size_t sampleStride = ReplayPathStrideForSampleCount( stats.sampleCount );

    context.replayRuntime.PathVisualizer().futureNodes.clear();
    const PhysicsBodyStore& bodyStore = context.models.GetPhysicsEngine().BodyStore();
    const ColliderStore& colliderStore = context.models.GetPhysicsEngine().Colliders();
    for ( RunReplayPathTarget& target : context.replayRuntime.PathVisualizer().targets )
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
            futureContext.collection = &context.models;
            futureContext.budgetStart = &visualizerStart;
            futureContext.rootId = target.id;
            futureContext.presentFrame = presentFrame;
            futureContext.budgetMilliseconds = REPLAY_PREDICTION_MAX_WORK_MILLISECONDS;
            futureContext.includeRagdollVisuals = context.replayRuntime.Prediction().ragdollVisualsEnabled;
            context.replayRuntime.Solver().ForEachSampleChronological( BuildReplayFutureNodes, &futureContext );
        }
        if ( ReplayPredictionBudgetExpired( visualizerStart, REPLAY_PREDICTION_MAX_WORK_MILLISECONDS ) )
        {
            return;
        }

        {
            PROFILE_SCOPED( "Frame/Replay/PathVisualizer/RetainedTarget/DrawRoot" );
            ReplayPathRootDrawContext rootDraw;
            rootDraw.tracer = &context.tracer;
            rootDraw.budgetStart = &visualizerStart;
            rootDraw.rootId = target.id;
            rootDraw.firstFrame = bounds.firstFrame;
            rootDraw.presentFrame = presentFrame;
            rootDraw.lastFrame = bounds.lastFrame;
            rootDraw.budgetMilliseconds = REPLAY_PREDICTION_MAX_WORK_MILLISECONDS;
            rootDraw.sampleStride = sampleStride;
            context.replayRuntime.Solver().ForEachSampleChronological( DrawReplayRootPath, &rootDraw );
        }
        if ( ReplayPredictionBudgetExpired( visualizerStart, REPLAY_PREDICTION_MAX_WORK_MILLISECONDS ) )
        {
            return;
        }

        ReplayPathChildDrawContext childDraw;
        childDraw.tracer = &context.tracer;
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
            context.replayRuntime.Solver().ForEachSampleChronological( DrawReplayChildPaths, &childDraw );
            DrawReplayChildFinalMarkers( childDraw );
        }
        if ( ReplayPredictionBudgetExpired( visualizerStart, REPLAY_PREDICTION_MAX_WORK_MILLISECONDS ) )
        {
            return;
        }

        if ( target.id.value == context.replayRuntime.PathVisualizer().targetId.value )
        {
            context.replayRuntime.PathVisualizer().futureNodes = targetVisualizer.futureNodes;
        }

        {
            PROFILE_SCOPED( "Frame/Replay/PathVisualizer/RetainedTarget/DrawMarker" );
            if ( ReplayPredictionBudgetExpired( visualizerStart, REPLAY_PREDICTION_MAX_WORK_MILLISECONDS ) )
            {
                return;
            }

            ModelRowHint targetHint;
            targetHint.value = target.modelIndex;
            int markerIndex = -1;
            const bool markerResolved = TryResolveReplayBodyModelIndex( bodyStore,
                                                                        target.id,
                                                                        targetHint,
                                                                        context.models.SceneEntityCount(),
                                                                        markerIndex );
            target.modelIndex = targetHint.value;
            if ( target.id.value == context.replayRuntime.PathVisualizer().targetId.value )
            {
                context.replayRuntime.PathVisualizer().targetModelIndex = targetHint.value;
            }
            if ( markerResolved )
            {
                TryAddReplayTargetMarkerFromStores( context.tracer, bodyStore, colliderStore, markerIndex );
            }
        }
    }
}
} // namespace SkullbonezCore::Basics::ReplayOverlay

void Run::RenderReplayPathVisualizer( RunEditorTracer& tracer )
{
    const auto physicsWorldForces = m_cWorldEnvironment.GetPhysicsWorldForces();
    // Lifetime: physicsWorldForces is a local value because the overlay context
    // borrows it by reference for the immediate delegate call.
    const SkullbonezCore::Basics::ReplayOverlay::ReplayPathVisualizerRenderContext context{
        m_replayRuntime,
        m_cGameModelCollection,
        *m_systems.config,
        physicsWorldForces,
        *m_systems.workerPool,
        tracer,
        SceneState().isScenePhysics,
        SceneState().currentFrame,
        m_timers.simulationTimer.GetTimeSinceLastStart(),
        m_timers.simulationTimer.GetTotalTime() };
    SkullbonezCore::Basics::ReplayOverlay::RenderReplayPathVisualizer( context );
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
        ModelRowHint focusHint;
        focusHint.value = m_replayRuntime.Camera().focusModelIndex;
        int focusedModelIndex = -1;
        if ( TryResolveReplayBodyModelIndex( bodyStore,
                                             m_replayRuntime.Camera().focusedId,
                                             focusHint,
                                             bodyStore.Count(),
                                             focusedModelIndex ) )
        {
            m_replayRuntime.Camera().focusModelIndex = focusHint.value;
            TryAddReplayTargetMarkerFromStores( tracer, bodyStore, colliderStore, focusedModelIndex );
            return;
        }
        m_replayRuntime.Camera().focusModelIndex = focusHint.value;
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
                          static_cast<int>( m_replayRuntime.Prediction().futureNodeCache.futureNodes.size() ) )
            {
                const RunReplayPathTraceNode& node =
                    m_replayRuntime.Prediction().futureNodeCache.futureNodes[static_cast<std::size_t>(
                        m_replayRuntime.Camera().focusContactIndex )];
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
