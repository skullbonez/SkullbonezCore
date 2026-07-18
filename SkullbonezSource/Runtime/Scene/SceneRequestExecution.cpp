/*
File: SceneRequestExecution.cpp
Purpose:
  Executes the SceneController-owned fixed request batch at the frame checkpoint.

Summary:
  SceneController accepts owner-specific requests during input, then drains the
  batch once with explicit cold-load dependencies. Replay records only work
  that the concrete scene operation accepted and completed successfully.

Glossary:
  Scene request batch: Fixed owner queue drained once at the post-input frame
    checkpoint in original submission order.
  Transition request: Load, reset, or create work; only the first accepted
    transition in one batch may mutate scene lifetime.
  Owner event: Stable replay record emitted only after the concrete scene
    operation completes successfully.
  Lane R result: Recoverable scene/load failure that stops the batch without
    recording rejected work as successful.

Invariants:
  - Requests after the first accepted transition are counted as rejected and
    cannot mutate the replacement scene.
  - Replay receives only successfully completed operations and stable wire
    codes; raw SceneRequestType ordinals are never serialized.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "SceneController.h"
#include "../RuntimeOverlayDiagnostics.h"
#include "../RuntimeValidationHarness.h"
#include "../InputFrame.h"
#include "../Tools/RuntimeTools.h"
#include "SceneRuntimeCreate.h"
#include "../../UI/UI.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;


bool SceneController::ExecutePending( const SceneLoadPolicyInputs& policy,
                                      const SceneLoadHostParticipants& host,
                                      const SceneLoadInteractionParticipants& interaction,
                                      const SceneLoadPresentationParticipants& presentation,
                                      SceneLoadConsumerOutputs& consumerOutputs )
{
    SceneController& m_sceneController = *this;
    const auto executeSceneLoadRequest = [&]( const SceneLoadRequest& request )
    {
        if ( !request.accepted )
        {
            return false;
        }
        return m_sceneController.Load( request, policy, host, interaction, presentation, consumerOutputs ).ok;
    };
    const SceneRequestBatch batch = m_sceneController.TakePendingRequests();
    if ( batch.rejectedTransitionCount > 0 )
    {
        std::fprintf( stderr,
                      "Runtime/SceneController: rejected %zu additional same-frame scene transition(s)\n",
                      batch.rejectedTransitionCount );
        std::fflush( stderr );
    }
    for ( std::size_t requestIndex = 0; requestIndex < batch.count; ++requestIndex )
    {
        const SceneRequest& request = batch.requests[requestIndex];
        bool accepted = false;
        ReplayOwnerEventCode eventCode = ReplayOwnerEventCode::SceneLoadBrowserIndex;
        int eventIndex = request.index;
        const char* eventText = nullptr;

        switch ( request.type )
        {
        case SceneRequestType::LoadBrowserIndex:
            eventCode = ReplayOwnerEventCode::SceneLoadBrowserIndex;
            accepted = executeSceneLoadRequest(
                interaction.navigation.LoadSceneFromBrowserIndex( request.index, m_sceneController.Runtime() ) );
            break;
        case SceneRequestType::LoadDemoScene:
            eventCode = ReplayOwnerEventCode::SceneLoadDemo;
            accepted = executeSceneLoadRequest( interaction.navigation.LoadDemoScene( m_sceneController.Runtime() ) );
            break;
        case SceneRequestType::ResetCurrentScene:
            eventCode = ReplayOwnerEventCode::SceneReset;
            accepted = executeSceneLoadRequest( m_sceneController.ResetCurrentScene( request.preserveUIState,
                                                                                     request.suppressExitOnComplete,
                                                                                     request.preserveRuntimeState ) );
            break;
        case SceneRequestType::CreateScene:
        {
            eventCode = ReplayOwnerEventCode::SceneCreate;
            eventText = request.text;
            const SceneLoadRequest createRequest =
                CreateSceneFromUI( SceneRuntimeCreateContext{ m_sceneController }, request.text );
            accepted = executeSceneLoadRequest( createRequest );
            // Why: file creation can succeed before a later load phase fails.
            // The UI still needs to discover that durable authored file, while
            // replay records the create action only after the load completes.
            consumerOutputs.refreshSceneBrowser = createRequest.accepted;
            break;
        }
        case SceneRequestType::SaveCurrentDefaults:
            eventCode = ReplayOwnerEventCode::SceneSaveDefaults;
            {
                const RunDebugState presentationState = presentation.overlays.PresentationSnapshot();
                const SceneLoadNavigationState& currentNavigation =
                    SceneNavigationForFollowingRequest( interaction.navigation, consumerOutputs );
                const SkullbonezCore::Core::SbResult saveResult =
                    m_sceneController.SaveCurrentDefaults( SceneDefaultsSaveView{ presentationState,
                                                                                  presentation.renderer,
                                                                                  interaction.camera,
                                                                                  currentNavigation.overrides } );
                if ( !saveResult.ok )
                {
                    std::fprintf( stderr, "[%s] %s\n", saveResult.error.owner, saveResult.error.message );
                    std::fflush( stderr );
                }
                accepted = saveResult.ok;
                if ( accepted )
                {
                    // Invariant: only the completed authored write advances
                    // the editor's clean cursor; a failed Lane-R save remains dirty.
                    interaction.runtimeTools.Editor().history.MarkClean();
                }
                break;
            }
        }

        // Invariant: replay observes completed owner work. Rejected browser
        // indices, failed loads, invalid create names, and failed writes leave
        // no serialized action that a restore could mistake for applied state.
        if ( accepted )
        {
            presentation.replayRuntime.SubmitEvent( ReplayEventCommandOperations::BuildCommand(
                ReplayEventKind::OwnerAction,
                0,
                true,
                ReplaySceneRequestFlags( request ),
                static_cast<int32_t>( eventCode ),
                eventIndex,
                0,
                0,
                0,
                eventText ? eventText : ReplayOwnerEventName( eventCode ) ) );
        }
        if ( !SceneRequestBatchContinuesAfter( request.type, accepted ) )
        {
            // Hazard: load/create teardown may already have cleared the old
            // world before a recoverable failure. Never let a later save or
            // owner action consume that incomplete replacement topology.
            break;
        }
    }
    return batch.count > 0;
}
