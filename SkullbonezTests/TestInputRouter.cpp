/*
File: TestInputRouter.cpp
Purpose:
  Verifies allocation-free input snapshots, action edges, binding predicates,
  phase order, and focus cancellation without constructing Run or polling Win32.

Summary:
  Each test supplies immutable device frames and a tiny static binding table.
  BeginFrame advances all physical edge memory; RoutePhase then proves which
  semantic events are eligible under current context facts. Pointer arbitration
  tests drive the production phase cursor without constructing domain owners.

Glossary:
  UI (user interface): Interactive engine controls evaluated between routing
    phases.
  Win32: Windows desktop API that defines the virtual-key constants in tests.
  Held edge: Event emitted after an accepted press remains physically down.
  Context activation: Mode/UI fact mask that permits a binding to emit.
  Ghost press: False press caused by activating a context while its key was
    already held.
  Fluid-surface command: World-unit adjustment value emitted instead of raw
    Page Up/Page Down flags.
  Focus resynchronization: Refocus sample that remembers held input without
    treating it as newly pressed.
  Lifecycle generation: Scene-load identity that lets cursor intent publish
    once after activation without polling hardware in tests.
  Pointer arbitration: Ordered production cursor that gives the first consuming
    editor, pickup, camera, Replay, or launcher stage exclusive ownership.

Invariants:
  - Tests use no hardware, window, renderer, physics, or dynamic action storage.
  - Output order follows binding order inside the caller-selected phase order.
  - A key observed in an inactive/blocked context cannot fire until released and
    pressed again.
  - World-facing snapshots expose domain values, not physical water-control keys.
  - Repeated activation samples for one scene generation cannot republish the
    cursor-reset request.
  - Every pointer-claim combination preserves editor-to-launcher precedence and
    suppresses owner calls after the first accepted stage.

Related:
  - SkullbonezSource/Runtime/Input/InputRouter.h
  - SkullbonezTests/TestRuntimeInputBindings.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Core/SbDiagnosticStore.h"
#include "../SkullbonezSource/Runtime/Input/InputRouter.h"
#include "../SkullbonezSource/Runtime/App/InputFrame.h"
#include "../SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorInputPolicy.h"
#include "../SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.h"

#include <array>
#include <initializer_list>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::DevelopmentTools;
using namespace SkullbonezCore::UI::InputControl;
using SkullbonezCore::Core::SbDiagnosticStore;

namespace
{
template <std::size_t N> RuntimeInputKeyBindingView BindingView( const RuntimeInputKeyBinding ( &bindings )[N] )
{
    return RuntimeInputKeyBindingView { bindings, N };
}


RuntimeInputContextMask Context( RuntimeInputBindingContext context )
{
    return RuntimeInputContextBit( context );
}


InputKeySnapshot SnapshotFromDownKeys( std::initializer_list<int> downKeys )
{
    std::array<uint64_t, InputKeySnapshot::WORD_COUNT> words = {};
    for ( const int virtualKey : downKeys )
    {
        if ( virtualKey >= 0 && virtualKey < InputKeySnapshot::VIRTUAL_KEY_COUNT )
        {
            words[static_cast<std::size_t>( virtualKey ) / 64u] |=
                uint64_t { 1 } << ( static_cast<unsigned int>( virtualKey ) % 64u );
        }
    }
    return InputKeySnapshot::FromWords( words );
}


DeviceInputFrame FocusedFrame( std::initializer_list<int> downKeys, bool leftDown = false, bool rightDown = false )
{
    DeviceInputFrame frame;
    frame.keys = SnapshotFromDownKeys( downKeys );
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


TEST_CASE( "Input router: world pointer arbitration exhaustively preserves production precedence" )
{
    constexpr std::array<RuntimePointerRouteStage, 5> stages = {
        RuntimePointerRouteStage::Editor, RuntimePointerRouteStage::MousePickup, RuntimePointerRouteStage::AttachedCamera,
        RuntimePointerRouteStage::Replay, RuntimePointerRouteStage::Launcher,
    };

    for ( unsigned int claimMask = 0; claimMask < ( 1u << stages.size() ); ++claimMask )
    {
        CAPTURE( claimMask );
        RuntimePointerArbitration arbitration;
        RuntimePointerRouteStage expectedWinner = RuntimePointerRouteStage::None;

        for ( std::size_t stageIndex = 0; stageIndex < stages.size(); ++stageIndex )
        {
            const bool ownerMayRun = arbitration.BeginStage( stages[stageIndex] );
            CHECK( ownerMayRun == ( expectedWinner == RuntimePointerRouteStage::None ) );

            const bool stageClaims = ownerMayRun && ( claimMask & ( 1u << stageIndex ) ) != 0;
            arbitration.FinishStage( stages[stageIndex], stageClaims );

            if ( stageClaims )
            {
                expectedWinner = stages[stageIndex];
            }
        }

        CHECK( arbitration.Consumed() == ( expectedWinner != RuntimePointerRouteStage::None ) );
        CHECK( arbitration.Winner() == expectedWinner );
    }
}


TEST_CASE( "Input router: key snapshot is bounded and ignores invalid virtual keys" )
{
    const InputKeySnapshot snapshot = SnapshotFromDownKeys( { -1, 0, 'A', 255, 256 } );

    CHECK( snapshot.IsDown( 0 ) );
    CHECK( snapshot.IsDown( 'A' ) );
    CHECK( snapshot.IsDown( 255 ) );
    CHECK_FALSE( snapshot.IsDown( -1 ) );
    CHECK_FALSE( snapshot.IsDown( 256 ) );
    CHECK( snapshot.Words().size() == InputKeySnapshot::WORD_COUNT );
    static_assert( InputActions::CAPACITY == static_cast<std::size_t>( RuntimeInputAction::Count ) );
}


TEST_CASE( "Runtime copies device levels and pointer edges into a detached UI snapshot" )
{
    DeviceInputFrame frame = FocusedFrame( { VK_SHIFT, 'A' }, true );
    frame.clientX = 41;
    frame.clientY = 73;
    frame.hasClientPosition = true;
    frame.wheelDelta = -120;
    RuntimeMouseEdges mouse;
    mouse.leftDown = true;
    mouse.leftPressed = true;

    const UIInputSnapshot copied = BuildUIInputSnapshot( frame, mouse, {} );
    CHECK( copied.keyWords == frame.keys.Words() );
    CHECK( IsVirtualKeyDown( copied, VK_SHIFT ) );
    CHECK( IsVirtualKeyDown( copied, 'A' ) );
    CHECK_FALSE( IsVirtualKeyDown( copied, 'B' ) );
    CHECK( copied.mouseX == 41 );
    CHECK( copied.mouseY == 73 );
    CHECK( copied.wheelDelta == -120 );
    CHECK( copied.leftDown );
    CHECK( copied.leftPressed );
    CHECK_FALSE( copied.leftReleased );

    frame.keys = {};
    frame.clientX = 0;
    frame.clientY = 0;
    mouse = {};
    CHECK( copied.keyWords != frame.keys.Words() );
    CHECK( IsVirtualKeyDown( copied, 'A' ) );
    CHECK( copied.mouseX == 41 );
    CHECK( copied.leftPressed );

    const UIInputSnapshot overridden = BuildUIInputSnapshot( frame, mouse, UIPointerOverride { true, 12, 34 } );
    CHECK( overridden.mouseX == 12 );
    CHECK( overridden.mouseY == 34 );
}


TEST_CASE( "ImGui input policy: the selected surface routes each event class to one application consumer" )
{
    struct MatrixRow
    {
        const char* label;
        ImGuiEditorInputIntent intent;
        ImGuiEditorMessageClass messageClass;
        bool editorConsumes;
    };

    const MatrixRow rows[] = {
        { "legacy tool mouse", { false, true, true, true, false, false }, ImGuiEditorMessageClass::Mouse, false },
        { "legacy tool keyboard", { false, true, true, true, false, false }, ImGuiEditorMessageClass::Keyboard, false },
        { "imgui tool drag", { true, true, false, false, false, false }, ImGuiEditorMessageClass::Mouse, true },
        { "imgui tool drag repeat", { true, true, false, false, false, false }, ImGuiEditorMessageClass::Mouse, true },
        { "imgui tool typing", { true, false, true, true, false, false }, ImGuiEditorMessageClass::Keyboard, true },
        { "imgui tool text", { true, false, true, true, false, false }, ImGuiEditorMessageClass::Text, true },
        { "viewport camera drag", { true, true, false, false, true, true }, ImGuiEditorMessageClass::Mouse, false },
        { "viewport replay shortcut", { true, false, true, false, true, true }, ImGuiEditorMessageClass::Keyboard, false },
        { "focused field text over viewport", { true, false, true, true, true, true }, ImGuiEditorMessageClass::Text, true },
        { "alt tab focus and dpi", { true, true, true, true, false, false }, ImGuiEditorMessageClass::Platform, false },
    };

    for ( const MatrixRow& row : rows )
    {
        CAPTURE( row.label );
        const ImGuiEditorInputCapture capture = EvaluateImGuiEditorInputCapture( row.intent );
        const ImGuiEditorMessageDecision decision = DecideImGuiEditorMessageRoute( row.messageClass, capture );
        CHECK( decision.editorConsumes == row.editorConsumes );
        CHECK( decision.engineConsumes != decision.editorConsumes );
    }

    CHECK( ClassifyImGuiEditorNativeMessage( WM_INPUT, 0 ) == ImGuiEditorMessageClass::Mouse );
    CHECK( ClassifyImGuiEditorNativeMessage( WM_MOUSEWHEEL, 0 ) == ImGuiEditorMessageClass::Mouse );
    CHECK( ClassifyImGuiEditorNativeMessage( WM_KEYDOWN, VK_ESCAPE ) == ImGuiEditorMessageClass::Keyboard );
    CHECK( ClassifyImGuiEditorNativeMessage( WM_SYSKEYDOWN, VK_TAB ) == ImGuiEditorMessageClass::Platform );
    CHECK( ClassifyImGuiEditorNativeMessage( WM_SYSKEYDOWN, VK_F4 ) == ImGuiEditorMessageClass::Platform );
    CHECK( ClassifyImGuiEditorNativeMessage( WM_IME_COMPOSITION, 0 ) == ImGuiEditorMessageClass::Text );
    CHECK( ClassifyImGuiEditorNativeMessage( WM_SETFOCUS, 0 ) == ImGuiEditorMessageClass::Platform );
    CHECK( ClassifyImGuiEditorNativeMessage( WM_SIZE, 0 ) == ImGuiEditorMessageClass::Platform );
    CHECK( ClassifyImGuiEditorNativeMessage( WM_DPICHANGED, 0 ) == ImGuiEditorMessageClass::Platform );
}


TEST_CASE( "Input router: captured tool input requires release and repress before gameplay" )
{
    const RuntimeInputKeyBinding bindings[] = {
        { 'A', RuntimeInputAction::ToggleEditor, Context( RuntimeInputBindingContext::KeyboardUnblocked ) },
    };

    const RuntimeInputKeyBindingView view = BindingView( bindings );
    const RuntimeInputContextMask active = Context( RuntimeInputBindingContext::KeyboardUnblocked );
    SbDiagnosticStore diagnostics;
    InputRouter router { diagnostics };
    InputActions output;

    router.BeginFrame( FocusedFrame( {} ), view, output );
    router.BeginFrame( FocusedFrame( { 'A' }, true ), view, output, UiInputCaptureIntent { true, true, true } );
    router.RoutePhase( view, InputActionPhase::PreUi, active, output );
    CHECK( output.Count() == 0 );
    CHECK_FALSE( output.mouse.leftPressed );
    CHECK_FALSE( router.DeviceFrame().keys.IsDown( 'A' ) );
    CHECK_FALSE( router.DeviceFrame().leftDown );

    // Tool focus returns while the physical inputs remain held. The router
    // resynchronizes levels instead of manufacturing a press.
    router.BeginFrame( FocusedFrame( { 'A' }, true ), view, output );
    router.RoutePhase( view, InputActionPhase::PreUi, active, output );
    CHECK( output.Count() == 0 );
    CHECK_FALSE( output.mouse.leftPressed );

    router.BeginFrame( FocusedFrame( {} ), view, output );
    router.RoutePhase( view, InputActionPhase::PreUi, active, output );
    CHECK( output.Count() == 0 );
    CHECK( output.mouse.leftReleased );

    router.BeginFrame( FocusedFrame( { 'A' }, true ), view, output );
    router.RoutePhase( view, InputActionPhase::PreUi, active, output );
    REQUIRE( output.Count() == 1 );
    CHECK( output[0].edge == InputActionEdge::Pressed );
    CHECK( output.mouse.leftPressed );
}


TEST_CASE( "Input router: action storage belongs to the router" )
{
    SbDiagnosticStore diagnostics;
    InputRouter router { diagnostics };

    router.BeginFrame( FocusedFrame( {}, true ), RuntimeInputKeyBindingView {}, router.Actions() );
    CHECK( router.Actions().mouse.leftPressed );

    router.BeginFrame( FocusedFrame( {}, false ), RuntimeInputKeyBindingView {}, router.Actions() );
    CHECK( router.Actions().mouse.leftReleased );
}


TEST_CASE( "Input router: press hold and release preserve binding order" )
{
    const RuntimeInputKeyBinding bindings[] = {
        { 'A', RuntimeInputAction::ToggleEditor, Context( RuntimeInputBindingContext::KeyboardUnblocked ) },
        { 'B', RuntimeInputAction::CycleCameraMode, Context( RuntimeInputBindingContext::KeyboardUnblocked ) },
    };

    const RuntimeInputKeyBindingView view = BindingView( bindings );
    const RuntimeInputContextMask active = Context( RuntimeInputBindingContext::KeyboardUnblocked );
    SbDiagnosticStore diagnostics;
    InputRouter router { diagnostics };
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
        { 'M', RuntimeInputAction::CycleLauncherFireMode,
          Context( RuntimeInputBindingContext::KeyboardUnblocked ) | RuntimeInputBindingContext::Launcher },
    };

    const RuntimeInputKeyBindingView view = BindingView( bindings );
    const RuntimeInputContextMask keyboard = Context( RuntimeInputBindingContext::KeyboardUnblocked );
    const RuntimeInputContextMask launcher = keyboard | RuntimeInputBindingContext::Launcher;
    SbDiagnosticStore diagnostics;
    InputRouter router { diagnostics };
    InputActions output;

    router.BeginFrame( FocusedFrame( { 'M' } ), view, output );
    router.RoutePhase( view, InputActionPhase::PreUi, keyboard, output );
    CHECK( output.Count() == 0 );

    router.BeginFrame( FocusedFrame( { 'M' } ), view, output );
    router.RoutePhase( view, InputActionPhase::PreUi, launcher, output );
    CHECK( output.Count() == 0 );

    router.BeginFrame( FocusedFrame( {} ), view, output );
    router.RoutePhase( view, InputActionPhase::PreUi, launcher, output );
    CHECK( output.Count() == 0 );

    router.BeginFrame( FocusedFrame( { 'M' } ), view, output );
    router.RoutePhase( view, InputActionPhase::PreUi, launcher, output );
    REQUIRE( output.Count() == 1 );
    CHECK( output[0].edge == InputActionEdge::Pressed );
}


TEST_CASE( "Input router: UI refusal requires release and repress" )
{
    const RuntimeInputKeyBinding bindings[] = {
        { VK_ESCAPE, RuntimeInputAction::DismissOrExitUI,
          Context( RuntimeInputBindingContext::AfterUIUpdate ) | RuntimeInputBindingContext::UINotInteracted },
    };

    const RuntimeInputKeyBindingView view = BindingView( bindings );
    const RuntimeInputContextMask accepted = Context( RuntimeInputBindingContext::UINotInteracted );
    SbDiagnosticStore diagnostics;
    InputRouter router { diagnostics };
    InputActions output;

    router.BeginFrame( FocusedFrame( { VK_ESCAPE } ), view, output );
    router.RoutePhase( view, InputActionPhase::AfterUi, 0u, output );
    CHECK( output.Count() == 0 );

    router.BeginFrame( FocusedFrame( { VK_ESCAPE } ), view, output );
    router.RoutePhase( view, InputActionPhase::AfterUi, accepted, output );
    CHECK( output.Count() == 0 );

    router.BeginFrame( FocusedFrame( {} ), view, output );
    router.RoutePhase( view, InputActionPhase::AfterUi, accepted, output );
    CHECK( output.Count() == 0 );

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
        { 'M', RuntimeInputAction::CycleLauncherFireMode,
          Context( RuntimeInputBindingContext::KeyboardUnblocked ) | RuntimeInputBindingContext::Launcher },
    };

    const RuntimeInputKeyBindingView view = BindingView( bindings );
    const RuntimeInputContextMask preLauncher = Context( RuntimeInputBindingContext::KeyboardUnblocked );
    SbDiagnosticStore diagnostics;
    InputRouter router { diagnostics };
    InputActions output;

    router.BeginFrame( FocusedFrame( { 'N', 'M' } ), view, output );
    router.RoutePhase( view, InputActionPhase::PreUi, preLauncher, output );

    REQUIRE( output.Count() == 1 );
    CHECK( output[0].action == RuntimeInputAction::ToggleLauncher );
    CHECK( output[0].edge == InputActionEdge::Pressed );
}


TEST_CASE( "Input router: quick-repeat timing is action-owned" )
{
    SbDiagnosticStore diagnostics;
    InputRouter router { diagnostics };

    CHECK_FALSE( router.IsQuickRepeat( RuntimeInputAction::DismissOrExitUI, 10.0, 0.32 ) );
    router.RecordTap( RuntimeInputAction::DismissOrExitUI, 10.0 );
    CHECK( router.IsQuickRepeat( RuntimeInputAction::DismissOrExitUI, 10.31, 0.32 ) );
    CHECK_FALSE( router.IsQuickRepeat( RuntimeInputAction::DismissOrExitUI, 10.33, 0.32 ) );
    CHECK_FALSE( router.IsQuickRepeat( RuntimeInputAction::ToggleEditor, 10.1, 0.32 ) );

}


TEST_CASE( "Input router: a skipped capture phase cannot replay a stale press" )
{
    const RuntimeInputKeyBinding bindings[] = {
        { VK_F3, RuntimeInputAction::SaveScreenshot, Context( RuntimeInputBindingContext::Capture ) },
    };

    const RuntimeInputKeyBindingView view = BindingView( bindings );
    SbDiagnosticStore diagnostics;
    InputRouter router { diagnostics };
    InputActions output;

    router.BeginFrame( FocusedFrame( { VK_F3 } ), view, output );
    CHECK( output.Count() == 0 );

    router.BeginFrame( FocusedFrame( { VK_F3 } ), view, output );
    router.RoutePhase( view, InputActionPhase::Capture, 0u, output );
    CHECK( output.Count() == 0 );

    router.BeginFrame( FocusedFrame( {} ), view, output );
    router.RoutePhase( view, InputActionPhase::Capture, 0u, output );
    CHECK( output.Count() == 0 );

    router.BeginFrame( FocusedFrame( { VK_F3 } ), view, output );
    router.RoutePhase( view, InputActionPhase::Capture, 0u, output );
    REQUIRE( output.Count() == 1 );
    CHECK( output[0].edge == InputActionEdge::Pressed );
}


TEST_CASE( "Input router: post-UI pointer snapshot is published once as a value" )
{
    SbDiagnosticStore diagnostics;
    InputRouter router { diagnostics };
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


TEST_CASE( "Input router: cold start initializes cursor and focus loss restores cursor intent" )
{
    SbDiagnosticStore diagnostics;
    InputRouter router { diagnostics };
    InputActions output;
    PointerPresentationState presentation;

    // Regression: the Win32 cursor latch starts outside InputRouter ownership.
    // The first consume must publish visible/no-capture even though those are
    // also the router's member defaults.
    REQUIRE( router.ConsumePointerPresentationChange( presentation ) );
    CHECK_FALSE( presentation.nativeCapture );
    CHECK( presentation.cursorVisible );
    CHECK_FALSE( router.ConsumePointerPresentationChange( presentation ) );

    // A platform UI may have changed HWND capture/cursor state without changing
    // the engine's desired values. Deferral must republish those same values.
    router.DeferPointerPresentationCommit();
    REQUIRE( router.ConsumePointerPresentationChange( presentation ) );
    CHECK_FALSE( presentation.nativeCapture );
    CHECK( presentation.cursorVisible );

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
        { 'M', RuntimeInputAction::CycleLauncherFireMode,
          Context( RuntimeInputBindingContext::KeyboardUnblocked ) | RuntimeInputBindingContext::Launcher },
    };

    const RuntimeInputKeyBindingView view = BindingView( bindings );
    const RuntimeInputContextMask keyboard = Context( RuntimeInputBindingContext::KeyboardUnblocked );
    const RuntimeInputContextMask launcher = keyboard | RuntimeInputBindingContext::Launcher;
    SbDiagnosticStore diagnostics;
    InputRouter router { diagnostics };
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
    CHECK( output.Count() == 0 );
}


TEST_CASE( "Input router: phases append in explicit caller order" )
{
    const RuntimeInputKeyBinding bindings[] = {
        { 'A', RuntimeInputAction::ToggleEditor, Context( RuntimeInputBindingContext::KeyboardUnblocked ) },
        { VK_ESCAPE, RuntimeInputAction::DismissOrExitUI,
          Context( RuntimeInputBindingContext::AfterUIUpdate ) | RuntimeInputBindingContext::UINotInteracted },
        { VK_F3, RuntimeInputAction::SaveScreenshot, Context( RuntimeInputBindingContext::Capture ) },
    };

    const RuntimeInputKeyBindingView view = BindingView( bindings );
    SbDiagnosticStore diagnostics;
    InputRouter router { diagnostics };
    InputActions output;

    router.BeginFrame( FocusedFrame( { 'A', VK_ESCAPE, VK_F3 } ), view, output );
    router.RoutePhase( view, InputActionPhase::PreUi, Context( RuntimeInputBindingContext::KeyboardUnblocked ), output );
    router.RoutePhase( view, InputActionPhase::AfterUi, Context( RuntimeInputBindingContext::UINotInteracted ), output );
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
    SbDiagnosticStore diagnostics;
    InputRouter router { diagnostics };
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
    SbDiagnosticStore diagnostics;
    InputRouter router { diagnostics };
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
    CHECK( output.Count() == 0 );

    router.BeginFrame( FocusedFrame( { 'A' }, true, true ), view, output );
    router.RoutePhase( view, InputActionPhase::PreUi, active, output );
    CHECK( output.focusGained );
    CHECK( output.Count() == 0 );
    CHECK_FALSE( output.mouse.leftPressed );
    CHECK_FALSE( output.mouse.rightPressed );

    router.BeginFrame( FocusedFrame( {} ), view, output );
    router.RoutePhase( view, InputActionPhase::PreUi, active, output );
    CHECK( output.Count() == 0 );
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
                                             RuntimeInputBindingContext::Launcher | RuntimeInputBindingContext::DebugOnly;

    const RuntimeInputContextMask partial = Context( RuntimeInputBindingContext::KeyboardUnblocked ) |
                                            RuntimeInputBindingContext::Launcher;

    CHECK( InputRouter::ContextsSatisfied( 0u, 0u ) );
    CHECK_FALSE( InputRouter::ContextsSatisfied( required, partial ) );
    CHECK( InputRouter::ContextsSatisfied( required, partial | RuntimeInputBindingContext::DebugOnly ) );

    const RuntimeInputKeyBinding afterBinding = { VK_ESCAPE, RuntimeInputAction::DismissOrExitUI,
                                                  Context( RuntimeInputBindingContext::AfterUIUpdate ) };

    const RuntimeInputKeyBinding captureBinding = { VK_F3, RuntimeInputAction::SaveScreenshot,
                                                    Context( RuntimeInputBindingContext::Capture ) };

    CHECK( InputRouter::PhaseForBinding( afterBinding ) == InputActionPhase::AfterUi );
    CHECK( InputRouter::PhaseForBinding( captureBinding ) == InputActionPhase::Capture );
}


TEST_CASE( "Input router: runtime snapshot joins one device and UI pointer frame" )
{
    SbDiagnosticStore diagnostics;
    InputRouter router { diagnostics };
    InputActions output;
    DeviceInputFrame device = FocusedFrame( { VK_CONTROL, VK_SHIFT, VK_RETURN, VK_NEXT, VK_PRIOR }, true, true );
    device.clientX = 321;
    device.clientY = 654;
    device.hasClientPosition = true;
    router.BeginFrame( device, RuntimeInputKeyBindingView {}, output );

    UiInputHitSnapshot ui;
    ui.mouse = output.mouse;
    ui.blocksKeyboard = true;
    ui.blocksCameraMouse = true;
    ui.wantsNativeCursor = true;
    router.PublishUiSnapshot( ui );

    RuntimeInteractionFrameInput frameInput;
    frameInput.scenePhysicsEnabled = true;
    frameInput.stepHeld = true;
    frameInput.sceneTimeScale = 0.5f;
    const RuntimeInputSnapshot& snapshot = router.PublishRuntimeSnapshot( frameInput, true );
    CHECK( snapshot.appFocused );
    CHECK( snapshot.uiBlocksKeyboard );
    CHECK( snapshot.uiBlocksMouse );
    CHECK( snapshot.pointer.clientX == 321 );
    CHECK( snapshot.pointer.clientY == 654 );
    CHECK( snapshot.pointer.hasClientPosition );
    CHECK( snapshot.pointer.leftPressed );
    CHECK( snapshot.pointer.rightPressed );
    CHECK( snapshot.pointer.controlDown );
    CHECK( snapshot.pointer.shiftDown );
    CHECK( snapshot.pointer.uiWantsNativeMouseCursor );
    CHECK( snapshot.pointer.suppressWorldAction );
    CHECK( snapshot.enterDown );
    CHECK( snapshot.fluidSurfaceAdjustment.velocityMetersPerSecond == doctest::Approx( 0.0f ) );
    CHECK( snapshot.frameInput.scenePhysicsEnabled );
    CHECK( snapshot.frameInput.stepHeld );
    CHECK( snapshot.frameInput.sceneTimeScale == doctest::Approx( 0.5f ) );
    CHECK( &router.RuntimeSnapshot() == &snapshot );

    router.BeginFrame( DeviceInputFrame {}, RuntimeInputKeyBindingView {}, output );
    CHECK_FALSE( router.RuntimeSnapshot().appFocused );
    CHECK_FALSE( router.RuntimeSnapshot().pointer.hasClientPosition );
    CHECK_FALSE( router.RuntimeSnapshot().enterDown );
    CHECK( router.RuntimeSnapshot().fluidSurfaceAdjustment.velocityMetersPerSecond == doctest::Approx( 0.0f ) );
}


TEST_CASE( "Input router: fluid surface command hides physical key semantics" )
{
    SbDiagnosticStore diagnostics;
    InputRouter router { diagnostics };
    InputActions output;

    router.BeginFrame( FocusedFrame( { VK_PRIOR } ), RuntimeInputKeyBindingView {}, output );
    const RuntimeInputSnapshot raise = router.BuildRuntimeSnapshot( RuntimeInteractionFrameInput {}, false );
    CHECK( raise.fluidSurfaceAdjustment.velocityMetersPerSecond == doctest::Approx( 20.0f ) );
    CHECK( raise.fluidSurfaceAdjustment.DeltaMeters( 0.5f ) == doctest::Approx( 10.0f ) );

    router.BeginFrame( FocusedFrame( { VK_NEXT } ), RuntimeInputKeyBindingView {}, output );
    const RuntimeInputSnapshot lower = router.BuildRuntimeSnapshot( RuntimeInteractionFrameInput {}, false );
    CHECK( lower.fluidSurfaceAdjustment.velocityMetersPerSecond == doctest::Approx( -20.0f ) );

    router.BeginFrame( FocusedFrame( { VK_NEXT, VK_PRIOR } ), RuntimeInputKeyBindingView {}, output );
    const RuntimeInputSnapshot cancelled = router.BuildRuntimeSnapshot( RuntimeInteractionFrameInput {}, false );
    CHECK( cancelled.fluidSurfaceAdjustment.velocityMetersPerSecond == doctest::Approx( 0.0f ) );
}


TEST_CASE( "Input router: pointer presentation joins owner facts with one frame" )
{
    SbDiagnosticStore diagnostics;
    InputRouter router { diagnostics };
    InputActions output;
    router.BeginFrame( FocusedFrame( {}, false, true ), RuntimeInputKeyBindingView {}, output );
    router.PublishUiSnapshot( UiInputHitSnapshot {} );

    PointerPresentationPolicy policy = router.EvaluatePointerPresentation( PointerPresentationPolicyInput {} );
    CHECK( policy.mouseLookOwnsCursor );
    CHECK( policy.hideNativeCursor );

    UiInputHitSnapshot blockedUi;
    blockedUi.blocksCameraMouse = true;
    router.PublishUiSnapshot( blockedUi );
    policy = router.EvaluatePointerPresentation( PointerPresentationPolicyInput {} );
    CHECK_FALSE( policy.mouseLookOwnsCursor );
    CHECK_FALSE( policy.hideNativeCursor );

    router.BeginFrame( FocusedFrame( {} ), RuntimeInputKeyBindingView {}, output );
    router.PublishUiSnapshot( UiInputHitSnapshot {} );
    PointerPresentationPolicyInput editor;
    editor.editorModeEnabled = true;
    editor.editorPlacementModeEnabled = true;
    editor.editorPlacementPreviewVisible = true;
    policy = router.EvaluatePointerPresentation( editor );
    CHECK_FALSE( policy.mouseLookOwnsCursor );
    CHECK( policy.hideNativeCursor );
}

TEST_CASE( "Input router: scene activation publishes cursor reset once per generation" )
{
    SbDiagnosticStore diagnostics;
    InputRouter router { diagnostics };
    SceneLifecyclePacket packet;
    packet.generation = 1;
    packet.event = SceneRuntimeLifecycleEvent::AfterSceneCleared;

    CHECK_FALSE( router.ObserveSceneLifecycle( packet, true ) );
    CHECK( router.CursorVisibleRequested() );

    packet.event = SceneRuntimeLifecycleEvent::AfterSceneActivated;
    CHECK( router.ObserveSceneLifecycle( packet, true ) );
    CHECK_FALSE( router.CursorVisibleRequested() );
    CHECK_FALSE( router.ObserveSceneLifecycle( packet, true ) );

    packet.generation = 2;
    CHECK( router.ObserveSceneLifecycle( packet, true ) );
    CHECK_FALSE( router.ObserveSceneLifecycle( packet, true ) );
}
