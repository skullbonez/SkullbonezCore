/*
File: SkullbonezSource/Runtime/Scene/SceneLoadTransaction.h
Purpose:
  Owns one scene-load request batch's phase order and follow-up value arbitration.

Summary:
  SceneLoadTransaction replaces caller-enforced Load, runtime-reaction, and
  presentation ordering with a stack-scoped invariant owner. It retains only
  detached request/submission values, outputs, and a phase cursor; concrete
  runtime owners are borrowed by the phase method that uses them and are never
  stored.
  The transaction is a one-way turnstile. Scene mutation fills its private
  outputs, runtime owners consume the lifecycle generation, and only then may
  external window/UI/validation presentation observe the result. A later
  request in the same fixed batch asks this owner which detached value is
  current instead of reimplementing stale-versus-loaded arbitration.

Glossary:
  Load phase: Scene mutation or a completed no-load request batch.
  Runtime reactions: Lifecycle consumption by timer, overlay, input, camera,
    tools, interaction, and Replay owners.
  Presentation: Window, UI, validation, and device policy publication.
  Following request: A later cold request in the same fixed owner batch.

Invariants:
  - The only legal phase walk is Idle -> Load -> RuntimeReactions ->
    Presentation -> Complete; an illegal transition is fatal-invariant fatal.
  - Request, submitted camera/navigation/presentation/time/name, outputs, and
    arbitration state are private values. No runtime owner pointer or reference
    survives a phase-method return.
  - Following requests use loaded navigation after it is committed, and loaded
    presentation only after this transaction loads and reaches AfterSceneCleared.

Related:
  - SkullbonezSource/Runtime/Scene/SceneController.h
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - SkullbonezSource/Runtime/Scene/SceneRequestExecution.cpp
  - SkullbonezTests/TestOwnerRequestQueues.cpp
*/
#pragma once

#include "SceneController.h"
#include "SceneLoadPresentation.h"
#include "SceneResetPreservation.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace SkullbonezCore
{
namespace Runtime
{
struct SceneLoadTransactionTestAccess;
struct SceneLoadBeginResult;

class SceneLoadPhaseCursor
{
  public:
    enum class Phase : uint8_t
    {
        Idle,
        Load,
        RuntimeReactions,
        Presentation,
        Complete,
        Count
    };

    static constexpr bool IsLegalTransition( Phase from, Phase to )
    {
        return ( from == Phase::Idle && to == Phase::Load ) || ( from == Phase::Load && to == Phase::RuntimeReactions ) ||
               ( from == Phase::RuntimeReactions && to == Phase::Presentation ) ||
               ( from == Phase::Presentation && to == Phase::Complete );
    }

    bool TryAdvance( Phase next )
    {
        if ( !IsLegalTransition( m_phase, next ) )
        {
            return false;
        }

        m_phase = next;
        return true;
    }

    Phase Current() const
    {
        return m_phase;
    }

  private:
    Phase m_phase = Phase::Idle;
};

// Invariant:
// - One request batch may expose presentation only after Load and
//   RuntimeReactions complete through the adjacent phase walk.
// - The owner retains detached values and its cursor, never a borrowed runtime
//   owner. TestOwnerRequestQueues.cpp proves the complete transition matrix and
//   loaded-versus-submitted arbitration.
class SceneLoadTransaction
{
  public:
    SceneLoadTransaction() = default;
    SceneLoadTransaction( const SceneLoadTransaction& ) = delete;
    SceneLoadTransaction& operator=( const SceneLoadTransaction& ) = delete;

    // Captures detached per-batch values before any request is executed.
    // Concrete load owners are still borrowed only by Load below.
    void CaptureSubmittedState( const CameraControlState& camera, const SceneLoadNavigationState& navigation,
                                const OverlayDebugState& debug, const char* rendererName, double sceneTimeSeconds );

    SkullbonezCore::Core::SbResult Load( SceneController& sceneController, const SceneLoadRequest& request,
                                         SkullbonezCore::Core::EngineConfig& config, RunLaunchOptions& launchOptions,
                                         const SkullbonezCore::Core::CinematicRenderConfig& defaultCinematicRender,
                                         const RunStartupState& startup, Assets::AssetSystem& assets,
                                         Threading::WorkerPool& workerPool, DiagnosticsRuntime& diagnosticsRuntime,
                                         Rendering::Dx12FrameOwner* renderFrame,
                                         Rendering::Dx12ResourceBuilder* renderResources, RuntimeRenderer& renderer );

    void ApplyRuntimeReactions( const RunLaunchOptions& launchOptions, RunTimerState& timers,
                                RuntimeOverlayDiagnostics& overlays, SceneController& sceneController,
                                InputRouter& inputRouter, RuntimeInteractionController& interaction,
                                CameraControlState& camera, AttachedCameraController& attachedCamera,
                                RuntimeTools& runtimeTools, ReplayRuntime& replayRuntime );

    void ApplyPresentationOutputs( Window& window, UI::InGameUI& operatorUi, RuntimeValidationHarness& validationHarness,
                                   const RunLaunchOptions& launchOptions, Rendering::Dx12RenderDevice* renderDevice,
                                   bool rendererVsyncEnabled, SceneController& sceneController );

    const SceneLoadNavigationState& NavigationForFollowingRequest( const SceneLoadNavigationState& submitted ) const
    {
        return m_outputs.applyNavigation ? m_outputs.navigation : submitted;
    }

    const OverlayDebugState& PresentationForFollowingRequest( const OverlayDebugState& submitted,
                                                              const SceneLifecyclePacket& lifecycle ) const
    {
        return m_request.HasLoad() && m_phase.Current() == SceneLoadPhaseCursor::Phase::Load
                   ? ( SceneLifecycleReached( lifecycle.event, SceneRuntimeLifecycleEvent::AfterSceneCleared )
                           ? m_outputs.presentation
                           : submitted )
                   : submitted;
    }

    // The stress lane may suppress Legacy activation without recovering the
    // complete private output record.
    void PreserveInactiveDevelopmentUi();

    SceneLoadPhaseCursor::Phase Phase() const
    {
        return m_phase.Current();
    }

  private:
    struct Outputs
    {
        SceneUiActivation uiActivation;
        SceneAutomationGateConfiguration automationGates;
        SceneLoadNavigationState navigation;
        OverlayDebugState presentation;
        CameraControlState camera;
        std::array<SceneLoadCompletedWorldChange, 2> completedWorldChanges = {};
        std::size_t completedWorldChangeCount = 0;
        SceneRequestBatch completedRequests;
        char windowTitle[256] = {};
        bool applyNavigation = false;
        bool refreshSceneBrowser = false;

        void ResetForLoad();
    };

    friend class SceneController;
    friend struct SceneLoadTransactionTestAccess;

    // A request batch with no transition still completes a load phase so every
    // caller follows the same reaction/presentation schedule.
    void FinishLoadPhase();
    void AdvanceOrFatal( SceneLoadPhaseCursor::Phase next, const char* operation );
    static SceneLoadBeginResult PrepareLoad( const SceneController& controller,
                                             const SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides,
                                             const RuntimeRenderer& renderer, const OverlayDebugState& debug,
                                             const CameraControlState& camera, Rendering::Dx12FrameOwner* renderFrame,
                                             bool interactiveSceneRunRequested, int index, bool suppressExitOnComplete,
                                             bool preserveRuntimeState );
    static void CommitLoad( SceneController& controller, SceneLoadNavigationState& navigation,
                            const SceneLoadBeginResult& prepared );
    static SceneResetPreservationSnapshot
    CaptureResetSnapshot( const SceneController& controller, const SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides,
                          const RuntimeRenderer& renderer, const OverlayDebugState& debug,
                          const CameraControlState& camera );
    static void RestoreResetSnapshot( SceneController& controller, SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides,
                                      RuntimeRenderer& renderer, OverlayDebugState& debug, CameraControlState& camera,
                                      const SceneResetPreservationSnapshot& snapshot, bool suppressExitOnComplete );
    static void ClearUiOverrides( SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides );
    static void PrepareUiOptions( DiagnosticsRuntime& diagnostics, OverlayDebugState& debug, SceneUiActivation& activation,
                                  const SceneUIOptions& options, double nowSeconds, bool preserveUIState,
                                  bool automationScene );
    static void ApplyUiActivation( UI::InGameUI& ui, const SceneUiActivation& activation );

    SceneLoadRequest m_request = SceneLoadRequest::None();
    Outputs m_outputs;
    SceneLoadPhaseCursor m_phase;
    char m_rendererName[64] = "unknown";
    double m_sceneTimeSeconds = 0.0;
};
} // namespace Runtime
} // namespace SkullbonezCore
