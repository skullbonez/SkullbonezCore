/*
File: SkullbonezTests/TestRuntimeInputBindings.cpp
Purpose:
  Locks the runtime keyboard shortcut table as observable data.

Mental model:
  These tests do not press keys or construct Run. They inspect the shared
  key/action/context rows that RunInput dispatches, so a shortcut regression
  fails before it reaches an interaction or DX12 launch test.

Glossary:
  Virtual key: Win32 integer key code used by the runtime input poller.
  Context mask: Binding bits that decide which dispatch pass or runtime state
    owns a shortcut.
  Exact binding: One virtual key plus one context mask mapping to one action.

Invariants:
  - Duplicate virtual keys are allowed only when their context masks differ.
  - The shared table is the source under test; do not mirror it wholesale here.

Related:
  - SkullbonezSource/Runtime/InputController.Bindings.h
  - SkullbonezSource/Runtime/RunInput.cpp
  - Agentic/Plans/TODO/behavioral-test-depth.md
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/InputController.Bindings.h"

using SkullbonezCore::Basics::RuntimeInputAction;
using SkullbonezCore::Basics::RuntimeInputBindingContext;
using SkullbonezCore::Basics::RuntimeInputContextBit;
using SkullbonezCore::Basics::RuntimeInputContextMask;
using SkullbonezCore::Basics::RuntimeInputKeyBinding;
using SkullbonezCore::Basics::RuntimeInputKeyBindingView;
using SkullbonezCore::Basics::TakeInputKeyboardBindings;

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
    CHECK( table.count == 38u );
    CheckExactBinding( VK_OEM_3, keyboard, RuntimeInputAction::ToggleEditor );
    CheckExactBinding( VK_TAB, keyboard, RuntimeInputAction::CycleCameraMode );
    CheckExactBinding( 'F', keyboard, RuntimeInputAction::ToggleFlyCamera );
    CheckExactBinding( 'N', keyboard, RuntimeInputAction::ToggleLauncher );
    CheckExactBinding( '0', keyboard, RuntimeInputAction::ToggleUIVisibility );
    CheckExactBinding( VK_F9, keyboard, RuntimeInputAction::ReloadShadersFromSource );
    CheckExactBinding( VK_LEFT, keyboard, RuntimeInputAction::NavigateScenePrevious );
    CheckExactBinding( VK_RIGHT, keyboard, RuntimeInputAction::NavigateSceneNext );
}

TEST_CASE( "Runtime input bindings: contextual shortcuts stay on their owning contexts" )
{
    const RuntimeInputContextMask keyboard = Context( RuntimeInputBindingContext::KeyboardUnblocked );

    CheckExactBinding( 'M',
                       keyboard | RuntimeInputBindingContext::Launcher,
                       RuntimeInputAction::CycleLauncherFireMode );
    CheckExactBinding( VK_F1,
                       keyboard | RuntimeInputBindingContext::AttachedCamera,
                       RuntimeInputAction::CycleAttachedCameraSubmode );
    CheckExactBinding( VK_RETURN,
                       keyboard | RuntimeInputBindingContext::AttachedCamera,
                       RuntimeInputAction::ToggleAttachedCameraPin );
    CheckExactBinding( 'B',
                       keyboard | RuntimeInputBindingContext::Director,
                       RuntimeInputAction::ToggleDirectorGrab );
    CheckExactBinding( 'J',
                       keyboard | RuntimeInputBindingContext::DirectorAuthoring,
                       RuntimeInputAction::SetDirectorPhasePose );
    CheckExactBinding( 'K',
                       keyboard | RuntimeInputBindingContext::DirectorAuthoring,
                       RuntimeInputAction::StepDirectorPhase );
    CheckExactBinding( 'L',
                       keyboard | RuntimeInputBindingContext::DirectorAuthoring,
                       RuntimeInputAction::SaveDirectorShotList );
    CheckExactBinding( VK_RETURN,
                       keyboard | RuntimeInputBindingContext::Launcher |
                           RuntimeInputBindingContext::ReplayRestoreNotConsumed | RuntimeInputBindingContext::DebugOnly,
                       RuntimeInputAction::WriteLauncherReproSnapshot );
}

TEST_CASE( "Runtime input bindings: late and capture shortcuts are explicitly grouped" )
{
    const RuntimeInputContextMask afterUI = Context( RuntimeInputBindingContext::AfterUIUpdate );
    const RuntimeInputContextMask capture = Context( RuntimeInputBindingContext::Capture );

    CheckExactBinding( VK_ESCAPE,
                       afterUI | RuntimeInputBindingContext::UINotInteracted,
                       RuntimeInputAction::DismissOrExitUI );
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
