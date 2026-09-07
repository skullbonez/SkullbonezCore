#include "Run.h"
#include "../Startup/Window.h"
#include "../Input/InputFrameValues.h"
#include "../Diagnostics/RuntimeOverlayDiagnostics.h"
#include "../UI/GameUI/UI.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include <commdlg.h>
#include <cmath>

using namespace SkullbonezCore;
using namespace SkullbonezCore::Runtime;
using Math::Vector::Vector3;
namespace
{
bool ChooseComparisonFile( HWND window, char ( &path )[260], bool save )
{
    OPENFILENAMEA dialog {};
    dialog.lStructSize = sizeof( dialog );
    dialog.hwndOwner = window;
    dialog.lpstrFilter = "Comparison or finding (*.json)\0*.json\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = 260;
    dialog.lpstrDefExt = "json";
    dialog.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | ( save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST );
    return ( save ? GetSaveFileNameA( &dialog ) : GetOpenFileNameA( &dialog ) ) != FALSE;
}
Vector3 Cross( const Vector3& a, const Vector3& b )
{
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
float Length( const Vector3& v )
{
    return std::sqrt( v.x * v.x + v.y * v.y + v.z * v.z );
}
ReplayCameraSample CameraSample( const Environment::CameraCollection& cameras )
{
    ReplayCameraSample camera;
    camera.eye = cameras.GetCameraTranslation();
    camera.view = cameras.GetCameraView();
    camera.up = cameras.GetCameraUp();
    return camera;
}
} // namespace
bool Run::LoadComparison( const char* path, bool finding )
{
    const bool started = m_comparisonLoad.Start( path, finding, m_comparison.MemoryCharge() );
    if ( started )
    {
        m_sceneController.EnterInteractiveRun();
        m_capture.DisableAutomationExit();
    }
    return started;
}
bool Run::PublishComparisonLoad()
{
    Core::Allocation::RuntimeAllocationScope loading( Core::Allocation::RuntimeAllocationPhase::Capture );
    ReplayCameraSample camera;
    const bool finding = m_comparisonLoad.Finding();
    if ( !m_comparisonLoad.Take( m_comparison, camera ) )
    {
        return false;
    }
    auto edit = m_overlayDiagnostics->EditPresentation();
    const int index = m_sceneController.Append( m_comparison.ScenePath() );
    if ( !ExecuteInputSceneLoadRequest( SceneLoadRequest::Load( index, true, true, false, true ), edit ) )
    {
        m_comparison.Close();
        return false;
    }
    m_comparisonPanel.Prepare( m_sceneController.Scene().Colliders(), m_sceneController.Scene().RenderInstances() );
    auto& cameras = m_sceneController.Scene().Cameras();
    cameras.CancelTween();
    if ( finding )
    {
        cameras.SetPrimaryPose( camera.eye, camera.view, camera.up );
    }
    m_sceneController.EnterInteractiveRun();
    m_capture.DisableAutomationExit();
    return true;
}
void Run::PollComparisonLoad()
{
    if ( !m_comparisonLoad.Pending() || !m_comparisonLoad.Ready() )
    {
        return;
    }
    const bool success = PublishComparisonLoad();
#if defined( SKULLBONEZ_SKARNESS )
    if ( !m_comparisonLoadRequest.empty() )
    {
        SkarnessCommand stateCommand;
        stateCommand.type = SkarnessCommandType::ComparisonState;
        SkarnessCommandApplication application;
        ApplySkarnessComparisonCommand( stateCommand, application );
        m_skarness.CompleteCommand( m_comparisonLoadRequest, success, application.result,
                                    success ? nullptr : m_comparisonLoad.Error().c_str() );
        m_comparisonLoadRequest.clear();
    }
#else
    (void)success;
#endif
}
void Run::FocusComparison()
{
    const auto* a = m_comparison.Body( 0, m_comparison.Selected(), m_comparison.Tick() );
    const auto* b = m_comparison.Body( 1, m_comparison.Selected(), m_comparison.Tick() );
    if ( !a && !b )
    {
        return;
    }
    const Vector3 center = a && b ? ( a->position + b->position ) * 0.5f : a ? a->position : b->position;
    const float radius = m_comparisonPanel.Radius( m_comparison.Selected() ) +
                         ( a && b ? Length( a->position - b->position ) * 0.5f : 0 );
    auto& cameras = m_sceneController.Scene().Cameras();
    auto direction = cameras.GetCameraTranslation() - cameras.GetCameraView();
    if ( !direction.TryNormalise() )
    {
        direction = { 0, 0, 1 };
    }
    cameras.CancelTween();
    const auto frame = m_comparisonPanel.BuildFrame( m_comparison, m_window.ClientWidth(), m_window.ClientHeight() );
    const float lens = (std::max)( frame.projection.m[0], frame.projection.m[5] );
    const float distance = (std::max)( 6.0f, radius * std::sqrt( 1 + lens * lens ) * 1.15f + 4 );
    cameras.SetPrimaryPose( center + direction * distance, center, { 0, 1, 0 } );
}
void Run::MoveComparisonCamera( float yaw, float pitch, float panX, float panY, float zoom )
{
    auto& cameras = m_sceneController.Scene().Cameras();
    Vector3 eye = cameras.GetCameraTranslation(), pivot = cameras.GetCameraView();
    const auto* selected = m_comparison.Body( 0, m_comparison.Selected(), m_comparison.Tick() );
    if ( !selected )
    {
        selected = m_comparison.Body( 1, m_comparison.Selected(), m_comparison.Tick() );
    }
    const bool orbit = m_comparison.Settings().orbitSelected && selected;
    if ( orbit )
    {
        pivot = selected->position;
    }
    const auto offset = eye - pivot;
    float distance = (std::max)( 0.02f, Length( offset ) );
    float azimuth = std::atan2( offset.x, offset.z ) + yaw;
    float elevation = std::clamp( std::asin( std::clamp( offset.y / distance, -1.0f, 1.0f ) ) + pitch, -1.55f, 1.55f );
    distance = std::clamp( distance * std::exp( zoom ), 0.02f, 1000000.0f );
    const Vector3 direction { std::sin( azimuth ) * std::cos( elevation ), std::sin( elevation ),
                              std::cos( azimuth ) * std::cos( elevation ) };
    const Vector3 right { std::cos( azimuth ), 0, -std::sin( azimuth ) };
    const Vector3 up = Cross( direction, right );
    const Vector3 pan = right * ( panX * distance ) + up * ( panY * distance );
    if ( orbit )
    {
        pivot = pivot + pan;
        eye = pivot + direction * distance;
    }
    else
    {
        // Free look rotates around the eye. Pan and dolly translate both ends
        // of the camera ray, so no object can silently retain the orbit pivot.
        eye = eye + pan + direction * ( distance - Length( offset ) );
        pivot = eye - direction * distance;
    }
    cameras.CancelTween();
    cameras.SetPrimaryPose( eye, pivot, { 0, 1, 0 } );
}
void Run::FlyComparisonCamera( float forward, float strafe, float seconds )
{
    if ( forward == 0 && strafe == 0 )
    {
        return;
    }
    auto& settings = m_comparison.Settings();
    settings.orbitSelected = false;
    settings.followA = false;
    auto& cameras = m_sceneController.Scene().Cameras();
    Vector3 direction = cameras.GetCameraView() - cameras.GetCameraTranslation();
    if ( !direction.TryNormalise() )
    {
        return;
    }
    Vector3 right = Cross( direction, cameras.GetCameraUp() );
    if ( !right.TryNormalise() )
    {
        return;
    }
    Vector3 movement = direction * forward + right * strafe;
    if ( !movement.TryNormalise() )
    {
        return;
    }
    const Vector3 delta = movement * ( 60.0f * seconds );
    cameras.CancelTween();
    cameras.SetPrimaryPose( cameras.GetCameraTranslation() + delta, cameras.GetCameraView() + delta, cameras.GetCameraUp() );
}
void Run::PickComparisonObject( int x, int y )
{
    const auto frame = m_comparisonPanel.BuildFrame( m_comparison, m_window.ClientWidth(), m_window.ClientHeight() );
    if ( x < frame.x || y < frame.y || x >= frame.x + frame.width || y >= frame.y + frame.height )
    {
        return;
    }
    const int width = frame.ImageWidth(), height = frame.ImageHeight();
    int side = frame.mode == 2 ? 0 : frame.mode == 3 ? 1 : -1;
    x -= frame.x;
    y -= frame.y;
    if ( frame.mode == 0 )
    {
        side = frame.stacked ? y / height : x / width;
        if ( frame.stacked )
        {
            y %= height;
        }
        else
        {
            x %= width;
        }
    }
    auto& cameras = m_sceneController.Scene().Cameras();
    Vector3 forward = cameras.GetCameraView() - cameras.GetCameraTranslation();
    if ( !forward.TryNormalise() )
    {
        return;
    }
    Vector3 right = Cross( forward, cameras.GetCameraUp() );
    if ( !right.TryNormalise() )
    {
        return;
    }
    const Vector3 up = Cross( right, forward );
    // Invariant: hit testing inverts the same per-viewport lens used for drawing.
    Vector3 direction = forward + right * ( ( 2.0f * x / width - 1 ) / frame.projection.m[0] ) +
                        up * ( ( 1 - 2.0f * y / height ) / frame.projection.m[5] );
    if ( !direction.TryNormalise() )
    {
        return;
    }
    m_comparison.Select( m_comparisonPanel.Pick( m_comparison, cameras.GetCameraTranslation(), direction, side ) );
}
bool Run::UpdateComparisonInput( bool textActive )
{
    PollComparisonLoad();
    const double now = GetTickCount64() / 1000.0;
    const auto& device = m_inputRouter.DeviceFrame();
    ComparisonPanelAction action = ComparisonPanelAction::None;
    if ( !textActive && m_inputRouter.ConsumeRepeatingAction( RuntimeInputAction::OpenComparison, now, 1.0e30 ) )
    {
        action = ComparisonPanelAction::Open;
    }
    if ( m_comparisonLoad.Pending() || !m_comparisonLoad.Error().empty() )
    {
        const auto ui = BuildUIInputSnapshot( device, m_inputRouter.UiSnapshot().mouse, m_operatorUi->InputOverride() );
        if ( m_comparisonPanel.Input( m_comparison, ui, false ) == ComparisonPanelAction::Close )
        {
            if ( m_comparisonLoad.Pending() )
            {
                m_comparisonLoad.Cancel();
            }
            else
            {
                m_comparisonLoad.DismissError();
            }
        }
        return true;
    }
    if ( m_comparison.Active() )
    {
        const uint64_t previousSelection = m_comparison.Selected();
        const uint64_t previousEvent = m_comparison.EventSelectionRevision();
        const auto* before = m_comparison.Body( 0, previousSelection, m_comparison.Tick() );
        const Vector3 previous = before ? before->position : Vector3 { 0, 0, 0 };
        const double elapsed = m_comparisonPanel.Advance( m_comparison, now );
        const auto ui = BuildUIInputSnapshot( device, m_inputRouter.UiSnapshot().mouse, m_operatorUi->InputOverride() );
        const bool dragging = m_inputRouter.UpdateTimelineDrag( m_comparisonPanel.TimelineContains( ui.mouseX, ui.mouseY ) );
        const auto panelAction = m_comparisonPanel.Input( m_comparison, ui, dragging );
        if ( panelAction != ComparisonPanelAction::None )
        {
            action = panelAction;
        }
        if ( !textActive )
        {
            if ( m_inputRouter.ConsumeRepeatingAction( RuntimeInputAction::ComparisonStepBackward, now, 0.075 ) )
            {
                m_comparison.Step( -1 );
            }
            if ( m_inputRouter.ConsumeRepeatingAction( RuntimeInputAction::ComparisonStepForward, now, 0.075 ) )
            {
                m_comparison.Step( 1 );
            }
            if ( m_inputRouter.ConsumeRepeatingAction( RuntimeInputAction::ComparisonPlayPause, now, 1.0e30 ) )
            {
                m_comparison.Play( m_comparison.Direction() ? 0 : 1 );
            }
        }
        const auto* after = m_comparison.Body( 0, m_comparison.Selected(), m_comparison.Tick() );
        if ( m_comparison.Settings().followA && previousSelection == m_comparison.Selected() &&
             previousEvent == m_comparison.EventSelectionRevision() && before && after )
        {
            auto& cameras = m_sceneController.Scene().Cameras();
            const auto delta = after->position - previous;
            cameras.SetPrimaryPose( cameras.GetCameraTranslation() + delta, cameras.GetCameraView() + delta,
                                    cameras.GetCameraUp() );
        }
        if ( device.appFocused && !textActive )
        {
            const float forward = ( device.keys.IsDown( 'W' ) ? 1.0f : 0 ) - ( device.keys.IsDown( 'S' ) ? 1.0f : 0 );
            const float strafe = ( device.keys.IsDown( 'D' ) ? 1.0f : 0 ) - ( device.keys.IsDown( 'A' ) ? 1.0f : 0 );
            FlyComparisonCamera( forward, strafe,
                                 static_cast<float>( elapsed ) * ( device.keys.IsDown( VK_SHIFT ) ? 3.0f : 1.0f ) );
        }
        if ( device.appFocused && !dragging && !m_comparisonPanel.Contains( device.clientX, device.clientY ) )
        {
            if ( ui.leftPressed )
            {
                PickComparisonObject( ui.mouseX, ui.mouseY );
            }
            if ( device.rightDown || device.middleDown || device.wheelDelta )
            {
                MoveComparisonCamera( device.rightDown ? InputController::ResolveMouseLookRadians( device.rawMouseX, 0.005f )
                                                       : 0,
                                      device.rightDown ? device.rawMouseY * 0.005f : 0,
                                      device.middleDown ? -device.rawMouseX * 0.001f : 0,
                                      device.middleDown ? device.rawMouseY * 0.001f : 0, -device.wheelDelta * 0.001f );
            }
        }
    }
    char path[260] {};
    if ( action == ComparisonPanelAction::Focus )
    {
        FocusComparison();
    }
    else if ( action == ComparisonPanelAction::Close )
    {
        m_comparison.Close();
    }
    else if ( action == ComparisonPanelAction::Open || action == ComparisonPanelAction::Restore )
    {
        if ( ChooseComparisonFile( m_window.NativeWindowHandle(), path, false ) )
        {
            LoadComparison( path, action == ComparisonPanelAction::Restore );
        }
    }
    else if ( action == ComparisonPanelAction::Save && ChooseComparisonFile( m_window.NativeWindowHandle(), path, true ) )
    {
        m_comparison.SaveFinding( path, CameraSample( m_sceneController.Scene().Cameras() ),
                                  m_comparison.FindingNote().c_str() );
    }
    return ComparisonUiActive();
}
void Run::RenderComparison()
{
    auto& cameras = m_sceneController.Scene().Cameras();
    cameras.SetCamera();
    auto frame = m_comparisonPanel.BuildFrame( m_comparison, m_window.ClientWidth(), m_window.ClientHeight() );
    frame.view = cameras.GetViewMatrix();

    Renderer().RenderPairedViews( frame );
}
#if defined( SKULLBONEZ_SKARNESS )
void Run::ApplySkarnessComparisonCommand( const SkarnessCommand& command, SkarnessCommandApplication& application )
{
    if ( command.type < SkarnessCommandType::ComparisonLoad || command.type > SkarnessCommandType::ComparisonState )
    {
        return;
    }
    application.handled = true;
    if ( command.type == SkarnessCommandType::ComparisonLoad || command.type == SkarnessCommandType::ComparisonLoadFinding )
    {
        application.applied = LoadComparison( command.text.c_str(),
                                              command.type == SkarnessCommandType::ComparisonLoadFinding );
        if ( application.applied )
        {
            m_comparisonLoadRequest = command.requestId;
            application.deferred = true;
            return;
        }
    }
    else if ( command.type == SkarnessCommandType::ComparisonClose && m_comparisonLoad.Pending() )
    {
        m_comparisonLoad.Cancel();
        return;
    }
    else if ( !m_comparison.Active() && command.type != SkarnessCommandType::ComparisonState )
    {
        application.applied = false;
        application.reason = "No complete comparison is loaded";
        return;
    }
    else
    {
        switch ( command.type )
        {
        case SkarnessCommandType::ComparisonClose:
            m_comparison.Close();
            break;
        case SkarnessCommandType::ComparisonSeek:
            application.applied = command.integer <= m_comparison.LastTick();
            if ( application.applied )
            {
                m_comparison.Seek( command.integer );
            }
            break;
        case SkarnessCommandType::ComparisonStep:
            m_comparison.Step( command.integer );
            break;
        case SkarnessCommandType::ComparisonPlay:
            m_comparison.Play( command.integer );
            break;
        case SkarnessCommandType::ComparisonMode:
            application.applied = m_comparison.SetDisplay( command.text.c_str() );
            break;
        case SkarnessCommandType::ComparisonSelect:
            m_comparison.Select( command.unsignedInteger );
            break;
        case SkarnessCommandType::ComparisonEvent:
            application.applied = m_comparison.SelectEvent( static_cast<std::size_t>( command.integer ) );
            break;
        case SkarnessCommandType::ComparisonFocus:
            FocusComparison();
            break;
        case SkarnessCommandType::ComparisonNext:
            application.applied = m_comparison.NextDifference();
            break;
        case SkarnessCommandType::ComparisonSetting:
            application.applied = m_comparison.SetSetting( command.text.c_str(), command.number );
            break;
        case SkarnessCommandType::ComparisonLoop:
            application.applied = command.integer >= 0 && command.secondInteger >= command.integer &&
                                  command.secondInteger <= m_comparison.LastTick();
            if ( application.applied )
            {
                m_comparison.SetLoop( command.integer, command.secondInteger, command.enabled );
            }
            break;
        case SkarnessCommandType::ComparisonCamera:
            MoveComparisonCamera( static_cast<float>( command.number ), static_cast<float>( command.secondNumber ),
                                  static_cast<float>( command.fourthNumber ), static_cast<float>( command.fifthNumber ),
                                  static_cast<float>( command.thirdNumber ) );
            break;
        case SkarnessCommandType::ComparisonSaveFinding:
            application.applied = m_comparison.SaveFinding( command.text.c_str(),
                                                            CameraSample( m_sceneController.Scene().Cameras() ),
                                                            command.secondText.c_str() );
            break;
        default:
            break;
        }
    }
    if ( !application.applied )
    {
        application.reason = "Invalid comparison request or incompatible bundle";
        application.result.valueName = "detail";
        application.result.hasTextValue = true;
        application.result.textValue = m_comparison.Error();
    }
    auto& state = application.result;
    state.hasComparison = true;
    state.comparisonLoading = m_comparisonLoad.Pending();
    state.comparisonLoadPercent = m_comparisonLoad.Percent();
    state.comparisonStacked = m_comparison.Settings().stackedViews;
    state.comparisonOrbit = m_comparison.Settings().orbitSelected;
    state.comparisonDragging = m_inputRouter.TimelineDragActive();
    const auto camera = CameraSample( m_sceneController.Scene().Cameras() );
    state.comparisonEye = { camera.eye.x, camera.eye.y, camera.eye.z };
    state.comparisonView = { camera.view.x, camera.view.y, camera.view.z };
    state.comparisonTick = m_comparison.Tick();
    state.comparisonLastTick = m_comparison.LastTick();
    state.comparisonDirection = m_comparison.Direction();
    state.comparisonMode = static_cast<int>( m_comparison.Settings().display );
    state.comparisonSelected = m_comparison.Selected();
    state.comparisonEventCount = m_comparison.Events().size();
    for ( int side = 0; side < 2; ++side )
    {
        state.comparisonCoverage[side] = m_comparison.Recording( side ).Frame( m_comparison.Tick() ) != nullptr;
        state.comparisonDiagnostics[side] = m_comparison.Recording( side ).Evidence( m_comparison.Tick() ) != nullptr;
        if ( const auto* body = m_comparison.Body( side, m_comparison.Selected(), m_comparison.Tick() ) )
        {
            ( side ? state.comparisonPositionB : state.comparisonPositionA ) = { body->position.x, body->position.y,
                                                                                 body->position.z };
        }
    }
    const auto difference = m_comparison.Difference( m_comparison.Selected(), m_comparison.Tick() );
    state.comparisonDistance = difference.distance;
    state.comparisonAngle = difference.angleDegrees;
}
#endif
