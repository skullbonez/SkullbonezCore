/*
File: SkullbonezSource/Runtime/Render/RuntimeRenderHost.h
Purpose:
  Names the non-render services borrowed by runtime render passes.

Mental model:
  RuntimeRenderer owns pass order and pass objects. RuntimeRenderHost is the
  explicit bridge to runtime services while later phases continue moving editor,
  scene, and UI presentation behind narrower services.

Glossary:
  Render host: Borrowed service view used by render passes while Run remains
  the broader composition root.
  Binding: Pointer set that connects host methods to current runtime owners.
  Callback: Transitional function pointer used for behavior still implemented
  on Run.

Invariants:
  - RuntimeRenderHost does not own the referenced state.
  - All references must outlive RuntimeRenderer and its passes.
  - Callback functions are bound once by Run construction; they preserve the
    remaining Run-side behavior until later phases move those services behind
    narrower owners.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h
  - Agentic/Plans/runtime-run-decomposition-plan.md
*/
#pragma once

#include "../../Core/Common.h"
#include "../../Core/MainMemoryStats.h"
#include "../../Maths/Matrix4.h"
#include "../../Maths/Vector3.h"
#include "../../Rendering/Shadow.h"
#include "../../UI/UI.h"
#include "../Replay/ReplayRuntime.h"
#include "../Tools/RuntimeTools.h"

#include <array>
#include <cstdint>

namespace SkullbonezCore
{
namespace Environment
{
class WorldEnvironment;
}
namespace GameObjects
{
class GameModelCollection;
}
namespace Physics
{
class BroadphaseVisualizer;
class CollisionVisualizer;
class PhysicsDebugVisualizer;
} // namespace Physics
namespace UI
{
class InGameUI;
}
namespace Basics
{
class LauncherLaser;
class RuntimeInputContext;
class SceneController;
enum class RunCameraMode;
struct CinematicRenderConfig;
struct RenderFrameContext;
struct ReplayPresentationSample;
struct ReplaySolverFrameSample;
struct RunCameraState;
struct RunDebugState;
struct RunEditorPlacementState;
struct RunMousePickupState;
struct RunRayCastTestState;
struct RunReplayPredictionFrame;
struct RunRuntimeSettings;
struct RunSceneBrowserState;
struct RunSceneState;
struct RunSubsystemState;
struct RunTimerState;
class ReplayRuntime;
struct RuntimeViewModel;
namespace ReplayOverlay
{
struct ReplayOverlayRenderContext;
}

struct RuntimeRenderHostBindings
{
    RunSubsystemState* systems = nullptr;
    RunDebugState* debug = nullptr;
    RunTimerState* timers = nullptr;
    RunRuntimeSettings* runtimeSettings = nullptr;
    GameObjects::GameModelCollection* gameModelCollection = nullptr;
    Environment::WorldEnvironment* worldEnvironment = nullptr;
    Physics::CollisionVisualizer* collisionVisualizer = nullptr;
    Physics::BroadphaseVisualizer* broadphaseVisualizer = nullptr;
    Physics::PhysicsDebugVisualizer* physicsDebugVisualizer = nullptr;
    std::array<float, MAX_GAME_MODELS * 16>* dxrReflectionTransforms = nullptr;
    RunRayCastTestState* rayCastTest = nullptr;
    RunEditorPlacementState* editor = nullptr;
    RunMousePickupState* mousePickup = nullptr;
    ReplayRuntime* replayRuntime = nullptr;
    LauncherLaser* launcherLaser = nullptr;
    UI::InGameUI* ui = nullptr;
    RuntimeInputContext* runtimeInput = nullptr;
    RunCameraState* camera = nullptr;
    RuntimeViewModel* runtimeViewModel = nullptr;
    SceneController* sceneController = nullptr;
    RunSceneBrowserState* sceneBrowser = nullptr;
};

struct RuntimeRenderHostCallbacks
{
    using ActiveCinematicConfigFn = CinematicRenderConfig& (*)( void* user );
    using BoolFn = bool ( * )( void* user );
    using TextureHandleFn = uint32_t ( * )( void* user, uint32_t textureHash );
    using SelectRenderTextureFn = void ( * )( void* user, uint32_t textureHash );
    using IntFn = int ( * )( void* user );
    using LogLifecycleStepFn = void ( * )( void* user, const char* phase, const char* step );
    using RenderEditorOverlayFn = void ( * )( void* user,
                                              const Math::Transformation::Matrix4& viewProjection,
                                              const Math::Vector::Vector3& cameraEye,
                                              const Math::Vector::Vector3& cameraUp );
    using VoidFn = void ( * )( void* user );
    using SceneStateFn = const RunSceneState& (*)( void* user );
    using CameraModeEnabledMaskFn = uint32_t ( * )( void* user );
    using CameraModeLabelFn = const char* (*)( void* user, RunCameraMode mode );
    using MainMemoryStatsFn = MainMemoryStats ( * )( void* user, double nowSeconds );

    void* user = nullptr;
    ActiveCinematicConfigFn activeCinematicConfig = nullptr;
    BoolFn isCinematicRenderingEnabled = nullptr;
    BoolFn isLauncherCameraMode = nullptr;
    TextureHandleFn textureHandle = nullptr;
    SelectRenderTextureFn selectRenderTexture = nullptr;
    IntFn windowScreenWidth = nullptr;
    IntFn windowScreenHeight = nullptr;
    LogLifecycleStepFn logRenderResourceLifecycleStep = nullptr;
    RenderEditorOverlayFn renderEditorOverlay = nullptr;
    VoidFn refreshRuntimeViewModel = nullptr;
    SceneStateFn sceneState = nullptr;
    IntFn currentSceneBrowserIndex = nullptr;
    CameraModeEnabledMaskFn cameraModeEnabledMask = nullptr;
    CameraModeLabelFn cameraModeLabel = nullptr;
    MainMemoryStatsFn refreshMainMemoryStats = nullptr;
};

class RuntimeRenderHost
{
  public:
    RuntimeRenderHost( RuntimeRenderHostBindings bindings, RuntimeRenderHostCallbacks callbacks )
        : m_systems( *bindings.systems ), m_debug( *bindings.debug ), m_timers( *bindings.timers ),
          m_runtimeSettings( *bindings.runtimeSettings ), m_cGameModelCollection( *bindings.gameModelCollection ),
          m_cWorldEnvironment( *bindings.worldEnvironment ), m_collisionVisualizer( *bindings.collisionVisualizer ),
          m_broadphaseVisualizer( *bindings.broadphaseVisualizer ),
          m_physicsDebugVisualizer( *bindings.physicsDebugVisualizer ),
          m_dxrReflectionTransforms( *bindings.dxrReflectionTransforms ), m_rayCastTest( *bindings.rayCastTest ),
          m_editor( *bindings.editor ), m_mousePickup( *bindings.mousePickup ),
          m_replayRuntime( *bindings.replayRuntime ), m_launcherLaser( *bindings.launcherLaser ), m_UI( *bindings.ui ),
          m_runtimeInput( *bindings.runtimeInput ), m_camera( *bindings.camera ),
          m_runtimeViewModel( *bindings.runtimeViewModel ), m_sceneController( *bindings.sceneController ),
          m_sceneBrowser( *bindings.sceneBrowser ), m_callbacks( callbacks )
    {
    }

    CinematicRenderConfig& ActiveCinematicConfig() const
    {
        return m_callbacks.activeCinematicConfig( m_callbacks.user );
    }

    bool IsCinematicRenderingEnabled() const
    {
        return m_callbacks.isCinematicRenderingEnabled( m_callbacks.user );
    }

    bool IsLauncherCameraMode() const
    {
        return m_callbacks.isLauncherCameraMode( m_callbacks.user );
    }

    uint32_t TextureHandle( uint32_t textureHash ) const
    {
        return m_callbacks.textureHandle( m_callbacks.user, textureHash );
    }

    void SelectRenderTexture( uint32_t textureHash ) const
    {
        m_callbacks.selectRenderTexture( m_callbacks.user, textureHash );
    }

    int WindowScreenWidth() const
    {
        return m_callbacks.windowScreenWidth( m_callbacks.user );
    }

    int WindowScreenHeight() const
    {
        return m_callbacks.windowScreenHeight( m_callbacks.user );
    }

    void LogRenderResourceLifecycleStep( const char* phase, const char* step ) const
    {
        m_callbacks.logRenderResourceLifecycleStep( m_callbacks.user, phase, step );
    }

    const ReplayPresentationSample* CurrentReplayScrubSample() const
    {
        return m_replayRuntime.CurrentScrubSample();
    }

    const ReplaySolverFrameSample* CurrentReplaySolverScrubSample() const
    {
        return m_replayRuntime.CurrentSolverScrubSample();
    }

    const RunReplayPredictionFrame* CurrentReplayPredictionScrubFrame() const
    {
        return m_replayRuntime.CurrentPredictionScrubFrame();
    }

    void RenderEditorOverlay( const Math::Transformation::Matrix4& viewProjection,
                              const Math::Vector::Vector3& cameraEye,
                              const Math::Vector::Vector3& cameraUp ) const
    {
        m_callbacks.renderEditorOverlay( m_callbacks.user, viewProjection, cameraEye, cameraUp );
    }

    void RefreshRuntimeViewModel() const
    {
        m_callbacks.refreshRuntimeViewModel( m_callbacks.user );
    }

    const RunSceneState& SceneState() const
    {
        return m_callbacks.sceneState( m_callbacks.user );
    }

    bool ShouldRenderReplayScrubber() const
    {
        return m_replayRuntime.ShouldRenderScrubber( m_editor.editorModeEnabled, m_UI.IsVisible(), m_UI.IsMinimized() );
    }

    void RenderReplayScrubberOverlay() const;

    int CurrentSceneBrowserIndex() const
    {
        return m_callbacks.currentSceneBrowserIndex( m_callbacks.user );
    }

    uint32_t CameraModeEnabledMask() const
    {
        return m_callbacks.cameraModeEnabledMask( m_callbacks.user );
    }

    const char* CameraModeLabel( RunCameraMode mode ) const
    {
        return m_callbacks.cameraModeLabel( m_callbacks.user, mode );
    }

    MainMemoryStats RefreshMainMemoryStats( double nowSeconds ) const
    {
        return m_callbacks.refreshMainMemoryStats ? m_callbacks.refreshMainMemoryStats( m_callbacks.user, nowSeconds )
                                                  : MainMemoryStats();
    }

    bool BuildReplayFocusModelMask() const
    {
        return m_replayRuntime.BuildFocusModelMask( m_cGameModelCollection );
    }

    void RenderReplayPredictionGhosts( const RenderFrameContext& frame,
                                       const CinematicRenderConfig* cinematic,
                                       const Rendering::ShadowFrameData* shadow ) const;

    RunSubsystemState& m_systems;
    RunDebugState& m_debug;
    RunTimerState& m_timers;
    RunRuntimeSettings& m_runtimeSettings;
    GameObjects::GameModelCollection& m_cGameModelCollection;
    Environment::WorldEnvironment& m_cWorldEnvironment;
    Physics::CollisionVisualizer& m_collisionVisualizer;
    Physics::BroadphaseVisualizer& m_broadphaseVisualizer;
    Physics::PhysicsDebugVisualizer& m_physicsDebugVisualizer;
    std::array<float, MAX_GAME_MODELS * 16>& m_dxrReflectionTransforms;
    RunRayCastTestState& m_rayCastTest;
    RunEditorPlacementState& m_editor;
    RunMousePickupState& m_mousePickup;
    ReplayRuntime& m_replayRuntime;
    LauncherLaser& m_launcherLaser;
    UI::InGameUI& m_UI;
    RuntimeInputContext& m_runtimeInput;
    RunCameraState& m_camera;
    RuntimeViewModel& m_runtimeViewModel;
    SceneController& m_sceneController;
    RunSceneBrowserState& m_sceneBrowser;

  private:
    ReplayOverlay::ReplayOverlayRenderContext BuildReplayOverlayRenderContext() const;
    void RenderReplayCauseTreeOverlay() const;

    RuntimeRenderHostCallbacks m_callbacks;
};

} // namespace Basics
} // namespace SkullbonezCore
