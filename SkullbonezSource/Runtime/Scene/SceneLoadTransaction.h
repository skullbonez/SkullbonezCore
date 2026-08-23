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
  outputs, App applies runtime-owner reactions, and only then may external
  window/UI/validation presentation observe the result. A later
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
#include "SceneLoadPreparation.h"
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

enum class SceneCaptureReactionKind : uint8_t
{
    DisableAutomationExit,
    ResetScreenshot,
    ApplyAutomation
};

struct SceneCaptureAutomationValues
{
    int screenshotFrame = -1;
    int screenshotMs = -1;
    bool screenshotAndExit = false;
    char screenshotPath[256] = {};
    int screenshotInterval = -1;
    char screenshotDirectory[256] = {};
};

struct SceneCaptureReaction
{
    SceneCaptureReactionKind kind = SceneCaptureReactionKind::DisableAutomationExit;
    SceneCaptureAutomationValues automation;
};

inline constexpr std::size_t SCENE_CAPTURE_REACTION_CAPACITY = 4;

struct SceneCaptureReactionBatch
{
    std::array<SceneCaptureReaction, SCENE_CAPTURE_REACTION_CAPACITY> reactions = {};
    std::size_t count = 0;
};

// Detached result of one cold scene-load batch. App consumes these values at
// the runtime-reaction and presentation checkpoints; Scene retains no borrow
// of the owners that apply them.
struct SceneLoadResult
{
    SceneUiActivation uiActivation;
    SceneAutomationGateConfiguration automationGates;
    SceneLoadNavigationState navigation;
    ScenePresentationValues presentation;
    CameraControlState camera;
    SceneRenderPolicyState renderPolicy;
    SceneRenderActivationRequest renderActivation;
    SceneCaptureReactionBatch captureReactions;
    SceneDiagnosticsReactionBatch diagnosticsReactions;
    std::array<SceneLoadCompletedWorldChange, 2> completedWorldChanges = {};
    std::size_t completedWorldChangeCount = 0;
    SceneRequestBatch completedRequests;
    char windowTitle[256] = {};
    bool applyNavigation = false;
    bool refreshSceneBrowser = false;

    void ResetForLoad();
};

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
                                const ScenePresentationValues& presentation, SceneRenderPolicyState renderPolicy,
                                const char* rendererName, double sceneTimeSeconds );

    const SceneLoadBeginResult& Prepare( SceneController& sceneController, const SceneLoadRequest& request,
                                         Rendering::Dx12FrameOwner* renderFrame,
                                         bool interactiveSceneRunRequested );
    void CompleteBeforeUnloadDiagnostics();

    SkullbonezCore::Core::SbResult Load( SceneController& sceneController, const SceneLoadRequest& request,
                                         SkullbonezCore::Core::EngineConfig& config, RunLaunchOptions& launchOptions,
                                         const SkullbonezCore::Core::CinematicRenderConfig& defaultCinematicRender,
                                         const RunStartupState& startup, Assets::AssetSystem& assets,
                                         Threading::WorkerPool& workerPool,
                                         const SceneDiagnosticsLoadInput& diagnosticsInput,
                                         Rendering::Dx12FrameOwner* renderFrame,
                                         Rendering::Dx12ResourceBuilder* renderResources );

    const SceneRenderPolicyState& RenderPolicy() const
    {
        return m_outputs.renderPolicy;
    }
    const SceneRenderActivationRequest& RenderActivation() const
    {
        return m_outputs.renderActivation;
    }
    const SceneDiagnosticsReactionBatch& DiagnosticsReactions() const
    {
        return m_outputs.diagnosticsReactions;
    }
    void CompleteRenderActivation( SceneController& sceneController );
    const CameraControlState& CurrentCamera() const
    {
        return m_outputs.camera;
    }
    void SetRefreshSceneBrowser( bool refresh )
    {
        m_outputs.refreshSceneBrowser = refresh;
    }
    void RecordCompletedRequest( const SceneRequest& request );
    void FinishRequestBatch();

    // App advances each checkpoint and synchronously applies this immutable
    // result to the concrete owners. The transaction retains values only.
    const SceneLoadResult& BeginRuntimeReactions();
    const SceneLoadResult& BeginPresentation();
    SceneAutomationGateConfiguration TakeAutomationGates();
    void CompletePresentation();

    const SceneLoadNavigationState& NavigationForFollowingRequest( const SceneLoadNavigationState& submitted ) const
    {
        return m_outputs.applyNavigation ? m_outputs.navigation : submitted;
    }

    ScenePresentationValues PresentationForFollowingRequest( const ScenePresentationValues& submitted,
                                                              const SceneLifecyclePacket& lifecycle ) const
    {
        return m_request.HasLoad() && m_phase.Current() == SceneLoadPhaseCursor::Phase::Load
                   ? ( SceneLifecycleReached( lifecycle.event, SceneRuntimeLifecycleEvent::AfterSceneCleared )
                           ? m_outputs.presentation
                           : submitted )
                   : submitted;
    }

    // The stress lane may suppress GameUI activation without recovering the
    // complete private output record.
    void PreserveInactiveDevelopmentUi();

    SceneLoadPhaseCursor::Phase Phase() const
    {
        return m_phase.Current();
    }

  private:
    friend class SceneController;
    friend struct SceneLoadTransactionTestAccess;

    // A request batch with no transition still completes a load phase so every
    // caller follows the same reaction/presentation schedule.
    void AdvanceOrFatal( SceneLoadPhaseCursor::Phase next, const char* operation );
    static SceneLoadBeginResult PrepareLoad( const SceneController& controller,
                                             const SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides,
                                             SceneRenderPolicyState renderPolicy,
                                             const ScenePresentationValues& presentation,
                                             const CameraControlState& camera, Rendering::Dx12FrameOwner* renderFrame,
                                             bool interactiveSceneRunRequested, int index, bool suppressExitOnComplete,
                                             bool preserveRuntimeState );
    static void CommitLoad( SceneController& controller, SceneLoadNavigationState& navigation,
                            const SceneLoadBeginResult& prepared );
    static SceneResetPreservationSnapshot
    CaptureResetSnapshot( const SceneController& controller, const SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides,
                          SceneRenderPolicyState renderPolicy, const ScenePresentationValues& presentation,
                          const CameraControlState& camera );
    static void RestoreResetSnapshot( SceneController& controller, SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides,
                                      SceneRenderPolicyState& renderPolicy, ScenePresentationValues& presentation,
                                      CameraControlState& camera,
                                      const SceneResetPreservationSnapshot& snapshot, bool suppressExitOnComplete );
    static void ClearUiOverrides( SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides );
    void PrepareUiOptions( ScenePresentationValues& presentation, SceneUiActivation& activation,
                           const SceneUIOptions& options, double nowSeconds, bool preserveUIState,
                           bool automationScene );
    void AppendDiagnosticsReaction( const SceneDiagnosticsReaction& reaction );
    SceneLoadRequest m_request = SceneLoadRequest::None();
    SceneLoadBeginResult m_preparedLoad;
    SceneLoadResult m_outputs;
    SceneLoadPhaseCursor m_phase;
    bool m_hasPreparedLoad = false;
    bool m_beforeUnloadDiagnosticsComplete = false;
    char m_rendererName[64] = "unknown";
    double m_sceneTimeSeconds = 0.0;
};
} // namespace Runtime
} // namespace SkullbonezCore
