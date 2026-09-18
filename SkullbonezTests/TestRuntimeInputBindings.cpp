/*
File: SkullbonezTests/TestRuntimeInputBindings.cpp
Purpose:
  Locks the runtime keyboard shortcut table as observable data.

Summary:
  These tests do not press keys or construct Run. They inspect the shared
  key/action/context rows that the InputRouter interaction layer dispatches,
  so a shortcut regression fails before it reaches an interaction or DX12
  launch test.

Glossary:
  Virtual key: Win32 integer key code used by the runtime input poller.

  Exact binding: One virtual key plus one context mask mapping to one action.

Invariants:
  - Duplicate virtual keys are allowed only when their context masks differ.
  - The shared table is the source under test; do not mirror it wholesale here.

Related:
  - Agentic/Reference/engine-glossary.md
  - SkullbonezSource/Runtime/Input/InputController.Bindings.h
  - SkullbonezSource/Runtime/App/InputRouter.Interactions.cpp
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Input/InputController.Bindings.h"
#include "../SkullbonezSource/Runtime/Render/RenderPresentationSettings.h"

using SkullbonezCore::Runtime::RuntimeInputAction;
using SkullbonezCore::Runtime::RuntimeInputBindingContext;
using SkullbonezCore::Runtime::RuntimeInputContextBit;
using SkullbonezCore::Runtime::RuntimeInputContextMask;
using SkullbonezCore::Runtime::RuntimeInputKeyBinding;
using SkullbonezCore::Runtime::RuntimeInputKeyBindingView;
using SkullbonezCore::Runtime::TakeInputKeyboardBindings;

namespace
{
RuntimeInputContextMask Context( RuntimeInputBindingContext context )
{
    return RuntimeInputContextBit( context );
}

const RuntimeInputKeyBinding* FindExactBinding( int virtualKey, RuntimeInputContextMask contexts )
{
    const RuntimeInputKeyBindingView table = TakeInputKeyboardBindings();
    for ( std::size_t i = 0; i < table.count; ++i )
    {
        const RuntimeInputKeyBinding& binding = table.bindings[i];
        if ( binding.virtualKey == virtualKey && binding.contexts == contexts )
        {
            return &binding;
        }
    }
    return nullptr;
}

void CheckExactBinding( int virtualKey, RuntimeInputContextMask contexts, RuntimeInputAction expectedAction )
{
    const RuntimeInputKeyBinding* binding = FindExactBinding( virtualKey, contexts );
    REQUIRE( binding != nullptr );
    CHECK( binding->action == expectedAction );
}
} // namespace

TEST_CASE( "Runtime input bindings: core keyboard shortcuts map to actions" )
{
    const RuntimeInputContextMask keyboard = Context( RuntimeInputBindingContext::KeyboardUnblocked );
    const RuntimeInputKeyBindingView table = TakeInputKeyboardBindings();

    REQUIRE( table.bindings != nullptr );
    CHECK( table.count == 51u );
    CheckExactBinding( VK_OEM_3, keyboard, RuntimeInputAction::ToggleEditor );
    CheckExactBinding( VK_TAB, keyboard, RuntimeInputAction::CycleCameraMode );
    CheckExactBinding( 'F', keyboard, RuntimeInputAction::ToggleFlyCamera );
    CheckExactBinding( 'N', keyboard, RuntimeInputAction::ToggleLauncher );
    CheckExactBinding( '0', keyboard, RuntimeInputAction::ToggleUIVisibility );
    CheckExactBinding( VK_F9, keyboard, RuntimeInputAction::ReloadShadersFromSource );
    CheckExactBinding( VK_F5, keyboard, RuntimeInputAction::TogglePerformanceHistogram );
    CheckExactBinding( VK_F6, keyboard, RuntimeInputAction::ToggleMemoryOverlay );
    CheckExactBinding( VK_F7, keyboard, RuntimeInputAction::ToggleSplitFutureLook );
    CheckExactBinding( VK_F10, keyboard, RuntimeInputAction::RerollLookLab );
    CheckExactBinding( VK_F11, keyboard, RuntimeInputAction::SaveLookLabBundle );
    CheckExactBinding( VK_F8, keyboard, RuntimeInputAction::ToggleInteractionRecording );
    CheckExactBinding( VK_OEM_4, keyboard, RuntimeInputAction::StepPhysicsPipelinePrevious );
    CheckExactBinding( VK_OEM_6, keyboard, RuntimeInputAction::StepPhysicsPipelineNext );
    CheckExactBinding( VK_OEM_COMMA, keyboard, RuntimeInputAction::CycleReplayPathColorMode );
    CheckExactBinding( 'H', keyboard, RuntimeInputAction::ToggleReplayGuideArcs );
    CheckExactBinding( 'J', keyboard, RuntimeInputAction::ToggleReplayTripPlanner );
    CheckExactBinding( 'I', keyboard, RuntimeInputAction::ToggleReplayPorkchopPanel );
    CheckExactBinding( 'P', keyboard, RuntimeInputAction::TogglePredictionInspection );
    CHECK( FindExactBinding( VK_OEM_PERIOD, keyboard ) == nullptr );
    CHECK( FindExactBinding( VK_LEFT, keyboard ) == nullptr );
    CHECK( FindExactBinding( VK_RIGHT, keyboard ) == nullptr );
    CheckExactBinding( 'Z', keyboard | RuntimeInputBindingContext::Editor, RuntimeInputAction::UndoEditor );
    CheckExactBinding( 'Y', keyboard | RuntimeInputBindingContext::Editor, RuntimeInputAction::RedoEditor );
    CheckExactBinding( VK_DELETE, keyboard | RuntimeInputBindingContext::Editor, RuntimeInputAction::DeleteEditorSelection );
}

TEST_CASE( "Runtime input bindings: contextual shortcuts stay on their owning contexts" )
{
    const RuntimeInputContextMask keyboard = Context( RuntimeInputBindingContext::KeyboardUnblocked );

    const auto comparison = Context( RuntimeInputBindingContext::Comparison );
    CheckExactBinding( VK_LEFT, comparison, RuntimeInputAction::ComparisonStepBackward );
    CheckExactBinding( VK_RIGHT, comparison, RuntimeInputAction::ComparisonStepForward );
    CheckExactBinding( VK_SPACE, comparison, RuntimeInputAction::ComparisonPlayPause );
    CheckExactBinding( 'M', keyboard | RuntimeInputBindingContext::Launcher, RuntimeInputAction::CycleLauncherFireMode );
    CheckExactBinding( VK_F1, keyboard | RuntimeInputBindingContext::AttachedCamera, RuntimeInputAction::CycleAttachedCameraSubmode );
    CheckExactBinding( VK_RETURN, keyboard | RuntimeInputBindingContext::AttachedCamera, RuntimeInputAction::ToggleAttachedCameraPin );
    CheckExactBinding( 'B', keyboard | RuntimeInputBindingContext::Director, RuntimeInputAction::ToggleDirectorGrab );
    CheckExactBinding( 'J', keyboard | RuntimeInputBindingContext::DirectorAuthoring, RuntimeInputAction::SetDirectorPhasePose );
    CheckExactBinding( 'K', keyboard | RuntimeInputBindingContext::DirectorAuthoring, RuntimeInputAction::StepDirectorPhase );
    CheckExactBinding( 'L', keyboard | RuntimeInputBindingContext::DirectorAuthoring, RuntimeInputAction::SaveDirectorShotList );
    CheckExactBinding( VK_RETURN, keyboard | RuntimeInputBindingContext::Launcher | RuntimeInputBindingContext::ReplayRestoreNotConsumed | RuntimeInputBindingContext::DebugOnly, RuntimeInputAction::WriteLauncherReproSnapshot );
}

TEST_CASE( "Runtime input bindings: late and capture shortcuts are explicitly grouped" )
{
    const RuntimeInputContextMask afterUI = Context( RuntimeInputBindingContext::AfterUIUpdate );
    const RuntimeInputContextMask capture = Context( RuntimeInputBindingContext::Capture );

    CheckExactBinding( VK_ESCAPE, afterUI | RuntimeInputBindingContext::UINotInteracted, RuntimeInputAction::DismissOrExitUI );
    CheckExactBinding( 'R', afterUI, RuntimeInputAction::ResetScene );
    CheckExactBinding( VK_BACK, afterUI | RuntimeInputBindingContext::Scene, RuntimeInputAction::ResetSceneFromBackspace );
    CheckExactBinding( VK_F2, capture, RuntimeInputAction::SaveSceneSnapshot );
    CheckExactBinding( VK_F3, capture, RuntimeInputAction::SaveScreenshot );
}

TEST_CASE( "Runtime input bindings: key and context pairs are unique" )
{
    const RuntimeInputKeyBindingView table = TakeInputKeyboardBindings();
    REQUIRE( table.bindings != nullptr );

    for ( std::size_t i = 0; i < table.count; ++i )
    {
        for ( std::size_t j = i + 1u; j < table.count; ++j )
        {
            const RuntimeInputKeyBinding& left = table.bindings[i];
            const RuntimeInputKeyBinding& right = table.bindings[j];
            CHECK( !( left.virtualKey == right.virtualKey && left.contexts == right.contexts ) );
        }
    }
}

TEST_CASE( "Split Future look override: round trips preserve authored scenes and survive scene changes" )
{
    using SkullbonezCore::Runtime::SplitFutureLookOverride;
    SkullbonezCore::Core::CinematicRenderConfig authored;
    authored.objectStyle = 3;
    authored.skyMode = 7;
    authored.exposure = 0.42f;
    SplitFutureLookOverride look;
    CHECK_FALSE( look.Resolve( authored, false ).enabled );
    look.Toggle( false );
    const auto on = look.Resolve( authored, false );
    CHECK( on.enabled );
    CHECK( on.skyMode == 22 );
    CHECK( on.terrainMode == 16 );
    CHECK( on.objectStyle == 14 );
    CHECK( on.waterMode == 5 );
    CHECK( on.exposure == authored.exposure );
    CHECK( authored.objectStyle == 3 );
    // A different scene while enabled uses the same override, with its own light.
    auto otherScene = authored;
    otherScene.objectStyle = 6;
    CHECK( look.Resolve( otherScene, true ).objectStyle == 14 );
    look.Toggle( false );
    CHECK_FALSE( look.Resolve( authored, false ).enabled );
    CHECK( look.Resolve( authored, false ).skyMode == 7 );
    CHECK( look.Resolve( otherScene, true ).enabled );
    CHECK( look.Resolve( otherScene, true ).objectStyle == 6 );
}

TEST_CASE( "Split Future look override: authored showcase toggles off on its first press" )
{
    SkullbonezCore::Core::CinematicRenderConfig authored;
    authored.objectStyle = 14;
    SkullbonezCore::Runtime::SplitFutureLookOverride look;
    CHECK( look.Resolve( authored, true ).enabled );
    look.Toggle( true );
    CHECK_FALSE( look.Resolve( authored, true ).enabled );
    look.Toggle( true );
    CHECK( look.Resolve( authored, true ).enabled );
    CHECK( authored.objectStyle == 14 );
}
