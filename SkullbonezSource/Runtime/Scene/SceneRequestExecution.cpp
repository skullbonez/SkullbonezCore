/*
File: SceneRequestExecution.cpp
Purpose:
  Executes the SceneController-owned fixed request batch at the frame checkpoint.

Mental model:
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
#include "../RunInternal.h"
#include "../InputFrame.h"
#include "SceneRuntimeCreate.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;


bool SceneController::ExecutePending( EngineConfig& m_config,
                                      RunLaunchOptions& m_launchOptions,
                                      const CinematicRenderConfig& m_defaultCinematicRender,
                                      const RunStartupState& m_startup,
                                      DiagnosticsRuntime& m_diagnosticsRuntime,
                                      RunRuntimeSettings& m_runtimeSettings,
                                      RunTimerState& m_timers,
                                      SkullbonezCore::Assets::AssetSystem& assets,
                                      Threading::WorkerPool& workerPool,
                                      Window& window,
                                      InputRouter& m_inputRouter,
                                      RuntimeInteractionController& m_interaction,
                                      RunCameraState& m_camera,
                                      AttachedCameraState& attachedCamera,
                                      SimulationSystem& m_simulation,
                                      ReplayRuntime& m_replayRuntime,
                                      SkullbonezCore::Runtime::Audio::ContactAudioService& m_contactAudio,
                                      SkullbonezCore::UI::InGameUI& m_UI,
                                      RunDebugState& m_debug,
                                      GraphicsStressController& m_graphicsStress,
                                      RuntimeTools& m_runtimeTools,
                                      Physics::PhysicsDebugVisualizer& m_physicsDebugVisualizer,
                                      const RuntimeRenderBackendView& m_renderBackendView,
                                      RuntimeRenderer& m_renderer,
                                      int& sPerfPass )
{
    SceneController& m_sceneController = *this;
    const auto executeSceneLoadRequest = [&]( const SceneLoadRequest& request )
    {
        if ( !request.accepted )
        {
            return false;
        }
        return m_sceneController
            .Load( request,
                   m_config,
                   m_launchOptions,
                   m_defaultCinematicRender,
                   m_startup,
                   m_diagnosticsRuntime,
                   m_runtimeSettings,
                   m_timers,
                   assets,
                   workerPool,
                   window,
                   m_inputRouter,
                   m_interaction,
                   m_camera,
                   attachedCamera,
                   m_simulation,
                   m_replayRuntime,
                   m_contactAudio,
                   m_UI,
                   m_debug,
                   m_graphicsStress,
                   m_runtimeTools,
                   m_physicsDebugVisualizer,
                   m_renderBackendView,
                   m_renderer,
                   sPerfPass )
            .ok;
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
            accepted = executeSceneLoadRequest( m_sceneController.LoadSceneFromBrowserIndex( request.index ) );
            break;
        case SceneRequestType::LoadDemoScene:
            eventCode = ReplayOwnerEventCode::SceneLoadDemo;
            accepted = executeSceneLoadRequest( m_sceneController.LoadDemoSceneFromUI() );
            break;
        case SceneRequestType::ResetCurrentScene:
            eventCode = ReplayOwnerEventCode::SceneReset;
            accepted = executeSceneLoadRequest( m_sceneController.ResetCurrentScene( request.preserveUIState,
                                                                                     request.suppressExitOnComplete,
                                                                                     request.preserveRuntimeState ) );
            break;
        case SceneRequestType::CreateScene:
            eventCode = ReplayOwnerEventCode::SceneCreate;
            eventText = request.text;
            accepted = executeSceneLoadRequest(
                CreateSceneFromUI( SceneRuntimeCreateContext{ m_sceneController, m_sceneController.Browser() },
                                   request.text ) );
            break;
        case SceneRequestType::SaveCurrentDefaults:
            eventCode = ReplayOwnerEventCode::SceneSaveDefaults;
            {
                const SbResult saveResult = m_sceneController.SaveCurrentDefaults(
                    SceneDefaultsSaveView{ m_debug, m_runtimeSettings, m_camera } );
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
            m_replayRuntime.RecordEvent( ReplayEventKind::OwnerAction,
                                         m_replayRuntime.NextEventFrameIndex(),
                                         ReplaySceneRequestFlags( request ),
                                         static_cast<int32_t>( eventCode ),
                                         eventIndex,
                                         0,
                                         0,
                                         0,
                                         eventText ? eventText : ReplayOwnerEventName( eventCode ) );
        }
    }
    return batch.count > 0;
}
