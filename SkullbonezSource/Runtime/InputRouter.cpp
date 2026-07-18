/*
File: InputRouter.cpp
Purpose:
  Implements fixed-capacity key/button edge routing from immutable device
  snapshots to ordered semantic action events.

Summary:
  BeginFrame samples every binding before any context is evaluated. RoutePhase
  later decides which already-observed edges are eligible for delivery. This
  separation prevents held keys from firing when UI or tool contexts change.
  Water-height keys are translated here into a world-unit command so simulation
  code never reinterprets device vocabulary.

Glossary:
  Delivered action: Press edge previously emitted to a consumer; held and
    release events are emitted only for that accepted press.
  Context exit release: Synthetic release emitted when an accepted action is
    still held but its owning context becomes inactive.
  Focus cancellation: Release events emitted when the application loses focus.
  UI (user interface): Interactive engine controls evaluated between routing
    phases.
  Fluid-surface command: Signed world-space velocity published in the immutable
    RuntimeInputSnapshot.

Invariants:
  - No operation in this file allocates or retains caller-owned storage.
  - One action contributes at most one event per routed phase and frame.
  - Context matching is all-of; every required bit must be active.
  - Refocus synchronizes held inputs without inventing press edges.

Related:
  - InputRouter.h defines the value records and caller contract.
  - SkullbonezTests/TestInputRouter.cpp covers edge and context behavior.
*/
#include "InputRouter.h"
#include "RuntimeInteractionController.h"

namespace SkullbonezCore
{
namespace Runtime
{
namespace
{
constexpr float FLUID_SURFACE_CONTROL_SPEED_METERS_PER_SECOND = 20.0f;

constexpr RuntimeInputContextMask ContextBit( RuntimeInputBindingContext context )
{
    return RuntimeInputContextBit( context );
}

Environment::FluidSurfaceAdjustment BuildFluidSurfaceAdjustment( const InputKeySnapshot& keys )
{
    const bool lower = keys.IsDown( VK_NEXT );
    const bool raise = keys.IsDown( VK_PRIOR );
    if ( lower == raise )
    {
        return {};
    }

    return Environment::FluidSurfaceAdjustment{ raise ? FLUID_SURFACE_CONTROL_SPEED_METERS_PER_SECOND
                                                      : -FLUID_SURFACE_CONTROL_SPEED_METERS_PER_SECOND };
}
} // namespace


InputKeySnapshot InputKeySnapshot::FromWords( const std::array<uint64_t, WORD_COUNT>& words )
{
    InputKeySnapshot snapshot;
    snapshot.m_words = words;
    return snapshot;
}


InputKeySnapshot InputKeySnapshot::FromDownKeys( const int* virtualKeys, std::size_t keyCount )
{
    InputKeySnapshot snapshot;
    if ( !virtualKeys )
    {
        return snapshot;
    }

    for ( std::size_t index = 0; index < keyCount; ++index )
    {
        const int virtualKey = virtualKeys[index];
        if ( virtualKey < 0 || virtualKey >= VIRTUAL_KEY_COUNT )
        {
            continue;
        }

        const std::size_t word = static_cast<std::size_t>( virtualKey ) / 64u;
        const uint64_t bit = uint64_t{ 1 } << ( static_cast<unsigned int>( virtualKey ) & 63u );
        snapshot.m_words[word] |= bit;
    }
    return snapshot;
}


bool InputKeySnapshot::IsDown( int virtualKey ) const
{
    if ( virtualKey < 0 || virtualKey >= VIRTUAL_KEY_COUNT )
    {
        return false;
    }

    const std::size_t word = static_cast<std::size_t>( virtualKey ) / 64u;
    const uint64_t bit = uint64_t{ 1 } << ( static_cast<unsigned int>( virtualKey ) & 63u );
    return ( m_words[word] & bit ) != 0u;
}


const std::array<uint64_t, InputKeySnapshot::WORD_COUNT>& InputKeySnapshot::Words() const
{
    return m_words;
}


void InputActions::Reset()
{
    m_count = 0;
    m_overflowed = false;
    mouse = {};
    focusLost = false;
    focusGained = false;
}


std::size_t InputActions::Count() const
{
    return m_count;
}


bool InputActions::Empty() const
{
    return m_count == 0;
}


bool InputActions::Overflowed() const
{
    return m_overflowed;
}


const InputActionEvent& InputActions::operator[]( std::size_t index ) const
{
    return m_events[index];
}


const InputActionEvent* InputActions::Data() const
{
    return m_events.data();
}


bool InputActions::TryAppend( const InputActionEvent& event )
{
    if ( m_count >= CAPACITY )
    {
        // Hazard: action loss is observable. The owner-facing integration must
        // treat Overflowed() as a fatal capacity invariant rather than growing.
        m_overflowed = true;
        return false;
    }

    m_events[m_count++] = event;
    return true;
}


InputRouter::InputRouter()
{
    Reset();
}


RuntimeInputContext& InputRouter::RuntimeContext()
{
    return m_runtimeContext;
}


const RuntimeInputContext& InputRouter::RuntimeContext() const
{
    return m_runtimeContext;
}


InputActions& InputRouter::Actions()
{
    return m_actions;
}


const InputActions& InputRouter::Actions() const
{
    return m_actions;
}


void InputRouter::BeginFrame( const DeviceInputFrame& frame, RuntimeInputKeyBindingView bindings, InputActions& output )
{
    m_deviceFrame = frame;
    m_uiSnapshot = {};
    m_runtimeSnapshot = {};
    m_runtimeSnapshot.appFocused = frame.appFocused;
    m_runtimeSnapshot.pointer.hasClientPosition = frame.hasClientPosition;
    m_runtimeSnapshot.pointer.clientX = frame.clientX;
    m_runtimeSnapshot.pointer.clientY = frame.clientY;
    m_runtimeSnapshot.pointer.leftDown = frame.leftDown;
    m_runtimeSnapshot.pointer.rightDown = frame.rightDown;
    m_runtimeSnapshot.pointer.controlDown = frame.keys.IsDown( VK_CONTROL );
    m_runtimeSnapshot.pointer.shiftDown = frame.keys.IsDown( VK_SHIFT );
    m_runtimeSnapshot.enterDown = frame.keys.IsDown( VK_RETURN );
    m_runtimeSnapshot.fluidSurfaceAdjustment = BuildFluidSurfaceAdjustment( frame.keys );
    output.Reset();
    m_actionSampledThisFrame.fill( false );
    m_frameEdges.fill( InputActionEdge::Released );
    m_phaseRoutedThisFrame.fill( false );

    const bool focusLost = m_hasFrame && m_appFocused && !frame.appFocused;
    const bool focusGained = m_hasFrame && !m_appFocused && frame.appFocused;
    m_frameFocused = frame.appFocused;

    if ( !frame.appFocused )
    {
        if ( focusLost )
        {
            output.focusLost = true;
            CaptureFocusLoss( bindings, output );
            CancelPointerPresentation();
        }
        else
        {
            m_actionDown.fill( false );
            m_actionDelivered.fill( false );
            m_leftWasDown = false;
            m_rightWasDown = false;
        }

        m_appFocused = false;
        m_hasFrame = true;
        return;
    }

    if ( focusGained )
    {
        // Invariant: input pressed while another application owned focus must
        // not arrive as a new command. Remember current levels and wait for a
        // physical release/repress cycle.
        output.focusGained = true;
        output.mouse.leftDown = frame.leftDown;
        output.mouse.rightDown = frame.rightDown;
        SynchronizeFocusedInputs( frame, bindings );
        m_leftWasDown = frame.leftDown;
        m_rightWasDown = frame.rightDown;
    }
    else
    {
        CapturePointerEdges( frame, output );
        SampleKeyboard( frame, bindings );
    }

    m_appFocused = true;
    m_hasFrame = true;
}


void InputRouter::RoutePhase( RuntimeInputKeyBindingView bindings,
                              InputActionPhase phase,
                              RuntimeInputContextMask activeContexts,
                              InputActions& output )
{
    if ( !m_frameFocused || !bindings.bindings || !IsPhaseValid( phase ) )
    {
        return;
    }

    const std::size_t phaseIndex = PhaseIndex( phase );
    if ( m_phaseRoutedThisFrame[phaseIndex] )
    {
        // Invariant: a phase contributes at most one pass to a frame's ordered
        // output, even if an integration bug asks to route it twice.
        return;
    }
    m_phaseRoutedThisFrame[phaseIndex] = true;

    const RuntimeInputContextMask effectiveContexts = EffectiveContexts( phase, activeContexts );
    std::array<bool, ACTION_COUNT> routedThisCall = {};
    for ( std::size_t bindingIndex = 0; bindingIndex < bindings.count; ++bindingIndex )
    {
        const RuntimeInputKeyBinding& binding = bindings.bindings[bindingIndex];
        if ( PhaseForBinding( binding ) != phase || !IsActionValid( binding.action ) )
        {
            continue;
        }

        const std::size_t actionIndex = ActionIndex( binding.action );
        if ( routedThisCall[actionIndex] )
        {
            // Invariant: the repository table owns one key per action. Ignoring a
            // duplicate fails closed without double-emitting an action event.
            continue;
        }
        routedThisCall[actionIndex] = true;

        const InputActionEdge observedEdge = m_frameEdges[actionIndex];
        const bool contextActive = ContextsSatisfied( binding.contexts, effectiveContexts );
        if ( m_actionDelivered[actionIndex] && ( observedEdge == InputActionEdge::Released || !contextActive ) )
        {
            output.TryAppend( InputActionEvent{ binding.action,
                                                RuntimeInputActionSource::Keyboard,
                                                phase,
                                                InputActionEdge::Released,
                                                binding.virtualKey } );
            m_actionDelivered[actionIndex] = false;
            continue;
        }

        if ( !contextActive )
        {
            continue;
        }

        if ( observedEdge == InputActionEdge::Pressed )
        {
            if ( output.TryAppend( InputActionEvent{ binding.action,
                                                     RuntimeInputActionSource::Keyboard,
                                                     phase,
                                                     InputActionEdge::Pressed,
                                                     binding.virtualKey } ) )
            {
                m_actionDelivered[actionIndex] = true;
            }
        }
        else if ( observedEdge == InputActionEdge::Held && m_actionDelivered[actionIndex] )
        {
            output.TryAppend( InputActionEvent{ binding.action,
                                                RuntimeInputActionSource::Keyboard,
                                                phase,
                                                InputActionEdge::Held,
                                                binding.virtualKey } );
        }
    }
}


void InputRouter::Reset()
{
    m_actionDown.fill( false );
    m_actionDelivered.fill( false );
    m_actionSampledThisFrame.fill( false );
    m_frameEdges.fill( InputActionEdge::Released );
    m_phaseRoutedThisFrame.fill( false );
    m_lastTapSeconds.fill( -1000.0 );
    m_deviceFrame = {};
    m_uiSnapshot = {};
    m_runtimeSnapshot = {};
    m_nativeCaptureRequested = false;
    m_committedNativeCapture = false;
    m_cursorVisibleRequested = true;
    m_committedCursorVisible = true;
    m_pointerPresentationCommitted = false;
    m_hasFrame = false;
    m_appFocused = false;
    m_frameFocused = false;
    m_leftWasDown = false;
    m_rightWasDown = false;
}


const DeviceInputFrame& InputRouter::DeviceFrame() const
{
    return m_deviceFrame;
}


void InputRouter::PublishUiSnapshot( const UiInputHitSnapshot& snapshot )
{
    m_uiSnapshot = snapshot;
}


const UiInputHitSnapshot& InputRouter::UiSnapshot() const
{
    return m_uiSnapshot;
}


RuntimeInputSnapshot InputRouter::BuildRuntimeSnapshot( const RuntimeInteractionFrameInput& frameInput,
                                                        bool suppressWorldAction ) const
{
    RuntimeInputSnapshot snapshot;
    snapshot.appFocused = m_deviceFrame.appFocused;
    snapshot.uiBlocksKeyboard = m_uiSnapshot.blocksKeyboard;
    snapshot.uiBlocksMouse = m_uiSnapshot.blocksCameraMouse;
    snapshot.enterDown = m_deviceFrame.keys.IsDown( VK_RETURN );
    snapshot.fluidSurfaceAdjustment = BuildFluidSurfaceAdjustment( m_deviceFrame.keys );
    if ( m_deviceFrame.hasClientPosition )
    {
        snapshot.pointer.hasClientPosition = true;
        snapshot.pointer.clientX = m_deviceFrame.clientX;
        snapshot.pointer.clientY = m_deviceFrame.clientY;
    }
    snapshot.pointer.leftDown = m_uiSnapshot.mouse.leftDown;
    snapshot.pointer.leftPressed = m_uiSnapshot.mouse.leftPressed;
    snapshot.pointer.leftReleased = m_uiSnapshot.mouse.leftReleased;
    snapshot.pointer.rightDown = m_uiSnapshot.mouse.rightDown;
    snapshot.pointer.rightPressed = m_uiSnapshot.mouse.rightPressed;
    snapshot.pointer.rightReleased = m_uiSnapshot.mouse.rightReleased;
    snapshot.pointer.controlDown = m_deviceFrame.keys.IsDown( VK_CONTROL );
    snapshot.pointer.shiftDown = m_deviceFrame.keys.IsDown( VK_SHIFT );
    snapshot.pointer.uiWantsNativeMouseCursor = m_uiSnapshot.wantsNativeCursor;
    snapshot.pointer.uiBlocksCameraMouse = m_uiSnapshot.blocksCameraMouse;
    snapshot.pointer.suppressWorldAction = suppressWorldAction;
    if ( m_uiSnapshot.mouse.leftPressed || m_uiSnapshot.mouse.leftReleased || m_uiSnapshot.mouse.leftDown )
    {
        snapshot.pointer.button = RuntimePointerButton::Left;
    }
    else if ( m_uiSnapshot.mouse.rightPressed || m_uiSnapshot.mouse.rightReleased || m_uiSnapshot.mouse.rightDown )
    {
        snapshot.pointer.button = RuntimePointerButton::Right;
    }
    snapshot.frameInput = frameInput;
    return snapshot;
}


const RuntimeInputSnapshot& InputRouter::PublishRuntimeSnapshot( const RuntimeInteractionFrameInput& frameInput,
                                                                 bool suppressWorldAction )
{
    m_runtimeSnapshot = BuildRuntimeSnapshot( frameInput, suppressWorldAction );
    return m_runtimeSnapshot;
}


const RuntimeInputSnapshot& InputRouter::RuntimeSnapshot() const
{
    return m_runtimeSnapshot;
}


PointerPresentationPolicy InputRouter::EvaluatePointerPresentation( const PointerPresentationPolicyInput& input ) const
{
    PointerPresentationPolicy policy;
    if ( !m_runtimeSnapshot.appFocused || m_uiSnapshot.blocksCameraMouse )
    {
        return policy;
    }
    if ( input.editorModeEnabled )
    {
        policy.mouseLookOwnsCursor = input.editorViewportLookActive || m_runtimeSnapshot.pointer.rightDown;
    }
    else if ( input.replayInspectionActive )
    {
        policy.mouseLookOwnsCursor = input.replayInspectionLookActive || m_runtimeSnapshot.pointer.rightDown;
    }
    else
    {
        policy.mouseLookOwnsCursor = m_runtimeSnapshot.pointer.rightDown;
    }
    policy.hideNativeCursor =
        policy.mouseLookOwnsCursor || ( input.editorModeEnabled && input.editorPlacementModeEnabled &&
                                        input.editorPlacementPreviewVisible && !m_uiSnapshot.wantsNativeCursor );
    return policy;
}


void InputRouter::ApplyPointerPresentation( const PointerPresentationPolicy& policy )
{
    RequestCursorVisible( !policy.hideNativeCursor );
}


bool InputRouter::ReleasePointerToUi( const PointerPresentationPolicy& policy )
{
    // Invariant: UI release cannot steal the pointer from an active camera-look
    // gesture. A true result tells the camera owner to clear accumulated deltas.
    if ( policy.mouseLookOwnsCursor )
    {
        return false;
    }
    ReleaseNativeCapture();
    return true;
}


void InputRouter::RequestNativeCapture()
{
    m_nativeCaptureRequested = true;
}


void InputRouter::ReleaseNativeCapture()
{
    m_nativeCaptureRequested = false;
}


void InputRouter::RequestCursorVisible( bool visible )
{
    m_cursorVisibleRequested = visible;
}


void InputRouter::CancelPointerPresentation()
{
    m_nativeCaptureRequested = false;
    m_cursorVisibleRequested = true;
}


bool InputRouter::ConsumePointerPresentationChange( PointerPresentationState& state )
{
    state.nativeCapture = m_nativeCaptureRequested;
    state.cursorVisible = m_cursorVisibleRequested;
    // Hazard: Win32's process-local cursor latch predates InputRouter and may
    // start hidden. Publish once even when desired values equal member defaults;
    // otherwise the composition root never normalizes the native cursor.
    const bool changed = !m_pointerPresentationCommitted || m_committedNativeCapture != m_nativeCaptureRequested ||
                         m_committedCursorVisible != m_cursorVisibleRequested;
    m_committedNativeCapture = m_nativeCaptureRequested;
    m_committedCursorVisible = m_cursorVisibleRequested;
    m_pointerPresentationCommitted = true;
    return changed;
}


bool InputRouter::NativeCaptureRequested() const
{
    return m_nativeCaptureRequested;
}


bool InputRouter::CursorVisibleRequested() const
{
    return m_cursorVisibleRequested;
}


bool InputRouter::IsQuickRepeat( RuntimeInputAction action, double nowSeconds, double intervalSeconds ) const
{
    if ( !IsActionValid( action ) )
    {
        return false;
    }
    // Preserve the existing UI clock contract: the caller supplies one
    // monotonic timeline and owns the repeat interval policy.
    return nowSeconds - m_lastTapSeconds[ActionIndex( action )] <= intervalSeconds;
}


void InputRouter::RecordTap( RuntimeInputAction action, double nowSeconds )
{
    if ( IsActionValid( action ) )
    {
        m_lastTapSeconds[ActionIndex( action )] = nowSeconds;
    }
}


bool InputRouter::AppFocused() const
{
    return m_appFocused;
}


bool InputRouter::ContextsSatisfied( RuntimeInputContextMask requiredContexts, RuntimeInputContextMask activeContexts )
{
    return ( requiredContexts & activeContexts ) == requiredContexts;
}


InputActionPhase InputRouter::PhaseForBinding( const RuntimeInputKeyBinding& binding )
{
    if ( ( binding.contexts & ContextBit( RuntimeInputBindingContext::Capture ) ) != 0u )
    {
        return InputActionPhase::Capture;
    }
    if ( ( binding.contexts & ContextBit( RuntimeInputBindingContext::AfterUIUpdate ) ) != 0u )
    {
        return InputActionPhase::AfterUi;
    }
    return InputActionPhase::PreUi;
}


bool InputRouter::IsActionValid( RuntimeInputAction action )
{
    const int value = static_cast<int>( action );
    return value > static_cast<int>( RuntimeInputAction::None ) &&
           value < static_cast<int>( RuntimeInputAction::Count );
}


bool InputRouter::IsPhaseValid( InputActionPhase phase )
{
    const uint8_t value = static_cast<uint8_t>( phase );
    return value <= static_cast<uint8_t>( InputActionPhase::Capture );
}


std::size_t InputRouter::ActionIndex( RuntimeInputAction action )
{
    return static_cast<std::size_t>( action );
}


std::size_t InputRouter::PhaseIndex( InputActionPhase phase )
{
    return static_cast<std::size_t>( phase );
}


RuntimeInputContextMask InputRouter::EffectiveContexts( InputActionPhase phase, RuntimeInputContextMask activeContexts )
{
    if ( phase == InputActionPhase::AfterUi )
    {
        activeContexts |= ContextBit( RuntimeInputBindingContext::AfterUIUpdate );
    }
    else if ( phase == InputActionPhase::Capture )
    {
        activeContexts |= ContextBit( RuntimeInputBindingContext::Capture );
    }
    return activeContexts;
}


void InputRouter::CapturePointerEdges( const DeviceInputFrame& frame, InputActions& output )
{
    output.mouse.leftDown = frame.leftDown;
    output.mouse.leftPressed = frame.leftDown && !m_leftWasDown;
    output.mouse.leftReleased = !frame.leftDown && m_leftWasDown;
    output.mouse.rightDown = frame.rightDown;
    output.mouse.rightPressed = frame.rightDown && !m_rightWasDown;
    output.mouse.rightReleased = !frame.rightDown && m_rightWasDown;
    m_leftWasDown = frame.leftDown;
    m_rightWasDown = frame.rightDown;
}


void InputRouter::CaptureFocusLoss( RuntimeInputKeyBindingView bindings, InputActions& output )
{
    output.mouse.leftReleased = m_leftWasDown;
    output.mouse.rightReleased = m_rightWasDown;

    std::array<bool, ACTION_COUNT> released = {};
    if ( bindings.bindings )
    {
        for ( std::size_t bindingIndex = 0; bindingIndex < bindings.count; ++bindingIndex )
        {
            const RuntimeInputKeyBinding& binding = bindings.bindings[bindingIndex];
            if ( !IsActionValid( binding.action ) )
            {
                continue;
            }

            const std::size_t actionIndex = ActionIndex( binding.action );
            if ( released[actionIndex] || !m_actionDelivered[actionIndex] )
            {
                continue;
            }
            released[actionIndex] = true;
            output.TryAppend( InputActionEvent{ binding.action,
                                                RuntimeInputActionSource::FocusLost,
                                                PhaseForBinding( binding ),
                                                InputActionEdge::Released,
                                                binding.virtualKey } );
        }
    }

    m_actionDown.fill( false );
    m_actionDelivered.fill( false );
    m_leftWasDown = false;
    m_rightWasDown = false;
}


void InputRouter::SynchronizeFocusedInputs( const DeviceInputFrame& frame, RuntimeInputKeyBindingView bindings )
{
    m_actionDown.fill( false );
    m_actionDelivered.fill( false );
    m_actionSampledThisFrame.fill( false );
    m_frameEdges.fill( InputActionEdge::Released );

    if ( !bindings.bindings )
    {
        return;
    }
    for ( std::size_t bindingIndex = 0; bindingIndex < bindings.count; ++bindingIndex )
    {
        const RuntimeInputKeyBinding& binding = bindings.bindings[bindingIndex];
        if ( !IsActionValid( binding.action ) )
        {
            continue;
        }

        const std::size_t actionIndex = ActionIndex( binding.action );
        if ( m_actionSampledThisFrame[actionIndex] )
        {
            continue;
        }
        m_actionSampledThisFrame[actionIndex] = true;
        m_actionDown[actionIndex] = frame.keys.IsDown( binding.virtualKey );
    }
}


void InputRouter::SampleKeyboard( const DeviceInputFrame& frame, RuntimeInputKeyBindingView bindings )
{
    if ( !bindings.bindings )
    {
        return;
    }

    for ( std::size_t bindingIndex = 0; bindingIndex < bindings.count; ++bindingIndex )
    {
        const RuntimeInputKeyBinding& binding = bindings.bindings[bindingIndex];
        if ( !IsActionValid( binding.action ) )
        {
            continue;
        }

        const std::size_t actionIndex = ActionIndex( binding.action );
        if ( m_actionSampledThisFrame[actionIndex] )
        {
            continue;
        }
        m_actionSampledThisFrame[actionIndex] = true;

        const bool wasDown = m_actionDown[actionIndex];
        const bool isDown = frame.keys.IsDown( binding.virtualKey );
        m_actionDown[actionIndex] = isDown;
        if ( isDown && !wasDown )
        {
            m_frameEdges[actionIndex] = InputActionEdge::Pressed;
        }
        else if ( isDown )
        {
            m_frameEdges[actionIndex] = InputActionEdge::Held;
        }
        else
        {
            m_frameEdges[actionIndex] = InputActionEdge::Released;
        }
    }
}
} // namespace Runtime
} // namespace SkullbonezCore
