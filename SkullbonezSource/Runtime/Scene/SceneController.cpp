/*
File: SkullbonezSource/Runtime/Scene/SceneController.cpp
Purpose:
  Implements scene state, physics ownership, lifecycle, and deferred requests.

Summary:
  Scene state, physics sequencing, completion gates, and deferred intent live
  behind one controller. A physics step returns bounded post-step facts for
  presentation consumers instead of mutating render feedback through relays.

Glossary:
  Scene runtime: Mutable per-scene queue, completion, and automation state.
  Scene queue: Ordered list of authored scenes or demo entries to run.
  Request batch: Ordered fixed-capacity copy consumed at one frame checkpoint.
  Post-step output: Borrowed bounded physics facts consumed before the next step.
  Proceed policy: One post-input decision shared by every late-frame consumer
    that can advance scene work.

Invariants:
  - Controller accessors must preserve the existing SceneRuntime semantics.
  - Interactive scene requests cannot bypass the controller-owned ring.
  - Frame completion returns value-only load/quit/hold intent to the process shell.
  - Physics post-step spans borrow fixed-capacity rows only until the next step.

Related:
  - SkullbonezSource/Runtime/Scene/SceneController.h
  - SkullbonezSource/Runtime/Scene/SceneRuntime.cpp
*/
#include "SceneController.h"
#include "../Automation/RuntimeValidationHarness.h"

#include "../../Core/FatalError.h"
#include "../../Core/Config.h"
#include "../../Core/Log.h"
#include "../../Core/WorkerPool.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsDiagnosticsSink.h"
#include "../../Physics/PhysicsWorldForces.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace SkullbonezCore
{
namespace Runtime
{
namespace
{
constexpr double SCENE_PERF_PASS_SECONDS = 2.0;
} // namespace

SceneController::SceneController()
{
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


SceneFrameProceedPolicy SceneController::BuildFrameProceedPolicy( bool stepRequested ) const
{
    // Invariant: the lock can be bypassed only by the step edge sampled for
    // this frame. Callers consume proceedAllowed instead of re-deriving it.
    return ResolveSceneFrameProceedPolicy( m_crossScenePauseLocked, stepRequested );
}


SceneController::SceneController( std::vector<std::string> queue ) : m_runtime( std::move( queue ) )
{
}


SceneSessionState& SceneController::State()
{
    return m_runtime.State();
}


const SceneSessionState& SceneController::State() const
{
    return m_runtime.State();
}


SceneWorld& SceneController::Scene()
{
    return m_world;
}


const SceneWorld& SceneController::Scene() const
{
    return m_world;
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


void SceneController::BeginLoadAttempt( int index, const SceneLifecycleBeginPolicy& lifecyclePolicy )
{
    m_runtime.BeginLoadAttempt( index, lifecyclePolicy );
}


void SceneController::BeginLoad( int index )
{
    m_runtime.BeginLoad( index );
}


void SceneController::RecordLifecycleEvent( SceneRuntimeLifecycleEvent event, SceneLifecycleConsumerMask consumers )
{
    const int entityCount = m_world.Entities().Count();
    const int bodyCount = Physics::PhysicsEngine::ReadBodies( m_world.Physics() ).Count();
    const int colliderCount = Physics::PhysicsEngine::ReadColliders( m_world.Physics() ).Count();
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


const SceneLifecyclePacket& SceneController::LifecyclePacket() const
{
    return m_runtime.LifecyclePacket();
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
    const SkullbonezCore::Core::SbResult result = m_requests.Submit( request );
    if ( !result.ok )
    {
        SB_FATAL( result.error.owner, "%s", result.error.message );
    }
}


void SceneController::SubmitLoadDemoScene()
{
    SceneRequest request;
    request.type = SceneRequestType::LoadDemoScene;
    const SkullbonezCore::Core::SbResult result = m_requests.Submit( request );
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
    const SkullbonezCore::Core::SbResult result = m_requests.Submit( request );
    if ( !result.ok )
    {
        SB_FATAL( result.error.owner, "%s", result.error.message );
    }
}


SkullbonezCore::Core::SbResult SceneController::SubmitCreateScene( const char* requestedName )
{
    const std::size_t nameLength = requestedName ? strnlen_s( requestedName, SCENE_REQUEST_TEXT_CAPACITY ) : 0;
    if ( requestedName && nameLength >= SCENE_REQUEST_TEXT_CAPACITY )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Runtime/SceneController",
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
    const SkullbonezCore::Core::SbResult result = m_requests.Submit( request );
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


SceneFrameAdvanceResult SceneController::AdvanceFrame( const SceneAutomationGateStatus& automationGates,
                                                       bool proceedAllowed,
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
    const bool hasRequiredSceneGate = automationGates.hasRequirements;
    const bool requiredSceneComplete = automationGates.complete;

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
            result.reportMissingRequirements = true;
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
} // namespace Runtime
} // namespace SkullbonezCore
