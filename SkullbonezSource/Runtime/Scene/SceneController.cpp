/*
File: SkullbonezSource/Runtime/Scene/SceneController.cpp
Purpose:
  Implements scene state, physics ownership, navigation, and deferred requests.

Mental model:
  Scene state, physics, and deferred intent live together here. Run still
  executes the returned request batch while deeper scene-loading side effects
  move out of RunScene.cpp during lifecycle extraction C1.

Glossary:
  Scene runtime: Mutable per-scene queue, completion, and automation state.
  Scene queue: Ordered list of authored scenes or demo entries to run.
  Request batch: Ordered fixed-capacity copy consumed at one frame checkpoint.

Invariants:
  - Controller accessors must preserve the existing SceneRuntime semantics.
  - Interactive scene requests cannot bypass the controller-owned ring.
  - No scene load side effects live here yet; RunScene.cpp still applies them.

Related:
  - SkullbonezSource/Runtime/Scene/SceneController.h
  - SkullbonezSource/Runtime/Scene/SceneRuntime.cpp
*/
#include "SceneController.h"

#include "../../Core/FatalError.h"
#include "../../GameObjects/GameModelCollection.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsEngineStoreQueries.h"

#include <cstring>
#include <utility>

namespace SkullbonezCore
{
namespace Basics
{
SceneController::SceneController( std::vector<std::string> queue ) : m_runtime( std::move( queue ) )
{
}


bool SceneController::TrimForReplayRestore( GameObjects::GameModelCollection& presentations,
                                            Physics::PhysicsEngine& physics,
                                            int bodyCount )
{
    const int liveBodyCount = Physics::PhysicsEngineStoreQueries::BodyStore( physics ).Count();
    const int liveColliderCount = Physics::PhysicsEngineStoreQueries::Colliders( physics ).Count();
    const uint32_t authoredBodyCount = physics.AuthoredBodyDescriptorCount().value;
    if ( bodyCount < 0 || bodyCount > liveBodyCount || static_cast<uint32_t>( bodyCount ) > authoredBodyCount ||
         !presentations.CanTrimPresentationRowsForSceneRestore( bodyCount ) || bodyCount > m_entities.Count() )
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
    if ( !physics.TrimBodiesToCount( bodies ) ||
         ( liveColliderCount > bodyCount && !physics.TrimCollidersToCount( colliders ) ) ||
         !physics.TrimAuthoredBodyDescriptorsToCount( authored ) ||
         !presentations.TrimPresentationRowsForSceneRestore( bodyCount ) || !m_entities.TrimToCount( bodyCount ) )
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


void SceneController::RecordLifecycleEvent( SceneRuntimeLifecycleEvent event )
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
    m_runtime.RecordLifecycleEvent( event );
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


void SceneController::UpdateRequiredContacts( SkullbonezCore::GameObjects::GameModelCollection& models,
                                              float contactEpsilon )
{
    m_runtime.UpdateRequiredContacts( models, contactEpsilon );
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
