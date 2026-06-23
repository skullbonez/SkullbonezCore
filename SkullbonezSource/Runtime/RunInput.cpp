/*
File: SkullbonezSource/Runtime/RunInput.cpp
Purpose:
  Routes raw keyboard, mouse, and UI commands into runtime state changes.

Mental model:
  Input arbitration stays here.
  Editor, launcher, and replay behavior live in dedicated runtime files.

Glossary:
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "RunInternal.h"
#include "InputController.h"
#include "RuntimeTuning.h"
#include "../UI/UIInput.h"
#include "../UI/UILayout.h"

#include <cstddef>
#include <cstdint>
#include <utility>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::UI::Layout;
using namespace SkullbonezCore::Basics::RunInternal;

namespace
{
bool CameraModeUsesFlyControls( RunCameraMode mode )
{
    return mode == RunCameraMode::Free || mode == RunCameraMode::Launcher || mode == RunCameraMode::Manipulator;
}


bool CameraModeUsesLauncher( RunCameraMode mode )
{
    return mode == RunCameraMode::Launcher;
}


RuntimeInputModeState BuildRuntimeInputModeState( RunCameraMode mode, const RunEditorPlacementState& editor )
{
    RuntimeInputModeState state;
    state.flyCamera = CameraModeUsesFlyControls( mode );
    state.launcher = CameraModeUsesLauncher( mode );
    state.manipulator = mode == RunCameraMode::Manipulator;
    state.editor = editor.editorModeEnabled;
    state.editorPlacement = editor.placementModeEnabled;
    state.editorViewportLook = editor.viewportLookActive;
    state.editorPlacementScale = editor.placementScaleActive;
    state.editorGizmoDrag = editor.gizmoDragActive;
    state.editorGizmoRotation = editor.gizmoDragIsRotation;
    state.editorGizmoScale = editor.gizmoDragIsScale;
    return state;
}

const char* ReplayRuntimeCommandName( RuntimeCommandType type )
{
    switch ( type )
    {
    case RuntimeCommandType::LoadSceneIndex:
        return "LoadSceneIndex";
    case RuntimeCommandType::LoadDemoScene:
        return "LoadDemoScene";
    case RuntimeCommandType::ResetCurrentScene:
        return "ResetCurrentScene";
    case RuntimeCommandType::CreateScene:
        return "CreateScene";
    case RuntimeCommandType::SaveScreenshot:
        return "SaveScreenshot";
    case RuntimeCommandType::SaveSceneDefaults:
        return "SaveSceneDefaults";
    case RuntimeCommandType::SaveRenderDefaults:
        return "SaveRenderDefaults";
    case RuntimeCommandType::AdvanceScene:
        return "AdvanceScene";
    case RuntimeCommandType::Quit:
        return "Quit";
    case RuntimeCommandType::None:
    default:
        return "None";
    }
}

uint32_t ReplayRuntimeCommandFlags( const RuntimeCommand& command )
{
    uint32_t flags = 0;
    flags |= command.preserveUIState ? 1u : 0u;
    flags |= command.suppressExitOnComplete ? 2u : 0u;
    flags |= command.preserveRuntimeState ? 4u : 0u;
    return flags;
}

struct RuntimeInputKeyBinding
{
    RuntimeInputAction action;
    int virtualKey;
};

void AdvanceTakeInputKeyboardActionMemories( RuntimeInputContext& input )
{
    static const RuntimeInputKeyBinding kBindings[] = { { RuntimeInputAction::ToggleFlyCamera, 'F' },
                                                        { RuntimeInputAction::ToggleLauncher, 'N' },
                                                        { RuntimeInputAction::CycleCameraMode, VK_TAB },
                                                        { RuntimeInputAction::ToggleEditor, VK_OEM_3 },
                                                        { RuntimeInputAction::ToggleEditorTool, VK_MENU },
                                                        { RuntimeInputAction::CycleLauncherFireMode, 'M' },
                                                        { RuntimeInputAction::WriteLauncherReproSnapshot, VK_RETURN },
                                                        { RuntimeInputAction::ToggleWaterFreeze, '1' },
                                                        { RuntimeInputAction::CycleWaterReflection, '2' },
                                                        { RuntimeInputAction::ToggleWaterFlat, '3' },
                                                        { RuntimeInputAction::ToggleTerrainHidden, '4' },
                                                        { RuntimeInputAction::ToggleWaterHidden, '5' },
                                                        { RuntimeInputAction::ToggleCollisionVisualizer, 'V' },
                                                        { RuntimeInputAction::CyclePhysicsDebugOverlay, 'C' },
                                                        { RuntimeInputAction::ToggleTerrainContactProbe, 'O' },
                                                        { RuntimeInputAction::StepPhysicsPipelinePrevious, VK_F7 },
                                                        { RuntimeInputAction::StepPhysicsPipelineNext, VK_F8 },
                                                        { RuntimeInputAction::TogglePhysicsDebugTransparent, '6' },
                                                        { RuntimeInputAction::ReportRendererRuntimeRetired, 'Q' },
                                                        { RuntimeInputAction::ToggleBroadphaseOverlay, 'G' },
                                                        { RuntimeInputAction::ToggleUIVisibility, '0' },
                                                        { RuntimeInputAction::NavigateScenePrevious, VK_LEFT },
                                                        { RuntimeInputAction::NavigateSceneNext, VK_RIGHT },
                                                        { RuntimeInputAction::DismissOrExitUI, VK_ESCAPE },
                                                        { RuntimeInputAction::SaveSceneSnapshot, VK_F2 },
                                                        { RuntimeInputAction::SaveScreenshot, VK_F3 },
                                                        { RuntimeInputAction::ResetScene, 'R' },
                                                        { RuntimeInputAction::ResetSceneFromBackspace, VK_BACK } };

    for ( std::size_t i = 0; i < sizeof( kBindings ) / sizeof( kBindings[0] ); ++i )
    {
        input.SetActionDown( kBindings[i].action, Input::IsKeyDown( kBindings[i].virtualKey ) );
    }
}

} // namespace

void Run::StepPhysicsPipelineStage( int direction )
{
    const int stageCount = static_cast<int>( PhysicsPipelineStage::Count );
    if ( stageCount <= 0 || direction == 0 )
    {
        return;
    }

    m_debug.physicsDebugFlags |= PHYSICS_DEBUG_PIPELINE;
    int nextStage = ( m_debug.physicsDebugPipelineStageCursor + direction ) % stageCount;
    if ( nextStage < 0 )
    {
        nextStage += stageCount;
    }
    m_debug.physicsDebugPipelineStageCursor = nextStage;
}


void Run::UpdateRuntimeInputModeAfterAction( RuntimeInputAction action, RuntimeInputActionSource source )
{
    InputController::ApplyModeAction(
        m_runtimeInput,
        InputController::ResolveMode( BuildRuntimeInputModeState( m_camera.mode, m_editor ) ),
        action,
        source );
}


const char* Run::CameraModeLabel( RunCameraMode mode ) const
{
    switch ( mode )
    {
    case RunCameraMode::Demo:
        return "Demo";
    case RunCameraMode::Scene:
        return "Scene";
    case RunCameraMode::Free:
        return "Free";
    case RunCameraMode::Launcher:
        return "Launcher";
    case RunCameraMode::Manipulator:
        return "Manipulator";
    default:
        return "Unknown";
    }
}


bool Run::IsDemoCameraModeAvailable() const
{
    if ( SceneState().isSceneMode )
    {
        return false;
    }
    return m_cGameModelCollection.GetModelCount() > 0;
}


RunCameraMode Run::NormalizeCameraModeForCurrentScene( RunCameraMode mode ) const
{
    if ( SceneState().isSceneMode )
    {
        return mode == RunCameraMode::Demo ? RunCameraMode::Scene : mode;
    }
    if ( mode == RunCameraMode::Scene )
    {
        return IsDemoCameraModeAvailable() ? RunCameraMode::Demo : RunCameraMode::Free;
    }
    if ( mode == RunCameraMode::Demo && !IsDemoCameraModeAvailable() )
    {
        return RunCameraMode::Free;
    }
    return mode;
}


bool Run::IsFlyCameraMode() const
{
    return CameraModeUsesFlyControls( m_camera.mode );
}


bool Run::IsLauncherCameraMode() const
{
    return CameraModeUsesLauncher( m_camera.mode );
}


bool Run::IsManipulatorCameraMode() const
{
    return m_camera.mode == RunCameraMode::Manipulator;
}


uint32_t Run::CameraModeEnabledMask() const
{
    uint32_t mask = 0;
    if ( IsDemoCameraModeAvailable() )
    {
        mask |= 1u << static_cast<int>( RunCameraMode::Demo );
    }
    if ( SceneState().isSceneMode )
    {
        mask |= 1u << static_cast<int>( RunCameraMode::Scene );
    }
    mask |= 1u << static_cast<int>( RunCameraMode::Free );
    mask |= 1u << static_cast<int>( RunCameraMode::Launcher );
    mask |= 1u << static_cast<int>( RunCameraMode::Manipulator );
    return mask;
}


void Run::ApplyCameraMode( RunCameraMode mode, RuntimeInputActionSource source )
{
    const int modeIndex = static_cast<int>( mode );
    if ( modeIndex < 0 || modeIndex >= static_cast<int>( RunCameraMode::Count ) )
    {
        return;
    }
    mode = NormalizeCameraModeForCurrentScene( mode );

    if ( mode == RunCameraMode::Demo )
    {
        const int modelCount = m_cGameModelCollection.GetModelCount();
        if ( m_camera.trackBallIndex < 0 || m_camera.trackBallIndex >= modelCount )
        {
            m_camera.trackBallIndex = 0;
        }
        if ( m_camera.trackHeight <= 0.0f )
        {
            m_camera.trackHeight = 300.0f;
        }
    }

    const bool wasFlyMode = IsFlyCameraMode();
    m_camera.mode = mode;
    if ( m_editor.editorModeEnabled )
    {
        m_editor.restoreCameraModeAfterEditor = mode;
    }
    if ( mode != RunCameraMode::Manipulator )
    {
        CancelMousePickup();
    }

    const bool isFlyMode = IsFlyCameraMode();
    if ( wasFlyMode != isFlyMode )
    {
        if ( isFlyMode )
        {
            EnterFlyModeCamera();
        }
        else
        {
            ExitFlyModeCamera();
        }
    }
    else
    {
        InputController::ResetMouseLook( m_camera );
        ApplyCursorOwnership();
    }
    UpdateRuntimeInputModeAfterAction( source == RuntimeInputActionSource::UI ? RuntimeInputAction::SetCameraMode
                                                                              : RuntimeInputAction::CycleCameraMode,
                                       source );
}


void Run::CycleCameraMode()
{
    const uint32_t enabledMask = CameraModeEnabledMask();
    int current = static_cast<int>( m_camera.mode );
    if ( current < 0 || current >= static_cast<int>( RunCameraMode::Count ) )
    {
        current = static_cast<int>( SceneState().isSceneMode ? RunCameraMode::Scene : RunCameraMode::Demo );
    }

    for ( int step = 1; step <= static_cast<int>( RunCameraMode::Count ); ++step )
    {
        const int next = ( current + step ) % static_cast<int>( RunCameraMode::Count );
        if ( ( enabledMask & ( 1u << next ) ) != 0 )
        {
            ApplyCameraMode( static_cast<RunCameraMode>( next ), RuntimeInputActionSource::Keyboard );
            return;
        }
    }
}


bool Run::ReplayInspectionActive() const
{
    return m_replayCamera.active || m_replayScrubber.paused || m_replayScrubber.simulationPaused;
}


bool Run::ReplayInspectionMouseLookActive() const
{
    return ReplayInspectionActive() && Input::IsRightMouseDown() && !m_UI.WantsNativeMouseCursor() &&
           !m_UI.BlocksCameraMouse();
}


bool Run::MouseLookOwnsCursor() const
{
    if ( m_UI.WantsNativeMouseCursor() || m_UI.BlocksCameraMouse() )
    {
        return false;
    }

    if ( m_editor.editorModeEnabled )
    {
        return m_editor.viewportLookActive;
    }

    if ( ReplayInspectionActive() )
    {
        return ReplayInspectionMouseLookActive();
    }

    return IsFlyCameraMode() && m_camera.mode != RunCameraMode::Manipulator;
}


bool Run::ShouldHideNativeCursor() const
{
    if ( MouseLookOwnsCursor() )
    {
        return true;
    }

    return m_editor.editorModeEnabled && m_editor.placementModeEnabled && m_editor.placementPreviewVisible &&
           !m_UI.WantsNativeMouseCursor() && !m_UI.BlocksCameraMouse();
}


void Run::ApplyCursorOwnership()
{
    Input::SetSystemCursorVisible( !ShouldHideNativeCursor() );
}


void Run::ReleaseMouseToUI()
{
    if ( !MouseLookOwnsCursor() )
    {
        ReleaseCapture();
        InputController::ResetMouseLook( m_camera );
    }
}


void Run::EnterFlyModeCamera()
{
    // Entering fly mode: generated demo mode snaps to free camera; scene mode stays
    // on the current camera so fly controls work without requiring CAMERA_FREE
    if ( !SceneState().isSceneMode )
    {
        m_systems.cameras->SelectCamera( CAMERA_FREE, false );
    }
    m_camera.cameraTime = 0.0f;
    XZBounds unbounded;
    unbounded.m_xMin = -99999.9f;
    unbounded.m_xMax = 99999.9f;
    unbounded.m_zMin = -99999.9f;
    unbounded.m_zMax = 99999.9f;
    uint32_t activeCam = SceneState().isSceneMode ? m_systems.cameras->GetSelectedCameraName() : CAMERA_FREE;
    m_systems.cameras->SetCameraXZBounds( activeCam, unbounded );
    if ( ShouldHideNativeCursor() )
    {
        Input::SetSystemCursorVisible( false );
    }
    else
    {
        ReleaseMouseToUI();
        Input::SetSystemCursorVisible( true );
    }
    InputController::ResetMouseLook( m_camera );
}


void Run::ExitFlyModeCamera()
{
    // Exiting fly mode restores terrain bounds, the camera-cycle clock, and
    // the stock Windows cursor.
    uint32_t activeCam = SceneState().isSceneMode ? m_systems.cameras->GetSelectedCameraName() : CAMERA_FREE;
    m_systems.cameras->SetCameraXZBounds( activeCam, m_systems.terrain->GetXZBounds() );
    Input::SetSystemCursorVisible( true );
    m_camera.cameraTime = 0.0f;
    InputController::ResetMouseLook( m_camera );
}


void Run::TakeInput()
{
    if ( !Input::IsAppFocused() )
    {
        Input::SetSystemCursorVisible( true );
        if ( m_replayScrubber.mouseCaptured )
        {
            UI::InputControl::EndMouseCapture();
        }
        ResetReplayScrubber();
        m_replayPrediction.checkboxHovered = false;
        m_replayPrediction.decreaseHovered = false;
        m_replayPrediction.increaseHovered = false;
        m_replayPrediction.horizonHovered = false;
        m_replayPrediction.horizonDragging = false;
        m_replayVelocityEdit.toggleHovered = false;
        m_replayVelocityEdit.keyboardAltWasDown = false;
        m_replayVelocityEdit.dragging = false;
        m_replayVelocityEdit.draggingAngular = false;
        m_replayVelocityEdit.activeAxis = -1;
        m_replayVelocityEdit.hotLinearAxis = -1;
        m_replayVelocityEdit.hotAngularAxis = -1;
        if ( m_replayVelocityEdit.mouseCaptured )
        {
            UI::InputControl::EndMouseCapture();
            m_replayVelocityEdit.mouseCaptured = false;
        }
        CancelMousePickup();
        if ( m_replayCauseTree.draggingWindow || m_replayCauseTree.resizingWindow )
        {
            UI::InputControl::EndMouseCapture();
            m_replayCauseTree.draggingWindow = false;
            m_replayCauseTree.resizingWindow = false;
        }
        ResetEditorUnfocusedInputState();
        InputController::ResetUnfocusedInput( m_camera, m_leftSceneCycleWasDown, m_rightSceneCycleWasDown );
        m_runtimeInput.ResetEdges();
        InputController::BeginFrame( m_runtimeInput,
                                     BuildRuntimeInputModeState( m_camera.mode, m_editor ),
                                     false,
                                     true,
                                     true );
        m_UI.CancelInputCapture();
        RunUIStressActions();
        return;
    }

    ApplyCursorOwnership();

    const bool UIBlocksKeyboardBeforeInput = m_UI.BlocksKeyboard();
    InputController::BeginFrame( m_runtimeInput,
                                 BuildRuntimeInputModeState( m_camera.mode, m_editor ),
                                 true,
                                 UIBlocksKeyboardBeforeInput,
                                 m_UI.BlocksCameraMouse() );
    bool keyboardToggleEditorMode = false;
    if ( !UIBlocksKeyboardBeforeInput )
    {
        keyboardToggleEditorMode =
            InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::ToggleEditor, VK_OEM_3 );

        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                          RuntimeInputAction::CycleCameraMode,
                                                          VK_TAB ) )
        {
            CycleCameraMode();
        }

        // Compatibility shortcut: F enters Free, or returns to the passive camera mode when already free.
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::ToggleFlyCamera, 'F' ) )
        {
            const RunCameraMode passiveMode = SceneState().isSceneMode ? RunCameraMode::Scene : RunCameraMode::Demo;
            ApplyCameraMode( m_camera.mode == RunCameraMode::Free ? passiveMode : RunCameraMode::Free,
                             RuntimeInputActionSource::Keyboard );
        }

        // Compatibility shortcut: N toggles launcher view with live simulation.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::ToggleLauncher,
                                                              'N' ) )
            {
                ApplyCameraMode(
                    m_camera.mode == RunCameraMode::Launcher ? RunCameraMode::Free : RunCameraMode::Launcher,
                    RuntimeInputActionSource::Keyboard );
            }
        }

        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::CycleLauncherFireMode,
                                                              'M' ) &&
                 IsLauncherCameraMode() )
            {
                m_rayCastTest.fireMode = m_rayCastTest.fireMode == RunLauncherFireMode::Laser
                                             ? RunLauncherFireMode::Projectile
                                             : RunLauncherFireMode::Laser;
            }
        }

#ifdef _DEBUG
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::WriteLauncherReproSnapshot,
                                                              VK_RETURN ) &&
                 IsLauncherCameraMode() && !m_replayScrubber.restoreConsumedThisFrame )
            {
                WriteLauncherReproSnapshot();
            }
        }
#endif

        if ( m_editor.editorModeEnabled )
        {
            HandleEditorKeyboardShortcuts();
        }
        else
        {
            const bool altDown = Input::IsKeyDown( VK_MENU );
            if ( altDown && !m_replayVelocityEdit.keyboardAltWasDown )
            {
                SetReplayVelocityEditEnabled( !m_replayVelocityEdit.enabled );
            }
            m_replayVelocityEdit.keyboardAltWasDown = altDown;
            m_runtimeInput.SetActionDown( RuntimeInputAction::ToggleEditorTool, altDown );
            m_runtimeInput.SetActionDown( RuntimeInputAction::CycleCameraMode, Input::IsKeyDown( VK_TAB ) );
            m_editor.altShortcutWasDown = altDown;
            m_editor.tabShortcutWasDown = Input::IsKeyDown( VK_TAB );
        }

        // Water m_shader debug toggles
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::ToggleWaterFreeze, '1' ) )
        {
            m_debug.isWaterFreezeDebug = !m_debug.isWaterFreezeDebug;
            if ( m_debug.isWaterFreezeDebug )
            {
                m_debug.frozenWaterTime = static_cast<float>( m_timers.simulationTimer.GetTimeSinceLastStart() );
            }
        }
        // Key '2' cycles water reflection modes in a predictable loop:
        // FBO mirror rendering, then DXR raytraced reflection when supported,
        // then no reflection, then back to FBO. Machines without DXR skip the
        // unsupported mode instead of leaving the toggle in a dead state.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::CycleWaterReflection,
                                                              '2' ) )
            {
                if ( !m_debug.isWaterRTReflect && !m_debug.isWaterNoReflect )
                {
                    if ( Gfx().GetCapabilities().supportsDxrReflection )
                    {
                        m_debug.isWaterRTReflect = true;
                    }
                    else
                    {
                        m_debug.isWaterNoReflect = true;
                    }
                }
                else if ( m_debug.isWaterRTReflect )
                {
                    m_debug.isWaterRTReflect = false;
                    m_debug.isWaterNoReflect = true;
                }
                else
                {
                    m_debug.isWaterNoReflect = false;
                }
            }
        }
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::ToggleWaterFlat,
                                                              '3' ) )
            {
                m_debug.isWaterFlatDebug = !m_debug.isWaterFlatDebug;
            }
        }
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::ToggleTerrainHidden,
                                                              '4' ) )
            {
                m_debug.isTerrainHidden = !m_debug.isTerrainHidden;
            }
        }
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::ToggleWaterHidden,
                                                              '5' ) )
            {
                m_debug.isWaterHidden = !m_debug.isWaterHidden;
            }
        }
        // V key: collision visualizer for balls and boxes as solid debug colours.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::ToggleCollisionVisualizer,
                                                              'V' ) )
            {
                m_debug.isCollisionVisualizer = !m_debug.isCollisionVisualizer;
            }
        }

        // C key: cycle physics debug overlay - None -> Axes -> Contacts -> Sleep -> All -> None.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::CyclePhysicsDebugOverlay,
                                                              'C' ) )
            {
                switch ( m_debug.physicsDebugFlags )
                {
                case PHYSICS_DEBUG_NONE:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_AXES;
                    break;
                case PHYSICS_DEBUG_AXES:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_CONTACTS;
                    break;
                case PHYSICS_DEBUG_CONTACTS:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_SLEEP;
                    break;
                case PHYSICS_DEBUG_SLEEP:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_ALL;
                    break;
                default:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_NONE;
                    break;
                }
            }
        }

        // O key: toggle the terrain polygon/contact probe. It is independent of
        // the C-key debug cycle so it can be layered over any other physics view.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::ToggleTerrainContactProbe,
                                                              'O' ) )
            {
                m_debug.physicsDebugFlags ^= PHYSICS_DEBUG_TERRAIN_CONTACT;
            }
        }

        // F7/F8: step the physics pipeline visualizer through the bounded Catto
        // stage trace from the most recent physics tick. The simulation can be
        // paused with fly mode and advanced separately with Space.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::StepPhysicsPipelinePrevious,
                                                              VK_F7 ) )
            {
                StepPhysicsPipelineStage( -1 );
            }
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::StepPhysicsPipelineNext,
                                                              VK_F8 ) )
            {
                StepPhysicsPipelineStage( 1 );
            }
        }

        // 6 key: translucent debug collision volumes for inspecting axes/contact rows inside bodies.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::TogglePhysicsDebugTransparent,
                                                              '6' ) )
            {
                m_debug.isPhysicsDebugTransparent = !m_debug.isPhysicsDebugTransparent;
            }
        }

        // Q key used to cycle legacy renderers; it now reports that DX12 is the only runtime renderer.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::ReportRendererRuntimeRetired,
                                                              'Q' ) )
            {
                fprintf( stderr, "Renderer switch ignored: DX12 is the only runtime renderer.\n" );
            }
        }

        // G key: toggle broadphase overlay, or cycle tracked ball if overlay is off.
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                          RuntimeInputAction::ToggleBroadphaseOverlay,
                                                          'G' ) )
        {
            if ( SceneState().isSceneMode && m_camera.trackBallIndex >= 0 && !m_debug.isBroadphaseOverlay )
            {
                int count = m_cGameModelCollection.GetModelCount();
                if ( count > 0 )
                {
                    m_camera.trackBallIndex = ( m_camera.trackBallIndex + 1 ) % count;
                }
            }
            else
            {
                m_debug.isBroadphaseOverlay = !m_debug.isBroadphaseOverlay;
            }
        }

        // 0 key: toggle the in-game diagnostics window. Tabs replace the old overlay cycle.
        // Edge-detected in both scene and generated demo modes; one toggle per keypress.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::ToggleUIVisibility,
                                                              '0' ) )
            {
                EnterInteractiveSceneRun();
                m_UI.ToggleVisible( m_timers.simulationTimer.GetTotalTime() );
                m_debug.overlayMode = OverlayMode::None;
                ApplyCursorOwnership();
                ReleaseMouseToUI();
                UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleUIVisibility,
                                                   RuntimeInputActionSource::Keyboard );
            }
        }

        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                          RuntimeInputAction::NavigateScenePrevious,
                                                          VK_LEFT ) )
        {
            EnterInteractiveSceneRun();
            if ( !ApplyAdjacentCinematicMode( -1 ) )
            {
                LoadAdjacentSceneFromBrowser( -1 );
            }
        }
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                          RuntimeInputAction::NavigateSceneNext,
                                                          VK_RIGHT ) )
        {
            EnterInteractiveSceneRun();
            if ( !ApplyAdjacentCinematicMode( 1 ) )
            {
                LoadAdjacentSceneFromBrowser( 1 );
            }
        }
    }
    else
    {
        AdvanceTakeInputKeyboardActionMemories( m_runtimeInput );
        m_leftSceneCycleWasDown = Input::IsKeyDown( VK_LEFT );
        m_rightSceneCycleWasDown = Input::IsKeyDown( VK_RIGHT );
        m_replayVelocityEdit.keyboardAltWasDown = Input::IsKeyDown( VK_MENU );
        m_editor.altShortcutWasDown = Input::IsKeyDown( VK_MENU );
        m_editor.tabShortcutWasDown = Input::IsKeyDown( VK_TAB );
        m_editor.tildeShortcutWasDown = Input::IsKeyDown( VK_OEM_3 );
    }

    bool suppressWorldActionThisFrame = UIBlocksKeyboardBeforeInput;
    int editorUnhandledWheelDelta = 0;
    if ( m_systems.window )
    {
        const int selectedSceneBrowserIndex = CurrentSceneBrowserIndex();
        InGameUIInputResult UIResult =
            m_UI.UpdateInput( m_systems.window->m_sWindow,
                              static_cast<int>( m_systems.window->m_sWindowDimensions.x ),
                              static_cast<int>( m_systems.window->m_sWindowDimensions.y ),
                              m_timers.simulationTimer.GetTotalTime(),
                              m_editor.editorModeEnabled,
                              m_editor.placementModeEnabled,
                              m_editor.placeStaticObject,
                              m_editor.autoTerrainAlign,
                              m_editor.objectType,
                              static_cast<int>( m_camera.mode ),
                              CameraModeEnabledMask(),
                              m_sceneBrowserNamePtrs.empty() ? nullptr : m_sceneBrowserNamePtrs.data(),
                              static_cast<int>( m_sceneBrowserNamePtrs.size() ),
                              selectedSceneBrowserIndex );
        editorUnhandledWheelDelta = UIResult.unhandledWheelDelta;
        const InGameUICommands& uiCommands = UIResult.commands;
        if ( uiCommands.ui.userInteracted )
        {
            EnterInteractiveSceneRun();
        }
        suppressWorldActionThisFrame =
            suppressWorldActionThisFrame || uiCommands.ui.userInteracted || m_UI.BlocksCameraMouse();
        const bool replayScrubberOwnsMouse =
            TickReplayScrubberInput( m_systems.window->m_sWindow, m_UI.BlocksCameraMouse() );
        const bool replayCauseTreeOwnsMouse =
            TickReplayCauseTreeInput( m_systems.window->m_sWindow,
                                      m_UI.BlocksCameraMouse() || replayScrubberOwnsMouse,
                                      editorUnhandledWheelDelta );
        const bool replayVelocityEditOwnsMouse = TickReplayVelocityEditInput(
            m_systems.window->m_sWindow,
            m_UI.BlocksCameraMouse() || replayScrubberOwnsMouse || replayCauseTreeOwnsMouse );
        suppressWorldActionThisFrame = suppressWorldActionThisFrame || replayScrubberOwnsMouse ||
                                       replayCauseTreeOwnsMouse || replayVelocityEditOwnsMouse;
        m_runtimeInput.BeginFrame( true,
                                   m_UI.BlocksKeyboard(),
                                   m_UI.BlocksCameraMouse() || replayScrubberOwnsMouse || replayCauseTreeOwnsMouse ||
                                       replayVelocityEditOwnsMouse );

        // ESC flicks the diagnostics window between minimized and expanded, with
        // a very fast double-tap escape hatch for quitting interactive runs.
        // Run it after UI input processing so focused controls keep their local ESC
        // behavior first, such as closing the scene filter combo without also
        // hiding the whole diagnostics surface on the same frame.
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                          RuntimeInputAction::DismissOrExitUI,
                                                          VK_ESCAPE ) &&
             !uiCommands.ui.userInteracted )
        {
            constexpr double ESC_QUICK_EXIT_SECONDS = 0.32;
            const double UINow = m_timers.simulationTimer.GetTotalTime();
            if ( UINow - m_lastEscapeTapTime <= ESC_QUICK_EXIT_SECONDS )
            {
                PostQuitMessage( 0 );
            }
            else
            {
                EnterInteractiveSceneRun();
                m_UI.ToggleVisible( UINow );
                m_debug.overlayMode = OverlayMode::None;
                m_lastEscapeTapTime = UINow;
                ApplyCursorOwnership();
                ReleaseMouseToUI();
            }
        }

        if ( uiCommands.renderer.toggleVsync )
        {
            m_runtimeSettings.isVsyncEnabled = !m_runtimeSettings.isVsyncEnabled;
            Gfx().SetVsyncEnabled( m_runtimeSettings.isVsyncEnabled );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleVsync, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.run.requestedCameraMode >= 0 &&
             uiCommands.run.requestedCameraMode < static_cast<int>( RunCameraMode::Count ) )
        {
            ApplyCameraMode( static_cast<RunCameraMode>( uiCommands.run.requestedCameraMode ),
                             RuntimeInputActionSource::UI );
        }
        ApplyEditorUICommands( uiCommands, keyboardToggleEditorMode );
        if ( uiCommands.physics.toggleCollisionVisualizer )
        {
            m_debug.isCollisionVisualizer = !m_debug.isCollisionVisualizer;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleCollisionVisualizer,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.togglePhysicsSleepPolicy )
        {
            m_runtimeSettings.isPhysicsSleepEnabled = !m_runtimeSettings.isPhysicsSleepEnabled;
            m_cGameModelCollection.SetPhysicsSleepEnabled( m_runtimeSettings.isPhysicsSleepEnabled );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::TogglePhysicsSleepPolicy,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.togglePhysicsDebugFlags != 0 )
        {
            m_debug.physicsDebugFlags ^= ( uiCommands.physics.togglePhysicsDebugFlags & PHYSICS_DEBUG_ALL );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::TogglePhysicsDebugFlags,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.stepPhysicsPipelinePrevious )
        {
            StepPhysicsPipelineStage( -1 );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::StepPhysicsPipelinePrevious,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.stepPhysicsPipelineNext )
        {
            StepPhysicsPipelineStage( 1 );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::StepPhysicsPipelineNext,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.togglePhysicsDebugTransparent )
        {
            m_debug.isPhysicsDebugTransparent = !m_debug.isPhysicsDebugTransparent;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::TogglePhysicsDebugTransparent,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.toggleBroadphaseOverlay )
        {
            m_debug.isBroadphaseOverlay = !m_debug.isBroadphaseOverlay;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleBroadphaseOverlay,
                                               RuntimeInputActionSource::UI );
        }
        bool tornadoFieldChanged = false;
        const bool hasTornadoSystem = !m_runtimeSettings.tornadoSystem.vortices.empty();
        const auto applyTornadoFieldValue = [&]( float TornadoFieldConfig::* field, float value )
        {
            if ( hasTornadoSystem )
            {
                for ( TornadoVortexConfig& vortex : m_runtimeSettings.tornadoSystem.vortices )
                {
                    vortex.field.*field = value;
                }
            }
            else
            {
                m_runtimeSettings.tornadoField.*field = value;
            }
        };
        if ( uiCommands.physics.toggleTornado )
        {
            bool tornadoEnabled = false;
            if ( hasTornadoSystem )
            {
                m_runtimeSettings.tornadoSystem.enabled = !m_runtimeSettings.tornadoSystem.enabled;
                tornadoEnabled = m_runtimeSettings.tornadoSystem.enabled;
            }
            else
            {
                m_runtimeSettings.tornadoField.enabled = !m_runtimeSettings.tornadoField.enabled;
                tornadoEnabled = m_runtimeSettings.tornadoField.enabled;
            }
            if ( m_runtimeSettings.tornadoVisual.autoEnableWithTornado )
            {
                m_runtimeSettings.tornadoVisual.enabled = tornadoEnabled;
            }
            tornadoFieldChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleTornado, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.toggleTornadoVisualShell )
        {
            m_runtimeSettings.tornadoVisual.enabled = !m_runtimeSettings.tornadoVisual.enabled;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleTornadoVisualShell,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.toggleTornadoFieldVectors )
        {
            if ( hasTornadoSystem )
            {
                m_runtimeSettings.tornadoSystem.visualizeVelocityField =
                    !m_runtimeSettings.tornadoSystem.visualizeVelocityField;
            }
            else
            {
                m_runtimeSettings.tornadoField.visualizeVelocityField =
                    !m_runtimeSettings.tornadoField.visualizeVelocityField;
            }
            tornadoFieldChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleTornadoFieldVectors,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.toggleRayCastVisualization )
        {
            m_rayCastTest.visualizeRays = !m_rayCastTest.visualizeRays;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleRayCastVisualization,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestTornadoRadius )
        {
            applyTornadoFieldValue(
                &TornadoFieldConfig::radius,
                std::clamp( uiCommands.physics.requestedTornadoRadius, UI_TORNADO_RADIUS_MIN, UI_TORNADO_RADIUS_MAX ) );
            tornadoFieldChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyTornadoSettings, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestTornadoHeight )
        {
            applyTornadoFieldValue(
                &TornadoFieldConfig::height,
                std::clamp( uiCommands.physics.requestedTornadoHeight, UI_TORNADO_HEIGHT_MIN, UI_TORNADO_HEIGHT_MAX ) );
            tornadoFieldChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyTornadoSettings, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestTornadoInward )
        {
            applyTornadoFieldValue(
                &TornadoFieldConfig::inwardAcceleration,
                std::clamp( uiCommands.physics.requestedTornadoInward, UI_TORNADO_INWARD_MIN, UI_TORNADO_INWARD_MAX ) );
            tornadoFieldChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyTornadoSettings, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestTornadoSwirl )
        {
            applyTornadoFieldValue(
                &TornadoFieldConfig::swirlAcceleration,
                std::clamp( uiCommands.physics.requestedTornadoSwirl, UI_TORNADO_SWIRL_MIN, UI_TORNADO_SWIRL_MAX ) );
            tornadoFieldChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyTornadoSettings, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestTornadoLift )
        {
            applyTornadoFieldValue(
                &TornadoFieldConfig::liftAcceleration,
                std::clamp( uiCommands.physics.requestedTornadoLift, UI_TORNADO_LIFT_MIN, UI_TORNADO_LIFT_MAX ) );
            tornadoFieldChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyTornadoSettings, RuntimeInputActionSource::UI );
        }
        if ( tornadoFieldChanged )
        {
            SyncTornadoFieldToPhysics();
        }
        if ( uiCommands.physics.toggleTerrainContactProbe )
        {
            m_debug.physicsDebugFlags ^= PHYSICS_DEBUG_TERRAIN_CONTACT;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleTerrainContactProbe,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.toggleTextOnly )
        {
            m_debug.isTextOnly = !m_debug.isTextOnly;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleTextOnly, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.toggleFixedStep )
        {
            SceneState().isFixedStep = !SceneState().isFixedStep;
            m_simulation.Reset();
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleFixedStep, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.toggleTerrainHidden )
        {
            m_debug.isTerrainHidden = !m_debug.isTerrainHidden;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleTerrainHidden, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.toggleWaterHidden )
        {
            m_debug.isWaterHidden = !m_debug.isWaterHidden;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleWaterHidden, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.toggleWaterFreeze )
        {
            m_debug.isWaterFreezeDebug = !m_debug.isWaterFreezeDebug;
            if ( m_debug.isWaterFreezeDebug )
            {
                m_debug.frozenWaterTime = static_cast<float>( m_timers.simulationTimer.GetTimeSinceLastStart() );
            }
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleWaterFreeze, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.toggleWaterFlat )
        {
            m_debug.isWaterFlatDebug = !m_debug.isWaterFlatDebug;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleWaterFlat, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.toggleShadows )
        {
            if ( IsCinematicRenderingEnabled() )
            {
                const bool shadowsActive = ActiveCinematicConfig().shadowsEnabled;
                m_cmdHasCinematicShadowsOverride = false;
                SetCinematicShadowsEnabledFromUI( ActiveCinematicConfig(), SceneState(), !shadowsActive );
            }
            else
            {
                Cfg().ordinaryRender.shadowsEnabled = !Cfg().ordinaryRender.shadowsEnabled;
            }
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleShadows, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.renderTuning.toggleShadows )
        {
            Cfg().ordinaryRender.shadowsEnabled = !Cfg().ordinaryRender.shadowsEnabled;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleRenderShadows, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.renderTuning.saveDefaults )
        {
            m_runtimeCommands.Push( RuntimeCommand{ RuntimeCommandType::SaveRenderDefaults } );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SaveRenderDefaults, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.renderTuning.requestedParam != UIRenderParam::None )
        {
            ApplyOrdinaryRenderUIParam( Cfg().ordinaryRender,
                                        uiCommands.renderTuning.requestedParam,
                                        uiCommands.renderTuning.requestedValue );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyRenderTuning, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.water.toggleWaterReflection )
        {
            if ( m_debug.isWaterNoReflect )
            {
                m_debug.isWaterNoReflect = false;
            }
            else
            {
                m_debug.isWaterNoReflect = true;
                m_debug.isWaterRTReflect = false;
            }
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleWaterReflection,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.water.requestedWaterReflectionMode >= 0 )
        {
            const int mode = std::clamp( uiCommands.water.requestedWaterReflectionMode, 0, 2 );
            m_debug.isWaterRTReflect = mode == 1;
            m_debug.isWaterNoReflect = mode == 2;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetWaterReflectionMode,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.requestedTimeScale > 0.0f )
        {
            m_UITimeScaleOverride = std::clamp( uiCommands.sceneOptions.requestedTimeScale, 0.10f, 10.00f );
            SceneState().timeScale = m_UITimeScaleOverride;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetTimeScale, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.run.requestedSeed > 0 )
        {
            SceneState().rngSeed = static_cast<unsigned int>( std::clamp( uiCommands.run.requestedSeed, 1, 999999 ) );
            SceneState().rngState = SceneState().rngSeed;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetRunSeed, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestedPhysicsDebugAlpha >= 0.0f )
        {
            m_debug.physicsDebugAlpha = std::clamp( uiCommands.physics.requestedPhysicsDebugAlpha, 0.05f, 1.0f );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetPhysicsDebugAlpha, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestedPhysicsDebugContactLinger >= 0.0f )
        {
            m_debug.physicsDebugContactLinger =
                std::clamp( uiCommands.physics.requestedPhysicsDebugContactLinger, 0.0f, 5.0f );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetPhysicsDebugContactLinger,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestRayCastImpulseStrength )
        {
            const float previousImpulse = m_rayCastTest.impulseStrength;
            m_rayCastTest.impulseStrength = std::clamp( uiCommands.physics.requestedRayCastImpulseStrength,
                                                        UI_RAY_IMPULSE_MIN,
                                                        UI_RAY_IMPULSE_MAX );
            RecordReplayLauncherConfigEvent( previousImpulse != m_rayCastTest.impulseStrength ? 1u : 0u );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetRayCastImpulseStrength,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestLauncherProjectileSpeed )
        {
            const float previousProjectileSpeed = m_rayCastTest.projectileSpeed;
            m_rayCastTest.projectileSpeed = std::clamp( uiCommands.physics.requestedLauncherProjectileSpeed,
                                                        UI_LAUNCHER_PROJECTILE_SPEED_MIN,
                                                        UI_LAUNCHER_PROJECTILE_SPEED_MAX );
            RecordReplayLauncherConfigEvent( previousProjectileSpeed != m_rayCastTest.projectileSpeed ? 2u : 0u );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetLauncherProjectileSpeed,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.requestedModelCount >= 0 )
        {
            ApplyUIModelCountOverride( uiCommands.sceneOptions.requestedModelCount );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetModelCount, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.profiler.requestedWorkerThreads >= -1 )
        {
            ApplyWorkerThreadCountOverride( uiCommands.profiler.requestedWorkerThreads );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetWorkerThreads, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.run.requestedSolverBallCount >= 0 )
        {
            const int modelCapacity = ActiveGameModelCapacity();
            const int boxes =
                m_UISolverBoxCountOverride >= 0 ? m_UISolverBoxCountOverride : SceneState().solverBoxCount;
            ApplyUISolverObjectCounts(
                std::clamp( uiCommands.run.requestedSolverBallCount, 0, (std::max)( 0, modelCapacity - boxes ) ),
                boxes );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetSolverCounts, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.run.requestedSolverBoxCount >= 0 )
        {
            const int modelCapacity = ActiveGameModelCapacity();
            const int balls =
                m_UISolverBallCountOverride >= 0 ? m_UISolverBallCountOverride : SceneState().solverBallCount;
            ApplyUISolverObjectCounts(
                balls,
                std::clamp( uiCommands.run.requestedSolverBoxCount, 0, (std::max)( 0, modelCapacity - balls ) ) );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetSolverCounts, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.water.requestWorldGravity || uiCommands.water.requestWorldFluidHeight ||
             uiCommands.water.requestWorldFluidDensity )
        {
            const float gravity = uiCommands.water.requestWorldGravity ? uiCommands.water.requestedWorldGravity
                                                                       : m_cWorldEnvironment.GetGravity();
            const float fluidHeight = uiCommands.water.requestWorldFluidHeight
                                          ? uiCommands.water.requestedWorldFluidHeight
                                          : m_cWorldEnvironment.GetFluidSurfaceHeight();
            const float fluidDensity = uiCommands.water.requestWorldFluidDensity
                                           ? uiCommands.water.requestedWorldFluidDensity
                                           : m_cWorldEnvironment.GetFluidDensity();
            ApplyUIWorldOverride( std::clamp( gravity, -100.0f, 0.0f ),
                                  std::clamp( fluidHeight, -100.0f, 200.0f ),
                                  std::clamp( fluidDensity, 0.0f, 5.0f ) );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyWorldWaterSettings,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.cinematic.toggleRendering )
        {
            // Master Cine switch. Clearing m_cmdHasCinematicRenderingOverride lets
            // the runtime toggle become the new source of truth after launch.
            CinematicRenderConfig& cinematic = ActiveCinematicConfig();
            const bool currentlyEnabled =
                m_cmdHasCinematicRenderingOverride ? m_cmdCinematicRendering : cinematic.enabled;
            cinematic.enabled = !currentlyEnabled;
            m_cmdHasCinematicRenderingOverride = false;
            if ( SceneState().isSceneMode )
            {
                SceneState().hasCinematicRenderingOverride = true;
                SceneState().isCinematicRenderingEnabled = cinematic.enabled;
                SceneState().cinematicOverrideMask |= SCENE_CINE_RENDERING;
                SceneState().uiCinematicOverrideMask |= SCENE_CINE_RENDERING;
            }
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleCinematicRendering,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.cinematic.requestedModeSceneIndex >= -1 )
        {
            ApplyCinematicModeFromBrowserIndex( uiCommands.cinematic.requestedModeSceneIndex );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SelectCinematicScene, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.cinematic.requestedFeature != UICinematicFeature::None )
        {
            CinematicRenderConfig& cinematic = ActiveCinematicConfig();
            if ( uiCommands.cinematic.requestedFeature == UICinematicFeature::Shadows )
            {
                m_cmdHasCinematicShadowsOverride = false;
            }
            ToggleCinematicUIFeature( cinematic, SceneState(), uiCommands.cinematic.requestedFeature );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleCinematicFeature,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.cinematic.requestedParam != UICinematicParam::None )
        {
            CinematicRenderConfig& cinematic = ActiveCinematicConfig();
            ApplyCinematicUIParam( cinematic,
                                   SceneState(),
                                   uiCommands.cinematic.requestedParam,
                                   uiCommands.cinematic.requestedValue );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyCinematicParam, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.scene.resetScene )
        {
            m_runtimeCommands.Push( RuntimeCommand{ RuntimeCommandType::ResetCurrentScene } );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ResetScene, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.scene.resetSceneDefaults )
        {
            RuntimeCommand command{ RuntimeCommandType::ResetCurrentScene };
            command.preserveUIState = false;
            command.preserveRuntimeState = false;
            m_runtimeCommands.Push( std::move( command ) );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ResetSceneDefaults, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.scene.requestDemoScene )
        {
            m_runtimeCommands.Push( RuntimeCommand{ RuntimeCommandType::LoadDemoScene } );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::LoadDemoScene, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.scene.saveSceneDefaults )
        {
            m_runtimeCommands.Push( RuntimeCommand{ RuntimeCommandType::SaveSceneDefaults } );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SaveSceneDefaults, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.scene.createScene )
        {
            RuntimeCommand command{ RuntimeCommandType::CreateScene };
            command.text = uiCommands.scene.requestedSceneName;
            m_runtimeCommands.Push( std::move( command ) );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::CreateScene, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.scene.requestedSceneIndex >= 0 )
        {
            RuntimeCommand command{ RuntimeCommandType::LoadSceneIndex };
            command.index = uiCommands.scene.requestedSceneIndex;
            m_runtimeCommands.Push( command );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SelectScene, RuntimeInputActionSource::UI );
        }

        RunUIStressActions();

        TickEditorViewportAndPlacementScaleInput( editorUnhandledWheelDelta );
    }

    // Editor, replay, and launcher actions share world clicks. UI hover/capture
    // suppresses them so panel interaction never mutates the scene.
    {
        const RuntimeMouseEdges mouseEdges =
            m_runtimeInput.CaptureMouseButtons( Input::IsLeftMouseDown(), Input::IsRightMouseDown() );
        const bool leftPressed = mouseEdges.leftPressed;
        bool consumedWorldClick = TickEditorWorldClick( mouseEdges, suppressWorldActionThisFrame );
        if ( !consumedWorldClick )
        {
            consumedWorldClick = TickMousePickupInput( m_systems.window ? m_systems.window->m_sWindow : nullptr,
                                                       mouseEdges,
                                                       suppressWorldActionThisFrame );
        }
        if ( !consumedWorldClick && leftPressed && !suppressWorldActionThisFrame && !m_editor.editorModeEnabled &&
             !m_UI.WantsNativeMouseCursor() && ( Input::IsKeyDown( VK_CONTROL ) || !IsLauncherCameraMode() ) )
        {
            const bool additiveReplayPick = Input::IsKeyDown( VK_SHIFT );
            TryPickReplayPathTargetFromMouse( additiveReplayPick, !additiveReplayPick );
            consumedWorldClick = true;
        }

        if ( !consumedWorldClick && IsLauncherCameraMode() && leftPressed && !suppressWorldActionThisFrame &&
             !m_UI.WantsNativeMouseCursor() )
        {
            EnterInteractiveSceneRun();
            FireRayCastTest();
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::FireLauncher, RuntimeInputActionSource::Mouse );
        }
    }

    if ( m_UI.BlocksKeyboard() )
    {
        AdvanceTakeInputKeyboardActionMemories( m_runtimeInput );
        InputController::ResetMouseLook( m_camera );
        m_camera.input.Set( InputState::Up, false );
        m_camera.input.Set( InputState::Down, false );
        m_camera.input.Set( InputState::Left, false );
        m_camera.input.Set( InputState::Right, false );
        ApplyCursorOwnership();
        return;
    }

    HandleEditorSaveHotkeys();

    // R: reset/reload the current scene from scratch. Backspace remains as a scene-mode alias.
    {
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::ResetScene, 'R' ) )
        {
            m_runtimeCommands.Push( RuntimeCommand{ RuntimeCommandType::ResetCurrentScene } );
        }
    }
    if ( SceneState().isSceneMode )
    {
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                          RuntimeInputAction::ResetSceneFromBackspace,
                                                          VK_BACK ) )
        {
            m_runtimeCommands.Push( RuntimeCommand{ RuntimeCommandType::ResetCurrentScene } );
        }
    }

    const bool cameraMouseLookActive =
        ( !m_editor.editorModeEnabled && IsFlyCameraMode() && m_camera.mode != RunCameraMode::Manipulator &&
          ( !ReplayInspectionActive() || ReplayInspectionMouseLookActive() ) ) ||
        m_editor.viewportLookActive;
    const bool cameraKeyboardControlsActive = IsFlyCameraMode() || m_editor.viewportLookActive;
    if ( cameraMouseLookActive )
    {
        // Diagnostics UI owns the native cursor; mouse-look hides it while
        // consuming raw Win32 deltas, with cursor-position deltas as a
        // remote-desktop friendly fallback when raw input is unavailable.
        if ( !Input::IsAppFocused() )
        {
            InputController::ResetMouseLook( m_camera );
        }
        else if ( !MouseLookOwnsCursor() )
        {
            ApplyCursorOwnership();
            InputController::ResetMouseLook( m_camera );
        }
        else
        {
            Input::SetSystemCursorVisible( false );
            long rawX = 0;
            long rawY = 0;
            const bool hasRawDelta = Input::ConsumeRawMouseDelta( rawX, rawY );
            POINT currentClient = Input::GetClientMouseCoordinates();

            if ( m_camera.needsMouseLookReset )
            {
                m_camera.input.xMove = 0;
                m_camera.input.yMove = 0;
                m_camera.mouseLookLastClient = currentClient;
                m_camera.hasMouseLookLastClient = true;
                m_camera.needsMouseLookReset = false;
            }
            else if ( hasRawDelta )
            {
                InputController::SetMouseLookDelta( m_camera, rawX, rawY );
                m_camera.mouseLookLastClient = currentClient;
                m_camera.hasMouseLookLastClient = true;
            }
            else if ( !m_camera.hasMouseLookLastClient )
            {
                m_camera.input.xMove = 0;
                m_camera.input.yMove = 0;
                m_camera.mouseLookLastClient = currentClient;
                m_camera.hasMouseLookLastClient = true;
            }
            else
            {
                InputController::SetMouseLookDelta( m_camera,
                                                    currentClient.x - m_camera.mouseLookLastClient.x,
                                                    currentClient.y - m_camera.mouseLookLastClient.y );
                m_camera.mouseLookLastClient = currentClient;
            }
        }
    }
    else
    {
        InputController::ResetMouseLook( m_camera );
        ApplyCursorOwnership();
    }

    if ( cameraKeyboardControlsActive )
    {
        // WASD movement
        m_camera.input.Set( InputState::Up, Input::IsKeyDown( 'W' ) );
        m_camera.input.Set( InputState::Left, Input::IsKeyDown( 'A' ) );
        m_camera.input.Set( InputState::Down, Input::IsKeyDown( 'S' ) );
        m_camera.input.Set( InputState::Right, Input::IsKeyDown( 'D' ) );
    }
    else
    {
        InputController::ResetMouseLook( m_camera );
        m_camera.input.Set( InputState::Up, false );
        m_camera.input.Set( InputState::Down, false );
        m_camera.input.Set( InputState::Left, false );
        m_camera.input.Set( InputState::Right, false );
    }

    DrainRuntimeCommands();
}


bool Run::DrainRuntimeCommands()
{
    bool processed = false;
    RuntimeCommand command;
    while ( m_runtimeCommands.TryPop( command ) )
    {
        processed = true;
        switch ( command.type )
        {
        case RuntimeCommandType::LoadSceneIndex:
            LoadSceneFromBrowserIndex( command.index );
            break;
        case RuntimeCommandType::LoadDemoScene:
            LoadDemoSceneFromUI();
            break;
        case RuntimeCommandType::ResetCurrentScene:
            EnterInteractiveSceneRun();
            ResetCurrentScene( command.preserveUIState, command.suppressExitOnComplete, command.preserveRuntimeState );
            break;
        case RuntimeCommandType::CreateScene:
            CreateSceneFromUI( command.text.c_str() );
            break;
        case RuntimeCommandType::SaveScreenshot:
            if ( !command.text.empty() )
            {
                SaveScreenshot( command.text.c_str() );
            }
            break;
        case RuntimeCommandType::SaveSceneDefaults:
            SaveCurrentSceneDefaults();
            break;
        case RuntimeCommandType::SaveRenderDefaults:
            SaveRenderDefaults();
            break;
        case RuntimeCommandType::AdvanceScene:
            if ( !AdvanceScene() )
            {
                PostQuitMessage( 0 );
            }
            break;
        case RuntimeCommandType::Quit:
            PostQuitMessage( 0 );
            break;
        case RuntimeCommandType::None:
            break;
        }
        if ( command.type != RuntimeCommandType::None )
        {
            RecordReplayEvent( ReplayEventKind::RuntimeCommand,
                               NextReplayEventFrameIndex(),
                               ReplayRuntimeCommandFlags( command ),
                               static_cast<int32_t>( command.type ),
                               command.index,
                               0,
                               0,
                               0,
                               command.text.empty() ? ReplayRuntimeCommandName( command.type ) : command.text.c_str() );
        }
    }

    if ( processed )
    {
        RefreshRuntimeViewModel();
    }
    return processed;
}


void Run::MoveCamera( float keyMovementQty, float mouseMovementQty )
{
    if ( IsFlyCameraMode() || m_editor.viewportLookActive )
    {
        // Shift held = 3x speed
        float speedMult = Input::IsKeyDown( VK_SHIFT ) ? 3.0f : 1.0f;

        // Mouse look
        if ( ( !m_editor.editorModeEnabled || m_editor.viewportLookActive ) &&
             ( m_camera.input.xMove != 0 || m_camera.input.yMove != 0 ) )
        {
            m_systems.cameras->RotatePrimary( m_camera.input.xMove * mouseMovementQty,
                                              m_camera.input.yMove * mouseMovementQty );
        }

        // WASD movement
        if ( m_camera.input.Get( InputState::Up ) )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Forward, keyMovementQty * speedMult );
        }
        if ( m_camera.input.Get( InputState::Left ) )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Left, keyMovementQty * speedMult );
        }
        if ( m_camera.input.Get( InputState::Down ) )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Backward, keyMovementQty * speedMult );
        }
        if ( m_camera.input.Get( InputState::Right ) )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Right, keyMovementQty * speedMult );
        }

        m_systems.cameras->ApplyPrimaryMovementBuffer();
    }

    // Clamp camera Y between m_terrain surface and Cfg().maxCameraHeight (not in fly mode, not in scene mode)
    if ( !IsFlyCameraMode() && !m_editor.viewportLookActive && !SceneState().isSceneMode )
    {
        Vector3 translatedCameraPosition = m_systems.cameras->GetCameraTranslation();
        float minY =
            m_systems.terrain->GetTerrainHeightAt( translatedCameraPosition.x, translatedCameraPosition.z, true ) +
            Cfg().minCameraHeight;
        if ( minY > translatedCameraPosition.y )
        {
            m_systems.cameras->AmmendPrimaryY( minY );
        }
        else if ( translatedCameraPosition.y > Cfg().maxCameraHeight )
        {
            m_systems.cameras->AmmendPrimaryY( Cfg().maxCameraHeight );
        }
    }
}
