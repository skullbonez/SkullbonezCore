/*
File: SkullbonezSource/Runtime/Scene/SceneRuntime.cpp
Purpose:
  Owns scene queue and scene-run state for the application runtime.

Mental model:
  SceneRuntime is intentionally narrow. It owns queue/index bookkeeping and
  scene-run state, while Run still performs the current object,
  camera, terrain, UI, and renderer side effects around scene loads.

Glossary:
  Scene queue: Ordered list of authored scene paths, where an empty path means
  the generated demo scene.
  Scene-run state: Counters, flags, and overrides that describe the currently
  loaded scene.

Invariants:
  - Queue paths are normalized with forward slashes for comparisons.
  - Generated demo scenes are represented by an empty queue path.
  - Cinematic deck detection is filename-based and must match browser/load
    helpers.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRuntime.h
  - Agentic/Reference/runtime-reference.md
*/
#include "SceneRuntime.h"

#include "../../Physics/PhysicsBodyStore.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

using namespace SkullbonezCore::Basics;

namespace
{
const char* FileNameFromPath( const char* path )
{
    if ( !path )
    {
        return "";
    }

    const char* slash = strrchr( path, '/' );
    const char* backslash = strrchr( path, '\\' );
    const char* separator = slash;
    if ( backslash && ( !separator || backslash > separator ) )
    {
        separator = backslash;
    }
    return separator ? separator + 1 : path;
}

std::string NormalizeScenePath( const std::string& path )
{
    std::string normalized = path;
    std::replace( normalized.begin(), normalized.end(), '\\', '/' );
    return normalized;
}

bool IsCineScenePath( const std::string& path )
{
    const char* name = FileNameFromPath( path.c_str() );
    return strncmp( name, "concept_", 8 ) == 0 || strncmp( name, "cinematic_", 10 ) == 0 ||
           strstr( name, "_cine_" ) != nullptr || strstr( name, "cine_" ) == name;
}
} // namespace


const char* SkullbonezCore::Basics::SceneRuntimeLifecycleEventName( SceneRuntimeLifecycleEvent event )
{
    switch ( event )
    {
    case SceneRuntimeLifecycleEvent::BeforeSceneUnload:
        return "BeforeSceneUnload";
    case SceneRuntimeLifecycleEvent::AfterSceneCleared:
        return "AfterSceneCleared";
    case SceneRuntimeLifecycleEvent::BeforeScenePopulate:
        return "BeforeScenePopulate";
    case SceneRuntimeLifecycleEvent::AfterScenePopulate:
        return "AfterScenePopulate";
    case SceneRuntimeLifecycleEvent::AfterSceneActivated:
        return "AfterSceneActivated";
    case SceneRuntimeLifecycleEvent::None:
    default:
        return "None";
    }
}


void RunSceneState::ResetForLoad( const CinematicRenderConfig& cinematicDefaults )
{
    // Lifetime: This clears per-load runtime state only. Queue position, scene
    // paths, and manual reset counts stay with SceneRuntime/SceneController.
    isScenePhysics = true;
    isSceneText = true;
    targetFrameCount = -1;
    currentFrame = 0;
    solverBallCount = 0;
    solverBoxCount = 0;
    nextSceneObjectId = 1;
    timeScale = 1.0f;
    isFixedStep = false;
    isExitOnComplete = false;
    isTestComplete = false;
    isFinishLogged = false;
    isEditableScene = false;
    hasFlatSlope = false;
    flatBaseY = 0.0f;
    flatSlopeX = 0.0f;
    flatSlopeZ = 0.0f;

    hasCinematicRenderingOverride = false;
    isCinematicRenderingEnabled = false;
    hasCinematicExposure = false;
    cinematicExposure = cinematicDefaults.exposure;
    hasCinematicGamma = false;
    cinematicGamma = cinematicDefaults.gamma;
    cinematicOverrideMask = 0;
    uiCinematicOverrideMask = 0;
    cinematicRender = cinematicDefaults;
}


SkullbonezCore::Physics::PhysicsSceneObjectId RunSceneState::AllocateSceneObjectId()
{
    return AllocateSceneObjectIdRange( 1 );
}


SkullbonezCore::Physics::PhysicsSceneObjectId RunSceneState::AllocateSceneObjectIdRange( int count )
{
    // Invariant: scene object id 0 means "not assigned." Compound creators
    // reserve one contiguous range before appending any child bodies so partial
    // failure cannot interleave another object's replay-facing identity.
    if ( count <= 0 )
    {
        return SkullbonezCore::Physics::PhysicsSceneObjectId{};
    }

    const uint32_t countValue = static_cast<uint32_t>( count );
    const uint32_t maxSceneObjectId = ( std::numeric_limits<uint32_t>::max )();
    if ( nextSceneObjectId == 0 || nextSceneObjectId == maxSceneObjectId ||
         countValue > maxSceneObjectId - nextSceneObjectId )
    {
        throw std::runtime_error( "Scene object id range exhausted." );
    }

    SkullbonezCore::Physics::PhysicsSceneObjectId first;
    first.value = nextSceneObjectId;
    nextSceneObjectId += countValue;
    return first;
}


void RunSceneState::ResetSceneObjectIdCursor( const SkullbonezCore::Physics::PhysicsBodyStore& bodyStore )
{
    // Why: replay restore can trim runtime-spawned bodies, then replay their
    // creation events. Rebase the scene-owned cursor from live body rows so the
    // next spawn receives the same id it had in the original timeline.
    uint32_t nextId = 1;
    const uint32_t maxSceneObjectId = ( std::numeric_limits<uint32_t>::max )();
    for ( const SkullbonezCore::Physics::PhysicsBodyRecord& body : bodyStore.Records() )
    {
        const uint32_t id = body.sceneObjectId.IsValid() ? body.sceneObjectId.value : body.replayBodyId;
        if ( id == maxSceneObjectId )
        {
            nextId = maxSceneObjectId;
            break;
        }
        if ( id != 0 )
        {
            nextId = (std::max)( nextId, id + 1u );
        }
    }
    nextSceneObjectId = nextId;
}


SceneRuntime::SceneRuntime( std::vector<std::string> queue ) : m_queue( std::move( queue ) )
{
}


RunSceneState& SceneRuntime::State()
{
    return m_state;
}


const RunSceneState& SceneRuntime::State() const
{
    return m_state;
}


bool SceneRuntime::HasEntry( int index ) const
{
    return index >= 0 && index < static_cast<int>( m_queue.size() );
}


bool SceneRuntime::HasCurrentEntry() const
{
    return HasEntry( m_state.currentSceneIndex );
}


const std::string* SceneRuntime::CurrentPath() const
{
    return HasCurrentEntry() ? &m_queue[m_state.currentSceneIndex] : nullptr;
}


const std::string& SceneRuntime::PathAt( int index ) const
{
    return m_queue[index];
}


int SceneRuntime::QueueSize() const
{
    return static_cast<int>( m_queue.size() );
}


int SceneRuntime::CurrentIndex() const
{
    return m_state.currentSceneIndex;
}


int SceneRuntime::NextIndex() const
{
    return m_state.currentSceneIndex + 1;
}


const std::vector<std::string>& SceneRuntime::Queue() const
{
    return m_queue;
}


void SceneRuntime::BeginLoad( int index )
{
    m_state.currentSceneIndex = index;
    ++m_state.loadCount;
}


void SceneRuntime::RecordLifecycleEvent( SceneRuntimeLifecycleEvent event )
{
    // Concept: Scene lifecycle names live with scene runtime ownership. Run can
    // still perform broad side effects, but call sites now mark which phase a
    // future owner API should absorb.
    m_lastLifecycleEvent = event;
}


SceneRuntimeLifecycleEvent SceneRuntime::LastLifecycleEvent() const
{
    return m_lastLifecycleEvent;
}


void SceneRuntime::MarkManualReset()
{
    ++m_state.manualResetCount;
}


int SceneRuntime::FindNormalizedPath( const std::string& normalizedPath ) const
{
    for ( int i = 0; i < QueueSize(); ++i )
    {
        if ( NormalizeScenePath( m_queue[i] ) == normalizedPath )
        {
            return i;
        }
    }
    return -1;
}


int SceneRuntime::FindGeneratedDemo() const
{
    for ( int i = 0; i < QueueSize(); ++i )
    {
        if ( m_queue[i].empty() )
        {
            return i;
        }
    }
    return -1;
}


int SceneRuntime::Append( std::string path )
{
    m_queue.push_back( std::move( path ) );
    return QueueSize() - 1;
}


bool SceneRuntime::CurrentQueueIsCinematicDeck() const
{
    if ( !HasCurrentEntry() || m_queue.size() <= 1 )
    {
        return false;
    }
    for ( const std::string& queuedPath : m_queue )
    {
        if ( queuedPath.empty() || !IsCineScenePath( queuedPath ) )
        {
            return false;
        }
    }
    return true;
}


int SceneRuntime::AdjacentQueueIndex( int direction ) const
{
    const int queueCount = QueueSize();
    if ( queueCount <= 0 )
    {
        return -1;
    }
    return ( m_state.currentSceneIndex + ( direction < 0 ? -1 : 1 ) + queueCount ) % queueCount;
}


std::vector<RunRequiredContactState>& SceneRuntime::RequiredContacts()
{
    return m_requiredContacts;
}


const std::vector<RunRequiredContactState>& SceneRuntime::RequiredContacts() const
{
    return m_requiredContacts;
}


std::vector<RunRequiredBroadphaseXCellsState>& SceneRuntime::RequiredBroadphaseXCells()
{
    return m_requiredBroadphaseXCells;
}


const std::vector<RunRequiredBroadphaseXCellsState>& SceneRuntime::RequiredBroadphaseXCells() const
{
    return m_requiredBroadphaseXCells;
}


void SceneRuntime::ClearRequiredAutomationGates()
{
    m_requiredContacts.clear();
    m_requiredBroadphaseXCells.clear();
}
