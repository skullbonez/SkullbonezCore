/*
File: SkullbonezSource/Runtime/Scene/SceneController.cpp
Purpose:
  Implements scene state, physics ownership, navigation, and deferred requests.

Mental model:
  Scene state, physics, navigation, completion gates, and deferred intent live
  behind one controller. Cold load implementation is split into RunScene.cpp,
  but remains a SceneController transaction with explicit synchronous borrows.

Glossary:
  Scene runtime: Mutable per-scene queue, completion, and automation state.
  Scene queue: Ordered list of authored scenes or demo entries to run.
  Request batch: Ordered fixed-capacity copy consumed at one frame checkpoint.

Invariants:
  - Controller accessors must preserve the existing SceneRuntime semantics.
  - Interactive scene requests cannot bypass the controller-owned ring.
  - Frame completion returns value-only load/quit/hold intent to the process shell.

Related:
  - SkullbonezSource/Runtime/Scene/SceneController.h
  - SkullbonezSource/Runtime/Scene/SceneRuntime.cpp
*/
#include "SceneController.h"

#include "../../Core/FatalError.h"
#include "../../Core/Config.h"
#include "../../Core/Log.h"
#include "../../Core/WorkerPool.h"
#include "../../GameObjects/GameModelCollection.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsEngineStoreQueries.h"
#include "../../Physics/PhysicsDiagnosticsSink.h"
#include "../../Physics/PhysicsWorldForces.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace SkullbonezCore
{
namespace Basics
{
namespace
{
constexpr double SCENE_PERF_PASS_SECONDS = 2.0;
constexpr float WATER_HEIGHT_CONTROL_SPEED = 20.0f; // World meters per second.
#ifdef _DEBUG
void WriteScenePhysicsDiagnosticsCsv( void*, const char* fileName, const char* fmt, va_list args )
{
    // Why: Physics receives only a value writer; the scene owner keeps the
    // process logging dependency outside the solver and its hot store loops.
    Log().WriteVf( fileName, fmt, args );
}
#endif
} // namespace

SceneController::SceneController() : m_models( m_physics )
{
}


void SceneController::StepPhysics( float fixedDt,
                                   const EngineConfig& config,
                                   const Physics::PhysicsWorldForces& worldForces,
                                   Threading::WorkerPool& workerPool )
{
    const int modelCount = m_models.SceneEntityCount();
    // Invariant: PhysicsBodyStore is the per-tick body authority. Descriptor
    // sidecars are imported only when topology changes; same-count editor or
    // replay mutations must commit explicitly before this step reads rows.
    m_models.RepairPhysicsBodyAndColliderTopology();
    m_models.TickContactHighlights( modelCount, fixedDt );

    const char* const* diagnosticNames = nullptr;
    int diagnosticNameCount = 0;
    Physics::PhysicsDiagnosticsCsvWriter diagnosticsCsvWriter;
#ifdef _DEBUG
    diagnosticsCsvWriter.writeVf = WriteScenePhysicsDiagnosticsCsv;
    std::vector<const char*> physicsDiagnosticsModelNames;
    if ( m_physics.ShouldEmitStepDiagnostics() || m_physics.ShouldEmitCollisionTimeDiagnostics() )
    {
        // Lifetime: Debug diagnostics borrow name pointers only until Step
        // returns; physics never retains this presentation table.
        m_models.FillPhysicsDiagnosticsNames( Physics::PhysicsEngineStoreQueries::BodyStore( m_physics ).Count(),
                                              physicsDiagnosticsModelNames );
        diagnosticNames = physicsDiagnosticsModelNames.empty() ? nullptr : physicsDiagnosticsModelNames.data();
        diagnosticNameCount = static_cast<int>( physicsDiagnosticsModelNames.size() );
    }
#endif
    m_physics
        .Step( fixedDt, config, worldForces, workerPool, diagnosticNames, diagnosticNameCount, diagnosticsCsvWriter );

    // Why: fixed-contact highlights are presentation feedback, not solver
    // state. Keeping this edge beside the scene stores makes that split visible.
    for ( int index : Physics::PhysicsEngineStoreQueries::FixedContactHighlightBodies( m_physics ) )
    {
        m_models.NotifyFixedContact( index, 0.5f );
    }
}


void SceneController::ApplyWaterHeightControl( bool pageDown, bool pageUp, float dt )
{
    if ( pageDown == pageUp )
    {
        return;
    }
    const float direction = pageUp ? 1.0f : -1.0f;
    m_world.SetFluidSurfaceHeight( m_world.GetFluidSurfaceHeight() + direction * WATER_HEIGHT_CONTROL_SPEED * dt );
}


void SceneController::EnterInteractiveRun()
{
    m_runtime.State().isInteractiveRun = true;
    m_runtime.State().isExitOnComplete = false;
}


bool SceneController::CanAutomationQuit() const
{
    return !m_runtime.State().isInteractiveRun;
}


void SceneController::MarkInteractiveRunComplete()
{
    m_runtime.State().isTestComplete = true;
    m_runtime.State().isExitOnComplete = false;
}


void SceneController::ToggleCrossScenePause()
{
    m_crossScenePauseLocked = !m_crossScenePauseLocked;
}


bool SceneController::CrossScenePauseLocked() const
{
    return m_crossScenePauseLocked;
}


SceneController::SceneController( std::vector<std::string> queue )
    : m_runtime( std::move( queue ) ), m_models( m_physics )
{
}


bool SceneController::TrimForReplayRestore( int bodyCount )
{
    const int liveBodyCount = Physics::PhysicsEngineStoreQueries::BodyStore( m_physics ).Count();
    const int liveColliderCount = Physics::PhysicsEngineStoreQueries::Colliders( m_physics ).Count();
    const uint32_t authoredBodyCount = m_physics.AuthoredBodyDescriptorCount().value;
    if ( bodyCount < 0 || bodyCount > liveBodyCount || static_cast<uint32_t>( bodyCount ) > authoredBodyCount ||
         !m_models.CanTrimPresentationRowsForSceneRestore( bodyCount ) || bodyCount > m_entities.Count() )
    {
        return false;
    }

    const Physics::PhysicsBodyCount bodies = Physics::MakePhysicsBodyCountFromNonNegativeInt( bodyCount );
    const Physics::PhysicsColliderCount colliders = Physics::MakePhysicsColliderCountFromNonNegativeInt( bodyCount );
    const Physics::PhysicsAuthoredBodyCount authored =
        Physics::MakePhysicsAuthoredBodyCountFromNonNegativeInt( bodyCount );
    // Concept: replay topology restore is a two-phase transaction. Every owner
    // rejects an impossible target above before the first write. Once commit
    // starts, a failed shrink means an internal topology invariant broke; it is
    // not a recoverable replay-file error because earlier owners may already
    // have retired handles.
    // Invariant: physics rows shrink before presentation and metadata rows.
    // Every surviving handle was validated by replay id before this command,
    // and PhysicsBodyStore retires removed handles.
    if ( !m_physics.TrimBodiesToCount( bodies ) ||
         ( liveColliderCount > bodyCount && !m_physics.TrimCollidersToCount( colliders ) ) ||
         !m_physics.TrimAuthoredBodyDescriptorsToCount( authored ) ||
         !m_models.TrimPresentationRowsForSceneRestore( bodyCount ) || !m_entities.TrimToCount( bodyCount ) )
    {
        SB_FATAL( "Runtime/SceneController",
                  "Replay topology commit failed after a successful preflight; live owners may be partially trimmed" );
    }
    return true;
}


RunSceneState& SceneController::State()
{
    return m_runtime.State();
}


const RunSceneState& SceneController::State() const
{
    return m_runtime.State();
}


RunSceneBrowserState& SceneController::Browser()
{
    // Invariant: Scene browser arrays live for the whole run so UI/render host
    // name-pointer views remain stable until the next explicit browser refresh.
    return m_browser;
}


const RunSceneBrowserState& SceneController::Browser() const
{
    return m_browser;
}


RunSceneUIOverrideState& SceneController::UIOverrides()
{
    return m_uiOverrides;
}


const RunSceneUIOverrideState& SceneController::UIOverrides() const
{
    return m_uiOverrides;
}


SceneEntityStore& SceneController::Entities()
{
    return m_entities;
}


const SceneEntityStore& SceneController::Entities() const
{
    return m_entities;
}


GameObjects::GameModelCollection& SceneController::Models()
{
    return m_models;
}


const GameObjects::GameModelCollection& SceneController::Models() const
{
    return m_models;
}


Environment::CameraCollection& SceneController::Cameras()
{
    return m_cameras;
}


const Environment::CameraCollection& SceneController::Cameras() const
{
    return m_cameras;
}


Environment::WorldEnvironment& SceneController::World()
{
    return m_world;
}


const Environment::WorldEnvironment& SceneController::World() const
{
    return m_world;
}


SceneTerrain& SceneController::Terrain()
{
    return m_terrain;
}


const SceneTerrain& SceneController::Terrain() const
{
    return m_terrain;
}


Physics::PhysicsEngine& SceneController::Physics()
{
    return m_physics;
}


const Physics::PhysicsEngine& SceneController::Physics() const
{
    return m_physics;
}


bool SceneController::HasEntry( int index ) const
{
    return m_runtime.HasEntry( index );
}


bool SceneController::HasCurrentEntry() const
{
    return m_runtime.HasCurrentEntry();
}


const std::string* SceneController::CurrentPath() const
{
    return m_runtime.CurrentPath();
}


const std::string& SceneController::PathAt( int index ) const
{
    return m_runtime.PathAt( index );
}


int SceneController::QueueSize() const
{
    return m_runtime.QueueSize();
}


int SceneController::CurrentIndex() const
{
    return m_runtime.CurrentIndex();
}


int SceneController::NextIndex() const
{
    return m_runtime.NextIndex();
}


const std::vector<std::string>& SceneController::Queue() const
{
    return m_runtime.Queue();
}


void SceneController::BeginLoad( int index )
{
    m_runtime.BeginLoad( index );
}


void SceneController::RecordLifecycleEvent( SceneRuntimeLifecycleEvent event, SceneLifecycleConsumerMask consumers )
{
    const int entityCount = m_entities.Count();
    const int bodyCount = Physics::PhysicsEngineStoreQueries::BodyStore( m_physics ).Count();
    const int colliderCount = Physics::PhysicsEngineStoreQueries::Colliders( m_physics ).Count();
    const bool requiresEmptyTopology = event == SceneRuntimeLifecycleEvent::AfterSceneCleared ||
                                       event == SceneRuntimeLifecycleEvent::BeforeScenePopulate;
    const bool requiresMatchedTopology = event == SceneRuntimeLifecycleEvent::AfterScenePopulate ||
                                         event == SceneRuntimeLifecycleEvent::AfterSceneActivated;
    // Invariant: lifecycle publication is the commit edge observed by later
    // owners. Never publish a cleared or populated phase while scene metadata,
    // bodies, and colliders disagree about the live topology.
    if ( ( requiresEmptyTopology && ( entityCount != 0 || bodyCount != 0 || colliderCount != 0 ) ) ||
         ( requiresMatchedTopology && ( entityCount != bodyCount || entityCount != colliderCount ) ) )
    {
        SB_FATAL( "Runtime/SceneController",
                  "Scene lifecycle topology mismatch. phase=%s entities=%d bodies=%d colliders=%d",
                  SceneRuntimeLifecycleEventName( event ),
                  entityCount,
                  bodyCount,
                  colliderCount );
    }
    m_runtime.RecordLifecycleEvent( event, consumers );
}


void SceneController::MarkManualReset()
{
    m_runtime.MarkManualReset();
}


int SceneController::FindNormalizedPath( const std::string& normalizedPath ) const
{
    return m_runtime.FindNormalizedPath( normalizedPath );
}


int SceneController::FindGeneratedDemo() const
{
    return m_runtime.FindGeneratedDemo();
}


int SceneController::Append( std::string path )
{
    return m_runtime.Append( std::move( path ) );
}


bool SceneController::CurrentQueueIsCinematicDeck() const
{
    return m_runtime.CurrentQueueIsCinematicDeck();
}


int SceneController::AdjacentQueueIndex( int direction ) const
{
    return m_runtime.AdjacentQueueIndex( direction );
}


void SceneController::SubmitLoadBrowserIndex( int index )
{
    SceneRequest request;
    request.type = SceneRequestType::LoadBrowserIndex;
    request.index = index;
    const SbResult result = m_requests.Submit( request );
    if ( !result.ok )
    {
        SB_FATAL( result.error.owner, "%s", result.error.message );
    }
}


void SceneController::SubmitLoadDemoScene()
{
    SceneRequest request;
    request.type = SceneRequestType::LoadDemoScene;
    const SbResult result = m_requests.Submit( request );
    if ( !result.ok )
    {
        SB_FATAL( result.error.owner, "%s", result.error.message );
    }
}


void SceneController::SubmitResetCurrentScene( bool preserveUIState,
                                               bool suppressExitOnComplete,
                                               bool preserveRuntimeState )
{
    SceneRequest request;
    request.type = SceneRequestType::ResetCurrentScene;
    request.preserveUIState = preserveUIState;
    request.suppressExitOnComplete = suppressExitOnComplete;
    request.preserveRuntimeState = preserveRuntimeState;
    const SbResult result = m_requests.Submit( request );
    if ( !result.ok )
    {
        SB_FATAL( result.error.owner, "%s", result.error.message );
    }
}


SbResult SceneController::SubmitCreateScene( const char* requestedName )
{
    const std::size_t nameLength = requestedName ? strnlen_s( requestedName, SCENE_REQUEST_TEXT_CAPACITY ) : 0;
    if ( requestedName && nameLength >= SCENE_REQUEST_TEXT_CAPACITY )
    {
        return SbResult::Failure( "Runtime/SceneController",
                                  "Scene name exceeds the fixed %d-byte request payload",
                                  SCENE_REQUEST_TEXT_CAPACITY - 1 );
    }

    SceneRequest request;
    request.type = SceneRequestType::CreateScene;
    if ( requestedName )
    {
        strcpy_s( request.text, requestedName );
    }
    return m_requests.Submit( request );
}


void SceneController::SubmitSaveCurrentDefaults()
{
    SceneRequest request;
    request.type = SceneRequestType::SaveCurrentDefaults;
    const SbResult result = m_requests.Submit( request );
    if ( !result.ok )
    {
        SB_FATAL( result.error.owner, "%s", result.error.message );
    }
}


SceneRequestBatch SceneController::TakePendingRequests()
{
    return m_requests.TakePending();
}


std::size_t SceneController::PendingRequestCount() const
{
    return m_requests.Size();
}


std::vector<RunRequiredContactState>& SceneController::RequiredContacts()
{
    return m_runtime.RequiredContacts();
}


const std::vector<RunRequiredContactState>& SceneController::RequiredContacts() const
{
    return m_runtime.RequiredContacts();
}


std::vector<RunRequiredBroadphaseXCellsState>& SceneController::RequiredBroadphaseXCells()
{
    return m_runtime.RequiredBroadphaseXCells();
}


const std::vector<RunRequiredBroadphaseXCellsState>& SceneController::RequiredBroadphaseXCells() const
{
    return m_runtime.RequiredBroadphaseXCells();
}


void SceneController::ClearRequiredAutomationGates()
{
    m_runtime.ClearRequiredAutomationGates();
}


void SceneController::UpdateRequiredContacts( float contactEpsilon )
{
    m_runtime.UpdateRequiredContacts( m_models, contactEpsilon );
}


bool SceneController::RequiredContactsComplete() const
{
    return m_runtime.RequiredContactsComplete();
}


void SceneController::UpdateRequiredBroadphaseXCells(
    const Math::CollisionDetection::SpatialGrid::ActiveCell* activeCells,
    int activeCellCount )
{
    m_runtime.UpdateRequiredBroadphaseXCells( activeCells, activeCellCount );
}


bool SceneController::RequiredBroadphaseXCellsComplete() const
{
    return m_runtime.RequiredBroadphaseXCellsComplete();
}


SceneFrameAdvanceResult SceneController::AdvanceFrame( bool proceedAllowed,
                                                       bool perfTestActive,
                                                       bool screenshotSaved,
                                                       bool manualCameraActive,
                                                       double elapsedSeconds )
{
    SceneFrameAdvanceResult result;
    if ( !proceedAllowed )
    {
        return result;
    }

    ++m_runtime.State().currentFrame;
    const bool hasRequiredSceneGate = !RequiredContacts().empty() || !RequiredBroadphaseXCells().empty();
    const bool requiredSceneComplete = RequiredContactsComplete() && RequiredBroadphaseXCellsComplete();

    const auto finishInteractiveOrQueueNext = [&]( const char* reason )
    {
        result.finishReason = reason;
        if ( m_runtime.State().isExitOnComplete && CanAutomationQuit() )
        {
            result.loadRequest = AdvanceScene( perfTestActive, m_runtime.State().isInteractiveRun );
            result.requestQuit = !result.loadRequest.HasLoad();
            result.quitIfLoadFails = true;
            result.restartFrame = true;
            return;
        }
        if ( CanAutomationQuit() )
        {
            m_runtime.State().isTestComplete = true;
        }
        else
        {
            MarkInteractiveRunComplete();
            result.holdInteractive = true;
        }
    };

    if ( hasRequiredSceneGate && requiredSceneComplete && !m_runtime.State().isTestComplete )
    {
        finishInteractiveOrQueueNext( "required_scene_gates" );
        if ( result.restartFrame )
        {
            return result;
        }
    }

    if ( m_runtime.State().targetFrameCount > 0 && !screenshotSaved &&
         m_runtime.State().currentFrame >= m_runtime.State().targetFrameCount )
    {
        const bool frameCountCompletesScene = !hasRequiredSceneGate || requiredSceneComplete;
        if ( !m_runtime.State().isTestComplete )
        {
            result.finishReason = frameCountCompletesScene ? "frame_count" : "required_scene_gates_missing";
        }
        if ( !frameCountCompletesScene )
        {
            // Probe diagnostics belong to the scene gate owner so callers do not
            // reopen its mutable contact/broadphase rows to explain a failure.
            for ( const RunRequiredContactState& contact : RequiredContacts() )
            {
                if ( contact.bodyA < 0 || contact.bodyB < 0 || !contact.touched )
                {
                    fprintf( stderr, "[scene] required_contact missing: %s <-> %s\n", contact.nameA, contact.nameB );
                }
            }
            for ( const RunRequiredBroadphaseXCellsState& cells : RequiredBroadphaseXCells() )
            {
                if ( !cells.activated )
                {
                    fprintf( stderr,
                             "[scene] required_broadphase_x_cells missing: x %d..%d y %d z %d first_missing=%d "
                             "active_cells=%d observed_x=%s%d..%d\n",
                             cells.minCellX,
                             cells.maxCellX,
                             cells.cellY,
                             cells.cellZ,
                             cells.lastMissingCellX,
                             cells.lastActiveCellCount,
                             cells.hasObservedXRange ? "" : "none ",
                             cells.lastObservedMinX,
                             cells.lastObservedMaxX );
                }
            }
            return result;
        }
        finishInteractiveOrQueueNext( result.finishReason ? result.finishReason : "frame_count" );
        if ( result.restartFrame )
        {
            return result;
        }
    }

    if ( !m_runtime.State().isSceneMode && !manualCameraActive && elapsedSeconds > 20.0 )
    {
        result.loadRequest = SceneLoadRequest::Load( m_runtime.State().currentSceneIndex,
                                                     m_runtime.State().isInteractiveRun,
                                                     m_runtime.State().isInteractiveRun,
                                                     m_runtime.State().isInteractiveRun );
        result.restartFrame = true;
        result.restartSimulationTimerAfterLoad = true;
        return result;
    }

    if ( perfTestActive && m_runtime.State().targetFrameCount <= 0 && elapsedSeconds > SCENE_PERF_PASS_SECONDS )
    {
        result.finishReason = "perf_duration";
        result.loadRequest = AdvanceScene( true, m_runtime.State().isInteractiveRun );
        result.restartFrame = true;
        if ( !result.loadRequest.HasLoad() )
        {
            if ( CanAutomationQuit() )
            {
                result.requestQuit = true;
            }
            else
            {
                MarkInteractiveRunComplete();
                result.holdInteractive = true;
            }
        }
        result.quitIfLoadFails = CanAutomationQuit();
    }
    return result;
}


SceneRuntime& SceneController::Runtime()
{
    return m_runtime;
}


const SceneRuntime& SceneController::Runtime() const
{
    return m_runtime;
}
} // namespace Basics
} // namespace SkullbonezCore
