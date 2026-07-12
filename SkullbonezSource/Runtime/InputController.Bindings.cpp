/*
File: InputController.Bindings.cpp
Purpose:
  Owns the static keyboard binding rows used by the runtime input loop.

Summary:
  This file is the command table. It does not decide whether the player is in
  editor, launcher, replay, or director mode; it only records the virtual key,
  normalized action, and context bits that later dispatch code interprets.

Glossary:
  Virtual key: Win32 integer key code read from the immutable device snapshot.
  Dispatch pass: Part of TakeInput that consumes a subset of binding rows, such
    as pre-UI keyboard, capture hotkeys, or post-UI reset handling.
  Debug-only binding: Row whose action is compiled or executed only for debug
    tooling but remains visible in the table for tests.

Invariants:
  - Preserve row order unless the matching RunInput dispatch order changes.
  - Duplicate virtual keys must have different context masks.

Related:
  - InputController.Bindings.h exposes the table view.
  - RunInput.cpp consumes the table and applies side effects.
  - SkullbonezTests/TestRuntimeInputBindings.cpp locks key/context mappings.
*/
#include "InputController.Bindings.h"

namespace SkullbonezCore
{
namespace Basics
{
namespace
{
using InputBindingContext = RuntimeInputBindingContext;
constexpr RuntimeInputContextMask kKeyboardUnblockedContext =
    RuntimeInputContextBit( InputBindingContext::KeyboardUnblocked );
constexpr RuntimeInputContextMask kAfterUIUpdateContext = RuntimeInputContextBit( InputBindingContext::AfterUIUpdate );
constexpr RuntimeInputContextMask kCaptureContext = RuntimeInputContextBit( InputBindingContext::Capture );

const RuntimeInputKeyBinding kTakeInputKeyboardBindings[] = {
    { VK_OEM_3, RuntimeInputAction::ToggleEditor, kKeyboardUnblockedContext },
    { VK_TAB, RuntimeInputAction::CycleCameraMode, kKeyboardUnblockedContext },
    { 'F', RuntimeInputAction::ToggleFlyCamera, kKeyboardUnblockedContext },
    { 'N', RuntimeInputAction::ToggleLauncher, kKeyboardUnblockedContext },
    { 'M', RuntimeInputAction::CycleLauncherFireMode, kKeyboardUnblockedContext | InputBindingContext::Launcher },
    { VK_F1,
      RuntimeInputAction::CycleAttachedCameraSubmode,
      kKeyboardUnblockedContext | InputBindingContext::AttachedCamera },
    { VK_RETURN,
      RuntimeInputAction::ToggleAttachedCameraPin,
      kKeyboardUnblockedContext | InputBindingContext::AttachedCamera },
    { 'B', RuntimeInputAction::ToggleDirectorGrab, kKeyboardUnblockedContext | InputBindingContext::Director },
    { 'J',
      RuntimeInputAction::SetDirectorPhasePose,
      kKeyboardUnblockedContext | InputBindingContext::DirectorAuthoring },
    { 'K', RuntimeInputAction::StepDirectorPhase, kKeyboardUnblockedContext | InputBindingContext::DirectorAuthoring },
    { 'L',
      RuntimeInputAction::SaveDirectorShotList,
      kKeyboardUnblockedContext | InputBindingContext::DirectorAuthoring },
    { VK_RETURN,
      RuntimeInputAction::WriteLauncherReproSnapshot,
      kKeyboardUnblockedContext | InputBindingContext::Launcher | InputBindingContext::ReplayRestoreNotConsumed |
          InputBindingContext::DebugOnly },
    { VK_MENU, RuntimeInputAction::ToggleEditorTool, kKeyboardUnblockedContext },
    { 'Z', RuntimeInputAction::UndoEditor, kKeyboardUnblockedContext | InputBindingContext::Editor },
    { 'Y', RuntimeInputAction::RedoEditor, kKeyboardUnblockedContext | InputBindingContext::Editor },
    { VK_DELETE, RuntimeInputAction::DeleteEditorSelection, kKeyboardUnblockedContext | InputBindingContext::Editor },
    { '1', RuntimeInputAction::ToggleWaterFreeze, kKeyboardUnblockedContext },
    { '2', RuntimeInputAction::CycleWaterReflection, kKeyboardUnblockedContext },
    { '3', RuntimeInputAction::ToggleWaterFlat, kKeyboardUnblockedContext },
    { '4', RuntimeInputAction::ToggleTerrainHidden, kKeyboardUnblockedContext },
    { '5', RuntimeInputAction::ToggleWaterHidden, kKeyboardUnblockedContext },
    { 'V', RuntimeInputAction::ToggleCollisionVisualizer, kKeyboardUnblockedContext },
    { 'C', RuntimeInputAction::CyclePhysicsDebugOverlay, kKeyboardUnblockedContext },
    { 'O', RuntimeInputAction::ToggleTerrainContactProbe, kKeyboardUnblockedContext },
    { VK_F7, RuntimeInputAction::StepPhysicsPipelinePrevious, kKeyboardUnblockedContext },
    { VK_F8, RuntimeInputAction::StepPhysicsPipelineNext, kKeyboardUnblockedContext },
    { '6', RuntimeInputAction::TogglePhysicsDebugTransparent, kKeyboardUnblockedContext },
    { 'Q', RuntimeInputAction::ReportRendererRuntimeRetired, kKeyboardUnblockedContext },
    { VK_F9, RuntimeInputAction::ReloadShadersFromSource, kKeyboardUnblockedContext },
    // TEMPORARY DEBUG AUTHORING: delete with RunEditorTracer's replay look explorer.
    { VK_OEM_PERIOD, RuntimeInputAction::CycleReplayRibbonAuthoringLook, kKeyboardUnblockedContext },
    { 'P', RuntimeInputAction::ToggleCrossScenePause, kKeyboardUnblockedContext },
    { 'G', RuntimeInputAction::ToggleBroadphaseOverlay, kKeyboardUnblockedContext },
    { '0', RuntimeInputAction::ToggleUIVisibility, kKeyboardUnblockedContext },
    { VK_F5, RuntimeInputAction::TogglePerformanceHistogram, kKeyboardUnblockedContext },
    { VK_F6, RuntimeInputAction::ToggleMemoryOverlay, kKeyboardUnblockedContext },
    { VK_LEFT, RuntimeInputAction::NavigateScenePrevious, kKeyboardUnblockedContext },
    { VK_RIGHT, RuntimeInputAction::NavigateSceneNext, kKeyboardUnblockedContext },
    { VK_ESCAPE, RuntimeInputAction::DismissOrExitUI, kAfterUIUpdateContext | InputBindingContext::UINotInteracted },
    { VK_F2, RuntimeInputAction::SaveSceneSnapshot, kCaptureContext },
    { VK_F3, RuntimeInputAction::SaveScreenshot, kCaptureContext },
    { 'R', RuntimeInputAction::ResetScene, kAfterUIUpdateContext },
    { VK_BACK, RuntimeInputAction::ResetSceneFromBackspace, kAfterUIUpdateContext | InputBindingContext::Scene } };
constexpr std::size_t kTakeInputKeyboardBindingCount =
    sizeof( kTakeInputKeyboardBindings ) / sizeof( kTakeInputKeyboardBindings[0] );
} // namespace

RuntimeInputKeyBindingView TakeInputKeyboardBindings()
{
    return RuntimeInputKeyBindingView{ kTakeInputKeyboardBindings, kTakeInputKeyboardBindingCount };
}
} // namespace Basics
} // namespace SkullbonezCore
