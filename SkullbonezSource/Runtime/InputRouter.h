/*
File: InputRouter.h
Purpose:
  Declares the allocation-free device snapshot, semantic action router, shared
  post-UI pointer value, and native pointer-presentation intent owner.

Mental model:
  A device owner captures hardware once into DeviceInputFrame. InputRouter then
  advances key/button memory exactly once and emits ordered semantic action
  events in the same order as the immutable binding table. Context predicates
  decide whether an observed edge is delivered; they never decide whether edge
  memory advances.

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
    root applies only changes reported by ConsumePointerPresentationChange.

Related:
  - InputController.h defines the existing action and context vocabulary.
  - InputController.Bindings.h publishes the current immutable binding table.
  - Agentic/Plans/TODO/runtime-shell-decomposition.md owns the extraction.
*/
#pragma once

#include "InputController.Bindings.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace SkullbonezCore
{
namespace Basics
{
struct RuntimeInputSnapshot;
struct RuntimeInteractionFrameInput;
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
    InputRouter();

    // Captures button edges, advances every binding's key memory, and handles
    // focus transitions. The output is reset here so subsequent RoutePhase
    // calls append one ordered frame result.
    void BeginFrame( const DeviceInputFrame& frame, RuntimeInputKeyBindingView bindings, InputActions& output );

    // Emits the selected phase in binding-table order. activeContexts contains
    // current mode/UI facts; AfterUi/Capture phase bits are supplied by the
    // router so callers cannot accidentally route a row through the wrong pass.
    void RoutePhase( RuntimeInputKeyBindingView bindings,
                     InputActionPhase phase,
                     RuntimeInputContextMask activeContexts,
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

    // Pointer presentation requests are reconciled here so UI/tools/camera do
    // not manipulate Win32 capture or cursor counters independently.
    void RequestNativeCapture();
    void ReleaseNativeCapture();
    void RequestCursorVisible( bool visible );
    void CancelPointerPresentation();
    // Returns true only when the hardware-facing state changed since the last
    // consume. Callers apply the returned value atomically at the frame edge.
    bool ConsumePointerPresentationChange( PointerPresentationState& state );
    bool NativeCaptureRequested() const;
    bool CursorVisibleRequested() const;
    // Presentation timing for repeated semantic taps belongs beside action
    // edges, not in a consumer's compatibility input context.
    bool IsQuickRepeat( RuntimeInputAction action, double nowSeconds, double intervalSeconds ) const;
    void RecordTap( RuntimeInputAction action, double nowSeconds );

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

    std::array<bool, ACTION_COUNT> m_actionDown = {};
    std::array<bool, ACTION_COUNT> m_actionDelivered = {};
    std::array<bool, ACTION_COUNT> m_actionSampledThisFrame = {};
    std::array<InputActionEdge, ACTION_COUNT> m_frameEdges = {};
    std::array<bool, PHASE_COUNT> m_phaseRoutedThisFrame = {};
    std::array<double, ACTION_COUNT> m_lastTapSeconds = {};
    DeviceInputFrame m_deviceFrame;
    UiInputHitSnapshot m_uiSnapshot;
    bool m_nativeCaptureRequested = false;
    bool m_committedNativeCapture = false;
    bool m_cursorVisibleRequested = true;
    bool m_committedCursorVisible = true;
    bool m_hasFrame = false;
    bool m_appFocused = false;
    bool m_frameFocused = false;
    bool m_leftWasDown = false;
    bool m_rightWasDown = false;
};
} // namespace Basics
} // namespace SkullbonezCore
