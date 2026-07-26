/*
File: SceneRequestExecution.cpp
Purpose:
  Executes the SceneController-owned fixed request batch at the frame checkpoint.

Summary:
  SceneController accepts owner-specific requests during input, then drains the
  batch once with explicit cold-load dependencies. Completed request values
  return to the Replay owner only after lifecycle reactions finish.

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
  - Only successfully completed requests enter the returned fixed batch; Replay
    converts their domain values to stable wire codes outside the transaction.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "SceneController.h"
#include "SceneLoadTransaction.h"
#include "../Automation/RuntimeValidationHarness.h"
#include "../Tools/RuntimeTools.h"
#include "SceneRuntimeCreate.h"
#include "../../Core/FatalError.h"
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


bool SceneController::ExecutePending( SceneLoadTransaction& transaction, SkullbonezCore::Core::EngineConfig& config,
                                      RunLaunchOptions& launchOptions,
                                      const SkullbonezCore::Core::CinematicRenderConfig& defaultCinematicRender,
                                      const RunStartupState& startup, Assets::AssetSystem& assets,
                                      Threading::WorkerPool& workerPool, DiagnosticsRuntime& diagnosticsRuntime,
                                      Rendering::Dx12FrameOwner* renderFrame,
                                      Rendering::Dx12ResourceBuilder* renderResources, RuntimeRenderer& renderer )
{
    SceneRequestBatch completedRequests;
    SceneController& m_sceneController = *this;
    const CameraControlState& camera = transaction.m_outputs.camera;
    const SceneLoadNavigationState& navigation = transaction.m_outputs.navigation;
    const auto executeSceneLoadRequest = [&]( const SceneLoadRequest& request )
    {

        if ( !request.accepted )
        {
            return false;
        }

        return transaction
            .Load( m_sceneController, request, config, launchOptions, defaultCinematicRender, startup, assets, workerPool,
                   diagnosticsRuntime, renderFrame, renderResources, renderer )
            .ok;
    };

    const SceneRequestBatch batch = m_sceneController.TakePendingRequests();

    if ( batch.rejectedTransitionCount > 0 )
    {
        std::fprintf( stderr, "Runtime/SceneController: rejected %zu additional same-frame scene transition(s)\n",
                      batch.rejectedTransitionCount );

        std::fflush( stderr );
    }

    for ( std::size_t requestIndex = 0; requestIndex < batch.count; ++requestIndex )
    {
        const SceneRequest& request = batch.requests[requestIndex];
        bool accepted = false;

        switch ( request.type )
        {
        case SceneRequestType::LoadBrowserIndex:
            accepted = executeSceneLoadRequest( navigation.LoadSceneFromBrowserIndex( request.index, m_sceneController.Runtime() ) );

            break;
        case SceneRequestType::LoadDemoScene:
            accepted = executeSceneLoadRequest( navigation.LoadDemoScene( m_sceneController.Runtime() ) );
            break;
        case SceneRequestType::ResetCurrentScene:
            accepted = executeSceneLoadRequest( m_sceneController.ResetCurrentScene( request.preserveUIState,
                                                                                     request.suppressExitOnComplete,
                                                                                     request.preserveRuntimeState ) );

            break;
        case SceneRequestType::CreateScene:
        {
            const SceneLoadRequest createRequest = CreateSceneFromUI( SceneRuntimeCreateContext { m_sceneController },
                                                                      request.text );

            accepted = executeSceneLoadRequest( createRequest );

            // Why: file creation can succeed before a later load phase fails.
            // The UI still needs to discover that durable authored file, while
            // replay records the create action only after the load completes.
            transaction.m_outputs.refreshSceneBrowser = createRequest.accepted;
            break;
        }
        case SceneRequestType::SaveCurrentDefaults:
        {
            const OverlayDebugState& presentationState = transaction.PresentationForFollowingRequest( transaction.m_outputs
                                                                                                          .presentation,
                                                                                                      LifecyclePacket() );

            const SceneLoadNavigationState& currentNavigation = transaction.NavigationForFollowingRequest( transaction.m_outputs.navigation );

            const SkullbonezCore::Core::SbResult saveResult = m_sceneController.SaveCurrentDefaults( SceneDefaultsSaveView { presentationState, renderer, camera, currentNavigation.overrides } );

            if ( !saveResult.ok )
            {
                std::fprintf( stderr, "[%s] %s\n", saveResult.error.owner, saveResult.error.message );
                std::fflush( stderr );
            }

            accepted = saveResult.ok;
            break;
        }
        }

        // Invariant: replay observes completed owner work. Rejected browser
        // indices, failed loads, invalid create names, and failed writes leave
        // no serialized action that a restore could mistake for applied state.

        if ( accepted )
        {

            if ( completedRequests.count >= SCENE_REQUEST_QUEUE_CAPACITY )
            {
                SB_FATAL( "Runtime/SceneController", "Fixed completed scene-request capacity exhausted." );
            }

            completedRequests.requests[completedRequests.count++] = request;
        }

        if ( !SceneRequestBatchContinuesAfter( request.type, accepted ) )
        {

            // Hazard: load/create teardown may already have cleared the old
            // world before a recoverable failure. Never let a later save or
            // owner action consume that incomplete replacement topology.
            break;
        }
    }

    transaction.m_outputs.completedRequests = completedRequests;
    transaction.FinishLoadPhase();
    return batch.count > 0;
}
