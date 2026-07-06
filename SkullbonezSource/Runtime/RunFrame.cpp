/*
File: SkullbonezSource/Runtime/RunFrame.cpp
Purpose:
  Runs one frame of input, simulation, rendering, profiling, and presentation.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Simulation tick: One runtime decision about whether to advance logic, camera,
    and zero or more fixed physics steps this frame.
  Contact-audio flash mode: Render-only diagnostic selector that decides which
    completed audio decisions paint body flashes after a fixed physics step.
  Contact-audio simple mode: Presentation-only path that emits from body linear
    velocity changes rather than solver contact rows.
  Fixed-step edge: Runtime-owned code that repairs model/body topology before
    PhysicsEngine::Step and applies presentation-only refresh work after it.
  PhysicsBodyStore: Physics-owned body rows for live pose, velocity, fixed
    state, and replay identity.
  ColliderStore: Physics-owned collider rows for exact shape variants, material
    parameters, and broadphase radius.
  Replay event payload: Saved event data that must be decoded exactly so replay
    restore and validation compare the same floating-point bits.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - Replay event payload bits are decoded without numeric conversion so saved
    timelines reproduce exact float values.
  - Frame work updates input, simulation, capture, rendering, and diagnostics
    in a stable order used by validation and replay comparisons.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "RunInternal.h"
#include "Scene/SceneRuntimeLoad.h"

#include "CaptureSystem.h"
#include "Editor/EditorTools.h"
#include "Replay/ReplayV2Artifact.h"
#include "Allocation/RuntimeAllocationTracker.h"
#include "RuntimeTuning.h"
#include "Scene/SceneRuntimeStyle.h"

#include "../Physics/ColliderStore.h"
#include "../Physics/PhysicsApi.h"
#include "../Rendering/RenderInstanceStore.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;
using SkullbonezCore::Math::Vector::Vector3;
namespace RuntimeAllocation = SkullbonezCore::Runtime::Allocation;

namespace
{
// Why: Profile builds do not emit Debug-only scene-finished telemetry, so
// automation exits need an explicit stdout breadcrumb near the quit request.
void PrintRuntimeExitReason( const char* reason )
{
    printf( "[runtime-exit] %s\n", reason );
    fflush( stdout );
}

bool ShouldFlashContactAudioDecision( ContactAudioFlashMode mode,
                                      const SkullbonezCore::Runtime::Audio::ContactAudioDecision& decision )
{
    switch ( mode )
    {
    case ContactAudioFlashMode::Off:
        return false;
    case ContactAudioFlashMode::Emitted:
        return decision.submitted && decision.flashEligible;
    case ContactAudioFlashMode::Candidates:
        return true;
    case ContactAudioFlashMode::Rejected:
        return !decision.submitted;
    default:
        return decision.submitted && decision.flashEligible;
    }
}

Vector3 RenderProbeMatrixTranslation( const Matrix4& matrix )
{
    return Vector3( matrix.m[12], matrix.m[13], matrix.m[14] );
}

bool TryPrepareReplayProbeRenderPosition( SkullbonezCore::GameObjects::GameModelCollection& collection,
                                          int modelIndex,
                                          Vector3& outPosition )
{
    const auto& instances = collection.RenderInstances().Records();
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( instances.size() ) )
    {
        return false;
    }

    outPosition = RenderProbeMatrixTranslation( instances[static_cast<std::size_t>( modelIndex )].modelMatrix );
    return true;
}

bool ApplyReplayProbePresentationSampleForRender( SkullbonezCore::GameObjects::GameModelCollection& collection,
                                                  ReplayRuntime& replayRuntime,
                                                  const ReplayPresentationSample& sample )
{
    // Why: probes consume replay scrub poses exactly where the renderer consumes
    // them: after the live render snapshot refresh and before draw submission.
    // This proves presentation overrides do not mutate live body rows.
    collection.PrepareRenderInstances();
    PhysicsEngine& physics = collection.GetPhysicsEngine();
    return replayRuntime.ApplyPresentationSampleForRender( physics, sample );
}

void RestoreReplayProbeRenderInstances( SkullbonezCore::GameObjects::GameModelCollection& collection )
{
    collection.PrepareRenderInstances();
}

const PhysicsBodyRecord*
TryGetReplayProbeBodyRecord( const SkullbonezCore::GameObjects::GameModelCollection& collection, int modelIndex )
{
    const PhysicsBodyStore& bodyStore = collection.GetPhysicsEngine().BodyStore();
    const PhysicsBodyHandle bodyHandle = bodyStore.HandleForModelIndex( modelIndex );
    const PhysicsBodyRecord* body = bodyStore.RecordForHandle( bodyHandle );
    if ( !body || bodyStore.ModelIndexForHandle( bodyHandle ) != modelIndex )
    {
        return nullptr;
    }
    return body;
}


// Why: editor transform replay still mutates authoring data, but the shape it
// scales from is already owned by ColliderStore. Reading the store row here
// avoids treating presentation data as collision authority.
const ColliderRecord*
TryGetEditorTransformColliderRecord( const SkullbonezCore::GameObjects::GameModelCollection& collection,
                                     PhysicsColliderHandle colliderHandle,
                                     int modelIndex,
                                     uint32_t replayBodyId )
{
    const ColliderStore& colliderStore = collection.Colliders();
    const PhysicsBodyStore& bodyStore = collection.GetPhysicsEngine().BodyStore();
    const PhysicsBodyHandle bodyHandle = replayBodyId != 0u
                                             ? bodyStore.HandleForReplayBodyId( replayBodyId, modelIndex )
                                             : bodyStore.HandleForModelIndex( modelIndex );
    const PhysicsColliderHandle resolvedHandle =
        colliderHandle.IsValid() ? colliderHandle : colliderStore.HandleForBodyHandle( bodyHandle );
    const ColliderRecord* collider = colliderStore.RecordForHandle( resolvedHandle );
    if ( !collider || colliderStore.ModelIndexForHandle( resolvedHandle ) != modelIndex )
    {
        return nullptr;
    }
    if ( replayBodyId != 0 && collider->replayBodyId != replayBodyId )
    {
        return nullptr;
    }
    return collider;
}

constexpr uint32_t REPLAY_WORLD_OVERRIDE_GRAVITY_CHANGED = 1u;
constexpr uint32_t REPLAY_WORLD_OVERRIDE_FLUID_HEIGHT_CHANGED = 2u;
constexpr uint32_t REPLAY_WORLD_OVERRIDE_FLUID_DENSITY_CHANGED = 4u;
constexpr uint32_t REPLAY_LAUNCHER_FIRE_PROJECTILE = 1u;
constexpr uint32_t REPLAY_EDITOR_PLACE_FIXED = 1u;
constexpr uint32_t REPLAY_EDITOR_PLACE_TERRAIN_ALIGN = 2u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_TRANSLATE = 1u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_ROTATE = 2u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_SCALE = 4u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_SUPPORTED =
    REPLAY_EDITOR_TRANSFORM_TRANSLATE | REPLAY_EDITOR_TRANSFORM_ROTATE | REPLAY_EDITOR_TRANSFORM_SCALE;
constexpr uint32_t REPLAY_GENERATED_SCENE_EXACT_SOLVER_COUNTS = 1u;
constexpr uint32_t REPLAY_GENERATED_SCENE_UI_MODEL_COUNT = 2u;
constexpr uint32_t REPLAY_GENERATED_SCENE_UI_SOLVER_COUNTS = 4u;
constexpr uint32_t REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT = 8u;
constexpr uint32_t REPLAY_GENERATED_SCENE_OVERRIDE_MASK = 3u << REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT;

// Concept: replay flags are compact wire-format fields. Keep these masks local
// to decode logic so new replay payload versions do not inherit accidental UI
// or runtime enum values.

// Concept: fixed-step edge.
//
// PhysicsEngine::Step owns the store-backed solve. GameModelCollection still
// owns topology repair, model highlight timers, and Debug presentation names,
// so the runtime frame applies those owner-side edges explicitly around the
// store-owned engine step.
void StepRuntimePhysicsTick( SkullbonezCore::GameObjects::GameModelCollection& modelCollection,
                             float fixedDt,
                             const EngineConfig& config,
                             const PhysicsWorldForces& worldForces,
                             SkullbonezCore::Threading::WorkerPool& workerPool )
{
    const int modelCount = modelCollection.ModelCount();
    // Invariant: PhysicsBodyStore is the per-tick body authority. Descriptor
    // sidecars are imported only when model/body/collider topology changes;
    // same-count editor or replay mutations must commit explicitly before this
    // step reads store rows.
    modelCollection.RepairPhysicsBodyAndColliderTopology();
    modelCollection.TickContactHighlights( modelCount, fixedDt );

    PhysicsEngine& physicsEngine = modelCollection.GetPhysicsEngine();
    const char* const* diagnosticNames = nullptr;
    int diagnosticNameCount = 0;
#ifdef _DEBUG
    std::vector<const char*> physicsDiagnosticsModelNames;
    if ( physicsEngine.ShouldEmitStepDiagnostics() || physicsEngine.ShouldEmitCollisionTimeDiagnostics() )
    {
        // Lifetime: Debug diagnostics borrow model name pointers only until
        // Step returns; physics never retains this presentation table after
        // emitting frame diagnostics.
        modelCollection.FillPhysicsDiagnosticsNames( physicsEngine.BodyStore().Count(), physicsDiagnosticsModelNames );
        diagnosticNames = physicsDiagnosticsModelNames.empty() ? nullptr : physicsDiagnosticsModelNames.data();
        diagnosticNameCount = static_cast<int>( physicsDiagnosticsModelNames.size() );
    }
#endif
    physicsEngine.Step( fixedDt, config, worldForces, workerPool, diagnosticNames, diagnosticNameCount );

    // Why: fixed-contact highlights are presentation feedback, not solver
    // state. Keeping this edge here leaves the normal step visibly store-owned
    // instead of hiding side effects in GameModelCollection.
    for ( int index : physicsEngine.GetFixedContactHighlightBodies() )
    {
        modelCollection.NotifyFixedContact( index, 0.5f );
    }
}

void CompareLatestReplaySamples( ReplayRuntime& replayRuntime, RunReplayMismatchState& mismatchState )
{
    const ReplayPresentationSample* presentation = replayRuntime.Presentation().LatestSample();
    const ReplaySolverFrameSample* solver = replayRuntime.Solver().LatestSample();
    if ( !presentation || !solver )
    {
        return;
    }

    const bool matches = presentation->frameIndex == solver->frameIndex &&
                         presentation->stateHash == solver->presentationHash &&
                         presentation->bodies.size() == solver->bodies.size();
    if ( matches )
    {
        return;
    }

    if ( mismatchState.reports < 8 )
    {
        ++mismatchState.reports;
        fprintf( stderr,
                 "[replay] Solver/presentation capture mismatch #%u: presentation_frame=%llu solver_frame=%llu "
                 "presentation_hash=0x%016llX solver_presentation_hash=0x%016llX solver_hash=0x%016llX "
                 "presentation_bodies=%llu solver_bodies=%llu\n",
                 mismatchState.reports,
                 static_cast<unsigned long long>( presentation->frameIndex ),
                 static_cast<unsigned long long>( solver->frameIndex ),
                 static_cast<unsigned long long>( presentation->stateHash ),
                 static_cast<unsigned long long>( solver->presentationHash ),
                 static_cast<unsigned long long>( solver->solverHash ),
                 static_cast<unsigned long long>( presentation->bodies.size() ),
                 static_cast<unsigned long long>( solver->bodies.size() ) );
    }
    else if ( !mismatchState.suppressed )
    {
        mismatchState.suppressed = true;
        fprintf( stderr,
                 "[replay] Further solver/presentation capture mismatch diagnostics suppressed for this replay "
                 "timeline.\n" );
    }
}

SceneGeneratedModelContext BuildSceneGeneratedModelContext( RunSceneState& scene,
                                                            const EngineConfig& config,
                                                            SkullbonezCore::Environment::WorldEnvironment& world,
                                                            SkullbonezCore::Geometry::Terrain* terrain,
                                                            SkullbonezCore::GameObjects::GameModelCollection& models,
                                                            SkullbonezCore::Physics::PhysicsEngine& physics,
                                                            GeneratedObjectTypeOverride objectTypeOverride )
{
    return SceneGeneratedModelContext{ scene, config, world, terrain, models, physics, objectTypeOverride };
}

float ReplayEventFloatFromBits( int32_t signedBits )
{
    uint32_t bits = 0;
    float value = 0.0f;
    static_assert( sizeof( bits ) == sizeof( signedBits ), "Replay event float bits must be 32-bit." );
    static_assert( sizeof( bits ) == sizeof( value ), "Replay event float payloads assume 32-bit floats." );
    std::memcpy( &bits, &signedBits, sizeof( bits ) );
    std::memcpy( &value, &bits, sizeof( value ) );
    return value;
}

int ReplayHexNibble( char value )
{
    if ( value >= '0' && value <= '9' )
    {
        return value - '0';
    }
    if ( value >= 'a' && value <= 'f' )
    {
        return value - 'a' + 10;
    }
    if ( value >= 'A' && value <= 'F' )
    {
        return value - 'A' + 10;
    }
    return -1;
}

bool ReadReplayHexFloat( const char*& cursor, float& outValue )
{
    uint32_t bits = 0;
    for ( int i = 0; i < 8; ++i )
    {
        const int nibble = ReplayHexNibble( cursor[i] );
        if ( nibble < 0 )
        {
            return false;
        }
        bits = ( bits << 4 ) | static_cast<uint32_t>( nibble );
    }
    cursor += 8;
    std::memcpy( &outValue, &bits, sizeof( outValue ) );
    return true;
}

bool DecodeReplayRay9Payload( const ReplayEventSample& event,
                              Vector3& outOrigin,
                              Vector3& outDirection,
                              Vector3& outCameraUp )
{
    constexpr char prefix[] = "ray9:";
    if ( std::strncmp( event.text, prefix, sizeof( prefix ) - 1 ) != 0 )
    {
        return false;
    }

    const char* cursor = event.text + sizeof( prefix ) - 1;
    float values[9] = {};
    for ( float& value : values )
    {
        if ( !ReadReplayHexFloat( cursor, value ) )
        {
            return false;
        }
    }

    outOrigin = Vector3( values[0], values[1], values[2] );
    outDirection = Vector3( values[3], values[4], values[5] );
    outCameraUp = Vector3( values[6], values[7], values[8] );
    return true;
}

bool DecodeReplayPlacePayload( const ReplayEventSample& event,
                               Vector3& outTerrainPoint,
                               Vector3& outPlacementScale,
                               float& outPlacementYawRadians )
{
    constexpr char prefix6[] = "place6:";
    constexpr char prefix7[] = "place7:";
    int valueCount = 0;
    const char* cursor = nullptr;
    if ( std::strncmp( event.text, prefix7, sizeof( prefix7 ) - 1 ) == 0 )
    {
        valueCount = 7;
        cursor = event.text + sizeof( prefix7 ) - 1;
    }
    else if ( std::strncmp( event.text, prefix6, sizeof( prefix6 ) - 1 ) == 0 )
    {
        valueCount = 6;
        cursor = event.text + sizeof( prefix6 ) - 1;
    }
    else
    {
        return false;
    }

    float values[7] = {};
    for ( int i = 0; i < valueCount; ++i )
    {
        if ( !ReadReplayHexFloat( cursor, values[i] ) )
        {
            return false;
        }
    }

    outTerrainPoint = Vector3( values[0], values[1], values[2] );
    outPlacementScale = Vector3( values[3], values[4], values[5] );
    outPlacementYawRadians = valueCount >= 7 ? values[6] : 0.0f;
    return true;
}


bool DecodeReplayTransformPayload( const ReplayEventSample& event,
                                   Vector3& outPosition,
                                   Quaternion& outOrientation,
                                   float& outScaleFactor,
                                   bool& outHasScaleFactor )
{
    constexpr char prefix7[] = "xform7:";
    constexpr char prefix8[] = "xform8:";
    int valueCount = 0;
    const char* cursor = nullptr;
    if ( std::strncmp( event.text, prefix7, sizeof( prefix7 ) - 1 ) == 0 )
    {
        valueCount = 7;
        cursor = event.text + sizeof( prefix7 ) - 1;
        outHasScaleFactor = false;
    }
    else if ( std::strncmp( event.text, prefix8, sizeof( prefix8 ) - 1 ) == 0 )
    {
        valueCount = 8;
        cursor = event.text + sizeof( prefix8 ) - 1;
        outHasScaleFactor = true;
    }
    else
    {
        return false;
    }

    float values[8] = {};
    for ( int i = 0; i < valueCount; ++i )
    {
        if ( !ReadReplayHexFloat( cursor, values[i] ) )
        {
            return false;
        }
    }

    outPosition = Vector3( values[0], values[1], values[2] );
    outOrientation = Quaternion( values[3], values[4], values[5], values[6] );
    outOrientation.Normalise();
    outScaleFactor = outHasScaleFactor ? values[7] : 1.0f;
    return true;
}


const ReplayV2SolverHashSample* FindReplaySolverHashForFrame( const std::vector<ReplayV2SolverHashSample>& hashes,
                                                              ReplayFrameIndex frameIndex )
{
    for ( const ReplayV2SolverHashSample& hash : hashes )
    {
        if ( hash.frameIndex == frameIndex )
        {
            return &hash;
        }
    }
    return nullptr;
}

const ReplayPresentationSample* FindReplayPresentationForFrame( const std::vector<ReplayPresentationSample>& samples,
                                                                ReplayFrameIndex frameIndex )
{
    for ( const ReplayPresentationSample& sample : samples )
    {
        if ( sample.frameIndex == frameIndex )
        {
            return &sample;
        }
    }
    return nullptr;
}

void WriteReplayProbeReason( char* outReason, std::size_t reasonSize, const char* reason )
{
    if ( outReason && reasonSize > 0 )
    {
        strncpy_s( outReason, reasonSize, reason ? reason : "event replay failed", _TRUNCATE );
    }
}

class ScopedReplayProbeProfilerFrame
{
  public:
    ScopedReplayProbeProfilerFrame()
    {
        PROFILE_FRAME_BEGIN();
    }
    ~ScopedReplayProbeProfilerFrame()
    {
        PROFILE_FRAME_END();
    }
};
} // namespace

void Run::Execute()
{
    MSG msg;

    for ( ;; )
    {
        if ( PeekMessage( &msg, nullptr, 0, 0, PM_REMOVE ) )
        {
            if ( msg.message == WM_QUIT )
            {
                if ( m_graphicsStress.enabled )
                {
                    printf( "[graphics-stress] WM_QUIT received at frame=%d scene_frame=%d scene_loads=%d\n",
                            m_graphicsStress.framesRun,
                            SceneState().currentFrame,
                            m_graphicsStress.sceneLoadsRequested );
                    fflush( stdout );
                }
                break;
            }
            TranslateMessage( &msg );
            DispatchMessage( &msg );
        }
        else
        {
            RuntimeAllocation::RuntimeAllocationScope frameAllocationScope(
                RuntimeAllocation::RuntimeAllocationPhase::SteadyGameplay );
            double secondsPerFrame = m_timers.frameTimer.GetElapsedTime();
            secondsPerFrame = std::clamp( secondsPerFrame, 0.0, 0.05 );

            m_timers.frameTimer.StartTimer();
            PROFILE_FRAME_BEGIN();
            m_timers.workTimer.StartTimer();
            // Lifetime: borrow the startup-owned renderer once for this frame
            // turn. Narrow facets keep reset, GPU-drain, UI accounting, and
            // present from each reaching through the process-global service.
            if ( !m_renderBackendView.deviceLifecycle || !m_renderBackendView.renderDiagnostics ||
                 !m_renderBackendView.renderResources || !m_renderBackendView.renderCommands )
            {
                throw std::runtime_error( "Run::Execute requires a render backend" );
            }
            SkullbonezCore::Rendering::IRenderDiagnostics& frameRenderDiagnostics =
                *m_renderBackendView.renderDiagnostics;
            SkullbonezCore::Rendering::IRenderDeviceLifecycle& renderLifecycle = *m_renderBackendView.deviceLifecycle;
            SkullbonezCore::Rendering::IRenderResourceFactory& frameRenderResources =
                *m_renderBackendView.renderResources;
            SkullbonezCore::Rendering::IRenderCommandContext& frameRenderCommands = *m_renderBackendView.renderCommands;
            const SkullbonezCore::UI::UIRenderContext uiRender = { &m_systems.assets,
                                                                   &frameRenderResources,
                                                                   &frameRenderCommands };
            frameRenderDiagnostics.ResetFrameDrawCalls();

            PROFILE_BEGIN( "Frame/Input" );
            TickInteractionAutomationBeforeInput();
            TakeInput();
            TickLiveStyleControl();
            PROFILE_END( "Frame/Input" );

            m_cGameModelCollection.BeginCollisionVisualFrame();
            {
                RuntimeAllocation::RuntimeAllocationScope allocationScope(
                    RuntimeAllocation::RuntimeAllocationPhase::Physics );
                TickPhysics( secondsPerFrame );
            }

            PROFILE_BEGIN( "Frame/PostPhysics" );

            PROFILE_BEGIN( "Frame/PostPhysics/BroadphaseVisualizer" );
            // Update broadphase visualizer state (runs even when overlay is hidden so fades are correct)
            {
                m_broadphaseVisualizer.SetEnabled( m_debug.isBroadphaseOverlay );
                m_broadphaseVisualizer.SetCellSize( m_cGameModelCollection.GetSpatialGrid().GetCellSize() );
                const SpatialGrid& grid = m_cGameModelCollection.GetSpatialGrid();
                SpatialGrid::ActiveCell activeCellBuf[SpatialGrid::MAX_BUCKETS];
                int activeCellCount = grid.GetActiveCellCount();
                grid.GetActiveCells( activeCellBuf, SpatialGrid::MAX_BUCKETS );
                const std::vector<int64_t>& collisionKeys = m_cGameModelCollection.GetCollisionCellKeys();
                m_broadphaseVisualizer.Update( static_cast<float>( secondsPerFrame ),
                                               activeCellBuf,
                                               activeCellCount,
                                               collisionKeys.data(),
                                               static_cast<int>( collisionKeys.size() ) );
                UpdateRequiredSceneBroadphaseXCells( activeCellBuf,
                                                     (std::min)( activeCellCount, SpatialGrid::MAX_BUCKETS ) );
            }
            PROFILE_END( "Frame/PostPhysics/BroadphaseVisualizer" );

            PROFILE_BEGIN( "Frame/PostPhysics/CollisionVisualizer" );
            m_collisionVisualizer.SetEnabled( m_debug.isCollisionVisualizer );
            m_cGameModelCollection.UpdateCollisionVisualizer( m_collisionVisualizer,
                                                              static_cast<float>( secondsPerFrame ) );
            PROFILE_END( "Frame/PostPhysics/CollisionVisualizer" );

            PROFILE_BEGIN( "Frame/PostPhysics/PhysicsDebugVisualizer" );
            m_physicsDebugVisualizer.SetFlags( m_debug.physicsDebugFlags );
            m_physicsDebugVisualizer.SetContactLingerSeconds( m_debug.physicsDebugContactLinger );
            m_physicsDebugVisualizer.SetPipelineStageCursor( m_debug.physicsDebugPipelineStageCursor );
            m_cGameModelCollection.UpdatePhysicsDebugVisualizer( m_physicsDebugVisualizer,
                                                                 static_cast<float>( secondsPerFrame ) );
            UpdateRequiredSceneContacts();
            PROFILE_END( "Frame/PostPhysics/PhysicsDebugVisualizer" );

            PROFILE_BEGIN( "Frame/PostPhysics/EndCollisionVisualFrame" );
            m_cGameModelCollection.EndCollisionVisualFrame();
            PROFILE_END( "Frame/PostPhysics/EndCollisionVisualFrame" );

            PROFILE_END( "Frame/PostPhysics" );

            // Concept: graphics stress is render/runtime churn, not UI command
            // processing. Tick it once per rendered frame so headless and
            // overnight launches keep mutating DX12 state even when the UI
            // command panel is not producing control messages.
            RunGraphicsStressActions( frameRenderDiagnostics );

            if ( m_runtimeSettings.isPipelineSyncEnabled )
            {
                PROFILE_BEGIN( "Frame/PipelineSync" );
                {
                    RuntimeAllocation::RuntimeAllocationScope allocationScope(
                        RuntimeAllocation::RuntimeAllocationPhase::Render );
                    renderLifecycle.Finish();
                }
                PROFILE_END( "Frame/PipelineSync" );
            }

            RuntimeRenderModelFrameView renderModels = BuildRuntimeRenderModelFrameView();

            PROFILE_BEGIN( "Frame/Render" );
            {
                RuntimeAllocation::RuntimeAllocationScope allocationScope(
                    RuntimeAllocation::RuntimeAllocationPhase::Render );
                DRAW_CALL_TRACE_SCOPE( "Frame/Render" );
                Render( renderModels );
            }
            PROFILE_END( "Frame/Render" );

            // Lifetime: the UI text pass borrows these Run-owned objects for
            // this late frame only. ShouldRender samples only flow/UI toggles;
            // RefreshRuntimeViewModel below updates the referenced view before
            // the pass builds draw data.
            const RunSceneBrowserState& uiSceneBrowser = m_sceneController.Browser();
            const std::string* uiScenePath = m_sceneController.CurrentPath();
            const UiTextPassState uiTextState{
                m_debug,
                m_timers,
                SceneState(),
                m_runtimeSettings,
                m_config,
                m_cWorldEnvironment,
                m_runtimeTools.RayCastTest(),
                m_runtimeTools.Editor(),
                m_UI,
                m_runtimeInput,
                m_camera,
                m_runtimeViewModel,
                uiSceneBrowser,
                m_systems.renderPasses,
                m_systems.workerPool,
                RuntimeWindowScreenWidth( m_systems, m_config ),
                RuntimeWindowScreenHeight( m_systems, m_config ),
                m_sceneController.QueueSize(),
                m_sceneController.HasCurrentEntry(),
                uiScenePath ? uiScenePath->c_str() : nullptr,
                CurrentSceneBrowserIndex( m_sceneController, uiSceneBrowser ),
                CameraModeEnabledMask(),
                CameraModeLabel( m_camera.mode ),
                m_runtimeTools.LauncherFireModeLabel(),
                IsLauncherCameraMode(),
                m_replayRuntime.ShouldRenderScrubber( m_runtimeTools.Editor().editorModeEnabled,
                                                      m_UI.IsVisible(),
                                                      m_UI.IsMinimized() ),
                m_replayRuntime.HasPathVisualizerTarget() };

            if ( m_renderer.ShouldRenderUiText( uiTextState ) )
            {
                RefreshRuntimeViewModel();
                const CinematicRenderConfig& uiCinematic = RuntimeActiveCinematicConfig( SceneState(), m_config );
                const bool uiCinematicRendering =
                    RuntimeCinematicRenderingEnabled( SceneState(), m_config, m_launchOptions, m_debug, true );
                const ReplayOverlayFrameState replayOverlay{
                    m_runtimeTools.Editor().editorModeEnabled,
                    m_UI.IsVisible(),
                    m_UI.IsMinimized(),
                    SceneState().isScenePhysics,
                    RuntimeWindowScreenWidth( m_systems, m_config ),
                    RuntimeWindowScreenHeight( m_systems, m_config ),
                    m_timers.simulationTimer.GetTotalTime(),
                };
                const int uiDrawCallStart = frameRenderDiagnostics.GetFrameDrawCallCount();
                PROFILE_BEGIN( "Frame/UI" );
                {
                    RuntimeAllocation::RuntimeAllocationScope allocationScope(
                        RuntimeAllocation::RuntimeAllocationPhase::Render );
                    DRAW_CALL_TRACE_SCOPE( "Frame/UI" );
                    m_renderer.RenderUiText( frameRenderDiagnostics,
                                             uiRender,
                                             uiTextState,
                                             renderModels,
                                             m_diagnosticsRuntime,
                                             m_replayRuntime,
                                             replayOverlay,
                                             uiCinematic,
                                             uiCinematicRendering,
                                             secondsPerFrame );
                }
                PROFILE_END( "Frame/UI" );
                const int uiDrawCallEnd = frameRenderDiagnostics.GetFrameDrawCallCount();
                m_timers.lastUIDrawCalls = (std::max)( 0, uiDrawCallEnd - uiDrawCallStart );
            }
            else
            {
                m_timers.lastUIDrawCalls = 0;
            }

            PROFILE_BEGIN( "Frame/PostDraw/LiveStyleCapture" );
            {
                RuntimeAllocation::RuntimeAllocationScope allocationScope(
                    RuntimeAllocation::RuntimeAllocationPhase::Capture );
                TickLiveStyleControlCapture();
            }
            PROFILE_END( "Frame/PostDraw/LiveStyleCapture" );

            PROFILE_BEGIN( "Frame/PostDraw/InteractionAutomation" );
            TickInteractionAutomationAfterRender();
            PROFILE_END( "Frame/PostDraw/InteractionAutomation" );

            {
                RuntimeAllocation::RuntimeAllocationScope allocationScope(
                    RuntimeAllocation::RuntimeAllocationPhase::Capture );
                if ( TickScreenshots() )
                {
                    continue;
                }
            }

            PROFILE_BEGIN( "Frame/PostDraw/AutoCycle" );
            TickAutoCycle();
            PROFILE_END( "Frame/PostDraw/AutoCycle" );

            m_timers.workTimer.StopTimer();
            m_timers.cpuFrameWorkMs =
                static_cast<float>( std::clamp( m_timers.workTimer.GetElapsedTime(), 0.0, 0.25 ) * 1000.0 );

            PROFILE_BEGIN( "Frame/VsyncWait" );
            {
                RuntimeAllocation::RuntimeAllocationScope allocationScope(
                    RuntimeAllocation::RuntimeAllocationPhase::Render );
                renderLifecycle.Present();
            }
            PROFILE_END( "Frame/VsyncWait" );

            m_timers.frameTimer.StopTimer();
            PROFILE_FRAME_END();

#if defined( SKULLBONEZ_PROFILE_ENABLED )
            {
                const RuntimeProfilerFrameTimes profilerTimes = RuntimeDiagnostics::SampleProfilerFrameTimes();
                m_timers.physicsTime = profilerTimes.physicsTimeSeconds;
                m_timers.renderTime = profilerTimes.renderTimeSeconds;
                m_timers.gpuFrameWorkMs = profilerTimes.gpuFrameWorkMs;
            }
#endif

            m_diagnosticsRuntime.TickPerfLog( RuntimePerfTickContext{ sPerfPass + 1,
                                                                      SceneState().currentFrame + 1,
                                                                      m_timers.physicsTime,
                                                                      m_timers.renderTime } );

            if ( TickSceneAdvance() )
            {
                continue;
            }
        }
    }
}


void Run::TickPhysics( double secondsPerFrame )
{
    if ( m_replayRuntime.IsScrubPaused() )
    {
        PROFILE_SCOPED( "Frame/Replay/ScrubCamera" );
        UpdateLogic( 0.0f, static_cast<float>( secondsPerFrame ) );
        return;
    }

    const bool replayLiveAdvanceHeld = m_replayRuntime.Scrubber().liveAdvanceHeld;
    const bool stepRequested = Input::IsKeyDown( VK_SPACE );
    const bool replayCapture = m_replayRuntime.IsCaptureEnabled();
#ifdef _DEBUG
    const bool physicsCapture = m_diagnosticsRuntime.PerfLog().physicsRegressionLogOverride[0] != '\0' ||
                                m_diagnosticsRuntime.PerfLog().physicsCollisionTimeLogOverride[0] != '\0' ||
                                m_diagnosticsRuntime.PhysicsDiagnostics().isEnabled;
#else
    constexpr bool physicsCapture = false;
#endif
    RuntimeInteractionFramePolicy policy = m_interaction.BuildFramePolicy(
        RuntimeInteractionFrameInput{ SceneState().isScenePhysics,
                                      stepRequested,
                                      false,
                                      replayLiveAdvanceHeld,
                                      Input::IsRightMouseDown(),
                                      m_runtimeTools.Editor().viewportLookActive,
                                      m_replayRuntime.InspectionMouseLookActive( Input::IsRightMouseDown(),
                                                                                 m_UI.WantsNativeMouseCursor(),
                                                                                 m_UI.BlocksCameraMouse() ),
                                      physicsCapture,
                                      SceneState().timeScale } );
    if ( m_debug.isCrossScenePauseLocked )
    {
        // Invariant: the P-key pause lock outranks camera/tool mode. Launcher
        // and passive scene cameras normally keep physics running, but the lock
        // requires Space before any simulation step can proceed.
        policy.physicsAdvance = PhysicsAdvanceState::RunWhileStepHeld;
        if ( !stepRequested )
        {
            policy.physicsTimeScale = 0.0f;
        }
    }
    const bool manipulatorPhysics = policy.manipulatorActive;
    const bool contactAudioStep = m_contactAudio.IsEnabled();
    const auto physicsWorldForces = m_cWorldEnvironment.GetPhysicsWorldForces();
    const bool canStepPhysics = m_systems.config != nullptr && m_systems.workerPool != nullptr;
    const SimulationTickResult tick = m_simulation.Tick( SimulationTickInput{ secondsPerFrame,
                                                                              policy.physicsTimeScale,
                                                                              SceneState().isSceneMode,
                                                                              SceneState().isScenePhysics,
                                                                              SceneState().isFixedStep,
                                                                              policy.physicsAdvance,
                                                                              stepRequested,
                                                                              canStepPhysics } );
    if ( tick.committedPhysicsTicks > 0 && canStepPhysics )
    {
        PROFILE_BEGIN( "Frame/Physics" );
        // Why: SimulationSystem now returns only a deterministic tick count.
        // Runtime executes the store-owned physics step directly, then applies
        // the remaining model-owned presentation sync as explicit edge work.
        for ( int tickIndex = 0; tickIndex < tick.committedPhysicsTicks; ++tickIndex )
        {
            PROFILE_SCOPED( "Frame/Physics/Step" );
            if ( manipulatorPhysics )
            {
                ApplyMousePickupPhysicsStep();
            }

            StepRuntimePhysicsTick( m_cGameModelCollection,
                                    PHYSICS_FIXED_DT,
                                    *m_systems.config,
                                    physicsWorldForces,
                                    *m_systems.workerPool );

            if ( manipulatorPhysics || replayCapture || contactAudioStep )
            {
                AfterPhysicsStep();
            }
        }
        PROFILE_END( "Frame/Physics" );
    }
    m_runtimeTools.TickRayCastTestLines( static_cast<float>( secondsPerFrame ) );
    m_runtimeTools.Laser().Update( static_cast<float>( secondsPerFrame ) );
    if ( tick.shouldUpdateLogic )
    {
        UpdateLogic( tick.simulationDt, tick.cameraDt );
    }
}


void Run::AfterPhysicsStep()
{
    RestoreMousePickupAngularVelocity();
    if ( m_contactAudio.IsEnabled() )
    {
        PROFILE_SCOPED( "Frame/Physics/Step/ContactAudio" );

        const Vector3 listenerPosition =
            m_systems.cameras ? m_systems.cameras->GetRenderCameraTranslation() : Math::Vector::ZERO_VECTOR;
        m_contactAudio.BeginPhysicsStep( PHYSICS_FIXED_DT, listenerPosition );

        const auto& colliderRecords = m_cGameModelCollection.GetPhysicsEngine().Colliders().Records();
        auto materialForBody = [&]( int bodyIndex ) -> uint32_t
        {
            if ( bodyIndex >= 0 && bodyIndex < static_cast<int>( colliderRecords.size() ) )
            {
                return colliderRecords[static_cast<std::size_t>( bodyIndex )].contactMaterialId;
            }
            return HashStr( "default" );
        };

        if ( m_contactAudio.SimpleModeEnabled() )
        {
            // Why: Simple Mode answers the practical sound question directly:
            // did a dynamic body experience enough mass-scaled linear velocity
            // change to be heard? Motion comes from PhysicsBodyStore and contact
            // material comes from the paired ColliderStore row.
            const auto& bodyRecords = m_cGameModelCollection.GetPhysicsEngine().BodyStore().Records();
            const int simpleBodyCount = static_cast<int>(
                bodyRecords.size() < colliderRecords.size() ? bodyRecords.size() : colliderRecords.size() );
            m_contactAudio.BeginSimpleLinearStep( simpleBodyCount );
            for ( int bodyIndex = 0; bodyIndex < simpleBodyCount; ++bodyIndex )
            {
                const PhysicsBodyRecord& body = bodyRecords[static_cast<std::size_t>( bodyIndex )];
                if ( body.isFixed )
                {
                    continue;
                }
                m_contactAudio.SubmitLinearMotion(
                    bodyIndex,
                    colliderRecords[static_cast<std::size_t>( bodyIndex )].contactMaterialId,
                    body.position,
                    body.linearVelocity,
                    body.mass );
            }
        }
        else
        {
            // Why: PhysicsDebugContact rows are emitted after accumulated normal
            // impulses are known. Audio can consume those facts without entering
            // solver math or changing deterministic physics state.
            const std::vector<PhysicsDebugContact>& contacts = m_cGameModelCollection.GetPhysicsDebugContacts();
            for ( const PhysicsDebugContact& contact : contacts )
            {
                if ( contact.bodyA < 0 || contact.normalImpulse <= 0.0f )
                {
                    continue;
                }

                Runtime::Audio::ContactAudioEvent event;
                event.bodyA = contact.bodyA;
                event.bodyB = contact.bodyB;
                event.featureId = contact.featureId;
                event.materialA = materialForBody( contact.bodyA );
                event.materialB = materialForBody( contact.bodyB );
                event.point = contact.point;
                event.normal = contact.normal;
                event.normalImpulse = contact.normalImpulse;
                // Why: sound uses pre-solve relative motion so stationary wall bricks
                // receiving propagated constraint force do not all become emitters.
                event.normalClosingSpeed = contact.preSolveClosingSpeed;
                event.tangentSlipSpeed = contact.preSolveSlipSpeed;
                event.isTerrain = contact.bodyB < 0;
                event.hasMotionData = true;
                m_contactAudio.SubmitContact( event );
            }
        }

        m_contactAudio.EndPhysicsStep();
#ifdef _DEBUG
        if ( m_diagnosticsRuntime.PhysicsDiagnosticsEnabled() )
        {
            RuntimeDiagnostics::LogContactAudioStepStats( m_diagnosticsRuntime.PhysicsDiagnostics(),
                                                          SceneState(),
                                                          m_contactAudio.StepStats() );
            const int decisionCount = m_contactAudio.DecisionCount();
            for ( int i = 0; i < decisionCount; ++i )
            {
                Runtime::Audio::ContactAudioDecision decision;
                if ( m_contactAudio.GetDecision( i, decision ) )
                {
                    RuntimeDiagnostics::LogContactAudioDecision( m_diagnosticsRuntime.PhysicsDiagnostics(),
                                                                 SceneState(),
                                                                 decision );
                }
            }
        }
#endif
        if ( m_runtimeSettings.contactAudioFlashMode != ContactAudioFlashMode::Off )
        {
            // Why: Sound-tab diagnostics can visualize emitted sounds, all
            // candidates, or rejected candidates without touching physics state.
            constexpr float CONTACT_AUDIO_FLASH_SECONDS = 0.1f;
            const int decisionCount = m_contactAudio.DecisionCount();
            for ( int i = 0; i < decisionCount; ++i )
            {
                Runtime::Audio::ContactAudioDecision decision;
                if ( !m_contactAudio.GetDecision( i, decision ) ||
                     !ShouldFlashContactAudioDecision( m_runtimeSettings.contactAudioFlashMode, decision ) )
                {
                    continue;
                }

                m_cGameModelCollection.NotifyAudioContact( decision.event.bodyA, CONTACT_AUDIO_FLASH_SECONDS );
                m_cGameModelCollection.NotifyAudioContact( decision.event.bodyB, CONTACT_AUDIO_FLASH_SECONDS );
            }
        }
        if ( m_runtimeSettings.contactAudioDebugCounters )
        {
            m_timers.contactAudioStatsLogTime += PHYSICS_FIXED_DT;
            if ( m_timers.contactAudioStatsLogTime >= 1.0f )
            {
                const Runtime::Audio::ContactAudioStats& stats = m_contactAudio.Stats();
                printf( "[audio] contact stats facts=%u patches=%u merged=%u threshold=%u cooldown=%u "
                        "submitted=%u rolling=%u/%u budget=%u dropped=%u\n",
                        stats.eventsSeen,
                        stats.patchCandidates,
                        stats.mergedCandidates,
                        stats.rejectedByThreshold,
                        stats.rejectedByCooldown,
                        stats.submittedVoices,
                        stats.rollingSubmittedVoices,
                        stats.rollingCandidates,
                        stats.candidateOverflows + stats.burstWindowSkippedCandidates + stats.budgetRejectedCandidates,
                        stats.droppedVoices );
                m_contactAudio.ResetFrameStats();
                m_timers.contactAudioStatsLogTime = 0.0f;
            }
        }
    }
    if ( m_replayRuntime.IsCaptureEnabled() )
    {
        RuntimeAllocation::RuntimeAllocationScope allocationScope( RuntimeAllocation::RuntimeAllocationPhase::Replay );
        PROFILE_SCOPED( "Frame/Physics/Step/ReplayCapture" );
        m_runtimeTools.BuildReplayLauncherVisualSample( m_replayLauncherVisualScratch );

        ReplayCaptureInput input;
        input.sceneFrame = SceneState().currentFrame;
        input.simulationSeconds = m_timers.simulationTimer.GetTimeSinceLastStart();
        input.physicsDt = PHYSICS_FIXED_DT;
        input.fixedStep = SceneState().isFixedStep;
        input.scenePhysicsEnabled = SceneState().isScenePhysics;
        input.sceneTextEnabled = SceneState().isSceneText;
        input.waterHidden = m_debug.isWaterHidden;
        input.terrainHidden = m_debug.isTerrainHidden;
        input.cameras = m_systems.cameras;
        input.world = &m_cWorldEnvironment;
        input.models = &m_cGameModelCollection;
        input.bodyStore = &m_cGameModelCollection.GetPhysicsEngine().BodyStore();
        input.colliderStore = &m_cGameModelCollection.GetPhysicsEngine().Colliders();
        input.launcherVisual = &m_replayLauncherVisualScratch;
        m_replayRuntime.CaptureFrame( input );
        CompareLatestReplaySamples( m_replayRuntime, m_solverReplayMismatch );
#ifdef _DEBUG
        TickReplayScrubProbe();
        TickReplayRestoreProbe();
        TickReplaySaveProbe();
#endif
    }
}


#ifdef _DEBUG
void Run::TickReplayScrubProbe()
{
    auto distanceSquared = []( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b ) -> float
    {
        const Math::Vector::Vector3 delta = a - b;
        return delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    };

    if ( !m_replayScrubProbe.enabled || m_replayScrubProbe.completed )
    {
        return;
    }

    const ReplayRecorderStats stats = m_replayRuntime.Presentation().GetStats();
    if ( stats.sampleCount < static_cast<std::size_t>( m_replayScrubProbe.minSampleCount ) )
    {
        return;
    }

    const ReplayPresentationSample* selected =
        m_replayRuntime.Presentation().SampleAtNormalized( m_replayScrubProbe.normalized );
    const ReplayPresentationSample* live = m_replayRuntime.Presentation().LatestSample();
    if ( !selected || !live || selected->frameIndex >= live->frameIndex )
    {
        throw std::runtime_error( "replay scrub probe could not select an older replay sample" );
    }

    const ReplayBodyPresentationSample* selectedBody = nullptr;
    const ReplayBodyPresentationSample* liveBody = nullptr;
    float bestDistanceSquared = 0.0f;
    for ( const ReplayBodyPresentationSample& candidate : selected->bodies )
    {
        if ( candidate.fixed )
        {
            continue;
        }

        for ( const ReplayBodyPresentationSample& liveCandidate : live->bodies )
        {
            if ( liveCandidate.id.value != candidate.id.value )
            {
                continue;
            }

            const float candidateDistanceSquared = distanceSquared( liveCandidate.position, candidate.position );
            if ( candidateDistanceSquared > bestDistanceSquared )
            {
                bestDistanceSquared = candidateDistanceSquared;
                selectedBody = &candidate;
                liveBody = &liveCandidate;
            }
            break;
        }
    }

    if ( !selectedBody || !liveBody || bestDistanceSquared < m_replayScrubProbe.minDistanceSquared )
    {
        throw std::runtime_error( "replay scrub probe did not find a moved body in the selected replay window" );
    }

    const int probedModelIndex = liveBody->modelIndex;
    const PhysicsBodyRecord* probedBody = TryGetReplayProbeBodyRecord( m_cGameModelCollection, probedModelIndex );
    if ( !probedBody )
    {
        throw std::runtime_error( "replay scrub probe selected an invalid live body index" );
    }

    // Why: scrub probes prove presentation overrides do not mutate live
    // simulation state. Read that state from PhysicsBodyStore so the proof does
    // not depend on temporary presentation rows.
    const Math::Vector::Vector3 preApplyPosition = probedBody->position;
    const float preLiveDeltaSquared = distanceSquared( preApplyPosition, liveBody->position );
    if ( preLiveDeltaSquared > m_replayScrubProbe.minDistanceSquared )
    {
        throw std::runtime_error(
            "replay scrub probe live body did not match the current replay sample before applying scrub state" );
    }

    const bool applied =
        ApplyReplayProbePresentationSampleForRender( m_cGameModelCollection, m_replayRuntime, *selected );
    if ( !applied )
    {
        throw std::runtime_error( "replay scrub probe failed to apply the selected presentation sample" );
    }
    const PhysicsBodyRecord* appliedBody = TryGetReplayProbeBodyRecord( m_cGameModelCollection, probedModelIndex );
    if ( !appliedBody )
    {
        RestoreReplayProbeRenderInstances( m_cGameModelCollection );
        throw std::runtime_error( "replay scrub probe lost the selected live body after applying scrub state" );
    }
    const Math::Vector::Vector3 liveAfterApplyPosition = appliedBody->position;
    const float livePreservedDeltaSquared = distanceSquared( liveAfterApplyPosition, preApplyPosition );
    if ( livePreservedDeltaSquared > m_replayScrubProbe.minDistanceSquared )
    {
        RestoreReplayProbeRenderInstances( m_cGameModelCollection );
        throw std::runtime_error( "replay scrub probe mutated the live body while applying scrub state" );
    }

    Math::Vector::Vector3 appliedRenderPosition;
    if ( !TryPrepareReplayProbeRenderPosition( m_cGameModelCollection, probedModelIndex, appliedRenderPosition ) )
    {
        RestoreReplayProbeRenderInstances( m_cGameModelCollection );
        throw std::runtime_error( "replay scrub probe lost the selected render instance after applying scrub state" );
    }
    const float appliedDeltaSquared = distanceSquared( appliedRenderPosition, selectedBody->position );
    if ( appliedDeltaSquared > m_replayScrubProbe.minDistanceSquared )
    {
        RestoreReplayProbeRenderInstances( m_cGameModelCollection );
        throw std::runtime_error( "replay scrub probe did not move the render instance to the selected replay sample" );
    }

    RestoreReplayProbeRenderInstances( m_cGameModelCollection );
    const PhysicsBodyRecord* restoredBody = TryGetReplayProbeBodyRecord( m_cGameModelCollection, probedModelIndex );
    if ( !restoredBody )
    {
        throw std::runtime_error( "replay scrub probe lost the selected live body after restoring scrub state" );
    }
    const Math::Vector::Vector3 restoredPosition = restoredBody->position;
    const float restoredDeltaSquared = distanceSquared( restoredPosition, preApplyPosition );
    const bool restored = restoredDeltaSquared <= m_replayScrubProbe.minDistanceSquared;
    if ( !restored )
    {
        throw std::runtime_error(
            "replay scrub probe did not restore the live model after applying the selected sample" );
    }

    m_diagnosticsRuntime.LogReplayScrubProbe( SceneState(),
                                              *selected,
                                              *live,
                                              *selectedBody,
                                              *liveBody,
                                              m_replayScrubProbe.normalized,
                                              bestDistanceSquared,
                                              applied,
                                              restored,
                                              preLiveDeltaSquared,
                                              appliedDeltaSquared,
                                              restoredDeltaSquared );

    m_replayScrubProbe.completed = true;
    printf(
        "[replay] Scrub probe passed: selected_replay_frame=%llu live_replay_frame=%llu body_id=%u distance_sq=%.6f\n",
        static_cast<unsigned long long>( selected->frameIndex ),
        static_cast<unsigned long long>( live->frameIndex ),
        selectedBody->id.value,
        bestDistanceSquared );
    PostQuitMessage( 0 );
}

void Run::TickReplayRestoreProbe()
{
    if ( !m_replayRestoreProbe.enabled || m_replayRestoreProbe.completed )
    {
        return;
    }

    const ReplayRecorderStats stats = m_replayRuntime.Solver().GetStats();
    if ( stats.sampleCount < static_cast<std::size_t>( m_replayRestoreProbe.minSampleCount ) )
    {
        return;
    }

    const ReplaySolverFrameSample* selectedSample =
        m_replayRuntime.Solver().SampleAtNormalized( m_replayRestoreProbe.normalized );
    const ReplaySolverFrameSample* latestSample = m_replayRuntime.Solver().LatestSample();
    if ( !selectedSample || !latestSample )
    {
        throw std::runtime_error( "replay restore probe could not select retained solver samples" );
    }
    if ( selectedSample->frameIndex >= latestSample->frameIndex )
    {
        throw std::runtime_error( "replay restore probe did not select an older solver sample" );
    }

    const ReplaySolverFrameSample selected = *selectedSample;
    const ReplayFrameIndex latestFrame = latestSample->frameIndex;
    const uint64_t selectedHash = selected.solverHash;
    char reason[160] = {};
    const bool restored = RestoreReplaySolverSampleAsLive( selected, reason, sizeof( reason ) );
    if ( !restored )
    {
        char message[224] = {};
        sprintf_s( message,
                   sizeof( message ),
                   "replay restore probe failed: %s",
                   reason[0] != '\0' ? reason : "unknown restore failure" );
        throw std::runtime_error( message );
    }

    m_replayRestoreProbe.completed = true;
    printf( "[replay] Restore probe passed: target_replay_frame=%llu previous_live_replay_frame=%llu "
            "solver_hash=0x%016llX\n",
            static_cast<unsigned long long>( selected.frameIndex ),
            static_cast<unsigned long long>( latestFrame ),
            static_cast<unsigned long long>( selectedHash ) );
    PostQuitMessage( 0 );
}

void Run::TickReplaySaveProbe()
{
    auto distanceSquared = []( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b ) -> float
    {
        const Math::Vector::Vector3 delta = a - b;
        return delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    };

    if ( !m_replaySaveProbe.enabled || m_replaySaveProbe.completed )
    {
        return;
    }

    const ReplayRecorderStats stats = m_replayRuntime.Presentation().GetStats();
    if ( !m_replaySaveProbe.runtimeResetCoverageInjected && stats.sampleCount >= 4 )
    {
        m_replaySaveProbe.runtimeResetCoverageInjected = true;
        m_replaySaveProbe.eventCoverageInjected = false;
        m_runtimeCommands.Push( RuntimeCommand{ RuntimeCommandType::ResetCurrentScene } );
        return;
    }

    if ( !m_replaySaveProbe.eventCoverageInjected && stats.sampleCount >= 4 )
    {
        m_replaySaveProbe.eventCoverageInjected = true;
        const float currentGravity = m_cWorldEnvironment.GetGravity();
        const float probeGravity = currentGravity != 0.0f ? currentGravity * 0.95f : -0.25f;
        ApplyUIWorldOverride( m_cWorldEnvironment,
                              m_replayRuntime,
                              probeGravity,
                              m_cWorldEnvironment.GetFluidSurfaceHeight(),
                              m_cWorldEnvironment.GetFluidDensity() );
        m_runtimeTools.Editor().placementScale = Vector3( 2.0f, 2.0f, 2.0f );
        m_runtimeTools.Editor().autoTerrainAlign = false;
        const int modelCountBeforePlace = m_cGameModelCollection.GetModelCount();
        EditorObjectPlacementContext placementContext{ m_runtimeTools.Editor(),
                                                       m_cGameModelCollection,
                                                       SceneState(),
                                                       m_cWorldEnvironment,
                                                       m_systems.terrain.get(),
                                                       m_systems.assets,
                                                       m_startup.gameModelCapacity };
        EditorObjectPlacementRequest placementRequest{ UI::EditorTab::OBJECT_BOX, true, Vector3( 18.0f, 0.0f, 18.0f ) };
        EditorObjectPlacementResult placementResult;
        if ( CanPlaceEditorObjectAtTerrainPoint( placementContext, placementRequest ) )
        {
            EnterInteractiveSceneRun();
            PlaceEditorObjectAtTerrainPoint( placementContext, placementRequest, placementResult );
        }
        if ( placementResult.placed )
        {
            m_replayRuntime.RecordEditorPlaceEvent( placementResult.objectType,
                                                    placementResult.fixedObject,
                                                    placementResult.autoTerrainAlign,
                                                    placementResult.modelCountBefore,
                                                    placementResult.terrainPoint,
                                                    placementResult.placementScale,
                                                    placementResult.placementYawRadians );
            const PhysicsBodyRecord* placedBodyBeforeEdit =
                m_cGameModelCollection.GetPhysicsBodyStore().RecordForHandle( placementResult.placedBody );
            if ( !placedBodyBeforeEdit )
            {
                throw std::runtime_error( "replay save probe failed to resolve placed body record" );
            }
            // Why: placement has already registered a PhysicsBodyHandle. Use the
            // authoritative body row as the starting transform, then commit the
            // edited descriptor back into the stores below.
            SkullbonezCore::GameObjects::PhysicsBodyStateEdit placedBodyEdit;
            placedBodyEdit.hasPosition = true;
            placedBodyEdit.position = placedBodyBeforeEdit->position + Vector3( 4.0f, 0.0f, 0.0f );
            Quaternion placedOrientation = placedBodyBeforeEdit->orientation;
            placedOrientation.RotateAboutAxis( Vector3( 0.0f, 1.0f, 0.0f ), 0.25f );
            placedBodyEdit.hasOrientation = true;
            placedBodyEdit.orientation = placedOrientation;
            const ColliderRecord* placedColliderBeforeEdit =
                TryGetEditorTransformColliderRecord( m_cGameModelCollection,
                                                     placementResult.placedCollider,
                                                     modelCountBeforePlace,
                                                     placedBodyBeforeEdit->replayBodyId );
            if ( !placedColliderBeforeEdit )
            {
                throw std::runtime_error( "replay save probe failed to resolve placed collider record" );
            }
            const CollisionShape placedShapeBeforeScale = placedColliderBeforeEdit->shape;
            constexpr int PROBE_SCALE_AXIS = 0;
            constexpr float PROBE_SCALE_FACTOR = 1.5f;
            CollisionShape placedShapeAfterScale;
            if ( !ScaleShapeAxisFromBase( placedShapeBeforeScale,
                                          PROBE_SCALE_AXIS,
                                          PROBE_SCALE_FACTOR,
                                          placedShapeAfterScale ) )
            {
                throw std::runtime_error( "replay save probe failed to apply editor transform scale" );
            }
            placedBodyEdit.hasLinearVelocity = true;
            placedBodyEdit.linearVelocity = Vector3( 0.0f, 0.0f, 0.0f );
            placedBodyEdit.hasAngularVelocity = true;
            placedBodyEdit.angularVelocity = Vector3( 0.0f, 0.0f, 0.0f );
            // Invariant: the replay probe exercises the same explicit collider
            // edit command as the editor instead of relying on a model recapture.
            m_cGameModelCollection.ApplyPhysicsBodyColliderEdit(
                modelCountBeforePlace,
                placedBodyEdit,
                MakeColliderCreateDesc( std::move( placedShapeAfterScale ),
                                        placedColliderBeforeEdit->restitution,
                                        placedColliderBeforeEdit->contactMaterialId ) );
            const PhysicsBodyRecord* placedBodyAfterEdit =
                m_cGameModelCollection.GetPhysicsBodyStore().RecordForModelIndex( modelCountBeforePlace );
            if ( !placedBodyAfterEdit || placedBodyAfterEdit->replayBodyId == 0 )
            {
                throw std::runtime_error( "replay save probe failed to capture edited body record" );
            }
            m_replayRuntime.RecordEditorTransformEvent(
                modelCountBeforePlace,
                REPLAY_EDITOR_TRANSFORM_TRANSLATE | REPLAY_EDITOR_TRANSFORM_ROTATE | REPLAY_EDITOR_TRANSFORM_SCALE,
                placedBodyAfterEdit->replayBodyId,
                placedBodyAfterEdit->position,
                placedBodyAfterEdit->orientation,
                m_cGameModelCollection.GetModelCount(),
                PROBE_SCALE_AXIS,
                PROBE_SCALE_FACTOR );
        }
        m_runtimeTools.RayCastTest().projectileSpeed += 1.0f;
        m_replayRuntime.RecordLauncherConfigEvent( 2u,
                                                   m_runtimeTools.RayCastTest().impulseStrength,
                                                   m_runtimeTools.RayCastTest().projectileSpeed );
        Vector3 rayOrigin;
        Vector3 rayDirection;
        Vector3 cameraUp;
        if ( m_runtimeTools.TryBuildLauncherCameraRay( m_systems.cameras, rayOrigin, rayDirection, cameraUp ) )
        {
            m_replayRuntime.RecordLauncherFireEvent(
                rayOrigin,
                rayDirection,
                cameraUp,
                m_runtimeTools.RayCastTest().fireMode == RunLauncherFireMode::Projectile,
                m_runtimeTools.RayCastTest().impulseStrength,
                m_runtimeTools.RayCastTest().projectileSpeed,
                m_cGameModelCollection.GetModelCount() );
            // Why: RuntimeTools now fails closed unless Run has completed the
            // cold collection-to-store topology repair at the owner boundary.
            const bool launcherStoresReady = m_cGameModelCollection.RepairPhysicsBodyAndColliderTopology();
            if ( launcherStoresReady && m_runtimeTools.FireLauncherRay( m_cGameModelCollection,
                                                                        SceneState(),
                                                                        m_systems.terrain.get(),
                                                                        m_startup.gameModelCapacity,
                                                                        rayOrigin,
                                                                        rayDirection,
                                                                        cameraUp ) )
            {
                SceneState().modelCount = m_cGameModelCollection.GetModelCount();
            }
        }
    }
    if ( stats.sampleCount < static_cast<std::size_t>( m_replaySaveProbe.minSampleCount ) )
    {
        return;
    }

    ReplayV2SaveResult result;
    if ( !m_replayRuntime.SavePresentationWithSolverHashes( m_replaySaveProbe.path, &result ) )
    {
        throw std::runtime_error( "replay save probe failed to write v2 presentation artifact" );
    }
    if ( result.solverHashCount < result.sampleCount )
    {
        throw std::runtime_error( "replay save probe wrote v2 artifact without a full solver hash track" );
    }
    if ( result.solverCheckpointCount == 0 )
    {
        throw std::runtime_error( "replay save probe wrote v2 artifact without solver checkpoint chunks" );
    }
    if ( result.eventCount == 0 )
    {
        throw std::runtime_error( "replay save probe wrote v2 artifact without event chunks" );
    }
    if ( result.eventCursorCount == 0 )
    {
        throw std::runtime_error( "replay save probe wrote v2 artifact without checkpoint event cursors" );
    }

    std::vector<ReplayPresentationSample> loadedSamples;
    ReplayV2LoadResult loadResult;
    if ( !ReplayV2Artifact::LoadPresentation( m_replaySaveProbe.path, loadedSamples, &loadResult ) )
    {
        throw std::runtime_error( "replay save probe failed to reload v2 presentation artifact" );
    }
    if ( loadedSamples.size() < 2 )
    {
        throw std::runtime_error( "replay save probe loaded too few v2 presentation samples" );
    }

    const std::size_t selectedIndex = (std::min)( loadedSamples.size() / 4, loadedSamples.size() - 2 );
    const ReplayPresentationSample& selected = loadedSamples[selectedIndex];
    const ReplayPresentationSample& live = loadedSamples.back();
    if ( selected.frameIndex >= live.frameIndex )
    {
        throw std::runtime_error( "replay save probe could not seek to an older loaded v2 sample" );
    }

    const ReplayBodyPresentationSample* selectedBody = nullptr;
    const ReplayBodyPresentationSample* liveBody = nullptr;
    float bestDistanceSquared = 0.0f;
    for ( const ReplayBodyPresentationSample& candidate : selected.bodies )
    {
        for ( const ReplayBodyPresentationSample& liveCandidate : live.bodies )
        {
            if ( liveCandidate.id.value != candidate.id.value )
            {
                continue;
            }

            const float candidateDistanceSquared = distanceSquared( liveCandidate.position, candidate.position );
            if ( candidateDistanceSquared > bestDistanceSquared )
            {
                bestDistanceSquared = candidateDistanceSquared;
                selectedBody = &candidate;
                liveBody = &liveCandidate;
            }
            break;
        }
    }
    if ( !selectedBody || !liveBody || bestDistanceSquared < 0.0001f )
    {
        throw std::runtime_error( "replay save probe did not find a moved body in the loaded v2 artifact" );
    }

    const int probedModelIndex = liveBody->modelIndex;
    const PhysicsBodyRecord* probedBody = TryGetReplayProbeBodyRecord( m_cGameModelCollection, probedModelIndex );
    if ( !probedBody )
    {
        throw std::runtime_error( "replay save probe loaded an invalid live body index" );
    }

    const Math::Vector::Vector3 preApplyPosition = probedBody->position;
    const float preLiveDeltaSquared = distanceSquared( preApplyPosition, liveBody->position );
    if ( preLiveDeltaSquared > 0.0001f )
    {
        throw std::runtime_error( "replay save probe live body did not match the loaded v2 live sample" );
    }

    const bool applied =
        ApplyReplayProbePresentationSampleForRender( m_cGameModelCollection, m_replayRuntime, selected );
    if ( !applied )
    {
        throw std::runtime_error( "replay save probe failed to apply the loaded v2 presentation sample" );
    }
    const PhysicsBodyRecord* appliedBody = TryGetReplayProbeBodyRecord( m_cGameModelCollection, probedModelIndex );
    if ( !appliedBody )
    {
        RestoreReplayProbeRenderInstances( m_cGameModelCollection );
        throw std::runtime_error( "replay save probe lost the selected live body after applying the v2 sample" );
    }
    const Math::Vector::Vector3 liveAfterApplyPosition = appliedBody->position;
    const float livePreservedDeltaSquared = distanceSquared( liveAfterApplyPosition, preApplyPosition );
    if ( livePreservedDeltaSquared > 0.0001f )
    {
        RestoreReplayProbeRenderInstances( m_cGameModelCollection );
        throw std::runtime_error( "replay save probe mutated the live body while applying the v2 sample" );
    }

    Math::Vector::Vector3 appliedRenderPosition;
    if ( !TryPrepareReplayProbeRenderPosition( m_cGameModelCollection, probedModelIndex, appliedRenderPosition ) )
    {
        RestoreReplayProbeRenderInstances( m_cGameModelCollection );
        throw std::runtime_error( "replay save probe lost the selected render instance after applying the v2 sample" );
    }
    const float appliedDeltaSquared = distanceSquared( appliedRenderPosition, selectedBody->position );
    if ( appliedDeltaSquared > 0.0001f )
    {
        RestoreReplayProbeRenderInstances( m_cGameModelCollection );
        throw std::runtime_error( "replay save probe did not move the render instance to the loaded v2 sample" );
    }

    RestoreReplayProbeRenderInstances( m_cGameModelCollection );
    const PhysicsBodyRecord* restoredBody = TryGetReplayProbeBodyRecord( m_cGameModelCollection, probedModelIndex );
    if ( !restoredBody )
    {
        throw std::runtime_error( "replay save probe lost the selected live body after restoring the v2 sample" );
    }
    const Math::Vector::Vector3 restoredPosition = restoredBody->position;
    const float restoredDeltaSquared = distanceSquared( restoredPosition, preApplyPosition );
    if ( restoredDeltaSquared > 0.0001f )
    {
        throw std::runtime_error( "replay save probe live body changed after applying the loaded v2 sample" );
    }

    m_replaySaveProbe.completed = true;
    printf( "[replay] Save probe wrote: path=%s samples=%llu bodies=%llu solver_hashes=%llu "
            "solver_checkpoints=%llu events=%llu event_cursors=%llu bytes=%llu\n",
            m_replaySaveProbe.path,
            static_cast<unsigned long long>( result.sampleCount ),
            static_cast<unsigned long long>( result.bodyDictionaryCount ),
            static_cast<unsigned long long>( result.solverHashCount ),
            static_cast<unsigned long long>( result.solverCheckpointCount ),
            static_cast<unsigned long long>( result.eventCount ),
            static_cast<unsigned long long>( result.eventCursorCount ),
            static_cast<unsigned long long>( result.fileBytes ) );
    printf( "[replay] Save probe loaded: samples=%llu bodies=%llu first_frame=%llu selected_frame=%llu "
            "latest_frame=%llu body_id=%u distance_sq=%.6f\n",
            static_cast<unsigned long long>( loadResult.sampleCount ),
            static_cast<unsigned long long>( loadResult.bodyDictionaryCount ),
            static_cast<unsigned long long>( loadResult.firstFrame ),
            static_cast<unsigned long long>( selected.frameIndex ),
            static_cast<unsigned long long>( live.frameIndex ),
            selectedBody->id.value,
            bestDistanceSquared );
    PostQuitMessage( 0 );
}

void Run::VerifyLoadedReplayPresentationProbe( float normalized )
{
    auto distanceSquared = []( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b ) -> float
    {
        const Math::Vector::Vector3 delta = a - b;
        return delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    };

    if ( !m_replayRuntime.HasLoadedPresentation() )
    {
        throw std::runtime_error( "replay load probe requires a loaded v2 presentation artifact" );
    }

    if ( m_replayRuntime.Scrubber().liveAdvanceHeld )
    {
        m_replayRuntime.SetLiveAdvanceHeld( false );
    }
    CancelReplayToolDragState();

    m_replayRuntime.ClearCameraFocusForRestore();
    ExitReplayInspectionCamera();
    const bool armed = m_replayRuntime.ArmLoadedPresentationScrubber( std::clamp( normalized, 0.0f, 1.0f ),
                                                                      m_timers.simulationTimer.GetTotalTime() );
    if ( !armed )
    {
        throw std::runtime_error( "replay load probe could not arm the loaded presentation scrubber" );
    }
    SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayScrub,
                                                        InteractionExitReason::EnterReplay );
    if ( m_replayRuntime.ShouldUseInspectionCamera() )
    {
        EnterReplayInspectionCamera();
    }
    else
    {
        ExitReplayInspectionCamera();
    }
    const ReplayPresentationSample* selected = m_replayRuntime.CurrentScrubSample();
    const ReplayPresentationSample* latest = m_replayRuntime.LoadedPresentationLatestSample();
    if ( !selected || !latest )
    {
        throw std::runtime_error( "replay load probe could not select a loaded presentation sample" );
    }
    if ( selected->frameIndex >= latest->frameIndex )
    {
        throw std::runtime_error( "replay load probe did not select an older v2 presentation sample" );
    }

    const ReplayBodyPresentationSample* selectedBody = nullptr;
    const ReplayBodyPresentationSample* latestBody = nullptr;
    float bestDistanceSquared = 0.0f;
    for ( const ReplayBodyPresentationSample& candidate : selected->bodies )
    {
        for ( const ReplayBodyPresentationSample& latestCandidate : latest->bodies )
        {
            if ( latestCandidate.id.value != candidate.id.value )
            {
                continue;
            }

            const float candidateDistanceSquared = distanceSquared( latestCandidate.position, candidate.position );
            if ( candidateDistanceSquared > bestDistanceSquared )
            {
                bestDistanceSquared = candidateDistanceSquared;
                selectedBody = &candidate;
                latestBody = &latestCandidate;
            }
            break;
        }
    }
    if ( !selectedBody || !latestBody || bestDistanceSquared < 0.0001f )
    {
        throw std::runtime_error( "replay load probe did not find a moved body in the loaded v2 artifact" );
    }

    const int probedModelIndex = selectedBody->modelIndex;
    const PhysicsBodyRecord* probedBody = TryGetReplayProbeBodyRecord( m_cGameModelCollection, probedModelIndex );
    if ( !probedBody )
    {
        throw std::runtime_error( "replay load probe loaded an invalid body index" );
    }

    const Math::Vector::Vector3 preApplyPosition = probedBody->position;
    const bool applied =
        ApplyReplayProbePresentationSampleForRender( m_cGameModelCollection, m_replayRuntime, *selected );
    if ( !applied )
    {
        throw std::runtime_error( "replay load probe failed to apply the selected loaded v2 sample" );
    }

    const PhysicsBodyRecord* appliedBody = TryGetReplayProbeBodyRecord( m_cGameModelCollection, probedModelIndex );
    if ( !appliedBody )
    {
        RestoreReplayProbeRenderInstances( m_cGameModelCollection );
        throw std::runtime_error( "replay load probe lost the selected body after applying the v2 sample" );
    }
    const Math::Vector::Vector3 liveAfterApplyPosition = appliedBody->position;
    const float livePreservedDeltaSquared = distanceSquared( liveAfterApplyPosition, preApplyPosition );
    if ( livePreservedDeltaSquared > 0.0001f )
    {
        RestoreReplayProbeRenderInstances( m_cGameModelCollection );
        throw std::runtime_error( "replay load probe mutated the live body while applying the v2 sample" );
    }

    Math::Vector::Vector3 appliedRenderPosition;
    if ( !TryPrepareReplayProbeRenderPosition( m_cGameModelCollection, probedModelIndex, appliedRenderPosition ) )
    {
        RestoreReplayProbeRenderInstances( m_cGameModelCollection );
        throw std::runtime_error( "replay load probe lost the selected render instance after applying the v2 sample" );
    }
    const float appliedDeltaSquared = distanceSquared( appliedRenderPosition, selectedBody->position );
    if ( appliedDeltaSquared > 0.0001f )
    {
        RestoreReplayProbeRenderInstances( m_cGameModelCollection );
        throw std::runtime_error(
            "replay load probe did not move the render instance to the selected loaded v2 sample" );
    }

    RestoreReplayProbeRenderInstances( m_cGameModelCollection );
    const PhysicsBodyRecord* restoredBody = TryGetReplayProbeBodyRecord( m_cGameModelCollection, probedModelIndex );
    if ( !restoredBody )
    {
        throw std::runtime_error( "replay load probe lost the selected body after restoring the v2 sample" );
    }
    const Math::Vector::Vector3 restoredPosition = restoredBody->position;
    const float restoredDeltaSquared = distanceSquared( restoredPosition, preApplyPosition );
    if ( restoredDeltaSquared > 0.0001f )
    {
        throw std::runtime_error( "replay load probe live body changed after applying the selected loaded v2 sample" );
    }

    printf( "[replay] Load probe passed: path=%s samples=%llu bodies=%llu first_frame=%llu selected_frame=%llu "
            "latest_frame=%llu body_id=%u distance_sq=%.6f\n",
            m_replayRuntime.LoadedPresentation().path,
            static_cast<unsigned long long>( m_replayRuntime.LoadedPresentation().samples.size() ),
            static_cast<unsigned long long>( m_replayRuntime.LoadedPresentation().bodyDictionaryCount ),
            static_cast<unsigned long long>( m_replayRuntime.LoadedPresentation().firstFrame ),
            static_cast<unsigned long long>( selected->frameIndex ),
            static_cast<unsigned long long>( latest->frameIndex ),
            selectedBody->id.value,
            bestDistanceSquared );
}

void Run::VerifyReplaySolverCheckpointFileProbe( const char* path )
{
    if ( !path || path[0] == '\0' )
    {
        throw std::runtime_error( "replay restore file probe requires a v2 artifact path" );
    }

    std::vector<ReplaySolverFrameSample> checkpoints;
    ReplayV2SolverCheckpointLoadResult result;
    if ( !ReplayV2Artifact::LoadSolverCheckpoints( path, checkpoints, &result ) )
    {
        throw std::runtime_error( "replay restore file probe failed to load v2 solver checkpoints" );
    }
    if ( checkpoints.empty() )
    {
        throw std::runtime_error( "replay restore file probe found no v2 solver checkpoints" );
    }

    const ReplaySolverFrameSample& checkpoint = checkpoints.front();
    if ( checkpoint.eventCursor == 0 )
    {
        throw std::runtime_error( "replay restore file probe loaded a checkpoint without an event cursor" );
    }
    char reason[160] = {};
    if ( !RestoreReplaySolverSampleAsLive( checkpoint, reason, sizeof( reason ) ) )
    {
        char message[256] = {};
        sprintf_s( message,
                   sizeof( message ),
                   "replay restore file probe failed: %s",
                   reason[0] != '\0' ? reason : "unknown restore failure" );
        throw std::runtime_error( message );
    }

    printf( "[replay] Restore file probe passed: path=%s checkpoints=%llu first_frame=%llu target_frame=%llu "
            "event_cursor=%u bodies=%llu solver_hash=0x%016llX bytes=%llu\n",
            path,
            static_cast<unsigned long long>( result.checkpointCount ),
            static_cast<unsigned long long>( result.firstFrame ),
            static_cast<unsigned long long>( checkpoint.frameIndex ),
            checkpoint.eventCursor,
            static_cast<unsigned long long>( checkpoint.bodies.size() ),
            static_cast<unsigned long long>( checkpoint.solverHash ),
            static_cast<unsigned long long>( result.fileBytes ) );
}
#endif

bool Run::RestoreReplayV2ArtifactTargetState( const char* path,
                                              ReplayFrameIndex requestedFrame,
                                              bool makeLiveBranch,
                                              RunReplayV2TargetRestoreResult& outResult,
                                              char* outReason,
                                              std::size_t reasonSize )
{
    outResult = RunReplayV2TargetRestoreResult();
    auto writeReason = [outReason, reasonSize]( const char* reason )
    { WriteReplayProbeReason( outReason, reasonSize, reason ); };
    constexpr ReplayFrameIndex LATEST_NON_CHECKPOINT_TARGET = ( std::numeric_limits<ReplayFrameIndex>::max )();
    const char* restoreSource = makeLiveBranch ? "v2_file_branch" : "v2_file_target";
#ifndef _DEBUG
    (void)restoreSource;
#endif
    const ReplayV2SolverHashSample* target = nullptr;
    const ReplaySolverFrameSample* checkpoint = nullptr;

    auto logRestoreDiagnostic = [&]( const char* failureReason,
                                     const ReplayV2SolverHashSample* diagnosticTarget,
                                     const ReplaySolverFrameSample* diagnosticCheckpoint,
                                     uint64_t restoredSolverHash,
                                     uint64_t restoredPresentationHash,
                                     std::size_t restoredBodyCount,
                                     bool hashCaptured,
                                     bool hashMatched,
                                     bool fallbackAttempted,
                                     bool fallbackRestored )
    {
#ifdef _DEBUG
        const ReplayFrameIndex targetFrame =
            diagnosticTarget ? diagnosticTarget->frameIndex
                             : ( requestedFrame == LATEST_NON_CHECKPOINT_TARGET ? 0 : requestedFrame );
        m_diagnosticsRuntime.LogReplayRestoreResult(
            SceneState(),
            restoreSource,
            targetFrame,
            diagnosticTarget ? diagnosticTarget->sceneFrame : SceneState().currentFrame,
            diagnosticCheckpoint ? diagnosticCheckpoint->frameIndex : 0,
            diagnosticTarget ? diagnosticTarget->solverHash : 0,
            diagnosticTarget ? diagnosticTarget->presentationHash : 0,
            diagnosticTarget ? diagnosticTarget->bodyCount : 0,
            restoredSolverHash,
            restoredPresentationHash,
            restoredBodyCount,
            diagnosticCheckpoint ? diagnosticCheckpoint->contactCount : 0,
            diagnosticCheckpoint ? diagnosticCheckpoint->pipelineRecordCount : 0,
            diagnosticCheckpoint ? diagnosticCheckpoint->checkpointBoundary : false,
            hashCaptured,
            hashMatched,
            fallbackAttempted,
            fallbackRestored,
            failureReason );
#else
        (void)failureReason;
        (void)diagnosticTarget;
        (void)diagnosticCheckpoint;
        (void)restoredSolverHash;
        (void)restoredPresentationHash;
        (void)restoredBodyCount;
        (void)hashCaptured;
        (void)hashMatched;
        (void)fallbackAttempted;
        (void)fallbackRestored;
#endif
    };

    auto failWithDiagnostic = [&]( const char* message,
                                   const ReplayV2SolverHashSample* diagnosticTarget,
                                   const ReplaySolverFrameSample* diagnosticCheckpoint,
                                   uint64_t restoredSolverHash = 0,
                                   uint64_t restoredPresentationHash = 0,
                                   std::size_t restoredBodyCount = 0,
                                   bool hashCaptured = false,
                                   bool hashMatched = false,
                                   bool fallbackAttempted = false,
                                   bool fallbackRestored = false ) -> bool
    {
        logRestoreDiagnostic( message,
                              diagnosticTarget,
                              diagnosticCheckpoint,
                              restoredSolverHash,
                              restoredPresentationHash,
                              restoredBodyCount,
                              hashCaptured,
                              hashMatched,
                              fallbackAttempted,
                              fallbackRestored );
        writeReason( message );
        return false;
    };

    auto applyReplayEventForRestoreTarget =
        [&]( const ReplayEventSample& event, char* eventOutReason, std::size_t eventReasonSize ) -> bool
    {
        if ( event.payloadVersion != 1 )
        {
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "unsupported replay event payload version" );
            return false;
        }

        switch ( event.kind )
        {
        case ReplayEventKind::TimelineStart:
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "ignored" );
            return true;
        case ReplayEventKind::RuntimeCommand:
        {
            const RuntimeCommandType commandType = static_cast<RuntimeCommandType>( event.value0 );
            switch ( commandType )
            {
            case RuntimeCommandType::SaveScreenshot:
            case RuntimeCommandType::SaveSceneDefaults:
            case RuntimeCommandType::SaveRenderDefaults:
            case RuntimeCommandType::SaveSkyDefaults:
            case RuntimeCommandType::Quit:
            case RuntimeCommandType::None:
                WriteReplayProbeReason( eventOutReason, eventReasonSize, "ignored non-solver runtime command" );
                return true;
            case RuntimeCommandType::ResetCurrentScene:
            case RuntimeCommandType::LoadSceneIndex:
            case RuntimeCommandType::LoadDemoScene:
            case RuntimeCommandType::CreateScene:
            case RuntimeCommandType::AdvanceScene:
            default:
                WriteReplayProbeReason( eventOutReason,
                                        eventReasonSize,
                                        "unsupported runtime timeline mutation event" );
                return false;
            }
        }
        case ReplayEventKind::BranchRestore:
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "unsupported timeline mutation event" );
            return false;
        case ReplayEventKind::WorldOverride:
            if ( event.flags & REPLAY_WORLD_OVERRIDE_GRAVITY_CHANGED )
            {
                m_cWorldEnvironment.SetGravity( ReplayEventFloatFromBits( event.value0 ) );
            }
            if ( event.flags & REPLAY_WORLD_OVERRIDE_FLUID_HEIGHT_CHANGED )
            {
                m_cWorldEnvironment.SetFluidSurfaceHeight( ReplayEventFloatFromBits( event.value1 ) );
            }
            if ( event.flags & REPLAY_WORLD_OVERRIDE_FLUID_DENSITY_CHANGED )
            {
                m_cWorldEnvironment.SetFluidDensity( ReplayEventFloatFromBits( event.value2 ) );
            }
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "applied world override" );
            return true;
        case ReplayEventKind::LauncherConfig:
            m_runtimeTools.RayCastTest().impulseStrength = ReplayEventFloatFromBits( event.value0 );
            m_runtimeTools.RayCastTest().projectileSpeed = ReplayEventFloatFromBits( event.value1 );
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "applied launcher config" );
            return true;
        case ReplayEventKind::LauncherFire:
        {
            Vector3 rayOrigin;
            Vector3 rayDirection;
            Vector3 cameraUp;
            if ( !DecodeReplayRay9Payload( event, rayOrigin, rayDirection, cameraUp ) )
            {
                WriteReplayProbeReason( eventOutReason, eventReasonSize, "invalid launcher fire payload" );
                return false;
            }
            m_runtimeTools.RayCastTest().fireMode = ( event.flags & REPLAY_LAUNCHER_FIRE_PROJECTILE ) != 0
                                                        ? RunLauncherFireMode::Projectile
                                                        : RunLauncherFireMode::Laser;
            m_runtimeTools.RayCastTest().impulseStrength = ReplayEventFloatFromBits( event.value1 );
            m_runtimeTools.RayCastTest().projectileSpeed = ReplayEventFloatFromBits( event.value2 );
            // Why: RuntimeTools now fails closed unless Run has completed the
            // cold collection-to-store topology repair at the owner boundary.
            const bool launcherStoresReady = m_cGameModelCollection.RepairPhysicsBodyAndColliderTopology();
            if ( launcherStoresReady && m_runtimeTools.FireLauncherRay( m_cGameModelCollection,
                                                                        SceneState(),
                                                                        m_systems.terrain.get(),
                                                                        m_startup.gameModelCapacity,
                                                                        rayOrigin,
                                                                        rayDirection,
                                                                        cameraUp ) )
            {
                SceneState().modelCount = m_cGameModelCollection.GetModelCount();
            }
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "applied launcher fire" );
            return true;
        }
        case ReplayEventKind::GeneratedSceneConfig:
            if ( SceneState().modelCount != event.value0 || SceneState().solverBallCount != event.value1 ||
                 SceneState().solverBoxCount != event.value2 ||
                 static_cast<int32_t>( SceneState().rngSeed ) != event.value3 )
            {
                WriteReplayProbeReason( eventOutReason,
                                        eventReasonSize,
                                        "generated scene config event does not match live state" );
                return false;
            }
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "verified generated scene config" );
            return true;
        case ReplayEventKind::EditorPlace:
        {
            Vector3 terrainPoint;
            Vector3 placementScale;
            float placementYawRadians = 0.0f;
            if ( !DecodeReplayPlacePayload( event, terrainPoint, placementScale, placementYawRadians ) )
            {
                WriteReplayProbeReason( eventOutReason, eventReasonSize, "invalid editor placement payload" );
                return false;
            }

            const int modelCountBefore = m_cGameModelCollection.GetModelCount();
            if ( event.value3 != modelCountBefore )
            {
                WriteReplayProbeReason( eventOutReason,
                                        eventReasonSize,
                                        "editor placement model count precondition mismatch" );
                return false;
            }

            const Vector3 previousPlacementScale = m_runtimeTools.Editor().placementScale;
            const bool previousTerrainAlign = m_runtimeTools.Editor().autoTerrainAlign;
            const float previousPlacementYawRadians = m_runtimeTools.Editor().placementYawRadians;
            m_runtimeTools.Editor().placementScale = placementScale;
            m_runtimeTools.Editor().autoTerrainAlign = ( event.flags & REPLAY_EDITOR_PLACE_TERRAIN_ALIGN ) != 0;
            m_runtimeTools.Editor().placementYawRadians = placementYawRadians;
            EditorObjectPlacementContext placementContext{ m_runtimeTools.Editor(),
                                                           m_cGameModelCollection,
                                                           SceneState(),
                                                           m_cWorldEnvironment,
                                                           m_systems.terrain.get(),
                                                           m_systems.assets,
                                                           m_startup.gameModelCapacity };
            EditorObjectPlacementRequest placementRequest{ event.value0,
                                                           ( event.flags & REPLAY_EDITOR_PLACE_FIXED ) != 0,
                                                           terrainPoint };
            EditorObjectPlacementResult placementResult;
            bool placed = false;
            if ( CanPlaceEditorObjectAtTerrainPoint( placementContext, placementRequest ) )
            {
                EnterInteractiveSceneRun();
                placed = PlaceEditorObjectAtTerrainPoint( placementContext, placementRequest, placementResult );
            }
            m_runtimeTools.Editor().placementScale = previousPlacementScale;
            m_runtimeTools.Editor().autoTerrainAlign = previousTerrainAlign;
            m_runtimeTools.Editor().placementYawRadians = previousPlacementYawRadians;
            if ( !placed )
            {
                WriteReplayProbeReason( eventOutReason, eventReasonSize, "failed to replay editor placement" );
                return false;
            }
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "applied editor placement" );
            return true;
        }
        case ReplayEventKind::EditorTransform:
        {
            if ( event.flags == 0 || ( event.flags & ~REPLAY_EDITOR_TRANSFORM_SUPPORTED ) != 0 )
            {
                WriteReplayProbeReason( eventOutReason, eventReasonSize, "unsupported editor transform flags" );
                return false;
            }

            Vector3 position;
            Quaternion orientation;
            float scaleFactor = 1.0f;
            bool hasScaleFactor = false;
            if ( !DecodeReplayTransformPayload( event, position, orientation, scaleFactor, hasScaleFactor ) )
            {
                WriteReplayProbeReason( eventOutReason, eventReasonSize, "invalid editor transform payload" );
                return false;
            }
            if ( ( event.flags & REPLAY_EDITOR_TRANSFORM_SCALE ) != 0 &&
                 ( !hasScaleFactor || event.value3 < 0 || event.value3 > 2 || !std::isfinite( scaleFactor ) ||
                   scaleFactor <= 0.0f ) )
            {
                WriteReplayProbeReason( eventOutReason, eventReasonSize, "invalid editor transform scale payload" );
                return false;
            }

            if ( event.value2 != m_cGameModelCollection.GetModelCount() )
            {
                WriteReplayProbeReason( eventOutReason,
                                        eventReasonSize,
                                        "editor transform model count precondition mismatch" );
                return false;
            }
            if ( event.value0 < 0 || event.value0 >= m_cGameModelCollection.GetModelCount() )
            {
                WriteReplayProbeReason( eventOutReason,
                                        eventReasonSize,
                                        "editor transform model index is out of range" );
                return false;
            }

            const PhysicsBodyStore& bodyStoreBeforeEdit = m_cGameModelCollection.GetPhysicsEngine().BodyStore();
            const PhysicsBodyHandle eventBody =
                bodyStoreBeforeEdit.HandleForReplayBodyId( static_cast<uint32_t>( event.value1 ), event.value0 );
            const PhysicsBodyRecord* eventBodyRecord = bodyStoreBeforeEdit.RecordForHandle( eventBody );
            if ( !eventBodyRecord || bodyStoreBeforeEdit.ModelIndexForHandle( eventBody ) != event.value0 ||
                 eventBodyRecord->replayBodyId != static_cast<uint32_t>( event.value1 ) )
            {
                WriteReplayProbeReason( eventOutReason, eventReasonSize, "editor transform replay body id mismatch" );
                return false;
            }

            SkullbonezCore::GameObjects::PhysicsBodyStateEdit bodyEdit;
            if ( event.flags & REPLAY_EDITOR_TRANSFORM_TRANSLATE )
            {
                bodyEdit.hasPosition = true;
                bodyEdit.position = position;
            }
            if ( event.flags & REPLAY_EDITOR_TRANSFORM_ROTATE )
            {
                bodyEdit.hasOrientation = true;
                bodyEdit.orientation = orientation;
            }
            PhysicsColliderCreateDesc editedColliderDesc;
            bool hasEditedColliderDesc = false;
            if ( event.flags & REPLAY_EDITOR_TRANSFORM_SCALE )
            {
                const ColliderRecord* colliderBeforeScale =
                    TryGetEditorTransformColliderRecord( m_cGameModelCollection,
                                                         PhysicsColliderHandle{},
                                                         event.value0,
                                                         eventBodyRecord->replayBodyId );
                if ( !colliderBeforeScale )
                {
                    WriteReplayProbeReason( eventOutReason, eventReasonSize, "editor transform collider row missing" );
                    return false;
                }
                const CollisionShape baseShape = colliderBeforeScale->shape;
                CollisionShape scaledShape;
                if ( !ScaleShapeAxisFromBase( baseShape, event.value3, scaleFactor, scaledShape ) )
                {
                    WriteReplayProbeReason( eventOutReason,
                                            eventReasonSize,
                                            "failed to replay editor transform scale" );
                    return false;
                }
                // Invariant: restore reuses the previous collider material and
                // replaces only the decoded scale shape, keeping replay payload
                // semantics independent from legacy model-side recapture.
                editedColliderDesc = MakeColliderCreateDesc( std::move( scaledShape ),
                                                             colliderBeforeScale->restitution,
                                                             colliderBeforeScale->contactMaterialId );
                hasEditedColliderDesc = true;
            }
            bodyEdit.hasLinearVelocity = true;
            bodyEdit.linearVelocity = Vector3( 0.0f, 0.0f, 0.0f );
            bodyEdit.hasAngularVelocity = true;
            bodyEdit.angularVelocity = Vector3( 0.0f, 0.0f, 0.0f );
            if ( hasEditedColliderDesc )
            {
                m_cGameModelCollection.ApplyPhysicsBodyColliderEdit( event.value0,
                                                                     bodyEdit,
                                                                     std::move( editedColliderDesc ) );
            }
            else
            {
                m_cGameModelCollection.ApplyPhysicsBodyEdit( event.value0, bodyEdit );
            }
            // Why: the edited-state commit has already refreshed the
            // edited body row. The wake decision should read the committed
            // PhysicsBodyStore record, not presentation/authored pose data.
            PhysicsEngine& physics = m_cGameModelCollection.GetPhysicsEngine();
            const PhysicsBodyStore& bodyStore = physics.BodyStore();
            const PhysicsBodyHandle body =
                bodyStore.HandleForReplayBodyId( static_cast<uint32_t>( event.value1 ), event.value0 );
            const PhysicsBodyRecord* bodyRecord = bodyStore.RecordForHandle( body );
            if ( bodyRecord && !bodyRecord->isFixed )
            {
                physics.WakeBody( body );
            }
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "applied editor transform" );
            return true;
        }
        default:
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "unsupported replay event kind" );
            return false;
        }
    };

    if ( !path || path[0] == '\0' )
    {
        return failWithDiagnostic( "replay v2 target restore requires a v2 artifact path", target, checkpoint );
    }

    std::vector<ReplaySolverFrameSample> checkpoints;
    ReplayV2SolverCheckpointLoadResult checkpointResult;
    if ( !ReplayV2Artifact::LoadSolverCheckpoints( path, checkpoints, &checkpointResult ) )
    {
        return failWithDiagnostic( "failed to load v2 solver checkpoints", target, checkpoint );
    }

    std::vector<ReplayV2SolverHashSample> hashes;
    ReplayV2SolverHashLoadResult hashResult;
    if ( !ReplayV2Artifact::LoadSolverHashes( path, hashes, &hashResult ) )
    {
        return failWithDiagnostic( "failed to load v2 solver hashes", target, checkpoint );
    }

    std::vector<ReplayEventSample> events;
    ReplayV2EventLoadResult eventResult;
    if ( !ReplayV2Artifact::LoadEvents( path, events, &eventResult ) )
    {
        return failWithDiagnostic( "failed to load v2 events", target, checkpoint );
    }

    std::vector<ReplayPresentationSample> presentationSamples;
    ReplayV2LoadResult presentationResult;
    if ( !ReplayV2Artifact::LoadPresentation( path, presentationSamples, &presentationResult ) )
    {
        return failWithDiagnostic( "failed to load v2 presentation frames", target, checkpoint );
    }

    if ( requestedFrame == LATEST_NON_CHECKPOINT_TARGET )
    {
        for ( auto it = hashes.rbegin(); it != hashes.rend(); ++it )
        {
            if ( !it->checkpointBoundary )
            {
                target = &*it;
                break;
            }
        }
        if ( !target )
        {
            return failWithDiagnostic( "found no saved non-checkpoint target hash", target, checkpoint );
        }
    }
    else
    {
        for ( const ReplayV2SolverHashSample& hash : hashes )
        {
            if ( hash.frameIndex == requestedFrame )
            {
                target = &hash;
                break;
            }
        }
        if ( !target )
        {
            char message[192] = {};
            sprintf_s( message,
                       sizeof( message ),
                       "found no saved hash for requested target frame %llu",
                       static_cast<unsigned long long>( requestedFrame ) );
            return failWithDiagnostic( message, target, checkpoint );
        }
    }

    for ( const ReplaySolverFrameSample& candidate : checkpoints )
    {
        if ( candidate.frameIndex <= target->frameIndex &&
             ( !checkpoint || candidate.frameIndex > checkpoint->frameIndex ) )
        {
            checkpoint = &candidate;
        }
    }
    if ( !checkpoint )
    {
        return failWithDiagnostic( "found no checkpoint before target hash", target, checkpoint );
    }
    if ( checkpoint->frameIndex > target->frameIndex )
    {
        return failWithDiagnostic( "selected checkpoint after target frame", target, checkpoint );
    }
    if ( checkpoint->eventCursor == 0 )
    {
        return failWithDiagnostic( "loaded a checkpoint without an event cursor", target, checkpoint );
    }
    if ( target->frameIndex - checkpoint->frameIndex >
         static_cast<ReplayFrameIndex>( hashes.size() + events.size() + 1u ) )
    {
        return failWithDiagnostic( "selected an implausibly distant target frame", target, checkpoint );
    }

    ReplaySolverFrameSample liveBackup;
    bool hasLiveBackup = false;
    if ( const ReplaySolverFrameSample* latest = m_replayRuntime.Solver().LatestSample() )
    {
        liveBackup = *latest;
        hasLiveBackup = true;
    }
    bool stateMutated = false;

    auto checkpointTopologyMatchesLive = [&]() -> bool
    {
        const int liveModelCount = m_cGameModelCollection.GetModelCount();
        if ( checkpoint->bodies.size() > static_cast<std::size_t>( liveModelCount ) )
        {
            return false;
        }
        for ( const ReplaySolverBodySample& body : checkpoint->bodies )
        {
            if ( body.modelIndex < 0 || body.modelIndex >= liveModelCount )
            {
                return false;
            }
            const PhysicsBodyRecord* bodyRecord =
                TryGetReplayProbeBodyRecord( m_cGameModelCollection, body.modelIndex );
            if ( !bodyRecord || bodyRecord->replayBodyId != body.id.value )
            {
                return false;
            }
        }
        return true;
    };

    auto latestGeneratedSceneConfigBeforeCheckpoint = [&]() -> const ReplayEventSample*
    {
        const ReplayEventSample* generatedConfig = nullptr;
        for ( const ReplayEventSample& event : events )
        {
            if ( event.kind != ReplayEventKind::GeneratedSceneConfig || event.frameIndex > checkpoint->frameIndex ||
                 event.sequence >= checkpoint->eventCursor )
            {
                continue;
            }
            if ( event.branch.branchId != checkpoint->branch.branchId )
            {
                continue;
            }
            generatedConfig = &event;
        }
        return generatedConfig;
    };

    auto rebuildGeneratedSceneTopology =
        [&]( const ReplayEventSample& event, char* rebuildReason, std::size_t rebuildReasonSize ) -> bool
    {
        if ( event.value0 < 0 || event.value1 < 0 || event.value2 < 0 || event.value3 <= 0 )
        {
            WriteReplayProbeReason( rebuildReason,
                                    rebuildReasonSize,
                                    "generated scene config contains invalid counts" );
            return false;
        }

        const uint32_t overrideBits =
            ( event.flags & REPLAY_GENERATED_SCENE_OVERRIDE_MASK ) >> REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT;
        if ( overrideBits > static_cast<uint32_t>( GeneratedObjectTypeOverride::AllBoxes ) )
        {
            WriteReplayProbeReason( rebuildReason,
                                    rebuildReasonSize,
                                    "generated scene config has invalid override bits" );
            return false;
        }

        const bool exactSolverCounts = ( event.flags & REPLAY_GENERATED_SCENE_EXACT_SOLVER_COUNTS ) != 0;
        const bool uiSolverCounts = ( event.flags & REPLAY_GENERATED_SCENE_UI_SOLVER_COUNTS ) != 0;
        const bool uiModelCount = ( event.flags & REPLAY_GENERATED_SCENE_UI_MODEL_COUNT ) != 0;
        if ( exactSolverCounts && event.value1 + event.value2 != event.value0 )
        {
            WriteReplayProbeReason( rebuildReason,
                                    rebuildReasonSize,
                                    "generated solver counts do not match model count" );
            return false;
        }
        if ( event.value0 > m_startup.gameModelCapacity )
        {
            WriteReplayProbeReason( rebuildReason,
                                    rebuildReasonSize,
                                    "generated scene model count exceeds active capacity" );
            return false;
        }

        m_cGameModelCollection.Clear();
        m_runtimeTools.ClearRayCastTestLines();
        m_simulation.Reset();
        SceneState().rngSeed = static_cast<unsigned int>( event.value3 );
        SceneState().rngState = static_cast<unsigned int>( event.value3 );
        m_launchOptions.generatedObjectTypeOverride = static_cast<GeneratedObjectTypeOverride>( overrideBits );
        m_sceneController.UIOverrides().modelCountOverride = uiModelCount ? event.value0 : -1;
        m_sceneController.UIOverrides().solverBallCountOverride =
            uiSolverCounts || exactSolverCounts ? event.value1 : -1;
        m_sceneController.UIOverrides().solverBoxCountOverride =
            uiSolverCounts || exactSolverCounts ? event.value2 : -1;

        if ( exactSolverCounts || uiSolverCounts )
        {
            SceneGeneratedSetup::SetUpSolverObjects(
                BuildSceneGeneratedModelContext( SceneState(),
                                                 m_config,
                                                 m_cWorldEnvironment,
                                                 m_systems.terrain.get(),
                                                 m_cGameModelCollection,
                                                 m_cGameModelCollection.GetPhysicsEngine(),
                                                 m_launchOptions.generatedObjectTypeOverride ),
                event.value1,
                event.value2 );
        }
        else
        {
            SceneGeneratedSetup::SetUpGameModels(
                BuildSceneGeneratedModelContext( SceneState(),
                                                 m_config,
                                                 m_cWorldEnvironment,
                                                 m_systems.terrain.get(),
                                                 m_cGameModelCollection,
                                                 m_cGameModelCollection.GetPhysicsEngine(),
                                                 m_launchOptions.generatedObjectTypeOverride ),
                event.value0 );
        }
        if ( !checkpointTopologyMatchesLive() )
        {
            WriteReplayProbeReason( rebuildReason,
                                    rebuildReasonSize,
                                    "rebuilt generated topology still mismatches checkpoint" );
            return false;
        }
        WriteReplayProbeReason( rebuildReason, rebuildReasonSize, "rebuilt generated topology" );
        return true;
    };

    auto failAfterMutation = [&]( const char* message,
                                  const ReplayV2SolverHashSample* diagnosticTarget,
                                  uint64_t restoredSolverHash = 0,
                                  uint64_t restoredPresentationHash = 0,
                                  std::size_t restoredBodyCount = 0,
                                  bool hashCaptured = false,
                                  bool hashMatched = false ) -> bool
    {
        bool fallbackRestored = false;
        if ( stateMutated && hasLiveBackup )
        {
            char fallbackReason[128] = {};
            fallbackRestored = ApplyReplaySolverSampleState( liveBackup, fallbackReason, sizeof( fallbackReason ) );
        }
        return failWithDiagnostic( message,
                                   diagnosticTarget,
                                   checkpoint,
                                   restoredSolverHash,
                                   restoredPresentationHash,
                                   restoredBodyCount,
                                   hashCaptured,
                                   hashMatched,
                                   stateMutated && hasLiveBackup,
                                   fallbackRestored );
    };

    bool generatedTopologyRebuilt = false;
    if ( !checkpointTopologyMatchesLive() )
    {
        const ReplayEventSample* generatedConfig = latestGeneratedSceneConfigBeforeCheckpoint();
        if ( !generatedConfig )
        {
            return failAfterMutation( "checkpoint topology does not match live scene and no generated config was saved",
                                      target );
        }

        char rebuildReason[160] = {};
        stateMutated = true;
        if ( !rebuildGeneratedSceneTopology( *generatedConfig, rebuildReason, sizeof( rebuildReason ) ) )
        {
            char message[320] = {};
            sprintf_s( message,
                       sizeof( message ),
                       "failed to rebuild generated scene topology: %s",
                       rebuildReason[0] != '\0' ? rebuildReason : "unknown rebuild failure" );
            return failAfterMutation( message, target );
        }
        generatedTopologyRebuilt = true;
    }

    char reason[192] = {};
    if ( !ApplyReplaySolverSampleState( *checkpoint, reason, sizeof( reason ) ) )
    {
        char message[288] = {};
        sprintf_s( message,
                   sizeof( message ),
                   "failed to apply checkpoint: %s",
                   reason[0] != '\0' ? reason : "unknown restore failure" );
        return failWithDiagnostic( message, target, checkpoint );
    }
    stateMutated = true;

    ReplayFrameIndex currentFrame = checkpoint->frameIndex;
    int currentSceneFrame = checkpoint->sceneFrame;
    uint32_t eventCursor = checkpoint->eventCursor;
    std::size_t eventsApplied = 0;
    std::size_t unsupportedEvents = 0;
    SceneState().currentFrame = currentSceneFrame;

    {
        ScopedReplayProbeProfilerFrame profilerFrame;
        while ( currentFrame < target->frameIndex )
        {
            const ReplayFrameIndex nextFrame = currentFrame + 1u;

            for ( const ReplayEventSample& event : events )
            {
                if ( event.frameIndex != nextFrame || event.sequence < eventCursor )
                {
                    continue;
                }
                if ( event.branch.branchId != checkpoint->branch.branchId )
                {
                    ++unsupportedEvents;
                    continue;
                }

                char eventReason[160] = {};
                if ( !applyReplayEventForRestoreTarget( event, eventReason, sizeof( eventReason ) ) )
                {
                    char message[320] = {};
                    sprintf_s( message,
                               sizeof( message ),
                               "replay restore target probe failed to apply event sequence %u at frame %llu: %s",
                               event.sequence,
                               static_cast<unsigned long long>( event.frameIndex ),
                               eventReason[0] != '\0' ? eventReason : "unknown event replay failure" );
                    return failAfterMutation( message, target );
                }
                eventCursor = (std::max)( eventCursor, event.sequence + 1u );
                ++eventsApplied;
            }

            m_runtimeTools.TickRayCastTestLines( PHYSICS_FIXED_DT );
            m_runtimeTools.Laser().Update( PHYSICS_FIXED_DT );
            m_cGameModelCollection.EndCollisionVisualFrame();
            ++currentSceneFrame;
            SceneState().currentFrame = currentSceneFrame;
            m_cGameModelCollection.BeginCollisionVisualFrame();

            const auto physicsWorldForces = m_cWorldEnvironment.GetPhysicsWorldForces();
            StepRuntimePhysicsTick( m_cGameModelCollection,
                                    PHYSICS_FIXED_DT,
                                    *m_systems.config,
                                    physicsWorldForces,
                                    *m_systems.workerPool );
            currentFrame = nextFrame;

            const ReplayV2SolverHashSample* expectedHash = FindReplaySolverHashForFrame( hashes, currentFrame );
            if ( !expectedHash )
            {
                return failAfterMutation( "could not find stepped hash metadata", target );
            }

            ReplaySolverFrameSample stepReference;
            stepReference.frameIndex = expectedHash->frameIndex;
            stepReference.branch = checkpoint->branch;
            stepReference.eventCursor = eventCursor;
            stepReference.sceneFrame = expectedHash->sceneFrame;
            stepReference.simulationSeconds = expectedHash->simulationSeconds;
            stepReference.physicsDt = PHYSICS_FIXED_DT;

            uint64_t stepSolverHash = 0;
            uint64_t stepPresentationHash = 0;
            std::size_t stepBodyCount = 0;
            if ( !CaptureCurrentReplaySolverHash( stepReference, stepSolverHash, stepPresentationHash, stepBodyCount ) )
            {
                return failAfterMutation( "failed to capture stepped hash", expectedHash );
            }
            if ( stepBodyCount != expectedHash->bodyCount || stepSolverHash != expectedHash->solverHash )
            {
                char message[1024] = {};
                const ReplayPresentationSample* expectedPresentation =
                    FindReplayPresentationForFrame( presentationSamples, currentFrame );
                const PhysicsBodyRecord* restoredBody = TryGetReplayProbeBodyRecord( m_cGameModelCollection, 0 );
                if ( expectedPresentation && !expectedPresentation->bodies.empty() && restoredBody )
                {
                    const ReplayBodyPresentationSample& expectedBody = expectedPresentation->bodies[0];
                    const Vector3& restoredPosition = restoredBody->position;
                    const Vector3& restoredVelocity = restoredBody->linearVelocity;
                    float restoredQx = 0.0f;
                    float restoredQy = 0.0f;
                    float restoredQz = 0.0f;
                    float restoredQw = 1.0f;
                    restoredBody->orientation.GetComponents( restoredQx, restoredQy, restoredQz, restoredQw );

                    sprintf_s( message,
                               sizeof( message ),
                               "replay restore target probe diverged at frame %llu: restored=0x%016llX "
                               "expected=0x%016llX restored_presentation=0x%016llX expected_presentation=0x%016llX "
                               "restored_pos=(%.6f,%.6f,%.6f) expected_pos=(%.6f,%.6f,%.6f) "
                               "restored_vel=(%.6f,%.6f,%.6f) restored_q=(%.6f,%.6f,%.6f,%.6f) "
                               "expected_q=(%.6f,%.6f,%.6f,%.6f) restored_body_id=%u expected_body_id=%u "
                               "events_applied=%llu",
                               static_cast<unsigned long long>( currentFrame ),
                               static_cast<unsigned long long>( stepSolverHash ),
                               static_cast<unsigned long long>( expectedHash->solverHash ),
                               static_cast<unsigned long long>( stepPresentationHash ),
                               static_cast<unsigned long long>( expectedHash->presentationHash ),
                               restoredPosition.x,
                               restoredPosition.y,
                               restoredPosition.z,
                               expectedBody.position.x,
                               expectedBody.position.y,
                               expectedBody.position.z,
                               restoredVelocity.x,
                               restoredVelocity.y,
                               restoredVelocity.z,
                               restoredQx,
                               restoredQy,
                               restoredQz,
                               restoredQw,
                               expectedBody.orientation[0],
                               expectedBody.orientation[1],
                               expectedBody.orientation[2],
                               expectedBody.orientation[3],
                               restoredBody->replayBodyId,
                               expectedBody.id.value,
                               static_cast<unsigned long long>( eventsApplied ) );
                }
                else
                {
                    sprintf_s( message,
                               sizeof( message ),
                               "replay restore target probe diverged at frame %llu: restored=0x%016llX "
                               "expected=0x%016llX restored_presentation=0x%016llX expected_presentation=0x%016llX "
                               "restored_bodies=%llu expected_bodies=%u events_applied=%llu",
                               static_cast<unsigned long long>( currentFrame ),
                               static_cast<unsigned long long>( stepSolverHash ),
                               static_cast<unsigned long long>( expectedHash->solverHash ),
                               static_cast<unsigned long long>( stepPresentationHash ),
                               static_cast<unsigned long long>( expectedHash->presentationHash ),
                               static_cast<unsigned long long>( stepBodyCount ),
                               expectedHash->bodyCount,
                               static_cast<unsigned long long>( eventsApplied ) );
                }
                return failAfterMutation( message,
                                          expectedHash,
                                          stepSolverHash,
                                          stepPresentationHash,
                                          stepBodyCount,
                                          true );
            }
        }
    }

    if ( unsupportedEvents != 0 )
    {
        return failAfterMutation( "encountered unsupported branch events before target", target );
    }

    ReplaySolverFrameSample reference;
    reference.frameIndex = target->frameIndex;
    reference.branch = checkpoint->branch;
    reference.eventCursor = eventCursor;
    reference.sceneFrame = target->sceneFrame;
    reference.simulationSeconds = target->simulationSeconds;
    reference.physicsDt = PHYSICS_FIXED_DT;

    uint64_t restoredSolverHash = 0;
    uint64_t restoredPresentationHash = 0;
    std::size_t restoredBodyCount = 0;
    if ( !CaptureCurrentReplaySolverHash( reference, restoredSolverHash, restoredPresentationHash, restoredBodyCount ) )
    {
        return failAfterMutation( "failed to capture target hash", target );
    }
    if ( restoredBodyCount != target->bodyCount )
    {
        char message[256] = {};
        sprintf_s( message,
                   sizeof( message ),
                   "replay restore target probe body count mismatch: restored=%llu expected=%u",
                   static_cast<unsigned long long>( restoredBodyCount ),
                   target->bodyCount );
        return failAfterMutation( message,
                                  target,
                                  restoredSolverHash,
                                  restoredPresentationHash,
                                  restoredBodyCount,
                                  true );
    }
    if ( restoredSolverHash != target->solverHash )
    {
        char message[320] = {};
        sprintf_s( message,
                   sizeof( message ),
                   "replay restore target probe solver hash mismatch: restored=0x%016llX expected=0x%016llX",
                   static_cast<unsigned long long>( restoredSolverHash ),
                   static_cast<unsigned long long>( target->solverHash ) );
        return failAfterMutation( message,
                                  target,
                                  restoredSolverHash,
                                  restoredPresentationHash,
                                  restoredBodyCount,
                                  true );
    }

    outResult.checkpointCount = checkpointResult.checkpointCount;
    outResult.eventCount = eventResult.eventCount;
    outResult.hashCount = hashResult.hashCount;
    outResult.eventsApplied = eventsApplied;
    outResult.bodyCount = restoredBodyCount;
    outResult.fileBytes = hashResult.fileBytes;
    outResult.checkpointFrame = checkpoint->frameIndex;
    outResult.targetFrame = target->frameIndex;
    outResult.eventCursor = eventCursor;
    outResult.solverHash = restoredSolverHash;
    outResult.presentationHash = restoredPresentationHash;
    outResult.generatedTopologyRebuilt = generatedTopologyRebuilt;

    logRestoreDiagnostic( "",
                          target,
                          checkpoint,
                          restoredSolverHash,
                          restoredPresentationHash,
                          restoredBodyCount,
                          true,
                          true,
                          false,
                          false );

    if ( makeLiveBranch )
    {
        const uint32_t parentBranchId =
            checkpoint->branch.branchId != 0
                ? checkpoint->branch.branchId
                : ( m_replayRuntime.Branch().branchId != 0 ? m_replayRuntime.Branch().branchId : 1u );
        ReplayBranchInfo restoredBranch;
        restoredBranch.branchId = (std::max)( m_replayRuntime.Branch().branchId, parentBranchId ) + 1u;
        restoredBranch.parentBranchId = parentBranchId;
        restoredBranch.startFrame = 0;
        restoredBranch.sourceFrame = target->frameIndex;
        restoredBranch.sourceSolverHash = target->solverHash;
        m_replayRuntime.Branch() = restoredBranch;
        ResetReplayTimelineForActiveScene( true );
        m_replayRuntime.RecordEvent( ReplayEventKind::BranchRestore,
                                     0,
                                     0,
                                     static_cast<int32_t>( parentBranchId ),
                                     target->sceneFrame,
                                     0,
                                     0,
                                     target->solverHash,
                                     "hash-verified v2 file restore" );
        outResult.branchId = restoredBranch.branchId;
        outResult.parentBranchId = parentBranchId;
        outResult.madeLiveBranch = true;
    }

    writeReason( "restored hash match" );
    return true;
}

#ifdef _DEBUG
void Run::VerifyReplaySolverTargetFileProbe( const char* path )
{
    RunReplayV2TargetRestoreResult result;
    char reason[256] = {};
    if ( !RestoreReplayV2ArtifactTargetState( path,
                                              ( std::numeric_limits<ReplayFrameIndex>::max )(),
                                              false,
                                              result,
                                              reason,
                                              sizeof( reason ) ) )
    {
        char message[384] = {};
        sprintf_s( message,
                   sizeof( message ),
                   "replay restore target probe failed: %s",
                   reason[0] != '\0' ? reason : "unknown restore failure" );
        throw std::runtime_error( message );
    }

    printf( "[replay] Restore target probe passed: path=%s checkpoints=%llu events=%llu hashes=%llu "
            "checkpoint_frame=%llu target_frame=%llu event_cursor=%u events_applied=%llu bodies=%llu "
            "generated_topology_rebuilt=%d "
            "solver_hash=0x%016llX presentation_hash=0x%016llX bytes=%llu\n",
            path,
            static_cast<unsigned long long>( result.checkpointCount ),
            static_cast<unsigned long long>( result.eventCount ),
            static_cast<unsigned long long>( result.hashCount ),
            static_cast<unsigned long long>( result.checkpointFrame ),
            static_cast<unsigned long long>( result.targetFrame ),
            result.eventCursor,
            static_cast<unsigned long long>( result.eventsApplied ),
            static_cast<unsigned long long>( result.bodyCount ),
            result.generatedTopologyRebuilt ? 1 : 0,
            static_cast<unsigned long long>( result.solverHash ),
            static_cast<unsigned long long>( result.presentationHash ),
            static_cast<unsigned long long>( result.fileBytes ) );
    PostQuitMessage( 0 );
}

void Run::VerifyReplaySolverFailureFileProbe( const char* path )
{
    constexpr ReplayFrameIndex MISSING_TARGET_FRAME = 999999999u;
    RunReplayV2TargetRestoreResult result;
    char reason[256] = {};
    if ( RestoreReplayV2ArtifactTargetState( path, MISSING_TARGET_FRAME, false, result, reason, sizeof( reason ) ) )
    {
        throw std::runtime_error( "replay restore failure probe unexpectedly restored a missing target frame" );
    }
    if ( strstr( reason, "found no saved hash for requested target frame" ) == nullptr )
    {
        char message[384] = {};
        sprintf_s( message,
                   sizeof( message ),
                   "replay restore failure probe produced an unexpected reason: %s",
                   reason[0] != '\0' ? reason : "unknown restore failure" );
        throw std::runtime_error( message );
    }

    printf( "[replay] Restore failure probe passed: path=%s missing_frame=%llu reason=\"%s\"\n",
            path,
            static_cast<unsigned long long>( MISSING_TARGET_FRAME ),
            reason );
}

void Run::VerifyReplaySolverBranchFileProbe( const char* path )
{
    if ( !LoadReplayPresentationArtifact( path, true ) )
    {
        throw std::runtime_error( "replay restore branch probe failed to load v2 presentation scrub source" );
    }
    m_replayRuntime.Scrubber().historicalSamplePaused = true;
    m_replayRuntime.Scrubber().activeTrack = RunReplayTrack::Presentation;
    m_replayRuntime.SetTrackPosition( RunReplayTrack::Presentation, 1.0f );

    RunReplayV2TargetRestoreResult result;
    char reason[256] = {};
    if ( !RestoreReplayScrubberSelectionAsLive( m_timers.simulationTimer.GetTotalTime(),
                                                &result,
                                                reason,
                                                sizeof( reason ) ) )
    {
        char message[384] = {};
        sprintf_s( message,
                   sizeof( message ),
                   "replay restore branch probe failed: %s",
                   reason[0] != '\0' ? reason : "unknown restore failure" );
        throw std::runtime_error( message );
    }
    if ( !result.madeLiveBranch || result.branchId == 0 )
    {
        throw std::runtime_error( "replay restore branch probe did not create a scrubber live branch" );
    }

    printf( "[replay] Restore branch probe passed: path=%s checkpoints=%llu events=%llu hashes=%llu "
            "checkpoint_frame=%llu target_frame=%llu event_cursor=%u events_applied=%llu bodies=%llu "
            "generated_topology_rebuilt=%d "
            "branch_id=%u parent_branch_id=%u solver_hash=0x%016llX presentation_hash=0x%016llX bytes=%llu\n",
            path,
            static_cast<unsigned long long>( result.checkpointCount ),
            static_cast<unsigned long long>( result.eventCount ),
            static_cast<unsigned long long>( result.hashCount ),
            static_cast<unsigned long long>( result.checkpointFrame ),
            static_cast<unsigned long long>( result.targetFrame ),
            result.eventCursor,
            static_cast<unsigned long long>( result.eventsApplied ),
            static_cast<unsigned long long>( result.bodyCount ),
            result.generatedTopologyRebuilt ? 1 : 0,
            result.branchId,
            result.parentBranchId,
            static_cast<unsigned long long>( result.solverHash ),
            static_cast<unsigned long long>( result.presentationHash ),
            static_cast<unsigned long long>( result.fileBytes ) );
}
#endif


void Run::EnterInteractiveSceneRun()
{
    SceneState().isInteractiveRun = true;
    SceneState().isExitOnComplete = false;
    m_diagnosticsRuntime.Capture().Screenshot().isScreenshotAndExit = false;
}


bool Run::CanSceneAutomationQuit() const
{
    return !SceneState().isInteractiveRun;
}


void Run::HoldCompletedInteractiveScene()
{
    SceneState().isTestComplete = true;
    SceneState().isExitOnComplete = false;
    m_diagnosticsRuntime.Capture().Screenshot().isScreenshotAndExit = false;
    m_camera.autoCycleInterval = -1.0f;
    m_camera.autoCycleAccum = 0.0f;
}


bool Run::TickScreenshots()
{
    PROFILE_BEGIN( "Frame/PostDraw/Screenshots" );
    if ( m_debug.isCrossScenePauseLocked && !Input::IsKeyDown( VK_SPACE ) )
    {
        PROFILE_END( "Frame/PostDraw/Screenshots" );
        return false;
    }

    auto executeSceneControlAction = [&]( const SceneRuntimeControlAction& action ) -> bool
    {
        if ( action.enterInteractiveSceneRun )
        {
            EnterInteractiveSceneRun();
        }

        switch ( action.type )
        {
        case SceneRuntimeControlActionType::ClearCurrentSceneAutomation:
            SceneState().isExitOnComplete = false;
            m_diagnosticsRuntime.Capture().Screenshot().isScreenshotAndExit = false;
            return true;
        case SceneRuntimeControlActionType::LoadScene:
            LoadScene( action.index,
                       action.preserveUIState,
                       action.suppressExitOnComplete,
                       action.preserveRuntimeState );
            return true;
        case SceneRuntimeControlActionType::ApplyCinematicModeFromBrowserIndex:
            EnterInteractiveSceneRun();
            return ApplyCinematicModeFromBrowserIndex(
                SceneRuntimeStyleContext{ m_launchOptions,
                                          SceneState(),
                                          m_sceneController.Browser(),
                                          m_cGameModelCollection,
                                          m_systems.assets,
                                          RuntimeActiveCinematicConfig( SceneState(), m_config ),
                                          m_defaultCinematicRender },
                action.index );
        case SceneRuntimeControlActionType::None:
            return false;
        }
        return false;
    };

    const RuntimeCaptureSink sink{ this,
                                   []( void* context, const char* path )
                                   { static_cast<Run*>( context )->SaveScreenshot( path ); } };
    const std::string* scenePath = m_sceneController.CurrentPath();
    const RuntimeCaptureResult result = m_diagnosticsRuntime.Capture().TickScreenshots(
        RuntimeCaptureSceneContext{ SceneState().isSceneMode,
                                    SceneState().isInteractiveRun,
                                    SceneState().currentFrame,
                                    m_timers.simulationTimer.GetTimeSinceLastStart() * 1000.0,
                                    scenePath ? scenePath->c_str() : nullptr },
        sink );

    PROFILE_END( "Frame/PostDraw/Screenshots" );

    if ( result.restartFrame )
    {
        PROFILE_FRAME_END();
    }

#ifdef _DEBUG
    if ( result.completion == RuntimeCaptureCompletion::ScreenshotAndExit )
    {
        LogSceneFinished( "screenshot_and_exit" );
    }
    else if ( result.completion == RuntimeCaptureCompletion::Screenshot )
    {
        LogSceneFinished( "screenshot" );
    }
#endif

    switch ( result.automation )
    {
    case RuntimeCaptureAutomation::Quit:
        if ( result.completion == RuntimeCaptureCompletion::ScreenshotAndExit )
        {
            PrintRuntimeExitReason( "Exiting because screenshot-and-exit capture completed." );
        }
        else if ( result.completion == RuntimeCaptureCompletion::AutoCycle )
        {
            PrintRuntimeExitReason( "Exiting because auto-cycle screenshot capture completed." );
        }
        PostQuitMessage( 0 );
        break;
    case RuntimeCaptureAutomation::AdvanceSceneOrQuit:
    {
        const SceneRuntimeControlAction action = m_sceneCoordinator.AdvanceScene( m_diagnosticsRuntime.PerfTestActive(),
                                                                                  sPerfPass,
                                                                                  SceneState().isInteractiveRun );
        if ( !executeSceneControlAction( action ) )
        {
            if ( result.completion == RuntimeCaptureCompletion::Screenshot )
            {
                PrintRuntimeExitReason(
                    "Exiting because scene screenshot capture completed and no next scene is queued." );
            }
            PostQuitMessage( 0 );
        }
        break;
    }
    case RuntimeCaptureAutomation::HoldInteractive:
        HoldCompletedInteractiveScene();
        break;
    case RuntimeCaptureAutomation::None:
        break;
    }

    return result.restartFrame;
}


void Run::TickAutoCycle()
{
    if ( m_debug.isCrossScenePauseLocked && !Input::IsKeyDown( VK_SPACE ) )
    {
        return;
    }

    const RuntimeCaptureSink sink{ this,
                                   []( void* context, const char* path )
                                   { static_cast<Run*>( context )->SaveScreenshot( path ); } };
    const RuntimeCaptureResult result =
        m_diagnosticsRuntime.Capture().TickAutoCycle( SceneState().isSceneMode,
                                                      SceneState().isInteractiveRun,
                                                      m_cGameModelCollection.GetModelCount(),
                                                      m_camera.autoCycleInterval,
                                                      m_camera.autoCycleAccum,
                                                      m_camera.autoCycleShotsTaken,
                                                      m_camera.trackBallIndex,
                                                      sink );

    if ( result.completion != RuntimeCaptureCompletion::AutoCycle )
    {
        return;
    }

#ifdef _DEBUG
    LogSceneFinished( "auto_cycle" );
#endif

    if ( result.automation == RuntimeCaptureAutomation::Quit )
    {
        PostQuitMessage( 0 );
    }
    else if ( result.automation == RuntimeCaptureAutomation::HoldInteractive )
    {
        HoldCompletedInteractiveScene();
    }
}


bool Run::TickSceneAdvance()
{
    auto executeSceneControlAction = [&]( const SceneRuntimeControlAction& action ) -> bool
    {
        if ( action.enterInteractiveSceneRun )
        {
            EnterInteractiveSceneRun();
        }

        switch ( action.type )
        {
        case SceneRuntimeControlActionType::ClearCurrentSceneAutomation:
            SceneState().isExitOnComplete = false;
            m_diagnosticsRuntime.Capture().Screenshot().isScreenshotAndExit = false;
            return true;
        case SceneRuntimeControlActionType::LoadScene:
            LoadScene( action.index,
                       action.preserveUIState,
                       action.suppressExitOnComplete,
                       action.preserveRuntimeState );
            return true;
        case SceneRuntimeControlActionType::ApplyCinematicModeFromBrowserIndex:
            EnterInteractiveSceneRun();
            return ApplyCinematicModeFromBrowserIndex(
                SceneRuntimeStyleContext{ m_launchOptions,
                                          SceneState(),
                                          m_sceneController.Browser(),
                                          m_cGameModelCollection,
                                          m_systems.assets,
                                          RuntimeActiveCinematicConfig( SceneState(), m_config ),
                                          m_defaultCinematicRender },
                action.index );
        case SceneRuntimeControlActionType::None:
            return false;
        }
        return false;
    };

    const bool sceneProceedAllowed = !m_debug.isCrossScenePauseLocked || Input::IsKeyDown( VK_SPACE );
    if ( !sceneProceedAllowed )
    {
        return false;
    }

    ++SceneState().currentFrame;

    const bool hasRequiredContactGate = !m_requiredSceneContacts.empty();
    const bool hasRequiredBroadphaseGate = !m_requiredBroadphaseXCells.empty();
    const bool hasRequiredSceneGate = hasRequiredContactGate || hasRequiredBroadphaseGate;
    const bool requiredContactsComplete = RequiredSceneContactsComplete();
    const bool requiredBroadphaseComplete = RequiredSceneBroadphaseXCellsComplete();
    const bool requiredSceneComplete = requiredContactsComplete && requiredBroadphaseComplete;
    if ( hasRequiredSceneGate && requiredSceneComplete && !SceneState().isTestComplete )
    {
#ifdef _DEBUG
        LogSceneFinished( "required_scene_gates" );
#endif
        if ( SceneState().isExitOnComplete && CanSceneAutomationQuit() )
        {
            if ( !executeSceneControlAction( m_sceneCoordinator.AdvanceScene( m_diagnosticsRuntime.PerfTestActive(),
                                                                              sPerfPass,
                                                                              SceneState().isInteractiveRun ) ) )
            {
                PostQuitMessage( 0 );
            }
            return true;
        }

        if ( CanSceneAutomationQuit() )
        {
            SceneState().isTestComplete = true;
        }
        else
        {
            HoldCompletedInteractiveScene();
        }
    }

    // Check if target frame count is reached (skip if screenshot auto-exit is still pending)
    if ( SceneState().targetFrameCount > 0 && !m_diagnosticsRuntime.Capture().Screenshot().isScreenshotSaved )
    {
        if ( SceneState().currentFrame >= SceneState().targetFrameCount )
        {
            const bool frameCountCompletesScene = !hasRequiredSceneGate || requiredSceneComplete;
#ifdef _DEBUG
            if ( !SceneState().isTestComplete &&
                 ( frameCountCompletesScene || SceneState().currentFrame == SceneState().targetFrameCount ) )
            {
                LogSceneFinished( frameCountCompletesScene ? "frame_count" : "required_scene_gates_missing" );
                if ( !frameCountCompletesScene )
                {
                    for ( const RunRequiredContactState& contact : m_requiredSceneContacts )
                    {
                        if ( contact.bodyA < 0 || contact.bodyB < 0 || !contact.touched )
                        {
                            fprintf( stderr,
                                     "[scene] required_contact missing: %s <-> %s\n",
                                     contact.nameA,
                                     contact.nameB );
                        }
                    }
                    for ( const RunRequiredBroadphaseXCellsState& cells : m_requiredBroadphaseXCells )
                    {
                        if ( !cells.activated )
                        {
                            fprintf( stderr,
                                     "[scene] required_broadphase_x_cells missing: x %d..%d y %d z %d first_missing=%d "
                                     "active_cells=%d observed_x=%s%d..%d\n",
                                     cells.minCellX,
                                     cells.maxCellX,
                                     cells.cellY,
                                     cells.cellZ,
                                     cells.lastMissingCellX,
                                     cells.lastActiveCellCount,
                                     cells.hasObservedXRange ? "" : "none ",
                                     cells.lastObservedMinX,
                                     cells.lastObservedMaxX );
                        }
                    }
                }
            }
#endif
            if ( !frameCountCompletesScene )
            {
                return false;
            }

            if ( SceneState().isExitOnComplete && CanSceneAutomationQuit() )
            {
                if ( !executeSceneControlAction( m_sceneCoordinator.AdvanceScene( m_diagnosticsRuntime.PerfTestActive(),
                                                                                  sPerfPass,
                                                                                  SceneState().isInteractiveRun ) ) )
                {
                    PostQuitMessage( 0 );
                }
                return true;
            }
            else
            {
                if ( frameCountCompletesScene && CanSceneAutomationQuit() )
                {
                    SceneState().isTestComplete = true;
                }
                else if ( frameCountCompletesScene )
                {
                    HoldCompletedInteractiveScene();
                }
            }
        }
    }

    // Generated demo mode: restart every 20s to keep the sandbox moving indefinitely.
    if ( !SceneState().isSceneMode && !IsManualCameraMode() && m_timers.simulationTimer.GetTimeSinceLastStart() > 20.0 )
    {
        LoadScene( SceneState().currentSceneIndex,
                   SceneState().isInteractiveRun,
                   SceneState().isInteractiveRun,
                   SceneState().isInteractiveRun );
        m_timers.simulationTimer.StartTimer();
        return true;
    }

    // Perf-log scenes without an explicit frame count still use a timed pass duration.
    if ( m_diagnosticsRuntime.PerfTestActive() && SceneState().targetFrameCount <= 0 &&
         m_timers.simulationTimer.GetTimeSinceLastStart() > PERF_TEST_PASS_SECONDS )
    {
#ifdef _DEBUG
        LogSceneFinished( "perf_duration" );
#endif
        if ( !executeSceneControlAction( m_sceneCoordinator.AdvanceScene( m_diagnosticsRuntime.PerfTestActive(),
                                                                          sPerfPass,
                                                                          SceneState().isInteractiveRun ) ) )
        {
            if ( CanSceneAutomationQuit() )
            {
                PostQuitMessage( 0 );
            }
            else
            {
                HoldCompletedInteractiveScene();
            }
        }
        return true;
    }

    return false;
}


void Run::UpdateLogic( float simulationDt, float cameraDt )
{
    // Auto-cycle
    if ( SceneState().isSceneMode && m_camera.autoCycleInterval > 0.0f )
    {
        m_camera.autoCycleAccum += simulationDt;
    }

    // Camera controls are presentation-time behavior, not simulation-time
    // behavior. Keyboard travel is velocity-based, so it consumes unscaled real
    // frame time. Mouse look consumes a per-frame cursor delta, so using live dt
    // would make sensitivity vary with FPS; the fixed reference preserves the
    // existing 60 Hz tuning while making the result frame-rate independent.
    const EngineConfig& cfg = m_config;
    MoveCamera( cameraDt * cfg.keySpeed, CAMERA_MOUSE_REFERENCE_DT * cfg.mouseSensitivity );
    TickAttachedCamera();

    UpdateWaterHeightControls( simulationDt );

    // Tween speed is also presentation-time behavior. The selected destination
    // camera can still track moving scene objects, but the interpolation rate
    // itself should be stable in real seconds instead of following time_scale.
    m_systems.cameras->SetTweenSpeed( cfg.cameraTweenRate * cameraDt );
}


void Run::UpdateWaterHeightControls( float dt )
{
    const bool downNow = Input::IsKeyDown( VK_NEXT );
    const bool upNow = Input::IsKeyDown( VK_PRIOR );
    if ( downNow == upNow )
    {
        return;
    }

    const float direction = upNow ? 1.0f : -1.0f;
    const float height = m_cWorldEnvironment.GetFluidSurfaceHeight() + direction * WATER_HEIGHT_CONTROL_SPEED * dt;
    m_cWorldEnvironment.SetFluidSurfaceHeight( height );
}
