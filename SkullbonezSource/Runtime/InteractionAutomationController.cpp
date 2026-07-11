/*
File: SkullbonezSource/Runtime/InteractionAutomationController.cpp
Purpose:
  Drives deterministic runtime interaction scripts through the normal input path.

Mental model:
  Interaction automation is a validation driver. It asks the same picking,
  replay, camera, director-shot, and world-input code that an operator would
  use, then writes a compact JSON report for the test harness.

Glossary:
  World click: Automation request that projects a screen-space click into the
  scene and routes it through the active runtime owner.
  Director shot action: Automation request that loads, plays, grabs, advances,
    or retargets a fixed camera shot list without taking ownership away from
    the runtime camera state.
  Prediction target: Replay body selected for future-path diagnostics.
  Automation report: JSON side-channel describing what the scripted interaction
  observed without mutating validation baselines directly.
  Probe failure: CLI validation failure persisted as report `ok=false` and
    returned to the process boundary after the frame loop exits.

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
#include "InteractionAutomationController.h"
#include "AttachedCameraController.h"
#include "CaptureController.h"
#include "InputRouter.h"
#include "RuntimeInteractionController.h"
#include "RuntimeCameraMode.h"
#include "RunCameraState.h"
#include "RunTimerState.h"
#include "Tools/RuntimeTools.h"
#include "Window.h"
#include "Scene/SceneController.h"
#include "Scene/SceneRuntime.h"
#include "Replay/ReplayRuntime.h"
#include "InputFrame.h"
#include "Allocation/RuntimeAllocationTracker.h"
#include "Editor/EditorTools.h"
#include "Replay/ReplayOverlayLayout.h"
#include "RunDemoDirector.h"
#include "RuntimeFileWriter.h"
#include "RuntimePickService.h"

#include "../Physics/PhysicsEngineStoreQueries.h"
#include "../Core/Config.h"
#include "../Rendering/IRenderCaptureBackend.h"
#include "../UI/UI.h"

#pragma warning( push, 0 )
#include "../../ThirdPtySource/nlohmann/json.hpp"
#pragma warning( pop )

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <sstream>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Basics::RunInternal;
using namespace SkullbonezCore::Basics::ReplayOverlay;
using namespace SkullbonezCore::GameObjects;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;
using SkullbonezCore::Hardware::Input;
namespace Physics = SkullbonezCore::Physics;
namespace RuntimeAllocation = SkullbonezCore::Runtime::Allocation;

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

constexpr uint64_t INTERACTION_PREDICTION_FINGERPRINT_OFFSET = 1469598103934665603ull;
constexpr uint64_t INTERACTION_PREDICTION_FINGERPRINT_PRIME = 1099511628211ull;

struct PredictionTrajectoryFingerprint
{
    uint64_t hash = INTERACTION_PREDICTION_FINGERPRINT_OFFSET;
    std::size_t recordCount = 0;
    std::size_t pointCount = 0;

    bool Ready() const
    {
        return recordCount > 0 && pointCount > 0;
    }
};

void HashPredictionByte( uint64_t& hash, uint8_t value )
{
    hash ^= static_cast<uint64_t>( value );
    hash *= INTERACTION_PREDICTION_FINGERPRINT_PRIME;
}

template <typename T> void HashPredictionScalar( uint64_t& hash, T value )
{
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>( &value );
    for ( std::size_t i = 0; i < sizeof( T ); ++i )
    {
        HashPredictionByte( hash, bytes[i] );
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

std::string FormatPredictionHash( uint64_t hash )
{
    char buffer[24] = {};
    sprintf_s( buffer, sizeof( buffer ), "0x%016llX", static_cast<unsigned long long>( hash ) );
    return buffer;
}

PredictionTrajectoryFingerprint BuildPredictionTrajectoryFingerprint( const ReplayRuntime& replayRuntime )
{
    PredictionTrajectoryFingerprint fingerprint;
    const ReplayTrajectoryStore& store = replayRuntime.Prediction().trajectoryStore;
    for ( const ReplayTrajectoryRecord& record : store.records )
    {
        const std::size_t publishedPointCount = (std::min)( record.publishedPointCount, record.points.size() );
        if ( publishedPointCount == 0 )
        {
            continue;
        }

        // Invariant: this report hash intentionally ignores record versions and
        // vector capacity. It fingerprints only the sampled polylines and draw
        // hierarchy that should be byte-identical across two identical
        // prediction runs.
        HashPredictionScalar( fingerprint.hash, record.key.bodyId.value );
        HashPredictionScalar( fingerprint.hash, static_cast<uint8_t>( record.key.lane ) );
        HashPredictionScalar( fingerprint.hash, record.key.branchOrdinal );
        HashPredictionScalar( fingerprint.hash, record.styleId );
        HashPredictionScalar( fingerprint.hash, record.parentId.value );
        HashPredictionScalar( fingerprint.hash, record.depth );
        HashPredictionScalar( fingerprint.hash, record.firstFrame );
        HashPredictionScalar( fingerprint.hash, static_cast<uint8_t>( record.contactDerived ? 1u : 0u ) );
        HashPredictionScalar( fingerprint.hash, static_cast<uint64_t>( publishedPointCount ) );
        for ( std::size_t i = 0; i < publishedPointCount; ++i )
        {
            const ReplayTrajectoryPoint& point = record.points[i];
            HashPredictionScalar( fingerprint.hash, point.frameIndex );
            HashPredictionVector( fingerprint.hash, point.position );
        }
        ++fingerprint.recordCount;
        fingerprint.pointCount += publishedPointCount;
    }
    return fingerprint;
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
    const RunReplayPredictionState& prediction = replayRuntime.Prediction();
    const std::vector<RunReplayPredictionFrame>* activePredictionFrames = &replayRuntime.ActivePredictionFrames();
    std::size_t activeFrameCount = activePredictionFrames->size();
    if ( activeFrameCount < 2 && prediction.BuildPrefixShouldBePresented() )
    {
        activePredictionFrames = &prediction.build.buildFrames;
        activeFrameCount = prediction.PublishedBuildFrameCount();
    }
    const ReplayBodyId targetId = replayRuntime.PathVisualizer().targetId;
    if ( targetId.value == 0 || activeFrameCount < 2 )
    {
        return false;
    }

    const RunReplayPredictionBodySample* first = FindPredictionBodyById( activePredictionFrames->front(), targetId );
    const RunReplayPredictionBodySample* last =
        FindPredictionBodyById( ( *activePredictionFrames )[activeFrameCount - 1], targetId );
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

std::size_t VisiblePredictionFrameCount( const ReplayRuntime& replayRuntime )
{
    const RunReplayPredictionState& prediction = replayRuntime.Prediction();
    const std::vector<RunReplayPredictionFrame>& activePredictionFrames = replayRuntime.ActivePredictionFrames();
    if ( activePredictionFrames.size() >= 2 )
    {
        return activePredictionFrames.size();
    }
    if ( prediction.build.building )
    {
        return prediction.PublishedBuildFrameCount();
    }
    return activePredictionFrames.size();
}

bool ReplayPredictionPathVisible( const ReplayRuntime& replayRuntime )
{
    // Concept: long prediction jobs expose a populated build prefix before the
    // final frame vector is swapped in. Automation should agree with the overlay
    // and count that prefix as visible once it can draw at least one segment.
    return replayRuntime.PathVisualizer().hasTarget &&
           ( !replayRuntime.PathVisualizer().futureNodes.empty() || VisiblePredictionFrameCount( replayRuntime ) >= 2 ||
             !replayRuntime.Prediction().futureNodeCache.futureNodes.empty() );
}

bool ReplayPredictionContactsIncomplete( const ReplayRuntime& replayRuntime )
{
    // Concept: automation reports should distinguish a valid root prediction
    // from a partial contact-derived tree, because contact reserve failures are
    // intentionally non-fatal to prediction drawing.
    const RunReplayPredictionState& prediction = replayRuntime.Prediction();
    const std::vector<RunReplayPredictionFrame>* frames = &prediction.simulation.frames;
    std::size_t frameCount = frames->size();
    if ( prediction.BuildPrefixShouldBePresented() )
    {
        frames = &prediction.build.buildFrames;
        frameCount = prediction.PublishedBuildFrameCount();
    }
    frameCount = (std::min)( frameCount, frames->size() );
    for ( std::size_t i = 0; i < frameCount; ++i )
    {
        if ( ( *frames )[i].contactsIncomplete )
        {
            return true;
        }
    }
    return false;
}

const DemoPhase* ActiveDirectorPhase( const RunCameraState& camera )
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

bool LiveSolverHashStableAcrossPrediction( const ReplayRuntime& replayRuntime,
                                           uint64_t* outSourceHash = nullptr,
                                           uint64_t* outLiveHash = nullptr )
{
    // Concept: prediction isolation proof. The source hash is captured before
    // the private prediction engine starts stepping; the live latest hash should
    // still match after prediction has produced visible frames.
    const ReplaySolverFrameSample* latest = replayRuntime.Solver().LatestSample();
    const uint64_t sourceHash = replayRuntime.Prediction().simulation.sourceSolverHash;
    const uint64_t liveHash = latest ? latest->solverHash : 0;
    if ( outSourceHash )
    {
        *outSourceHash = sourceHash;
    }
    if ( outLiveHash )
    {
        *outLiveHash = liveHash;
    }
    return latest && sourceHash != 0 && sourceHash == liveHash;
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
    case RunCameraMode::Director:
        return "Director";
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
    if ( value == "Director" )
    {
        outMode = RunCameraMode::Director;
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

const char* ReplayTrackName( RunReplayTrack track )
{
    return track == RunReplayTrack::Solver ? "Solver" : "Presentation";
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
    if ( value == "F5" )
    {
        outVirtualKey = VK_F5;
        return true;
    }
    if ( value == "F6" )
    {
        outVirtualKey = VK_F6;
        return true;
    }
    if ( value == "Enter" || value == "Return" )
    {
        outVirtualKey = VK_RETURN;
        return true;
    }
    if ( value == "Tab" )
    {
        outVirtualKey = VK_TAB;
        return true;
    }
    return false;
}

bool ReadAutomationVec3( const Json& value, Vector3& out )
{
    if ( !value.is_array() || value.size() != 3u || !value[0].is_number() || !value[1].is_number() ||
         !value[2].is_number() )
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
    case RunInteractionAutomationActionType::ClickObject:
        return "clickObject";
    case RunInteractionAutomationActionType::ClickReplayControl:
        return "clickReplayControl";
    case RunInteractionAutomationActionType::ScrubReplaySolverTrack:
        return "scrubReplaySolverTrack";
    case RunInteractionAutomationActionType::SetReplayPredictionEnabled:
        return "setReplayPredictionEnabled";
    case RunInteractionAutomationActionType::SetReplayPredictionHorizonSeconds:
        return "setReplayPredictionHorizonSeconds";
    case RunInteractionAutomationActionType::SetReplayPathTarget:
        return "setReplayPathTarget";
    case RunInteractionAutomationActionType::NudgeReplayPathTargetVelocity:
        return "nudgeReplayPathTargetVelocity";
    case RunInteractionAutomationActionType::ShowReplayScrubber:
        return "showReplayScrubber";
    case RunInteractionAutomationActionType::PressKey:
        return "pressKey";
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
    case RunInteractionAutomationAssertKind::PredictionPathVisible:
        return "predictionPathVisible";
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
    case RunInteractionAutomationAssertKind::LiveSolverHashStableAcrossPrediction:
        return "liveSolverHashStableAcrossPrediction";
    case RunInteractionAutomationAssertKind::PredictionTrajectoryFingerprintReady:
        return "predictionTrajectoryFingerprintReady";
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

void AppendReportAction( InteractionAutomationController& state,
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

void InjectAutomationLeftMousePress( InteractionAutomationController& state,
                                     RunInteractionAutomationAction& action,
                                     int frame,
                                     const SkullbonezCore::UI::UIRect& rect )
{
    POINT mouse = {};
    mouse.x = static_cast<LONG>( rect.x + rect.w * 0.5f );
    mouse.y = static_cast<LONG>( rect.y + rect.h * 0.5f );
    state.mouseClientPosition = mouse;
    state.hasMouseClientPosition = true;
    state.leftMouseDown = true;
    state.releaseLeftFrame = frame + 1;
    action.mouse = mouse;
    action.hasMouse = true;
}

void FailAutomation( InteractionAutomationController& state, const char* message )
{
    state.failed = true;
    if ( state.failure[0] == '\0' )
    {
        strcpy_s( state.failure, sizeof( state.failure ), message ? message : "interaction automation failed" );
    }
}

void ApplyInteractionAutomationDirectorCameraAction( InteractionAutomationController& state,
                                                     SkullbonezCore::Environment::CameraCollection& cameras,
                                                     RunCameraState& camera,
                                                     RunInteractionAutomationAction& action,
                                                     int frame )
{
    // Concept: director/camera automation seeds the same camera and director
    // owners used by live authoring. Camera-mode transitions are routed by the
    // caller through InputRouter before this helper handles director-local work.
    switch ( action.type )
    {
    case RunInteractionAutomationActionType::LoadShotList:
    {
        const bool loaded = DemoDirectorPlayback::LoadShotList( camera, cameras, action.path );
        if ( !loaded )
        {
            FailAutomation( state, "failed to load director shot list" );
        }
        AppendReportAction( state,
                            frame,
                            action.type,
                            action.path,
                            nullptr,
                            loaded,
                            loaded ? "shot list loaded" : "shot list unavailable" );
        break;
    }
    case RunInteractionAutomationActionType::DirectorAdvance:
    {
        const bool advanced = DemoDirectorPlayback::AdvancePhase( camera, cameras );
        if ( !advanced )
        {
            FailAutomation( state, "failed to advance director phase" );
        }
        AppendReportAction( state,
                            frame,
                            action.type,
                            "",
                            nullptr,
                            advanced,
                            advanced ? "director phase advanced" : "director phase unavailable" );
        break;
    }
    case RunInteractionAutomationActionType::DirectorGrab:
    {
        const bool grabbed = DemoDirectorPlayback::BeginGrab( camera, cameras );
        if ( !grabbed )
        {
            FailAutomation( state, "failed to grab director camera" );
        }
        AppendReportAction( state,
                            frame,
                            action.type,
                            "",
                            nullptr,
                            grabbed,
                            grabbed ? "director camera grabbed" : "director grab unavailable" );
        break;
    }
    case RunInteractionAutomationActionType::DirectorRelease:
    {
        const bool released = DemoDirectorPlayback::EndGrab( camera, cameras );
        if ( !released )
        {
            FailAutomation( state, "failed to release director camera" );
        }
        AppendReportAction( state,
                            frame,
                            action.type,
                            "",
                            nullptr,
                            released,
                            released ? "director camera released" : "director release unavailable" );
        break;
    }
    case RunInteractionAutomationActionType::SetPhaseStyle:
    {
        const bool applied = DemoDirectorPlayback::SetCurrentPhaseStyle( camera, action.path );
        if ( !applied )
        {
            FailAutomation( state, "failed to set director phase style" );
        }
        AppendReportAction( state,
                            frame,
                            action.type,
                            action.path,
                            nullptr,
                            applied,
                            applied ? "director phase style set" : "director phase unavailable" );
        break;
    }
    case RunInteractionAutomationActionType::SetCameraPose:
    {
        const bool applied = true;
        // Why: pose-authoring proofs seed the current camera, then use normal
        // J/L key handling to write and save the shot list.
        cameras.SetPrimaryPose( action.cameraPose.eye, action.cameraPose.view, action.cameraPose.up );
        AppendReportAction( state,
                            frame,
                            action.type,
                            "",
                            nullptr,
                            applied,
                            applied ? "camera pose applied" : "camera unavailable" );
        break;
    }
    default:
        break;
    }
}

template <typename TrySetReplayPathTarget, typename SetWorldInteractionOwnerAfterTransition>
void ApplyInteractionAutomationReplayStateAction( InteractionAutomationController& state,
                                                  RunTimerState& timers,
                                                  ReplayRuntime& replayRuntime,
                                                  Physics::PhysicsEngine& physics,
                                                  RunInteractionAutomationAction& action,
                                                  int frame,
                                                  TrySetReplayPathTarget trySetReplayPathTarget,
                                                  SetWorldInteractionOwnerAfterTransition setWorldInteractionOwner )
{
    // Concept: replay state automation changes only harness-visible replay
    // controls. Direct physics mutation is limited to the velocity-edit proof
    // path and still marks prediction dirty so replay owners rebuild outputs.
    switch ( action.type )
    {
    case RunInteractionAutomationActionType::ShowReplayScrubber:
        replayRuntime.Scrubber().visible = action.boolValue;
        if ( action.boolValue )
        {
            replayRuntime.Scrubber().visibleUntil = timers.simulationTimer.GetTotalTime() + 5.0;
        }
        AppendReportAction( state, frame, action.type, "", nullptr, true, action.boolValue ? "visible" : "hidden" );
        break;
    case RunInteractionAutomationActionType::SetReplayPredictionEnabled:
        replayRuntime.Prediction().enabled = action.boolValue;
        replayRuntime.Prediction().build.dirty = true;
        setWorldInteractionOwner(
            action.boolValue ? WorldInteractionOwner::ReplayPrediction : WorldInteractionOwner::None,
            InteractionExitReason::EnterReplay );
        AppendReportAction( state,
                            frame,
                            action.type,
                            "",
                            nullptr,
                            true,
                            action.boolValue ? "prediction enabled" : "prediction disabled" );
        break;
    case RunInteractionAutomationActionType::SetReplayPathTarget:
    {
        const bool targetSet = trySetReplayPathTarget( action.text );
        if ( !targetSet )
        {
            FailAutomation( state, "failed to set replay path target" );
        }
        AppendReportAction( state,
                            frame,
                            action.type,
                            action.text,
                            nullptr,
                            targetSet,
                            targetSet ? "replay path target set" : "replay path target unavailable" );
        break;
    }
    case RunInteractionAutomationActionType::SetReplayPredictionHorizonSeconds:
    {
        const float horizonSeconds =
            std::clamp( action.numberValue, REPLAY_PREDICTION_MIN_SECONDS, REPLAY_PREDICTION_MAX_SECONDS );
        // Why: automation should use the same bounded horizon value the replay UI
        // exposes, while still forcing a rebuild when a script changes it before
        // a proof.
        replayRuntime.Prediction().simulation.horizonSeconds = horizonSeconds;
        replayRuntime.MarkPredictionDirty();
        std::ostringstream detail;
        detail << "prediction horizon set to " << horizonSeconds << "s";
        AppendReportAction( state, frame, action.type, "", nullptr, true, detail.str().c_str() );
        break;
    }
    case RunInteractionAutomationActionType::NudgeReplayPathTargetVelocity:
    {
        const Physics::PhysicsBodyStore& bodyStore =
            SkullbonezCore::Physics::PhysicsEngineStoreQueries::BodyStore( physics );
        const Physics::PhysicsBodyHandle body = replayRuntime.ResolveVelocityEditBodyHandle( bodyStore );
        const Physics::PhysicsBodyRecord* record = bodyStore.RecordForHandle( body );
        const bool hasTarget =
            replayRuntime.PathVisualizer().hasTarget && replayRuntime.PathVisualizer().targetId.value != 0;
        bool applied = false;
        if ( hasTarget && record )
        {
            RunReplayPredictionState& prediction = replayRuntime.Prediction();
            if ( !prediction.build.complete || prediction.simulation.frames.size() < 2 )
            {
                FailAutomation( state, "replay path target velocity nudge requires a completed prediction baseline" );
            }
            else
            {
                // Why: automation needs the same old-vs-new future proof as a
                // mouse drag, but without depending on pixel-perfect axis hit
                // testing. Capture is still deferred to the visualizer.
                prediction.baseline.valid = false;
                prediction.baseline.comparisonActive = true;
                prediction.baseline.divergenceValid = false;
                prediction.baseline.divergenceUnits = 0.0f;

                const Vector3 nextLinearVelocity = record->linearVelocity + action.vectorValue;
                applied = physics.SetBodyVelocity( body, nextLinearVelocity, record->angularVelocity, true );
                if ( applied )
                {
                    replayRuntime.Prediction().enabled = true;
                    replayRuntime.MarkPredictionDirty();
                    replayRuntime.Scrubber().visible = true;
                    replayRuntime.Scrubber().visibleUntil =
                        timers.simulationTimer.GetTotalTime() + REPLAY_SCRUBBER_VISIBLE_SECONDS;
                    setWorldInteractionOwner( WorldInteractionOwner::ReplayVelocityEdit,
                                              InteractionExitReason::EnterReplay );
                }
            }
        }
        else
        {
            FailAutomation( state, "failed to resolve replay path target for velocity nudge" );
        }
        if ( !applied && !state.failed )
        {
            FailAutomation( state, "failed to apply replay path target velocity nudge" );
        }
        AppendReportAction( state,
                            frame,
                            action.type,
                            action.text,
                            nullptr,
                            applied,
                            applied ? "path target velocity nudged" : "path target velocity nudge failed" );
        break;
    }
    default:
        break;
    }
}

void ShowInteractionAutomationReplayScrubber( RunTimerState& timers, ReplayRuntime& replayRuntime )
{
    replayRuntime.Scrubber().visible = true;
    replayRuntime.Scrubber().visibleUntil = timers.simulationTimer.GetTotalTime() + REPLAY_SCRUBBER_VISIBLE_SECONDS;
}

void AppendInteractionAutomationReplayControlFailure( InteractionAutomationController& state,
                                                      int frame,
                                                      const RunInteractionAutomationAction& action,
                                                      const char* failure,
                                                      const char* detail )
{
    FailAutomation( state, failure );
    AppendReportAction( state, frame, action.type, action.text, nullptr, false, detail );
}

void InjectInteractionAutomationReplayControlClick( InteractionAutomationController& state,
                                                    RunTimerState& timers,
                                                    ReplayRuntime& replayRuntime,
                                                    RunInteractionAutomationAction& action,
                                                    int frame,
                                                    const SkullbonezCore::UI::UIRect& rect,
                                                    const char* detail )
{
    InjectAutomationLeftMousePress( state, action, frame, rect );
    ShowInteractionAutomationReplayScrubber( timers, replayRuntime );
    AppendReportAction( state, frame, action.type, action.text, &action.mouse, true, detail );
}

void ApplyInteractionAutomationReplayControlClick( InteractionAutomationController& state,
                                                   Window* window,
                                                   const EngineConfig& config,
                                                   const RunSceneState& scene,
                                                   RunTimerState& timers,
                                                   ReplayRuntime& replayRuntime,
                                                   RunInteractionAutomationAction& action,
                                                   int frame )
{
    // Concept: replay-control automation clicks the visible scrubber widgets
    // instead of mutating replay state directly. Normal replay input remains the
    // owner of prediction, pause/play, velocity-edit, and branch transitions.
    if ( strcmp( action.text, "predict" ) == 0 )
    {
        const int screenW = window ? window->ClientWidth() : config.window.screenX;
        const int screenH = window ? window->ClientHeight() : config.window.screenY;
        const ReplayRecorderStats solverReplayStats = replayRuntime.Solver().GetStats();
        // Why: interaction scripts should match the real UI: Predict can branch
        // from the current live solver state even before a paused scene has
        // accumulated two retained solver samples.
        const bool predictionToolsEnabled = solverReplayStats.enabled && scene.isScenePhysics;
        if ( screenW > 0 && screenH > 0 && predictionToolsEnabled )
        {
            InjectInteractionAutomationReplayControlClick( state,
                                                           timers,
                                                           replayRuntime,
                                                           action,
                                                           frame,
                                                           ReplayScrubberPredictToggleRect( screenW, screenH ),
                                                           "mouse press injected at predict toggle" );
        }
        else
        {
            AppendInteractionAutomationReplayControlFailure( state,
                                                             frame,
                                                             action,
                                                             "replay predict control unavailable",
                                                             "replay predict control unavailable" );
        }
        return;
    }

    if ( strcmp( action.text, "past" ) == 0 || strcmp( action.text, "pastPath" ) == 0 )
    {
        const int screenW = window ? window->ClientWidth() : config.window.screenX;
        const int screenH = window ? window->ClientHeight() : config.window.screenY;
        const ReplayRecorderStats solverReplayStats = replayRuntime.Solver().GetStats();
        const bool pastPathControlEnabled =
            solverReplayStats.enabled && solverReplayStats.sampleCount >= 2 && replayRuntime.PathVisualizer().hasTarget;
        if ( screenW > 0 && screenH > 0 && pastPathControlEnabled )
        {
            InjectInteractionAutomationReplayControlClick( state,
                                                           timers,
                                                           replayRuntime,
                                                           action,
                                                           frame,
                                                           ReplayScrubberPastPathToggleRect( screenW, screenH ),
                                                           "mouse press injected at past-path toggle" );
        }
        else
        {
            AppendInteractionAutomationReplayControlFailure( state,
                                                             frame,
                                                             action,
                                                             "replay past-path control unavailable",
                                                             "replay past-path control unavailable" );
        }
        return;
    }

    if ( strcmp( action.text, "pause" ) == 0 || strcmp( action.text, "play" ) == 0 )
    {
        const int screenW = window ? window->ClientWidth() : config.window.screenX;
        const int screenH = window ? window->ClientHeight() : config.window.screenY;
        const ReplayRecorderStats solverReplayStats = replayRuntime.Solver().GetStats();
        const bool solverToolsEnabled = solverReplayStats.enabled && solverReplayStats.sampleCount >= 2;
        if ( screenW > 0 && screenH > 0 && solverToolsEnabled )
        {
            // Concept: the scrubber exposes one physical button whose label
            // flips between pause and play. Automation clicks the real rectangle
            // so replay input ownership does the state transition and
            // prediction-freeze work.
            InjectInteractionAutomationReplayControlClick( state,
                                                           timers,
                                                           replayRuntime,
                                                           action,
                                                           frame,
                                                           ReplayScrubberPauseButtonRect( screenW, screenH ),
                                                           "mouse press injected at pause/play toggle" );
        }
        else
        {
            AppendInteractionAutomationReplayControlFailure( state,
                                                             frame,
                                                             action,
                                                             "replay pause/play control unavailable",
                                                             "replay pause/play control unavailable" );
        }
        return;
    }

    if ( strcmp( action.text, "velocity" ) == 0 )
    {
        const int screenW = window ? window->ClientWidth() : config.window.screenX;
        const int screenH = window ? window->ClientHeight() : config.window.screenY;
        const ReplayRecorderStats solverReplayStats = replayRuntime.Solver().GetStats();
        const bool solverToolsEnabled = solverReplayStats.enabled && solverReplayStats.sampleCount >= 2;
        if ( screenW > 0 && screenH > 0 && solverToolsEnabled )
        {
            // Concept: velocity automation toggles the visible scrubber control,
            // then lets the next scripted world click exercise replay velocity
            // targeting through normal input ownership.
            InjectInteractionAutomationReplayControlClick( state,
                                                           timers,
                                                           replayRuntime,
                                                           action,
                                                           frame,
                                                           ReplayScrubberVelocityEditToggleRect( screenW, screenH ),
                                                           "mouse press injected at velocity toggle" );
        }
        else
        {
            AppendInteractionAutomationReplayControlFailure( state,
                                                             frame,
                                                             action,
                                                             "replay velocity control unavailable",
                                                             "replay velocity control unavailable" );
        }
        return;
    }

    if ( strcmp( action.text, "branch" ) == 0 )
    {
        const int screenW = window ? window->ClientWidth() : config.window.screenX;
        const int screenH = window ? window->ClientHeight() : config.window.screenY;
        const ReplayRecorderStats solverReplayStats = replayRuntime.Solver().GetStats();
        const bool branchTargetAvailable = replayRuntime.Scrubber().historicalSamplePaused &&
                                           replayRuntime.Scrubber().activeTrack == RunReplayTrack::Solver &&
                                           solverReplayStats.enabled && solverReplayStats.sampleCount >= 2 &&
                                           replayRuntime.CurrentSolverScrubSample() != nullptr;
        if ( screenW > 0 && screenH > 0 && branchTargetAvailable )
        {
            // Why: branch-restore proof clicks the visible Branch rectangle
            // after a scripted scrub, so TickReplayScrubberInput remains the
            // owner of the restore.
            InjectInteractionAutomationReplayControlClick( state,
                                                           timers,
                                                           replayRuntime,
                                                           action,
                                                           frame,
                                                           ReplayScrubberBranchButtonRect( screenW, screenH ),
                                                           "mouse press injected at branch restore button" );
        }
        else
        {
            AppendInteractionAutomationReplayControlFailure( state,
                                                             frame,
                                                             action,
                                                             "replay branch control unavailable",
                                                             "replay branch control unavailable" );
        }
        return;
    }

    AppendInteractionAutomationReplayControlFailure( state,
                                                     frame,
                                                     action,
                                                     "unsupported replay control in interaction script",
                                                     "unsupported replay control" );
}

void ApplyInteractionAutomationSolverTrackScrub( InteractionAutomationController& state,
                                                 Window* window,
                                                 const EngineConfig& config,
                                                 RunTimerState& timers,
                                                 ReplayRuntime& replayRuntime,
                                                 RunInteractionAutomationAction& action,
                                                 int frame )
{
    const int screenW = window ? window->ClientWidth() : config.window.screenX;
    const int screenH = window ? window->ClientHeight() : config.window.screenY;
    const ReplayRecorderStats solverReplayStats = replayRuntime.Solver().GetStats();
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
        InjectInteractionAutomationReplayControlClick( state,
                                                       timers,
                                                       replayRuntime,
                                                       action,
                                                       frame,
                                                       target,
                                                       "mouse press injected at solver replay track" );
    }
    else
    {
        AppendInteractionAutomationReplayControlFailure( state,
                                                         frame,
                                                         action,
                                                         "replay solver scrub track unavailable",
                                                         "replay solver scrub track unavailable" );
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

    if ( entry.contains( "loadShotList" ) )
    {
        outAction.type = RunInteractionAutomationActionType::LoadShotList;
        CopyText( outAction.path, sizeof( outAction.path ), entry["loadShotList"].get<std::string>() );
        return true;
    }

    if ( entry.contains( "directorPlay" ) )
    {
        outAction.type = RunInteractionAutomationActionType::DirectorPlay;
        outAction.boolValue = ReadBool( entry["directorPlay"] );
        CopyText( outAction.text, sizeof( outAction.text ), outAction.boolValue ? "Director" : "Inspect" );
        return true;
    }

    if ( entry.contains( "directorAdvance" ) )
    {
        outAction.type = RunInteractionAutomationActionType::DirectorAdvance;
        return true;
    }

    if ( entry.contains( "directorGrab" ) )
    {
        outAction.type = RunInteractionAutomationActionType::DirectorGrab;
        return true;
    }

    if ( entry.contains( "directorRelease" ) )
    {
        outAction.type = RunInteractionAutomationActionType::DirectorRelease;
        return true;
    }

    if ( entry.contains( "setPhaseStyle" ) )
    {
        outAction.type = RunInteractionAutomationActionType::SetPhaseStyle;
        CopyText( outAction.path, sizeof( outAction.path ), entry["setPhaseStyle"].get<std::string>() );
        return true;
    }

    if ( entry.contains( "setCameraPose" ) )
    {
        outAction.type = RunInteractionAutomationActionType::SetCameraPose;
        return ReadAutomationCameraPose( entry["setCameraPose"], outAction.cameraPose, outError );
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
        if ( entry.contains( "holdFrames" ) )
        {
            outAction.holdFrames = (std::max)( 1, entry["holdFrames"].get<int>() );
        }
        return true;
    }

    if ( entry.contains( "loseFocus" ) )
    {
        outAction.type = RunInteractionAutomationActionType::LoseFocus;
        outAction.holdFrames = (std::max)( 1, entry["loseFocus"].get<int>() );
        return true;
    }

    if ( entry.contains( "moveMouse" ) )
    {
        const Json& point = entry["moveMouse"];
        if ( !point.is_array() || point.size() != 2 )
        {
            outError = "moveMouse must be a 2-integer array";
            return false;
        }
        outAction.type = RunInteractionAutomationActionType::MoveMouse;
        outAction.mouse = { point[0].get<long>(), point[1].get<long>() };
        outAction.hasMouse = true;
        return true;
    }

    if ( entry.contains( "clickReplayControl" ) )
    {
        outAction.type = RunInteractionAutomationActionType::ClickReplayControl;
        CopyText( outAction.text, sizeof( outAction.text ), entry["clickReplayControl"].get<std::string>() );
        return true;
    }

    if ( entry.contains( "scrubReplaySolverTrack" ) )
    {
        outAction.type = RunInteractionAutomationActionType::ScrubReplaySolverTrack;
        outAction.numberValue = std::clamp( entry["scrubReplaySolverTrack"].get<float>(), 0.0f, 1.0f );
        CopyText( outAction.text, sizeof( outAction.text ), "solver" );
        return true;
    }

    if ( entry.contains( "setReplayPredictionEnabled" ) )
    {
        outAction.type = RunInteractionAutomationActionType::SetReplayPredictionEnabled;
        outAction.boolValue = ReadBool( entry["setReplayPredictionEnabled"] );
        return true;
    }

    if ( entry.contains( "setReplayPredictionHorizonSeconds" ) )
    {
        outAction.type = RunInteractionAutomationActionType::SetReplayPredictionHorizonSeconds;
        outAction.numberValue = entry["setReplayPredictionHorizonSeconds"].get<float>();
        return true;
    }

    if ( entry.contains( "setReplayPathTarget" ) )
    {
        outAction.type = RunInteractionAutomationActionType::SetReplayPathTarget;
        CopyText( outAction.text, sizeof( outAction.text ), entry["setReplayPathTarget"].get<std::string>() );
        return true;
    }

    if ( entry.contains( "nudgeReplayPathTargetVelocity" ) )
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

    if ( entry.contains( "showReplayScrubber" ) )
    {
        outAction.type = RunInteractionAutomationActionType::ShowReplayScrubber;
        outAction.boolValue = ReadBool( entry["showReplayScrubber"] );
        return true;
    }

    if ( entry.contains( "pressKey" ) )
    {
        outAction.type = RunInteractionAutomationActionType::PressKey;
        const std::string keyName = entry["pressKey"].get<std::string>();
        if ( !TryParseVirtualKey( keyName, outAction.keyVirtualKey ) )
        {
            outError = "unknown pressKey value: " + keyName;
            return false;
        }
        CopyText( outAction.text, sizeof( outAction.text ), keyName );
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
        else if ( name == "directorGrabbed" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::DirectorGrabbed;
            outAction.boolValue = ReadBool( member.value() );
        }
        else if ( name == "directorPhaseIndex" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::DirectorPhaseIndex;
            outAction.numberValue = static_cast<float>( member.value().get<int>() );
        }
        else if ( name == "directorPhaseName" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::DirectorPhaseName;
            CopyText( outAction.text, sizeof( outAction.text ), member.value().get<std::string>() );
        }
        else if ( name == "directorPhaseStylePath" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::DirectorPhaseStylePath;
            CopyText( outAction.path, sizeof( outAction.path ), member.value().get<std::string>() );
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
        else if ( name == "predictionBaselineVisible" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::PredictionBaselineVisible;
            outAction.boolValue = ReadBool( member.value() );
        }
        else if ( name == "predictionDivergenceMin" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::PredictionDivergenceMin;
            outAction.numberValue = member.value().get<float>();
        }
        else if ( name == "replaySolverTrackAtPresent" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::ReplaySolverTrackAtPresent;
            outAction.boolValue = ReadBool( member.value() );
        }
        else if ( name == "predictionScrubFrameActive" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::PredictionScrubFrameActive;
            outAction.boolValue = ReadBool( member.value() );
        }
        else if ( name == "predictionTargetDisplacementMin" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::PredictionTargetDisplacementMin;
            outAction.numberValue = member.value().get<float>();
        }
        else if ( name == "liveSolverHashStableAcrossPrediction" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::LiveSolverHashStableAcrossPrediction;
            outAction.boolValue = ReadBool( member.value() );
        }
        else if ( name == "predictionTrajectoryFingerprintReady" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::PredictionTrajectoryFingerprintReady;
            outAction.boolValue = ReadBool( member.value() );
        }
        else if ( name == "gizmoVisible" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::GizmoVisible;
            outAction.boolValue = ReadBool( member.value() );
        }
        else if ( name == "mousePickupActive" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::MousePickupActive;
            outAction.boolValue = ReadBool( member.value() );
        }
        else if ( name == "pointerCapture" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::PointerCapture;
            CopyText( outAction.text, sizeof( outAction.text ), member.value().get<std::string>() );
        }
        else if ( name == "nativeCaptureRequested" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::NativeCaptureRequested;
            outAction.boolValue = ReadBool( member.value() );
        }
        else if ( name == "cursorVisibleRequested" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::CursorVisibleRequested;
            outAction.boolValue = ReadBool( member.value() );
        }
        else if ( name == "uiBlocksMouse" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::UiBlocksMouse;
            outAction.boolValue = ReadBool( member.value() );
        }
        else if ( name == "launcherRayActive" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::LauncherRayActive;
            outAction.boolValue = ReadBool( member.value() );
        }
        else if ( name == "replayActiveTrack" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::ReplayActiveTrack;
            CopyText( outAction.text, sizeof( outAction.text ), member.value().get<std::string>() );
        }
        else if ( name == "replayHistoricalSamplePaused" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::ReplayHistoricalSamplePaused;
            outAction.boolValue = ReadBool( member.value() );
        }
        else if ( name == "memoryOverlayEnabled" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::MemoryOverlayEnabled;
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

struct InteractionAutomationAssertionEvaluation
{
    std::string expected;
    std::string actual;
    bool passed = false;
};

template <typename InspectGizmoInteractionActive>
InteractionAutomationAssertionEvaluation
EvaluateInteractionAutomationAssertion( RuntimeTools& runtimeTools,
                                        ReplayRuntime& replayRuntime,
                                        RuntimeInteractionController& interaction,
                                        const InputRouter& inputRouter,
                                        RunCameraState& camera,
                                        GameModelCollection& gameModels,
                                        const SceneEntityStore& entities,
                                        SkullbonezCore::UI::InGameUI& ui,
                                        const RunInteractionAutomationAction& action,
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
        const int selectedIndex = PeekSelectedEditorModelIndex( runtimeTools.Editor(), gameModels.BodyStore() );
        if ( selectedIndex >= 0 && selectedIndex < gameModels.SceneEntityCount() )
        {
            evaluation.actual = entities.At( selectedIndex ).displayName;
        }
        evaluation.passed = evaluation.actual == evaluation.expected;
        break;
    }
    case RunInteractionAutomationAssertKind::Owner:
        evaluation.expected = action.text;
        evaluation.actual = OwnerName( interaction.Owner() );
        evaluation.passed = evaluation.actual == evaluation.expected;
        break;
    case RunInteractionAutomationAssertKind::CameraMode:
        evaluation.expected = CameraModeName( action.cameraMode );
        evaluation.actual = CameraModeName( camera.mode );
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
        evaluation.actual = BoolString( replayRuntime.Prediction().enabled );
        evaluation.passed = replayRuntime.Prediction().enabled == action.boolValue;
        break;
    case RunInteractionAutomationAssertKind::ReplayPathTarget:
        evaluation.expected = action.text;
        evaluation.actual = replayRuntime.PathVisualizer().hasTarget ? replayRuntime.PathVisualizer().targetName : "";
        evaluation.passed = evaluation.actual == evaluation.expected;
        break;
    case RunInteractionAutomationAssertKind::PredictionPathVisible:
    {
        const bool visible = ReplayPredictionPathVisible( replayRuntime );
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( visible );
        evaluation.passed = visible == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionBaselineVisible:
    {
        const ReplayPredictionBaselineSnapshot& baseline = replayRuntime.Prediction().baseline;
        const bool visible = baseline.valid && baseline.comparisonActive;
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( visible );
        evaluation.passed = visible == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionDivergenceMin:
    {
        const ReplayPredictionBaselineSnapshot& baseline = replayRuntime.Prediction().baseline;
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
        const float solverPosition = replayRuntime.TrackPosition( RunReplayTrack::Solver );
        const float presentT = replayRuntime.SolverPresentTrackPosition();
        const bool atPresent = ReplayRuntime::AtPresentTrackPosition( solverPosition, presentT );
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( atPresent );
        evaluation.passed = atPresent == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionScrubFrameActive:
    {
        const bool active = replayRuntime.CurrentPredictionScrubFrame() != nullptr;
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( active );
        evaluation.passed = active == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionTargetDisplacementMin:
    {
        float displacement = 0.0f;
        const bool valid = TryPredictionTargetDisplacement( replayRuntime, displacement );
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
    case RunInteractionAutomationAssertKind::LiveSolverHashStableAcrossPrediction:
    {
        const bool stable = LiveSolverHashStableAcrossPrediction( replayRuntime );
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( stable );
        evaluation.passed = stable == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionTrajectoryFingerprintReady:
    {
        const PredictionTrajectoryFingerprint fingerprint = BuildPredictionTrajectoryFingerprint( replayRuntime );
        const bool ready = fingerprint.Ready();
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( ready );
        evaluation.passed = ready == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::GizmoVisible:
    {
        const bool visible = runtimeTools.Editor().selectedBody.IsValid() &&
                             ( runtimeTools.Editor().editorModeEnabled || inspectGizmoInteractionActive() );
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
        evaluation.actual = ReplayTrackName( replayRuntime.Scrubber().activeTrack );
        evaluation.passed = evaluation.actual == evaluation.expected;
        break;
    case RunInteractionAutomationAssertKind::ReplayHistoricalSamplePaused:
    {
        const bool paused = replayRuntime.Scrubber().historicalSamplePaused;
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
    }
    return evaluation;
}

bool LoadScript( InteractionAutomationController& state )
{
    RuntimeAllocation::RuntimeAllocationScope diagnosticsScope(
        RuntimeAllocation::RuntimeAllocationPhase::Diagnostics );
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

    const std::size_t actionCount = root["actions"].size();
    state.actions.reserve( actionCount );
    state.actionReports.reserve( actionCount + 8u );
    state.assertionReports.reserve( actionCount + 8u );
    state.screenshots.reserve( actionCount );

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

bool TryFindInteractionAutomationModel( const SceneController& scene, const char* name, int& outIndex )
{
    outIndex = -1;
    if ( !name || name[0] == '\0' )
    {
        return false;
    }

    outIndex = scene.Entities().FindByDisplayName( name );
    return outIndex >= 0;
}

bool TryProjectInteractionAutomationModel( const SceneController& scene,
                                           InputRouter& inputRouter,
                                           Window* window,
                                           const char* name,
                                           POINT& outMouse )
{
    int modelIndex = -1;
    if ( !TryFindInteractionAutomationModel( scene, name, modelIndex ) || !window )
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
                const POINT candidate{ static_cast<LONG>( x ), static_cast<LONG>( y ) };

                Vector3 rayOrigin;
                Vector3 rayDirection;
                if ( inputRouter.TryBuildWorldRayAt( candidate, scene.Cameras(), *window, rayOrigin, rayDirection ) )
                {
                    RuntimePickRequest request;
                    request.purpose = RuntimePickPurpose::EditorSelection;
                    request.bodyStore = &scene.Models().BodyStore();
                    request.colliderStore = &scene.Models().Colliders();
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

void SkullbonezCore::Basics::ClearInteractionAutomationInput( InteractionAutomationController& state )
{
    state.leftMouseDown = false;
    state.rightMouseDown = false;
    state.keyVirtualKey = 0;
    state.keyDown = false;
    state.releaseLeftFrame = -1;
    state.releaseRightFrame = -1;
    state.releaseKeyFrame = -1;
    state.unfocusedInputFrames = 0;
    Input::ClearAutomationState();
}


SbResult SkullbonezCore::Basics::ConfigureInteractionAutomation( InteractionAutomationController& state,
                                                                 const char* scriptPath,
                                                                 const char* reportPath )
{
    state = InteractionAutomationController{};
    strcpy_s( state.reportPath,
              sizeof( state.reportPath ),
              reportPath && reportPath[0] != '\0' ? reportPath : "TestOutput\\interaction\\interaction_report.json" );
    if ( !scriptPath || scriptPath[0] == '\0' )
    {
        state.failed = true;
        state.finished = true;
        strcpy_s( state.failure, sizeof( state.failure ), "interaction automation requires a script path" );
        return SbResult::Failure( "InteractionAutomation", state.failure );
    }
    strcpy_s( state.scriptPath, sizeof( state.scriptPath ), scriptPath );
    state.enabled = true;
    printf( "[interaction] Script: %s\n", state.scriptPath );
    printf( "[interaction] Report: %s\n", state.reportPath );
    return SbResult::Success();
}


SbResult SkullbonezCore::Basics::InteractionAutomationResult( const InteractionAutomationController& state )
{
    if ( !state.failed )
    {
        return SbResult::Success();
    }
    const char* message = state.failure[0] != '\0' ? state.failure : "interaction automation failed";
    return SbResult::Failure( "InteractionAutomation", message );
}

InteractionAutomationFrameResult
SkullbonezCore::Basics::TickInteractionAutomationBeforeInput( InteractionAutomationController& state,
                                                              Window* window,
                                                              const EngineConfig& config,
                                                              SceneController& scene,
                                                              RunTimerState& timers,
                                                              ReplayRuntime& replayRuntime,
                                                              RunCameraState& camera,
                                                              InputRouter& inputRouter,
                                                              RuntimeInteractionController& interaction,
                                                              RuntimeTools& runtimeTools,
                                                              AttachedCameraController& attachedCamera,
                                                              UI::InGameUI& ui )
{
    InteractionAutomationFrameResult result;
    if ( !state.enabled || state.finished )
    {
        return result;
    }
    if ( !state.scriptLoaded && !LoadScript( state ) )
    {
        state.finished = true;
        ClearInteractionAutomationInput( state );
        // Why: latch the automation-owned diagnostic before WM_QUIT. The
        // report writer is another Lane R boundary, so it also cannot replace
        // the earlier script failure if both operations fail.
        result.status = InteractionAutomationResult( state );
        const SbResult reportResult =
            WriteInteractionAutomationReport( state, scene, runtimeTools, replayRuntime, interaction, camera, ui );
        if ( result.status.ok )
        {
            result.status = reportResult;
        }
        result.requestQuit = true;
        return result;
    }

    const int frame = scene.State().currentFrame;
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
    if ( state.releaseKeyFrame == frame )
    {
        state.keyVirtualKey = 0;
        state.keyDown = false;
        state.releaseKeyFrame = -1;
    }

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
            const RunCameraMode targetMode =
                action.type == RunInteractionAutomationActionType::DirectorPlay
                    ? ( action.boolValue ? RunCameraMode::Director : RunCameraMode::Inspect )
                    : action.cameraMode;
            inputRouter.ApplyCameraMode( camera,
                                         targetMode,
                                         RuntimeInputActionSource::Runtime,
                                         inputRouter.RuntimeContext(),
                                         interaction,
                                         runtimeTools,
                                         replayRuntime,
                                         attachedCamera,
                                         scene );
            const bool applied = camera.mode == targetMode;
            if ( !applied )
            {
                FailAutomation( state, "failed to apply automated camera mode" );
            }
            AppendReportAction( state,
                                frame,
                                action.type,
                                action.text,
                                nullptr,
                                applied,
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
            ApplyInteractionAutomationDirectorCameraAction( state, scene.Cameras(), camera, action, frame );
            action.processed = true;
            break;
        case RunInteractionAutomationActionType::ShowReplayScrubber:
        case RunInteractionAutomationActionType::SetReplayPredictionEnabled:
        case RunInteractionAutomationActionType::SetReplayPathTarget:
        case RunInteractionAutomationActionType::SetReplayPredictionHorizonSeconds:
        case RunInteractionAutomationActionType::NudgeReplayPathTargetVelocity:
            ApplyInteractionAutomationReplayStateAction(
                state,
                timers,
                replayRuntime,
                scene.Physics(),
                action,
                frame,
                [&]( const char* name )
                {
                    int modelIndex = -1;
                    return TryFindInteractionAutomationModel( scene, name, modelIndex ) &&
                           replayRuntime.SetPathTarget( name, modelIndex, scene.Models().BodyStore() );
                },
                [&]( WorldInteractionOwner owner, InteractionExitReason reason )
                {
                    inputRouter.SetWorldInteractionOwner(
                        owner,
                        reason,
                        replayRuntime,
                        runtimeTools,
                        interaction,
                        scene.Cameras(),
                        scene.Terrain().Get(),
                        scene.Models(),
                        scene.Physics(),
                        camera,
                        NormalizeRuntimeCameraMode( replayRuntime.Camera().restoreCameraMode,
                                                    scene.State().isSceneMode,
                                                    RuntimeCameraModeEnabledMask( scene ) ),
                        attachedCamera.State().activeFollow,
                        camera.director.grabbed );
                } );
            action.processed = true;
            break;
        case RunInteractionAutomationActionType::PressKey:
            // Why: key automation should still enter through Input and
            // RuntimeInputContext edge detection. This only supplies the
            // virtual-key state that a real keyboard would have provided.
            state.keyVirtualKey = action.keyVirtualKey;
            state.keyDown = true;
            state.releaseKeyFrame = frame + 1;
            AppendReportAction( state, frame, action.type, action.text, nullptr, true, "key press injected" );
            action.processed = true;
            break;
        case RunInteractionAutomationActionType::MoveMouse:
            state.mouseClientPosition = action.mouse;
            state.hasMouseClientPosition = true;
            AppendReportAction( state, frame, action.type, nullptr, &action.mouse, true, "mouse move injected" );
            action.processed = true;
            break;
        case RunInteractionAutomationActionType::ClickReplayControl:
            ApplyInteractionAutomationReplayControlClick( state,
                                                          window,
                                                          config,
                                                          scene.State(),
                                                          timers,
                                                          replayRuntime,
                                                          action,
                                                          frame );
            action.processed = true;
            break;
        case RunInteractionAutomationActionType::ScrubReplaySolverTrack:
            ApplyInteractionAutomationSolverTrackScrub( state, window, config, timers, replayRuntime, action, frame );
            action.processed = true;
            break;
        case RunInteractionAutomationActionType::ClickObject:
        {
            POINT mouse = {};
            const bool projected =
                TryProjectInteractionAutomationModel( scene, inputRouter, window, action.text, mouse );
            if ( projected )
            {
                state.mouseClientPosition = mouse;
                state.hasMouseClientPosition = true;
                action.mouse = mouse;
                action.hasMouse = true;
                if ( action.button == RunInteractionAutomationButton::Right )
                {
                    state.rightMouseDown = true;
                    state.releaseRightFrame = frame + action.holdFrames;
                }
                else
                {
                    state.leftMouseDown = true;
                    state.releaseLeftFrame = frame + action.holdFrames;
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
        case RunInteractionAutomationActionType::LoseFocus:
            state.unfocusedInputFrames = action.holdFrames;
            AppendReportAction( state, frame, action.type, "input", nullptr, true, "focus loss injected" );
            action.processed = true;
            break;
        case RunInteractionAutomationActionType::AssertState:
        case RunInteractionAutomationActionType::Screenshot:
            break;
        }
    }

    Input::AutomationState inputState;
    inputState.enabled = true;
    inputState.overrideAppFocused = state.unfocusedInputFrames > 0;
    inputState.appFocused = !inputState.overrideAppFocused;
    inputState.hasMouseClientPosition = state.hasMouseClientPosition;
    inputState.mouseClientPosition = state.mouseClientPosition;
    inputState.leftMouseDown = state.leftMouseDown;
    inputState.rightMouseDown = state.rightMouseDown;
    inputState.keyVirtualKey = state.keyVirtualKey;
    inputState.keyDown = state.keyDown;
    Input::SetAutomationState( inputState );
    if ( state.unfocusedInputFrames > 0 )
    {
        --state.unfocusedInputFrames;
    }
    return result;
}

InteractionAutomationFrameResult
SkullbonezCore::Basics::TickInteractionAutomationAfterRender( InteractionAutomationController& state,
                                                              SceneController& scene,
                                                              RuntimeTools& runtimeTools,
                                                              ReplayRuntime& replayRuntime,
                                                              RuntimeInteractionController& interaction,
                                                              InputRouter& inputRouter,
                                                              RunCameraState& camera,
                                                              UI::InGameUI& ui,
                                                              CaptureController& capture,
                                                              Rendering::IRenderCaptureBackend& captureBackend )
{
    InteractionAutomationFrameResult result;
    if ( !state.enabled || state.finished )
    {
        return result;
    }

    RuntimeAllocation::RuntimeAllocationScope diagnosticsAllocationScope(
        RuntimeAllocation::RuntimeAllocationPhase::Diagnostics );
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
                const SbResult captureResult = capture.SaveScreenshot( captureBackend, action.path );
                if ( captureResult.ok )
                {
                    state.screenshots.emplace_back( action.path );
                    AppendReportAction( state, frame, action.type, action.path, nullptr, true, "screenshot saved" );
                }
                else
                {
                    const char* message = captureResult.error.message[0] != '\0' ? captureResult.error.message
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

        const InteractionAutomationAssertionEvaluation evaluation = EvaluateInteractionAutomationAssertion(
            runtimeTools,
            replayRuntime,
            interaction,
            inputRouter,
            camera,
            scene.Models(),
            scene.Entities(),
            ui,
            action,
            [&]()
            { return runtimeTools.InspectGizmoInteractionActive( camera.mode, replayRuntime.InspectionActive() ); } );

        strcpy_s( assertion.expected, sizeof( assertion.expected ), evaluation.expected.c_str() );
        strcpy_s( assertion.actual, sizeof( assertion.actual ), evaluation.actual.c_str() );
        assertion.passed = evaluation.passed;
        state.assertionReports.push_back( assertion );
        if ( !evaluation.passed )
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
        if ( !state.failed && replayRuntime.Prediction().build.building )
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
        const SbResult reportResult =
            WriteInteractionAutomationReport( state, scene, runtimeTools, replayRuntime, interaction, camera, ui );
        if ( result.status.ok )
        {
            result.status = reportResult;
        }
        result.requestQuit = true;
    }
    return result;
}

SbResult SkullbonezCore::Basics::WriteInteractionAutomationReport( InteractionAutomationController& state,
                                                                   const SceneController& scene,
                                                                   const RuntimeTools& runtimeTools,
                                                                   const ReplayRuntime& replayRuntime,
                                                                   const RuntimeInteractionController& interaction,
                                                                   const RunCameraState& camera,
                                                                   const UI::InGameUI& ui )
{
    RuntimeAllocation::RuntimeAllocationScope diagnosticsScope(
        RuntimeAllocation::RuntimeAllocationPhase::Diagnostics );
    if ( state.reportWritten )
    {
        return InteractionAutomationResult( state );
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

    const int selectedIndex = PeekSelectedEditorModelIndex( runtimeTools.Editor(), scene.Models().BodyStore() );
    const char* selectedName = "";
    if ( selectedIndex >= 0 && selectedIndex < scene.Models().SceneEntityCount() )
    {
        selectedName = scene.Entities().At( selectedIndex ).displayName;
    }
    const bool gizmoVisible =
        selectedIndex >= 0 &&
        ( runtimeTools.Editor().editorModeEnabled ||
          runtimeTools.InspectGizmoInteractionActive( camera.mode, replayRuntime.InspectionActive() ) );
    const bool replayPastPathVisible =
        replayRuntime.PathVisualizer().hasTarget && replayRuntime.PathVisualizer().pastPathVisible;
    const std::size_t predictionVisibleFrameCount = VisiblePredictionFrameCount( replayRuntime );
    const bool predictionPathVisible = ReplayPredictionPathVisible( replayRuntime );
    const bool predictionContactsIncomplete = ReplayPredictionContactsIncomplete( replayRuntime );
    uint64_t predictionSourceSolverHash = 0;
    uint64_t liveSolverHash = 0;
    const bool liveSolverHashStableAcrossPrediction =
        LiveSolverHashStableAcrossPrediction( replayRuntime, &predictionSourceSolverHash, &liveSolverHash );
    const float replaySolverTrackPosition = replayRuntime.TrackPosition( RunReplayTrack::Solver );
    const float replaySolverPresentTrackPosition = replayRuntime.SolverPresentTrackPosition();
    const bool replaySolverTrackAtPresent =
        ReplayRuntime::AtPresentTrackPosition( replaySolverTrackPosition, replaySolverPresentTrackPosition );
    const bool predictionScrubFrameActive = replayRuntime.CurrentPredictionScrubFrame() != nullptr;
    bool predictionTargetDisplacementValid = false;
    Vector3 predictionTargetFirst = ZERO_VECTOR;
    Vector3 predictionTargetLast = ZERO_VECTOR;
    float predictionTargetDisplacement = 0.0f;
    predictionTargetDisplacementValid = TryPredictionTargetDisplacement( replayRuntime,
                                                                         predictionTargetDisplacement,
                                                                         &predictionTargetFirst,
                                                                         &predictionTargetLast );
    const RunReplayPredictionState& predictionState = replayRuntime.Prediction();
    const ReplayPredictionBaselineSnapshot& predictionBaseline = predictionState.baseline;
    const bool predictionBaselineVisible = predictionBaseline.valid && predictionBaseline.comparisonActive;
    const PredictionTrajectoryFingerprint predictionTrajectoryFingerprint =
        BuildPredictionTrajectoryFingerprint( replayRuntime );
    const ReplayTrajectorySubmissionProbeStats& predictionSubmissionProbe =
        replayRuntime.ReplayTrajectorySubmissionProbe();
    std::size_t predictionRetainedEntryMarkerCount = 0;
    std::size_t predictionRetainedRestMarkerCount = 0;
    std::size_t predictionRetainedHorizonMarkerCount = 0;
    // Why: prediction visual regressions are often spatial, so the interaction
    // report records the retained marker inventory that backs screenshot proof.
    for ( std::size_t i = 0; i < predictionState.futureNodeCache.retainedMarkerCount; ++i )
    {
        const ReplayPredictionRetainedMarker& marker = predictionState.futureNodeCache.retainedMarkers[i];
        if ( marker.hasEntryPose )
        {
            ++predictionRetainedEntryMarkerCount;
        }
        if ( marker.hasRestPose )
        {
            ++predictionRetainedRestMarkerCount;
        }
        if ( marker.hasHorizonPose )
        {
            ++predictionRetainedHorizonMarkerCount;
        }
    }

    Json directorPhaseCameraEye = nullptr;
    Json directorPhaseCameraView = nullptr;
    Json directorPhaseCameraUp = nullptr;
    Json directorPhaseRevealRate = nullptr;
    const char* directorPhaseName = "";
    const char* directorPhaseStylePath = "";
    const DemoDirectorPlaybackState& director = camera.director;
    if ( director.hasActiveShotList && director.currentPhaseIndex >= 0 &&
         director.currentPhaseIndex < director.activeShotList.phaseCount )
    {
        const DemoPhase& phase = director.activeShotList.phases[static_cast<std::size_t>( director.currentPhaseIndex )];
        directorPhaseName = phase.name;
        directorPhaseStylePath = phase.stylePath;
        directorPhaseCameraEye = Vec3Json( phase.camera.eye );
        directorPhaseCameraView = Vec3Json( phase.camera.view );
        directorPhaseCameraUp = Vec3Json( phase.camera.up );
        directorPhaseRevealRate = phase.revealRate;
    }

    const std::string* scenePath = scene.CurrentPath();
    Json report;
    report["ok"] = !state.failed;
    report["scene"] = scenePath ? *scenePath : "";
    report["script"] = state.scriptPath;
    report["framesRun"] = scene.State().currentFrame;
    report["actions"] = actions;
    report["assertions"] = assertions;
    report["screenshots"] = screenshots;
    report["failure"] = state.failure;
    report["finalState"] = Json{
        { "cameraMode", CameraModeName( camera.mode ) },
        { "directorShotListLoaded", camera.director.hasActiveShotList },
        { "directorPhaseIndex", camera.director.currentPhaseIndex },
        { "directorPhaseCount", camera.director.activeShotList.phaseCount },
        { "directorGrabbed", camera.director.grabbed },
        { "directorShotListPath", camera.director.activeShotListPath },
        { "directorPhaseName", directorPhaseName },
        { "directorPhaseStylePath", directorPhaseStylePath },
        { "directorPhaseRevealRate", directorPhaseRevealRate },
        { "directorAppliedStylePhaseIndex", camera.director.appliedStylePhaseIndex },
        { "directorAppliedStylePath", camera.director.appliedStylePath },
        { "directorAppliedStyleCount", camera.director.appliedStyleCount },
        { "directorAppliedRevealRatePhaseIndex", camera.director.appliedRevealRatePhaseIndex },
        { "directorAppliedRevealRate", camera.director.appliedRevealRate },
        { "directorAppliedRevealRateCount", camera.director.appliedRevealRateCount },
        { "directorPhaseCameraEye", directorPhaseCameraEye },
        { "directorPhaseCameraView", directorPhaseCameraView },
        { "directorPhaseCameraUp", directorPhaseCameraUp },
        { "workspace", WorkspaceName( interaction.Workspace() ) },
        { "owner", OwnerName( interaction.Owner() ) },
        { "selectedObject", selectedName },
        { "selectedModelIndex", selectedIndex },
        { "gizmoVisible", gizmoVisible },
        { "mousePickupActive", interaction.Gesture().kind == RuntimeInteractionGestureKind::MousePickupDrag },
        { "launcherRayActive", runtimeTools.Laser().HasActiveShots() },
        { "memoryOverlayEnabled", ui.IsMemoryOverlayEnabled() },
        { "replayPredictionEnabled", predictionState.enabled },
        { "predictionHorizonSeconds", predictionState.simulation.horizonSeconds },
        { "predictionRevealSecondsPerSecond", predictionState.revealClock.secondsPerSecond },
        { "replayPathTarget",
          replayRuntime.PathVisualizer().hasTarget ? replayRuntime.PathVisualizer().targetName : "" },
        { "replayPathTargetCount", static_cast<int>( replayRuntime.PathVisualizer().targets.size() ) },
        { "replayPastPathVisible", replayPastPathVisible },
        { "predictionPathVisible", predictionPathVisible },
        { "predictionContactsIncomplete", predictionContactsIncomplete },
        { "predictionBaselineVisible", predictionBaselineVisible },
        { "predictionBaselineRootPointCount", static_cast<int>( predictionBaseline.rootPolyline.size() ) },
        { "predictionBaselineBodyPoseCount", static_cast<int>( predictionBaseline.bodyPoses.size() ) },
        { "predictionDivergenceValid", predictionBaseline.divergenceValid },
        { "predictionDivergenceUnits", predictionBaseline.divergenceUnits },
        { "liveSolverHashStableAcrossPrediction", liveSolverHashStableAcrossPrediction },
        { "predictionSourceSolverHash", predictionSourceSolverHash },
        { "liveSolverHash", liveSolverHash },
        { "predictionActiveFrameCount", static_cast<int>( predictionVisibleFrameCount ) },
        { "predictionFrameCount", static_cast<int>( predictionState.simulation.frames.size() ) },
        { "predictionBuildFrameCount", static_cast<int>( predictionState.PublishedBuildFrameCount() ) },
        { "predictionTargetDisplacementValid", predictionTargetDisplacementValid },
        { "predictionTargetFirst", Vec3Json( predictionTargetFirst ) },
        { "predictionTargetLast", Vec3Json( predictionTargetLast ) },
        { "predictionTargetDisplacement", predictionTargetDisplacement },
        { "predictionTrajectoryFingerprintReady", predictionTrajectoryFingerprint.Ready() },
        { "predictionTrajectoryFingerprint", FormatPredictionHash( predictionTrajectoryFingerprint.hash ) },
        { "predictionTrajectoryRecordCount", static_cast<int>( predictionTrajectoryFingerprint.recordCount ) },
        { "predictionTrajectoryPointCount", static_cast<int>( predictionTrajectoryFingerprint.pointCount ) },
        { "predictionTrajectorySubmissionStable", predictionSubmissionProbe.stableWindowReady },
        { "predictionTrajectorySubmissionFrameCount", predictionSubmissionProbe.stableFrameCount },
        { "predictionTrajectorySubmissionObservedFrameCount", predictionSubmissionProbe.observedFrameCount },
        { "predictionTrajectorySubmissionHash", FormatPredictionHash( predictionSubmissionProbe.stableHash ) },
        { "predictionTrajectorySubmissionVertexBytes", predictionSubmissionProbe.vertexBytes },
        { "predictionTrajectorySubmissionVertexCount", static_cast<int>( predictionSubmissionProbe.vertexCount ) },
        { "predictionTrajectorySubmissionSegmentCount", static_cast<int>( predictionSubmissionProbe.segmentCount ) },
        { "predictionTrajectorySubmissionFirstFrame", predictionSubmissionProbe.firstFrame },
        { "predictionTrajectorySubmissionLastFrame", predictionSubmissionProbe.lastFrame },
        { "predictionTrajectorySteadyStateNoReserveGrowth", predictionSubmissionProbe.noReserveGrowth },
        { "predictionTrajectoryReserveGrowthEventsAtStart", predictionSubmissionProbe.reserveGrowthEventsAtStart },
        { "predictionTrajectoryReserveGrowthEventsAtEnd", predictionSubmissionProbe.reserveGrowthEventsAtEnd },
        { "predictionFutureNodeCount", static_cast<int>( predictionState.futureNodeCache.futureNodes.size() ) },
        { "predictionFutureNodeBuildFrameCount",
          static_cast<int>( predictionState.futureNodeCache.futureNodesBuiltFrameCount ) },
        { "predictionRetainedEntryMarkerCount", static_cast<int>( predictionRetainedEntryMarkerCount ) },
        { "predictionRetainedRestMarkerCount", static_cast<int>( predictionRetainedRestMarkerCount ) },
        { "predictionRetainedHorizonMarkerCount", static_cast<int>( predictionRetainedHorizonMarkerCount ) },
        { "replayActiveTrack", ReplayTrackName( replayRuntime.Scrubber().activeTrack ) },
        { "replayHistoricalSamplePaused", replayRuntime.Scrubber().historicalSamplePaused },
        { "replaySolverTrackPosition", replaySolverTrackPosition },
        { "replaySolverPresentTrackPosition", replaySolverPresentTrackPosition },
        { "replaySolverTrackAtPresent", replaySolverTrackAtPresent },
        { "predictionScrubFrameActive", predictionScrubFrameActive },
        { "replayFutureNodeCount", static_cast<int>( replayRuntime.PathVisualizer().futureNodes.size() ) } };

    std::ofstream output;
    if ( !RuntimeFileWriter::OpenTextFile( state.reportPath, output ) )
    {
        state.reportWritten = true;
        state.failed = true;
        strcpy_s( state.failure, sizeof( state.failure ), "failed to open interaction report path" );
        return SbResult::Failure( "InteractionAutomation", state.failure );
    }
    output << report.dump( 2 ) << "\n";
    output.close();
    state.reportWritten = true;
    printf( "[interaction] Report written: %s ok=%d\n", state.reportPath, state.failed ? 0 : 1 );
    return InteractionAutomationResult( state );
}
