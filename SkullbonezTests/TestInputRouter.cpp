/*
File: TestInputRouter.cpp
Purpose:
  Verifies allocation-free input snapshots, action edges, binding predicates,
  phase order, and focus cancellation without constructing Run or polling Win32.

Mental model:
  Each test supplies immutable device frames and a tiny static binding table.
  BeginFrame advances all physical edge memory; RoutePhase then proves which
  semantic events are eligible under current context facts.

Glossary:
  UI (user interface): Interactive engine controls evaluated between routing
    phases.
  Win32: Windows desktop API that defines the virtual-key constants in tests.
  Held edge: Event emitted after an accepted press remains physically down.
  Context activation: Mode/UI fact mask that permits a binding to emit.
  Ghost press: False press caused by activating a context while its key was
    already held.
  Focus resynchronization: Refocus sample that remembers held input without
    treating it as newly pressed.

Invariants:
  - Tests use no hardware, window, renderer, physics, or dynamic action storage.
  - Output order follows binding order inside the caller-selected phase order.
  - A key observed in an inactive/blocked context cannot fire until released and
    pressed again.

Related:
  - SkullbonezSource/Runtime/InputRouter.h
  - SkullbonezTests/TestRuntimeInputBindings.cpp
  - Agentic/Plans/TODO/runtime-shell-decomposition.md
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/InputRouter.h"
#include "../SkullbonezSource/Runtime/RuntimeInteractionController.h"

#include <initializer_list>

using namespace SkullbonezCore::Basics;

namespace
{
template <std::size_t N> RuntimeInputKeyBindingView BindingView( const RuntimeInputKeyBinding ( &bindings )[N] )
{
    return RuntimeInputKeyBindingView{ bindings, N };
}


RuntimeInputContextMask Context( RuntimeInputBindingContext context )
{
    return RuntimeInputContextBit( context );
}


DeviceInputFrame FocusedFrame( std::initializer_list<int> downKeys, bool leftDown = false, bool rightDown = false )
{
    DeviceInputFrame frame;
    frame.keys = InputKeySnapshot::FromDownKeys( downKeys.begin(), downKeys.size() );
    frame.appFocused = true;
    frame.leftDown = leftDown;
    frame.rightDown = rightDown;
    return frame;
}


DeviceInputFrame UnfocusedFrame()
{
    DeviceInputFrame frame;
    frame.appFocused = false;
    return frame;
}
} // namespace


TEST_CASE( "Input router: key snapshot is bounded and ignores invalid virtual keys" )
{
    const int downKeys[] = { -1, 0, 'A', 255, 256 };
    const InputKeySnapshot snapshot = InputKeySnapshot::FromDownKeys( downKeys, 5 );

    CHECK( snapshot.IsDown( 0 ) );
    CHECK( snapshot.IsDown( 'A' ) );
    CHECK( snapshot.IsDown( 255 ) );
    CHECK_FALSE( snapshot.IsDown( -1 ) );
    CHECK_FALSE( snapshot.IsDown( 256 ) );
    CHECK( snapshot.Words().size() == InputKeySnapshot::WORD_COUNT );
    static_assert( InputActions::CAPACITY == static_cast<std::size_t>( RuntimeInputAction::Count ) );
}


TEST_CASE( "Input router: press hold and release preserve binding order" )
{
    const RuntimeInputKeyBinding bindings[] = {
        { 'A', RuntimeInputAction::ToggleEditor, Context( RuntimeInputBindingContext::KeyboardUnblocked ) },
        { 'B', RuntimeInputAction::CycleCameraMode, Context( RuntimeInputBindingContext::KeyboardUnblocked ) },
    };
    const RuntimeInputKeyBindingView view = BindingView( bindings );
    const RuntimeInputContextMask active = Context( RuntimeInputBindingContext::KeyboardUnblocked );
    InputRouter router;
    InputActions output;

    router.BeginFrame( FocusedFrame( { 'B', 'A' } ), view, output );
    router.RoutePhase( view, InputActionPhase::PreUi, active, output );
    REQUIRE( output.Count() == 2 );
    CHECK( output[0].action == RuntimeInputAction::ToggleEditor );
    CHECK( output[1].action == RuntimeInputAction::CycleCameraMode );
    CHECK( output[0].edge == InputActionEdge::Pressed );
    CHECK( output[1].edge == InputActionEdge::Pressed );

    router.BeginFrame( FocusedFrame( { 'A', 'B' } ), view, output );
    router.RoutePhase( view, InputActionPhase::PreUi, active, output );
    REQUIRE( output.Count() == 2 );
    CHECK( output[0].edge == InputActionEdge::Held );
    CHECK( output[1].edge == InputActionEdge::Held );

    router.BeginFrame( FocusedFrame( {} ), view, output );
    router.RoutePhase( view, InputActionPhase::PreUi, active, output );
    REQUIRE( output.Count() == 2 );
    CHECK( output[0].edge == InputActionEdge::Released );
    CHECK( output[1].edge == InputActionEdge::Released );
    CHECK_FALSE( output.Overflowed() );
}


TEST_CASE( "Input router: inactive context advances memory and prevents ghost presses" )
{
    const RuntimeInputKeyBinding bindings[] = {
        { 'M',
          RuntimeInputAction::CycleLauncherFireMode,
          Context( RuntimeInputBindingContext::KeyboardUnblocked ) | RuntimeInputBindingContext::Launcher },
    };
    const RuntimeInputKeyBindingView view = BindingView( bindings );
    const RuntimeInputContextMask keyboard = Context( RuntimeInputBindingContext::KeyboardUnblocked );
    const RuntimeInputContextMask launcher = keyboard | RuntimeInputBindingContext::Launcher;
    InputRouter router;
    InputActions output;

    router.BeginFrame( FocusedFrame( { 'M' } ), view, output );
    router.RoutePhase( view, InputActionPhase::PreUi, keyboard, output );
    CHECK( output.Empty() );

    router.BeginFrame( FocusedFrame( { 'M' } ), view, output );
    router.RoutePhase( view, InputActionPhase::PreUi, launcher, output );
    CHECK( output.Empty() );

    router.BeginFrame( FocusedFrame( {} ), view, output );
    router.RoutePhase( view, InputActionPhase::PreUi, launcher, output );
    CHECK( output.Empty() );

    router.BeginFrame( FocusedFrame( { 'M' } ), view, output );
    router.RoutePhase( view, InputActionPhase::PreUi, launcher, output );
    REQUIRE( output.Count() == 1 );
    CHECK( output[0].edge == InputActionEdge::Pressed );
}


TEST_CASE( "Input router: UI refusal requires release and repress" )
{
    const RuntimeInputKeyBinding bindings[] = {
        { VK_ESCAPE,
          RuntimeInputAction::DismissOrExitUI,
          Context( RuntimeInputBindingContext::AfterUIUpdate ) | RuntimeInputBindingContext::UINotInteracted },
    };
    const RuntimeInputKeyBindingView view = BindingView( bindings );
    const RuntimeInputContextMask accepted = Context( RuntimeInputBindingContext::UINotInteracted );
    InputRouter router;
    InputActions output;

    router.BeginFrame( FocusedFrame( { VK_ESCAPE } ), view, output );
    router.RoutePhase( view, InputActionPhase::AfterUi, 0u, output );
    CHECK( output.Empty() );

    router.BeginFrame( FocusedFrame( { VK_ESCAPE } ), view, output );
    router.RoutePhase( view, InputActionPhase::AfterUi, accepted, output );
    CHECK( output.Empty() );

    router.BeginFrame( FocusedFrame( {} ), view, output );
    router.RoutePhase( view, InputActionPhase::AfterUi, accepted, output );
    CHECK( output.Empty() );

    router.BeginFrame( FocusedFrame( { VK_ESCAPE } ), view, output );
    router.RoutePhase( view, InputActionPhase::AfterUi, accepted, output );
    REQUIRE( output.Count() == 1 );
    CHECK( output[0].action == RuntimeInputAction::DismissOrExitUI );
    CHECK( output[0].edge == InputActionEdge::Pressed );
}


TEST_CASE( "Input router: simultaneous actions cannot activate a sibling context mid-phase" )
{
    const RuntimeInputKeyBinding bindings[] = {
        { 'N', RuntimeInputAction::ToggleLauncher, Context( RuntimeInputBindingContext::KeyboardUnblocked ) },
        { 'M',
          RuntimeInputAction::CycleLauncherFireMode,
          Context( RuntimeInputBindingContext::KeyboardUnblocked ) | RuntimeInputBindingContext::Launcher },
    };
    const RuntimeInputKeyBindingView view = BindingView( bindings );
    const RuntimeInputContextMask preLauncher = Context( RuntimeInputBindingContext::KeyboardUnblocked );
    InputRouter router;
    InputActions output;

    router.BeginFrame( FocusedFrame( { 'N', 'M' } ), view, output );
    router.RoutePhase( view, InputActionPhase::PreUi, preLauncher, output );

    REQUIRE( output.Count() == 1 );
    CHECK( output[0].action == RuntimeInputAction::ToggleLauncher );
    CHECK( output[0].edge == InputActionEdge::Pressed );
}


TEST_CASE( "Input router: quick-repeat timing is action-owned and resettable" )
{
    InputRouter router;

    CHECK_FALSE( router.IsQuickRepeat( RuntimeInputAction::DismissOrExitUI, 10.0, 0.32 ) );
    router.RecordTap( RuntimeInputAction::DismissOrExitUI, 10.0 );
    CHECK( router.IsQuickRepeat( RuntimeInputAction::DismissOrExitUI, 10.31, 0.32 ) );
    CHECK_FALSE( router.IsQuickRepeat( RuntimeInputAction::DismissOrExitUI, 10.33, 0.32 ) );
    CHECK_FALSE( router.IsQuickRepeat( RuntimeInputAction::ToggleEditor, 10.1, 0.32 ) );

    router.Reset();
    CHECK_FALSE( router.IsQuickRepeat( RuntimeInputAction::DismissOrExitUI, 10.1, 0.32 ) );
}


TEST_CASE( "Input router: a skipped capture phase cannot replay a stale press" )
{
    const RuntimeInputKeyBinding bindings[] = {
        { VK_F3, RuntimeInputAction::SaveScreenshot, Context( RuntimeInputBindingContext::Capture ) },
    };
    const RuntimeInputKeyBindingView view = BindingView( bindings );
    InputRouter router;
    InputActions output;

    router.BeginFrame( FocusedFrame( { VK_F3 } ), view, output );
    CHECK( output.Empty() );

    router.BeginFrame( FocusedFrame( { VK_F3 } ), view, output );
    router.RoutePhase( view, InputActionPhase::Capture, 0u, output );
    CHECK( output.Empty() );

    router.BeginFrame( FocusedFrame( {} ), view, output );
    router.RoutePhase( view, InputActionPhase::Capture, 0u, output );
    CHECK( output.Empty() );

    router.BeginFrame( FocusedFrame( { VK_F3 } ), view, output );
    router.RoutePhase( view, InputActionPhase::Capture, 0u, output );
    REQUIRE( output.Count() == 1 );
    CHECK( output[0].edge == InputActionEdge::Pressed );
}


TEST_CASE( "Input router: post-UI pointer snapshot is published once as a value" )
{
    InputRouter router;
    InputActions output;
    DeviceInputFrame frame = FocusedFrame( {} );
    frame.clientX = 320;
    frame.clientY = 180;
    frame.hasClientPosition = true;
    frame.leftDown = true;
    frame.wheelDelta = WHEEL_DELTA;

    router.BeginFrame( frame, {}, output );
    UiInputHitSnapshot hit;
    hit.mouse = output.mouse;
    hit.clientX = frame.clientX;
    hit.clientY = frame.clientY;
    hit.hasClientPosition = frame.hasClientPosition;
    hit.unhandledWheelDelta = frame.wheelDelta;
    hit.userInteracted = true;
    hit.blocksCameraMouse = true;
    router.PublishUiSnapshot( hit );

    frame.clientX = 999;
    hit.clientX = 999;
    CHECK( router.DeviceFrame().clientX == 320 );
    CHECK( router.UiSnapshot().clientX == 320 );
    CHECK( router.UiSnapshot().mouse.leftPressed );
    CHECK( router.UiSnapshot().unhandledWheelDelta == WHEEL_DELTA );
    CHECK( router.UiSnapshot().userInteracted );
    CHECK( router.UiSnapshot().blocksCameraMouse );
}


TEST_CASE( "Input router: focus loss cancels native capture and restores cursor intent" )
{
    InputRouter router;
    InputActions output;
    PointerPresentationState presentation;

    router.BeginFrame( FocusedFrame( {} ), {}, output );
    router.RequestNativeCapture();
    router.RequestCursorVisible( false );
    REQUIRE( router.ConsumePointerPresentationChange( presentation ) );
    CHECK( presentation.nativeCapture );
    CHECK_FALSE( presentation.cursorVisible );
    CHECK_FALSE( router.ConsumePointerPresentationChange( presentation ) );

    DeviceInputFrame unfocused;
    unfocused.appFocused = false;
    router.BeginFrame( unfocused, {}, output );
    REQUIRE( output.focusLost );
    REQUIRE( router.ConsumePointerPresentationChange( presentation ) );
    CHECK_FALSE( presentation.nativeCapture );
    CHECK( presentation.cursorVisible );
}


TEST_CASE( "Input router: context exit releases an accepted held action" )
{
    const RuntimeInputKeyBinding bindings[] = {
        { 'M',
          RuntimeInputAction::CycleLauncherFireMode,
          Context( RuntimeInputBindingContext::KeyboardUnblocked ) | RuntimeInputBindingContext::Launcher },
    };
    const RuntimeInputKeyBindingView view = BindingView( bindings );
    const RuntimeInputContextMask keyboard = Context( RuntimeInputBindingContext::KeyboardUnblocked );
    const RuntimeInputContextMask launcher = keyboard | RuntimeInputBindingContext::Launcher;
    InputRouter router;
    InputActions output;

    router.BeginFrame( FocusedFrame( { 'M' } ), view, output );
    router.RoutePhase( view, InputActionPhase::PreUi, launcher, output );
    REQUIRE( output.Count() == 1 );
    CHECK( output[0].edge == InputActionEdge::Pressed );

    router.BeginFrame( FocusedFrame( { 'M' } ), view, output );
    router.RoutePhase( view, InputActionPhase::PreUi, keyboard, output );
    REQUIRE( output.Count() == 1 );
    CHECK( output[0].edge == InputActionEdge::Released );

    router.BeginFrame( FocusedFrame( { 'M' } ), view, output );
    router.RoutePhase( view, InputActionPhase::PreUi, launcher, output );
    CHECK( output.Empty() );
}


TEST_CASE( "Input router: phases append in explicit caller order" )
{
    const RuntimeInputKeyBinding bindings[] = {
        { 'A', RuntimeInputAction::ToggleEditor, Context( RuntimeInputBindingContext::KeyboardUnblocked ) },
        { VK_ESCAPE,
          RuntimeInputAction::DismissOrExitUI,
          Context( RuntimeInputBindingContext::AfterUIUpdate ) | RuntimeInputBindingContext::UINotInteracted },
        { VK_F3, RuntimeInputAction::SaveScreenshot, Context( RuntimeInputBindingContext::Capture ) },
    };
    const RuntimeInputKeyBindingView view = BindingView( bindings );
    InputRouter router;
    InputActions output;

    router.BeginFrame( FocusedFrame( { 'A', VK_ESCAPE, VK_F3 } ), view, output );
    router.RoutePhase( view,
                       InputActionPhase::PreUi,
                       Context( RuntimeInputBindingContext::KeyboardUnblocked ),
                       output );
    router.RoutePhase( view,
                       InputActionPhase::AfterUi,
                       Context( RuntimeInputBindingContext::UINotInteracted ),
                       output );
    router.RoutePhase( view, InputActionPhase::Capture, 0u, output );
    router.RoutePhase( view, InputActionPhase::Capture, 0u, output );

    REQUIRE( output.Count() == 3 );
    CHECK( output[0].action == RuntimeInputAction::ToggleEditor );
    CHECK( output[0].phase == InputActionPhase::PreUi );
    CHECK( output[1].action == RuntimeInputAction::DismissOrExitUI );
    CHECK( output[1].phase == InputActionPhase::AfterUi );
    CHECK( output[2].action == RuntimeInputAction::SaveScreenshot );
    CHECK( output[2].phase == InputActionPhase::Capture );
}


TEST_CASE( "Input router: malformed action values fail closed" )
{
    const RuntimeInputKeyBinding bindings[] = {
        { 'A', static_cast<RuntimeInputAction>( -1 ), 0u },
        { 'B', static_cast<RuntimeInputAction>( 9999 ), 0u },
        { 'C', RuntimeInputAction::ToggleEditor, 0u },
    };
    const RuntimeInputKeyBindingView view = BindingView( bindings );
    InputRouter router;
    InputActions output;

    router.BeginFrame( FocusedFrame( { 'A', 'B', 'C' } ), view, output );
    router.RoutePhase( view, InputActionPhase::PreUi, 0u, output );

    REQUIRE( output.Count() == 1 );
    CHECK( output[0].action == RuntimeInputAction::ToggleEditor );
}


TEST_CASE( "Input router: focus loss cancels once and refocus resynchronizes held input" )
{
    const RuntimeInputKeyBinding bindings[] = {
        { 'A', RuntimeInputAction::ToggleEditor, Context( RuntimeInputBindingContext::KeyboardUnblocked ) },
    };
    const RuntimeInputKeyBindingView view = BindingView( bindings );
    const RuntimeInputContextMask active = Context( RuntimeInputBindingContext::KeyboardUnblocked );
    InputRouter router;
    InputActions output;

    router.BeginFrame( FocusedFrame( { 'A' }, true, true ), view, output );
    router.RoutePhase( view, InputActionPhase::PreUi, active, output );
    REQUIRE( output.Count() == 1 );
    CHECK( output[0].edge == InputActionEdge::Pressed );
    CHECK( output.mouse.leftPressed );
    CHECK( output.mouse.rightPressed );

    router.BeginFrame( UnfocusedFrame(), view, output );
    REQUIRE( output.Count() == 1 );
    CHECK( output.focusLost );
    CHECK( output[0].edge == InputActionEdge::Released );
    CHECK( output[0].source == RuntimeInputActionSource::FocusLost );
    CHECK( output.mouse.leftReleased );
    CHECK( output.mouse.rightReleased );

    router.BeginFrame( UnfocusedFrame(), view, output );
    CHECK_FALSE( output.focusLost );
    CHECK( output.Empty() );

    router.BeginFrame( FocusedFrame( { 'A' }, true, true ), view, output );
    router.RoutePhase( view, InputActionPhase::PreUi, active, output );
    CHECK( output.focusGained );
    CHECK( output.Empty() );
    CHECK_FALSE( output.mouse.leftPressed );
    CHECK_FALSE( output.mouse.rightPressed );

    router.BeginFrame( FocusedFrame( {} ), view, output );
    router.RoutePhase( view, InputActionPhase::PreUi, active, output );
    CHECK( output.Empty() );
    CHECK( output.mouse.leftReleased );
    CHECK( output.mouse.rightReleased );

    router.BeginFrame( FocusedFrame( { 'A' } ), view, output );
    router.RoutePhase( view, InputActionPhase::PreUi, active, output );
    REQUIRE( output.Count() == 1 );
    CHECK( output[0].edge == InputActionEdge::Pressed );
}


TEST_CASE( "Input router: context predicate requires every binding bit" )
{
    const RuntimeInputContextMask required = Context( RuntimeInputBindingContext::KeyboardUnblocked ) |
                                             RuntimeInputBindingContext::Launcher |
                                             RuntimeInputBindingContext::DebugOnly;
    const RuntimeInputContextMask partial =
        Context( RuntimeInputBindingContext::KeyboardUnblocked ) | RuntimeInputBindingContext::Launcher;

    CHECK( InputRouter::ContextsSatisfied( 0u, 0u ) );
    CHECK_FALSE( InputRouter::ContextsSatisfied( required, partial ) );
    CHECK( InputRouter::ContextsSatisfied( required, partial | RuntimeInputBindingContext::DebugOnly ) );

    const RuntimeInputKeyBinding afterBinding = { VK_ESCAPE,
                                                  RuntimeInputAction::DismissOrExitUI,
                                                  Context( RuntimeInputBindingContext::AfterUIUpdate ) };
    const RuntimeInputKeyBinding captureBinding = { VK_F3,
                                                    RuntimeInputAction::SaveScreenshot,
                                                    Context( RuntimeInputBindingContext::Capture ) };
    CHECK( InputRouter::PhaseForBinding( afterBinding ) == InputActionPhase::AfterUi );
    CHECK( InputRouter::PhaseForBinding( captureBinding ) == InputActionPhase::Capture );
}


TEST_CASE( "Input router: runtime snapshot joins one device and UI pointer frame" )
{
    InputRouter router;
    InputActions output;
    DeviceInputFrame device = FocusedFrame( { VK_CONTROL, VK_SHIFT }, true, true );
    device.clientX = 321;
    device.clientY = 654;
    device.hasClientPosition = true;
    router.BeginFrame( device, RuntimeInputKeyBindingView{}, output );

    UiInputHitSnapshot ui;
    ui.mouse = output.mouse;
    ui.blocksKeyboard = true;
    ui.blocksCameraMouse = true;
    ui.wantsNativeCursor = true;
    router.PublishUiSnapshot( ui );

    RuntimeInteractionFrameInput frameInput;
    frameInput.scenePhysicsEnabled = true;
    frameInput.sceneTimeScale = 0.5f;
    const RuntimeInputSnapshot snapshot = router.BuildRuntimeSnapshot( frameInput, true );
    CHECK( snapshot.appFocused );
    CHECK( snapshot.uiBlocksKeyboard );
    CHECK( snapshot.uiBlocksMouse );
    CHECK( snapshot.pointer.clientX == 321 );
    CHECK( snapshot.pointer.clientY == 654 );
    CHECK( snapshot.pointer.leftPressed );
    CHECK( snapshot.pointer.rightPressed );
    CHECK( snapshot.pointer.controlDown );
    CHECK( snapshot.pointer.shiftDown );
    CHECK( snapshot.pointer.uiWantsNativeMouseCursor );
    CHECK( snapshot.pointer.suppressWorldAction );
    CHECK( snapshot.frameInput.scenePhysicsEnabled );
    CHECK( snapshot.frameInput.sceneTimeScale == doctest::Approx( 0.5f ) );
}


TEST_CASE( "Input router: pointer presentation joins owner facts with one frame" )
{
    InputRouter router;
    InputActions output;
    router.BeginFrame( FocusedFrame( {}, false, true ), RuntimeInputKeyBindingView{}, output );
    router.PublishUiSnapshot( UiInputHitSnapshot{} );

    PointerPresentationPolicy policy = router.EvaluatePointerPresentation( PointerPresentationPolicyInput{} );
    CHECK( policy.mouseLookOwnsCursor );
    CHECK( policy.hideNativeCursor );

    UiInputHitSnapshot blockedUi;
    blockedUi.blocksCameraMouse = true;
    router.PublishUiSnapshot( blockedUi );
    policy = router.EvaluatePointerPresentation( PointerPresentationPolicyInput{} );
    CHECK_FALSE( policy.mouseLookOwnsCursor );
    CHECK_FALSE( policy.hideNativeCursor );

    router.BeginFrame( FocusedFrame( {} ), RuntimeInputKeyBindingView{}, output );
    router.PublishUiSnapshot( UiInputHitSnapshot{} );
    PointerPresentationPolicyInput editor;
    editor.editorModeEnabled = true;
    editor.editorPlacementModeEnabled = true;
    editor.editorPlacementPreviewVisible = true;
    policy = router.EvaluatePointerPresentation( editor );
    CHECK_FALSE( policy.mouseLookOwnsCursor );
    CHECK( policy.hideNativeCursor );
}
