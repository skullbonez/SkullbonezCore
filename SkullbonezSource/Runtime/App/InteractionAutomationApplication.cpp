/*
Purpose:
  Drives deterministic runtime interaction scripts through the normal input path.

Invariants:
  - Scripts must exercise normal runtime routing, not bypass tool ownership or
    replay state with hidden direct mutations.
  - Assertions and reports consume ReplayAutomationView; replay mutation uses
    named owner commands and never a mutable prediction/recorder reference.
  - Prediction assertions read only the presented published prefix and its
    matching evidence bank; unpublished rows remain private allocation storage.
  - Published samples are frame-local; this file must not retain their spans or
    pointers beyond the synchronous automation turn.
  - Forecast presentation assertions observe the detached post-render view;
    they never read the worker-owned rolling ring directly.
  - Surface selection accepts GameUI or ImGui only; one frame can publish at
    most one process-surface request.
  - Development UI application stops on the first recoverable command failure;
    Run owns process exit policy and converts that result at its boundary.
  - Recorded numeric fields are range-checked before narrowing so malformed
    evidence remains a recoverable automation failure.
  - Each turn trace row is flushed after rendering; a write failure fails the
    run instead of allowing incomplete evidence to look successful.
*/
#include "../Automation/InteractionAutomationController.h"
#include "../Automation/InteractionAutomationRecorder.h"
#include "InteractionAutomationApplication.h"
#include "../Planning/ContinuousOrbitalForecast.h"


#include "../Capture/CaptureController.h"
#include "../Input/InputRouter.h"
#include "../Interaction/RuntimeInteractionController.h"
#include "../Camera/RuntimeCameraMode.h"
#include "../Camera/CameraControlState.h"
#include "../Camera/CameraCollection.h"
#include "../RuntimeFrameViews.h"
#include "../Tools/RuntimeTools.h"
#include "../Startup/Window.h"
#include "../Scene/SceneController.h"
#include "../Scene/SceneSessionState.h"

#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/SbDiagnosticStore.h"
#include "../Editor/EditorTools.h"
#include "../Replay/ReplayOverlaySurface.h"
#include "../Replay/ReplayOverlayLayout.h"
#include "../Planning/ReplayCauseInspection.h"
#include "../Direction/DemoDirectorPlayback.h"
#include "../Tools/RuntimeFileWriter.h"
#include "../Interaction/RuntimePickService.h"
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
#include "../DevelopmentTools/ImGuiEditorOwner.h"
#endif

#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsTimestep.h"
#include "../../Core/Config.h"
#include "../../Core/ByteView.h"
#include "../../Rendering/RenderSceneSnapshot.h"
#include "../../Rendering/DX12/Dx12BackbufferCapture.h"
#include "../UI/GameUI/UI.h"

#pragma warning( push, 0 )
#include "../../../ThirdPtySource/nlohmann/json.hpp"
#pragma warning( pop )

#include <algorithm>
#include <array>
#include <bcrypt.h>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

#pragma comment( lib, "bcrypt.lib" )

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::ReplayScrubberOperations;
using namespace SkullbonezCore::Runtime::ReplayOverlay;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;
using SkullbonezCore::Hardware::Input;
namespace Physics = SkullbonezCore::Physics;
namespace Rendering = SkullbonezCore::Rendering;
namespace CoreAllocation = SkullbonezCore::Core::Allocation;

namespace
{
using Json = nlohmann::ordered_json;

const char* InputActionSourceName( RuntimeInputActionSource source )
{
    switch ( source )
    {
    case RuntimeInputActionSource::Keyboard:
        return "keyboard";
    case RuntimeInputActionSource::UI:
        return "ui";
    case RuntimeInputActionSource::Mouse:
        return "mouse";
    case RuntimeInputActionSource::FocusLost:
        return "focus_lost";
    case RuntimeInputActionSource::Runtime:
    default:
        return "runtime";
    }
}

const char* InputActionPhaseName( InputActionPhase phase )
{
    switch ( phase )
    {
    case InputActionPhase::PreUi:
        return "pre_ui";
    case InputActionPhase::AfterUi:
        return "after_ui";
    case InputActionPhase::Capture:
    default:
        return "capture";
    }
}

const char* InputActionEdgeName( InputActionEdge edge )
{
    switch ( edge )
    {
    case InputActionEdge::Pressed:
        return "pressed";
    case InputActionEdge::Held:
        return "held";
    case InputActionEdge::Released:
    default:
        return "released";
    }
}

std::string VirtualKeyName( int virtualKey )
{
    switch ( virtualKey )
    {
    case VK_LBUTTON:
        return "Mouse Left";
    case VK_RBUTTON:
        return "Mouse Right";
    case VK_MBUTTON:
        return "Mouse Middle";
    default:
        break;
    }

    char name[64] = {};
    const UINT scanCode = MapVirtualKeyA( static_cast<UINT>( virtualKey ), MAPVK_VK_TO_VSC );

    if ( scanCode != 0u && GetKeyNameTextA( static_cast<LONG>( scanCode << 16u ), name, sizeof( name ) ) > 0 )
    {
        return name;
    }

    return "VK_" + std::to_string( virtualKey );
}

bool WriteInteractionTraceTurn( InteractionAutomationController& state, InputRouter& inputRouter, CameraControlState& camera,
                                SkullbonezCore::UI::InGameUI& ui, SceneController& scene )
{
    if ( !state.traceOutput.is_open() )
    {
        return true;
    }

    const DeviceInputFrame& device = inputRouter.DeviceFrame();
    Json input = { { "focused", device.appFocused },    { "clientPositionAvailable", device.hasClientPosition },
                   { "clientX", device.clientX },       { "clientY", device.clientY },
                   { "rawMouseX", device.rawMouseX },   { "rawMouseY", device.rawMouseY },
                   { "wheelDelta", device.wheelDelta }, { "left", device.leftDown },
                   { "middle", device.middleDown },     { "right", device.rightDown } };
    Json downKeys = Json::array();

    for ( int key = 0; key < InputKeySnapshot::VIRTUAL_KEY_COUNT; ++key )
    {
        if ( device.keys.IsDown( key ) )
        {
            downKeys.push_back( { { "virtualKey", key }, { "name", VirtualKeyName( key ) } } );
        }
    }

    input["downVirtualKeys"] = std::move( downKeys );
    Json routed = Json::array();
    const InputActions& actions = inputRouter.Actions();

    for ( std::size_t index = 0u; index < actions.Count(); ++index )
    {
        const InputActionEvent& event = actions[index];
        routed.push_back( { { "actionId", static_cast<int>( event.action ) },
                            { "source", InputActionSourceName( event.source ) },
                            { "phase", InputActionPhaseName( event.phase ) },
                            { "edge", InputActionEdgeName( event.edge ) },
                            { "virtualKey", event.virtualKey },
                            { "virtualKeyName", VirtualKeyName( event.virtualKey ) } } );
    }

    const SceneSessionState& sceneState = scene.State();
    const SkullbonezCore::Environment::CameraCollection& cameras = scene.Scene().Cameras();
    const Vector3& cameraEye = cameras.GetCameraTranslation();
    const Vector3& cameraView = cameras.GetCameraView();
    const Vector3& cameraUp = cameras.GetCameraUp();
    const Json line = { { "type", "turn" },
                        { "turn", state.traceTurn },
                        { "recordingTurn", state.recordedManifest ? Json( state.recordedTurn ) : Json() },
                        { "deltaSeconds", state.recordedManifest ? state.recordedDeltaSeconds : 0.0 },
                        { "injected", std::move( input ) },
                        { "routed", std::move( routed ) },
                        { "observed",
                          { { "sceneFrame", sceneState.currentFrame },
                            { "sceneLoadCount", sceneState.loadCount },
                            { "sceneMode", sceneState.isSceneMode },
                            { "cameraMode", static_cast<int>( camera.mode ) },
                            { "cameraEye", { cameraEye.x, cameraEye.y, cameraEye.z } },
                            { "cameraView", { cameraView.x, cameraView.y, cameraView.z } },
                            { "cameraUp", { cameraUp.x, cameraUp.y, cameraUp.z } },
                            { "inputMode", static_cast<int>( inputRouter.RuntimeContext().CurrentMode() ) },
                            { "uiVisible", ui.IsVisible() },
                            { "uiMinimized", ui.IsMinimized() },
                            { "uiTab", static_cast<int>( ui.GetActiveTab() ) } } } };

    state.traceOutput << line.dump() << '\n';
    state.traceOutput.flush();
    return state.traceOutput.good();
}

void CopyText( char* destination, std::size_t destinationSize, const std::string& value )
{
    if ( destination && destinationSize > 0 )
    {
        strncpy_s( destination, destinationSize, value.c_str(), _TRUNCATE );
    }
}

Json Vec3Json( const Vector3& value )
{
    return Json::array( { value.x, value.y, value.z } );
}

constexpr uint64_t INTERACTION_PREDICTION_FINGERPRINT_OFFSET = 1469598103934665603ull;
constexpr uint64_t INTERACTION_PREDICTION_FINGERPRINT_PRIME = 1099511628211ull;

// Invariant: the mega probe holds the reveal at zero through prediction build
// and begins presentation at one fixed scene frame. Worker completion speed
// must never decide when the operator sees the causal unfold begin.
constexpr int REPLAY_VISUAL_FIDELITY_START_FRAME = 900;

// Real-time causal unfold, held independently of the shipped reveal default so
// the probe's captured animation cannot move when that default changes.
constexpr double REPLAY_VISUAL_FIDELITY_REVEAL_RATE = 1.0;

void HashPredictionByte( uint64_t& hash, uint8_t value )
{
    hash ^= static_cast<uint64_t>( value );
    hash *= INTERACTION_PREDICTION_FINGERPRINT_PRIME;
}

template <typename T> void HashPredictionScalar( uint64_t& hash, T value )
{
    for ( uint8_t byte : SkullbonezCore::Core::ObjectBytes( value ) )
    {
        HashPredictionByte( hash, byte );
    }
}

void HashPredictionFloat( uint64_t& hash, float value )
{
    uint32_t bits = 0;
    std::memcpy( &bits, &value, sizeof( bits ) );
    HashPredictionScalar( hash, bits );
}

void HashPredictionVector( uint64_t& hash, const Vector3& value )
{
    HashPredictionFloat( hash, value.x );
    HashPredictionFloat( hash, value.y );
    HashPredictionFloat( hash, value.z );
}

void HashInteractionText( uint64_t& hash, const char* text, std::size_t capacity )
{
    for ( std::size_t index = 0; index < capacity && text[index] != '\0'; ++index )
    {
        HashPredictionByte( hash, static_cast<uint8_t>( text[index] ) );
    }

    HashPredictionByte( hash, 0u );
}

struct EditorSelectionFingerprint
{
    uint64_t hash = INTERACTION_PREDICTION_FINGERPRINT_OFFSET;
    bool valid = false;
    bool hasTerrain = false;
};

EditorSelectionFingerprint BuildEditorSelectionFingerprint( EditorToolsOwner& editorTools, const SceneWorld& world )
{
    EditorSelectionFingerprint fingerprint;
    const int modelIndex = PeekSelectedEditorModelIndex( editorTools.Editor(), world.BodyStore() );

    if ( modelIndex < 0 || modelIndex >= world.SceneEntityCount() )
    {
        return fingerprint;
    }

    const SceneEntityRecord& entity = world.Entities().At( modelIndex );
    const Physics::PhysicsBodyRecord* body = world.BodyStore().RecordForModelIndex( modelIndex );
    const std::span<const Physics::BuoyancyBodyFacts> buoyancyFacts = Physics::PhysicsEngine::ReadBuoyancyFacts( world.Physics() );

    const Physics::PhysicsColliderHandle colliderHandle = world.Colliders().HandleForModelIndex( modelIndex );
    const Physics::ColliderRecord* collider = world.Colliders().RecordForHandle( colliderHandle );
    const Physics::ColliderAuthoringRecord* colliderAuthoring = world.Colliders().AuthoringRecordForHandle( colliderHandle );

    EditorPrimitiveShapeSnapshot shape;

    if ( !body || !collider || !colliderAuthoring || modelIndex >= static_cast<int>( buoyancyFacts.size() ) ||
         body->sceneObjectId.value != entity.sceneObjectId.value ||
         !TryCaptureEditorPrimitiveShape( collider->shape, shape ) )
    {
        return fingerprint;
    }

    const Physics::PhysicsBodyHotState hotState = Physics::LoadPhysicsBodyHotState( world.BodyStore().HotFields(),
                                                                                    static_cast<std::size_t>( modelIndex ) );

    uint64_t& hash = fingerprint.hash;
    HashPredictionScalar( hash, entity.sceneObjectId.value );
    HashInteractionText( hash, entity.displayName, sizeof( entity.displayName ) );
    HashInteractionText( hash, entity.renderMaterial.name, sizeof( entity.renderMaterial.name ) );
    HashPredictionScalar( hash, static_cast<uint8_t>( entity.renderMaterial.kind ) );

    for ( float value : entity.renderMaterial.baseColor )
    {
        HashPredictionFloat( hash, value );
    }

    for ( float value : entity.renderMaterial.emissiveColor )
    {
        HashPredictionFloat( hash, value );
    }

    HashPredictionFloat( hash, entity.renderMaterial.emissiveStrength );
    HashPredictionFloat( hash, entity.renderMaterial.roughness );
    HashPredictionFloat( hash, entity.renderMaterial.metallic );
    HashPredictionFloat( hash, entity.renderMaterial.specular );
    HashPredictionFloat( hash, entity.renderMaterial.transmission );
    HashPredictionFloat( hash, entity.renderMaterial.stylization );
    HashPredictionFloat( hash, entity.renderMaterial.textureMode );
    HashPredictionFloat( hash, entity.renderMaterial.contactFlashAlpha );
    HashPredictionScalar( hash, entity.renderMaterial.flags );

    HashPredictionVector( hash, hotState.position );
    float orientationX = 0.0f;
    float orientationY = 0.0f;
    float orientationZ = 0.0f;
    float orientationW = 1.0f;
    hotState.orientation.GetComponents( orientationX, orientationY, orientationZ, orientationW );
    HashPredictionFloat( hash, orientationX );
    HashPredictionFloat( hash, orientationY );
    HashPredictionFloat( hash, orientationZ );
    HashPredictionFloat( hash, orientationW );
    HashPredictionVector( hash, hotState.linearVelocity );
    HashPredictionVector( hash, hotState.angularVelocity );
    HashPredictionVector( hash, body->rotationalInertia );
    HashPredictionFloat( hash, body->mass );
    HashPredictionFloat( hash, hotState.boundingRadius );
    const Physics::BuoyancyBodyFacts& fluidFacts = buoyancyFacts[static_cast<std::size_t>( modelIndex )];
    HashPredictionFloat( hash, fluidFacts.volume );
    HashPredictionFloat( hash, fluidFacts.projectedSurfaceArea );
    HashPredictionFloat( hash, fluidFacts.dragCoefficient );
    HashPredictionFloat( hash, body->contactReleaseImpulseThreshold );
    HashPredictionFloat( hash, body->angularVelocityLimit );
    HashPredictionFloat( hash, fluidFacts.contactEpsilon );
    HashPredictionScalar( hash, static_cast<uint8_t>( hotState.fixed ) );
    HashPredictionScalar( hash, static_cast<uint8_t>( !hotState.awake ) );
    HashPredictionScalar( hash, static_cast<uint8_t>( body->releasesFromFixedOnContact ) );
    HashPredictionScalar( hash, static_cast<uint8_t>( body->usesWorldInertia ) );

    // Terrain availability is one SceneWorld lifetime fact, not duplicated in
    // every Physics body row. The fingerprint retains the same boolean signal.
    fingerprint.hasTerrain = world.Terrain().Get() != nullptr;
    HashPredictionScalar( hash, static_cast<uint8_t>( fingerprint.hasTerrain ) );

    HashPredictionScalar( hash, static_cast<uint8_t>( shape.kind ) );
    HashPredictionVector( hash, shape.dimensions );
    HashPredictionVector( hash, shape.localPosition );
    HashPredictionFloat( hash, shape.dragCoefficient );
    HashPredictionFloat( hash, collider->restitution );
    HashPredictionFloat( hash, collider->friction );
    HashPredictionScalar( hash, collider->contactMaterialId );
    HashInteractionText( hash, colliderAuthoring->contactMaterialName, sizeof( colliderAuthoring->contactMaterialName ) );

    fingerprint.valid = true;
    return fingerprint;
}


const DemoPhase* ActiveDirectorPhase( const CameraControlState& camera )
{
    // Concept: phase assertions observe the same active phase that playback
    // uses. They are report-only probes and must not advance or repair director
    // state just to make a scripted screenshot line up.
    const DemoDirectorPlaybackState& director = camera.director;

    if ( !director.hasActiveShotList || director.currentPhaseIndex < 0 ||
         director.currentPhaseIndex >= director.activeShotList.phaseCount )
    {
        return nullptr;
    }

    return &director.activeShotList.phases[static_cast<std::size_t>( director.currentPhaseIndex )];
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

    if ( value == "Director" )
    {
        outMode = RunCameraMode::Director;
        return true;
    }

    return false;
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


bool TryParseVirtualKey( const std::string& value, int& outVirtualKey )
{
    if ( value.size() == 1 )
    {
        const char key = value[0];

        // Why: Interaction scripts use human key labels; Win32 virtual-key
        // values for alphanumeric keys intentionally match ASCII.
        if ( key >= 'A' && key <= 'Z' )
        {
            outVirtualKey = key;
            return true;
        }

        if ( key >= 'a' && key <= 'z' )
        {
            outVirtualKey = 'A' + ( key - 'a' );
            return true;
        }

        if ( key >= '0' && key <= '9' )
        {
            outVirtualKey = key;
            return true;
        }
    }

    // Why: visible Look Lab acceptance must hold the same F10/F11 keys across
    // sampled frames instead of relying on an OS tap that can fall between
    // Input polls.
    if ( TryParseInteractionAutomationVirtualKey( value.c_str(), outVirtualKey ) )
    {
        return true;
    }

    if ( value == "Space" || value == "space" )
    {
        outVirtualKey = VK_SPACE;
        return true;
    }

    if ( value == "Escape" || value == "Esc" || value == "esc" )
    {
        outVirtualKey = VK_ESCAPE;
        return true;
    }

    if ( value == "Shift" || value == "shift" )
    {
        outVirtualKey = VK_SHIFT;
        return true;
    }

    if ( value == "Control" || value == "Ctrl" || value == "ctrl" )
    {
        outVirtualKey = VK_CONTROL;
        return true;
    }

    if ( value == "Up" || value == "up" )
    {
        outVirtualKey = VK_UP;
        return true;
    }

    if ( value == "Down" || value == "down" )
    {
        outVirtualKey = VK_DOWN;
        return true;
    }

    if ( value == "Left" || value == "left" )
    {
        outVirtualKey = VK_LEFT;
        return true;
    }

    if ( value == "Right" || value == "right" )
    {
        outVirtualKey = VK_RIGHT;
        return true;
    }

    if ( value == "Backspace" || value == "backspace" )
    {
        outVirtualKey = VK_BACK;
        return true;
    }

    if ( value == "Enter" || value == "Return" || value == "enter" || value == "return" )
    {
        outVirtualKey = VK_RETURN;
        return true;
    }

    if ( value == "Tab" || value == "tab" )
    {
        outVirtualKey = VK_TAB;
        return true;
    }

    if ( value == "Tilde" || value == "tilde" )
    {
        outVirtualKey = VK_OEM_3;
        return true;
    }

    if ( value == "Comma" || value == "comma" )
    {
        // Why: visual acceptance drives the same comma-owned presentation
        // command as a physical key, so mode order and UI reflection are tested
        // through the production input route.
        outVirtualKey = VK_OEM_COMMA;
        return true;
    }

    if ( value == "Delete" || value == "delete" )
    {
        outVirtualKey = VK_DELETE;
        return true;
    }

    if ( value == "Alt" || value == "alt" )
    {
        outVirtualKey = VK_MENU;
        return true;
    }

    return false;
}

bool ReadAutomationVec3( const Json& value, Vector3& out )
{
    if ( !value.is_array() || value.size() != 3u || !value[0].is_number() || !value[1].is_number() || !value[2].is_number() )
    {
        return false;
    }

    out.x = value[0].get<float>();
    out.y = value[1].get<float>();
    out.z = value[2].get<float>();
    return true;
}

bool ReadAutomationCameraPose( const Json& value, DemoCameraPose& out, std::string& outError )
{
    if ( !value.is_object() )
    {
        outError = "setCameraPose must be an object";
        return false;
    }

    if ( !value.contains( "position" ) || !ReadAutomationVec3( value["position"], out.eye ) )
    {
        outError = "setCameraPose.position must be a 3-number array";
        return false;
    }

    if ( !value.contains( "view" ) || !ReadAutomationVec3( value["view"], out.view ) )
    {
        outError = "setCameraPose.view must be a 3-number array";
        return false;
    }

    if ( !value.contains( "up" ) || !ReadAutomationVec3( value["up"], out.up ) )
    {
        outError = "setCameraPose.up must be a 3-number array";
        return false;
    }

    return true;
}

const char* ActionTypeName( RunInteractionAutomationActionType type )
{
    switch ( type )
    {
    case RunInteractionAutomationActionType::LoadShotList:
        return "loadShotList";
    case RunInteractionAutomationActionType::DirectorPlay:
        return "directorPlay";
    case RunInteractionAutomationActionType::DirectorAdvance:
        return "directorAdvance";
    case RunInteractionAutomationActionType::DirectorGrab:
        return "directorGrab";
    case RunInteractionAutomationActionType::DirectorRelease:
        return "directorRelease";
    case RunInteractionAutomationActionType::SetPhaseStyle:
        return "setPhaseStyle";
    case RunInteractionAutomationActionType::SetCameraPose:
        return "setCameraPose";
    case RunInteractionAutomationActionType::SetCameraMode:
        return "setCameraMode";
    case RunInteractionAutomationActionType::LoseFocus:
        return "loseFocus";
    case RunInteractionAutomationActionType::MoveMouse:
        return "moveMouse";
    case RunInteractionAutomationActionType::ScrollPoint:
        return "scrollPoint";
    case RunInteractionAutomationActionType::ClickObject:
        return "clickObject";
    case RunInteractionAutomationActionType::ClickPoint:
        return "clickPoint";
    case RunInteractionAutomationActionType::ClickReplayControl:
        return "clickReplayControl";
    case RunInteractionAutomationActionType::ScrubReplaySolverTrack:
        return "scrubReplaySolverTrack";
    case RunInteractionAutomationActionType::SelectReplayCauseRow:
        return "selectReplayCauseRow";
    case RunInteractionAutomationActionType::ScrubEditorReplayTrack:
        return "scrubEditorReplayTrack";
    case RunInteractionAutomationActionType::SetContinuousForecastCommand:
        return "setContinuousForecastCommand";
    case RunInteractionAutomationActionType::SetReplayPredictionEnabled:
        return "setReplayPredictionEnabled";
    case RunInteractionAutomationActionType::SetReplayPredictionHorizonSeconds:
        return "setReplayPredictionHorizonSeconds";
    case RunInteractionAutomationActionType::BeginReplayVisualFidelityCapture:
        return "beginReplayVisualFidelityCapture";
    case RunInteractionAutomationActionType::SetReplayPathTarget:
        return "setReplayPathTarget";
    case RunInteractionAutomationActionType::SetReplayInterceptTarget:
        return "setReplayInterceptTarget";
    case RunInteractionAutomationActionType::SetReplayTripPlannerCommand:
        return "setReplayTripPlannerCommand";
    case RunInteractionAutomationActionType::NudgeReplayPathTargetVelocity:
        return "nudgeReplayPathTargetVelocity";
    case RunInteractionAutomationActionType::ShowReplayScrubber:
        return "showReplayScrubber";
    case RunInteractionAutomationActionType::PressKey:
        return "pressKey";
    case RunInteractionAutomationActionType::CaptureEditorSelectionState:
        return "captureEditorSelectionState";
    case RunInteractionAutomationActionType::LoadScene:
        return "loadScene";
    case RunInteractionAutomationActionType::SetDevelopmentUiSurface:
        return "setDevelopmentUiSurface";
    case RunInteractionAutomationActionType::SetImGuiPanelVisible:
        return "setImGuiPanelVisible";
    case RunInteractionAutomationActionType::ResetImGuiLayout:
        return "resetImGuiLayout";
    case RunInteractionAutomationActionType::FocusImGuiPanel:
        return "focusImGuiPanel";
    case RunInteractionAutomationActionType::SetImGuiDpiScale:
        return "setImGuiDpiScale";
    case RunInteractionAutomationActionType::ResizeWindow:
        return "resizeWindow";
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
    case RunInteractionAutomationAssertKind::DirectorGrabbed:
        return "directorGrabbed";
    case RunInteractionAutomationAssertKind::DirectorPhaseIndex:
        return "directorPhaseIndex";
    case RunInteractionAutomationAssertKind::DirectorPhaseName:
        return "directorPhaseName";
    case RunInteractionAutomationAssertKind::DirectorPhaseStylePath:
        return "directorPhaseStylePath";
    case RunInteractionAutomationAssertKind::ReplayPredictionEnabled:
        return "replayPredictionEnabled";
    case RunInteractionAutomationAssertKind::ReplayPathTarget:
        return "replayPathTarget";
    case RunInteractionAutomationAssertKind::ReplayInterceptContact:
        return "replayInterceptContact";
    case RunInteractionAutomationAssertKind::ReplayInterceptMissMax:
        return "replayInterceptMissMax";
    case RunInteractionAutomationAssertKind::ReplayInterceptEtaMin:
        return "replayInterceptEtaMin";
    case RunInteractionAutomationAssertKind::ReplayInterceptEtaMax:
        return "replayInterceptEtaMax";
    case RunInteractionAutomationAssertKind::ReplayTripPlannerState:
        return "replayTripPlannerState";
    case RunInteractionAutomationAssertKind::ReplayTripPlannerIterationMax:
        return "replayTripPlannerIterationMax";
    case RunInteractionAutomationAssertKind::ReplayTripPlannerMissMax:
        return "replayTripPlannerMissMax";
    case RunInteractionAutomationAssertKind::ReplayTripPlannerMissesImprove:
        return "replayTripPlannerMissesImprove";
    case RunInteractionAutomationAssertKind::ReplayPorkchopComplete:
        return "replayPorkchopComplete";
    case RunInteractionAutomationAssertKind::ReplayPorkchopMinimumDeltaVMax:
        return "replayPorkchopMinimumDeltaVMax";
    case RunInteractionAutomationAssertKind::ReplayPorkchopMinimumDepartureDelayMax:
        return "replayPorkchopMinimumDepartureDelayMax";
    case RunInteractionAutomationAssertKind::ReplayPorkchopMinimumTimeOfFlightMin:
        return "replayPorkchopMinimumTimeOfFlightMin";
    case RunInteractionAutomationAssertKind::ReplayPorkchopMinimumTimeOfFlightMax:
        return "replayPorkchopMinimumTimeOfFlightMax";
    case RunInteractionAutomationAssertKind::ReplayPorkchopRefreshMillisecondsMax:
        return "replayPorkchopRefreshMillisecondsMax";
    case RunInteractionAutomationAssertKind::ReplayPorkchopMaximumFrameMillisecondsMax:
        return "replayPorkchopMaximumFrameMillisecondsMax";
    case RunInteractionAutomationAssertKind::ReplayPorkchopSweepAgeSecondsMax:
        return "replayPorkchopSweepAgeSecondsMax";
    case RunInteractionAutomationAssertKind::ReplayPorkchopSelected:
        return "replayPorkchopSelected";
    case RunInteractionAutomationAssertKind::ReplayTripPlannerTimeOfFlightMin:
        return "replayTripPlannerTimeOfFlightMin";
    case RunInteractionAutomationAssertKind::ReplayTripPlannerTimeOfFlightMax:
        return "replayTripPlannerTimeOfFlightMax";
    case RunInteractionAutomationAssertKind::ReplayPastTrajectoryFullRebuildCountMax:
        return "replayPastTrajectoryFullRebuildCountMax";
    case RunInteractionAutomationAssertKind::ReplayPastTrajectoryIncrementalTrimCountMin:
        return "replayPastTrajectoryIncrementalTrimCountMin";
    case RunInteractionAutomationAssertKind::ReplayPastTrajectoryPublishedPointCountMin:
        return "replayPastTrajectoryPublishedPointCountMin";
    case RunInteractionAutomationAssertKind::PredictionPathVisible:
        return "predictionPathVisible";
    case RunInteractionAutomationAssertKind::PredictionVelocityPreviewActive:
        return "predictionVelocityPreviewActive";
    case RunInteractionAutomationAssertKind::PredictionVelocityPreviewAwaitingReplacement:
        return "predictionVelocityPreviewAwaitingReplacement";
    case RunInteractionAutomationAssertKind::PredictionVelocityPreviewDeltaMin:
        return "predictionVelocityPreviewDeltaMin";
    case RunInteractionAutomationAssertKind::PredictionPresentedGenerationMin:
        return "predictionPresentedGenerationMin";
    case RunInteractionAutomationAssertKind::PredictionPresentedRootVelocityDeltaMin:
        return "predictionPresentedRootVelocityDeltaMin";
    case RunInteractionAutomationAssertKind::PredictionFullHorizonComplete:
        return "predictionFullHorizonComplete";
    case RunInteractionAutomationAssertKind::PredictionBuildMode:
        return "predictionBuildMode";
    case RunInteractionAutomationAssertKind::PredictionSupersededRestartCountMin:
        return "predictionSupersededRestartCountMin";
    case RunInteractionAutomationAssertKind::PredictionSupersededRestartCountMax:
        return "predictionSupersededRestartCountMax";
    case RunInteractionAutomationAssertKind::PredictionBaselineVisible:
        return "predictionBaselineVisible";
    case RunInteractionAutomationAssertKind::PredictionDivergenceMin:
        return "predictionDivergenceMin";
    case RunInteractionAutomationAssertKind::ReplaySolverTrackAtPresent:
        return "replaySolverTrackAtPresent";
    case RunInteractionAutomationAssertKind::PredictionScrubFrameActive:
        return "predictionScrubFrameActive";
    case RunInteractionAutomationAssertKind::PredictionTargetDisplacementMin:
        return "predictionTargetDisplacementMin";
    case RunInteractionAutomationAssertKind::PredictionTargetLastNear:
        return "predictionTargetLastNear";
    case RunInteractionAutomationAssertKind::LiveSolverHashStableAcrossPrediction:
        return "liveSolverHashStableAcrossPrediction";
    case RunInteractionAutomationAssertKind::PredictionEvidenceConsumerBalanced:
        return "predictionEvidenceConsumerBalanced";
    case RunInteractionAutomationAssertKind::PredictionEvidencePipelineRowsMin:
        return "predictionEvidencePipelineRowsMin";
    case RunInteractionAutomationAssertKind::PredictionEvidenceCurrentCapacityMax:
        return "predictionEvidenceCurrentCapacityMax";
    case RunInteractionAutomationAssertKind::PredictionDetailMode:
        return "predictionDetailMode";
    case RunInteractionAutomationAssertKind::PredictionCauseDetailVisible:
        return "predictionCauseDetailVisible";
    case RunInteractionAutomationAssertKind::PredictionCauseWindowAvailable:
        return "predictionCauseWindowAvailable";
    case RunInteractionAutomationAssertKind::PredictionEvidenceCapacityReleased:
        return "predictionEvidenceCapacityReleased";
    case RunInteractionAutomationAssertKind::PredictionEvidenceMemoryReconciled:
        return "predictionEvidenceMemoryReconciled";
    case RunInteractionAutomationAssertKind::PredictionCauseManifoldRowsMin:
        return "predictionCauseManifoldRowsMin";
    case RunInteractionAutomationAssertKind::PredictionCauseManifoldRowsMax:
        return "predictionCauseManifoldRowsMax";
    case RunInteractionAutomationAssertKind::PredictionCauseSolverRowsMin:
        return "predictionCauseSolverRowsMin";
    case RunInteractionAutomationAssertKind::PredictionCauseSolverRowsMax:
        return "predictionCauseSolverRowsMax";
    case RunInteractionAutomationAssertKind::PredictionCauseSyntheticRowsMin:
        return "predictionCauseSyntheticRowsMin";
    case RunInteractionAutomationAssertKind::PredictionCauseSyntheticRowsMax:
        return "predictionCauseSyntheticRowsMax";
    case RunInteractionAutomationAssertKind::PredictionTrajectoryFingerprintReady:
        return "predictionTrajectoryFingerprintReady";
    case RunInteractionAutomationAssertKind::PredictionAppearanceInvalidationCountMin:
        return "predictionAppearanceInvalidationCountMin";
    case RunInteractionAutomationAssertKind::ContinuousForecastActive:
        return "continuousForecastActive";
    case RunInteractionAutomationAssertKind::ContinuousForecastPreWrap:
        return "continuousForecastPreWrap";
    case RunInteractionAutomationAssertKind::ContinuousForecastWindowWrapped:
        return "continuousForecastWindowWrapped";
    case RunInteractionAutomationAssertKind::ContinuousForecastPresentationCoherent:
        return "continuousForecastPresentationCoherent";
    case RunInteractionAutomationAssertKind::ContinuousForecastAbsoluteTickMin:
        return "continuousForecastAbsoluteTickMin";
    case RunInteractionAutomationAssertKind::ContinuousForecastOldestTickMin:
        return "continuousForecastOldestTickMin";
    case RunInteractionAutomationAssertKind::ContinuousForecastRibbonSegmentsMin:
        return "continuousForecastRibbonSegmentsMin";
    case RunInteractionAutomationAssertKind::ContinuousForecastHeadMarkerCount:
        return "continuousForecastHeadMarkerCount";
    case RunInteractionAutomationAssertKind::ShadowPassExecuted:
        return "shadowPassExecuted";
    case RunInteractionAutomationAssertKind::TerrainShadowValid:
        return "terrainShadowValid";
    case RunInteractionAutomationAssertKind::ObjectShadowValid:
        return "objectShadowValid";
    case RunInteractionAutomationAssertKind::ReflectionPassExecuted:
        return "reflectionPassExecuted";
    case RunInteractionAutomationAssertKind::GizmoVisible:
        return "gizmoVisible";
    case RunInteractionAutomationAssertKind::MousePickupActive:
        return "mousePickupActive";
    case RunInteractionAutomationAssertKind::PointerCapture:
        return "pointerCapture";
    case RunInteractionAutomationAssertKind::NativeCaptureRequested:
        return "nativeCaptureRequested";
    case RunInteractionAutomationAssertKind::CursorVisibleRequested:
        return "cursorVisibleRequested";
    case RunInteractionAutomationAssertKind::UiBlocksMouse:
        return "uiBlocksMouse";
    case RunInteractionAutomationAssertKind::LauncherRayActive:
        return "launcherRayActive";
    case RunInteractionAutomationAssertKind::ReplayActiveTrack:
        return "replayActiveTrack";
    case RunInteractionAutomationAssertKind::ReplayHistoricalSamplePaused:
        return "replayHistoricalSamplePaused";
    case RunInteractionAutomationAssertKind::MemoryOverlayEnabled:
        return "memoryOverlayEnabled";
    case RunInteractionAutomationAssertKind::EditorUndoDepth:
        return "editorUndoDepth";
    case RunInteractionAutomationAssertKind::EditorRedoDepth:
        return "editorRedoDepth";
    case RunInteractionAutomationAssertKind::EditorSelectionExists:
        return "editorSelectionExists";
    case RunInteractionAutomationAssertKind::EditorSelectionHasTerrain:
        return "editorSelectionHasTerrain";
    case RunInteractionAutomationAssertKind::EditorSelectionMatchesCapture:
        return "editorSelectionMatchesCapture";
    case RunInteractionAutomationAssertKind::DevelopmentUiSurface:
        return "developmentUiSurface";
    case RunInteractionAutomationAssertKind::ImGuiVisible:
        return "imguiVisible";
    case RunInteractionAutomationAssertKind::GameUiReplayPresentationActive:
        return "gameUiReplayPresentationActive";
    case RunInteractionAutomationAssertKind::ImGuiPanelMask:
        return "imguiPanelMask";
    case RunInteractionAutomationAssertKind::ImGuiLayoutResetCountMin:
        return "imguiLayoutResetCountMin";
    case RunInteractionAutomationAssertKind::ImGuiFocusCountMin:
        return "imguiFocusCountMin";
    case RunInteractionAutomationAssertKind::ImGuiDpiScale:
        return "imguiDpiScale";
    case RunInteractionAutomationAssertKind::ImGuiDescriptorHighWaterMax:
        return "imguiDescriptorHighWaterMax";
    case RunInteractionAutomationAssertKind::ImGuiViewportRecreationsMin:
        return "imguiViewportRecreationsMin";
    case RunInteractionAutomationAssertKind::ImGuiPreferencesRecovered:
        return "imguiPreferencesRecovered";
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

bool IsBoolValue( const Json& value )
{
    return value.is_boolean() || value.is_number_integer() || value.is_string();
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

void AppendReportAction( InteractionAutomationController& state, int frame, RunInteractionAutomationActionType type,
                         const char* target, const POINT* mouse, bool consumed, const char* detail )
{
    state.reportWriter.AppendAction( frame, ActionTypeName( type ), target, mouse, consumed, detail );
}

void InjectAutomationLeftMousePress( InteractionAutomationController& state, RunInteractionAutomationAction& action,
                                     int frame, const SkullbonezCore::UI::UIRect& rect )
{
    POINT mouse = {};
    mouse.x = static_cast<LONG>( rect.x + rect.w * 0.5f );
    mouse.y = static_cast<LONG>( rect.y + rect.h * 0.5f );
    state.inputDriver.MoveMouse( mouse );
    state.inputDriver.PressMouse( false, frame, 1 );
    action.mouse = mouse;
    action.hasMouse = true;
}

void FailAutomation( InteractionAutomationController& state, const char* message )
{
    state.status.Fail( message );
}

void ApplyInteractionAutomationDirectorCameraAction( InteractionAutomationController& state,
                                                     SkullbonezCore::Environment::CameraCollection& cameras,
                                                     CameraControlState& camera, RunInteractionAutomationAction& action,
                                                     InteractionAutomationFrameResult& result, int frame )
{
    // Concept: director/camera automation seeds the same camera and director
    // owners used by live authoring. Camera-mode transitions are routed by the
    // caller through InputRouter before this helper handles director-local work.
    DemoCameraPose currentPose;
    currentPose.eye = cameras.GetCameraTranslation();
    currentPose.view = cameras.GetCameraView();
    currentPose.up = cameras.GetCameraUp();

    switch ( action.type )
    {
    case RunInteractionAutomationActionType::LoadShotList:
    {
        const bool loaded = DemoDirectorPlayback::LoadShotList( camera.director, currentPose, action.path );

        if ( !loaded )
        {
            FailAutomation( state, "failed to load director shot list" );
        }

        AppendReportAction( state, frame, action.type, action.path, nullptr, loaded,
                            loaded ? "shot list loaded" : "shot list unavailable" );

        break;
    }
    case RunInteractionAutomationActionType::DirectorAdvance:
    {
        const bool advanced = DemoDirectorPlayback::AdvancePhase( camera.director, currentPose );

        if ( !advanced )
        {
            FailAutomation( state, "failed to advance director phase" );
        }

        AppendReportAction( state, frame, action.type, "", nullptr, advanced,
                            advanced ? "director phase advanced" : "director phase unavailable" );

        break;
    }
    case RunInteractionAutomationActionType::DirectorGrab:
    {
        DemoDirectorCameraCommand cameraCommand;
        const bool grabbed = DemoDirectorPlayback::BeginGrab( camera.director, camera.mode == RunCameraMode::Director,
                                                              currentPose, cameraCommand );

        if ( cameraCommand.applyPose )
        {
            result.applyDirectorCameraPose = true;
            result.directorCameraPose = cameraCommand.pose;
        }

        if ( !grabbed )
        {
            FailAutomation( state, "failed to grab director camera" );
        }

        AppendReportAction( state, frame, action.type, "", nullptr, grabbed,
                            grabbed ? "director camera grabbed" : "director grab unavailable" );

        break;
    }
    case RunInteractionAutomationActionType::DirectorRelease:
    {
        const bool released = DemoDirectorPlayback::EndGrab( camera.director, camera.mode == RunCameraMode::Director,
                                                             currentPose );

        if ( !released )
        {
            FailAutomation( state, "failed to release director camera" );
        }

        AppendReportAction( state, frame, action.type, "", nullptr, released,
                            released ? "director camera released" : "director release unavailable" );

        break;
    }
    case RunInteractionAutomationActionType::SetPhaseStyle:
    {
        const bool applied = DemoDirectorPlayback::SetCurrentPhaseStyle( camera.director, action.path );

        if ( !applied )
        {
            FailAutomation( state, "failed to set director phase style" );
        }

        AppendReportAction( state, frame, action.type, action.path, nullptr, applied,
                            applied ? "director phase style set" : "director phase unavailable" );

        break;
    }
    case RunInteractionAutomationActionType::SetCameraPose:
    {
        const bool applied = true;

        // Why: pose-authoring proofs seed the current camera, then use normal
        // J/L key handling to write and save the shot list.
        result.applyDirectorCameraPose = true;
        result.directorCameraPose = action.cameraPose;
        AppendReportAction( state, frame, action.type, "", nullptr, applied,
                            applied ? "camera pose applied" : "camera unavailable" );

        break;
    }
    default:
        break;
    }
}

void PublishReplayScrubberVisibility( ReplayFrameIntent& intent, bool visible, double now, double holdSeconds )
{
    intent.setScrubberVisibility = true;
    intent.scrubberVisible = visible;
    intent.scrubberNow = now;
    intent.scrubberHoldSeconds = holdSeconds;
}


void PublishReplayPredictionEnabled( ReplayFrameIntent& intent, bool enabled )
{
    intent.setPredictionEnabled = true;
    intent.predictionEnabled = enabled;
}


void PublishReplayPredictionHorizon( ReplayFrameIntent& intent, float horizonSeconds )
{
    intent.setPredictionHorizon = true;
    intent.predictionHorizonSeconds = horizonSeconds;
}


bool PrepareReplayVelocityMutationBaseline( const ReplayAutomationView& replay, ReplayFrameIntent& intent )
{
    const bool prepared = ( replay.prediction.build.complete && replay.activePredictionFrames.size() >= 2u ) ||
                          replay.prediction.baseline.comparisonActive;

    intent.prepareVelocityMutationBaseline = true;
    return prepared;
}


void CommitReplayVelocityMutation( ReplayFrameIntent& intent )
{
    intent.commitVelocityMutation = true;
}


bool ReplayDeterministicRevealReady( const ReplayAutomationView& replay )
{
    return !replay.prediction.build.building && replay.activePredictionFrames.size() >= 2u &&
           replay.prediction.build.complete;
}


void PublishReplayDeterministicReveal( ReplayFrameIntent& intent, ReplayFrameIndex frame, bool resetPresentedFrame )
{
    intent.armDeterministicReveal = true;
    intent.revealFrame = frame;
    intent.resetPresentedRevealFrame = resetPresentedFrame;
}


template <typename TrySetReplayPathTarget, typename TrySetReplayInterceptTarget,
          typename SetWorldInteractionOwnerAfterTransition>
void ApplyInteractionAutomationReplayStateAction( InteractionAutomationController& state,
                                                  const RuntimeFrameMetricsSnapshot& timers, ReplayFrameIntent& replayIntent,
                                                  const ReplayAutomationView& replay, Physics::PhysicsEngine& physics,
                                                  RunInteractionAutomationAction& action, int frame,
                                                  TrySetReplayPathTarget trySetReplayPathTarget,
                                                  TrySetReplayInterceptTarget trySetReplayInterceptTarget,
                                                  SetWorldInteractionOwnerAfterTransition setWorldInteractionOwner )
{
    // Concept: replay state automation changes only harness-visible replay
    // controls. Direct physics mutation is limited to the velocity-edit proof
    // path and still marks prediction dirty so replay owners rebuild outputs.
    switch ( action.type )
    {
    case RunInteractionAutomationActionType::ShowReplayScrubber:
        PublishReplayScrubberVisibility( replayIntent, action.boolValue, timers.simulationTotalSeconds, 5.0 );
        AppendReportAction( state, frame, action.type, "", nullptr, true, action.boolValue ? "visible" : "hidden" );
        break;
    case RunInteractionAutomationActionType::SetReplayPredictionEnabled:
        PublishReplayPredictionEnabled( replayIntent, action.boolValue );
        setWorldInteractionOwner( action.boolValue ? WorldInteractionOwner::ReplayPrediction : WorldInteractionOwner::None,
                                  InteractionExitReason::EnterReplay );

        AppendReportAction( state, frame, action.type, "", nullptr, true,
                            action.boolValue ? "prediction enabled" : "prediction disabled" );

        break;
    case RunInteractionAutomationActionType::SetReplayPathTarget:
    {
        const bool targetSet = trySetReplayPathTarget( action.text );

        if ( !targetSet )
        {
            FailAutomation( state, "failed to set replay path target" );
        }

        AppendReportAction( state, frame, action.type, action.text, nullptr, targetSet,
                            targetSet ? "replay path target set" : "replay path target unavailable" );

        break;
    }
    case RunInteractionAutomationActionType::SetReplayInterceptTarget:
    {
        const bool targetSet = trySetReplayInterceptTarget( action.text );

        if ( !targetSet )
        {
            FailAutomation( state, "failed to set replay intercept target" );
        }

        AppendReportAction( state, frame, action.type, action.text, nullptr, targetSet,
                            targetSet ? "replay intercept target set" : "replay intercept target unavailable" );

        break;
    }
    case RunInteractionAutomationActionType::SetReplayTripPlannerCommand:
        replayIntent.hasTripPlannerCommand = true;
        replayIntent.tripPlannerCommand.kind = action.tripPlannerCommand;
        replayIntent.tripPlannerCommand.timeOfFlightSeconds = action.numberValue;
        AppendReportAction( state, frame, action.type, action.text, nullptr, true, "trip planner command queued" );
        break;
    case RunInteractionAutomationActionType::SetReplayPredictionHorizonSeconds:
    {
        const float horizonSeconds = std::clamp( action.numberValue, REPLAY_PREDICTION_MIN_SECONDS,
                                                 REPLAY_PREDICTION_MAX_SECONDS );

        // Why: automation should use the same bounded horizon value the replay UI
        // exposes, while still forcing a rebuild when a script changes it before
        // a proof.
        PublishReplayPredictionHorizon( replayIntent, horizonSeconds );

        // Why: this text exists only in the machine-readable automation report.
        // Keep stream/string formatting in Diagnostics even though the scripted
        // action executes inside the steady-gameplay input phase.
        CoreAllocation::RuntimeAllocationScope diagnosticsAllocationScope( CoreAllocation::RuntimeAllocationPhase::Diagnostics );

        std::ostringstream detail;
        detail << "prediction horizon set to " << horizonSeconds << "s";
        AppendReportAction( state, frame, action.type, "", nullptr, true, detail.str().c_str() );
        break;
    }
    case RunInteractionAutomationActionType::NudgeReplayPathTargetVelocity:
    {
        const Physics::PhysicsBodyStore& bodyStore = SkullbonezCore::Physics::PhysicsEngine::ReadBodies( physics );
        const Physics::PhysicsBodyHandle body = bodyStore.HandleForSceneObjectId( replay.path.targetId,
                                                                                  replay.path.targetModelRow.value );

        const Physics::PhysicsBodyRecord* record = bodyStore.RecordForHandle( body );
        const int bodyIndex = bodyStore.ModelIndexForHandle( body );
        const bool hasTarget = replay.path.hasTarget && replay.path.targetId.value != 0;
        bool applied = false;

        if ( hasTarget && record && bodyIndex >= 0 )
        {
            if ( !PrepareReplayVelocityMutationBaseline( replay, replayIntent ) )
            {
                FailAutomation( state, "replay path target velocity nudge requires a completed prediction baseline" );
            }
            else
            {
                // Why: automation needs the same old-vs-new future proof as a
                // mouse drag, but without depending on pixel-perfect axis hit
                // testing. Capture is still deferred to the visualizer.
                const Physics::PhysicsBodyHotState hotState = Physics::LoadPhysicsBodyHotState( bodyStore.HotFields(),
                                                                                                static_cast<std::size_t>( bodyIndex ) );

                const Vector3 nextLinearVelocity = hotState.linearVelocity + action.vectorValue;
                applied = physics.SetBodyVelocity( body, nextLinearVelocity, hotState.angularVelocity, true );

                if ( applied )
                {
                    CommitReplayVelocityMutation( replayIntent );
                    PublishReplayScrubberVisibility( replayIntent, true, timers.simulationTotalSeconds,
                                                     REPLAY_SCRUBBER_VISIBLE_SECONDS );

                    setWorldInteractionOwner( WorldInteractionOwner::ReplayVelocityEdit,
                                              InteractionExitReason::EnterReplay );
                }
            }
        }
        else
        {
            FailAutomation( state, "failed to resolve replay path target for velocity nudge" );
        }

        if ( !applied && !state.status.failed )
        {
            FailAutomation( state, "failed to apply replay path target velocity nudge" );
        }

        AppendReportAction( state, frame, action.type, action.text, nullptr, applied,
                            applied ? "path target velocity nudged" : "path target velocity nudge failed" );

        break;
    }
    default:
        break;
    }
}

void ShowInteractionAutomationReplayScrubber( const RuntimeFrameMetricsSnapshot& timers, ReplayFrameIntent& replayIntent )
{
    PublishReplayScrubberVisibility( replayIntent, true, timers.simulationTotalSeconds, REPLAY_SCRUBBER_VISIBLE_SECONDS );
}

void AppendInteractionAutomationReplayControlFailure( InteractionAutomationController& state, int frame,
                                                      const RunInteractionAutomationAction& action, const char* failure,
                                                      const char* detail )
{
    FailAutomation( state, failure );
    AppendReportAction( state, frame, action.type, action.text, nullptr, false, detail );
}

void InjectInteractionAutomationReplayControlClick( InteractionAutomationController& state,
                                                    const RuntimeFrameMetricsSnapshot& timers,
                                                    ReplayFrameIntent& replayIntent, RunInteractionAutomationAction& action,
                                                    int frame, const SkullbonezCore::UI::UIRect& rect, const char* detail )
{
    InjectAutomationLeftMousePress( state, action, frame, rect );
    ShowInteractionAutomationReplayScrubber( timers, replayIntent );
    AppendReportAction( state, frame, action.type, action.text, &action.mouse, true, detail );
}

void ApplyInteractionAutomationReplayControlClick( InteractionAutomationController& state, Window* window,
                                                   const SkullbonezCore::Core::EngineConfig& config,
                                                   const SceneSessionState& scene, const RuntimeFrameMetricsSnapshot& timers,
                                                   ReplayFrameIntent& replayIntent, const ReplayAutomationView& replay,
                                                   RunInteractionAutomationAction& action, int frame )
{
    // Concept: replay-control automation clicks the visible scrubber widgets
    // instead of mutating replay state directly. Normal replay input remains the
    // owner of prediction, detail-mode, velocity-edit, and branch transitions.
    if ( strcmp( action.text, "highDetail" ) == 0 )
    {
        const int screenW = window ? window->ClientWidth() : config.window.screenX;
        const int screenH = window ? window->ClientHeight() : config.window.screenY;
        const ReplayRecorderStats solverReplayStats = replay.solverStats;
        const bool predictionToolsEnabled = solverReplayStats.enabled && scene.isScenePhysics;

        if ( screenW > 0 && screenH > 0 && predictionToolsEnabled )
        {
            InjectInteractionAutomationReplayControlClick( state, timers, replayIntent, action, frame,
                                                           ReplayScrubberHighDetailToggleRect( screenW, screenH ),
                                                           "mouse press injected at high-detail toggle" );
        }
        else
        {
            AppendInteractionAutomationReplayControlFailure( state, frame, action, "replay high-detail control unavailable",
                                                             "replay high-detail control unavailable" );
        }

        return;
    }

    if ( strcmp( action.text, "predict" ) == 0 )
    {
        const int screenW = window ? window->ClientWidth() : config.window.screenX;
        const int screenH = window ? window->ClientHeight() : config.window.screenY;
        const ReplayRecorderStats solverReplayStats = replay.solverStats;

        // Why: interaction scripts should match the real UI: Predict can branch
        // from the current live solver state even before a paused scene has
        // accumulated two retained solver samples.
        const bool predictionToolsEnabled = solverReplayStats.enabled && scene.isScenePhysics;

        if ( screenW > 0 && screenH > 0 && predictionToolsEnabled )
        {
            InjectInteractionAutomationReplayControlClick( state, timers, replayIntent, action, frame,
                                                           ReplayScrubberPredictToggleRect( screenW, screenH ),
                                                           "mouse press injected at predict toggle" );
        }
        else
        {
            AppendInteractionAutomationReplayControlFailure( state, frame, action, "replay predict control unavailable",
                                                             "replay predict control unavailable" );
        }

        return;
    }

    if ( strcmp( action.text, "past" ) == 0 || strcmp( action.text, "pastPath" ) == 0 )
    {
        const int screenW = window ? window->ClientWidth() : config.window.screenX;
        const int screenH = window ? window->ClientHeight() : config.window.screenY;
        const ReplayRecorderStats solverReplayStats = replay.solverStats;
        const bool pastPathControlEnabled = solverReplayStats.enabled && solverReplayStats.sampleCount >= 2 &&
                                            replay.path.hasTarget;

        if ( screenW > 0 && screenH > 0 && pastPathControlEnabled )
        {
            InjectInteractionAutomationReplayControlClick( state, timers, replayIntent, action, frame,
                                                           ReplayScrubberPastPathToggleRect( screenW, screenH ),
                                                           "mouse press injected at past-path toggle" );
        }
        else
        {
            AppendInteractionAutomationReplayControlFailure( state, frame, action, "replay past-path control unavailable",
                                                             "replay past-path control unavailable" );
        }

        return;
    }

    if ( strcmp( action.text, "velocity" ) == 0 )
    {
        const int screenW = window ? window->ClientWidth() : config.window.screenX;
        const int screenH = window ? window->ClientHeight() : config.window.screenY;
        const ReplayRecorderStats solverReplayStats = replay.solverStats;
        const bool solverToolsEnabled = solverReplayStats.enabled && solverReplayStats.sampleCount >= 2;

        if ( screenW > 0 && screenH > 0 && solverToolsEnabled )
        {
            // Concept: velocity automation toggles the visible scrubber control,
            // then lets the next scripted world click exercise replay velocity
            // targeting through normal input ownership.
            InjectInteractionAutomationReplayControlClick( state, timers, replayIntent, action, frame,
                                                           ReplayScrubberVelocityEditToggleRect( screenW, screenH ),
                                                           "mouse press injected at velocity toggle" );
        }
        else
        {
            AppendInteractionAutomationReplayControlFailure( state, frame, action, "replay velocity control unavailable",
                                                             "replay velocity control unavailable" );
        }

        return;
    }

    if ( strcmp( action.text, "branch" ) == 0 )
    {
        const int screenW = window ? window->ClientWidth() : config.window.screenX;
        const int screenH = window ? window->ClientHeight() : config.window.screenY;
        const ReplayRecorderStats solverReplayStats = replay.solverStats;
        const bool branchTargetAvailable = replay.scrubber.historicalSamplePaused &&
                                           replay.scrubber.activeTrack == RunReplayTrack::Solver &&
                                           solverReplayStats.enabled && solverReplayStats.sampleCount >= 2 &&
                                           replay.currentSolverSample != nullptr;

        if ( screenW > 0 && screenH > 0 && branchTargetAvailable )
        {
            // Why: branch-restore proof clicks the visible Branch rectangle
            // after a scripted scrub, so TickReplayScrubberInput remains the
            // owner of the restore.
            InjectInteractionAutomationReplayControlClick( state, timers, replayIntent, action, frame,
                                                           ReplayScrubberBranchButtonRect( screenW, screenH ),
                                                           "mouse press injected at branch restore button" );
        }
        else
        {
            AppendInteractionAutomationReplayControlFailure( state, frame, action, "replay branch control unavailable",
                                                             "replay branch control unavailable" );
        }

        return;
    }

    if ( strcmp( action.text, "causeTabSummary" ) == 0 || strcmp( action.text, "causeTabRawRecord" ) == 0 ||
         strcmp( action.text, "causeTabIterations" ) == 0 || strcmp( action.text, "causeCloseDrawer" ) == 0 ||
         strcmp( action.text, "causeCopyRawRecord" ) == 0 )
    {
        const int screenW = window ? window->ClientWidth() : config.window.screenX;
        const int screenH = window ? window->ClientHeight() : config.window.screenY;
        const ReplayCauseInspectionView causeInspection = replay.causeInspection;
        const ReplayCauseInspectorLayout inspectorLayout = BuildReplayCauseInspectorLayout( causeInspection,
                                                                                            replay.causeTree, screenW,
                                                                                            screenH,
                                                                                            causeInspection.drawerProgress );

        if ( strcmp( action.text, "causeTabSummary" ) == 0 && causeInspection.detailVisible )
        {
            InjectInteractionAutomationReplayControlClick( state, timers, replayIntent, action, frame,
                                                           inspectorLayout.tabs[0], "mouse press at Summary tab" );
            return;
        }

        if ( strcmp( action.text, "causeTabRawRecord" ) == 0 && causeInspection.detailVisible )
        {
            InjectInteractionAutomationReplayControlClick( state, timers, replayIntent, action, frame,
                                                           inspectorLayout.tabs[1], "mouse press at Raw Record tab" );
            return;
        }

        if ( strcmp( action.text, "causeTabIterations" ) == 0 && causeInspection.detailVisible )
        {
            InjectInteractionAutomationReplayControlClick( state, timers, replayIntent, action, frame,
                                                           inspectorLayout.tabs[2], "mouse press at Iterations tab" );
            return;
        }

        if ( strcmp( action.text, "causeCloseDrawer" ) == 0 && causeInspection.detailVisible )
        {
            InjectInteractionAutomationReplayControlClick( state, timers, replayIntent, action, frame,
                                                           inspectorLayout.drawerClose,
                                                           "mouse press at drawer close button" );
            return;
        }

        if ( strcmp( action.text, "causeCopyRawRecord" ) == 0 && causeInspection.detailVisible )
        {
            InjectInteractionAutomationReplayControlClick( state, timers, replayIntent, action, frame,
                                                           inspectorLayout.rawCopy,
                                                           "mouse press at copy raw record button" );
            return;
        }
    }

    if ( strcmp( action.text, "causeFilterAll" ) == 0 || strcmp( action.text, "causeFilterPrediction" ) == 0 ||
         strcmp( action.text, "causeFilterContacts" ) == 0 || strcmp( action.text, "causeFilterField" ) == 0 ||
         strcmp( action.text, "causeFilterFunnel" ) == 0 )
    {
        if ( replay.causeTree.hasWindowPlacement )
        {
            if ( strcmp( action.text, "causeFilterAll" ) == 0 )
            {
                InjectInteractionAutomationReplayControlClick( state, timers, replayIntent, action, frame,
                                                               ReplayCauseWindowFilterChipRect( replay.causeTree,
                                                                                                RunReplayCauseTreeFilter::
                                                                                                    All ),
                                                               "mouse press at Filter All chip" );
                return;
            }

            if ( strcmp( action.text, "causeFilterPrediction" ) == 0 )
            {
                InjectInteractionAutomationReplayControlClick( state, timers, replayIntent, action, frame,
                                                               ReplayCauseWindowFilterChipRect( replay.causeTree,
                                                                                                RunReplayCauseTreeFilter::
                                                                                                    Prediction ),
                                                               "mouse press at Filter Prediction chip" );
                return;
            }

            if ( strcmp( action.text, "causeFilterContacts" ) == 0 )
            {
                InjectInteractionAutomationReplayControlClick( state, timers, replayIntent, action, frame,
                                                               ReplayCauseWindowFilterChipRect( replay.causeTree,
                                                                                                RunReplayCauseTreeFilter::
                                                                                                    Contacts ),
                                                               "mouse press at Filter Contacts chip" );
                return;
            }

            if ( strcmp( action.text, "causeFilterField" ) == 0 )
            {
                InjectInteractionAutomationReplayControlClick( state, timers, replayIntent, action, frame,
                                                               ReplayCauseWindowFilterFieldRect( replay.causeTree ),
                                                               "mouse press at Filter Field" );
                return;
            }

            if ( strcmp( action.text, "causeFilterFunnel" ) == 0 )
            {
                InjectInteractionAutomationReplayControlClick( state, timers, replayIntent, action, frame,
                                                               ReplayCauseWindowFilterFunnelRect( replay.causeTree ),
                                                               "mouse press at Filter Funnel" );
                return;
            }
        }
    }

    AppendInteractionAutomationReplayControlFailure( state, frame, action,
                                                     "unsupported replay control in interaction script",
                                                     "unsupported replay control" );
}

void ApplyInteractionAutomationSolverTrackScrub( InteractionAutomationController& state, Window* window,
                                                 const SkullbonezCore::Core::EngineConfig& config,
                                                 const RuntimeFrameMetricsSnapshot& timers, ReplayFrameIntent& replayIntent,
                                                 const ReplayAutomationView& replay, RunInteractionAutomationAction& action,
                                                 int frame )
{
    const int screenW = window ? window->ClientWidth() : config.window.screenX;
    const int screenH = window ? window->ClientHeight() : config.window.screenY;
    const ReplayRecorderStats solverReplayStats = replay.solverStats;
    const bool solverToolsEnabled = solverReplayStats.enabled && solverReplayStats.sampleCount >= 2;

    if ( screenW > 0 && screenH > 0 && solverToolsEnabled )
    {
        // Why: replay branch tests need a historical solver selection, but the
        // selection still comes from the scrubber track hitbox and normal
        // drag/release handling.
        const SkullbonezCore::UI::UIRect track = ReplayScrubberTrackRect( screenW, screenH, RunReplayTrack::Solver );
        SkullbonezCore::UI::UIRect target = track;
        target.x = track.x + track.w * std::clamp( action.numberValue, 0.0f, 1.0f );
        target.w = 1.0f;
        InjectInteractionAutomationReplayControlClick( state, timers, replayIntent, action, frame, target,
                                                       "mouse press injected at solver replay track" );
    }
    else
    {
        AppendInteractionAutomationReplayControlFailure( state, frame, action, "replay solver scrub track unavailable",
                                                         "replay solver scrub track unavailable" );
    }
}

bool ParseSetCameraModeAction( const Json& entry, RunInteractionAutomationAction& outAction, std::string& outError )
{
    if ( !entry["setCameraMode"].is_string() )
    {
        outError = "setCameraMode must be a string";
        return false;
    }

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

bool ParseLoadShotListAction( const Json& entry, RunInteractionAutomationAction& outAction, std::string& outError )
{
    if ( !entry["loadShotList"].is_string() )
    {
        outError = "loadShotList must be a string";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::LoadShotList;
    CopyText( outAction.path, sizeof( outAction.path ), entry["loadShotList"].get<std::string>() );
    return true;
}

bool ParseDirectorPlayAction( const Json& entry, RunInteractionAutomationAction& outAction, std::string& outError )
{
    if ( !IsBoolValue( entry["directorPlay"] ) )
    {
        outError = "directorPlay must be a boolean value";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::DirectorPlay;
    outAction.boolValue = ReadBool( entry["directorPlay"] );
    CopyText( outAction.text, sizeof( outAction.text ), outAction.boolValue ? "Director" : "Inspect" );
    return true;
}

bool ParseDirectorAdvanceAction( const Json&, RunInteractionAutomationAction& outAction, std::string& )
{
    outAction.type = RunInteractionAutomationActionType::DirectorAdvance;
    return true;
}

bool ParseDirectorGrabAction( const Json&, RunInteractionAutomationAction& outAction, std::string& )
{
    outAction.type = RunInteractionAutomationActionType::DirectorGrab;
    return true;
}

bool ParseDirectorReleaseAction( const Json&, RunInteractionAutomationAction& outAction, std::string& )
{
    outAction.type = RunInteractionAutomationActionType::DirectorRelease;
    return true;
}

bool ParseSetPhaseStyleAction( const Json& entry, RunInteractionAutomationAction& outAction, std::string& outError )
{
    if ( !entry["setPhaseStyle"].is_string() )
    {
        outError = "setPhaseStyle must be a string";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::SetPhaseStyle;
    CopyText( outAction.path, sizeof( outAction.path ), entry["setPhaseStyle"].get<std::string>() );
    return true;
}

bool ParseSetCameraPoseAction( const Json& entry, RunInteractionAutomationAction& outAction, std::string& outError )
{
    outAction.type = RunInteractionAutomationActionType::SetCameraPose;
    return ReadAutomationCameraPose( entry["setCameraPose"], outAction.cameraPose, outError );
}

bool ParseClickObjectAction( const Json& entry, RunInteractionAutomationAction& outAction, std::string& outError )
{
    if ( !entry["clickObject"].is_string() || ( entry.contains( "button" ) && !entry["button"].is_string() ) ||
         ( entry.contains( "holdFrames" ) && !entry["holdFrames"].is_number_integer() ) )
    {
        outError = "clickObject requires a string target, optional string button, and optional integer holdFrames";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::ClickObject;
    CopyText( outAction.text, sizeof( outAction.text ), entry["clickObject"].get<std::string>() );

    if ( entry.contains( "button" ) )
    {
        const std::string button = entry["button"].get<std::string>();
        outAction.button = button == "right" ? RunInteractionAutomationButton::Right : RunInteractionAutomationButton::Left;
    }

    if ( entry.contains( "holdFrames" ) )
    {
        outAction.holdFrames = (std::max)( 1, entry["holdFrames"].get<int>() );
    }

    return true;
}

bool ParseClickPointAction( const Json& entry, RunInteractionAutomationAction& outAction, std::string& outError )
{
    const Json& point = entry["clickPoint"];

    if ( !point.is_array() || point.size() != 2 || !point[0].is_number_integer() || !point[1].is_number_integer() ||
         ( entry.contains( "button" ) && !entry["button"].is_string() ) ||
         ( entry.contains( "holdFrames" ) && !entry["holdFrames"].is_number_integer() ) )
    {
        outError = "clickPoint must be a 2-integer array";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::ClickPoint;
    outAction.mouse = { point[0].get<long>(), point[1].get<long>() };
    outAction.hasMouse = true;

    if ( entry.contains( "normalizedPoint" ) && entry["normalizedPoint"].is_array() &&
         entry["normalizedPoint"].size() == 2 && entry["normalizedPoint"][0].is_number() &&
         entry["normalizedPoint"][1].is_number() )
    {
        outAction.vectorValue.x = entry["normalizedPoint"][0].get<float>();
        outAction.vectorValue.y = entry["normalizedPoint"][1].get<float>();
        outAction.boolValue = true;
    }

    if ( entry.contains( "button" ) )
    {
        const std::string button = entry["button"].get<std::string>();
        outAction.button = button == "right" ? RunInteractionAutomationButton::Right : RunInteractionAutomationButton::Left;
    }

    if ( entry.contains( "holdFrames" ) )
    {
        outAction.holdFrames = (std::max)( 1, entry["holdFrames"].get<int>() );
    }

    return true;
}

bool ParseScrollPointAction( const Json& entry, RunInteractionAutomationAction& outAction, std::string& outError )
{
    const Json& scroll = entry["scrollPoint"];

    if ( !scroll.is_array() || scroll.size() != 3 || !scroll[0].is_number_integer() || !scroll[1].is_number_integer() ||
         !scroll[2].is_number_integer() || scroll[2].get<int>() == 0 )
    {
        outError = "scrollPoint must be an [x, y, non-zero wheel delta] integer array";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::ScrollPoint;
    outAction.mouse = { scroll[0].get<long>(), scroll[1].get<long>() };
    outAction.integerValue = scroll[2].get<int>();
    outAction.hasMouse = true;
    return true;
}

bool ParseSelectReplayCauseRowAction( const Json& entry, RunInteractionAutomationAction& outAction, std::string& outError )
{
    if ( !entry["selectReplayCauseRow"].is_number_integer() || entry["selectReplayCauseRow"].get<int>() < 0 )
    {
        outError = "selectReplayCauseRow must be a non-negative row index";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::SelectReplayCauseRow;
    outAction.integerValue = entry["selectReplayCauseRow"].get<int>();
    return true;
}

bool ParseLoseFocusAction( const Json& entry, RunInteractionAutomationAction& outAction, std::string& outError )
{
    if ( !entry["loseFocus"].is_number_integer() )
    {
        outError = "loseFocus must be an integer frame count";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::LoseFocus;
    outAction.holdFrames = (std::max)( 1, entry["loseFocus"].get<int>() );
    return true;
}

bool ParseMoveMouseAction( const Json& entry, RunInteractionAutomationAction& outAction, std::string& outError )
{
    const Json& point = entry["moveMouse"];

    if ( !point.is_array() || point.size() != 2 || !point[0].is_number_integer() || !point[1].is_number_integer() )
    {
        outError = "moveMouse must be a 2-integer array";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::MoveMouse;
    outAction.mouse = { point[0].get<long>(), point[1].get<long>() };
    outAction.hasMouse = true;

    if ( entry.contains( "normalizedPoint" ) && entry["normalizedPoint"].is_array() &&
         entry["normalizedPoint"].size() == 2 && entry["normalizedPoint"][0].is_number() &&
         entry["normalizedPoint"][1].is_number() )
    {
        outAction.vectorValue.x = entry["normalizedPoint"][0].get<float>();
        outAction.vectorValue.y = entry["normalizedPoint"][1].get<float>();
        outAction.boolValue = true;
    }

    return true;
}

bool ParseClickReplayControlAction( const Json& entry, RunInteractionAutomationAction& outAction, std::string& outError )
{
    if ( !entry["clickReplayControl"].is_string() )
    {
        outError = "clickReplayControl must be a string";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::ClickReplayControl;
    CopyText( outAction.text, sizeof( outAction.text ), entry["clickReplayControl"].get<std::string>() );
    return true;
}

bool ParseScrubReplaySolverTrackAction( const Json& entry, RunInteractionAutomationAction& outAction, std::string& outError )
{
    if ( !entry["scrubReplaySolverTrack"].is_number() )
    {
        outError = "scrubReplaySolverTrack must be a number";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::ScrubReplaySolverTrack;
    outAction.numberValue = std::clamp( entry["scrubReplaySolverTrack"].get<float>(), 0.0f, 1.0f );
    CopyText( outAction.text, sizeof( outAction.text ), "solver" );
    return true;
}

bool ParseScrubEditorReplayTrackAction( const Json& entry, RunInteractionAutomationAction& outAction, std::string& outError )
{
    if ( !entry["scrubEditorReplayTrack"].is_number() )
    {
        outError = "scrubEditorReplayTrack must be a normalized number";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::ScrubEditorReplayTrack;
    outAction.numberValue = std::clamp( entry["scrubEditorReplayTrack"].get<float>(), 0.0f, 1.0f );
    return true;
}

bool ParseSetContinuousForecastCommandAction( const Json& entry, RunInteractionAutomationAction& outAction,
                                              std::string& outError )
{
    if ( !entry["setContinuousForecastCommand"].is_string() )
    {
        outError = "setContinuousForecastCommand must be toggle, reset, or exit";
        return false;
    }

    const std::string command = entry["setContinuousForecastCommand"].get<std::string>();

    if ( command != "toggle" && command != "reset" && command != "exit" )
    {
        outError = "setContinuousForecastCommand must be toggle, reset, or exit";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::SetContinuousForecastCommand;
    CopyText( outAction.text, sizeof( outAction.text ), command );
    return true;
}

bool ParseSetReplayPredictionEnabledAction( const Json& entry, RunInteractionAutomationAction& outAction,
                                            std::string& outError )
{
    if ( !IsBoolValue( entry["setReplayPredictionEnabled"] ) )
    {
        outError = "setReplayPredictionEnabled must be a boolean value";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::SetReplayPredictionEnabled;
    outAction.boolValue = ReadBool( entry["setReplayPredictionEnabled"] );
    return true;
}

bool ParseSetReplayPredictionHorizonSecondsAction( const Json& entry, RunInteractionAutomationAction& outAction,
                                                   std::string& outError )
{
    if ( !entry["setReplayPredictionHorizonSeconds"].is_number() )
    {
        outError = "setReplayPredictionHorizonSeconds must be a number";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::SetReplayPredictionHorizonSeconds;
    outAction.numberValue = entry["setReplayPredictionHorizonSeconds"].get<float>();
    return true;
}

bool ParseBeginReplayVisualFidelityCaptureAction( const Json& entry, RunInteractionAutomationAction& outAction,
                                                  std::string& outError )
{
    if ( !IsBoolValue( entry["beginReplayVisualFidelityCapture"] ) ||
         !ReadBool( entry["beginReplayVisualFidelityCapture"] ) )
    {
        outError = "beginReplayVisualFidelityCapture must be true";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::BeginReplayVisualFidelityCapture;
    outAction.boolValue = true;
    return true;
}

bool ParseSetReplayPathTargetAction( const Json& entry, RunInteractionAutomationAction& outAction, std::string& outError )
{
    if ( !entry["setReplayPathTarget"].is_string() )
    {
        outError = "setReplayPathTarget must be a string";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::SetReplayPathTarget;
    CopyText( outAction.text, sizeof( outAction.text ), entry["setReplayPathTarget"].get<std::string>() );
    return true;
}

bool ParseSetReplayInterceptTargetAction( const Json& entry, RunInteractionAutomationAction& outAction,
                                          std::string& outError )
{
    if ( !entry["setReplayInterceptTarget"].is_string() )
    {
        outError = "setReplayInterceptTarget must be a string";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::SetReplayInterceptTarget;
    CopyText( outAction.text, sizeof( outAction.text ), entry["setReplayInterceptTarget"].get<std::string>() );
    return true;
}

bool ParseSetReplayTripPlannerCommandAction( const Json& entry, RunInteractionAutomationAction& outAction,
                                             std::string& outError )
{
    if ( !entry["setReplayTripPlannerCommand"].is_string() )
    {
        outError = "setReplayTripPlannerCommand must be a string";
        return false;
    }

    const std::string command = entry["setReplayTripPlannerCommand"].get<std::string>();
    outAction.type = RunInteractionAutomationActionType::SetReplayTripPlannerCommand;
    outAction.tripPlannerCommand = command == "toggle"     ? ReplayTripPlannerCommandKind::TogglePanel
                                   : command == "decrease" ? ReplayTripPlannerCommandKind::DecreaseTimeOfFlight
                                   : command == "increase" ? ReplayTripPlannerCommandKind::IncreaseTimeOfFlight
                                   : command == "tof"      ? ReplayTripPlannerCommandKind::SetTimeOfFlight
                                   : command == "plan"     ? ReplayTripPlannerCommandKind::Plan
                                   : command == "commit"   ? ReplayTripPlannerCommandKind::Commit
                                   : command == "cancel"   ? ReplayTripPlannerCommandKind::Cancel
                                                           : ReplayTripPlannerCommandKind::None;

    if ( outAction.tripPlannerCommand == ReplayTripPlannerCommandKind::None )
    {
        outError = "unknown setReplayTripPlannerCommand value: " + command;
        return false;
    }

    if ( outAction.tripPlannerCommand == ReplayTripPlannerCommandKind::SetTimeOfFlight )
    {
        if ( !entry.contains( "timeOfFlightSeconds" ) || !entry["timeOfFlightSeconds"].is_number() )
        {
            outError = "trip planner tof command requires numeric timeOfFlightSeconds";
            return false;
        }

        outAction.numberValue = entry["timeOfFlightSeconds"].get<float>();
    }

    CopyText( outAction.text, sizeof( outAction.text ), command );
    return true;
}

bool ParseNudgeReplayPathTargetVelocityAction( const Json& entry, RunInteractionAutomationAction& outAction,
                                               std::string& outError )
{
    outAction.type = RunInteractionAutomationActionType::NudgeReplayPathTargetVelocity;

    if ( !ReadAutomationVec3( entry["nudgeReplayPathTargetVelocity"], outAction.vectorValue ) )
    {
        outError = "nudgeReplayPathTargetVelocity must be a 3-number array";
        return false;
    }

    CopyText( outAction.text, sizeof( outAction.text ), "path-target" );
    return true;
}

bool ParseShowReplayScrubberAction( const Json& entry, RunInteractionAutomationAction& outAction, std::string& outError )
{
    if ( !IsBoolValue( entry["showReplayScrubber"] ) )
    {
        outError = "showReplayScrubber must be a boolean value";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::ShowReplayScrubber;
    outAction.boolValue = ReadBool( entry["showReplayScrubber"] );
    return true;
}

bool ParsePressKeyAction( const Json& entry, RunInteractionAutomationAction& outAction, std::string& outError )
{
    if ( !entry["pressKey"].is_string() || ( entry.contains( "control" ) && !entry["control"].is_boolean() ) )
    {
        outError = "pressKey requires a string key and optional boolean control";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::PressKey;
    const std::string keyName = entry["pressKey"].get<std::string>();

    if ( !TryParseVirtualKey( keyName, outAction.keyVirtualKey ) )
    {
        outError = "unknown pressKey value: " + keyName;
        return false;
    }

    CopyText( outAction.text, sizeof( outAction.text ), keyName );
    outAction.boolValue = entry.value( "control", false );
    outAction.holdFrames = entry.value( "holdFrames", 1 );
    return true;
}

bool ParseCaptureEditorSelectionStateAction( const Json& entry, RunInteractionAutomationAction& outAction,
                                             std::string& outError )
{
    if ( !entry["captureEditorSelectionState"].is_number_integer() )
    {
        outError = "captureEditorSelectionState must be an integer slot";
        return false;
    }

    const int slot = entry["captureEditorSelectionState"].get<int>();

    if ( slot < 0 || slot >= 2 )
    {
        outError = "captureEditorSelectionState slot must be 0 or 1";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::CaptureEditorSelectionState;
    outAction.numberValue = static_cast<float>( slot );
    return true;
}

bool ParseLoadSceneAction( const Json& entry, RunInteractionAutomationAction& outAction, std::string& outError )
{
    if ( !entry["loadScene"].is_string() )
    {
        outError = "loadScene must be a scene-browser path";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::LoadScene;
    CopyText( outAction.path, sizeof( outAction.path ), entry["loadScene"].get<std::string>() );
    return true;
}

bool ParseSetDevelopmentUiSurfaceAction( const Json& entry, RunInteractionAutomationAction& outAction,
                                         std::string& outError )
{
    if ( !entry["setDevelopmentUiSurface"].is_string() )
    {
        outError = "setDevelopmentUiSurface must be game or imgui";
        return false;
    }

    const std::string surface = entry["setDevelopmentUiSurface"].get<std::string>();

    if ( surface != "game" && surface != "imgui" )
    {
        outError = "setDevelopmentUiSurface must be game or imgui";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::SetDevelopmentUiSurface;
    CopyText( outAction.text, sizeof( outAction.text ), surface );
    return true;
}

bool ParseSetImGuiPanelVisibleAction( const Json& entry, RunInteractionAutomationAction& outAction, std::string& outError )
{
    const Json& panel = entry["setImGuiPanelVisible"];

    if ( !panel.is_object() || !panel.contains( "panel" ) || !panel["panel"].is_string() || !panel.contains( "visible" ) ||
         !IsBoolValue( panel["visible"] ) )
    {
        outError = "setImGuiPanelVisible requires string panel and boolean visible";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::SetImGuiPanelVisible;
    CopyText( outAction.text, sizeof( outAction.text ), panel["panel"].get<std::string>() );
    outAction.boolValue = ReadBool( panel["visible"] );
    return true;
}

bool ParseResetImGuiLayoutAction( const Json& entry, RunInteractionAutomationAction& outAction, std::string& outError )
{
    if ( !IsBoolValue( entry["resetImGuiLayout"] ) || !ReadBool( entry["resetImGuiLayout"] ) )
    {
        outError = "resetImGuiLayout must be true";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::ResetImGuiLayout;
    return true;
}

bool ParseFocusImGuiPanelAction( const Json& entry, RunInteractionAutomationAction& outAction, std::string& outError )
{
    if ( !entry["focusImGuiPanel"].is_string() )
    {
        outError = "focusImGuiPanel must be a string panel name";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::FocusImGuiPanel;
    CopyText( outAction.text, sizeof( outAction.text ), entry["focusImGuiPanel"].get<std::string>() );
    return true;
}

bool ParseSetImGuiDpiScaleAction( const Json& entry, RunInteractionAutomationAction& outAction, std::string& outError )
{
    if ( !entry["setImGuiDpiScale"].is_number() )
    {
        outError = "setImGuiDpiScale must be a number";
        return false;
    }

    const float dpiScale = entry["setImGuiDpiScale"].get<float>();

    if ( !std::isfinite( dpiScale ) || dpiScale < 0.75f || dpiScale > 4.0f )
    {
        outError = "setImGuiDpiScale must be within 0.75..4.0";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::SetImGuiDpiScale;
    outAction.numberValue = dpiScale;
    return true;
}

bool ParseResizeWindowAction( const Json& entry, RunInteractionAutomationAction& outAction, std::string& outError )
{
    const Json& size = entry["resizeWindow"];

    if ( !size.is_array() || size.size() != 2 || !size[0].is_number_integer() || !size[1].is_number_integer() )
    {
        outError = "resizeWindow must be a 2-integer client-size array";
        return false;
    }

    const int width = size[0].get<int>();
    const int height = size[1].get<int>();

    if ( width < 1024 || width > 7680 || height < 640 || height > 4320 )
    {
        outError = "resizeWindow client size must be within 1024x640..7680x4320";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::ResizeWindow;
    outAction.mouse = { width, height };

    return true;
}

bool ParseScreenshotAction( const Json& entry, RunInteractionAutomationAction& outAction, std::string& outError )
{
    if ( !entry["screenshot"].is_string() )
    {
        outError = "screenshot must be a string path";
        return false;
    }

    outAction.type = RunInteractionAutomationActionType::Screenshot;
    CopyText( outAction.path, sizeof( outAction.path ), entry["screenshot"].get<std::string>() );
    return true;
}

enum class AssertionParseStatus
{
    NoMatch,
    Success,
    Failure,
};

AssertionParseStatus ParseBasicAssertion( const std::string& name, const Json& expected,
                                          RunInteractionAutomationAction& outAction, std::string& outError )
{
    if ( name == "selectedObject" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::SelectedObject;
        CopyText( outAction.text, sizeof( outAction.text ), expected.get<std::string>() );

        return AssertionParseStatus::Success;
    }

    if ( name == "owner" )
    {
        WorldInteractionOwner owner = WorldInteractionOwner::None;
        const std::string ownerName = expected.get<std::string>();

        if ( !TryParseOwner( ownerName, owner ) )
        {
            outError = "unknown owner assertion value: " + ownerName;
            return AssertionParseStatus::Failure;
        }

        outAction.assertKind = RunInteractionAutomationAssertKind::Owner;
        CopyText( outAction.text, sizeof( outAction.text ), ownerName );

        return AssertionParseStatus::Success;
    }

    if ( name == "cameraMode" )
    {
        RunCameraMode mode = RunCameraMode::Inspect;
        const std::string modeName = expected.get<std::string>();

        if ( !TryParseCameraMode( modeName, mode ) )
        {
            outError = "unknown cameraMode assertion value: " + modeName;
            return AssertionParseStatus::Failure;
        }

        outAction.assertKind = RunInteractionAutomationAssertKind::CameraMode;
        outAction.cameraMode = mode;
        CopyText( outAction.text, sizeof( outAction.text ), modeName );

        return AssertionParseStatus::Success;
    }

    if ( name == "directorGrabbed" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::DirectorGrabbed;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "directorPhaseIndex" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::DirectorPhaseIndex;
        outAction.numberValue = static_cast<float>( expected.get<int>() );

        return AssertionParseStatus::Success;
    }

    if ( name == "directorPhaseName" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::DirectorPhaseName;
        CopyText( outAction.text, sizeof( outAction.text ), expected.get<std::string>() );

        return AssertionParseStatus::Success;
    }

    if ( name == "directorPhaseStylePath" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::DirectorPhaseStylePath;
        CopyText( outAction.path, sizeof( outAction.path ), expected.get<std::string>() );

        return AssertionParseStatus::Success;
    }

    return AssertionParseStatus::NoMatch;
}

AssertionParseStatus ParsePredictionCauseAssertion( const std::string& name, const Json& expected,
                                                    RunInteractionAutomationAction& outAction )
{
    struct Entry
    {
        const char* name;
        RunInteractionAutomationAssertKind kind;
    };
    static constexpr std::array ENTRIES = {
        Entry { "predictionCauseManifoldRowsMin", RunInteractionAutomationAssertKind::PredictionCauseManifoldRowsMin },
        Entry { "predictionCauseManifoldRowsMax", RunInteractionAutomationAssertKind::PredictionCauseManifoldRowsMax },
        Entry { "predictionCauseSolverRowsMin", RunInteractionAutomationAssertKind::PredictionCauseSolverRowsMin },
        Entry { "predictionCauseSolverRowsMax", RunInteractionAutomationAssertKind::PredictionCauseSolverRowsMax },
        Entry { "predictionCauseSyntheticRowsMin", RunInteractionAutomationAssertKind::PredictionCauseSyntheticRowsMin },
        Entry { "predictionCauseSyntheticRowsMax", RunInteractionAutomationAssertKind::PredictionCauseSyntheticRowsMax },
    };

    const auto entry = std::find_if( ENTRIES.begin(), ENTRIES.end(),
                                     [&]( const Entry& candidate ) { return name == candidate.name; } );

    if ( entry == ENTRIES.end() )
    {
        return AssertionParseStatus::NoMatch;
    }

    outAction.assertKind = entry->kind;
    outAction.numberValue = expected.get<float>();
    return AssertionParseStatus::Success;
}

AssertionParseStatus ParseReplayAssertion( const std::string& name, const Json& expected,
                                           RunInteractionAutomationAction& outAction, std::string& )
{
    if ( name == "replayPredictionEnabled" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ReplayPredictionEnabled;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "replayPathTarget" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ReplayPathTarget;
        CopyText( outAction.text, sizeof( outAction.text ), expected.get<std::string>() );

        return AssertionParseStatus::Success;
    }

    if ( name == "replayInterceptContact" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ReplayInterceptContact;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "replayInterceptMissMax" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ReplayInterceptMissMax;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "replayInterceptEtaMin" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ReplayInterceptEtaMin;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "replayInterceptEtaMax" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ReplayInterceptEtaMax;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "replayTripPlannerState" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ReplayTripPlannerState;
        CopyText( outAction.text, sizeof( outAction.text ), expected.get<std::string>() );

        return AssertionParseStatus::Success;
    }

    if ( name == "replayTripPlannerIterationMax" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ReplayTripPlannerIterationMax;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "replayTripPlannerMissMax" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ReplayTripPlannerMissMax;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "replayTripPlannerMissesImprove" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ReplayTripPlannerMissesImprove;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "replayPorkchopComplete" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ReplayPorkchopComplete;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "replayPorkchopMinimumDeltaVMax" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ReplayPorkchopMinimumDeltaVMax;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "replayPorkchopMinimumDepartureDelayMax" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ReplayPorkchopMinimumDepartureDelayMax;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "replayPorkchopMinimumTimeOfFlightMin" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ReplayPorkchopMinimumTimeOfFlightMin;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "replayPorkchopMinimumTimeOfFlightMax" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ReplayPorkchopMinimumTimeOfFlightMax;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "replayPorkchopRefreshMillisecondsMax" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ReplayPorkchopRefreshMillisecondsMax;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "replayPorkchopMaximumFrameMillisecondsMax" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ReplayPorkchopMaximumFrameMillisecondsMax;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "replayPorkchopSweepAgeSecondsMax" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ReplayPorkchopSweepAgeSecondsMax;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "replayPorkchopSelected" )
    {


        outAction.assertKind = RunInteractionAutomationAssertKind::ReplayPorkchopSelected;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "replayTripPlannerTimeOfFlightMin" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ReplayTripPlannerTimeOfFlightMin;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "replayTripPlannerTimeOfFlightMax" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ReplayTripPlannerTimeOfFlightMax;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "replayPastTrajectoryFullRebuildCountMax" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ReplayPastTrajectoryFullRebuildCountMax;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "replayPastTrajectoryIncrementalTrimCountMin" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ReplayPastTrajectoryIncrementalTrimCountMin;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "replayPastTrajectoryPublishedPointCountMin" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ReplayPastTrajectoryPublishedPointCountMin;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "predictionPathVisible" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::PredictionPathVisible;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "predictionVelocityPreviewActive" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::PredictionVelocityPreviewActive;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "predictionVelocityPreviewAwaitingReplacement" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::PredictionVelocityPreviewAwaitingReplacement;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "predictionVelocityPreviewDeltaMin" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::PredictionVelocityPreviewDeltaMin;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "predictionPresentedGenerationMin" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::PredictionPresentedGenerationMin;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "predictionPresentedRootVelocityDeltaMin" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::PredictionPresentedRootVelocityDeltaMin;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "predictionFullHorizonComplete" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::PredictionFullHorizonComplete;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "predictionBuildMode" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::PredictionBuildMode;
        CopyText( outAction.text, sizeof( outAction.text ), expected.get<std::string>() );

        return AssertionParseStatus::Success;
    }

    if ( name == "predictionSupersededRestartCountMin" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::PredictionSupersededRestartCountMin;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "predictionSupersededRestartCountMax" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::PredictionSupersededRestartCountMax;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "predictionBaselineVisible" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::PredictionBaselineVisible;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "predictionDivergenceMin" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::PredictionDivergenceMin;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "replaySolverTrackAtPresent" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ReplaySolverTrackAtPresent;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "predictionScrubFrameActive" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::PredictionScrubFrameActive;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "predictionTargetDisplacementMin" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::PredictionTargetDisplacementMin;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "predictionTargetLastNear" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::PredictionTargetLastNear;
        outAction.vectorValue = Vector3( expected["position"][0].get<float>(), expected["position"][1].get<float>(),
                                         expected["position"][2].get<float>() );

        outAction.numberValue = expected["tolerance"].get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "liveSolverHashStableAcrossPrediction" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::LiveSolverHashStableAcrossPrediction;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "predictionEvidenceConsumerBalanced" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::PredictionEvidenceConsumerBalanced;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "predictionEvidencePipelineRowsMin" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::PredictionEvidencePipelineRowsMin;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "predictionEvidenceCurrentCapacityMax" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::PredictionEvidenceCurrentCapacityMax;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "predictionDetailMode" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::PredictionDetailMode;
        CopyText( outAction.text, sizeof( outAction.text ), expected.get<std::string>() );

        return AssertionParseStatus::Success;
    }

    if ( name == "predictionCauseDetailVisible" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::PredictionCauseDetailVisible;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "predictionCauseWindowAvailable" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::PredictionCauseWindowAvailable;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "predictionEvidenceCapacityReleased" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::PredictionEvidenceCapacityReleased;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "predictionEvidenceMemoryReconciled" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::PredictionEvidenceMemoryReconciled;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( ParsePredictionCauseAssertion( name, expected, outAction ) == AssertionParseStatus::Success )
    {
        return AssertionParseStatus::Success;
    }

    if ( name == "predictionTrajectoryFingerprintReady" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::PredictionTrajectoryFingerprintReady;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "predictionAppearanceInvalidationCountMin" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::PredictionAppearanceInvalidationCountMin;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    return AssertionParseStatus::NoMatch;
}

AssertionParseStatus ParseContinuousForecastAssertion( const std::string& name, const Json& expected,
                                                       RunInteractionAutomationAction& outAction, std::string& /*outError*/ )
{
    if ( name == "continuousForecastActive" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ContinuousForecastActive;
        outAction.boolValue = ReadBool( expected );
        return AssertionParseStatus::Success;
    }

    if ( name == "continuousForecastPreWrap" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ContinuousForecastPreWrap;
        outAction.boolValue = ReadBool( expected );
        return AssertionParseStatus::Success;
    }

    if ( name == "continuousForecastWindowWrapped" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ContinuousForecastWindowWrapped;
        outAction.boolValue = ReadBool( expected );
        return AssertionParseStatus::Success;
    }

    if ( name == "continuousForecastPresentationCoherent" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ContinuousForecastPresentationCoherent;
        outAction.boolValue = ReadBool( expected );
        return AssertionParseStatus::Success;
    }

    if ( name == "continuousForecastAbsoluteTickMin" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ContinuousForecastAbsoluteTickMin;
        outAction.numberValue = expected.get<float>();
        return AssertionParseStatus::Success;
    }

    if ( name == "continuousForecastOldestTickMin" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ContinuousForecastOldestTickMin;
        outAction.numberValue = expected.get<float>();
        return AssertionParseStatus::Success;
    }

    if ( name == "continuousForecastRibbonSegmentsMin" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ContinuousForecastRibbonSegmentsMin;
        outAction.numberValue = expected.get<float>();
        return AssertionParseStatus::Success;
    }

    if ( name == "continuousForecastHeadMarkerCount" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ContinuousForecastHeadMarkerCount;
        outAction.numberValue = expected.get<float>();
        return AssertionParseStatus::Success;
    }

    return AssertionParseStatus::NoMatch;
}

AssertionParseStatus ParseRuntimeAssertion( const std::string& name, const Json& expected,
                                            RunInteractionAutomationAction& outAction, std::string& outError )
{
    if ( name == "shadowPassExecuted" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ShadowPassExecuted;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "terrainShadowValid" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::TerrainShadowValid;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "objectShadowValid" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ObjectShadowValid;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "reflectionPassExecuted" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ReflectionPassExecuted;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "gizmoVisible" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::GizmoVisible;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "mousePickupActive" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::MousePickupActive;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "pointerCapture" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::PointerCapture;
        CopyText( outAction.text, sizeof( outAction.text ), expected.get<std::string>() );

        return AssertionParseStatus::Success;
    }

    if ( name == "nativeCaptureRequested" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::NativeCaptureRequested;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "cursorVisibleRequested" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::CursorVisibleRequested;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "uiBlocksMouse" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::UiBlocksMouse;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "launcherRayActive" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::LauncherRayActive;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "replayActiveTrack" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ReplayActiveTrack;
        CopyText( outAction.text, sizeof( outAction.text ), expected.get<std::string>() );

        return AssertionParseStatus::Success;
    }

    if ( name == "replayHistoricalSamplePaused" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ReplayHistoricalSamplePaused;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "memoryOverlayEnabled" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::MemoryOverlayEnabled;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "editorUndoDepth" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::EditorUndoDepth;
        outAction.numberValue = static_cast<float>( expected.get<int>() );

        return AssertionParseStatus::Success;
    }

    if ( name == "editorRedoDepth" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::EditorRedoDepth;
        outAction.numberValue = static_cast<float>( expected.get<int>() );

        return AssertionParseStatus::Success;
    }

    if ( name == "editorSelectionExists" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::EditorSelectionExists;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "editorSelectionHasTerrain" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::EditorSelectionHasTerrain;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "editorSelectionMatchesCapture" )
    {
        const int slot = expected.get<int>();

        if ( slot < 0 || slot >= 2 )
        {
            outError = "editorSelectionMatchesCapture slot must be 0 or 1";
            return AssertionParseStatus::Failure;
        }

        outAction.assertKind = RunInteractionAutomationAssertKind::EditorSelectionMatchesCapture;
        outAction.numberValue = static_cast<float>( slot );

        return AssertionParseStatus::Success;
    }

    if ( name == "developmentUiSurface" )
    {
        const std::string surface = expected.get<std::string>();

        if ( surface != "game" && surface != "imgui" )
        {
            outError = "developmentUiSurface must be game or imgui";
            return AssertionParseStatus::Failure;
        }

        outAction.assertKind = RunInteractionAutomationAssertKind::DevelopmentUiSurface;
        CopyText( outAction.text, sizeof( outAction.text ), surface );

        return AssertionParseStatus::Success;
    }

    if ( name == "imguiVisible" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ImGuiVisible;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "gameUiReplayPresentationActive" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::GameUiReplayPresentationActive;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    if ( name == "imguiPanelMask" )
    {
        const int mask = expected.get<int>();

        if ( mask < 0 || mask > 4095 )
        {
            outError = "imguiPanelMask must be within 0..4095";
            return AssertionParseStatus::Failure;
        }

        outAction.assertKind = RunInteractionAutomationAssertKind::ImGuiPanelMask;
        outAction.numberValue = static_cast<float>( mask );

        return AssertionParseStatus::Success;
    }

    if ( name == "imguiLayoutResetCountMin" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ImGuiLayoutResetCountMin;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "imguiFocusCountMin" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ImGuiFocusCountMin;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "imguiDpiScale" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ImGuiDpiScale;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "imguiDescriptorHighWaterMax" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ImGuiDescriptorHighWaterMax;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "imguiViewportRecreationsMin" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ImGuiViewportRecreationsMin;
        outAction.numberValue = expected.get<float>();

        return AssertionParseStatus::Success;
    }

    if ( name == "imguiPreferencesRecovered" )
    {
        outAction.assertKind = RunInteractionAutomationAssertKind::ImGuiPreferencesRecovered;
        outAction.boolValue = ReadBool( expected );

        return AssertionParseStatus::Success;
    }

    return AssertionParseStatus::NoMatch;
}

using AssertionParser = AssertionParseStatus ( * )( const std::string&, const Json&, RunInteractionAutomationAction&,
                                                    std::string& );

// Invariant: each assertion schema has one parser domain, and the table
// preserves the former basic/replay/runtime matching order before rejection.
constexpr AssertionParser ASSERTION_PARSERS[] = {
    ParseBasicAssertion,
    ParseReplayAssertion,
    ParseContinuousForecastAssertion,
    ParseRuntimeAssertion,
};

bool ParseAssertAction( const Json& entry, RunInteractionAutomationAction& outAction, std::string& outError )
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
    const Json& expected = member.value();

    // Invariant: JSON_NOEXCEPTION turns a mismatched get<T>() into an
    // abort, so the assertion vocabulary is classified before dispatch.
    const bool expectsString = name == "selectedObject" || name == "owner" || name == "cameraMode" ||
                               name == "directorPhaseName" || name == "directorPhaseStylePath" ||
                               name == "replayPathTarget" || name == "replayTripPlannerState" ||
                               name == "predictionBuildMode" || name == "pointerCapture" || name == "replayActiveTrack" ||
                               name == "developmentUiSurface";

    const bool expectsInteger = name == "directorPhaseIndex" || name == "editorUndoDepth" || name == "editorRedoDepth" ||
                                name == "editorSelectionMatchesCapture" || name == "imguiPanelMask";

    const bool expectsNumber = name == "replayPastTrajectoryFullRebuildCountMax" ||
                               name == "replayPastTrajectoryIncrementalTrimCountMin" ||
                               name == "replayPastTrajectoryPublishedPointCountMin" ||
                               name == "replayTripPlannerIterationMax" || name == "replayTripPlannerMissMax" ||
                               name == "replayInterceptMissMax" || name == "replayInterceptEtaMin" ||
                               name == "replayInterceptEtaMax" || name == "replayPorkchopMinimumDeltaVMax" ||
                               name == "replayPorkchopMinimumDepartureDelayMax" ||
                               name == "replayPorkchopMinimumTimeOfFlightMin" ||
                               name == "replayPorkchopMinimumTimeOfFlightMax" ||
                               name == "replayPorkchopRefreshMillisecondsMax" ||
                               name == "replayTripPlannerTimeOfFlightMin" || name == "replayTripPlannerTimeOfFlightMax" ||
                               name == "predictionSupersededRestartCountMin" ||
                               name == "predictionSupersededRestartCountMax" ||
                               name == "predictionVelocityPreviewDeltaMin" || name == "predictionPresentedGenerationMin" ||
                               name == "predictionPresentedRootVelocityDeltaMin" || name == "predictionDivergenceMin" ||
                               name == "predictionTargetDisplacementMin" || name == "predictionEvidencePipelineRowsMin" ||
                               name == "predictionEvidenceCurrentCapacityMax" || name == "predictionCauseManifoldRowsMin" ||
                               name == "predictionCauseManifoldRowsMax" || name == "predictionCauseSolverRowsMin" ||
                               name == "predictionCauseSolverRowsMax" || name == "predictionCauseSyntheticRowsMin" ||
                               name == "predictionCauseSyntheticRowsMax" ||
                               name == "predictionAppearanceInvalidationCountMin" ||
                               name == "continuousForecastAbsoluteTickMin" || name == "continuousForecastOldestTickMin" ||
                               name == "continuousForecastRibbonSegmentsMin" ||
                               name == "continuousForecastHeadMarkerCount" || name == "imguiLayoutResetCountMin" ||
                               name == "imguiFocusCountMin" || name == "imguiDpiScale" ||
                               name == "imguiDescriptorHighWaterMax" || name == "imguiViewportRecreationsMin";

    const bool expectsBool = name == "directorGrabbed" || name == "replayPredictionEnabled" ||
                             name == "predictionPathVisible" || name == "predictionFullHorizonComplete" ||
                             name == "predictionVelocityPreviewActive" ||
                             name == "predictionVelocityPreviewAwaitingReplacement" || name == "predictionBaselineVisible" ||
                             name == "replayInterceptContact" || name == "replayTripPlannerMissesImprove" ||
                             name == "replayPorkchopComplete" || name == "replayPorkchopSelected" ||
                             name == "replaySolverTrackAtPresent" || name == "predictionScrubFrameActive" ||
                             name == "liveSolverHashStableAcrossPrediction" ||
                             name == "predictionEvidenceConsumerBalanced" ||
                             name == "predictionTrajectoryFingerprintReady" || name == "continuousForecastActive" ||
                             name == "continuousForecastPreWrap" || name == "continuousForecastWindowWrapped" ||
                             name == "continuousForecastPresentationCoherent" || name == "shadowPassExecuted" ||
                             name == "terrainShadowValid" || name == "objectShadowValid" ||
                             name == "reflectionPassExecuted" || name == "gizmoVisible" || name == "mousePickupActive" ||
                             name == "nativeCaptureRequested" || name == "cursorVisibleRequested" ||
                             name == "uiBlocksMouse" || name == "launcherRayActive" ||
                             name == "replayHistoricalSamplePaused" || name == "memoryOverlayEnabled" ||
                             name == "editorSelectionExists" || name == "editorSelectionHasTerrain" ||
                             name == "imguiVisible" || name == "gameUiReplayPresentationActive" ||
                             name == "imguiPreferencesRecovered";

    const bool expectsPositionTolerance = name == "predictionTargetLastNear";
    const bool positionToleranceValid = !expectsPositionTolerance ||
                                        ( expected.is_object() && expected.contains( "position" ) &&
                                          expected["position"].is_array() && expected["position"].size() == 3u &&
                                          expected["position"][0].is_number() && expected["position"][1].is_number() &&
                                          expected["position"][2].is_number() && expected.contains( "tolerance" ) &&
                                          expected["tolerance"].is_number() && expected["tolerance"].get<float>() > 0.0f );

    if ( ( expectsString && !expected.is_string() ) || ( expectsInteger && !expected.is_number_integer() ) ||
         ( expectsNumber && !expected.is_number() ) || ( expectsBool && !IsBoolValue( expected ) ) ||
         !positionToleranceValid )
    {
        outError = "assertion field has the wrong value type: " + name;
        return false;
    }

    for ( AssertionParser parser : ASSERTION_PARSERS )
    {
        const AssertionParseStatus status = parser( name, expected, outAction, outError );

        if ( status != AssertionParseStatus::NoMatch )
        {
            return status == AssertionParseStatus::Success;
        }
    }

    outError = "unknown assertion field: " + name;
    return false;
}

using InteractionActionParser = bool ( * )( const Json&, RunInteractionAutomationAction&, std::string& );

// Invariant: table order preserves the legacy first-key-wins contract when
// an invalid script object contains more than one recognized action field.
constexpr std::pair<const char*, InteractionActionParser> INTERACTION_ACTION_PARSERS[] = {
    { "setCameraMode", ParseSetCameraModeAction },
    { "loadShotList", ParseLoadShotListAction },
    { "directorPlay", ParseDirectorPlayAction },
    { "directorAdvance", ParseDirectorAdvanceAction },
    { "directorGrab", ParseDirectorGrabAction },
    { "directorRelease", ParseDirectorReleaseAction },
    { "setPhaseStyle", ParseSetPhaseStyleAction },
    { "setCameraPose", ParseSetCameraPoseAction },
    { "clickObject", ParseClickObjectAction },
    { "clickPoint", ParseClickPointAction },
    { "scrollPoint", ParseScrollPointAction },
    { "selectReplayCauseRow", ParseSelectReplayCauseRowAction },
    { "loseFocus", ParseLoseFocusAction },
    { "moveMouse", ParseMoveMouseAction },
    { "clickReplayControl", ParseClickReplayControlAction },
    { "scrubReplaySolverTrack", ParseScrubReplaySolverTrackAction },
    { "scrubEditorReplayTrack", ParseScrubEditorReplayTrackAction },
    { "setContinuousForecastCommand", ParseSetContinuousForecastCommandAction },
    { "setReplayPredictionEnabled", ParseSetReplayPredictionEnabledAction },
    { "setReplayPredictionHorizonSeconds", ParseSetReplayPredictionHorizonSecondsAction },
    { "beginReplayVisualFidelityCapture", ParseBeginReplayVisualFidelityCaptureAction },
    { "setReplayPathTarget", ParseSetReplayPathTargetAction },
    { "setReplayInterceptTarget", ParseSetReplayInterceptTargetAction },
    { "setReplayTripPlannerCommand", ParseSetReplayTripPlannerCommandAction },
    { "nudgeReplayPathTargetVelocity", ParseNudgeReplayPathTargetVelocityAction },
    { "showReplayScrubber", ParseShowReplayScrubberAction },
    { "pressKey", ParsePressKeyAction },
    { "captureEditorSelectionState", ParseCaptureEditorSelectionStateAction },
    { "loadScene", ParseLoadSceneAction },
    { "setDevelopmentUiSurface", ParseSetDevelopmentUiSurfaceAction },
    { "setImGuiPanelVisible", ParseSetImGuiPanelVisibleAction },
    { "resetImGuiLayout", ParseResetImGuiLayoutAction },
    { "focusImGuiPanel", ParseFocusImGuiPanelAction },
    { "setImGuiDpiScale", ParseSetImGuiDpiScaleAction },
    { "resizeWindow", ParseResizeWindowAction },
    { "screenshot", ParseScreenshotAction },
    { "assert", ParseAssertAction },
};

bool ParseAction( const Json& entry, RunInteractionAutomationAction& outAction, std::string& outError )
{
    if ( !entry.is_object() || !TryReadFrame( entry, outAction.frame ) )
    {
        outError = "each action must be an object with an integer frame";
        return false;
    }


    for ( const auto& [field, parser] : INTERACTION_ACTION_PARSERS )
    {
        if ( entry.contains( field ) )
        {
            return parser( entry, outAction, outError );
        }
    }

    outError = "unknown action shape";
    return false;
}

std::string BoolString( bool value )
{
    return value ? "true" : "false";
}

const char* TripPlannerStateName( ReplayTripPlannerState state )
{
    switch ( state )
    {
    case ReplayTripPlannerState::Idle:
        return "Idle";
    case ReplayTripPlannerState::Seeding:
        return "Seeding";
    case ReplayTripPlannerState::AwaitingPrediction:
        return "AwaitingPrediction";
    case ReplayTripPlannerState::Correcting:
        return "Correcting";
    case ReplayTripPlannerState::Converged:
        return "Converged";
    case ReplayTripPlannerState::Failed:
        return "Failed";
    }

    return "Unknown";
}

struct InteractionAutomationAssertionEvaluation
{
    std::string expected;
    std::string actual;
    bool passed = false;
};

template <typename InspectGizmoInteractionActive>
InteractionAutomationAssertionEvaluation EvaluateInteractionAutomationAssertion( EditorToolsOwner& editorTools, RuntimeTools& runtimeTools, const InteractionAutomationController& automation,
                                                                                 const ReplayAutomationView& replay, RuntimeInteractionController& interaction, const InputRouter& inputRouter,
                                                                                 CameraControlState& camera, const SceneWorld& world, SkullbonezCore::UI::InGameUI& ui,
                                                                                 const InteractionAutomationDevelopmentUiView& developmentUi, const ContinuousOrbitalForecastView& forecast,
                                                                                 const Rendering::RenderSceneSnapshot& renderSnapshot, const RunInteractionAutomationAction& action,
                                                                                 InspectGizmoInteractionActive inspectGizmoInteractionActive )
{
    // Concept: after-render assertions are read-only probes over owner state.
    // The context keeps that state explicit so the Run tick only schedules,
    // reports, and fails automation work instead of owning assertion policy.
    InteractionAutomationAssertionEvaluation evaluation;

    switch ( action.assertKind )
    {
    case RunInteractionAutomationAssertKind::SelectedObject:
    {
        evaluation.expected = action.text;
        const int selectedIndex = PeekSelectedEditorModelIndex( editorTools.Editor(), world.BodyStore() );

        if ( selectedIndex >= 0 && selectedIndex < world.SceneEntityCount() )
        {
            evaluation.actual = world.Entities().At( selectedIndex ).displayName;
        }

        evaluation.passed = evaluation.actual == evaluation.expected;
        break;
    }
    case RunInteractionAutomationAssertKind::Owner:
        evaluation.expected = action.text;
        evaluation.actual = InteractionAutomationReportWriter::OwnerName( static_cast<int>( interaction.Owner() ) );
        evaluation.passed = evaluation.actual == evaluation.expected;
        break;
    case RunInteractionAutomationAssertKind::CameraMode:
        evaluation.expected = InteractionAutomationReportWriter::CameraModeName( action.cameraMode );
        evaluation.actual = InteractionAutomationReportWriter::CameraModeName( camera.mode );
        evaluation.passed = camera.mode == action.cameraMode;
        break;
    case RunInteractionAutomationAssertKind::DirectorGrabbed:
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( camera.director.grabbed );
        evaluation.passed = camera.director.grabbed == action.boolValue;
        break;
    case RunInteractionAutomationAssertKind::DirectorPhaseIndex:
    {
        const int expectedPhase = static_cast<int>( action.numberValue );
        evaluation.expected = std::to_string( expectedPhase );
        evaluation.actual = std::to_string( camera.director.currentPhaseIndex );
        evaluation.passed = camera.director.currentPhaseIndex == expectedPhase;
        break;
    }
    case RunInteractionAutomationAssertKind::DirectorPhaseName:
    {
        const DemoPhase* phase = ActiveDirectorPhase( camera );
        evaluation.expected = action.text;
        evaluation.actual = phase ? phase->name : "";
        evaluation.passed = evaluation.actual == evaluation.expected;
        break;
    }
    case RunInteractionAutomationAssertKind::DirectorPhaseStylePath:
    {
        const DemoPhase* phase = ActiveDirectorPhase( camera );
        evaluation.expected = action.path;
        evaluation.actual = phase ? phase->stylePath : "";
        evaluation.passed = evaluation.actual == evaluation.expected;
        break;
    }
    case RunInteractionAutomationAssertKind::ReplayPredictionEnabled:
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( replay.prediction.enabled );
        evaluation.passed = replay.prediction.enabled == action.boolValue;
        break;
    case RunInteractionAutomationAssertKind::ReplayPathTarget:
        evaluation.expected = action.text;
        evaluation.actual = replay.path.hasTarget ? replay.path.targetName : "";
        evaluation.passed = evaluation.actual == evaluation.expected;
        break;
    case RunInteractionAutomationAssertKind::ReplayInterceptContact:
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = replay.intercept.valid ? BoolString( replay.intercept.intercept ) : "unavailable";
        evaluation.passed = replay.intercept.valid && replay.intercept.intercept == action.boolValue;
        break;
    case RunInteractionAutomationAssertKind::ReplayInterceptMissMax:
        evaluation.expected = "<=" + std::to_string( action.numberValue );
        evaluation.actual = replay.intercept.valid ? std::to_string( replay.intercept.missDistance ) : "unavailable";
        evaluation.passed = replay.intercept.valid && replay.intercept.missDistance <= action.numberValue;
        break;
    case RunInteractionAutomationAssertKind::ReplayInterceptEtaMin:
        evaluation.expected = ">=" + std::to_string( action.numberValue );
        evaluation.actual = replay.intercept.valid ? std::to_string( replay.intercept.etaSeconds ) : "unavailable";
        evaluation.passed = replay.intercept.valid && replay.intercept.etaSeconds >= action.numberValue;
        break;
    case RunInteractionAutomationAssertKind::ReplayInterceptEtaMax:
        evaluation.expected = "<=" + std::to_string( action.numberValue );
        evaluation.actual = replay.intercept.valid ? std::to_string( replay.intercept.etaSeconds ) : "unavailable";
        evaluation.passed = replay.intercept.valid && replay.intercept.etaSeconds <= action.numberValue;
        break;
    case RunInteractionAutomationAssertKind::ReplayTripPlannerState:
        evaluation.expected = action.text;
        evaluation.actual = TripPlannerStateName( replay.tripPlanner.state );
        evaluation.passed = evaluation.actual == evaluation.expected;
        break;
    case RunInteractionAutomationAssertKind::ReplayTripPlannerIterationMax:
        evaluation.expected = "<=" + std::to_string( static_cast<uint32_t>( action.numberValue ) );
        evaluation.actual = std::to_string( replay.tripPlanner.iteration );
        evaluation.passed = replay.tripPlanner.iteration <= static_cast<uint32_t>( action.numberValue );
        break;
    case RunInteractionAutomationAssertKind::ReplayTripPlannerMissMax:
        evaluation.expected = "<=" + std::to_string( action.numberValue );
        evaluation.actual = std::to_string( replay.tripPlanner.missDistance );
        evaluation.passed = replay.tripPlanner.missDistance <= action.numberValue;
        break;
    case RunInteractionAutomationAssertKind::ReplayTripPlannerMissesImprove:
    {
        bool improves = replay.tripPlanner.iterationMissCount > 0;
        std::ostringstream misses;

        // Why: the assertion's actual field carries the bounded sequence, not
        // merely "true", so a passing test-probe report is also convergence evidence.
        for ( std::size_t index = 0; index < replay.tripPlanner.iterationMissCount; ++index )
        {
            if ( index != 0 )
            {
                misses << ',';
            }

            misses << replay.tripPlanner.iterationMissDistances[index];

            if ( index > 0 && replay.tripPlanner.iterationMissDistances[index] >=
                                  replay.tripPlanner.iterationMissDistances[index - 1] - 0.001f )
            {
                improves = false;
            }
        }

        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = misses.str();
        evaluation.passed = improves == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::ReplayPorkchopComplete:
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( replay.porkchop.complete );
        evaluation.passed = replay.porkchop.complete == action.boolValue;
        break;
    case RunInteractionAutomationAssertKind::ReplayPorkchopMinimumDeltaVMax:
        evaluation.expected = "<=" + std::to_string( action.numberValue );
        evaluation.actual = std::to_string( replay.porkchop.minimumDeltaV );
        evaluation.passed = replay.porkchop.minimumDeltaV >= 0.0f && replay.porkchop.minimumDeltaV <= action.numberValue;

        break;
    case RunInteractionAutomationAssertKind::ReplayPorkchopMinimumDepartureDelayMax:
    {
        const float departure = ReplayPorkchopPanel::DepartureDelaySeconds( replay.porkchop.minimumCell %
                                                                            REPLAY_PORKCHOP_COLUMNS );

        evaluation.expected = "<=" + std::to_string( action.numberValue );
        evaluation.actual = std::to_string( departure );
        evaluation.passed = replay.porkchop.complete && departure <= action.numberValue;
        break;
    }
    case RunInteractionAutomationAssertKind::ReplayPorkchopMinimumTimeOfFlightMin:
    case RunInteractionAutomationAssertKind::ReplayPorkchopMinimumTimeOfFlightMax:
    {
        const float tof = ReplayPorkchopPanel::TimeOfFlightSeconds( replay.porkchop.minimumCell / REPLAY_PORKCHOP_COLUMNS );

        const bool minimum = action.assertKind == RunInteractionAutomationAssertKind::ReplayPorkchopMinimumTimeOfFlightMin;

        evaluation.expected = std::string( minimum ? ">=" : "<=" ) + std::to_string( action.numberValue );
        evaluation.actual = std::to_string( tof );
        evaluation.passed = replay.porkchop.complete && ( minimum ? tof >= action.numberValue : tof <= action.numberValue );

        break;
    }
    case RunInteractionAutomationAssertKind::ReplayPorkchopRefreshMillisecondsMax:
        evaluation.expected = "<=" + std::to_string( action.numberValue );
        evaluation.actual = std::to_string( replay.porkchop.refreshComputeMilliseconds );
        evaluation.passed = replay.porkchop.complete && replay.porkchop.refreshComputeMilliseconds <= action.numberValue;

        break;
    case RunInteractionAutomationAssertKind::ReplayPorkchopMaximumFrameMillisecondsMax:
        evaluation.expected = "<=" + std::to_string( action.numberValue );
        evaluation.actual = std::to_string( replay.porkchop.maximumFrameComputeMilliseconds );
        evaluation.passed = replay.porkchop.complete &&
                            replay.porkchop.maximumFrameComputeMilliseconds <= action.numberValue;

        break;
    case RunInteractionAutomationAssertKind::ReplayPorkchopSweepAgeSecondsMax:
        evaluation.expected = "<=" + std::to_string( action.numberValue );
        evaluation.actual = std::to_string( replay.porkchop.sweepAgeSeconds );
        evaluation.passed = replay.porkchop.complete && replay.porkchop.sweepAgeSeconds <= action.numberValue;
        break;
    case RunInteractionAutomationAssertKind::ReplayPorkchopSelected:
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( replay.porkchop.selectedCell >= 0 );
        evaluation.passed = ( replay.porkchop.selectedCell >= 0 ) == action.boolValue;
        break;
    case RunInteractionAutomationAssertKind::ReplayTripPlannerTimeOfFlightMin:
        evaluation.expected = ">=" + std::to_string( action.numberValue );
        evaluation.actual = std::to_string( replay.tripPlanner.timeOfFlightSeconds );
        evaluation.passed = replay.tripPlanner.timeOfFlightSeconds >= action.numberValue;
        break;
    case RunInteractionAutomationAssertKind::ReplayTripPlannerTimeOfFlightMax:
        evaluation.expected = "<=" + std::to_string( action.numberValue );
        evaluation.actual = std::to_string( replay.tripPlanner.timeOfFlightSeconds );
        evaluation.passed = replay.tripPlanner.timeOfFlightSeconds <= action.numberValue;
        break;
    case RunInteractionAutomationAssertKind::ReplayPastTrajectoryFullRebuildCountMax:
    {
        const uint64_t rebuildCount = replay.path.pastTrajectory.fullRebuildCount;
        evaluation.expected = "<=" + std::to_string( static_cast<uint64_t>( action.numberValue ) );
        evaluation.actual = std::to_string( rebuildCount );
        evaluation.passed = rebuildCount <= static_cast<uint64_t>( action.numberValue );
        break;
    }
    case RunInteractionAutomationAssertKind::ReplayPastTrajectoryIncrementalTrimCountMin:
    {
        const uint64_t trimCount = replay.path.pastTrajectory.incrementalTrimCount;
        evaluation.expected = ">=" + std::to_string( static_cast<uint64_t>( action.numberValue ) );
        evaluation.actual = std::to_string( trimCount );
        evaluation.passed = trimCount >= static_cast<uint64_t>( action.numberValue );
        break;
    }
    case RunInteractionAutomationAssertKind::ReplayPastTrajectoryPublishedPointCountMin:
    {
        const std::size_t pointCount = InteractionAutomationReportWriter::ReplayPastTrajectoryPublishedPointCount( replay );

        evaluation.expected = ">=" + std::to_string( static_cast<std::size_t>( action.numberValue ) );
        evaluation.actual = std::to_string( pointCount );
        evaluation.passed = pointCount >= static_cast<std::size_t>( action.numberValue );
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionPathVisible:
    {
        const bool visible = InteractionAutomationReportWriter::ReplayPredictionPathVisible( replay );
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( visible );
        evaluation.passed = visible == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionVelocityPreviewActive:
    {
        const bool active = replay.prediction.velocityDragPreview.active;
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( active );
        evaluation.passed = active == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionVelocityPreviewAwaitingReplacement:
    {
        const bool awaiting = replay.prediction.velocityDragPreview.awaitingAuthoritativeReplacement;
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( awaiting );
        evaluation.passed = awaiting == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionVelocityPreviewDeltaMin:
    {
        const ReplayVelocityDragPreviewState& preview = replay.prediction.velocityDragPreview;
        const float delta = std::sqrt( VectorMagSquared( preview.velocityDelta ) );
        evaluation.expected = ">=" + std::to_string( action.numberValue );
        evaluation.actual = preview.active ? std::to_string( delta ) : "preview inactive";
        evaluation.passed = preview.active && delta >= action.numberValue;
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionPresentedGenerationMin:
    {
        const uint32_t expectedGeneration = static_cast<uint32_t>( action.numberValue );
        evaluation.expected = ">=" + std::to_string( expectedGeneration );
        evaluation.actual = replay.prediction.BuildPrefixHasBeenPresented()
                                ? std::to_string( replay.prediction.build.generationBeginCount )
                                : "no presented replacement prefix";

        evaluation.passed = replay.prediction.BuildPrefixHasBeenPresented() &&
                            replay.prediction.build.generationBeginCount >= expectedGeneration;

        break;
    }
    case RunInteractionAutomationAssertKind::PredictionPresentedRootVelocityDeltaMin:
    {
        const std::span<const RunReplayPredictionFrame> committedFrames = replay.prediction.CommittedFrames();
        float velocityDelta = 0.0f;
        bool comparable = replay.prediction.BuildPrefixHasBeenPresented() && !committedFrames.empty() &&
                          !replay.prediction.build.buildFrames.empty();

        if ( comparable )
        {
            const auto findTargetVelocity = [&]( const RunReplayPredictionFrame& frame, Vector3& outVelocity )
            {
                for ( const RunReplayPredictionBodySample& body : frame.bodies )
                {
                    if ( body.id.value == replay.prediction.simulation.targetId.value )
                    {
                        outVelocity = body.linearVelocity;

                        return true;
                    }
                }

                return false;
            };

            Vector3 committedVelocity = SkullbonezCore::Math::Vector::ZERO_VECTOR;
            Vector3 replacementVelocity = SkullbonezCore::Math::Vector::ZERO_VECTOR;
            comparable = findTargetVelocity( committedFrames.front(), committedVelocity ) &&
                         findTargetVelocity( replay.prediction.build.buildFrames.front(), replacementVelocity );

            if ( comparable )
            {
                velocityDelta = std::sqrt( VectorMagSquared( replacementVelocity - committedVelocity ) );
            }
        }

        evaluation.expected = ">=" + std::to_string( action.numberValue );
        evaluation.actual = comparable ? std::to_string( velocityDelta ) : "no comparable presented prefix";
        evaluation.passed = comparable && velocityDelta >= action.numberValue;
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionFullHorizonComplete:
    {
        const RunReplayPredictionState& prediction = replay.prediction;
        const std::size_t expectedFrameCount = static_cast<std::size_t>( std::ceil( prediction.simulation.horizonSeconds / PHYSICS_FIXED_DT ) ) +
                                               1u;

        const bool complete = prediction.build.complete && !prediction.build.building &&
                              prediction.CommittedFrameCount() == expectedFrameCount;

        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( complete );
        evaluation.passed = complete == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionBuildMode:
    {
        const char* actualMode = InteractionAutomationReportWriter::ReplayPredictionBuildModeName( replay.prediction.build.buildMode );

        evaluation.expected = action.text;
        evaluation.actual = actualMode;
        evaluation.passed = evaluation.expected == evaluation.actual;
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionSupersededRestartCountMin:
    {
        const uint32_t count = replay.prediction.build.supersededRestartCount;
        evaluation.expected = ">=" + std::to_string( static_cast<uint32_t>( action.numberValue ) );
        evaluation.actual = std::to_string( count );
        evaluation.passed = count >= static_cast<uint32_t>( action.numberValue );
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionSupersededRestartCountMax:
    {
        const uint32_t count = replay.prediction.build.supersededRestartCount;
        evaluation.expected = "<=" + std::to_string( static_cast<uint32_t>( action.numberValue ) );
        evaluation.actual = std::to_string( count );
        evaluation.passed = count <= static_cast<uint32_t>( action.numberValue );
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionBaselineVisible:
    {
        const ReplayPredictionBaselineSnapshot& baseline = replay.prediction.baseline;
        const bool visible = baseline.valid && baseline.comparisonActive;
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( visible );
        evaluation.passed = visible == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionDivergenceMin:
    {
        const ReplayPredictionBaselineSnapshot& baseline = replay.prediction.baseline;
        {
            std::ostringstream stream;
            stream << ">=" << action.numberValue;
            evaluation.expected = stream.str();
        }
        {
            std::ostringstream stream;
            stream << ( baseline.divergenceValid ? baseline.divergenceUnits : 0.0f );
            evaluation.actual = stream.str();
        }
        evaluation.passed = baseline.divergenceValid && baseline.divergenceUnits >= action.numberValue;
        break;
    }
    case RunInteractionAutomationAssertKind::ReplaySolverTrackAtPresent:
    {
        const float solverPosition = replay.solverTrackPosition;
        const float presentT = replay.solverPresentTrackPosition;
        const bool atPresent = ReplayAtPresentTrackPosition( solverPosition, presentT );
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( atPresent );
        evaluation.passed = atPresent == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionScrubFrameActive:
    {
        const bool active = replay.currentPredictionFrame != nullptr;
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( active );
        evaluation.passed = active == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionTargetDisplacementMin:
    {
        float displacement = 0.0f;
        const bool valid = InteractionAutomationReportWriter::TryPredictionTargetDisplacement( replay, displacement );
        {
            std::ostringstream stream;
            stream << ">=" << action.numberValue;
            evaluation.expected = stream.str();
        }
        {
            std::ostringstream stream;
            stream << ( valid ? displacement : 0.0f );
            evaluation.actual = stream.str();
        }
        evaluation.passed = valid && displacement >= action.numberValue;
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionTargetLastNear:
    {
        float displacement = 0.0f;
        Vector3 last = ZERO_VECTOR;
        const bool valid = InteractionAutomationReportWriter::TryPredictionTargetDisplacement( replay, displacement, nullptr,
                                                                                               &last );

        const float error = valid ? sqrtf( VectorMagSquared( last - action.vectorValue ) ) : 0.0f;
        {
            std::ostringstream stream;
            stream << "[" << action.vectorValue.x << "," << action.vectorValue.y << "," << action.vectorValue.z << "] +/- "
                   << action.numberValue;
            evaluation.expected = stream.str();
        }
        {
            std::ostringstream stream;
            stream << "[" << last.x << "," << last.y << "," << last.z << "] error=" << error;
            evaluation.actual = stream.str();
        }
        evaluation.passed = valid && error <= action.numberValue;
        break;
    }
    case RunInteractionAutomationAssertKind::LiveSolverHashStableAcrossPrediction:
    {
        const bool stable = InteractionAutomationReportWriter::LiveSolverHashStableAcrossPrediction( replay );
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( stable );
        evaluation.passed = stable == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionEvidenceConsumerBalanced:
    {
        const ReplayPredictionSolverEvidenceCaptureStats capture = replay.predictionEvidenceCapture;
        const bool balanced = !capture.consumerActive && capture.consumerAcquireCount == capture.consumerReleaseCount;
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( balanced );
        evaluation.passed = balanced == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionEvidencePipelineRowsMin:
    {
        const uint64_t count = replay.predictionEvidenceCapture.copiedPipelineCount;
        evaluation.expected = ">=" + std::to_string( static_cast<uint64_t>( action.numberValue ) );
        evaluation.actual = std::to_string( count );
        evaluation.passed = count >= static_cast<uint64_t>( action.numberValue );
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionEvidenceCurrentCapacityMax:
    {
        const uint64_t bytes = replay.predictionEvidenceMemory.currentCapacityBytes;
        evaluation.expected = "<=" + std::to_string( static_cast<uint64_t>( action.numberValue ) );
        evaluation.actual = std::to_string( bytes );
        evaluation.passed = bytes <= static_cast<uint64_t>( action.numberValue );
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionDetailMode:
    {
        const char* mode = replay.predictionDetailMode == ReplayPredictionDetailMode::High ? "High" : "Low";
        evaluation.expected = action.text;
        evaluation.actual = mode;
        evaluation.passed = evaluation.actual == evaluation.expected;
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionCauseDetailVisible:
    {
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( replay.causeInspection.detailVisible );
        evaluation.passed = replay.causeInspection.detailVisible == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionCauseWindowAvailable:
    {
        const bool predictionRows = !replay.causeTree.rows.empty() && replay.causeTree.rows.front().prediction;
        const bool available = !replay.causeTree.rows.empty() &&
                               ReplayPredictionCauseWindowAvailable( replay.predictionDetailMode, predictionRows );
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( available );
        evaluation.passed = available == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionEvidenceCapacityReleased:
    {
        const ReplayPredictionSolverEvidenceBanksMemoryStats& memory = replay.predictionEvidenceMemory;
        const bool released = memory.releaseCheckpointCount > 0u && memory.currentContactCapacityBytes == 0u &&
                              memory.currentPipelineCapacityBytes == 0u && memory.currentFrameCapacityBytes == 0u &&
                              memory.currentCapacityBytes == 0u && memory.lastReleaseBeforeCapacityBytes > 0u &&
                              memory.lastReleaseAfterCapacityBytes == 0u;
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( released );
        evaluation.passed = released == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionEvidenceMemoryReconciled:
    {
        const bool reconciled = SkullbonezCore::Core::MainMemoryReplayPredictionEvidenceReleaseReconciles( replay.memoryStats );
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( reconciled );
        evaluation.passed = reconciled == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionCauseManifoldRowsMin:
    case RunInteractionAutomationAssertKind::PredictionCauseManifoldRowsMax:
    case RunInteractionAutomationAssertKind::PredictionCauseSolverRowsMin:
    case RunInteractionAutomationAssertKind::PredictionCauseSolverRowsMax:
    case RunInteractionAutomationAssertKind::PredictionCauseSyntheticRowsMin:
    case RunInteractionAutomationAssertKind::PredictionCauseSyntheticRowsMax:
    {
        std::size_t count = 0u;

        for ( const RunReplayCauseTreeRow& row : replay.causeTree.rows )
        {
            const bool manifold = row.prediction && row.kind == RunReplayCauseTreeRowKind::Manifold;
            const bool solver = row.prediction && row.kind == RunReplayCauseTreeRowKind::SolverRow;
            const bool synthetic = row.prediction && ( row.kind == RunReplayCauseTreeRowKind::PredictionContact ||
                                                       row.kind == RunReplayCauseTreeRowKind::PredictionMotion );

            if ( ( ( action.assertKind == RunInteractionAutomationAssertKind::PredictionCauseManifoldRowsMin ||
                     action.assertKind == RunInteractionAutomationAssertKind::PredictionCauseManifoldRowsMax ) &&
                   manifold ) ||
                 ( ( action.assertKind == RunInteractionAutomationAssertKind::PredictionCauseSolverRowsMin ||
                     action.assertKind == RunInteractionAutomationAssertKind::PredictionCauseSolverRowsMax ) &&
                   solver ) ||
                 ( ( action.assertKind == RunInteractionAutomationAssertKind::PredictionCauseSyntheticRowsMin ||
                     action.assertKind == RunInteractionAutomationAssertKind::PredictionCauseSyntheticRowsMax ) &&
                   synthetic ) )
            {
                ++count;
            }
        }

        const std::size_t expected = static_cast<std::size_t>( action.numberValue );
        const bool maximum = action.assertKind == RunInteractionAutomationAssertKind::PredictionCauseManifoldRowsMax ||
                             action.assertKind == RunInteractionAutomationAssertKind::PredictionCauseSolverRowsMax ||
                             action.assertKind == RunInteractionAutomationAssertKind::PredictionCauseSyntheticRowsMax;
        evaluation.expected = std::string( maximum ? "<=" : ">=" ) + std::to_string( expected );
        evaluation.actual = std::to_string( count );
        evaluation.passed = maximum ? count <= expected : count >= expected;
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionTrajectoryFingerprintReady:
    {
        const PredictionTrajectoryFingerprint
            fingerprint = InteractionAutomationReportWriter::BuildPredictionTrajectoryFingerprint( replay );

        const bool ready = fingerprint.Ready();
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( ready );
        evaluation.passed = ready == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionAppearanceInvalidationCountMin:
    {
        const uint64_t count = replay.predictionAppearanceInvalidationCount;
        {
            std::ostringstream stream;
            stream << ">=" << static_cast<uint64_t>( action.numberValue );
            evaluation.expected = stream.str();
        }
        evaluation.actual = std::to_string( count );
        evaluation.passed = count >= static_cast<uint64_t>( action.numberValue );
        break;
    }
    case RunInteractionAutomationAssertKind::ContinuousForecastActive:
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( forecast.active );
        evaluation.passed = forecast.active == action.boolValue;
        break;
    case RunInteractionAutomationAssertKind::ContinuousForecastPreWrap:
    {
        const bool preWrap = forecast.active && forecast.presentation.coherent && !forecast.presentation.wrapped &&
                             forecast.presentation.newestAbsoluteTick > 0u;
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( preWrap );
        evaluation.passed = preWrap == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::ContinuousForecastWindowWrapped:
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( forecast.presentation.wrapped );
        evaluation.passed = forecast.presentation.wrapped == action.boolValue;
        break;
    case RunInteractionAutomationAssertKind::ContinuousForecastPresentationCoherent:
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( forecast.presentation.coherent );
        evaluation.passed = forecast.presentation.coherent == action.boolValue;
        break;
    case RunInteractionAutomationAssertKind::ContinuousForecastAbsoluteTickMin:
        evaluation.expected = ">=" + std::to_string( static_cast<std::uint64_t>( action.numberValue ) );
        evaluation.actual = std::to_string( forecast.presentation.newestAbsoluteTick );
        evaluation.passed = forecast.presentation.newestAbsoluteTick >= static_cast<std::uint64_t>( action.numberValue );
        break;
    case RunInteractionAutomationAssertKind::ContinuousForecastOldestTickMin:
        evaluation.expected = ">=" + std::to_string( static_cast<std::uint64_t>( action.numberValue ) );
        evaluation.actual = std::to_string( forecast.presentation.oldestAbsoluteTick );
        evaluation.passed = forecast.presentation.oldestAbsoluteTick >= static_cast<std::uint64_t>( action.numberValue );
        break;
    case RunInteractionAutomationAssertKind::ContinuousForecastRibbonSegmentsMin:
        evaluation.expected = ">=" + std::to_string( static_cast<std::size_t>( action.numberValue ) );
        evaluation.actual = std::to_string( forecast.presentation.ribbonSegmentCount );
        evaluation.passed = forecast.presentation.ribbonSegmentCount >= static_cast<std::size_t>( action.numberValue );
        break;
    case RunInteractionAutomationAssertKind::ContinuousForecastHeadMarkerCount:
        evaluation.expected = std::to_string( static_cast<std::size_t>( action.numberValue ) );
        evaluation.actual = std::to_string( forecast.presentation.headMarkerCount );
        evaluation.passed = forecast.presentation.headMarkerCount == static_cast<std::size_t>( action.numberValue );
        break;
    case RunInteractionAutomationAssertKind::ShadowPassExecuted:
    case RunInteractionAutomationAssertKind::TerrainShadowValid:
    case RunInteractionAutomationAssertKind::ObjectShadowValid:
    case RunInteractionAutomationAssertKind::ReflectionPassExecuted:
    {
        bool actual = false;

        if ( action.assertKind == RunInteractionAutomationAssertKind::ShadowPassExecuted )
        {
            actual = renderSnapshot.shadowPassExecuted;
        }
        else if ( action.assertKind == RunInteractionAutomationAssertKind::TerrainShadowValid )
        {
            actual = renderSnapshot.terrainShadowValid;
        }
        else if ( action.assertKind == RunInteractionAutomationAssertKind::ObjectShadowValid )
        {
            actual = renderSnapshot.objectShadowValid;
        }
        else
        {
            actual = renderSnapshot.reflectionPassExecuted;
        }

        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( actual );
        evaluation.passed = actual == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::GizmoVisible:
    {
        const bool visible = editorTools.Editor().selectedBody.IsValid() &&
                             ( editorTools.Editor().editorModeEnabled || inspectGizmoInteractionActive() );

        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( visible );
        evaluation.passed = visible == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::MousePickupActive:
    {
        const bool active = interaction.Gesture().kind == RuntimeInteractionGestureKind::MousePickupDrag;
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( active );
        evaluation.passed = active == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::PointerCapture:
    {
        const auto captureName = []( RuntimePointerCaptureOwner owner ) -> const char*
        {
            switch ( owner )
            {
            case RuntimePointerCaptureOwner::None:
                return "None";

            case RuntimePointerCaptureOwner::UI:
                return "UI";
            case RuntimePointerCaptureOwner::CameraLook:
                return "CameraLook";
            case RuntimePointerCaptureOwner::ToolGesture:
                return "ToolGesture";
            }

            return "Unknown";
        };

        evaluation.expected = action.text;
        evaluation.actual = captureName( interaction.PointerCapture() );
        evaluation.passed = evaluation.actual == evaluation.expected;
        break;
    }
    case RunInteractionAutomationAssertKind::NativeCaptureRequested:
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( inputRouter.NativeCaptureRequested() );
        evaluation.passed = inputRouter.NativeCaptureRequested() == action.boolValue;
        break;
    case RunInteractionAutomationAssertKind::CursorVisibleRequested:
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( inputRouter.CursorVisibleRequested() );
        evaluation.passed = inputRouter.CursorVisibleRequested() == action.boolValue;
        break;
    case RunInteractionAutomationAssertKind::UiBlocksMouse:
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( inputRouter.UiSnapshot().blocksCameraMouse );
        evaluation.passed = inputRouter.UiSnapshot().blocksCameraMouse == action.boolValue;
        break;
    case RunInteractionAutomationAssertKind::LauncherRayActive:
    {
        const bool active = runtimeTools.Laser().HasActiveShots();
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( active );
        evaluation.passed = active == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::ReplayActiveTrack:
        evaluation.expected = action.text;
        evaluation.actual = InteractionAutomationReportWriter::ReplayTrackName( replay.scrubber.activeTrack );
        evaluation.passed = evaluation.actual == evaluation.expected;
        break;
    case RunInteractionAutomationAssertKind::ReplayHistoricalSamplePaused:
    {
        const bool paused = replay.scrubber.historicalSamplePaused;
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( paused );
        evaluation.passed = paused == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::MemoryOverlayEnabled:
    {
        const bool enabled = ui.IsMemoryOverlayEnabled();
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( enabled );
        evaluation.passed = enabled == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::EditorUndoDepth:
    case RunInteractionAutomationAssertKind::EditorRedoDepth:
    {
        const int actual = static_cast<int>( action.assertKind == RunInteractionAutomationAssertKind::EditorUndoDepth
                                                 ? editorTools.Editor().history.UndoDepth()
                                                 : editorTools.Editor().history.RedoDepth() );

        const int expected = static_cast<int>( action.numberValue );
        evaluation.expected = std::to_string( expected );
        evaluation.actual = std::to_string( actual );
        evaluation.passed = actual == expected;
        break;
    }
    case RunInteractionAutomationAssertKind::EditorSelectionExists:
    case RunInteractionAutomationAssertKind::EditorSelectionHasTerrain:
    {
        const EditorSelectionFingerprint fingerprint = BuildEditorSelectionFingerprint( editorTools, world );
        const bool actual = action.assertKind == RunInteractionAutomationAssertKind::EditorSelectionExists
                                ? fingerprint.valid
                                : ( fingerprint.valid && fingerprint.hasTerrain );

        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( actual );
        evaluation.passed = actual == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::EditorSelectionMatchesCapture:
    {
        const int slot = static_cast<int>( action.numberValue );
        const EditorSelectionFingerprint fingerprint = BuildEditorSelectionFingerprint( editorTools, world );
        uint64_t capturedFingerprint = 0;
        const bool captureValid = automation.reportWriter.TryEditorSelectionCapture( slot, capturedFingerprint );
        evaluation.expected = captureValid ? InteractionAutomationReportWriter::FormatPredictionHash( capturedFingerprint )
                                           : "valid capture";

        evaluation.actual = fingerprint.valid ? InteractionAutomationReportWriter::FormatPredictionHash( fingerprint.hash )
                                              : "no selection";

        evaluation.passed = captureValid && fingerprint.valid && fingerprint.hash == capturedFingerprint;
        break;
    }
    case RunInteractionAutomationAssertKind::DevelopmentUiSurface:
        evaluation.expected = action.text;
        evaluation.actual = developmentUi.available ? ( developmentUi.selectedImGui ? "imgui" : "game" ) : "unavailable";

        evaluation.passed = developmentUi.available && evaluation.actual == evaluation.expected &&
                            !( developmentUi.gameUiVisible && developmentUi.imguiVisible );

        break;
    case RunInteractionAutomationAssertKind::ImGuiVisible:
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( developmentUi.imguiVisible );
        evaluation.passed = developmentUi.available && developmentUi.imguiVisible == action.boolValue;
        break;
    case RunInteractionAutomationAssertKind::GameUiReplayPresentationActive:
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( developmentUi.gameUiReplayPresentationActive );
        evaluation.passed = developmentUi.available && developmentUi.gameUiReplayPresentationActive == action.boolValue;
        break;
    case RunInteractionAutomationAssertKind::ImGuiPanelMask:
    {
        const uint32_t expected = static_cast<uint32_t>( action.numberValue );
        evaluation.expected = std::to_string( expected );
        evaluation.actual = std::to_string( developmentUi.panelVisibilityMask );
        evaluation.passed = developmentUi.available && developmentUi.panelVisibilityMask == expected;
        break;
    }
    case RunInteractionAutomationAssertKind::ImGuiLayoutResetCountMin:
    {
        const uint32_t expected = static_cast<uint32_t>( action.numberValue );
        evaluation.expected = ">=" + std::to_string( expected );
        evaluation.actual = std::to_string( developmentUi.layoutResetCount );
        evaluation.passed = developmentUi.available && developmentUi.layoutResetCount >= expected;
        break;
    }
    case RunInteractionAutomationAssertKind::ImGuiFocusCountMin:
    {
        const uint32_t expected = static_cast<uint32_t>( action.numberValue );
        evaluation.expected = ">=" + std::to_string( expected );
        evaluation.actual = std::to_string( developmentUi.automationFocusCount );
        evaluation.passed = developmentUi.available && developmentUi.automationFocusCount >= expected;
        break;
    }
    case RunInteractionAutomationAssertKind::ImGuiDpiScale:
    {
        std::ostringstream expected;
        expected << action.numberValue;
        std::ostringstream actual;
        actual << developmentUi.appliedDpiScale;
        evaluation.expected = expected.str();
        evaluation.actual = actual.str();
        evaluation.passed = developmentUi.available &&
                            std::fabs( developmentUi.appliedDpiScale - action.numberValue ) <= 0.01f;

        break;
    }
    case RunInteractionAutomationAssertKind::ImGuiDescriptorHighWaterMax:
    {
        const uint32_t expected = static_cast<uint32_t>( action.numberValue );
        evaluation.expected = "<=" + std::to_string( expected );
        evaluation.actual = std::to_string( developmentUi.rendererDescriptorHighWater );
        evaluation.passed = developmentUi.available && developmentUi.rendererDescriptorHighWater <= expected;
        break;
    }
    case RunInteractionAutomationAssertKind::ImGuiViewportRecreationsMin:
    {
        const uint32_t expected = static_cast<uint32_t>( action.numberValue );
        evaluation.expected = ">=" + std::to_string( expected );
        evaluation.actual = std::to_string( developmentUi.gameViewportRecreations );
        evaluation.passed = developmentUi.available && developmentUi.gameViewportRecreations >= expected;
        break;
    }
    case RunInteractionAutomationAssertKind::ImGuiPreferencesRecovered:
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( developmentUi.preferencesRecovered );
        evaluation.passed = developmentUi.available && developmentUi.preferencesRecovered == action.boolValue;
        break;
    }

    return evaluation;
}

bool IsSafeRecordingSidecarPath( const std::filesystem::path& path )
{
    if ( path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory() )
    {
        return false;
    }

    for ( const std::filesystem::path& component : path )
    {
        if ( component == ".." )
        {
            return false;
        }
    }

    return true;
}

bool HashInteractionSidecar( const std::filesystem::path& path, std::string& outDigest )
{
    std::ifstream input( path, std::ios::binary );

    if ( !input )
    {
        return false;
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectBytes = 0;
    DWORD resultBytes = 0;
    std::vector<UCHAR> object;
    std::array<UCHAR, 32> digest = {};
    bool ok = BCryptOpenAlgorithmProvider( &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0 ) >= 0;

    if ( ok )
    {
        ok = BCryptGetProperty( algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>( &objectBytes ),
                                sizeof( objectBytes ), &resultBytes, 0 ) >= 0;
    }

    if ( ok )
    {
        object.resize( objectBytes );
        ok = BCryptCreateHash( algorithm, &hash, object.data(), objectBytes, nullptr, 0, 0 ) >= 0;
    }

    std::array<char, 64 * 1024> buffer = {};

    while ( ok && input )
    {
        input.read( buffer.data(), static_cast<std::streamsize>( buffer.size() ) );
        const std::streamsize count = input.gcount();

        if ( count > 0 )
        {
            ok = BCryptHashData( hash, reinterpret_cast<PUCHAR>( buffer.data() ), static_cast<ULONG>( count ), 0 ) >= 0;
        }
    }

    // A normal EOF sets failbit after the final short read; badbit instead
    // means the digest covered only a prefix and must never authenticate it.
    ok = ok && !input.bad();

    if ( ok )
    {
        ok = BCryptFinishHash( hash, digest.data(), static_cast<ULONG>( digest.size() ), 0 ) >= 0;
    }

    if ( hash )
    {
        BCryptDestroyHash( hash );
    }

    if ( algorithm )
    {
        BCryptCloseAlgorithmProvider( algorithm, 0 );
    }

    if ( !ok )
    {
        return false;
    }

    std::ostringstream stream;
    stream << std::hex << std::setfill( '0' );

    for ( const UCHAR byte : digest )
    {
        stream << std::setw( 2 ) << static_cast<unsigned int>( byte );
    }

    outDigest = stream.str();
    return true;
}

bool ValidateInteractionSidecar( const Json& root, const char* field, const std::filesystem::path& manifestPath,
                                 bool required, std::string& outError )
{
    const auto sidecar = root.find( field );

    if ( sidecar == root.end() )
    {
        if ( required )
        {
            outError = std::string( "recorded manifest requires " ) + field + " sidecar metadata";
            return false;
        }

        return true;
    }

    if ( !sidecar->is_object() || !sidecar->contains( "path" ) || !( *sidecar )["path"].is_string() ||
         !sidecar->contains( "sha256" ) || !( *sidecar )["sha256"].is_string() )
    {
        outError = std::string( "recorded manifest " ) + field + " metadata is invalid";
        return false;
    }

    const std::filesystem::path relative = ( *sidecar )["path"].get<std::string>();

    if ( !IsSafeRecordingSidecarPath( relative ) )
    {
        outError = std::string( "recorded manifest " ) + field + " path is not a safe relative path";
        return false;
    }

    const std::string expected = ( *sidecar )["sha256"].get<std::string>();
    std::string actual;

    if ( expected.size() != 64u || !HashInteractionSidecar( manifestPath.parent_path() / relative, actual ) ||
         actual != expected )
    {
        outError = std::string( "recorded manifest " ) + field + " SHA-256 mismatch";
        return false;
    }

    return true;
}

bool ParseHexKeyWord( const Json& value, uint64_t& out )
{
    if ( !value.is_string() )
    {
        return false;
    }

    const std::string text = value.get<std::string>();

    if ( text.size() != 16u || text.find_first_not_of( "0123456789abcdefABCDEF" ) != std::string::npos )
    {
        return false;
    }

    const char* begin = text.data();
    const char* end = begin + text.size();
    const std::from_chars_result parsed = std::from_chars( begin, end, out, 16 );
    return parsed.ec == std::errc {} && parsed.ptr == end;
}

bool ReadUnsignedInteger( const Json& value, uint64_t& out )
{
    if ( !value.is_number_unsigned() )
    {
        return false;
    }

    out = value.get<uint64_t>();
    return true;
}

bool ReadSignedInteger( const Json& value, int64_t& out )
{
    if ( value.is_number_unsigned() )
    {
        const uint64_t unsignedValue = value.get<uint64_t>();

        if ( unsignedValue > static_cast<uint64_t>( ( std::numeric_limits<int64_t>::max )() ) )
        {
            return false;
        }

        out = static_cast<int64_t>( unsignedValue );
        return true;
    }

    if ( !value.is_number_integer() )
    {
        return false;
    }

    out = value.get<int64_t>();
    return true;
}

bool ReadInt( const Json& value, int& out )
{
    int64_t parsed = 0;

    if ( !ReadSignedInteger( value, parsed ) || parsed < ( std::numeric_limits<int>::min )() ||
         parsed > ( std::numeric_limits<int>::max )() )
    {
        return false;
    }

    out = static_cast<int>( parsed );
    return true;
}

bool ReadLong( const Json& value, long& out )
{
    int64_t parsed = 0;

    if ( !ReadSignedInteger( value, parsed ) || parsed < ( std::numeric_limits<long>::min )() ||
         parsed > ( std::numeric_limits<long>::max )() )
    {
        return false;
    }

    out = static_cast<long>( parsed );
    return true;
}

bool LoadRecordedInteractionManifest( InteractionAutomationController& state, const Json& root )
{
    std::string error;
    const std::filesystem::path manifestPath = std::filesystem::path( state.scriptPath ).lexically_normal();
    int version = 0;

    if ( !root.contains( "complete" ) || !root["complete"].is_boolean() || !root["complete"].get<bool>() ||
         !root.contains( "version" ) || !ReadInt( root["version"], version ) || version != 1 )
    {
        FailAutomation( state, "recorded manifest is incomplete or has an unsupported version" );
        return false;
    }

    int sourceWidth = 0;
    int sourceHeight = 0;

    if ( !root.contains( "sourceViewport" ) || !root["sourceViewport"].is_object() ||
         !root["sourceViewport"].contains( "width" ) || !ReadInt( root["sourceViewport"]["width"], sourceWidth ) ||
         !root["sourceViewport"].contains( "height" ) || !ReadInt( root["sourceViewport"]["height"], sourceHeight ) ||
         sourceWidth <= 0 || sourceHeight <= 0 || !root.contains( "durationSeconds" ) ||
         !root["durationSeconds"].is_number() )
    {
        FailAutomation( state, "recorded manifest viewport or duration metadata is invalid" );
        return false;
    }

    const double declaredDuration = root["durationSeconds"].get<double>();

    if ( !std::isfinite( declaredDuration ) || declaredDuration < 0.0 ||
         declaredDuration > static_cast<double>( InteractionAutomationRecorder::MAX_RECORDING_MINUTES ) * 60.0 )
    {
        FailAutomation( state, "recorded manifest duration is outside the supported range" );
        return false;
    }

    if ( !ValidateInteractionSidecar( root, "scene", manifestPath, true, error ) ||
         !ValidateInteractionSidecar( root, "replay", manifestPath, false, error ) )
    {
        FailAutomation( state, error.c_str() );
        return false;
    }

    const auto frames = root.find( "frames" );

    if ( frames == root.end() || !frames->is_array() ||
         frames->size() >
             InteractionAutomationRecorder::FRAMES_PER_MINUTE * InteractionAutomationRecorder::MAX_RECORDING_MINUTES )
    {
        FailAutomation( state, "recorded manifest frames array is missing or exceeds the hard limit" );
        return false;
    }

    state.recordedFrames.reserve( frames->size() );
    double accumulatedDuration = 0.0;

    for ( std::size_t index = 0u; index < frames->size(); ++index )
    {
        const Json& entry = ( *frames )[index];
        RecordedInputFrame frame;
        uint64_t recordedTurn = 0;

        if ( !entry.is_object() || !entry.contains( "turn" ) || !ReadUnsignedInteger( entry["turn"], recordedTurn ) ||
             recordedTurn != index || !entry.contains( "deltaSeconds" ) || !entry["deltaSeconds"].is_number() )
        {
            FailAutomation( state, "recorded manifest turn numbering or timing is invalid" );
            return false;
        }

        frame.turn = index;
        frame.deltaSeconds = entry["deltaSeconds"].get<double>();

        if ( !std::isfinite( frame.deltaSeconds ) || frame.deltaSeconds < 0.0 || frame.deltaSeconds > 0.05 ||
             !entry.contains( "keys" ) || !entry["keys"].is_array() || entry["keys"].size() != 4u )
        {
            FailAutomation( state, "recorded manifest contains invalid timing or key state" );
            return false;
        }

        accumulatedDuration += frame.deltaSeconds;

        for ( std::size_t word = 0u; word < frame.keyWords.size(); ++word )
        {
            if ( !ParseHexKeyWord( entry["keys"][word], frame.keyWords[word] ) )
            {
                FailAutomation( state, "recorded manifest contains an invalid key word" );
                return false;
            }
        }

        if ( !entry.contains( "focused" ) || !entry["focused"].is_boolean() || !entry.contains( "left" ) ||
             !entry["left"].is_boolean() || !entry.contains( "right" ) || !entry["right"].is_boolean() ||
             !entry.contains( "middle" ) || !entry["middle"].is_boolean() || !entry.contains( "wheel" ) )
        {
            FailAutomation( state, "recorded manifest contains invalid device state" );
            return false;
        }

        frame.appFocused = entry["focused"].get<bool>();
        frame.leftDown = entry["left"].get<bool>();
        frame.rightDown = entry["right"].get<bool>();
        frame.middleDown = entry["middle"].get<bool>();

        if ( !ReadInt( entry["wheel"], frame.wheelDelta ) )
        {
            FailAutomation( state, "recorded manifest wheel delta is outside the supported integer range" );
            return false;
        }

        if ( entry.contains( "rawMouse" ) )
        {
            if ( !entry["rawMouse"].is_array() || entry["rawMouse"].size() != 2u ||
                 !ReadLong( entry["rawMouse"][0], frame.rawMouseX ) || !ReadLong( entry["rawMouse"][1], frame.rawMouseY ) )
            {
                FailAutomation( state, "recorded manifest raw mouse delta is invalid" );
                return false;
            }
        }

        if ( entry.contains( "pointer" ) )
        {
            const Json& pointer = entry["pointer"];

            if ( !pointer.is_array() || pointer.size() != 2u || !pointer[0].is_number() || !pointer[1].is_number() )
            {
                FailAutomation( state, "recorded manifest pointer is invalid" );
                return false;
            }

            frame.normalizedX = pointer[0].get<float>();
            frame.normalizedY = pointer[1].get<float>();
            frame.hasPointer = std::isfinite( frame.normalizedX ) && std::isfinite( frame.normalizedY ) &&
                               frame.normalizedX >= 0.0f && frame.normalizedX <= 1.0f && frame.normalizedY >= 0.0f &&
                               frame.normalizedY <= 1.0f;

            if ( !frame.hasPointer )
            {
                FailAutomation( state, "recorded manifest normalized pointer is outside [0,1]" );
                return false;
            }
        }

        if ( entry.contains( "semanticAnchor" ) )
        {
            if ( !entry["semanticAnchor"].is_string() ||
                 entry["semanticAnchor"].get_ref<const std::string&>().size() >= sizeof( frame.semanticAnchor ) )
            {
                FailAutomation( state, "recorded manifest semantic anchor is invalid" );
                return false;
            }

            CopyText( frame.semanticAnchor, sizeof( frame.semanticAnchor ), entry["semanticAnchor"].get<std::string>() );
        }

        state.recordedFrames.push_back( frame );
    }

    uint64_t turnCount = 0;

    if ( !root.contains( "turnCount" ) || !ReadUnsignedInteger( root["turnCount"], turnCount ) ||
         turnCount != state.recordedFrames.size() )
    {
        FailAutomation( state, "recorded manifest turnCount does not match frames" );
        return false;
    }

    if ( std::abs( accumulatedDuration - declaredDuration ) > 1.0e-6 )
    {
        FailAutomation( state, "recorded manifest duration does not match its frame timing" );
        return false;
    }

    if ( !root.contains( "baseline" ) || !root["baseline"].is_object() )
    {
        FailAutomation( state, "recorded manifest baseline is missing" );
        return false;
    }

    const Json& baseline = root["baseline"];
    const Json camera = baseline.value( "camera", Json::object() );
    const Json interaction = baseline.value( "interaction", Json::object() );
    const Json tools = baseline.value( "tools", Json::object() );
    const Json ui = baseline.value( "ui", Json::object() );
    const Json replay = baseline.value( "replay", Json::object() );
    const Json causeInspection = replay.value( "causeInspection", Json::object() );

    if ( !camera.contains( "mode" ) || !interaction.contains( "worldOwner" ) || !tools.contains( "editorMode" ) ||
         !tools["editorMode"].is_boolean() || !tools.contains( "placementMode" ) || !tools["placementMode"].is_boolean() ||
         !tools.contains( "placeStatic" ) || !tools["placeStatic"].is_boolean() || !tools.contains( "terrainAlign" ) ||
         !tools["terrainAlign"].is_boolean() || !tools.contains( "objectType" ) || !tools.contains( "selection" ) ||
         !tools["selection"].is_string() || !ui.contains( "visible" ) || !ui["visible"].is_boolean() ||
         !ui.contains( "minimized" ) || !ui["minimized"].is_boolean() || !ui.contains( "activeTab" ) ||
         !ui.contains( "developmentSurface" ) || !replay.contains( "active" ) || !replay["active"].is_boolean() ||
         !replay.contains( "scrubPaused" ) || !replay["scrubPaused"].is_boolean() || !replay.contains( "liveAdvanceHeld" ) ||
         !replay["liveAdvanceHeld"].is_boolean() || !replay.contains( "predictionEnabled" ) ||
         !replay["predictionEnabled"].is_boolean() || !replay.contains( "track" ) ||
         !replay.contains( "presentationTrackPosition" ) || !replay["presentationTrackPosition"].is_number() ||
         !replay.contains( "solverTrackPosition" ) || !replay["solverTrackPosition"].is_number() ||
         !replay.contains( "pathTarget" ) || !replay["pathTarget"].is_string() || !causeInspection.contains( "mode" ) ||
         !causeInspection.contains( "selectedRow" ) || !causeInspection.contains( "activeTab" ) ||
         !causeInspection.contains( "selectedDetailContactRow" ) || !causeInspection.contains( "solverDetailFirstRow" ) ||
         !causeInspection.contains( "rawRecordFirstRow" ) || !causeInspection.contains( "iterationsFirstRow" ) ||
         !causeInspection.contains( "sourceFrame" ) || !causeInspection.contains( "targetFrame" ) ||
         !causeInspection.contains( "presentedFrame" ) || !causeInspection.contains( "detailVisible" ) ||
         !causeInspection["detailVisible"].is_boolean() || !causeInspection.contains( "ownsPause" ) ||
         !causeInspection["ownsPause"].is_boolean() || !causeInspection.contains( "transportPending" ) ||
         !causeInspection["transportPending"].is_boolean() || !causeInspection.contains( "transportInFlight" ) ||
         !causeInspection["transportInFlight"].is_boolean() || !causeInspection.contains( "returnIssued" ) ||
         !causeInspection["returnIssued"].is_boolean() || !causeInspection.contains( "easedProgress" ) ||
         !causeInspection["easedProgress"].is_number() || !causeInspection.contains( "drawerProgress" ) ||
         !causeInspection["drawerProgress"].is_number() )
    {
        FailAutomation( state, "recorded manifest baseline state is incomplete or invalid" );
        return false;
    }

    int cameraMode = 0;
    int worldOwner = 0;
    int objectType = 0;
    int activeUiTab = 0;
    int developmentUiSurface = 0;
    int replayTrack = 0;
    int causeMode = 0;
    int causeSelectedRow = -1;
    int causeActiveTab = 0;
    int causeSelectedDetailContactRow = -1;
    int causeSolverDetailFirstRow = 0;
    int causeRawRecordFirstRow = 0;
    int causeIterationsFirstRow = 0;
    uint64_t causeSourceFrame = 0u;
    uint64_t causeTargetFrame = 0u;
    uint64_t causePresentedFrame = 0u;
    const std::string& selection = tools["selection"].get_ref<const std::string&>();
    const std::string& pathTarget = replay["pathTarget"].get_ref<const std::string&>();
    const double presentationTrackPosition = replay["presentationTrackPosition"].get<double>();
    const double solverTrackPosition = replay["solverTrackPosition"].get<double>();
    const double causeEasedProgress = causeInspection["easedProgress"].get<double>();
    const double causeDrawerProgress = causeInspection["drawerProgress"].get<double>();

    if ( !ReadInt( camera["mode"], cameraMode ) || !ReadInt( interaction["worldOwner"], worldOwner ) ||
         !ReadInt( tools["objectType"], objectType ) || !ReadInt( ui["activeTab"], activeUiTab ) ||
         !ReadInt( ui["developmentSurface"], developmentUiSurface ) || !ReadInt( replay["track"], replayTrack ) ||
         !ReadInt( causeInspection["mode"], causeMode ) || !ReadInt( causeInspection["selectedRow"], causeSelectedRow ) ||
         !ReadInt( causeInspection["activeTab"], causeActiveTab ) ||
         !ReadInt( causeInspection["selectedDetailContactRow"], causeSelectedDetailContactRow ) ||
         !ReadInt( causeInspection["solverDetailFirstRow"], causeSolverDetailFirstRow ) ||
         !ReadInt( causeInspection["rawRecordFirstRow"], causeRawRecordFirstRow ) ||
         !ReadInt( causeInspection["iterationsFirstRow"], causeIterationsFirstRow ) ||
         !ReadUnsignedInteger( causeInspection["sourceFrame"], causeSourceFrame ) ||
         !ReadUnsignedInteger( causeInspection["targetFrame"], causeTargetFrame ) ||
         !ReadUnsignedInteger( causeInspection["presentedFrame"], causePresentedFrame ) || cameraMode < 0 ||
         cameraMode >= static_cast<int>( RunCameraMode::Count ) ||
         worldOwner < static_cast<int>( WorldInteractionOwner::None ) ||
         worldOwner > static_cast<int>( WorldInteractionOwner::Manipulator ) || objectType < 0 ||
         objectType >= SkullbonezCore::UI::EditorTab::OBJECT_TYPE_COUNT || activeUiTab < 0 ||
         activeUiTab >= static_cast<int>( SkullbonezCore::UI::InGameUITab::Count ) || developmentUiSurface < 0 ||
         developmentUiSurface > 1 || replayTrack < static_cast<int>( RunReplayTrack::Presentation ) ||
         replayTrack > static_cast<int>( RunReplayTrack::Solver ) ||
         causeMode < static_cast<int>( ReplayCauseInspectionMode::Inactive ) ||
         causeMode > static_cast<int>( ReplayCauseInspectionMode::Returning ) || causeSelectedRow < -1 ||
         causeActiveTab < static_cast<int>( ReplayCauseInspectorTab::Summary ) ||
         causeActiveTab > static_cast<int>( ReplayCauseInspectorTab::Iterations ) || causeSelectedDetailContactRow < -1 ||
         causeSolverDetailFirstRow < 0 || causeRawRecordFirstRow < 0 || causeIterationsFirstRow < 0 ||
         selection.size() >= sizeof( state.recordedBaseline.editorSelectionName ) ||
         pathTarget.size() >= sizeof( state.recordedBaseline.replayPathTargetName ) ||
         !std::isfinite( presentationTrackPosition ) || presentationTrackPosition < 0.0 || presentationTrackPosition > 1.0 ||
         !std::isfinite( solverTrackPosition ) || solverTrackPosition < 0.0 || solverTrackPosition > 1.0 ||
         !std::isfinite( causeEasedProgress ) || causeEasedProgress < 0.0 || causeEasedProgress > 1.0 ||
         !std::isfinite( causeDrawerProgress ) || causeDrawerProgress < 0.0 || causeDrawerProgress > 1.0 ||
         ( causeMode == static_cast<int>( ReplayCauseInspectionMode::Inactive ) && causeSelectedRow != -1 ) ||
         ( causeMode != static_cast<int>( ReplayCauseInspectionMode::Inactive ) &&
           ( causeSelectedRow < 0 || !replay.value( "active", false ) ) ) )
    {
        FailAutomation( state, "recorded manifest baseline values are outside their supported ranges" );
        return false;
    }

    state.recordedBaseline.cameraMode = cameraMode;
    state.recordedBaseline.worldInteractionOwner = worldOwner;
    state.recordedBaseline.editorModeEnabled = tools.value( "editorMode", false );
    state.recordedBaseline.editorPlacementModeEnabled = tools.value( "placementMode", false );
    state.recordedBaseline.editorPlaceStatic = tools.value( "placeStatic", false );
    state.recordedBaseline.editorTerrainAlign = tools.value( "terrainAlign", false );
    state.recordedBaseline.editorObjectType = objectType;
    CopyText( state.recordedBaseline.editorSelectionName, sizeof( state.recordedBaseline.editorSelectionName ), selection );
    state.recordedBaseline.uiVisible = ui.value( "visible", true );
    state.recordedBaseline.uiMinimized = ui.value( "minimized", false );
    state.recordedBaseline.activeUiTab = activeUiTab;
    state.recordedBaseline.developmentUiSurface = developmentUiSurface;
    state.recordedBaseline.replayActive = replay.value( "active", false );
    state.recordedBaseline.replayScrubPaused = replay.value( "scrubPaused", false );
    state.recordedBaseline.replayLiveAdvanceHeld = replay.value( "liveAdvanceHeld", false );
    state.recordedBaseline.replayPredictionEnabled = replay.value( "predictionEnabled", false );
    state.recordedBaseline.replayTrack = replayTrack;
    state.recordedBaseline.replayPresentationTrackPosition = static_cast<float>( presentationTrackPosition );
    state.recordedBaseline.replaySolverTrackPosition = static_cast<float>( solverTrackPosition );
    state.recordedBaseline.replayCauseInspectionMode = causeMode;
    state.recordedBaseline.replayCauseSelectedRow = causeSelectedRow;
    state.recordedBaseline.replayCauseActiveTab = causeActiveTab;
    state.recordedBaseline.replayCauseSelectedDetailContactRow = causeSelectedDetailContactRow;
    state.recordedBaseline.replayCauseSolverDetailFirstRow = causeSolverDetailFirstRow;
    state.recordedBaseline.replayCauseRawRecordFirstRow = causeRawRecordFirstRow;
    state.recordedBaseline.replayCauseIterationsFirstRow = causeIterationsFirstRow;
    state.recordedBaseline.replayCauseSourceFrame = causeSourceFrame;
    state.recordedBaseline.replayCauseTargetFrame = causeTargetFrame;
    state.recordedBaseline.replayCausePresentedFrame = causePresentedFrame;
    state.recordedBaseline.replayCauseDetailVisible = causeInspection.value( "detailVisible", false );
    state.recordedBaseline.replayCauseOwnsPause = causeInspection.value( "ownsPause", false );
    state.recordedBaseline.replayCauseTransportPending = causeInspection.value( "transportPending", false );
    state.recordedBaseline.replayCauseTransportInFlight = causeInspection.value( "transportInFlight", false );
    state.recordedBaseline.replayCauseReturnIssued = causeInspection.value( "returnIssued", false );
    state.recordedBaseline.replayCauseEasedProgress = static_cast<float>( causeEasedProgress );
    state.recordedBaseline.replayCauseDrawerProgress = static_cast<float>( causeDrawerProgress );
    CopyText( state.recordedBaseline.replayPathTargetName, sizeof( state.recordedBaseline.replayPathTargetName ),
              pathTarget );
    state.recordedManifest = true;
    return true;
}

bool LoadScript( InteractionAutomationController& state )
{
    CoreAllocation::RuntimeAllocationScope diagnosticsScope( CoreAllocation::RuntimeAllocationPhase::Diagnostics );
    state.scriptLoaded = true;
    std::ifstream input( state.scriptPath );

    if ( !input.is_open() )
    {
        FailAutomation( state, "failed to open interaction script" );
        return false;
    }

    Json root = Json::parse( input, nullptr, false );

    if ( root.is_discarded() )
    {
        FailAutomation( state, "failed to parse interaction script: invalid JSON" );
        return false;
    }

    const auto format = root.find( "format" );

    if ( format != root.end() && !format->is_string() )
    {
        FailAutomation( state, "interaction script format must be a string" );
        return false;
    }

    if ( format != root.end() && format->get_ref<const std::string&>() == "skullbonez.interaction-recording" )
    {
        return LoadRecordedInteractionManifest( state, root );
    }

    if ( !root.contains( "actions" ) || !root["actions"].is_array() )
    {
        FailAutomation( state, "interaction script requires an actions array" );
        return false;
    }

    const std::size_t actionCount = root["actions"].size();
    state.actions.reserve( actionCount );
    state.reportWriter.ReserveForActions( actionCount );

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

    std::sort( state.actions.begin(), state.actions.end(),
               []( const RunInteractionAutomationAction& lhs, const RunInteractionAutomationAction& rhs )
               { return lhs.frame < rhs.frame; } );

    return true;
}
} // namespace

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
InteractionAutomationDevelopmentUiApplyResult SkullbonezCore::Runtime::ApplyInteractionAutomationDevelopmentUiCommands( const InteractionAutomationController& state, const InteractionAutomationFrameResult& frame, Window& window,
                                                                                                                        DevelopmentTools::ImGuiEditorOwner& editor )
{
    InteractionAutomationDevelopmentUiApplyResult result;

    for ( std::size_t commandIndex = 0u; commandIndex < frame.developmentUiCommandCount; ++commandIndex )
    {
        const InteractionAutomationDevelopmentUiCommand& command = frame.developmentUiCommands[commandIndex];
        SkullbonezCore::Core::SbResult commandStatus = SkullbonezCore::Core::SbResult::Success();

        switch ( command.type )
        {
        case InteractionAutomationDevelopmentUiCommandType::SelectSurface:

            // Process selection remains Run-owned; publish the typed request
            // after interpreting the script command alongside its peers.
            result.selectSurface = true;
            result.surface = std::strcmp( command.target, "imgui" ) == 0 ? DevelopmentUiMode::ImGui
                                                                         : DevelopmentUiMode::GameUI;

            break;
        case InteractionAutomationDevelopmentUiCommandType::SetPanelVisible:
        case InteractionAutomationDevelopmentUiCommandType::FocusPanel:
        {
            DevelopmentTools::ImGuiEditorPanelId panel = DevelopmentTools::ImGuiEditorPanelId::Count;

            if ( !DevelopmentTools::TryParseImGuiEditorPanel( command.target, panel ) )
            {
                commandStatus = state.resultDiagnostics.Failure( "DevelopmentTools/ImGuiAutomation",
                                                                 "Interaction script names an unknown ImGui panel: %s",
                                                                 command.target );

                break;
            }

            DevelopmentTools::ImGuiEditorAutomationCommand editorCommand;
            editorCommand.type = command.type == InteractionAutomationDevelopmentUiCommandType::SetPanelVisible
                                     ? DevelopmentTools::ImGuiEditorAutomationCommandType::SetPanelVisible
                                     : DevelopmentTools::ImGuiEditorAutomationCommandType::FocusPanel;

            editorCommand.panel = panel;
            editorCommand.visible = command.boolValue;
            commandStatus = editor.ApplyAutomationCommand( editorCommand );
            break;
        }
        case InteractionAutomationDevelopmentUiCommandType::ResetLayout:
        {
            DevelopmentTools::ImGuiEditorAutomationCommand editorCommand;
            editorCommand.type = DevelopmentTools::ImGuiEditorAutomationCommandType::ResetLayout;
            commandStatus = editor.ApplyAutomationCommand( editorCommand );
            break;
        }
        case InteractionAutomationDevelopmentUiCommandType::SetDpiScale:
        {
            DevelopmentTools::ImGuiEditorAutomationCommand editorCommand;
            editorCommand.type = DevelopmentTools::ImGuiEditorAutomationCommandType::SetDpiScale;
            editorCommand.dpiScale = command.numberValue;
            commandStatus = editor.ApplyAutomationCommand( editorCommand );
            break;
        }
        case InteractionAutomationDevelopmentUiCommandType::ResizeWindow:
        {
            // Why: scripts describe client pixels because those are the
            // editor's layout coordinates. Win32 resizes the outer frame, so
            // include the current style and monitor DPI exactly once.
            RECT outer { 0, 0, command.width, command.height };

            const HWND nativeWindow = window.NativeWindowHandle();
            const DWORD style = static_cast<DWORD>( GetWindowLongPtr( nativeWindow, GWL_STYLE ) );
            const DWORD extendedStyle = static_cast<DWORD>( GetWindowLongPtr( nativeWindow, GWL_EXSTYLE ) );
            const UINT dpi = GetDpiForWindow( nativeWindow );

            if ( !AdjustWindowRectExForDpi( &outer, style, FALSE, extendedStyle, dpi ) ||
                 !SetWindowPos( nativeWindow, nullptr, 0, 0, outer.right - outer.left, outer.bottom - outer.top,
                                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE ) )
            {
                commandStatus = state.resultDiagnostics.Failure( "DevelopmentTools/ImGuiAutomation",
                                                                 "Failed to resize the automation client area to %dx%d",
                                                                 command.width, command.height );
            }

            break;
        }
        }

        if ( !commandStatus.Ok() )
        {
            result.status = commandStatus;
            break;
        }
    }

    return result;
}

SkullbonezCore::Core::SbResult
InteractionAutomationController::SubmitOperatorEditorReplayCommand( const InteractionAutomationFrameResult& frame,
                                                                    UI::OperatorEditorCommandQueues& commands ) const
{
    if ( !frame.hasOperatorEditorReplayCommand )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    return UI::SubmitOperatorEditorCommand( resultDiagnostics, commands.replay, frame.operatorEditorReplayCommand );
}

SkullbonezCore::Core::SbResult
InteractionAutomationController::SubmitOperatorEditorForecastCommand( const InteractionAutomationFrameResult& frame,
                                                                      UI::OperatorEditorCommandQueues& commands ) const
{
    if ( !frame.hasOperatorEditorForecastCommand )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    return UI::SubmitOperatorEditorCommand( resultDiagnostics, commands.forecast, frame.operatorEditorForecastCommand );
}

InteractionAutomationDevelopmentUiView
InteractionAutomationController::BuildDevelopmentUiView( const DevelopmentTools::ImGuiEditorStatus& editor,
                                                         bool gameUiVisible, bool gameUiReplayPresentationActive ) const
{
    InteractionAutomationDevelopmentUiView view;
    view.available = editor.initialized;
    view.selectedImGui = editor.selectedSurface == DevelopmentUiMode::ImGui;
    view.gameUiVisible = gameUiVisible;
    view.imguiVisible = editor.visible;
    view.gameUiReplayPresentationActive = gameUiReplayPresentationActive;
    view.panelVisibilityMask = editor.panelVisibilityMask;
    view.layoutResetCount = editor.layoutResetCount;
    view.automationFocusCount = editor.automationFocusCount;
    view.appliedDpiScale = editor.appliedDpiScale;
    view.rendererDescriptorHighWater = editor.rendererDescriptorHighWater;
    view.gameViewportRecreations = editor.gameViewportRecreations;
    view.preferencesRecovered = editor.preferencesRecovered;
    return view;
}
#endif

bool TryFindInteractionAutomationModel( const SceneWorld& world, const char* name, int& outIndex )
{
    outIndex = -1;

    if ( !name || name[0] == '\0' )
    {
        return false;
    }

    outIndex = world.Entities().FindByDisplayName( name );
    return outIndex >= 0;
}

bool TryProjectInteractionAutomationModel( const SceneWorld& world, InputRouter& inputRouter, Window* window,
                                           const char* name, POINT& outMouse )
{
    int modelIndex = -1;

    if ( !TryFindInteractionAutomationModel( world, name, modelIndex ) || !window )
    {
        return false;
    }

    const int width = (std::max)( 1, window->ClientWidth() );
    const int height = (std::max)( 1, window->ClientHeight() );
    const int steps[] = { 96, 48, 24, 12, 6 };

    for ( const int step : steps )
    {
        for ( int y = step / 2; y < height; y += step )
        {
            for ( int x = step / 2; x < width; x += step )
            {
                const POINT candidate { static_cast<LONG>( x ), static_cast<LONG>( y ) };

                Vector3 rayOrigin;
                Vector3 rayDirection;

                if ( inputRouter.TryBuildWorldRayAt( candidate, world.Cameras(), *window, rayOrigin, rayDirection ) )
                {
                    RuntimePickRequest request;
                    request.purpose = RuntimePickPurpose::EditorSelection;
                    request.bodyStore = &world.BodyStore();
                    request.colliderStore = &world.Colliders();
                    request.rayOrigin = rayOrigin;
                    request.rayDirection = rayDirection;

                    RuntimePickResult result;

                    if ( RuntimePickService::TryPickModel( request, result ) && result.modelRow.value == modelIndex )
                    {
                        outMouse = candidate;
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

void SkullbonezCore::Runtime::ClearInteractionAutomationInput( InteractionAutomationController& state )
{
    state.inputDriver.Reset();
    state.reportWriter.ResetEditorSelectionCaptures();
}


SkullbonezCore::Core::SbResult
SkullbonezCore::Runtime::ConfigureInteractionAutomation( InteractionAutomationController& state, const char* scriptPath,
                                                         const char* reportPath, const char* tracePath )
{
    // Configure can be called again while applying startup options. Reset the
    // sequencer in place because its report writer owns store-bound tracer
    // storage and is intentionally not assignable.
    state.enabled = false;
    state.scriptLoaded = false;
    state.finished = false;
    state.recordedManifest = false;
    state.recordedBaselineApplied = false;
    state.recordedFramePublished = false;
    state.recordedTurn = 0u;
    state.traceTurn = 0u;
    state.recordedDeltaSeconds = 0.0;
    state.scriptPath[0] = '\0';
    state.tracePath[0] = '\0';
    state.traceOutput.close();
    state.actions.clear();
    state.recordedFrames.clear();
    state.recordedBaseline = {};
    state.status = {};
    state.inputDriver.Reset();
    state.reportWriter.Configure( reportPath, scriptPath );

    if ( !scriptPath || scriptPath[0] == '\0' )
    {
        state.finished = true;
        state.status.Fail( "interaction automation requires a script path" );
        return state.status.Result( state.resultDiagnostics );
    }

    strcpy_s( state.scriptPath, sizeof( state.scriptPath ), scriptPath );

    if ( tracePath && tracePath[0] != '\0' )
    {
        strcpy_s( state.tracePath, sizeof( state.tracePath ), tracePath );

        if ( !RuntimeFileWriter::OpenTextFile( state.tracePath, state.traceOutput ) )
        {
            state.finished = true;
            state.status.Fail( "interaction turn trace could not be opened" );
            return state.status.Result( state.resultDiagnostics );
        }

        state.traceOutput << Json( { { "type", "header" },
                                     { "schema", "skullbonez.interaction-trace" },
                                     { "version", 1 },
                                     { "script", state.scriptPath } } )
                                 .dump()
                          << '\n';
        state.traceOutput.flush();

        if ( !state.traceOutput.good() )
        {
            state.finished = true;
            state.status.Fail( "interaction turn trace header could not be written" );
            return state.status.Result( state.resultDiagnostics );
        }
    }

    state.enabled = true;
    printf( "[interaction] Script: %s\n", state.scriptPath );
    printf( "[interaction] Report: %s\n", state.reportWriter.Path() );
    printf( "[interaction] Trace: %s\n", state.tracePath[0] != '\0' ? state.tracePath : "disabled" );
    return SkullbonezCore::Core::SbResult::Success();
}


SkullbonezCore::Core::SbResult
SkullbonezCore::Runtime::InteractionAutomationResult( const InteractionAutomationController& state )
{
    return state.status.Result( state.resultDiagnostics );
}

InteractionAutomationFrameResult SkullbonezCore::Runtime::TickInteractionAutomationBeforeInput( InteractionAutomationController& state, Window& windowOwner, const SkullbonezCore::Core::EngineConfig& config,
                                                                                                SceneController& scene, const RuntimeFrameMetricsSnapshot& timers, CameraControlState& camera, InputRouter& inputRouter,
                                                                                                RuntimeInteractionController& interaction, EditorToolsOwner& editorTools, RuntimeTools& runtimeTools,
                                                                                                SkullbonezCore::UI::InGameUI& ui, const ReplayAutomationView& replayView,
                                                                                                const Rendering::RenderSceneSnapshot& renderSnapshot )
{
    Window* window = &windowOwner;
    InteractionAutomationFrameResult result;

    if ( !state.enabled || state.finished )
    {
        return result;
    }

    const ReplayInputView replayInput = replayView.input;

    if ( !state.scriptLoaded && !LoadScript( state ) )
    {
        state.finished = true;
        ClearInteractionAutomationInput( state );

        // Why: latch the automation-owned diagnostic before WM_QUIT. The
        // report writer is another recoverable boundary, so it also cannot replace
        // the earlier script failure if both operations fail.
        result.status = InteractionAutomationResult( state );
        const SkullbonezCore::Core::SbResult reportResult = state.reportWriter.Write( state.status, scene.Scene(),
                                                                                      scene.State(),
                                                                                      scene.CurrentPath()
                                                                                          ? scene.CurrentPath()->c_str()
                                                                                          : nullptr,
                                                                                      editorTools, runtimeTools, replayView,
                                                                                      interaction, camera, ui,
                                                                                      renderSnapshot );

        if ( result.status.Ok() )
        {
            result.status = reportResult;
        }

        result.requestQuit = true;
        return result;
    }

    if ( state.recordedManifest )
    {
        if ( !state.recordedBaselineApplied )
        {
            const InteractionRecordingBaseline& baseline = state.recordedBaseline;

            if ( baseline.cameraMode < 0 || baseline.cameraMode >= static_cast<int>( RunCameraMode::Count ) ||
                 baseline.worldInteractionOwner < static_cast<int>( WorldInteractionOwner::None ) ||
                 baseline.worldInteractionOwner > static_cast<int>( WorldInteractionOwner::Manipulator ) ||
                 baseline.activeUiTab < 0 || baseline.activeUiTab >= static_cast<int>( UI::InGameUITab::Count ) )
            {
                FailAutomation( state, "recorded manifest baseline contains an invalid enum value" );
                result.status = InteractionAutomationResult( state );
                result.requestQuit = true;
                return result;
            }

            result.applyCameraMode = true;
            result.cameraMode = static_cast<RunCameraMode>( baseline.cameraMode );
            result.setWorldInteractionOwner = true;
            result.worldInteractionOwner = baseline.worldInteractionOwner;
            result.worldInteractionReason = static_cast<int>( InteractionExitReason::EnterReplay );
            editorTools.Editor().editorModeEnabled = baseline.editorModeEnabled;
            editorTools.Editor().placementModeEnabled = baseline.editorModeEnabled && baseline.editorPlacementModeEnabled;
            editorTools.Editor().placeStaticObject = baseline.editorPlaceStatic;
            editorTools.Editor().autoTerrainAlign = baseline.editorTerrainAlign;
            editorTools.Editor().objectType = baseline.editorObjectType;

            if ( baseline.editorSelectionName[0] != '\0' )
            {
                int selectedModel = -1;

                if ( !TryFindInteractionAutomationModel( scene.Scene(), baseline.editorSelectionName, selectedModel ) )
                {
                    FailAutomation( state, "recorded manifest editor selection could not be restored" );
                    result.status = InteractionAutomationResult( state );
                    result.requestQuit = true;
                    return result;
                }

                const Physics::PhysicsBodyRecord* selectedBody = scene.Scene().BodyStore().RecordForModelIndex( selectedModel );
                const Physics::PhysicsColliderHandle selectedCollider = scene.Scene().Colliders().HandleForModelIndex( selectedModel );

                if ( !selectedBody || !selectedBody->handle.IsValid() || !selectedCollider.IsValid() )
                {
                    FailAutomation( state, "recorded manifest editor selection identities are unavailable" );
                    result.status = InteractionAutomationResult( state );
                    result.requestQuit = true;
                    return result;
                }

                editorTools.Editor().selectedModelRow.value = selectedModel;
                editorTools.Editor().selectedBody = selectedBody->handle;
                editorTools.Editor().selectedCollider = selectedCollider;
            }

            ui.SetVisible( baseline.uiVisible, timers.simulationTotalSeconds );
            ui.SetMinimized( baseline.uiMinimized, timers.simulationTotalSeconds );
            ui.SetActiveTab( static_cast<UI::InGameUITab>( baseline.activeUiTab ) );
            result.replayIntent.setPredictionEnabled = true;
            result.replayIntent.predictionEnabled = baseline.replayPredictionEnabled;
            result.restoreRecordedReplayBaseline = baseline.replayActive;
            result.recordedReplayScrubPaused = baseline.replayScrubPaused;
            result.recordedReplayLiveAdvanceHeld = baseline.replayLiveAdvanceHeld;
            result.recordedReplayTrack = static_cast<RunReplayTrack>( baseline.replayTrack );
            result.recordedReplayPresentationTrackPosition = baseline.replayPresentationTrackPosition;
            result.recordedReplaySolverTrackPosition = baseline.replaySolverTrackPosition;
            result.restoreRecordedReplayCauseBaseline = baseline.replayActive &&
                                                        baseline.replayCauseInspectionMode !=
                                                            static_cast<int>( ReplayCauseInspectionMode::Inactive );
            result.recordedReplayCauseBaseline = baseline;

            if ( baseline.replayPathTargetName[0] != '\0' )
            {
                int modelIndex = -1;

                if ( TryFindInteractionAutomationModel( scene.Scene(), baseline.replayPathTargetName, modelIndex ) )
                {
                    const Physics::PhysicsBodyRecord* body = scene.Scene().BodyStore().RecordForModelIndex( modelIndex );

                    if ( body && body->sceneObjectId.IsValid() )
                    {
                        result.replayIntent.setPathTarget = true;
                        result.replayIntent.pathTargetId = body->sceneObjectId;
                        result.replayIntent.pathTargetModelRow.value = modelIndex;
                        strncpy_s( result.replayIntent.pathTargetName, sizeof( result.replayIntent.pathTargetName ),
                                   baseline.replayPathTargetName, _TRUNCATE );
                    }
                }
            }

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )

            if ( result.developmentUiCommandCount < result.developmentUiCommands.size() )
            {
                InteractionAutomationDevelopmentUiCommand&
                    command = result.developmentUiCommands[result.developmentUiCommandCount++];
                command.type = InteractionAutomationDevelopmentUiCommandType::SelectSurface;
                strcpy_s( command.target,
                          baseline.developmentUiSurface == static_cast<int>( DevelopmentUiMode::ImGui ) ? "imgui" : "game" );
            }
#endif
            state.recordedBaselineApplied = true;
        }

        if ( state.recordedTurn >= state.recordedFrames.size() )
        {
            // A zero-turn prefix still has a meaningful restored baseline. Run
            // one neutral synthetic frame so returned camera/replay/ownership
            // commands apply before the after-render report observes them.
            RecordedInputFrame neutralFrame;
            neutralFrame.appFocused = false;
            result.recordedCursor = state.inputDriver.PublishRecordedFrame( neutralFrame, windowOwner.ClientWidth(),
                                                                            windowOwner.ClientHeight(), false );
            state.recordedDeltaSeconds = 0.0;
            state.recordedFramePublished = true;
            result.hasRecordedDeltaSeconds = true;
            result.recordedDeltaSeconds = 0.0;
            return result;
        }

        const RecordedInputFrame& recorded = state.recordedFrames[static_cast<std::size_t>( state.recordedTurn )];
        POINT semanticPosition = {};
        int semanticX = 0;
        int semanticY = 0;
        const bool resolvedSemantic = recorded.hasPointer && recorded.semanticAnchor[0] != '\0' &&
                                      ui.ResolveInteractionAnchor( recorded.semanticAnchor, semanticX, semanticY );
        semanticPosition.x = semanticX;
        semanticPosition.y = semanticY;
        result.recordedCursor = state.inputDriver.PublishRecordedFrame( recorded, windowOwner.ClientWidth(),
                                                                        windowOwner.ClientHeight(), true,
                                                                        resolvedSemantic ? &semanticPosition : nullptr );
        state.recordedDeltaSeconds = recorded.deltaSeconds;
        state.recordedFramePublished = true;
        result.hasRecordedDeltaSeconds = true;
        result.recordedDeltaSeconds = recorded.deltaSeconds;
        return result;
    }

    const int frame = scene.State().currentFrame;

    if ( state.reportWriter.ReplayVisualCaptureEnabled() )
    {
        // Invariant: the mega probe is one presented cascade. Advancing the
        // authoritative scene after the reveal would show a second, unrelated
        // wall fall and make a visually broken run appear to be test coverage.
        ReplayFrameIndex revealFrame = 0;
        bool resetReveal = false;

        if ( state.reportWriter.UpdateReplayVisualReveal( frame, REPLAY_VISUAL_FIDELITY_START_FRAME,
                                                          replayInput.liveAdvanceHeld,
                                                          ReplayDeterministicRevealReady( replayView ), state.status,
                                                          revealFrame, resetReveal ) )
        {
            PublishReplayDeterministicReveal( result.replayIntent, revealFrame, resetReveal );
        }
    }

    state.inputDriver.AdvanceReleases( frame );

    for ( RunInteractionAutomationAction& action : state.actions )
    {
        if ( action.processed || action.frame != frame )
        {
            continue;
        }

        switch ( action.type )
        {
        case RunInteractionAutomationActionType::DirectorPlay:
        case RunInteractionAutomationActionType::SetCameraMode:
        {
            const RunCameraMode targetMode = action.type == RunInteractionAutomationActionType::DirectorPlay
                                                 ? ( action.boolValue ? RunCameraMode::Director : RunCameraMode::Inspect )
                                                 : action.cameraMode;

            result.applyCameraMode = true;
            result.cameraMode = targetMode;
            const bool applied = true;

            if ( !applied )
            {
                FailAutomation( state, "failed to apply automated camera mode" );
            }

            AppendReportAction( state, frame, action.type, action.text, nullptr, applied,
                                applied ? "camera mode applied" : "camera mode failed" );

            action.processed = true;
            break;
        }
        case RunInteractionAutomationActionType::LoadShotList:
        case RunInteractionAutomationActionType::DirectorAdvance:
        case RunInteractionAutomationActionType::DirectorGrab:
        case RunInteractionAutomationActionType::DirectorRelease:
        case RunInteractionAutomationActionType::SetPhaseStyle:
        case RunInteractionAutomationActionType::SetCameraPose:
            ApplyInteractionAutomationDirectorCameraAction( state, scene.Scene().Cameras(), camera, action, result, frame );
            action.processed = true;
            break;
        case RunInteractionAutomationActionType::ShowReplayScrubber:
        case RunInteractionAutomationActionType::SetReplayPredictionEnabled:
        case RunInteractionAutomationActionType::SetReplayPathTarget:
        case RunInteractionAutomationActionType::SetReplayInterceptTarget:
        case RunInteractionAutomationActionType::SetReplayTripPlannerCommand:
        case RunInteractionAutomationActionType::SetReplayPredictionHorizonSeconds:
        case RunInteractionAutomationActionType::NudgeReplayPathTargetVelocity:
            ApplyInteractionAutomationReplayStateAction( state, timers, result.replayIntent, replayView, scene.Scene().Physics(), action, frame,
                                                         [&]( const char* name )
                                                         {
                                                             int modelIndex = -1;

                                                             if ( !TryFindInteractionAutomationModel( scene.Scene(), name, modelIndex ) )
                                                             {
                                                                 return false;
                                                             }

                                                             const Physics::PhysicsBodyRecord* body = scene.Scene().BodyStore().RecordForModelIndex( modelIndex );

                                                             if ( !body || !body->sceneObjectId.IsValid() )
                                                             {
                                                                 return false;
                                                             }

                                                             result.replayIntent.setPathTarget = true;
                                                             result.replayIntent.pathTargetId = body->sceneObjectId;
                                                             result.replayIntent.pathTargetModelRow.value = modelIndex;
                                                             strncpy_s( result.replayIntent.pathTargetName, sizeof( result.replayIntent.pathTargetName ), name,
                                                                        _TRUNCATE );

                                                             return true;
                                                         },
                                                         [&]( const char* name )
                                                         {
                                                             int modelIndex = -1;

                                                             if ( !TryFindInteractionAutomationModel( scene.Scene(), name, modelIndex ) )
                                                             {
                                                                 return false;
                                                             }

                                                             const Physics::PhysicsBodyRecord* body = scene.Scene().BodyStore().RecordForModelIndex( modelIndex );

                                                             if ( !body || !body->sceneObjectId.IsValid() )
                                                             {
                                                                 return false;
                                                             }

                                                             result.replayIntent.setInterceptTarget = true;
                                                             result.replayIntent.interceptTargetId = body->sceneObjectId;
                                                             result.replayIntent.interceptTargetModelRow.value = modelIndex;
                                                             return true;
                                                         },
                                                         [&]( WorldInteractionOwner owner, InteractionExitReason reason )
                                                         {
                                                             result.setWorldInteractionOwner = true;
                                                             result.worldInteractionOwner = static_cast<int>( owner );
                                                             result.worldInteractionReason = static_cast<int>( reason );
                                                         } );
            action.processed = true;
            break;
        case RunInteractionAutomationActionType::BeginReplayVisualFidelityCapture:
        {
            state.reportWriter.BeginReplayVisualCapture( static_cast<std::size_t>( REPLAY_FUTURE_DEFAULT_SECONDS / PHYSICS_FIXED_DT ) + 2u );

            // Invariant: the script arms this hold before target/horizon setup
            // and the sole Predict click. Letting wall-clock reveal run first
            // would retain markers, then rewinding to zero would create a
            // broken second presentation pass.
            PublishReplayDeterministicReveal( result.replayIntent, 0, true );

            // Invariant: a deterministic visual probe pins its own pacing rather
            // than inheriting the operator-facing default. That default is now
            // effectively immediate, which makes retained trails and trajectory
            // lines snap instead of animate, so a probe that inherited it would
            // capture a different picture than the one its goldens approve.
            // Real-time here reproduces the pacing the goldens were captured at.
            result.replayIntent.applyPredictionRevealRate = true;
            result.replayIntent.predictionRevealRate = REPLAY_VISUAL_FIDELITY_REVEAL_RATE;
            AppendReportAction( state, frame, action.type, "prediction", nullptr, true,
                                "reveal held at zero; frame-exact capture starts after prediction publication" );

            action.processed = true;
            break;
        }
        case RunInteractionAutomationActionType::PressKey:

            // Why: key automation should still enter through Input and
            // RuntimeInputContext edge detection. This only supplies the
            // virtual-key state that a real keyboard would have provided.
            state.inputDriver.PressKey( action.keyVirtualKey, action.boolValue, frame, action.holdFrames );
            AppendReportAction( state, frame, action.type, action.text, nullptr, true, "key press injected" );
            action.processed = true;
            break;
        case RunInteractionAutomationActionType::CaptureEditorSelectionState:
        {
            const int slot = static_cast<int>( action.numberValue );
            const EditorSelectionFingerprint fingerprint = BuildEditorSelectionFingerprint( editorTools, scene.Scene() );

            state.reportWriter.CaptureEditorSelection( slot, fingerprint.hash, fingerprint.valid );

            if ( !fingerprint.valid )
            {
                FailAutomation( state, "failed to capture editor selection state" );
            }

            char detail[128] = {};

            sprintf_s( detail, sizeof( detail ), "slot=%d fingerprint=%s terrain=%d", slot,
                       InteractionAutomationReportWriter::FormatPredictionHash( fingerprint.hash ).c_str(),
                       fingerprint.hasTerrain ? 1 : 0 );

            AppendReportAction( state, frame, action.type, "selection", nullptr, fingerprint.valid, detail );
            action.processed = true;
            break;
        }
        case RunInteractionAutomationActionType::LoadScene:
        {
            const std::vector<std::string>& browserPaths = ui.SceneNavigation().browser.paths;
            int browserIndex = -1;

            for ( int index = 0; index < static_cast<int>( browserPaths.size() ); ++index )
            {
                if ( browserPaths[static_cast<std::size_t>( index )] == action.path )
                {
                    browserIndex = index;
                    break;
                }
            }

            const bool found = browserIndex >= 0;

            if ( found )
            {
                // Why: automation submits the same fixed scene-owner request as
                // the browser. The load executes at the normal post-input
                // checkpoint and cannot retain this cold script action.
                scene.SubmitLoadBrowserIndex( browserIndex );
            }
            else
            {
                FailAutomation( state, "automated scene path was not found in the scene browser" );
            }

            AppendReportAction( state, frame, action.type, action.path, nullptr, found,
                                found ? "scene load submitted" : "scene path not found" );

            action.processed = true;
            break;
        }
        case RunInteractionAutomationActionType::SetDevelopmentUiSurface:
        case RunInteractionAutomationActionType::SetImGuiPanelVisible:
        case RunInteractionAutomationActionType::ResetImGuiLayout:
        case RunInteractionAutomationActionType::FocusImGuiPanel:
        case RunInteractionAutomationActionType::SetImGuiDpiScale:
        case RunInteractionAutomationActionType::ResizeWindow:
        {
            const bool hasCapacity = result.developmentUiCommandCount < result.developmentUiCommands.size();
            bool duplicateSurfaceSelection = false;

            if ( action.type == RunInteractionAutomationActionType::SetDevelopmentUiSurface )
            {
                for ( std::size_t commandIndex = 0u; commandIndex < result.developmentUiCommandCount; ++commandIndex )
                {
                    duplicateSurfaceSelection = result.developmentUiCommands[commandIndex].type ==
                                                InteractionAutomationDevelopmentUiCommandType::SelectSurface;

                    if ( duplicateSurfaceSelection )
                    {
                        break;
                    }
                }
            }

            const bool published = hasCapacity && !duplicateSurfaceSelection;

            if ( published )
            {
                InteractionAutomationDevelopmentUiCommand&
                    command = result.developmentUiCommands[result.developmentUiCommandCount++];

                command.boolValue = action.boolValue;
                command.numberValue = action.numberValue;
                strcpy_s( command.target, action.text );

                switch ( action.type )
                {
                case RunInteractionAutomationActionType::SetDevelopmentUiSurface:
                    command.type = InteractionAutomationDevelopmentUiCommandType::SelectSurface;
                    break;
                case RunInteractionAutomationActionType::SetImGuiPanelVisible:
                    command.type = InteractionAutomationDevelopmentUiCommandType::SetPanelVisible;
                    break;
                case RunInteractionAutomationActionType::ResetImGuiLayout:
                    command.type = InteractionAutomationDevelopmentUiCommandType::ResetLayout;
                    break;
                case RunInteractionAutomationActionType::FocusImGuiPanel:
                    command.type = InteractionAutomationDevelopmentUiCommandType::FocusPanel;
                    break;
                case RunInteractionAutomationActionType::SetImGuiDpiScale:
                    command.type = InteractionAutomationDevelopmentUiCommandType::SetDpiScale;
                    break;
                case RunInteractionAutomationActionType::ResizeWindow:
                    command.type = InteractionAutomationDevelopmentUiCommandType::ResizeWindow;
                    command.width = static_cast<int>( action.mouse.x );
                    command.height = static_cast<int>( action.mouse.y );
                    break;
                default:
                    break;
                }
            }
            else if ( duplicateSurfaceSelection )
            {
                // Invariant: Run receives at most one process-surface request
                // per frame, so selection is never silently collapsed or reordered.
                FailAutomation( state, "multiple development UI surface selections share one frame" );
            }
            else
            {
                FailAutomation( state, "development UI automation command capacity exceeded" );
            }

            AppendReportAction( state, frame, action.type, action.text, nullptr, published,
                                published ? "development UI command published"
                                          : ( duplicateSurfaceSelection ? "duplicate frame surface selection"
                                                                        : "command capacity exceeded" ) );

            action.processed = true;
            break;
        }
        case RunInteractionAutomationActionType::MoveMouse:
        {
            POINT mousePos = action.mouse;

            if ( action.boolValue && ( action.vectorValue.x > 0.0f || action.vectorValue.y > 0.0f ) )
            {
                const int screenW = window ? window->ClientWidth() : config.window.screenX;
                const int screenH = window ? window->ClientHeight() : config.window.screenY;

                if ( screenW > 0 && screenH > 0 )
                {
                    mousePos.x = static_cast<long>( action.vectorValue.x * static_cast<float>( screenW ) );
                    mousePos.y = static_cast<long>( action.vectorValue.y * static_cast<float>( screenH ) );
                }
            }

            state.inputDriver.MoveMouse( mousePos );
            AppendReportAction( state, frame, action.type, nullptr, &mousePos, true, "mouse move injected" );
            action.processed = true;
            break;
        }
        case RunInteractionAutomationActionType::ClickReplayControl:
            ApplyInteractionAutomationReplayControlClick( state, window, config, scene.State(), timers, result.replayIntent,
                                                          replayView, action, frame );

            action.processed = true;
            break;
        case RunInteractionAutomationActionType::ScrubReplaySolverTrack:
            ApplyInteractionAutomationSolverTrackScrub( state, window, config, timers, result.replayIntent, replayView,
                                                        action, frame );

            action.processed = true;
            break;
        case RunInteractionAutomationActionType::ScrubEditorReplayTrack:
        {
            const bool available = replayView.solverStats.enabled && replayView.solverStats.sampleCount >= 2;

            if ( available && !result.hasOperatorEditorReplayCommand )
            {
                result.hasOperatorEditorReplayCommand = true;
                result.operatorEditorReplayCommand.type = SkullbonezCore::UI::OperatorEditorReplayCommandType::Scrub;
                result.operatorEditorReplayCommand.value = action.numberValue;
            }
            else
            {
                FailAutomation( state, available ? "editor replay automation command capacity exceeded"
                                                 : "editor replay scrub track unavailable" );
            }

            AppendReportAction( state, frame, action.type, "shared replay queue", nullptr,
                                available && result.hasOperatorEditorReplayCommand,
                                available ? "typed editor replay scrub published" : "editor replay track unavailable" );

            action.processed = true;
            break;
        }
        case RunInteractionAutomationActionType::SetContinuousForecastCommand:
        {
            const bool available = !result.hasOperatorEditorForecastCommand;

            if ( available )
            {
                result.hasOperatorEditorForecastCommand = true;
                const std::string_view command( action.text );
                result.operatorEditorForecastCommand.type = command == "reset" ? UI::OperatorEditorForecastCommandType::Reset
                                                            : command == "exit"
                                                                ? UI::OperatorEditorForecastCommandType::Exit
                                                                : UI::OperatorEditorForecastCommandType::ToggleContinuous;
            }
            else
            {
                FailAutomation( state, "continuous forecast automation command capacity exceeded" );
            }

            AppendReportAction( state, frame, action.type, action.text, nullptr, available,
                                available ? "typed continuous forecast command published" : "command capacity exceeded" );
            action.processed = true;
            break;
        }
        case RunInteractionAutomationActionType::ClickObject:
        {
            POINT mouse = {};

            const bool projected = TryProjectInteractionAutomationModel( scene.Scene(), inputRouter, window, action.text,
                                                                         mouse );

            if ( projected )
            {
                state.inputDriver.MoveMouse( mouse );
                action.mouse = mouse;
                action.hasMouse = true;
                state.inputDriver.PressMouse( action.button == RunInteractionAutomationButton::Right, frame,
                                              action.holdFrames );
            }
            else
            {
                FailAutomation( state, "failed to project interaction target" );
            }

            AppendReportAction( state, frame, action.type, action.text, projected ? &mouse : nullptr, projected,
                                projected ? "mouse press injected" : "target projection failed" );

            action.processed = true;
            break;
        }
        case RunInteractionAutomationActionType::ClickPoint:
        {
            POINT mousePos = action.mouse;

            if ( action.boolValue && ( action.vectorValue.x > 0.0f || action.vectorValue.y > 0.0f ) )
            {
                const int screenW = window ? window->ClientWidth() : config.window.screenX;
                const int screenH = window ? window->ClientHeight() : config.window.screenY;

                if ( screenW > 0 && screenH > 0 )
                {
                    mousePos.x = static_cast<long>( action.vectorValue.x * static_cast<float>( screenW ) );
                    mousePos.y = static_cast<long>( action.vectorValue.y * static_cast<float>( screenH ) );
                }
            }

            state.inputDriver.MoveMouse( mousePos );
            state.inputDriver.PressMouse( action.button == RunInteractionAutomationButton::Right, frame, action.holdFrames );

            AppendReportAction( state, frame, action.type, nullptr, &mousePos, true, "mouse press injected" );
            action.processed = true;
            break;
        }
        case RunInteractionAutomationActionType::ScrollPoint:
        {
            POINT mousePos = action.mouse;

            if ( action.boolValue && ( action.vectorValue.x > 0.0f || action.vectorValue.y > 0.0f ) )
            {
                const int screenW = window ? window->ClientWidth() : config.window.screenX;
                const int screenH = window ? window->ClientHeight() : config.window.screenY;

                if ( screenW > 0 && screenH > 0 )
                {
                    mousePos.x = static_cast<long>( action.vectorValue.x * static_cast<float>( screenW ) );
                    mousePos.y = static_cast<long>( action.vectorValue.y * static_cast<float>( screenH ) );
                }
            }

            state.inputDriver.MoveMouse( mousePos );
            state.inputDriver.ScrollMouse( action.integerValue );
            AppendReportAction( state, frame, action.type, nullptr, &mousePos, true, "mouse wheel injected" );
            action.processed = true;
            break;
        }
        case RunInteractionAutomationActionType::SelectReplayCauseRow:
        {
            const bool available = action.integerValue >= 0 &&
                                   action.integerValue < static_cast<int>( replayView.causeTree.rows.size() );

            if ( available )
            {
                result.requestedReplayCauseRow = action.integerValue;
            }
            else
            {
                FailAutomation( state, "requested replay cause row is unavailable" );
            }

            char row[32] = {};
            sprintf_s( row, sizeof( row ), "%d", action.integerValue );
            AppendReportAction( state, frame, action.type, row, nullptr, available,
                                available ? "cause row intent published" : "cause row unavailable" );
            action.processed = true;
            break;
        }
        case RunInteractionAutomationActionType::LoseFocus:
            state.inputDriver.LoseFocus( action.holdFrames );
            AppendReportAction( state, frame, action.type, "input", nullptr, true, "focus loss injected" );
            action.processed = true;
            break;
        case RunInteractionAutomationActionType::AssertState:
        case RunInteractionAutomationActionType::Screenshot:
            break;
        }
    }

    state.inputDriver.PublishFrame();
    return result;
}

InteractionAutomationFrameResult SkullbonezCore::Runtime::TickInteractionAutomationAfterRender( InteractionAutomationController& state, EditorToolsOwner& editorTools, RuntimeTools& runtimeTools,
                                                                                                RuntimeInteractionController& interaction, InputRouter& inputRouter, CameraControlState& camera,
                                                                                                SkullbonezCore::UI::InGameUI& ui, SceneController& scene, const ReplayAutomationView& replayView,
                                                                                                const InteractionAutomationDevelopmentUiView& developmentUiView, const ContinuousOrbitalForecastView& forecastView,
                                                                                                const Rendering::RenderSceneSnapshot& renderSnapshot, CaptureController& capture,
                                                                                                Rendering::Dx12BackbufferCapture& backbufferCapture )
{
    InteractionAutomationFrameResult result;

    if ( !state.enabled || state.finished )
    {
        return result;
    }

    if ( !WriteInteractionTraceTurn( state, inputRouter, camera, ui, scene ) )
    {
        FailAutomation( state, "interaction turn trace write failed" );
        state.finished = true;
        ClearInteractionAutomationInput( state );
        result.status = InteractionAutomationResult( state );
        result.requestQuit = true;
        return result;
    }

    ++state.traceTurn;

    if ( state.recordedManifest )
    {
        if ( !state.recordedFramePublished )
        {
            return result;
        }

        state.recordedFramePublished = false;
        ++state.recordedTurn;

        if ( state.recordedTurn < state.recordedFrames.size() )
        {
            return result;
        }

        state.finished = true;
        ClearInteractionAutomationInput( state );
        result.status = InteractionAutomationResult( state );
        const SkullbonezCore::Core::SbResult reportResult = state.reportWriter.Write( state.status, scene.Scene(),
                                                                                      scene.State(),
                                                                                      scene.CurrentPath()
                                                                                          ? scene.CurrentPath()->c_str()
                                                                                          : nullptr,
                                                                                      editorTools, runtimeTools, replayView,
                                                                                      interaction, camera, ui,
                                                                                      renderSnapshot );

        if ( result.status.Ok() )
        {
            result.status = reportResult;
        }

        result.requestQuit = true;
        return result;
    }

    CoreAllocation::RuntimeAllocationScope diagnosticsAllocationScope( CoreAllocation::RuntimeAllocationPhase::Diagnostics );
    const int frame = scene.State().currentFrame;

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
                const SkullbonezCore::Core::SbResult captureResult = capture.SaveScreenshot( backbufferCapture,
                                                                                             action.path );

                if ( captureResult.Ok() )
                {
                    state.reportWriter.AddScreenshot( action.path );
                    AppendReportAction( state, frame, action.type, action.path, nullptr, true, "screenshot saved" );
                }
                else
                {
                    const char* message = captureResult.ErrorMessage()[0] != '\0' ? captureResult.ErrorMessage()
                                                                                  : "screenshot capture failed";

                    FailAutomation( state, message );
                    AppendReportAction( state, frame, action.type, action.path, nullptr, false, message );
                }
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

        const InteractionAutomationAssertionEvaluation evaluation = EvaluateInteractionAutomationAssertion( editorTools, runtimeTools, state, replayView, interaction, inputRouter, camera, scene.Scene(), ui,
                                                                                                            developmentUiView, forecastView, renderSnapshot, action,
                                                                                                            [&]() { return editorTools.InspectGizmoInteractionActive( camera.mode, replayView.input.inspectionActive ); } );

        strcpy_s( assertion.expected, sizeof( assertion.expected ), evaluation.expected.c_str() );
        strcpy_s( assertion.actual, sizeof( assertion.actual ), evaluation.actual.c_str() );
        assertion.passed = evaluation.passed;
        state.reportWriter.AppendAssertion( assertion );

        if ( !evaluation.passed )
        {
            char message[256] = {};
            sprintf_s( message, sizeof( message ), "interaction assertion failed: %s expected=%s actual=%s", assertion.name,
                       assertion.expected, assertion.actual );

            FailAutomation( state, message );
        }

        action.processed = true;
    }

    if ( !state.reportWriter.CaptureReplayVisualFrame( frame, replayView, state.status ) )
    {
        return result;
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
        // This command runs after the final reveal screenshot while live
        // physics still holds the seed pose used by root markers. The writer
        // owns the CPU-only proof and cannot initiate a second presented pass.
        if ( !state.status.failed &&
             !state.reportWriter.FinishReplayVisualCapture( state.status, runtimeTools, scene.Scene(), replayView ) )
        {
            ClearInteractionAutomationInput( state );
            return result;
        }

        if ( !state.status.failed && replayView.prediction.build.building )
        {
            // Why: prediction reports read committed topology, frame counts,
            // and trajectory hashes. Let the normal render-frame replay path
            // finish its worker swap/rebuild instead of draining physics under
            // the post-draw automation profiler scope.
            ClearInteractionAutomationInput( state );
            return result;
        }

        state.finished = true;
        ClearInteractionAutomationInput( state );

        // Invariant: assertion failure retains precedence over report IO.
        result.status = InteractionAutomationResult( state );
        const SkullbonezCore::Core::SbResult reportResult = state.reportWriter.Write( state.status, scene.Scene(),
                                                                                      scene.State(),
                                                                                      scene.CurrentPath()
                                                                                          ? scene.CurrentPath()->c_str()
                                                                                          : nullptr,
                                                                                      editorTools, runtimeTools, replayView,
                                                                                      interaction, camera, ui,
                                                                                      renderSnapshot );

        if ( result.status.Ok() )
        {
            result.status = reportResult;
        }

        result.requestQuit = true;
    }

    return result;
}


bool SkullbonezCore::Runtime::InteractionAutomationWillCaptureAfterRender( const InteractionAutomationController& state,
                                                                           int frame )
{
    if ( !state.enabled || state.finished )
    {
        return false;
    }

    for ( const RunInteractionAutomationAction& action : state.actions )
    {
        if ( !action.processed && action.frame == frame && action.type == RunInteractionAutomationActionType::Screenshot )
        {
            return true;
        }
    }

    return false;
}
