/*
File: SkullbonezSource/Runtime/Scene/SceneController.cpp
Purpose:
  Implements scene state, physics ownership, lifecycle, and deferred requests.

Summary:
  Scene state, physics sequencing, completion gates, and deferred intent live
  behind one controller. A physics step returns bounded post-step facts for
  presentation consumers instead of mutating render feedback through relays.

Invariants:
  - Scene queue, session, and lifecycle state have one concrete owner.
  - Interactive scene requests cannot bypass the controller-owned ring.
  - Frame completion returns value-only load/quit/hold intent to the process shell.
  - Physics post-step spans borrow fixed-capacity rows only until the next step.

Related:
  - SkullbonezSource/Runtime/Scene/SceneController.h
  - SkullbonezSource/Runtime/Scene/SceneSessionState.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "SceneController.h"
#include "../../Core/SbDiagnosticStore.h"
#include "../Automation/RuntimeValidationHarness.h"

#include "../../Core/FatalError.h"
#include "../../Core/Config.h"
#include "../../Physics/PhysicsEngine.h"

#include <cstring>
#include <utility>

namespace SkullbonezCore
{
namespace Runtime
{
namespace
{
constexpr double SCENE_PERF_PASS_SECONDS = 2.0;
} // namespace

SceneController::SceneController( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics )
    : m_resultDiagnostics( resultDiagnostics ), m_world( resultDiagnostics )
{
}


void SceneController::EnterInteractiveRun()
{
    State().isInteractiveRun = true;
    State().isExitOnComplete = false;
}


bool SceneController::CanAutomationQuit() const
{
    return !State().isInteractiveRun;
}


void SceneController::MarkInteractiveRunComplete()
{
    State().isTestComplete = true;
    State().isExitOnComplete = false;
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


SceneController::SceneController( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics,
                                  std::vector<std::string> queue )
    : SceneSession( std::move( queue ) ), m_resultDiagnostics( resultDiagnostics ), m_world( resultDiagnostics )
{
}

SceneWorld& SceneController::Scene()
{
    return m_world;
}


const SceneWorld& SceneController::Scene() const
{
    return m_world;
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
                  SceneRuntimeLifecycleEventName( event ), entityCount, bodyCount, colliderCount );
    }

    SceneSession::RecordLifecycleEvent( event, consumers );
}

void SceneController::SubmitLoadBrowserIndex( int index )
{
    SceneRequest request;
    request.type = SceneRequestType::LoadBrowserIndex;
    request.index = index;
    const SkullbonezCore::Core::SbResult result = m_requests.Submit( m_resultDiagnostics, request );

    if ( !result.Ok() )
    {
        SB_FATAL( result.ErrorOwner(), "%s", result.ErrorMessage() );
    }
}


void SceneController::SubmitLoadDemoScene()
{
    SceneRequest request;
    request.type = SceneRequestType::LoadDemoScene;
    const SkullbonezCore::Core::SbResult result = m_requests.Submit( m_resultDiagnostics, request );

    if ( !result.Ok() )
    {
        SB_FATAL( result.ErrorOwner(), "%s", result.ErrorMessage() );
    }
}


void SceneController::SubmitResetCurrentScene( bool preserveUIState, bool suppressExitOnComplete, bool preserveRuntimeState )
{
    SceneRequest request;
    request.type = SceneRequestType::ResetCurrentScene;
    request.preserveUIState = preserveUIState;
    request.suppressExitOnComplete = suppressExitOnComplete;
    request.preserveRuntimeState = preserveRuntimeState;
    const SkullbonezCore::Core::SbResult result = m_requests.Submit( m_resultDiagnostics, request );

    if ( !result.Ok() )
    {
        SB_FATAL( result.ErrorOwner(), "%s", result.ErrorMessage() );
    }
}


SkullbonezCore::Core::SbResult SceneController::SubmitCreateScene( const char* requestedName )
{
    const std::size_t nameLength = requestedName ? strnlen_s( requestedName, SCENE_REQUEST_TEXT_CAPACITY ) : 0;

    if ( requestedName && nameLength >= SCENE_REQUEST_TEXT_CAPACITY )
    {
        return m_resultDiagnostics.Failure( "Runtime/SceneController",
                                            "Scene name exceeds the fixed %d-byte request payload",
                                            SCENE_REQUEST_TEXT_CAPACITY - 1 );
    }

    SceneRequest request;
    request.type = SceneRequestType::CreateScene;

    if ( requestedName )
    {
        strcpy_s( request.text, requestedName );
    }

    return m_requests.Submit( m_resultDiagnostics, request );
}


void SceneController::SubmitSaveCurrentDefaults()
{
    SceneRequest request;
    request.type = SceneRequestType::SaveCurrentDefaults;
    const SkullbonezCore::Core::SbResult result = m_requests.Submit( m_resultDiagnostics, request );

    if ( !result.Ok() )
    {
        SB_FATAL( result.ErrorOwner(), "%s", result.ErrorMessage() );
    }
}


SceneRequestBatch SceneController::TakePendingRequests()
{
    return m_requests.TakePending();
}


bool SceneController::HasPendingTransition() const
{
    return m_requests.HasTransition();
}


SceneFrameAdvanceResult SceneController::AdvanceFrame( const SceneAutomationGateStatus& automationGates, bool proceedAllowed,
                                                       bool perfTestActive, bool screenshotSaved, bool manualCameraActive,
                                                       double elapsedSeconds )
{
    SceneFrameAdvanceResult result;

    if ( !proceedAllowed )
    {
        return result;
    }

    ++State().currentFrame;
    const bool hasRequiredSceneGate = automationGates.hasRequirements;
    const bool requiredSceneComplete = automationGates.complete;

    const auto finishInteractiveOrQueueNext = [&]( const char* reason )
    {
        result.finishReason = reason;

        if ( State().isExitOnComplete && CanAutomationQuit() )
        {
            result.loadRequest = AdvanceScene( perfTestActive, State().isInteractiveRun );
            result.requestQuit = !result.loadRequest.HasLoad();
            result.quitIfLoadFails = true;
            result.restartFrame = true;
            return;
        }

        if ( CanAutomationQuit() )
        {
            State().isTestComplete = true;
        }
        else
        {
            MarkInteractiveRunComplete();
            result.holdInteractive = true;
        }
    };

    if ( hasRequiredSceneGate && requiredSceneComplete && !State().isTestComplete )
    {
        finishInteractiveOrQueueNext( "required_scene_gates" );

        if ( result.restartFrame )
        {
            return result;
        }
    }

    if ( State().targetFrameCount > 0 && !screenshotSaved && State().currentFrame >= State().targetFrameCount )
    {
        const bool frameCountCompletesScene = !hasRequiredSceneGate || requiredSceneComplete;

        if ( !State().isTestComplete )
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

    if ( !State().isSceneMode && !manualCameraActive && elapsedSeconds > 20.0 )
    {
        result.loadRequest = SceneLoadRequest::Load( State().currentSceneIndex, State().isInteractiveRun,
                                                     State().isInteractiveRun, State().isInteractiveRun );

        result.restartFrame = true;
        result.restartSimulationTimerAfterLoad = true;
        return result;
    }

    if ( perfTestActive && State().targetFrameCount <= 0 && elapsedSeconds > SCENE_PERF_PASS_SECONDS )
    {
        result.finishReason = "perf_duration";
        result.loadRequest = AdvanceScene( true, State().isInteractiveRun );
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
} // namespace Runtime
} // namespace SkullbonezCore
