/*
File: SkullbonezSource/Runtime/Scene/SceneSessionState.cpp
Purpose:
  Implements per-scene session state and queue-path policy.

Summary:
  SceneSessionState owns reset, identity-allocation, and persistence views.
  Stateless path helpers normalize controller-owned scene queue entries.

Glossary:
  Scene-run state: Counters, flags, and overrides that describe the currently
    loaded scene.

Invariants:
  - Queue paths are normalized with forward slashes for comparisons.
  - Scene object id 0 is reserved as "not assigned"; live allocations must never
    wrap or cross the uint32 id ceiling.

Related:
  - SkullbonezSource/Runtime/Scene/SceneSessionState.h
  - SkullbonezSource/Runtime/Scene/SceneController.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/engine-glossary.md
*/
#include "SceneSessionState.h"

#include "../../Core/FatalError.h"
#include "../../Physics/PhysicsBodyStore.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Physics;

namespace
{
bool IsCineScenePath( const std::string& path )
{
    const char* name = SceneFileNameFromPath( path.c_str() );
    return strncmp( name, "concept_", 8 ) == 0 || strncmp( name, "cinematic_", 10 ) == 0 ||
           strstr( name, "_cine_" ) != nullptr || strstr( name, "cine_" ) == name;
}
} // namespace


SkullbonezCore::GameObjects::SceneSessionSaveState SceneSessionState::GetSaveState() const
{
    return { isScenePhysics, isSceneText, isEditableScene, isFixedStep, hasFlatSlope, flatBaseY, flatSlopeX, flatSlopeZ };
}

const char* SkullbonezCore::Runtime::SceneFileNameFromPath( const char* path )
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


std::string SkullbonezCore::Runtime::NormalizeSceneQueuePath( const std::string& path )
{
    std::string normalized = path;
    std::replace( normalized.begin(), normalized.end(), '\\', '/' );
    return normalized;
}


const char* SkullbonezCore::Runtime::SceneRuntimeLifecycleEventName( SceneRuntimeLifecycleEvent event )
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


void SceneSessionState::ResetForLoad( const SkullbonezCore::Core::CinematicRenderConfig& cinematicDefaults )
{
    // Lifetime: This clears per-load runtime state only. Queue position, scene
    // paths, and manual reset counts stay with the enclosing SceneSession.
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


SkullbonezCore::Physics::PhysicsSceneObjectId SceneSessionState::AllocateSceneObjectId()
{
    return AllocateSceneObjectIdRange( 1 );
}


SkullbonezCore::Physics::PhysicsSceneObjectId SceneSessionState::AllocateSceneObjectIdRange( int count )
{
    // Invariant: scene object id 0 means "not assigned." Compound creators
    // reserve one contiguous range before appending any child bodies so partial
    // failure cannot interleave another object's replay-facing identity.
    if ( count <= 0 )
    {
        return SkullbonezCore::Physics::PhysicsSceneObjectId {};
    }

    const uint32_t countValue = static_cast<uint32_t>( count );
    const uint32_t maxSceneObjectId = ( std::numeric_limits<uint32_t>::max )();

    if ( nextSceneObjectId == 0 || nextSceneObjectId == maxSceneObjectId ||
         countValue > maxSceneObjectId - nextSceneObjectId )
    {
        SB_FATAL( "SceneSessionState", "Scene object id range exhausted. next=%u requested=%u max=%u", nextSceneObjectId,
                  countValue, maxSceneObjectId );
    }

    SkullbonezCore::Physics::PhysicsSceneObjectId first;
    first.value = nextSceneObjectId;
    nextSceneObjectId += countValue;
    return first;
}


void SceneSessionState::ResetSceneObjectIdCursor( const SkullbonezCore::Physics::PhysicsBodyStore& bodyStore )
{
    // Why: replay restore can trim runtime-spawned bodies, then replay their
    // creation events. Rebase the scene-owned cursor from live body rows so the
    // next spawn receives the same id it had in the original timeline.
    uint32_t nextId = 1;
    const uint32_t maxSceneObjectId = ( std::numeric_limits<uint32_t>::max )();

    for ( const SkullbonezCore::Physics::PhysicsBodyRecord& body : bodyStore.Records() )
    {
        const uint32_t id = body.sceneObjectId.value;

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

SceneSession::SceneSession( std::vector<std::string> queue ) : m_queue( std::move( queue ) )
{
}

SceneSessionState& SceneSession::State()
{
    return m_state;
}

const SceneSessionState& SceneSession::State() const
{
    return m_state;
}

bool SceneSession::HasEntry( int index ) const
{
    return index >= 0 && index < static_cast<int>( m_queue.size() );
}

bool SceneSession::HasCurrentEntry() const
{
    return HasEntry( m_state.currentSceneIndex );
}

const std::string* SceneSession::CurrentPath() const
{
    return HasCurrentEntry() ? &m_queue[m_state.currentSceneIndex] : nullptr;
}

const std::string& SceneSession::PathAt( int index ) const
{
    return m_queue[index];
}

int SceneSession::QueueSize() const
{
    return static_cast<int>( m_queue.size() );
}

int SceneSession::CurrentIndex() const
{
    return m_state.currentSceneIndex;
}

int SceneSession::NextIndex() const
{
    return m_state.currentSceneIndex + 1;
}


void SceneSession::BeginLoadAttempt( int index, const SceneLifecycleBeginPolicy& lifecyclePolicy )
{
    // Hazard: generation zero is the observer sentinel. Wrapping would make a
    // real load invisible and could suppress every once-per-generation reset.
    if ( m_lifecyclePacket.generation == UINT64_MAX )
    {
        SB_FATAL( "Runtime/SceneSession", "Scene lifecycle generation exhausted." );
    }

    ++m_lifecyclePacket.generation;
    m_lifecyclePacket.event = SceneRuntimeLifecycleEvent::None;
    m_lifecyclePacket.policy = lifecyclePolicy;
    m_lifecyclePacket.sceneIndex = index;
    m_lifecyclePacket.sceneMode = false;
    m_lastLifecycleEvent = SceneRuntimeLifecycleEvent::None;
}

void SceneSession::BeginLoad( int index )
{
    m_state.currentSceneIndex = index;
    ++m_state.loadCount;
}

void SceneSession::RecordLifecycleEvent( SceneRuntimeLifecycleEvent event, SceneLifecycleConsumerMask consumers )
{
    // Hazard: accepting a skipped or repeated phase would publish plausible
    // but false progress to every generation observer. A retry must begin a new
    // generation before it can emit BeforeSceneUnload again.
    if ( !SceneRuntimeLifecycleTransitionValid( m_lastLifecycleEvent, event ) )
    {
        SB_FATAL( "Runtime/SceneSession", "Invalid scene lifecycle transition. previous=%s next=%s",
                  SceneRuntimeLifecycleEventName( m_lastLifecycleEvent ), SceneRuntimeLifecycleEventName( event ) );
    }

    const SceneLifecycleConsumerMask requiredConsumers = SceneLifecycleRequiredConsumers( event );

    if ( consumers != requiredConsumers )
    {
        SB_FATAL( "Runtime/SceneSession", "Scene lifecycle consumer mismatch. phase=%s expected=0x%X actual=0x%X",
                  SceneRuntimeLifecycleEventName( event ), requiredConsumers, consumers );
    }

    m_lastLifecycleEvent = event;
    m_lifecyclePacket.event = event;
    m_lifecyclePacket.sceneMode = m_state.isSceneMode;
}

const SceneLifecyclePacket& SceneSession::LifecyclePacket() const
{
    return m_lifecyclePacket;
}

void SceneSession::MarkManualReset()
{
    ++m_state.manualResetCount;
}

int SceneSession::FindNormalizedPath( const std::string& normalizedPath ) const
{
    for ( int i = 0; i < QueueSize(); ++i )
    {
        if ( NormalizeSceneQueuePath( m_queue[i] ) == normalizedPath )
        {
            return i;
        }
    }

    return -1;
}

int SceneSession::FindGeneratedDemo() const
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

int SceneSession::Append( std::string path )
{
    m_queue.push_back( std::move( path ) );
    return QueueSize() - 1;
}

bool SceneSession::CurrentQueueIsCinematicDeck() const
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

int SceneSession::AdjacentQueueIndex( int direction ) const
{
    const int queueCount = QueueSize();

    if ( queueCount <= 0 )
    {
        return -1;
    }

    return ( m_state.currentSceneIndex + ( direction < 0 ? -1 : 1 ) + queueCount ) % queueCount;
}
