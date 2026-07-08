/*
File: SkullbonezSource/Runtime/InputController.h
Purpose:
  Provides runtime input edge detection and camera mouse-look input policy.

Mental model:
  Hardware::Input reads device state. InputController turns that state into
  stable per-frame runtime input events and camera deltas.

Glossary:
  Input edge: Transition from not pressed to pressed, used for one-shot
  commands.
  Camera delta: Per-frame mouse movement accumulated before camera update.
  Runtime input event: Frame-local input state consumed by Run.

Invariants:
  - RuntimeInputAction order is shared by fixed-size arrays in
    RuntimeInputContext and should only grow by appending before Count.
  - RuntimeInputFrameState contains resolved per-frame policy; it must not own
    persistent subsystem state.

Related:
  - SkullbonezSource/Runtime/InputController.cpp
  - SkullbonezSource/Runtime/RunInput.cpp
*/
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "Input.h"

namespace SkullbonezCore
{
namespace Basics
{
struct RunCameraState;

struct RuntimeKeyEdge
{
    bool isDown = false;
    bool wasPressed = false;
};

enum class RuntimeInputMode
{
    Scene,
    FlyCamera,
    Launcher,
    Manipulator,
    EditorPlace,
    EditorGizmo,
    EditorViewportLook,
    EditorPlaceScale,
    EditorGizmoTranslate,
    EditorGizmoRotate,
    EditorGizmoScale
};

enum class RuntimeInputAction
{
    None,
    ToggleFlyCamera,
    ToggleLauncher,
    CycleCameraMode,
    SetCameraMode,
    CycleAttachedCameraSubmode,
    ToggleAttachedCameraPin,
    ToggleEditor,
    ToggleEditorTool,
    CycleEditorPlacementType,
    ToggleEditorStaticPlacement,
    ToggleEditorTerrainAlign,
    BeginEditorViewportLook,
    EndEditorViewportLook,
    BeginEditorPlacementScale,
    EndEditorPlacementScale,
    BeginEditorGizmoTranslate,
    BeginEditorGizmoRotate,
    BeginEditorGizmoScale,
    EndEditorGizmoDrag,
    CycleLauncherFireMode,
    FireLauncher,
    WriteLauncherReproSnapshot,
    ToggleWaterFreeze,
    CycleWaterReflection,
    ToggleWaterFlat,
    ToggleTerrainHidden,
    ToggleWaterHidden,
    ToggleCollisionVisualizer,
    CyclePhysicsDebugOverlay,
    ToggleTerrainContactProbe,
    StepPhysicsPipelinePrevious,
    StepPhysicsPipelineNext,
    TogglePhysicsDebugTransparent,
    ReportRendererRuntimeRetired,
    ToggleBroadphaseOverlay,
    ToggleUIVisibility,
    TogglePerformanceHistogram,
    ToggleMemoryOverlay,
    NavigateScenePrevious,
    NavigateSceneNext,
    DismissOrExitUI,
    SaveSceneSnapshot,
    SaveScreenshot,
    ResetScene,
    ResetSceneFromBackspace,
    ResetSceneDefaults,
    LoadDemoScene,
    SaveSceneDefaults,
    CreateScene,
    SelectScene,
    ToggleVsync,
    TogglePhysicsSleepPolicy,
    TogglePhysicsDebugFlags,
    ToggleTornado,
    ToggleTornadoVisualShell,
    ToggleTornadoFieldVectors,
    ToggleRayCastVisualization,
    ApplyTornadoSettings,
    ToggleTextOnly,
    ToggleFixedStep,
    ToggleShadows,
    SetTimeScale,
    SetRunSeed,
    SetPhysicsDebugAlpha,
    SetPhysicsDebugContactLinger,
    SetRayCastImpulseStrength,
    SetLauncherProjectileSpeed,
    ApplyPhysicsFrictionSettings,
    SetModelCount,
    SetWorkerThreads,
    SetSolverCounts,
    ToggleWaterReflection,
    SetWaterReflectionMode,
    ApplyWorldWaterSettings,
    ToggleRenderShadows,
    SaveRenderDefaults,
    ApplyRenderTuning,
    ApplySoundTuning,
    ToggleCinematicRendering,
    SelectCinematicScene,
    ToggleCinematicFeature,
    ApplyCinematicParam,
    SaveSkyDefaults,
    ToggleCrossScenePause,
    ToggleDirectorGrab,
    SetDirectorPhasePose,
    StepDirectorPhase,
    SaveDirectorShotList,
    Count
};

enum class RuntimeInputActionSource
{
    Keyboard,
    UI,
    Mouse,
    FocusLost,
    Runtime
};

using RuntimeInputContextMask = uint32_t;

enum class RuntimeInputBindingContext : RuntimeInputContextMask
{
    Always = 0u,
    KeyboardUnblocked = 1u << 0,
    Scene = 1u << 1,
    GeneratedDemo = 1u << 2,
    FlyCamera = 1u << 3,
    Launcher = 1u << 4,
    AttachedCamera = 1u << 5,
    AttachedCameraActive = 1u << 6,
    Director = 1u << 7,
    DirectorAuthoring = 1u << 8,
    Editor = 1u << 9,
    EditorInactive = 1u << 10,
    Replay = 1u << 11,
    UI = 1u << 12,
    DebugOnly = 1u << 13
};

constexpr RuntimeInputContextMask RuntimeInputContextBit( RuntimeInputBindingContext context )
{
    return static_cast<RuntimeInputContextMask>( context );
}

struct RuntimeInputKeyBinding
{
    // Concept: The table vocabulary is shared input metadata. Step 1.1 only
    // names key/action/context records; later slices will move TakeInput's
    // branch dispatch onto this data without changing command behavior here.
    int virtualKey = 0;
    RuntimeInputAction action = RuntimeInputAction::None;
    RuntimeInputContextMask contexts = RuntimeInputContextBit( RuntimeInputBindingContext::KeyboardUnblocked );
};

struct RuntimeInputModeState
{
    bool flyCamera = false;
    bool launcher = false;
    bool manipulator = false;
    bool editor = false;
    bool editorPlacement = false;
    bool editorViewportLook = false;
    bool editorPlacementScale = false;
    bool editorGizmoDrag = false;
    bool editorGizmoRotation = false;
    bool editorGizmoScale = false;
};

struct RuntimeMouseEdges
{
    bool leftDown = false;
    bool leftPressed = false;
    bool leftReleased = false;
    bool rightDown = false;
    bool rightPressed = false;
    bool rightReleased = false;
};

struct RuntimeInputTransition
{
    RuntimeInputMode from = RuntimeInputMode::Scene;
    RuntimeInputMode to = RuntimeInputMode::Scene;
    RuntimeInputAction action = RuntimeInputAction::None;
    RuntimeInputActionSource source = RuntimeInputActionSource::Runtime;
};

class RuntimeInputContext
{
  public:
    RuntimeInputContext();

    void BeginFrame( bool appFocused, bool uiBlocksKeyboard, bool uiBlocksMouse );
    void ResetEdges();
    bool CaptureActionPress( RuntimeInputAction action, int virtualKey );
    void SetActionDown( RuntimeInputAction action, bool isDown );
    RuntimeMouseEdges CaptureMouseButtons( bool leftDown, bool rightDown );
    void SetMode( RuntimeInputMode mode, RuntimeInputAction action, RuntimeInputActionSource source );

    RuntimeInputMode CurrentMode() const;
    RuntimeInputMode PreviousMode() const;
    bool AppFocused() const;
    bool UIBlocksKeyboard() const;
    bool UIBlocksMouse() const;
    int TransitionCount() const;
    RuntimeInputTransition TransitionAt( int historyIndex ) const;

  private:
    static constexpr int TRANSITION_HISTORY_COUNT = 8;
    static constexpr std::size_t ACTION_COUNT = static_cast<std::size_t>( RuntimeInputAction::Count );

    void ResetMouseButtons();
    void SyncMouseButtons( bool leftDown, bool rightDown );

    RuntimeInputMode m_currentMode = RuntimeInputMode::Scene;
    RuntimeInputMode m_previousMode = RuntimeInputMode::Scene;
    bool m_appFocused = true;
    bool m_uiBlocksKeyboard = false;
    bool m_uiBlocksMouse = false;
    std::array<bool, ACTION_COUNT> m_actionDown = {};
    bool m_leftMouseWasDown = false;
    bool m_rightMouseWasDown = false;
    RuntimeInputTransition m_transitions[TRANSITION_HISTORY_COUNT] = {};
    int m_transitionWriteIndex = 0;
    int m_transitionCount = 0;
};

class InputController
{
  public:
    static RuntimeKeyEdge
    CaptureKeyEdge( Hardware::InputState& state, Hardware::InputState::Key memoryKey, int virtualKey );
    static bool CaptureKeyPress( bool& wasDown, int virtualKey );
    static void BeginFrame( RuntimeInputContext& context,
                            const RuntimeInputModeState& modeState,
                            bool appFocused,
                            bool uiBlocksKeyboard,
                            bool uiBlocksMouse );
    static bool CaptureKeyboardActionPress( RuntimeInputContext& context, RuntimeInputAction action, int virtualKey );
    static void ApplyModeAction( RuntimeInputContext& context,
                                 RuntimeInputMode mode,
                                 RuntimeInputAction action,
                                 RuntimeInputActionSource source );
    static RuntimeInputMode ResolveMode( const RuntimeInputModeState& state );
    static const char* DescribeMode( RuntimeInputMode mode );
    static const char* DescribeAction( RuntimeInputAction action );
    static const char* DescribeSource( RuntimeInputActionSource source );
    static void DescribeLastTransitions( const RuntimeInputContext& context, char* out, std::size_t outSize );
    static void
    ResetUnfocusedInput( RunCameraState& camera, bool& leftSceneCycleWasDown, bool& rightSceneCycleWasDown );
    static void ResetMouseLook( RunCameraState& camera );
    static void SetMouseLookDelta( RunCameraState& camera, long rawX, long rawY );
};
} // namespace Basics
} // namespace SkullbonezCore
