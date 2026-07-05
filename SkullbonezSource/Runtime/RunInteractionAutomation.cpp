/*
File: SkullbonezSource/Runtime/RunInteractionAutomation.cpp
Purpose:
  Drives deterministic runtime world-click scripts through the normal input path.

Mental model:
  Interaction automation is a validation driver. It asks the same picking,
  replay, camera, and world-input code that an operator would use, then writes a
  compact JSON report for the test harness.

Glossary:
  World click: Automation request that projects a screen-space click into the
  scene and routes it through the active runtime owner.
  Prediction target: Replay body selected for future-path diagnostics.
  Automation report: JSON side-channel describing what the scripted interaction
  observed without mutating validation baselines directly.

Invariants:
  - Scripts must exercise normal runtime routing, not bypass tool ownership or
    replay state with hidden direct mutations.
  - Reported samples are snapshots of already-owned runtime state; this file
    must not become a second owner for replay or picker lifetimes.

Related:
  - SkullbonezSource/Runtime/RuntimePickService.h
  - SkullbonezSource/Runtime/RuntimeInteractionController.h
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
*/
#include "RunInternal.h"
#include "Replay/ReplayOverlayLayout.h"
#include "RuntimeFileWriter.h"
#include "RuntimePickService.h"

#pragma warning( push, 0 )
#include "../../ThirdPtySource/nlohmann/json.hpp"
#pragma warning( pop )

#include <sstream>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Basics::RunInternal;
using namespace SkullbonezCore::Basics::ReplayOverlay;
using namespace SkullbonezCore::GameObjects;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;

namespace
{
using Json = nlohmann::ordered_json;

void CopyText( char* destination, std::size_t destinationSize, const std::string& value )
{
    if ( destination && destinationSize > 0 )
    {
        strcpy_s( destination, destinationSize, value.c_str() );
    }
}

Json Vec3Json( const Vector3& value )
{
    return Json::array( { value.x, value.y, value.z } );
}

const RunReplayPredictionBodySample* FindPredictionBodyById( const RunReplayPredictionFrame& frame, ReplayBodyId id )
{
    for ( const RunReplayPredictionBodySample& body : frame.bodies )
    {
        if ( body.id.value == id.value )
        {
            return &body;
        }
    }
    return nullptr;
}

bool TryPredictionTargetDisplacement( const ReplayRuntime& replayRuntime,
                                      float& outDisplacement,
                                      Vector3* outFirst = nullptr,
                                      Vector3* outLast = nullptr )
{
    // Concept: automation reports compare the first and last prediction sample
    // for the selected replay body. Missing target data is a clean "not ready",
    // not an error state for the running scene.
    const std::vector<RunReplayPredictionFrame>& activePredictionFrames = replayRuntime.ActivePredictionFrames();
    const ReplayBodyId targetId = replayRuntime.PathVisualizer().targetId;
    if ( targetId.value == 0 || activePredictionFrames.empty() )
    {
        return false;
    }

    const RunReplayPredictionBodySample* first = FindPredictionBodyById( activePredictionFrames.front(), targetId );
    const RunReplayPredictionBodySample* last = FindPredictionBodyById( activePredictionFrames.back(), targetId );
    if ( !first || !last )
    {
        return false;
    }

    outDisplacement = VectorMag( last->position - first->position );
    if ( outFirst )
    {
        *outFirst = first->position;
    }
    if ( outLast )
    {
        *outLast = last->position;
    }
    return true;
}

const char* CameraModeName( RunCameraMode mode )
{
    switch ( mode )
    {
    case RunCameraMode::Demo:
        return "Demo";
    case RunCameraMode::Scene:
        return "Scene";
    case RunCameraMode::Inspect:
        return "Inspect";
    case RunCameraMode::Attach:
        return "Attach";
    case RunCameraMode::Launcher:
        return "Launcher";
    case RunCameraMode::Manipulator:
        return "Manipulator";
    case RunCameraMode::Count:
        break;
    }
    return "Unknown";
}

bool TryParseCameraMode( const std::string& value, RunCameraMode& outMode )
{
    if ( value == "Demo" )
    {
        outMode = RunCameraMode::Demo;
        return true;
    }
    if ( value == "Scene" )
    {
        outMode = RunCameraMode::Scene;
        return true;
    }
    if ( value == "Inspect" )
    {
        outMode = RunCameraMode::Inspect;
        return true;
    }
    if ( value == "Attach" )
    {
        outMode = RunCameraMode::Attach;
        return true;
    }
    if ( value == "Launcher" )
    {
        outMode = RunCameraMode::Launcher;
        return true;
    }
    if ( value == "Manipulator" )
    {
        outMode = RunCameraMode::Manipulator;
        return true;
    }
    return false;
}

const char* WorkspaceName( RuntimeWorkspace workspace )
{
    switch ( workspace )
    {
    case RuntimeWorkspace::Live:
        return "Live";
    case RuntimeWorkspace::Inspect:
        return "Inspect";
    case RuntimeWorkspace::Edit:
        return "Edit";
    case RuntimeWorkspace::Replay:
        return "Replay";
    }
    return "Unknown";
}

const char* OwnerName( WorldInteractionOwner owner )
{
    switch ( owner )
    {
    case WorldInteractionOwner::None:
        return "None";
    case WorldInteractionOwner::InspectGizmo:
        return "InspectGizmo";
    case WorldInteractionOwner::EditorPlacement:
        return "EditorPlacement";
    case WorldInteractionOwner::EditorGizmo:
        return "EditorGizmo";
    case WorldInteractionOwner::ReplayScrub:
        return "ReplayScrub";
    case WorldInteractionOwner::ReplayVelocityEdit:
        return "ReplayVelocityEdit";
    case WorldInteractionOwner::ReplayPrediction:
        return "ReplayPrediction";
    case WorldInteractionOwner::ReplayBranchTarget:
        return "ReplayBranchTarget";
    case WorldInteractionOwner::ReplayCauseTree:
        return "ReplayCauseTree";
    case WorldInteractionOwner::Launcher:
        return "Launcher";
    case WorldInteractionOwner::Manipulator:
        return "Manipulator";
    }
    return "Unknown";
}

bool TryParseOwner( const std::string& value, WorldInteractionOwner& outOwner )
{
    if ( value == "None" )
    {
        outOwner = WorldInteractionOwner::None;
        return true;
    }
    if ( value == "InspectGizmo" )
    {
        outOwner = WorldInteractionOwner::InspectGizmo;
        return true;
    }
    if ( value == "EditorPlacement" )
    {
        outOwner = WorldInteractionOwner::EditorPlacement;
        return true;
    }
    if ( value == "EditorGizmo" )
    {
        outOwner = WorldInteractionOwner::EditorGizmo;
        return true;
    }
    if ( value == "ReplayPrediction" )
    {
        outOwner = WorldInteractionOwner::ReplayPrediction;
        return true;
    }
    if ( value == "ReplayScrub" )
    {
        outOwner = WorldInteractionOwner::ReplayScrub;
        return true;
    }
    if ( value == "ReplayVelocityEdit" )
    {
        outOwner = WorldInteractionOwner::ReplayVelocityEdit;
        return true;
    }
    if ( value == "ReplayBranchTarget" )
    {
        outOwner = WorldInteractionOwner::ReplayBranchTarget;
        return true;
    }
    if ( value == "ReplayCauseTree" )
    {
        outOwner = WorldInteractionOwner::ReplayCauseTree;
        return true;
    }
    if ( value == "Launcher" )
    {
        outOwner = WorldInteractionOwner::Launcher;
        return true;
    }
    if ( value == "Manipulator" )
    {
        outOwner = WorldInteractionOwner::Manipulator;
        return true;
    }
    return false;
}

const char* ActionTypeName( RunInteractionAutomationActionType type )
{
    switch ( type )
    {
    case RunInteractionAutomationActionType::SetCameraMode:
        return "setCameraMode";
    case RunInteractionAutomationActionType::ClickObject:
        return "clickObject";
    case RunInteractionAutomationActionType::ClickReplayControl:
        return "clickReplayControl";
    case RunInteractionAutomationActionType::SetReplayPredictionEnabled:
        return "setReplayPredictionEnabled";
    case RunInteractionAutomationActionType::ShowReplayScrubber:
        return "showReplayScrubber";
    case RunInteractionAutomationActionType::AssertState:
        return "assert";
    case RunInteractionAutomationActionType::Screenshot:
        return "screenshot";
    }
    return "unknown";
}

const char* AssertName( RunInteractionAutomationAssertKind kind )
{
    switch ( kind )
    {
    case RunInteractionAutomationAssertKind::SelectedObject:
        return "selectedObject";
    case RunInteractionAutomationAssertKind::Owner:
        return "owner";
    case RunInteractionAutomationAssertKind::CameraMode:
        return "cameraMode";
    case RunInteractionAutomationAssertKind::ReplayPredictionEnabled:
        return "replayPredictionEnabled";
    case RunInteractionAutomationAssertKind::ReplayPathTarget:
        return "replayPathTarget";
    case RunInteractionAutomationAssertKind::PredictionPathVisible:
        return "predictionPathVisible";
    case RunInteractionAutomationAssertKind::PredictionTargetDisplacementMin:
        return "predictionTargetDisplacementMin";
    case RunInteractionAutomationAssertKind::GizmoVisible:
        return "gizmoVisible";
    }
    return "unknown";
}

bool ReadBool( const Json& value )
{
    if ( value.is_boolean() )
    {
        return value.get<bool>();
    }
    if ( value.is_number_integer() )
    {
        return value.get<int>() != 0;
    }
    if ( value.is_string() )
    {
        const std::string text = value.get<std::string>();
        return text == "true" || text == "on" || text == "1";
    }
    return false;
}

bool TryReadFrame( const Json& entry, int& outFrame )
{
    if ( !entry.contains( "frame" ) || !entry["frame"].is_number_integer() )
    {
        return false;
    }
    outFrame = (std::max)( 0, entry["frame"].get<int>() );
    return true;
}

void AppendReportAction( RunInteractionAutomationState& state,
                         int frame,
                         RunInteractionAutomationActionType type,
                         const char* target,
                         const POINT* mouse,
                         bool consumed,
                         const char* detail )
{
    RunInteractionAutomationReportAction report;
    report.frame = frame;
    strcpy_s( report.type, sizeof( report.type ), ActionTypeName( type ) );
    if ( target )
    {
        strcpy_s( report.target, sizeof( report.target ), target );
    }
    if ( mouse )
    {
        report.mouse = *mouse;
        report.hasMouse = true;
    }
    report.consumed = consumed;
    if ( detail )
    {
        strcpy_s( report.detail, sizeof( report.detail ), detail );
    }
    state.actionReports.push_back( report );
}

void FailAutomation( RunInteractionAutomationState& state, const char* message )
{
    state.failed = true;
    if ( state.failure[0] == '\0' )
    {
        strcpy_s( state.failure, sizeof( state.failure ), message ? message : "interaction automation failed" );
    }
}

bool ParseAction( const Json& entry, RunInteractionAutomationAction& outAction, std::string& outError )
{
    if ( !entry.is_object() || !TryReadFrame( entry, outAction.frame ) )
    {
        outError = "each action must be an object with an integer frame";
        return false;
    }

    if ( entry.contains( "setCameraMode" ) )
    {
        outAction.type = RunInteractionAutomationActionType::SetCameraMode;
        const std::string modeName = entry["setCameraMode"].get<std::string>();
        if ( !TryParseCameraMode( modeName, outAction.cameraMode ) )
        {
            outError = "unknown setCameraMode value: " + modeName;
            return false;
        }
        CopyText( outAction.text, sizeof( outAction.text ), modeName );
        return true;
    }

    if ( entry.contains( "clickObject" ) )
    {
        outAction.type = RunInteractionAutomationActionType::ClickObject;
        CopyText( outAction.text, sizeof( outAction.text ), entry["clickObject"].get<std::string>() );
        if ( entry.contains( "button" ) )
        {
            const std::string button = entry["button"].get<std::string>();
            outAction.button =
                button == "right" ? RunInteractionAutomationButton::Right : RunInteractionAutomationButton::Left;
        }
        return true;
    }

    if ( entry.contains( "clickReplayControl" ) )
    {
        outAction.type = RunInteractionAutomationActionType::ClickReplayControl;
        CopyText( outAction.text, sizeof( outAction.text ), entry["clickReplayControl"].get<std::string>() );
        return true;
    }

    if ( entry.contains( "setReplayPredictionEnabled" ) )
    {
        outAction.type = RunInteractionAutomationActionType::SetReplayPredictionEnabled;
        outAction.boolValue = ReadBool( entry["setReplayPredictionEnabled"] );
        return true;
    }

    if ( entry.contains( "showReplayScrubber" ) )
    {
        outAction.type = RunInteractionAutomationActionType::ShowReplayScrubber;
        outAction.boolValue = ReadBool( entry["showReplayScrubber"] );
        return true;
    }

    if ( entry.contains( "screenshot" ) )
    {
        outAction.type = RunInteractionAutomationActionType::Screenshot;
        CopyText( outAction.path, sizeof( outAction.path ), entry["screenshot"].get<std::string>() );
        return true;
    }

    if ( entry.contains( "assert" ) )
    {
        const Json& assertion = entry["assert"];
        if ( !assertion.is_object() || assertion.empty() )
        {
            outError = "assert action must contain one assertion field";
            return false;
        }
        outAction.type = RunInteractionAutomationActionType::AssertState;
        const auto member = assertion.begin();
        const std::string name = member.key();
        if ( name == "selectedObject" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::SelectedObject;
            CopyText( outAction.text, sizeof( outAction.text ), member.value().get<std::string>() );
        }
        else if ( name == "owner" )
        {
            WorldInteractionOwner owner = WorldInteractionOwner::None;
            const std::string ownerName = member.value().get<std::string>();
            if ( !TryParseOwner( ownerName, owner ) )
            {
                outError = "unknown owner assertion value: " + ownerName;
                return false;
            }
            outAction.assertKind = RunInteractionAutomationAssertKind::Owner;
            CopyText( outAction.text, sizeof( outAction.text ), ownerName );
        }
        else if ( name == "cameraMode" )
        {
            RunCameraMode mode = RunCameraMode::Inspect;
            const std::string modeName = member.value().get<std::string>();
            if ( !TryParseCameraMode( modeName, mode ) )
            {
                outError = "unknown cameraMode assertion value: " + modeName;
                return false;
            }
            outAction.assertKind = RunInteractionAutomationAssertKind::CameraMode;
            outAction.cameraMode = mode;
            CopyText( outAction.text, sizeof( outAction.text ), modeName );
        }
        else if ( name == "replayPredictionEnabled" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::ReplayPredictionEnabled;
            outAction.boolValue = ReadBool( member.value() );
        }
        else if ( name == "replayPathTarget" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::ReplayPathTarget;
            CopyText( outAction.text, sizeof( outAction.text ), member.value().get<std::string>() );
        }
        else if ( name == "predictionPathVisible" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::PredictionPathVisible;
            outAction.boolValue = ReadBool( member.value() );
        }
        else if ( name == "predictionTargetDisplacementMin" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::PredictionTargetDisplacementMin;
            outAction.numberValue = member.value().get<float>();
        }
        else if ( name == "gizmoVisible" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::GizmoVisible;
            outAction.boolValue = ReadBool( member.value() );
        }
        else
        {
            outError = "unknown assertion field: " + name;
            return false;
        }
        return true;
    }

    outError = "unknown action shape";
    return false;
}

std::string BoolString( bool value )
{
    return value ? "true" : "false";
}

bool LoadScript( RunInteractionAutomationState& state )
{
    state.scriptLoaded = true;
    std::ifstream input( state.scriptPath );
    if ( !input.is_open() )
    {
        FailAutomation( state, "failed to open interaction script" );
        return false;
    }

    Json root;
    try
    {
        input >> root;
    }
    catch ( const std::exception& e )
    {
        char message[512] = {};
        sprintf_s( message, sizeof( message ), "failed to parse interaction script: %s", e.what() );
        FailAutomation( state, message );
        return false;
    }

    if ( !root.contains( "actions" ) || !root["actions"].is_array() )
    {
        FailAutomation( state, "interaction script requires an actions array" );
        return false;
    }

    for ( const Json& entry : root["actions"] )
    {
        RunInteractionAutomationAction action;
        std::string error;
        if ( !ParseAction( entry, action, error ) )
        {
            FailAutomation( state, error.c_str() );
            return false;
        }
        state.actions.push_back( action );
    }

    std::sort( state.actions.begin(),
               state.actions.end(),
               []( const RunInteractionAutomationAction& lhs, const RunInteractionAutomationAction& rhs )
               { return lhs.frame < rhs.frame; } );
    return true;
}
} // namespace

bool Run::TryFindInteractionAutomationModel( const char* name, int& outIndex ) const
{
    outIndex = -1;
    if ( !name || name[0] == '\0' )
    {
        return false;
    }

    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    for ( std::size_t i = 0; i < models.size(); ++i )
    {
        const char* modelName = models[i].GetName();
        if ( modelName && strcmp( modelName, name ) == 0 )
        {
            outIndex = static_cast<int>( i );
            return true;
        }
    }
    return false;
}

bool Run::TryProjectInteractionAutomationModel( const char* name, POINT& outMouse )
{
    int modelIndex = -1;
    if ( !TryFindInteractionAutomationModel( name, modelIndex ) || !m_systems.cameras || !m_systems.window )
    {
        return false;
    }

    const int width = static_cast<int>( (std::max)( 1L, m_systems.window->m_sWindowDimensions.x ) );
    const int height = static_cast<int>( (std::max)( 1L, m_systems.window->m_sWindowDimensions.y ) );
    const int steps[] = { 96, 48, 24, 12, 6 };
    for ( const int step : steps )
    {
        for ( int y = step / 2; y < height; y += step )
        {
            for ( int x = step / 2; x < width; x += step )
            {
                Input::AutomationState inputState;
                inputState.enabled = true;
                inputState.hasMouseClientPosition = true;
                inputState.mouseClientPosition = POINT{ static_cast<LONG>( x ), static_cast<LONG>( y ) };
                Input::SetAutomationState( inputState );

                Vector3 rayOrigin;
                Vector3 rayDirection;
                if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) )
                {
                    RuntimePickRequest request;
                    request.purpose = RuntimePickPurpose::EditorSelection;
                    request.bodyStore = &m_cGameModelCollection.GetPhysicsBodyStore();
                    request.colliderStore = &m_cGameModelCollection.GetColliderStore();
                    request.rayOrigin = rayOrigin;
                    request.rayDirection = rayDirection;

                    RuntimePickResult result;
                    if ( RuntimePickService::TryPickModel( request, result ) && result.modelIndex == modelIndex )
                    {
                        outMouse = inputState.mouseClientPosition;
                        Input::ClearAutomationState();
                        return true;
                    }
                }
            }
        }
    }

    Input::ClearAutomationState();
    return false;
}

void Run::ClearInteractionAutomationInput()
{
    m_interactionAutomation.leftMouseDown = false;
    m_interactionAutomation.rightMouseDown = false;
    m_interactionAutomation.releaseLeftFrame = -1;
    m_interactionAutomation.releaseRightFrame = -1;
    Input::ClearAutomationState();
}

void Run::TickInteractionAutomationBeforeInput()
{
    RunInteractionAutomationState& state = m_interactionAutomation;
    if ( !state.enabled || state.finished )
    {
        return;
    }
    if ( !state.scriptLoaded && !LoadScript( state ) )
    {
        WriteInteractionAutomationReport();
        throw std::runtime_error( state.failure[0] != '\0' ? state.failure : "interaction automation script failed" );
    }

    const int frame = SceneState().currentFrame;
    if ( state.releaseLeftFrame == frame )
    {
        state.leftMouseDown = false;
        state.releaseLeftFrame = -1;
    }
    if ( state.releaseRightFrame == frame )
    {
        state.rightMouseDown = false;
        state.releaseRightFrame = -1;
    }

    for ( RunInteractionAutomationAction& action : state.actions )
    {
        if ( action.processed || action.frame != frame )
        {
            continue;
        }

        switch ( action.type )
        {
        case RunInteractionAutomationActionType::SetCameraMode:
            ApplyCameraMode( action.cameraMode, RuntimeInputActionSource::Runtime );
            AppendReportAction( state, frame, action.type, action.text, nullptr, true, "camera mode applied" );
            action.processed = true;
            break;
        case RunInteractionAutomationActionType::ShowReplayScrubber:
            m_replayRuntime.Scrubber().visible = action.boolValue;
            if ( action.boolValue )
            {
                m_replayRuntime.Scrubber().visibleUntil = m_timers.simulationTimer.GetTotalTime() + 5.0;
            }
            AppendReportAction( state, frame, action.type, "", nullptr, true, action.boolValue ? "visible" : "hidden" );
            action.processed = true;
            break;
        case RunInteractionAutomationActionType::SetReplayPredictionEnabled:
            m_replayRuntime.Prediction().enabled = action.boolValue;
            m_replayRuntime.Prediction().dirty = true;
            SetWorldInteractionOwnerAfterInteractionTransition(
                action.boolValue ? WorldInteractionOwner::ReplayPrediction : WorldInteractionOwner::None,
                InteractionExitReason::EnterReplay );
            AppendReportAction( state,
                                frame,
                                action.type,
                                "",
                                nullptr,
                                true,
                                action.boolValue ? "prediction enabled" : "prediction disabled" );
            action.processed = true;
            break;
        case RunInteractionAutomationActionType::ClickReplayControl:
            if ( strcmp( action.text, "predict" ) == 0 )
            {
                const int screenW = RuntimeWindowScreenWidth( m_systems, Cfg() );
                const int screenH = RuntimeWindowScreenHeight( m_systems, Cfg() );
                const ReplayRecorderStats solverReplayStats = m_replayRuntime.Solver().GetStats();
                // Why: interaction scripts should match the real UI: Predict
                // can branch from the current live solver state even before a
                // paused scene has accumulated two retained solver samples.
                const bool predictionToolsEnabled = solverReplayStats.enabled && SceneState().isScenePhysics;
                if ( screenW > 0 && screenH > 0 && predictionToolsEnabled )
                {
                    const UI::UIRect predictToggle = ReplayScrubberPredictToggleRect( screenW, screenH );
                    POINT mouse = {};
                    mouse.x = static_cast<LONG>( predictToggle.x + predictToggle.w * 0.5f );
                    mouse.y = static_cast<LONG>( predictToggle.y + predictToggle.h * 0.5f );
                    state.mouseClientPosition = mouse;
                    state.hasMouseClientPosition = true;
                    state.leftMouseDown = true;
                    state.releaseLeftFrame = frame + 1;
                    action.mouse = mouse;
                    action.hasMouse = true;
                    m_replayRuntime.Scrubber().visible = true;
                    m_replayRuntime.Scrubber().visibleUntil =
                        m_timers.simulationTimer.GetTotalTime() + REPLAY_SCRUBBER_VISIBLE_SECONDS;
                    AppendReportAction( state,
                                        frame,
                                        action.type,
                                        action.text,
                                        &mouse,
                                        true,
                                        "mouse press injected at predict toggle" );
                }
                else
                {
                    FailAutomation( state, "replay predict control unavailable" );
                    AppendReportAction( state,
                                        frame,
                                        action.type,
                                        action.text,
                                        nullptr,
                                        false,
                                        "replay predict control unavailable" );
                }
            }
            else
            {
                FailAutomation( state, "unsupported replay control in interaction script" );
                AppendReportAction( state,
                                    frame,
                                    action.type,
                                    action.text,
                                    nullptr,
                                    false,
                                    "unsupported replay control" );
            }
            action.processed = true;
            break;
        case RunInteractionAutomationActionType::ClickObject:
        {
            POINT mouse = {};
            const bool projected = TryProjectInteractionAutomationModel( action.text, mouse );
            if ( projected )
            {
                state.mouseClientPosition = mouse;
                state.hasMouseClientPosition = true;
                action.mouse = mouse;
                action.hasMouse = true;
                if ( action.button == RunInteractionAutomationButton::Right )
                {
                    state.rightMouseDown = true;
                    state.releaseRightFrame = frame + 1;
                }
                else
                {
                    state.leftMouseDown = true;
                    state.releaseLeftFrame = frame + 1;
                }
            }
            else
            {
                FailAutomation( state, "failed to project interaction target" );
            }
            AppendReportAction( state,
                                frame,
                                action.type,
                                action.text,
                                projected ? &mouse : nullptr,
                                projected,
                                projected ? "mouse press injected" : "target projection failed" );
            action.processed = true;
            break;
        }
        case RunInteractionAutomationActionType::AssertState:
        case RunInteractionAutomationActionType::Screenshot:
            break;
        }
    }

    Input::AutomationState inputState;
    inputState.enabled = true;
    inputState.hasMouseClientPosition = state.hasMouseClientPosition;
    inputState.mouseClientPosition = state.mouseClientPosition;
    inputState.leftMouseDown = state.leftMouseDown;
    inputState.rightMouseDown = state.rightMouseDown;
    Input::SetAutomationState( inputState );
}

void Run::TickInteractionAutomationAfterRender()
{
    RunInteractionAutomationState& state = m_interactionAutomation;
    if ( !state.enabled || state.finished )
    {
        return;
    }

    const int frame = SceneState().currentFrame;
    for ( RunInteractionAutomationAction& action : state.actions )
    {
        if ( action.processed || action.frame != frame )
        {
            continue;
        }

        if ( action.type == RunInteractionAutomationActionType::Screenshot )
        {
            if ( RuntimeFileWriter::EnsureParentDirectory( action.path ) )
            {
                SaveScreenshot( action.path );
                state.screenshots.emplace_back( action.path );
                AppendReportAction( state, frame, action.type, action.path, nullptr, true, "screenshot saved" );
            }
            else
            {
                FailAutomation( state, "failed to create screenshot parent directory" );
                AppendReportAction( state, frame, action.type, action.path, nullptr, false, "screenshot path failed" );
            }
            action.processed = true;
            continue;
        }

        if ( action.type != RunInteractionAutomationActionType::AssertState )
        {
            continue;
        }

        RunInteractionAutomationReportAssertion assertion;
        assertion.frame = frame;
        strcpy_s( assertion.name, sizeof( assertion.name ), AssertName( action.assertKind ) );

        std::string expected;
        std::string actual;
        bool passed = false;
        switch ( action.assertKind )
        {
        case RunInteractionAutomationAssertKind::SelectedObject:
        {
            expected = action.text;
            const int selectedIndex = m_runtimeTools.Editor().selectedModelIndex;
            if ( selectedIndex >= 0 && selectedIndex < m_cGameModelCollection.GetModelCount() )
            {
                actual = m_cGameModelCollection.GetModelAtIndex( selectedIndex ).GetName();
            }
            passed = actual == expected;
            break;
        }
        case RunInteractionAutomationAssertKind::Owner:
            expected = action.text;
            actual = OwnerName( m_interaction.Owner() );
            passed = actual == expected;
            break;
        case RunInteractionAutomationAssertKind::CameraMode:
            expected = CameraModeName( action.cameraMode );
            actual = CameraModeName( m_camera.mode );
            passed = m_camera.mode == action.cameraMode;
            break;
        case RunInteractionAutomationAssertKind::ReplayPredictionEnabled:
            expected = BoolString( action.boolValue );
            actual = BoolString( m_replayRuntime.Prediction().enabled );
            passed = m_replayRuntime.Prediction().enabled == action.boolValue;
            break;
        case RunInteractionAutomationAssertKind::ReplayPathTarget:
            expected = action.text;
            actual = m_replayRuntime.PathVisualizer().hasTarget ? m_replayRuntime.PathVisualizer().targetName : "";
            passed = actual == expected;
            break;
        case RunInteractionAutomationAssertKind::PredictionPathVisible:
        {
            const bool visible =
                m_replayRuntime.PathVisualizer().hasTarget && ( !m_replayRuntime.PathVisualizer().futureNodes.empty() ||
                                                                !m_replayRuntime.ActivePredictionFrames().empty() ||
                                                                !m_replayRuntime.Prediction().futureNodes.empty() );
            expected = BoolString( action.boolValue );
            actual = BoolString( visible );
            passed = visible == action.boolValue;
            break;
        }
        case RunInteractionAutomationAssertKind::PredictionTargetDisplacementMin:
        {
            float displacement = 0.0f;
            const bool valid = TryPredictionTargetDisplacement( m_replayRuntime, displacement );
            {
                std::ostringstream stream;
                stream << ">=" << action.numberValue;
                expected = stream.str();
            }
            {
                std::ostringstream stream;
                stream << ( valid ? displacement : 0.0f );
                actual = stream.str();
            }
            passed = valid && displacement >= action.numberValue;
            break;
        }
        case RunInteractionAutomationAssertKind::GizmoVisible:
        {
            const bool visible = m_runtimeTools.Editor().selectedModelIndex >= 0 &&
                                 ( m_runtimeTools.Editor().editorModeEnabled || InspectGizmoInteractionActive() );
            expected = BoolString( action.boolValue );
            actual = BoolString( visible );
            passed = visible == action.boolValue;
            break;
        }
        }

        strcpy_s( assertion.expected, sizeof( assertion.expected ), expected.c_str() );
        strcpy_s( assertion.actual, sizeof( assertion.actual ), actual.c_str() );
        assertion.passed = passed;
        state.assertionReports.push_back( assertion );
        if ( !passed )
        {
            char message[256] = {};
            sprintf_s( message,
                       sizeof( message ),
                       "interaction assertion failed: %s expected=%s actual=%s",
                       assertion.name,
                       assertion.expected,
                       assertion.actual );
            FailAutomation( state, message );
        }
        action.processed = true;
    }

    bool allProcessed = true;
    int lastFrame = frame;
    for ( const RunInteractionAutomationAction& action : state.actions )
    {
        allProcessed = allProcessed && action.processed;
        lastFrame = (std::max)( lastFrame, action.frame );
    }

    if ( allProcessed && frame >= lastFrame )
    {
        state.finished = true;
        ClearInteractionAutomationInput();
        WriteInteractionAutomationReport();
        if ( state.failed )
        {
            throw std::runtime_error( state.failure[0] != '\0' ? state.failure : "interaction automation failed" );
        }
        PostQuitMessage( 0 );
    }
}

void Run::WriteInteractionAutomationReport()
{
    RunInteractionAutomationState& state = m_interactionAutomation;
    if ( state.reportWritten )
    {
        return;
    }

    Json actions = Json::array();
    for ( const RunInteractionAutomationReportAction& report : state.actionReports )
    {
        Json item;
        item["frame"] = report.frame;
        item["type"] = report.type;
        item["target"] = report.target;
        item["consumed"] = report.consumed;
        item["detail"] = report.detail;
        if ( report.hasMouse )
        {
            item["mouse"] = Json::array( { report.mouse.x, report.mouse.y } );
        }
        actions.push_back( item );
    }

    Json assertions = Json::array();
    for ( const RunInteractionAutomationReportAssertion& assertion : state.assertionReports )
    {
        assertions.push_back( Json{ { "frame", assertion.frame },
                                    { "name", assertion.name },
                                    { "expected", assertion.expected },
                                    { "actual", assertion.actual },
                                    { "passed", assertion.passed } } );
    }

    Json screenshots = Json::array();
    for ( const std::string& screenshot : state.screenshots )
    {
        screenshots.push_back( screenshot );
    }

    const int selectedIndex = m_runtimeTools.Editor().selectedModelIndex;
    const char* selectedName = "";
    if ( selectedIndex >= 0 && selectedIndex < m_cGameModelCollection.GetModelCount() )
    {
        selectedName = m_cGameModelCollection.GetModelAtIndex( selectedIndex ).GetName();
    }
    const bool gizmoVisible =
        selectedIndex >= 0 && ( m_runtimeTools.Editor().editorModeEnabled || InspectGizmoInteractionActive() );
    const bool predictionPathVisible =
        m_replayRuntime.PathVisualizer().hasTarget &&
        ( !m_replayRuntime.PathVisualizer().futureNodes.empty() || !m_replayRuntime.ActivePredictionFrames().empty() ||
          !m_replayRuntime.Prediction().futureNodes.empty() );
    const std::vector<RunReplayPredictionFrame>& activePredictionFrames = m_replayRuntime.ActivePredictionFrames();
    bool predictionTargetDisplacementValid = false;
    Vector3 predictionTargetFirst = ZERO_VECTOR;
    Vector3 predictionTargetLast = ZERO_VECTOR;
    float predictionTargetDisplacement = 0.0f;
    predictionTargetDisplacementValid = TryPredictionTargetDisplacement( m_replayRuntime,
                                                                         predictionTargetDisplacement,
                                                                         &predictionTargetFirst,
                                                                         &predictionTargetLast );

    const std::string* scenePath = m_sceneController.CurrentPath();
    Json report;
    report["ok"] = !state.failed;
    report["scene"] = scenePath ? *scenePath : "";
    report["script"] = state.scriptPath;
    report["framesRun"] = SceneState().currentFrame;
    report["actions"] = actions;
    report["assertions"] = assertions;
    report["screenshots"] = screenshots;
    report["failure"] = state.failure;
    report["finalState"] =
        Json{ { "cameraMode", CameraModeName( m_camera.mode ) },
              { "workspace", WorkspaceName( m_interaction.Workspace() ) },
              { "owner", OwnerName( m_interaction.Owner() ) },
              { "selectedObject", selectedName },
              { "selectedModelIndex", selectedIndex },
              { "gizmoVisible", gizmoVisible },
              { "replayPredictionEnabled", m_replayRuntime.Prediction().enabled },
              { "replayPathTarget",
                m_replayRuntime.PathVisualizer().hasTarget ? m_replayRuntime.PathVisualizer().targetName : "" },
              { "replayPathTargetCount", static_cast<int>( m_replayRuntime.PathVisualizer().targets.size() ) },
              { "predictionPathVisible", predictionPathVisible },
              { "predictionActiveFrameCount", static_cast<int>( activePredictionFrames.size() ) },
              { "predictionFrameCount", static_cast<int>( m_replayRuntime.Prediction().frames.size() ) },
              { "predictionBuildFrameCount", static_cast<int>( m_replayRuntime.Prediction().buildFrames.size() ) },
              { "predictionTargetDisplacementValid", predictionTargetDisplacementValid },
              { "predictionTargetFirst", Vec3Json( predictionTargetFirst ) },
              { "predictionTargetLast", Vec3Json( predictionTargetLast ) },
              { "predictionTargetDisplacement", predictionTargetDisplacement },
              { "predictionFutureNodeCount", static_cast<int>( m_replayRuntime.Prediction().futureNodes.size() ) },
              { "predictionFutureNodeBuildFrameCount",
                static_cast<int>( m_replayRuntime.Prediction().futureNodesBuiltFrameCount ) },
              { "replayFutureNodeCount", static_cast<int>( m_replayRuntime.PathVisualizer().futureNodes.size() ) } };

    std::ofstream output;
    if ( !RuntimeFileWriter::OpenTextFile( state.reportPath, output ) )
    {
        state.reportWritten = true;
        state.failed = true;
        strcpy_s( state.failure, sizeof( state.failure ), "failed to open interaction report path" );
        return;
    }
    output << report.dump( 2 ) << "\n";
    output.close();
    state.reportWritten = true;
    printf( "[interaction] Report written: %s ok=%d\n", state.reportPath, state.failed ? 0 : 1 );
}
