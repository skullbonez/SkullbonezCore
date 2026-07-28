/*
File: InputRouter.h
Purpose:
  Declares the allocation-free device snapshot, semantic action router, shared
  post-UI pointer value, and native pointer-presentation intent owner.

Summary:
  A device owner captures hardware once into DeviceInputFrame. InputRouter then
  advances key/button memory exactly once and emits ordered semantic action
  events in the same order as the immutable binding table. Context predicates
  decide whether an observed edge is delivered; they never decide whether edge
  memory advances. A separate phase cursor enforces world-pointer precedence
  without storing the participating editor, pickup, camera, Replay, or launcher.

Glossary:
  UI (user interface): Interactive engine controls evaluated between the input
    router's pre-UI and after-UI phases.
  Win32: Windows desktop API whose virtual-key values occupy the range 0..255.
  Device snapshot: Immutable-by-contract value containing one frame's keys,
    pointer buttons, coordinates, wheel delta, and raw mouse delta.
  Semantic action: RuntimeInputAction produced from a physical key binding.
  Route phase: Pre-UI, after-UI, or capture stage that owns a binding row.
  Context predicate: All-of bit mask that must be active before an action edge
    may be delivered.
  Focus resynchronization: First focused sample after focus loss; held inputs are
    remembered without being reported as fresh presses.
  Transition cleanup: Ordered cancellation sent to the replay/tool owners before
    a new workspace or world-input owner begins consuming gestures.
  Lifecycle activation: Completed scene-load phase that can publish a new
    cursor intent and request hardware mouse-delta cleanup.
  Pointer arbitration: Ordered phase cursor that gives the first consuming
    world-pointer stage exclusive ownership.

Invariants:
  - BeginFrame is called once before any RoutePhase call for a device snapshot.
  - BeginFrame and every RoutePhase call for that frame use the same immutable
    binding view, and each phase is routed at most once.
  - Binding rows for one action are unique; action-indexed edge memory therefore
    has one physical key source.
  - Every binding is sampled even when its context is inactive, preventing a
    held key from becoming a false press when a context later activates.
  - Action output is fixed-capacity and preserves binding-table/phase call order.
  - Focus loss releases delivered actions and pointer buttons exactly once.
  - UI, replay, editor, and camera consumers observe one copied post-UI pointer
    snapshot; only InputRouter advances button-edge memory.
  - Native capture and cursor visibility are desired state. The composition
    root applies only changes reported by ConsumePointerPresentationChange;
    the first consume always publishes both values so Win32 cannot retain a
    pre-router startup state.
  - Transition cleanup borrows concrete owners synchronously and never stores
    their references or absorbs their domain state.
  - A scene generation publishes its activation cursor intent at most once.
  - World-pointer stages visit editor, mouse pickup, attached camera, Replay,
    and launcher exactly once in that order; later stages cannot replace a winner.

Related:
  - InputController.h defines the existing action and context vocabulary.
  - InputController.Bindings.h publishes the current immutable binding table.
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md owns the extraction.
*/
#pragma once

#include "../../Core/PlatformWin32.h"
#include "../Scene/SceneLifecycle.h"

#include "InputController.Bindings.h"
#include "../Replay/ReplayEventCommand.h"
#include "../Interaction/RuntimeInteractionController.h"
#include "../../Maths/Vector3.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace SkullbonezCore
{
namespace Core
{
class SbDiagnosticStore;
}
namespace Environment
{
class CameraCollection;
class WorldEnvironment;
} // namespace Environment
namespace Assets
{
class AssetSystem;
}
namespace Geometry
{
class Terrain;
}
namespace GameObjects
{
struct PresentationSaveState;
}
namespace Runtime
{
class SceneController;
class RuntimeOverlayDiagnostics;
} // namespace Runtime
namespace Physics
{
class PhysicsEngine;
} // namespace Physics
namespace UI
{
class InGameUI;
}
namespace Runtime
{
struct CameraControlState;
struct SceneSessionState;
enum class RunCameraMode;
enum class WorldInteractionOwner;
enum class InteractionExitReason;
class ReplayRuntime;
struct ReplayInputView;
class RuntimeInteractionController;
class RuntimeTools;
class AttachedCameraController;
class SceneEntityStore;
class SceneController;
class DiagnosticsRuntime;
class Window;
struct OverlayDebugState;

struct EditorPointerRouteResult
{
    static constexpr std::size_t MAX_MODE_ACTIONS = 2;
    ReplayEventCommandBatch replayEvents;
    RuntimeInteractionTransition interactionTransition;
    bool consumed = false;
    bool enteredInteractiveScene = false;
    bool hasInteractionTransition = false;
    std::array<RuntimeInputAction, MAX_MODE_ACTIONS> modeActions = {};
    std::size_t modeActionCount = 0;
};

struct RuntimePointerRouteResult
{

    // Invariant: actions preserve editor/end-before-begin and domain priority;
    // composition applies them after the router finishes synchronous borrows.
    static constexpr std::size_t MAX_MODE_ACTIONS = 2;
    bool consumed = false;
    bool enteredInteractiveScene = false;
    std::array<RuntimeInputAction, MAX_MODE_ACTIONS> modeActions = {};
    std::size_t modeActionCount = 0;
};

enum class RuntimePointerRouteStage : uint8_t
{
    None,
    Editor,
    MousePickup,
    AttachedCamera,
    Replay,
    Launcher,
    Complete
};

// Invariant: one route visits Editor -> MousePickup -> AttachedCamera -> Replay
// -> Launcher exactly once. The first consuming stage wins; later stages remain
// structurally visited but receive no owner call. Illegal phase transitions are
// Lane F failures. SkullbonezTests/TestInputRouter.cpp exhaustively exercises
// every combination of the five stage claims.
class RuntimePointerArbitration
{
  public:
    bool BeginStage( RuntimePointerRouteStage stage );
    void FinishStage( RuntimePointerRouteStage stage, bool consumed );
    bool Consumed() const;
    RuntimePointerRouteStage Winner() const;

  private:
    RuntimePointerRouteStage m_next = RuntimePointerRouteStage::Editor;
    RuntimePointerRouteStage m_active = RuntimePointerRouteStage::None;
    RuntimePointerRouteStage m_winner = RuntimePointerRouteStage::None;
};

class InputKeySnapshot
{
  public:
    static constexpr int VIRTUAL_KEY_COUNT = 256;
    static constexpr std::size_t WORD_COUNT = VIRTUAL_KEY_COUNT / 64;

    // Builds a snapshot from already-captured bit words. Bit n represents the
    // Win32 virtual key with value n; no hardware is polled by this type.
    static InputKeySnapshot FromWords( const std::array<uint64_t, WORD_COUNT>& words );

    // Convenience for tests and automation capture. Invalid virtual-key values
    // are ignored, and the caller-owned array is not retained.
    static InputKeySnapshot FromDownKeys( const int* virtualKeys, std::size_t keyCount );

    bool IsDown( int virtualKey ) const;
    const std::array<uint64_t, WORD_COUNT>& Words() const;

  private:
    std::array<uint64_t, WORD_COUNT> m_words = {};
};


struct DeviceInputFrame
{

    // Lifetime: capture code finishes this value before handing it to the
    // router. Router and downstream consumers receive it as const and never
    // retain references beyond the frame.
    InputKeySnapshot keys;
    int clientX = 0;
    int clientY = 0;
    long rawMouseX = 0;
    long rawMouseY = 0;
    int wheelDelta = 0;
    bool hasClientPosition = false;
    bool appFocused = false;
    bool leftDown = false;
    bool rightDown = false;
    bool middleDown = false;
};


struct UiInputCaptureIntent
{

    // Value-only arbitration from an external tool UI. InputRouter filters the
    // corresponding device class and resynchronizes held levels when ownership
    // returns so a tool keystroke cannot become a gameplay press.
    bool mouse = false;
    bool keyboard = false;
    bool text = false;
    bool nativePointerStateTouched = false;

    // Value-only fitted image rectangle from an external editor. Input
    // composition maps the one captured client point through it before any
    // world pick, placement, camera, or gizmo owner sees coordinates.
    bool gameViewportMappingActive = false;
    float gameViewportMinX = 0.0f;
    float gameViewportMinY = 0.0f;
    float gameViewportWidth = 0.0f;
    float gameViewportHeight = 0.0f;
    float gameViewportDpiScale = 1.0f;
    int gameViewportSourceWidth = 0;
    int gameViewportSourceHeight = 0;
};


struct UiInputHitSnapshot
{

    // Lifetime: published once after UI hit testing and retained by InputRouter
    // only until the next DeviceInputFrame begins.
    RuntimeMouseEdges mouse;
    int clientX = 0;
    int clientY = 0;
    int unhandledWheelDelta = 0;
    bool hasClientPosition = false;
    bool userInteracted = false;
    bool blocksKeyboard = false;
    bool blocksCameraMouse = false;
    bool wantsNativeCursor = false;
};


struct PointerPresentationState
{

    // Value boundary between the platform-neutral router and Win32 hardware.
    bool nativeCapture = false;
    bool cursorVisible = true;
};


struct PointerPresentationPolicyInput
{

    // Value-only owner facts joined with InputRouter's device/UI snapshots.
    bool editorModeEnabled = false;
    bool editorViewportLookActive = false;
    bool editorPlacementModeEnabled = false;
    bool editorPlacementPreviewVisible = false;
    bool replayInspectionActive = false;
    bool replayInspectionLookActive = false;
};


struct PointerPresentationPolicy
{
    bool mouseLookOwnsCursor = false;
    bool hideNativeCursor = false;
};


enum class InputActionPhase : uint8_t
{
    PreUi,
    AfterUi,
    Capture
};


enum class InputActionEdge : uint8_t
{
    Pressed,
    Held,
    Released
};


struct InputActionEvent
{
    RuntimeInputAction action = RuntimeInputAction::None;
    RuntimeInputActionSource source = RuntimeInputActionSource::Runtime;
    InputActionPhase phase = InputActionPhase::PreUi;
    InputActionEdge edge = InputActionEdge::Released;
    int virtualKey = 0;
};


class InputActions
{
  public:
    static constexpr std::size_t CAPACITY = static_cast<std::size_t>( RuntimeInputAction::Count );

    void Reset();
    std::size_t Count() const;
    bool Empty() const;
    bool Overflowed() const;

    // Precondition: index is less than Count().
    const InputActionEvent& operator[]( std::size_t index ) const;
    const InputActionEvent* Data() const;

    RuntimeMouseEdges mouse;
    bool focusLost = false;
    bool focusGained = false;

  private:
    friend class InputRouter;
    bool TryAppend( const InputActionEvent& event );

    std::array<InputActionEvent, CAPACITY> m_events = {};
    std::size_t m_count = 0;
    bool m_overflowed = false;
};


class InputRouter
{
  public:
    explicit InputRouter( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics );

    RuntimeInputContext& RuntimeContext();
    const RuntimeInputContext& RuntimeContext() const;
    InputActions& Actions();
    const InputActions& Actions() const;

    // Captures button edges, advances every binding's key memory, and handles
    // focus transitions. The output is reset here so subsequent RoutePhase
    // calls append one ordered frame result.
    void BeginFrame( const DeviceInputFrame& frame, RuntimeInputKeyBindingView bindings, InputActions& output,
                     UiInputCaptureIntent capture = {} );

    // Emits the selected phase in binding-table order. activeContexts contains
    // current mode/UI facts; AfterUi/Capture phase bits are supplied by the
    // router so callers cannot accidentally route a row through the wrong pass.
    void RoutePhase( RuntimeInputKeyBindingView bindings, InputActionPhase phase, RuntimeInputContextMask activeContexts,
                     InputActions& output );

    void Reset();
    bool AppFocused() const;
    const DeviceInputFrame& DeviceFrame() const;
    void PublishUiSnapshot( const UiInputHitSnapshot& snapshot );
    const UiInputHitSnapshot& UiSnapshot() const;

    // Builds the one post-UI pointer/policy value from router-owned snapshots.
    // Cross-domain policy facts arrive as values and are not retained.
    RuntimeInputSnapshot BuildRuntimeSnapshot( const RuntimeInteractionFrameInput& frameInput,
                                               bool suppressWorldAction ) const;

    // Publishes the immutable value consumed after the input turn; later phases
    // must not reopen DeviceFrame.
    const RuntimeInputSnapshot& PublishRuntimeSnapshot( const RuntimeInteractionFrameInput& frameInput,
                                                        bool suppressWorldAction );
    const RuntimeInputSnapshot& RuntimeSnapshot() const;
    PointerPresentationPolicy EvaluatePointerPresentation( const PointerPresentationPolicyInput& input ) const;
    void ApplyPointerPresentation( const PointerPresentationPolicy& policy ); // Commits the policy's desired native cursor visibility.
    bool ReleasePointerToUi( const PointerPresentationPolicy& policy );       // Releases native capture only when mouse look has no stronger claim.
    void ApplyInteractionTransitionCleanup( const RuntimeInteractionTransition& transition, RuntimeTools& runtimeTools,
                                            RuntimeInteractionController& interaction,
                                            AttachedCameraController& attachedCamera, CameraControlState& camera,
                                            SceneController& sceneController, ReplayRuntime& replayRuntime,
                                            RunCameraMode replayRestoreCameraMode );
    void ApplyInteractionTransition( const RuntimeInteractionTransition& transition, RuntimeTools& runtimeTools,
                                     RuntimeInteractionController& interaction, AttachedCameraController& attachedCamera,
                                     CameraControlState& camera, SceneController& sceneController,
                                     ReplayRuntime& replayRuntime, RunCameraMode replayRestoreCameraMode );
    RuntimeInteractionTransition
    SetWorldInteractionOwner( WorldInteractionOwner owner, InteractionExitReason reason, RuntimeTools& runtimeTools,
                              RuntimeInteractionController& interaction, AttachedCameraController& attachedCamera,
                              CameraControlState& camera, SceneController& sceneController, ReplayRuntime& replayRuntime,
                              RunCameraMode replayRestoreCameraMode );

    // Camera-mode requests are input-owner transitions: the router sequences
    // interaction cleanup, camera/editor state, and pointer presentation while
    // retaining none of the borrowed domain owners.
    void ApplyCameraMode( RunCameraMode mode, RuntimeInputActionSource source, RuntimeTools& runtimeTools,
                          RuntimeInteractionController& interaction, AttachedCameraController& attachedCamera,
                          CameraControlState& camera, SceneController& sceneController, ReplayRuntime& replayRuntime,
                          RuntimeInputContext& runtimeInput );
    void CycleCameraMode( RuntimeTools& runtimeTools, RuntimeInteractionController& interaction,
                          AttachedCameraController& attachedCamera, CameraControlState& camera,
                          SceneController& sceneController, ReplayRuntime& replayRuntime,
                          RuntimeInputContext& runtimeInput );
    bool HandleUnfocusedFrame( RuntimeTools& runtimeTools, RuntimeInteractionController& interaction,
                               AttachedCameraController& attachedCamera, CameraControlState& camera, UI::InGameUI& ui,
                               SceneController& sceneController, ReplayRuntime& replayRuntime,
                               RuntimeInputContext& runtimeInput );
    bool DispatchAfterUiDismiss( InputActions& actions, bool uiUserInteracted, double nowSeconds, bool legacyUiActive,
                                 DiagnosticsRuntime& diagnosticsRuntime, CameraControlState& camera,
                                 AttachedCameraController& attachedCamera, RuntimeTools& runtimeTools, UI::InGameUI& ui,
                                 SceneController& sceneController, RuntimeOverlayDiagnostics& overlays,
                                 const ReplayInputView& replayInput );
    void DispatchCaptureActions( InputActions& actions, DiagnosticsRuntime& diagnosticsRuntime,
                                 const CameraControlState& camera, const AttachedCameraController& attachedCamera,
                                 const UI::InGameUI& ui, SceneController& sceneController,
                                 const GameObjects::PresentationSaveState& presentation,
                                 const ReplayInputView& replayInput );
    void RecordModeAction( const CameraControlState& camera, const RuntimeTools& runtimeTools,
                           const RuntimeInteractionController& interaction, const AttachedCameraController& attachedCamera,
                           RuntimeInputContext& runtimeInput, RuntimeInputAction action, RuntimeInputActionSource source );
    EditorPointerRouteResult
    RouteEditorPointer( const RuntimePointerEvent& pointer, bool hasWorldRay, const Math::Vector::Vector3& rayOrigin,
                        const Math::Vector::Vector3& rayDirection, RunCameraMode cameraMode, bool replayInspectionActive,
                        int activeModelCapacity, Assets::AssetSystem& assets, RuntimeTools& runtimeTools,
                        RuntimeInteractionController& interaction, SceneController& sceneController );
    RuntimePointerRouteResult RouteRuntimePointer( const RuntimePointerEvent& pointer, bool replayInspectionActive,
                                                   int activeModelCapacity, const Window& window,
                                                   Assets::AssetSystem& assets, RuntimeTools& runtimeTools,
                                                   AttachedCameraController& attachedCamera,
                                                   RuntimeInteractionController& interaction, CameraControlState& camera,
                                                   SceneController& sceneController, ReplayRuntime& replayRuntime,
                                                   RunCameraMode replayRestoreCameraMode );
    bool TryBuildWorldRay( const Environment::CameraCollection& cameras, const Window& window,
                           Math::Vector::Vector3& outOrigin, Math::Vector::Vector3& outDirection,
                           bool clampToViewport = false ) const;
    bool TryBuildWorldRayAt( POINT clientPosition, const Environment::CameraCollection& cameras, const Window& window,
                             Math::Vector::Vector3& outOrigin, Math::Vector::Vector3& outDirection,
                             bool clampToViewport = false ) const;

    // Pointer presentation requests are reconciled here so UI/tools/camera do
    // not manipulate Win32 capture or cursor counters independently.
    void RequestNativeCapture();
    void ReleaseNativeCapture();
    void RequestCursorVisible( bool visible );
    void CancelPointerPresentation();

    // An external Win32 UI temporarily owns native capture/cursor calls. Force
    // the next engine-owned frame to republish its desired hardware state.
    void DeferPointerPresentationCommit();

    // Returns true only when the hardware-facing state changed since the last
    // consume. Callers apply the returned value atomically at the frame edge.
    bool ConsumePointerPresentationChange( PointerPresentationState& state );
    bool NativeCaptureRequested() const;
    bool CursorVisibleRequested() const;

    // Presentation timing for repeated semantic taps belongs beside action
    // edges, not in a consumer's compatibility input context.
    bool IsQuickRepeat( RuntimeInputAction action, double nowSeconds, double intervalSeconds ) const;
    void RecordTap( RuntimeInputAction action, double nowSeconds );
    bool ObserveSceneLifecycle( const SceneLifecyclePacket& packet, bool hideCursorAfterActivation );

    static bool ContextsSatisfied( RuntimeInputContextMask requiredContexts, RuntimeInputContextMask activeContexts );
    static InputActionPhase PhaseForBinding( const RuntimeInputKeyBinding& binding );

  private:
    static constexpr std::size_t ACTION_COUNT = static_cast<std::size_t>( RuntimeInputAction::Count );
    static constexpr std::size_t PHASE_COUNT = 3;

    static bool IsActionValid( RuntimeInputAction action );
    static bool IsPhaseValid( InputActionPhase phase );
    static std::size_t ActionIndex( RuntimeInputAction action );
    static std::size_t PhaseIndex( InputActionPhase phase );
    static RuntimeInputContextMask EffectiveContexts( InputActionPhase phase, RuntimeInputContextMask activeContexts );

    void CapturePointerEdges( const DeviceInputFrame& frame, InputActions& output );
    void CaptureFocusLoss( RuntimeInputKeyBindingView bindings, InputActions& output );
    void SynchronizeFocusedInputs( const DeviceInputFrame& frame, RuntimeInputKeyBindingView bindings );
    void SampleKeyboard( const DeviceInputFrame& frame, RuntimeInputKeyBindingView bindings );

    SkullbonezCore::Core::SbDiagnosticStore& m_resultDiagnostics;
    std::array<bool, ACTION_COUNT> m_actionDown = {};
    std::array<bool, ACTION_COUNT> m_actionDelivered = {};
    std::array<bool, ACTION_COUNT> m_actionSampledThisFrame = {};
    std::array<InputActionEdge, ACTION_COUNT> m_frameEdges = {};
    std::array<bool, PHASE_COUNT> m_phaseRoutedThisFrame = {};
    std::array<double, ACTION_COUNT> m_lastTapSeconds = {};
    DeviceInputFrame m_deviceFrame;
    UiInputHitSnapshot m_uiSnapshot;
    RuntimeInputSnapshot m_runtimeSnapshot;
    RuntimeInputContext m_runtimeContext;                                     // Semantic mode/action history belongs with routed edge memory.
    InputActions m_actions;                                                   // Fixed per-frame semantic output; reset by BeginFrame.
    bool m_nativeCaptureRequested = false;
    bool m_committedNativeCapture = false;
    bool m_cursorVisibleRequested = true;
    bool m_committedCursorVisible = true;

    // False forces the first composition frame to initialize Win32 capture and cursor state.
    bool m_pointerPresentationCommitted = false;
    bool m_hasFrame = false;
    bool m_appFocused = false;
    SceneLifecycleGenerationObserver m_sceneActivationObserver;
    bool m_frameFocused = false;
    bool m_keyboardCaptured = false;
    bool m_mouseCaptured = false;
    bool m_leftWasDown = false;
    bool m_rightWasDown = false;
};
} // namespace Runtime
} // namespace SkullbonezCore
