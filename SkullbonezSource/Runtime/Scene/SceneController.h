/*
File: SkullbonezSource/Runtime/Scene/SceneController.h
Purpose:
  Owns scene runtime state, cameras, terrain, world settings, durable entity
  metadata, physics, and scene requests.

Summary:
  SceneController owns scene queue, load transactions, frame completion policy,
  camera slots, replaceable terrain, world settings, physics topology, fixed
  entity records, and the ordered request batch. InGameUI owns browser/override
  policy; the process shell supplies explicit cold-operation owners and consumes
  bounded physics outputs at the presentation boundary.

Glossary:
  Scene runtime: Current scene state plus queue navigation data.
  Scene queue: Ordered authored scene list, with an empty path selecting the
    generated demo scene.
  Scene request: Deferred load, reset, create, or defaults-save owner intent.
  Scene entity store: Fixed scene-lifetime join between identity, live body,
    render material intent, and asset affiliation.
  World environment: Scene-owned gravity, fluid, and terrain-bound settings
    borrowed by physics, replay, and rendering.
  Scene cameras: Fixed camera slots, tween state, and active render pose reset
    and populated with each scene load.
  Scene terrain: Replaceable height-map or flat-slope owner published only after
    construction and any required GPU drain succeed.
  Presentation capture: Allocation-free previous/current solver endpoints
    maintained in RenderInstanceStore across fixed physics steps.
  Post-step output: Bounded solver facts borrowed synchronously by presentation.

Invariants:
  - SceneController owns queue/index bookkeeping, camera/terrain state, world
    settings, and scene-lifetime physics; consumers receive only borrowed owner
    references.
  - All interactive scene submissions enter its fixed request ring.
  - Durable display/material/asset metadata lives in its fixed entity store.
  - Empty queue path is the generated demo scene sentinel.
  - Queue index lookups must normalize path separators before matching.
  - Scene entity, physics body/collider, and render rows retain the same dense
    count after every successful creation or deletion.
  - Post-step dense rows never escape the synchronous frame/replay consumer.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRuntime.h
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#pragma once

#include "SceneControllerState.h"
#include "SceneEntityStore.h"
#include "SceneRequestQueue.h"
#include "SceneRuntime.h"
#include "SceneRuntimeCoordinator.h"
#include "SceneTerrain.h"
#include "../../Core/MainMemoryStats.h"
#include "../../Core/SbResult.h"
#include "../../Maths/Vector3.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Rendering/RenderInstanceStore.h"
#include "../CameraCollection.h"
#include "../../World/WorldEnvironment.h"

#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
struct CinematicRenderConfig;
} // namespace Core
namespace Assets
{
class AssetSystem;
}
namespace Environment
{
class CameraCollection;
class WorldEnvironment;
} // namespace Environment
namespace Physics
{
class PhysicsEngine;
class PhysicsDebugVisualizer;
struct PhysicsWorldForces;
} // namespace Physics
namespace Threading
{
class WorkerPool;
}
namespace Runtime
{
namespace Audio
{
class ContactAudioService;
}
} // namespace Runtime
namespace UI
{
class InGameUI;
}
namespace Runtime
{
class DiagnosticsRuntime;
class InputRouter;
class ReplayRuntime;
class RuntimeInteractionController;
class RuntimeOverlayDiagnostics;
class RuntimeValidationHarness;
struct SceneAutomationGateStatus;
class RuntimeRenderer;
class RuntimeTools;
class SimulationSystem;
class Window;
struct AttachedCameraState;
struct RunCameraState;
struct RunDebugState;
struct RunLaunchOptions;
struct RunStartupState;
struct RunTimerState;
struct RuntimeRenderBackendView;
struct SceneFrameAdvanceResult
{
    SceneLoadRequest loadRequest;
    const char* finishReason = nullptr;
    bool restartFrame = false;
    bool requestQuit = false;
    bool holdInteractive = false;
    bool quitIfLoadFails = false;
    bool restartSimulationTimerAfterLoad = false;
    // Value request only; validation retains diagnostic rows and printing.
    bool reportMissingRequirements = false;
};
struct ScenePhysicsPostStepOutput
{
    // Lifetime: the span borrows the physics owner's fixed-capacity event rows
    // until the next physics step. Dense model rows are valid only for this
    // synchronous presentation handoff and are never durable scene identity.
    std::span<const int> fixedContactModelIndices;
};
struct SceneDefaultsSaveView
{
    // Lifetime: every owner is borrowed only for one synchronous cold save.
    // The writer retains no pointers across a scene reload.
    const RunDebugState& debug;
    const RuntimeRenderer& renderer;
    const RunCameraState& camera;
    const RunSceneUIOverrideState& uiOverrides;
};

// Concept: scene loading borrows four phase-oriented values instead of
// accepting the process shell's complete owner graph as one flat call. Each
// value is synchronous-only, contains at most six concrete owners, and is
// never retained by SceneController.
struct SceneLoadPolicyInputs
{
    SkullbonezCore::Core::EngineConfig& config;
    RunLaunchOptions& launchOptions;
    const SkullbonezCore::Core::CinematicRenderConfig& defaultCinematicRender;
    const RunStartupState& startup;
    Assets::AssetSystem& assets;
    Threading::WorkerPool& workerPool;
};

struct SceneLoadHostParticipants
{
    Window& window;
    RunTimerState& timers;
    DiagnosticsRuntime& diagnosticsRuntime;
    SimulationSystem& simulation;
};

struct SceneLoadInteractionParticipants
{
    InputRouter& inputRouter;
    RuntimeInteractionController& interaction;
    RunCameraState& camera;
    AttachedCameraState& attachedCamera;
    RuntimeTools& runtimeTools;
    UI::InGameUI& operatorUi;
};

struct SceneLoadPresentationParticipants
{
    Audio::ContactAudioService& contactAudio;
    ReplayRuntime& replayRuntime;
    RuntimeOverlayDiagnostics& overlays;
    RuntimeValidationHarness& validationHarness;
    const RuntimeRenderBackendView& renderBackendView;
    RuntimeRenderer& renderer;
};

// Concept: scene creation returns the recoverable authoring result together
// with the physics handle published by the successful cross-store commit.
struct SceneEntityCreateResult
{
    SkullbonezCore::Core::SbResult status;
    Physics::PhysicsBodyHandle body;
};

class SceneController
{
  public:
    SceneController();
    explicit SceneController( std::vector<std::string> queue );

    void ApplyRuntimeConfig( const SkullbonezCore::Core::EngineConfig& config );
    // One preflighted scene-creation command publishes metadata, physics, and
    // render rows together. Lane R input failures leave every owner unchanged;
    // a mismatched owner count is a fatal topology invariant.
    SceneEntityCreateResult TryCreateSceneEntity( Runtime::SceneEntityCreateDesc entity,
                                                  Physics::PhysicsBodyCreateDesc bodyDesc,
                                                  Physics::PhysicsColliderCreateDesc colliderDesc );
    // Cold scene/editor deletion removes the entity's physics, metadata,
    // presentation, and render rows as one swap-last transaction.
    bool DestroySceneEntity( Physics::PhysicsBodyHandle body );
    void Clear();
    void BeginPhysicsStepPresentationCapture();
    void CompletePhysicsStepPresentationCapture();
    void PrepareRenderInstances( float presentationAlpha = 1.0f );
    // Legacy object-follow cameras can outlive the model slots they track.
    // Returns false only for an absent slot; a present model without a body is
    // store-topology drift and still fails through the fatal invariant lane.
    bool TryGetModelPosition( int index, Math::Vector::Vector3& outPosition ) const;
    bool TryGetPresentationPose( int index,
                                 float presentationAlpha,
                                 Math::Vector::Vector3& outPosition,
                                 Math::Orientation::Quaternion& outOrientation ) const;
    // Scene entity count is the stable model-slot count shared by scene files,
    // editor picks, replay streams, and cold owner-repair boundaries.
    int SceneEntityCount() const;
    // These compatibility queries read SceneEntityStore-owned stable behavior
    // groups; callers receive a row only when their operation requires one.
    Runtime::SceneBehaviorGroupKind GroupKindAt( int modelIndex ) const;
    Physics::PhysicsSceneObjectId GroupRootObjectIdAt( int modelIndex ) const;
    int GroupPartIndexAt( int modelIndex ) const;
    bool IsSimpleRagdollPart( int modelIndex ) const;
    bool IsSimpleRagdollTorso( int modelIndex ) const;
    int RagdollRootModelIndexForPart( int modelIndex ) const;
    bool TryFindSimpleRagdollPart( int selectedModelIndex, int partIndex, int& outModelIndex ) const;
    int GatherGroupMemberIndices( int selectedModelIndex, int* outIndices, int maxIndices ) const;
#ifdef _DEBUG
    bool TryGetPhysicsDiagnosticsModelName( int index, const char*& outName ) const;
    void FillPhysicsDiagnosticsNames( int bodyCount, std::vector<const char*>& outNames ) const;
#endif
    SkullbonezCore::Core::MainMemoryGameObjectStats CollectMemoryStats() const;
    // SceneController uses this narrow presentation-owner command while it
    // coordinates replay topology with physics and entity owners.
    bool CanTrimPresentationRowsForSceneRestore( int modelCount ) const;
    bool TrimPresentationRowsForSceneRestore( int modelCount );
    void CaptureReplaySolverWorldSnapshot( Runtime::ReplaySolverWorldSnapshot& outSnapshot ) const;
    bool RestoreReplaySolverWorldSnapshot( const Runtime::ReplaySolverWorldSnapshot& snapshot );
    // Explicit cold owner boundary before tool or picker code asks for body
    // handles and collider bounds. Read-only store accessors do not repair.
    bool RepairPhysicsBodyAndColliderTopology();
    // Current prepared collider snapshot. Hot render passes use this after
    // PrepareRenderInstances() instead of invoking topology repair mid-submit.
    const Physics::PhysicsBodyStore& BodyStore() const;
    const Physics::ColliderStore& Colliders() const;
    // Current prepared render snapshot. Call PrepareRenderInstances() before
    // frame passes; cold callers that need an ensured snapshot use GetRenderInstanceStore().
    Rendering::RenderInstanceStore& MutableRenderInstances();
    const Rendering::RenderInstanceStore& RenderInstances() const;
    // Replay presentation samples are one-frame render overrides. The collection
    // validates replay body identity before mutating its render snapshot so scrub
    // and prediction code cannot redirect stale model slots.
    bool TryQueueReplayRenderPoseOverride( int modelIndex,
                                           uint32_t replayBodyId,
                                           const Math::Vector::Vector3& position,
                                           const Math::Orientation::Quaternion& orientation );
    std::span<const Rendering::RenderInstancePresentationRecord> RenderPresentationRecords() const
    {
        return m_renderInstanceStore.PresentationRecords();
    }
    const Rendering::RenderInstanceStore& GetRenderInstanceStore();
    double GetSceneKineticEnergy();
    // Runtime-tool edge: ray tools release authored fixed tree props through
    // PhysicsBodyStore; presentation reads the store/render snapshot instead of
    // forcing a per-release model-side body projection.
    bool ReleaseAttachedFixedTreeParts( int sourceIndex,
                                        float releaseImpulseStrength,
                                        const Math::Vector::Vector3& seedLinearVelocity,
                                        const Math::Vector::Vector3& seedAngularVelocity );

    void BeginCollisionVisualFrame();
    void EndCollisionVisualFrame();

    RunSceneState& State();
    const RunSceneState& State() const;
    SceneEntityStore& Entities();
    const SceneEntityStore& Entities() const;
    Environment::CameraCollection& Cameras();
    const Environment::CameraCollection& Cameras() const;
    Environment::WorldEnvironment& World();
    const Environment::WorldEnvironment& World() const;
    SceneTerrain& Terrain();
    const SceneTerrain& Terrain() const;
    Physics::PhysicsEngine& Physics();
    const Physics::PhysicsEngine& Physics() const;
    // Executes one deterministic live-scene physics step against the
    // controller-owned model and physics stores. Replay restore may call this
    // same boundary so its hash proof cannot drift from ordinary frame steps.
    ScenePhysicsPostStepOutput StepPhysics( float fixedDt,
                                            const SkullbonezCore::Core::EngineConfig& config,
                                            const Physics::PhysicsWorldForces& worldForces,
                                            Threading::WorkerPool& workerPool );
    void EnterInteractiveRun();
    bool CanAutomationQuit() const;
    void MarkInteractiveRunComplete();
    void ToggleCrossScenePause();
    bool CrossScenePauseLocked() const;
    SceneFrameAdvanceResult AdvanceFrame( const SceneAutomationGateStatus& automationGates,
                                          bool proceedAllowed,
                                          bool perfTestActive,
                                          bool screenshotSaved,
                                          bool manualCameraActive,
                                          double elapsedSeconds );

    bool HasEntry( int index ) const;
    bool HasCurrentEntry() const;
    const std::string* CurrentPath() const;
    const std::string& PathAt( int index ) const;
    int QueueSize() const;
    int CurrentIndex() const;
    int NextIndex() const;
    const std::vector<std::string>& Queue() const;

    void BeginLoad( int index );
    void RecordLifecycleEvent( SceneRuntimeLifecycleEvent event, SceneLifecycleConsumerMask consumers );
    void MarkManualReset();
    int FindNormalizedPath( const std::string& normalizedPath ) const;
    int FindGeneratedDemo() const;
    int Append( std::string path );
    bool CurrentQueueIsCinematicDeck() const;
    int AdjacentQueueIndex( int direction ) const;
    SceneLoadRequest ResetCurrentScene( bool preserveUIState, bool suppressExitOnComplete, bool preserveRuntimeState );
    SceneLoadRequest AdvanceScene( bool perfTestActive, bool preserveInteractiveUI );
    int PerfPass() const;
    // Lifetime: cold load orchestration borrows each phase value only for this
    // call. No Run backpointer or complete mutable context is retained behind
    // the scene boundary.
    SkullbonezCore::Core::SbResult Load( const SceneLoadRequest& request,
                                         SceneLoadPolicyInputs policy,
                                         SceneLoadHostParticipants host,
                                         SceneLoadInteractionParticipants interaction,
                                         SceneLoadPresentationParticipants presentation );
    // Executes the fixed pending batch inside the scene owner. Replay records
    // only requests whose load/create/save operation completes successfully.
    bool ExecutePending( SceneLoadPolicyInputs policy,
                         SceneLoadHostParticipants host,
                         SceneLoadInteractionParticipants interaction,
                         SceneLoadPresentationParticipants presentation );
    SkullbonezCore::Core::SbResult SaveCurrentDefaults( const SceneDefaultsSaveView& view ) const;

    // Scene request submission stays owner-specific even while Run temporarily
    // executes the returned batch during lifecycle extraction C1.
    void SubmitLoadBrowserIndex( int index );
    void SubmitLoadDemoScene();
    void SubmitResetCurrentScene( bool preserveUIState = true,
                                  bool suppressExitOnComplete = true,
                                  bool preserveRuntimeState = true );
    SkullbonezCore::Core::SbResult SubmitCreateScene( const char* requestedName );
    void SubmitSaveCurrentDefaults();
    SceneRequestBatch TakePendingRequests();
    std::size_t PendingRequestCount() const;
    // Cold replay restore shrinks every scene-lifetime row owner as one
    // transaction; ReplayRuntime never writes topology through model facades.
    bool TrimForReplayRestore( int bodyCount );

    SceneRuntime& Runtime();
    const SceneRuntime& Runtime() const;

  private:
    Rendering::RenderInstanceStore m_renderInstanceStore; // Render snapshot in scene/model order, outside physics.
    // Configured model cap used by append/reserve guards.
    int m_activeGameModelCapacity = SkullbonezCore::Scene::Capacity::DEFAULT_GAME_MODEL_CAPACITY;
    void ReserveForActiveGameModelCapacity();
    const Runtime::SceneBehaviorGroup& BehaviorGroupAt( int modelIndex ) const;
    int ResolveBehaviorGroupRootModelIndex( const Runtime::SceneBehaviorGroup& group ) const;
    // Owner boundary: SceneEntityStore owns fixed-tree grouping. Body-store
    // import receives only derived row hints, never scene metadata accessors.
    std::vector<Physics::ModelRowHint> BuildFixedTreeReleaseRootsForReload() const;
    std::vector<const char*> BuildDiagnosticNamesForReload() const;
    bool RefreshPhysicsBodyStoreFromAuthoredDescriptors();
    // Private body-only repair is reserved for scene-owned projection phases.
    // Public tool/runtime reads use an explicit owner boundary before borrowing
    // PhysicsEngine store views.
    bool RepairPhysicsBodyTopology();
    int FixedTreeReleaseRootForModelIndex( int modelIndex ) const;
    void RefreshRenderInstances( float presentationAlpha = 1.0f );
    Runtime::SceneEntityStore& SceneEntities();
    const Runtime::SceneEntityStore& SceneEntities() const;
    void AssertSceneCreationTopology( int expectedCount ) const;

    SceneRuntime m_runtime;                               // Scene queue and active scene-run state
    SceneRequestQueue m_requests;                         // Fixed scene-only deferred intent ring.
    int m_perfPass = 0;                                   // Scene navigation pass index for two-pass performance captures.
    bool m_crossScenePauseLocked = false;                 // Operator scene-flow lock preserved across load transactions.
    SceneEntityStore m_entities;                          // Fixed scene-lifetime identity and durable presentation metadata.
    Environment::CameraCollection m_cameras;              // Fixed scene camera slots and active camera presentation state.
    Environment::WorldEnvironment m_world;                // Gravity, fluid, and terrain bounds for the active scene.
    SceneTerrain m_terrain;                               // Replaceable terrain and its matching scene-shape classification.
    // Lifetime: physics topology is born and cleared with the active scene.
    // Presentation owners borrow this engine; they never own or replace it.
    Physics::PhysicsEngine m_physics;
};
} // namespace Runtime
} // namespace SkullbonezCore
