/*
File: SkullbonezSource/Runtime/InteractionAutomationController.cpp
Purpose:
  Drives deterministic runtime interaction scripts through the normal input path.

Summary:
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
  - Assertions and reports consume ReplayAutomationView; replay mutation uses
    named owner commands and never a mutable prediction/recorder reference.
  - Published samples are frame-local; this file must not retain their spans or
    pointers beyond the synchronous automation turn.

Related:
  - SkullbonezSource/Runtime/RuntimePickService.h
  - SkullbonezSource/Runtime/RuntimeInteractionController.h
  - SkullbonezSource/Runtime/Replay/ReplayCoordination.h
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
#include "Replay/ReplayPredictionArchive.h"
#include "Replay/ReplayV2Artifact.h"
#include "InputFrame.h"
#include "Allocation/RuntimeAllocationTracker.h"
#include "Editor/EditorTools.h"
#include "Replay/ReplayOverlayLayout.h"
#include "RunDemoDirector.h"
#include "RuntimeFileWriter.h"
#include "RuntimePickService.h"

#include "../Physics/PhysicsEngine.h"
#include "../Physics/PhysicsTimestep.h"
#include "../Core/Config.h"
#include "../Rendering/IRenderCaptureBackend.h"
#include "../UI/UI.h"

#pragma warning( push, 0 )
#include "../../ThirdPtySource/nlohmann/json.hpp"
#pragma warning( pop )

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::RunInternal;
using namespace SkullbonezCore::Runtime::ReplayOverlay;
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
// Invariant: the mega probe holds the reveal at zero through prediction build
// and begins presentation at one fixed scene frame. Worker completion speed
// must never decide when the operator sees the causal unfold begin.
constexpr int REPLAY_VISUAL_FIDELITY_START_FRAME = 900;

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

ReplayCausalProofTick BuildReplayCausalProofTick( const ReplayVisualPacket& packet )
{
    ReplayCausalProofTick tick;
    tick.revealFrame = packet.header.revealFrame;
    tick.activeTopologyHash = INTERACTION_PREDICTION_FINGERPRINT_OFFSET;

    for ( const RunReplayPathTraceNode& node : packet.futureNodes )
    {
        if ( node.firstFrame > tick.revealFrame )
        {
            continue;
        }
        HashPredictionScalar( tick.activeTopologyHash, node.id.value );
        HashPredictionScalar( tick.activeTopologyHash, node.parentId.value );
        HashPredictionScalar( tick.activeTopologyHash, node.firstFrame );
        HashPredictionScalar( tick.activeTopologyHash, node.depth );
        HashPredictionScalar( tick.activeTopologyHash, static_cast<uint8_t>( node.contactDerived ) );
        ++tick.activeNodeCount;
    }

    for ( const ReplayTrajectoryRecord& record : packet.trajectoryRecords )
    {
        if ( record.key.lane == ReplayTrajectoryLane::PastRoot || record.firstFrame > tick.revealFrame )
        {
            continue;
        }
        const std::size_t publishedPointCount = (std::min)( record.publishedPointCount, record.points.size() );
        if ( publishedPointCount == 0 )
        {
            continue;
        }

        // The V0 buffer oracle already hashes every submitted point and vertex.
        // This causal row records stable eligibility/count transitions instead
        // of hashing internal record publication order, which is deliberately
        // not a presentation contract.
        ++tick.revealedRecordCount;
        tick.revealedPointCount += static_cast<uint32_t>( publishedPointCount );
        tick.revealedSegmentCount += static_cast<uint32_t>( publishedPointCount > 0 ? publishedPointCount - 1 : 0 );
    }

    for ( const ReplayPredictionRetainedMarker& marker : packet.retainedMarkers )
    {
        tick.entryMarkerCount += marker.hasEntryPose ? 1u : 0u;
        tick.restMarkerCount += marker.hasRestPose ? 1u : 0u;
        tick.horizonMarkerCount += marker.hasHorizonPose ? 1u : 0u;
    }
    tick.ghostRequestCount = static_cast<uint32_t>( packet.ghostRequests.size() );
    return tick;
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

EditorSelectionFingerprint BuildEditorSelectionFingerprint( RuntimeTools& runtimeTools, SceneController& scene )
{
    EditorSelectionFingerprint fingerprint;
    const int modelIndex = PeekSelectedEditorModelIndex( runtimeTools.Editor(), scene.BodyStore() );
    if ( modelIndex < 0 || modelIndex >= scene.SceneEntityCount() )
    {
        return fingerprint;
    }
    const SceneEntityRecord& entity = scene.Entities().At( modelIndex );
    const Physics::PhysicsBodyRecord* body = scene.BodyStore().RecordForModelIndex( modelIndex );
    const Physics::PhysicsColliderHandle colliderHandle = scene.Colliders().HandleForModelIndex( modelIndex );
    const Physics::ColliderRecord* collider = scene.Colliders().RecordForHandle( colliderHandle );
    EditorPrimitiveShapeSnapshot shape;
    if ( !body || !collider || body->sceneObjectId.value != entity.sceneObjectId.value ||
         !TryCaptureEditorPrimitiveShape( collider->shape, shape ) )
    {
        return fingerprint;
    }

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

    HashPredictionVector( hash, body->position );
    float orientationX = 0.0f;
    float orientationY = 0.0f;
    float orientationZ = 0.0f;
    float orientationW = 1.0f;
    body->orientation.GetComponents( orientationX, orientationY, orientationZ, orientationW );
    HashPredictionFloat( hash, orientationX );
    HashPredictionFloat( hash, orientationY );
    HashPredictionFloat( hash, orientationZ );
    HashPredictionFloat( hash, orientationW );
    HashPredictionVector( hash, body->linearVelocity );
    HashPredictionVector( hash, body->angularVelocity );
    HashPredictionVector( hash, body->rotationalInertia );
    HashPredictionFloat( hash, body->mass );
    HashPredictionFloat( hash, body->boundingRadius );
    HashPredictionFloat( hash, body->volume );
    HashPredictionFloat( hash, body->projectedSurfaceArea );
    HashPredictionFloat( hash, body->dragCoefficient );
    HashPredictionFloat( hash, body->contactReleaseImpulseThreshold );
    HashPredictionFloat( hash, body->angularVelocityLimit );
    HashPredictionFloat( hash, body->contactEpsilon );
    HashPredictionScalar( hash, static_cast<uint8_t>( body->isFixed ) );
    HashPredictionScalar( hash, static_cast<uint8_t>( body->isSleeping ) );
    HashPredictionScalar( hash, static_cast<uint8_t>( body->releasesFromFixedOnContact ) );
    HashPredictionScalar( hash, static_cast<uint8_t>( body->usesWorldInertia ) );
    fingerprint.hasTerrain = body->terrain != nullptr;
    HashPredictionScalar( hash, static_cast<uint8_t>( fingerprint.hasTerrain ) );

    HashPredictionScalar( hash, static_cast<uint8_t>( shape.kind ) );
    HashPredictionVector( hash, shape.dimensions );
    HashPredictionVector( hash, shape.localPosition );
    HashPredictionFloat( hash, shape.dragCoefficient );
    HashPredictionFloat( hash, collider->restitution );
    HashPredictionFloat( hash, collider->friction );
    HashPredictionScalar( hash, collider->contactMaterialId );
    HashInteractionText( hash, collider->contactMaterialName, sizeof( collider->contactMaterialName ) );
    fingerprint.valid = true;
    return fingerprint;
}

std::string FormatPredictionHash( uint64_t hash )
{
    char buffer[24] = {};
    sprintf_s( buffer, sizeof( buffer ), "0x%016llX", static_cast<unsigned long long>( hash ) );
    return buffer;
}

PredictionTrajectoryFingerprint BuildPredictionTrajectoryFingerprint( const ReplayAutomationView& replay )
{
    PredictionTrajectoryFingerprint fingerprint;
    const ReplayTrajectoryStore& store = replay.prediction.trajectoryStore;
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

bool TryPredictionTargetDisplacement( const ReplayAutomationView& replay,
                                      float& outDisplacement,
                                      Vector3* outFirst = nullptr,
                                      Vector3* outLast = nullptr )
{
    // Concept: automation reports compare the first and last prediction sample
    // for the selected replay body. Missing target data is a clean "not ready",
    // not an error state for the running scene.
    const RunReplayPredictionState& prediction = replay.prediction;
    std::span<const RunReplayPredictionFrame> activePredictionFrames = replay.activePredictionFrames;
    std::size_t activeFrameCount = activePredictionFrames.size();
    if ( activeFrameCount < 2 && prediction.BuildPrefixShouldBePresented() )
    {
        activeFrameCount = prediction.PublishedBuildFrameCount();
        activePredictionFrames = { prediction.build.buildFrames.data(), activeFrameCount };
    }
    const ReplayBodyId targetId = replay.path.targetId;
    if ( targetId.value == 0 || activeFrameCount < 2 )
    {
        return false;
    }

    const RunReplayPredictionBodySample* first = FindPredictionBodyById( activePredictionFrames.front(), targetId );
    const RunReplayPredictionBodySample* last =
        FindPredictionBodyById( activePredictionFrames[activeFrameCount - 1], targetId );
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

std::size_t VisiblePredictionFrameCount( const ReplayAutomationView& replay )
{
    const RunReplayPredictionState& prediction = replay.prediction;
    const std::span<const RunReplayPredictionFrame> activePredictionFrames = replay.activePredictionFrames;
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

bool ReplayPredictionPathVisible( const ReplayAutomationView& replay )
{
    // Concept: long prediction jobs expose a populated build prefix before the
    // final frame vector is swapped in. Automation should agree with the overlay
    // and count that prefix as visible once it can draw at least one segment.
    return replay.path.hasTarget && ( !replay.path.futureNodes.empty() || VisiblePredictionFrameCount( replay ) >= 2 ||
                                      !replay.prediction.futureNodeCache.futureNodes.empty() );
}

std::size_t ReplayPastTrajectoryPublishedPointCount( const ReplayAutomationView& replay )
{
    // Concept: this is a structural performance/flicker probe. The selected
    // path must retain a published drawable prefix while its recorder ring
    // advances, independent of machine-specific frame timing.
    const RunReplayPathVisualizerState& visualizer = replay.path;
    for ( const ReplayTrajectoryRecord& record : replay.prediction.trajectoryStore.records )
    {
        if ( record.key.lane == ReplayTrajectoryLane::PastRoot && record.key.bodyId.value == visualizer.targetId.value )
        {
            return (std::min)( record.publishedPointCount, record.points.size() );
        }
    }
    return 0u;
}

bool ReplayPredictionContactsIncomplete( const ReplayAutomationView& replay )
{
    // Concept: automation reports should distinguish a valid root prediction
    // from a partial contact-derived tree, because contact reserve failures are
    // intentionally non-fatal to prediction drawing.
    const RunReplayPredictionState& prediction = replay.prediction;
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

bool LiveSolverHashStableAcrossPrediction( const ReplayAutomationView& replay,
                                           uint64_t* outSourceHash = nullptr,
                                           uint64_t* outLiveHash = nullptr )
{
    // Concept: prediction isolation proof. The source hash is captured before
    // the private prediction engine starts stepping; the live latest hash should
    // still match after prediction has produced visible frames.
    const ReplaySolverFrameSample* latest = replay.latestSolverSample;
    const uint64_t sourceHash = replay.prediction.simulation.sourceSolverHash;
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

const char* ReplayPredictionBuildModeName( ReplayPredictionBuildMode mode )
{
    switch ( mode )
    {
    case ReplayPredictionBuildMode::Instant:
        return "Instant";
    case ReplayPredictionBuildMode::Amortized:
        return "Amortized";
    case ReplayPredictionBuildMode::Undecided:
    default:
        return "Undecided";
    }
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
    if ( value == "F9" )
    {
        outVirtualKey = VK_F9;
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
    if ( value == "Tilde" )
    {
        outVirtualKey = VK_OEM_3;
        return true;
    }
    if ( value == "Period" )
    {
        // TEMPORARY DEBUG AUTHORING: lets screenshot automation exercise the
        // same full-scene look cycler as the physical '.' key.
        outVirtualKey = VK_OEM_PERIOD;
        return true;
    }
    if ( value == "Delete" )
    {
        outVirtualKey = VK_DELETE;
        return true;
    }
    if ( value == "Alt" )
    {
        outVirtualKey = VK_MENU;
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
    case RunInteractionAutomationActionType::ClickPoint:
        return "clickPoint";
    case RunInteractionAutomationActionType::ClickReplayControl:
        return "clickReplayControl";
    case RunInteractionAutomationActionType::ScrubReplaySolverTrack:
        return "scrubReplaySolverTrack";
    case RunInteractionAutomationActionType::SetReplayPredictionEnabled:
        return "setReplayPredictionEnabled";
    case RunInteractionAutomationActionType::SetReplayPredictionHorizonSeconds:
        return "setReplayPredictionHorizonSeconds";
    case RunInteractionAutomationActionType::BeginReplayVisualFidelityCapture:
        return "beginReplayVisualFidelityCapture";
    case RunInteractionAutomationActionType::SetReplayPathTarget:
        return "setReplayPathTarget";
    case RunInteractionAutomationActionType::NudgeReplayPathTargetVelocity:
        return "nudgeReplayPathTargetVelocity";
    case RunInteractionAutomationActionType::ShowReplayScrubber:
        return "showReplayScrubber";
    case RunInteractionAutomationActionType::PressKey:
        return "pressKey";
    case RunInteractionAutomationActionType::CaptureEditorSelectionState:
        return "captureEditorSelectionState";
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
    case RunInteractionAutomationAssertKind::ReplayPastTrajectoryFullRebuildCountMax:
        return "replayPastTrajectoryFullRebuildCountMax";
    case RunInteractionAutomationAssertKind::ReplayPastTrajectoryIncrementalTrimCountMin:
        return "replayPastTrajectoryIncrementalTrimCountMin";
    case RunInteractionAutomationAssertKind::ReplayPastTrajectoryPublishedPointCountMin:
        return "replayPastTrajectoryPublishedPointCountMin";
    case RunInteractionAutomationAssertKind::PredictionPathVisible:
        return "predictionPathVisible";
    case RunInteractionAutomationAssertKind::PredictionFullHorizonComplete:
        return "predictionFullHorizonComplete";
    case RunInteractionAutomationAssertKind::PredictionBuildMode:
        return "predictionBuildMode";
    case RunInteractionAutomationAssertKind::PredictionSupersededRestartCountMin:
        return "predictionSupersededRestartCountMin";
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


template <typename TrySetReplayPathTarget, typename SetWorldInteractionOwnerAfterTransition>
void ApplyInteractionAutomationReplayStateAction( InteractionAutomationController& state,
                                                  RunTimerState& timers,
                                                  ReplayFrameIntent& replayIntent,
                                                  const ReplayAutomationView& replay,
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
        PublishReplayScrubberVisibility( replayIntent, action.boolValue, timers.simulationTimer.GetTotalTime(), 5.0 );
        AppendReportAction( state, frame, action.type, "", nullptr, true, action.boolValue ? "visible" : "hidden" );
        break;
    case RunInteractionAutomationActionType::SetReplayPredictionEnabled:
        PublishReplayPredictionEnabled( replayIntent, action.boolValue );
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
        PublishReplayPredictionHorizon( replayIntent, horizonSeconds );
        std::ostringstream detail;
        detail << "prediction horizon set to " << horizonSeconds << "s";
        AppendReportAction( state, frame, action.type, "", nullptr, true, detail.str().c_str() );
        break;
    }
    case RunInteractionAutomationActionType::NudgeReplayPathTargetVelocity:
    {
        const Physics::PhysicsBodyStore& bodyStore = SkullbonezCore::Physics::PhysicsEngine::ReadBodies( physics );
        const Physics::PhysicsBodyHandle body =
            bodyStore.HandleForReplayBodyId( replay.path.targetId.value, replay.path.targetModelRow.value );
        const Physics::PhysicsBodyRecord* record = bodyStore.RecordForHandle( body );
        const bool hasTarget = replay.path.hasTarget && replay.path.targetId.value != 0;
        bool applied = false;
        if ( hasTarget && record )
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
                const Vector3 nextLinearVelocity = record->linearVelocity + action.vectorValue;
                applied = physics.SetBodyVelocity( body, nextLinearVelocity, record->angularVelocity, true );
                if ( applied )
                {
                    CommitReplayVelocityMutation( replayIntent );
                    PublishReplayScrubberVisibility( replayIntent,
                                                     true,
                                                     timers.simulationTimer.GetTotalTime(),
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

void ShowInteractionAutomationReplayScrubber( RunTimerState& timers, ReplayFrameIntent& replayIntent )
{
    PublishReplayScrubberVisibility( replayIntent,
                                     true,
                                     timers.simulationTimer.GetTotalTime(),
                                     REPLAY_SCRUBBER_VISIBLE_SECONDS );
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
                                                    ReplayFrameIntent& replayIntent,
                                                    RunInteractionAutomationAction& action,
                                                    int frame,
                                                    const SkullbonezCore::UI::UIRect& rect,
                                                    const char* detail )
{
    InjectAutomationLeftMousePress( state, action, frame, rect );
    ShowInteractionAutomationReplayScrubber( timers, replayIntent );
    AppendReportAction( state, frame, action.type, action.text, &action.mouse, true, detail );
}

void ApplyInteractionAutomationReplayControlClick( InteractionAutomationController& state,
                                                   Window* window,
                                                   const SkullbonezCore::Core::EngineConfig& config,
                                                   const RunSceneState& scene,
                                                   RunTimerState& timers,
                                                   ReplayFrameIntent& replayIntent,
                                                   const ReplayAutomationView& replay,
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
        const ReplayRecorderStats solverReplayStats = replay.solverStats;
        // Why: interaction scripts should match the real UI: Predict can branch
        // from the current live solver state even before a paused scene has
        // accumulated two retained solver samples.
        const bool predictionToolsEnabled = solverReplayStats.enabled && scene.isScenePhysics;
        if ( screenW > 0 && screenH > 0 && predictionToolsEnabled )
        {
            InjectInteractionAutomationReplayControlClick( state,
                                                           timers,
                                                           replayIntent,
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
        const ReplayRecorderStats solverReplayStats = replay.solverStats;
        const bool pastPathControlEnabled =
            solverReplayStats.enabled && solverReplayStats.sampleCount >= 2 && replay.path.hasTarget;
        if ( screenW > 0 && screenH > 0 && pastPathControlEnabled )
        {
            InjectInteractionAutomationReplayControlClick( state,
                                                           timers,
                                                           replayIntent,
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
        const ReplayRecorderStats solverReplayStats = replay.solverStats;
        const bool solverToolsEnabled = solverReplayStats.enabled && solverReplayStats.sampleCount >= 2;
        if ( screenW > 0 && screenH > 0 && solverToolsEnabled )
        {
            // Concept: the scrubber exposes one physical button whose label
            // flips between pause and play. Automation clicks the real rectangle
            // so replay input ownership does the state transition and
            // prediction-freeze work.
            InjectInteractionAutomationReplayControlClick( state,
                                                           timers,
                                                           replayIntent,
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
        const ReplayRecorderStats solverReplayStats = replay.solverStats;
        const bool solverToolsEnabled = solverReplayStats.enabled && solverReplayStats.sampleCount >= 2;
        if ( screenW > 0 && screenH > 0 && solverToolsEnabled )
        {
            // Concept: velocity automation toggles the visible scrubber control,
            // then lets the next scripted world click exercise replay velocity
            // targeting through normal input ownership.
            InjectInteractionAutomationReplayControlClick( state,
                                                           timers,
                                                           replayIntent,
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
        const ReplayRecorderStats solverReplayStats = replay.solverStats;
        const bool branchTargetAvailable =
            replay.scrubber.historicalSamplePaused && replay.scrubber.activeTrack == RunReplayTrack::Solver &&
            solverReplayStats.enabled && solverReplayStats.sampleCount >= 2 && replay.currentSolverSample != nullptr;
        if ( screenW > 0 && screenH > 0 && branchTargetAvailable )
        {
            // Why: branch-restore proof clicks the visible Branch rectangle
            // after a scripted scrub, so TickReplayScrubberInput remains the
            // owner of the restore.
            InjectInteractionAutomationReplayControlClick( state,
                                                           timers,
                                                           replayIntent,
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
                                                 const SkullbonezCore::Core::EngineConfig& config,
                                                 RunTimerState& timers,
                                                 ReplayFrameIntent& replayIntent,
                                                 const ReplayAutomationView& replay,
                                                 RunInteractionAutomationAction& action,
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
        InjectInteractionAutomationReplayControlClick( state,
                                                       timers,
                                                       replayIntent,
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

    if ( entry.contains( "loadShotList" ) )
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

    if ( entry.contains( "directorPlay" ) )
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
        if ( !entry["setPhaseStyle"].is_string() )
        {
            outError = "setPhaseStyle must be a string";
            return false;
        }
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
            outAction.button =
                button == "right" ? RunInteractionAutomationButton::Right : RunInteractionAutomationButton::Left;
        }
        if ( entry.contains( "holdFrames" ) )
        {
            outAction.holdFrames = (std::max)( 1, entry["holdFrames"].get<int>() );
        }
        return true;
    }

    if ( entry.contains( "clickPoint" ) )
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
        if ( !entry["loseFocus"].is_number_integer() )
        {
            outError = "loseFocus must be an integer frame count";
            return false;
        }
        outAction.type = RunInteractionAutomationActionType::LoseFocus;
        outAction.holdFrames = (std::max)( 1, entry["loseFocus"].get<int>() );
        return true;
    }

    if ( entry.contains( "moveMouse" ) )
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
        return true;
    }

    if ( entry.contains( "clickReplayControl" ) )
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

    if ( entry.contains( "scrubReplaySolverTrack" ) )
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

    if ( entry.contains( "setReplayPredictionEnabled" ) )
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

    if ( entry.contains( "setReplayPredictionHorizonSeconds" ) )
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

    if ( entry.contains( "beginReplayVisualFidelityCapture" ) )
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

    if ( entry.contains( "setReplayPathTarget" ) )
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
        if ( !IsBoolValue( entry["showReplayScrubber"] ) )
        {
            outError = "showReplayScrubber must be a boolean value";
            return false;
        }
        outAction.type = RunInteractionAutomationActionType::ShowReplayScrubber;
        outAction.boolValue = ReadBool( entry["showReplayScrubber"] );
        return true;
    }

    if ( entry.contains( "pressKey" ) )
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
        return true;
    }

    if ( entry.contains( "captureEditorSelectionState" ) )
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

    if ( entry.contains( "screenshot" ) )
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
        const Json& expected = member.value();
        // Invariant: JSON_NOEXCEPTION turns a mismatched get<T>() into an
        // abort, so the assertion vocabulary is classified before dispatch.
        const bool expectsString = name == "selectedObject" || name == "owner" || name == "cameraMode" ||
                                   name == "directorPhaseName" || name == "directorPhaseStylePath" ||
                                   name == "replayPathTarget" || name == "predictionBuildMode" ||
                                   name == "pointerCapture" || name == "replayActiveTrack";
        const bool expectsInteger = name == "directorPhaseIndex" || name == "editorUndoDepth" ||
                                    name == "editorRedoDepth" || name == "editorSelectionMatchesCapture";
        const bool expectsNumber = name == "replayPastTrajectoryFullRebuildCountMax" ||
                                   name == "replayPastTrajectoryIncrementalTrimCountMin" ||
                                   name == "replayPastTrajectoryPublishedPointCountMin" ||
                                   name == "predictionSupersededRestartCountMin" || name == "predictionDivergenceMin" ||
                                   name == "predictionTargetDisplacementMin";
        const bool expectsBool =
            name == "directorGrabbed" || name == "replayPredictionEnabled" || name == "predictionPathVisible" ||
            name == "predictionFullHorizonComplete" || name == "predictionBaselineVisible" ||
            name == "replaySolverTrackAtPresent" || name == "predictionScrubFrameActive" ||
            name == "liveSolverHashStableAcrossPrediction" || name == "predictionTrajectoryFingerprintReady" ||
            name == "gizmoVisible" || name == "mousePickupActive" || name == "nativeCaptureRequested" ||
            name == "cursorVisibleRequested" || name == "uiBlocksMouse" || name == "launcherRayActive" ||
            name == "replayHistoricalSamplePaused" || name == "memoryOverlayEnabled" ||
            name == "editorSelectionExists" || name == "editorSelectionHasTerrain";
        if ( ( expectsString && !expected.is_string() ) || ( expectsInteger && !expected.is_number_integer() ) ||
             ( expectsNumber && !expected.is_number() ) || ( expectsBool && !IsBoolValue( expected ) ) )
        {
            outError = "assertion field has the wrong value type: " + name;
            return false;
        }
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
        else if ( name == "replayPastTrajectoryFullRebuildCountMax" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::ReplayPastTrajectoryFullRebuildCountMax;
            outAction.numberValue = member.value().get<float>();
        }
        else if ( name == "replayPastTrajectoryIncrementalTrimCountMin" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::ReplayPastTrajectoryIncrementalTrimCountMin;
            outAction.numberValue = member.value().get<float>();
        }
        else if ( name == "replayPastTrajectoryPublishedPointCountMin" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::ReplayPastTrajectoryPublishedPointCountMin;
            outAction.numberValue = member.value().get<float>();
        }
        else if ( name == "predictionPathVisible" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::PredictionPathVisible;
            outAction.boolValue = ReadBool( member.value() );
        }
        else if ( name == "predictionFullHorizonComplete" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::PredictionFullHorizonComplete;
            outAction.boolValue = ReadBool( member.value() );
        }
        else if ( name == "predictionBuildMode" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::PredictionBuildMode;
            CopyText( outAction.text, sizeof( outAction.text ), member.value().get<std::string>() );
        }
        else if ( name == "predictionSupersededRestartCountMin" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::PredictionSupersededRestartCountMin;
            outAction.numberValue = member.value().get<float>();
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
        else if ( name == "editorUndoDepth" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::EditorUndoDepth;
            outAction.numberValue = static_cast<float>( member.value().get<int>() );
        }
        else if ( name == "editorRedoDepth" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::EditorRedoDepth;
            outAction.numberValue = static_cast<float>( member.value().get<int>() );
        }
        else if ( name == "editorSelectionExists" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::EditorSelectionExists;
            outAction.boolValue = ReadBool( member.value() );
        }
        else if ( name == "editorSelectionHasTerrain" )
        {
            outAction.assertKind = RunInteractionAutomationAssertKind::EditorSelectionHasTerrain;
            outAction.boolValue = ReadBool( member.value() );
        }
        else if ( name == "editorSelectionMatchesCapture" )
        {
            const int slot = member.value().get<int>();
            if ( slot < 0 || slot >= 2 )
            {
                outError = "editorSelectionMatchesCapture slot must be 0 or 1";
                return false;
            }
            outAction.assertKind = RunInteractionAutomationAssertKind::EditorSelectionMatchesCapture;
            outAction.numberValue = static_cast<float>( slot );
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
                                        const InteractionAutomationController& automation,
                                        const ReplayAutomationView& replay,
                                        RuntimeInteractionController& interaction,
                                        const InputRouter& inputRouter,
                                        RunCameraState& camera,
                                        SceneController& sceneController,
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
        const int selectedIndex = PeekSelectedEditorModelIndex( runtimeTools.Editor(), sceneController.BodyStore() );
        if ( selectedIndex >= 0 && selectedIndex < sceneController.SceneEntityCount() )
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
        evaluation.actual = BoolString( replay.prediction.enabled );
        evaluation.passed = replay.prediction.enabled == action.boolValue;
        break;
    case RunInteractionAutomationAssertKind::ReplayPathTarget:
        evaluation.expected = action.text;
        evaluation.actual = replay.path.hasTarget ? replay.path.targetName : "";
        evaluation.passed = evaluation.actual == evaluation.expected;
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
        const std::size_t pointCount = ReplayPastTrajectoryPublishedPointCount( replay );
        evaluation.expected = ">=" + std::to_string( static_cast<std::size_t>( action.numberValue ) );
        evaluation.actual = std::to_string( pointCount );
        evaluation.passed = pointCount >= static_cast<std::size_t>( action.numberValue );
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionPathVisible:
    {
        const bool visible = ReplayPredictionPathVisible( replay );
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( visible );
        evaluation.passed = visible == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionFullHorizonComplete:
    {
        const RunReplayPredictionState& prediction = replay.prediction;
        const std::size_t expectedFrameCount =
            static_cast<std::size_t>( std::ceil( prediction.simulation.horizonSeconds / PHYSICS_FIXED_DT ) ) + 1u;
        const bool complete = prediction.build.complete && !prediction.build.building &&
                              prediction.simulation.frames.size() == expectedFrameCount;
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( complete );
        evaluation.passed = complete == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionBuildMode:
    {
        const char* actualMode = ReplayPredictionBuildModeName( replay.prediction.build.buildMode );
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
        const bool valid = TryPredictionTargetDisplacement( replay, displacement );
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
        const bool stable = LiveSolverHashStableAcrossPrediction( replay );
        evaluation.expected = BoolString( action.boolValue );
        evaluation.actual = BoolString( stable );
        evaluation.passed = stable == action.boolValue;
        break;
    }
    case RunInteractionAutomationAssertKind::PredictionTrajectoryFingerprintReady:
    {
        const PredictionTrajectoryFingerprint fingerprint = BuildPredictionTrajectoryFingerprint( replay );
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
        evaluation.actual = ReplayTrackName( replay.scrubber.activeTrack );
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
                                                 ? runtimeTools.Editor().history.UndoDepth()
                                                 : runtimeTools.Editor().history.RedoDepth() );
        const int expected = static_cast<int>( action.numberValue );
        evaluation.expected = std::to_string( expected );
        evaluation.actual = std::to_string( actual );
        evaluation.passed = actual == expected;
        break;
    }
    case RunInteractionAutomationAssertKind::EditorSelectionExists:
    case RunInteractionAutomationAssertKind::EditorSelectionHasTerrain:
    {
        const EditorSelectionFingerprint fingerprint = BuildEditorSelectionFingerprint( runtimeTools, sceneController );
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
        const EditorSelectionFingerprint fingerprint = BuildEditorSelectionFingerprint( runtimeTools, sceneController );
        evaluation.expected = automation.editorSelectionCaptureValid[slot]
                                  ? FormatPredictionHash( automation.editorSelectionCaptureFingerprints[slot] )
                                  : "valid capture";
        evaluation.actual = fingerprint.valid ? FormatPredictionHash( fingerprint.hash ) : "no selection";
        evaluation.passed = automation.editorSelectionCaptureValid[slot] && fingerprint.valid &&
                            fingerprint.hash == automation.editorSelectionCaptureFingerprints[slot];
        break;
    }
    }
    return evaluation;
}

ReplayVisualArchiveSample BuildReplayVisualArchiveSample( const ReplayVisualFidelityReportTick& tick )
{
    ReplayVisualArchiveSample packet;
    packet.sourceFrame = tick.sourceFrame;
    packet.revealFrame = tick.revealFrame;
    packet.semanticHash = tick.semanticHash;
    packet.visualStateHash = tick.visualStateHash;
    packet.exactPacketHash = tick.exactPacketHash;
    packet.schemaVersion = tick.schemaVersion;
    packet.targetId = tick.targetId;
    packet.branchId = tick.branchId;
    packet.eventCursor = tick.eventCursor;
    packet.topologyVersion = tick.topologyVersion;
    packet.publishedFrameCount = tick.publishedFrameCount;
    packet.predictionEnabled = tick.predictionEnabled ? 1u : 0u;
    packet.predictionBuilding = tick.predictionBuilding ? 1u : 0u;
    packet.predictionComplete = tick.predictionComplete ? 1u : 0u;
    packet.cameraEye = tick.cameraEye;
    packet.cameraUp = tick.cameraUp;
    packet.combinedLineHash = tick.combinedLineHash;
    packet.ordinaryLineHash = tick.ordinaryLineHash;
    packet.priorityLineHash = tick.priorityLineHash;
    packet.priorityLineCanonicalHash = tick.priorityLineCanonicalHash;
    packet.ordinaryRibbonHash = tick.ordinaryRibbonHash;
    packet.priorityRibbonHash = tick.priorityRibbonHash;
    packet.priorityRibbonCanonicalHash = tick.priorityRibbonCanonicalHash;
    packet.expandedVertexHash = tick.vertexHash;
    packet.ordinaryExpandedVertexHash = tick.ordinaryVertexHash;
    packet.droppedSegmentCount = tick.droppedSegmentCount;
    packet.replayReserveGrowthEvents = tick.replayReserveGrowthEvents;
    packet.combinedLineBytes = tick.combinedLineBytes;
    packet.ordinaryLineBytes = tick.ordinaryLineBytes;
    packet.priorityLineBytes = tick.priorityLineBytes;
    packet.ordinaryRibbonBytes = tick.ordinaryRibbonBytes;
    packet.priorityRibbonBytes = tick.priorityRibbonBytes;
    packet.expandedVertexBytes = tick.vertexBytes;
    packet.ordinaryExpandedVertexBytes = tick.ordinaryVertexBytes;
    packet.hasGeometry = tick.hasGeometry ? 1u : 0u;
    packet.trajectoryRecordCount = tick.trajectoryRecordCount;
    packet.futureNodeCount = tick.futureNodeCount;
    packet.retainedMarkerCount = tick.retainedMarkerCount;
    packet.ghostRequestCount = tick.ghostRequestCount;
    packet.combinedLineVertexCount = tick.combinedLineVertexCount;
    packet.ordinaryLineVertexCount = tick.ordinaryLineVertexCount;
    packet.priorityLineVertexCount = tick.priorityLineVertexCount;
    packet.ordinaryRibbonSegmentCount = tick.ordinaryRibbonSegmentCount;
    packet.priorityRibbonSegmentCount = tick.priorityRibbonSegmentCount;
    packet.expandedVertexCount = tick.vertexCount;
    packet.ordinaryExpandedVertexCount = tick.ordinaryVertexCount;
    packet.segmentCount = tick.segmentCount;
    return packet;
}

bool VerifyReplayVisualOfflineProjection( InteractionAutomationController& state,
                                          RuntimeFrameInteractionView& interactionOwners,
                                          RuntimeFrameSceneView& sceneOwners,
                                          const ReplaySolverFrameSample* latestSolverSample )
{
    if ( state.replayVisualOfflineProjectionComplete )
    {
        return true;
    }
    if ( state.replayVisualPredictionArchive.empty() || state.replayVisualFidelityTicks.empty() )
    {
        FailAutomation( state, "replay visual offline projection has no frozen prediction or RVIS rows" );
        return false;
    }

    ReplayPrediction offlinePrediction;
    ReplayPresentation offlinePresentation;
    offlinePrediction.EnterOfflineVerification();
    offlinePresentation.ResetTrajectoryVisualStats();
    char archiveReason[192] = {};
    RunReplayPathVisualizerState archivePath;
    if ( !offlinePrediction.LoadArchive( state.replayVisualPredictionArchive,
                                         archivePath,
                                         archiveReason,
                                         sizeof( archiveReason ) ) )
    {
        char message[320] = {};
        sprintf_s( message,
                   sizeof( message ),
                   "replay visual offline projection rejected RVPD: %s",
                   archiveReason[0] != '\0' ? archiveReason : "unknown archive failure" );
        FailAutomation( state, message );
        return false;
    }
    offlinePresentation.ApplyArchivePathState( archivePath );

    // The archived value retains the final marker prefix. CPU projection starts
    // at reveal zero and rebuilds first appearance exactly as the sole presented
    // run did. No renderer/backend method is reachable from this function.
    offlinePresentation.ResetTrajectoryVisualStats();
    offlinePrediction.ResetVerificationMarkers();
    RuntimeTools& runtimeTools = interactionOwners.runtimeTools;
    RunEditorTracer& tracer = runtimeTools.EditorTracer();
    SceneController& scene = sceneOwners.sceneController;
    std::vector<ReplayVisualTrajectoryDigestState> trajectoryDigests;
    trajectoryDigests.reserve( offlinePrediction.State().trajectoryStore.RecordCount() );
    for ( const ReplayVisualFidelityReportTick& tick : state.replayVisualFidelityTicks )
    {
        const ReplayVisualArchiveSample expected = BuildReplayVisualArchiveSample( tick );
        offlinePrediction.SetVerificationRevealFrame( expected.revealFrame );
        tracer.Clear();
        const RunReplayPathVisualizerState& path = offlinePresentation.PathVisualizer();
        ReplayPredictionUpdateResult update;
        offlinePrediction.PreparePresentation( scene.Entities(),
                                               Physics::PhysicsEngine::ReadColliders( scene.Physics() ),
                                               path.targetId,
                                               path.targetModelRow,
                                               path.hasTarget,
                                               5.0,
                                               update );
        if ( update.targetModelRowRepaired )
        {
            offlinePresentation.SetPathTargetModelRow( update.repairedTargetModelRow );
        }
        for ( std::size_t passIndex = 0; passIndex < update.budgetExpiries.size(); ++passIndex )
        {
            for ( uint32_t count = 0; count < update.budgetExpiries[passIndex]; ++count )
            {
                offlinePresentation.RecordTrajectoryBudgetExpiry(
                    static_cast<SkullbonezCore::Core::MainMemoryReplayBudgetPass>( passIndex ) );
            }
        }
        for ( std::size_t causeIndex = 0; causeIndex < update.rebuildCauses.size(); ++causeIndex )
        {
            for ( uint32_t count = 0; count < update.rebuildCauses[causeIndex]; ++count )
            {
                offlinePresentation.RecordTrajectoryRebuildCause(
                    static_cast<SkullbonezCore::Core::MainMemoryReplayRebuildCause>( causeIndex ) );
            }
        }
        offlinePresentation.PreparePathDrawing( scene.BodyStore() );
        const ReplayPredictionPresentationView prediction = offlinePrediction.PresentationView();
        offlinePresentation.RenderPathVisualizer( prediction,
                                                  latestSolverSample,
                                                  scene.Physics(),
                                                  scene.Entities(),
                                                  tracer );
        (void)offlinePresentation.BuildPredictionGhostDrawRequests( prediction,
                                                                    scene.RenderPresentationRecords(),
                                                                    scene.BodyStore() );
        ReplayVisualPacket projected = tracer.BuildReplayVisualPacket( expected.cameraEye, expected.cameraUp );
        offlinePresentation.PublishVisualPacket( projected,
                                                 prediction,
                                                 latestSolverSample,
                                                 expected.replayReserveGrowthEvents );
        projected = offlinePresentation.PublishedVisualPacketView();
        const ReplayVisualPacketFingerprint fingerprint =
            BuildReplayVisualPacketFingerprint( projected, trajectoryDigests );
        char difference[192] = {};
        if ( !ReplayVisualPacketMatchesArchiveSample( projected, expected, difference, sizeof( difference ) ) )
        {
            FailAutomation( state, difference );
            return false;
        }
        const auto laneHashMatches = [&]( const char* lane, uint64_t expectedHash, uint64_t actualHash )
        {
            if ( expectedHash == actualHash )
            {
                return true;
            }
            char message[320] = {};
            sprintf_s( message,
                       sizeof( message ),
                       "replay visual offline projection diverged at reveal %llu %s "
                       "expected=0x%016llX actual=0x%016llX",
                       static_cast<unsigned long long>( expected.revealFrame ),
                       lane,
                       static_cast<unsigned long long>( expectedHash ),
                       static_cast<unsigned long long>( actualHash ) );
            FailAutomation( state, message );
            return false;
        };
        if ( !laneHashMatches( "headerStateHash", tick.headerStateHash, fingerprint.headerStateHash ) ||
             !laneHashMatches( "trajectoryStateHash", tick.trajectoryStateHash, fingerprint.trajectoryStateHash ) ||
             !laneHashMatches( "topologyStateHash", tick.topologyStateHash, fingerprint.topologyStateHash ) ||
             !laneHashMatches( "markerStateHash", tick.markerStateHash, fingerprint.markerStateHash ) ||
             !laneHashMatches( "ghostStateHash", tick.ghostStateHash, fingerprint.ghostStateHash ) )
        {
            return false;
        }
        if ( fingerprint.visualStateHash != expected.visualStateHash ||
             fingerprint.exactHash != expected.exactPacketHash )
        {
            char message[320] = {};
            sprintf_s( message,
                       sizeof( message ),
                       "replay visual offline projection diverged at reveal %llu hashes "
                       "visual=0x%016llX/0x%016llX exact=0x%016llX/0x%016llX",
                       static_cast<unsigned long long>( expected.revealFrame ),
                       static_cast<unsigned long long>( expected.visualStateHash ),
                       static_cast<unsigned long long>( fingerprint.visualStateHash ),
                       static_cast<unsigned long long>( expected.exactPacketHash ),
                       static_cast<unsigned long long>( fingerprint.exactHash ) );
            FailAutomation( state, message );
            return false;
        }
    }

    // Leave the operator-visible enabled bit unchanged. The gate exits after
    // this offline pass; the one-way generation capability guarantees these
    // retained values cannot start another prediction.
    state.replayVisualOfflineProjectionComplete = true;
    return true;
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

    Json root = Json::parse( input, nullptr, false );
    if ( root.is_discarded() )
    {
        FailAutomation( state, "failed to parse interaction script: invalid JSON" );
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
                    request.bodyStore = &scene.BodyStore();
                    request.colliderStore = &scene.Colliders();
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
    state.leftMouseDown = false;
    state.rightMouseDown = false;
    state.keyVirtualKey = 0;
    state.keyDown = false;
    state.controlDown = false;
    state.releaseLeftFrame = -1;
    state.releaseRightFrame = -1;
    state.releaseKeyFrame = -1;
    state.unfocusedInputFrames = 0;
    for ( int slot = 0; slot < 2; ++slot )
    {
        state.editorSelectionCaptureFingerprints[slot] = 0;
        state.editorSelectionCaptureValid[slot] = false;
    }
    Input::ClearAutomationState();
}


SkullbonezCore::Core::SbResult
SkullbonezCore::Runtime::ConfigureInteractionAutomation( InteractionAutomationController& state,
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
        return SkullbonezCore::Core::SbResult::Failure( "InteractionAutomation", state.failure );
    }
    strcpy_s( state.scriptPath, sizeof( state.scriptPath ), scriptPath );
    state.enabled = true;
    printf( "[interaction] Script: %s\n", state.scriptPath );
    printf( "[interaction] Report: %s\n", state.reportPath );
    return SkullbonezCore::Core::SbResult::Success();
}


SkullbonezCore::Core::SbResult
SkullbonezCore::Runtime::InteractionAutomationResult( const InteractionAutomationController& state )
{
    if ( !state.failed )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }
    const char* message = state.failure[0] != '\0' ? state.failure : "interaction automation failed";
    return SkullbonezCore::Core::SbResult::Failure( "InteractionAutomation", message );
}

InteractionAutomationFrameResult
SkullbonezCore::Runtime::TickInteractionAutomationBeforeInput( InteractionAutomationController& state,
                                                               RuntimeFrameHostView& host,
                                                               RuntimeFrameInteractionView& interactionOwners,
                                                               RuntimeFrameSceneView& sceneOwners,
                                                               const ReplayAutomationView& replayView )
{
    Window* window = &host.window;
    const SkullbonezCore::Core::EngineConfig& config = sceneOwners.config;
    SceneController& scene = sceneOwners.sceneController;
    RunTimerState& timers = sceneOwners.timers;
    RunCameraState& camera = interactionOwners.camera;
    InputRouter& inputRouter = interactionOwners.inputRouter;
    RuntimeInteractionController& interaction = interactionOwners.interaction;
    RuntimeTools& runtimeTools = interactionOwners.runtimeTools;
    UI::InGameUI& ui = interactionOwners.ui;
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
        // report writer is another Lane R boundary, so it also cannot replace
        // the earlier script failure if both operations fail.
        result.status = InteractionAutomationResult( state );
        const SkullbonezCore::Core::SbResult reportResult =
            WriteInteractionAutomationReport( state, scene, runtimeTools, replayView, interaction, camera, ui );
        if ( result.status.ok )
        {
            result.status = reportResult;
        }
        result.requestQuit = true;
        return result;
    }

    const int frame = scene.State().currentFrame;
    if ( state.replayVisualFidelityCaptureEnabled )
    {
        // Invariant: the mega probe is one presented cascade. Advancing the
        // authoritative scene after the reveal would show a second, unrelated
        // wall fall and make a visually broken run appear to be test coverage.
        if ( replayInput.liveAdvanceHeld )
        {
            FailAutomation( state, "replay visual fidelity probe entered a second live playback pass" );
        }
        if ( state.replayVisualFidelityStartFrame < 0 && frame == REPLAY_VISUAL_FIDELITY_START_FRAME )
        {
            if ( !ReplayDeterministicRevealReady( replayView ) )
            {
                FailAutomation( state,
                                "replay visual fidelity prediction was not fully published before fixed reveal start" );
            }
            else
            {
                state.replayVisualFidelityStartFrame = frame;
                PublishReplayDeterministicReveal( result.replayIntent, 0, true );
            }
        }
        else if ( state.replayVisualFidelityStartFrame < 0 && frame > REPLAY_VISUAL_FIDELITY_START_FRAME )
        {
            FailAutomation( state, "replay visual fidelity missed its fixed reveal start frame" );
        }
        if ( state.replayVisualFidelityStartFrame >= 0 )
        {
            PublishReplayDeterministicReveal(
                result.replayIntent,
                static_cast<ReplayFrameIndex>( (std::max)( 0, frame - state.replayVisualFidelityStartFrame ) ),
                false );
        }
    }
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
        state.controlDown = false;
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
            result.applyCameraMode = true;
            result.cameraMode = targetMode;
            const bool applied = true;
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
                result.replayIntent,
                replayView,
                scene.Physics(),
                action,
                frame,
                [&]( const char* name )
                {
                    int modelIndex = -1;
                    if ( !TryFindInteractionAutomationModel( scene, name, modelIndex ) )
                    {
                        return false;
                    }
                    const Physics::PhysicsBodyRecord* body = scene.BodyStore().RecordForModelIndex( modelIndex );
                    if ( !body || body->replayBodyId == 0 )
                    {
                        return false;
                    }
                    result.replayIntent.setPathTarget = true;
                    result.replayIntent.pathTargetId.value = body->replayBodyId;
                    result.replayIntent.pathTargetModelRow.value = modelIndex;
                    strncpy_s( result.replayIntent.pathTargetName,
                               sizeof( result.replayIntent.pathTargetName ),
                               name,
                               _TRUNCATE );
                    return true;
                },
                [&]( WorldInteractionOwner owner, InteractionExitReason reason )
                {
                    result.setWorldInteractionOwner = true;
                    result.worldInteractionOwner = owner;
                    result.worldInteractionReason = reason;
                } );
            action.processed = true;
            break;
        case RunInteractionAutomationActionType::BeginReplayVisualFidelityCapture:
        {
            state.replayVisualFidelityCaptureEnabled = true;
            state.replayVisualFidelityStartFrame = -1;
            state.replayVisualFidelityTicks.clear();
            state.replayVisualFidelityTicks.reserve(
                static_cast<std::size_t>( REPLAY_FUTURE_BUFFER_SECONDS / PHYSICS_FIXED_DT ) + 2u );
            state.replayCausalProofTicks.clear();
            state.replayCausalProofTicks.reserve(
                static_cast<std::size_t>( REPLAY_FUTURE_BUFFER_SECONDS / PHYSICS_FIXED_DT ) + 2u );
            state.replayCausalTopology.clear();
            state.replayVisualFidelityTrajectoryHash = 0;
            state.replayVisualFidelityTrajectoryRecordCount = 0;
            state.replayVisualFidelityTrajectoryPointCount = 0;
            state.replayVisualFidelityTrajectoryCaptured = false;
            state.replayVisualOfflineProjectionComplete = false;
            state.replayVisualTrajectoryDigests.clear();
            state.replayVisualPredictionArchive.clear();
            // Invariant: the script arms this hold before target/horizon setup
            // and the sole Predict click. Letting wall-clock reveal run first
            // would retain markers, then rewinding to zero would create a
            // broken second presentation pass.
            PublishReplayDeterministicReveal( result.replayIntent, 0, true );
            AppendReportAction( state,
                                frame,
                                action.type,
                                "prediction",
                                nullptr,
                                true,
                                "reveal held at zero; frame-exact capture starts after prediction publication" );
            action.processed = true;
            break;
        }
        case RunInteractionAutomationActionType::PressKey:
            // Why: key automation should still enter through Input and
            // RuntimeInputContext edge detection. This only supplies the
            // virtual-key state that a real keyboard would have provided.
            state.keyVirtualKey = action.keyVirtualKey;
            state.keyDown = true;
            state.controlDown = action.boolValue;
            state.releaseKeyFrame = frame + 1;
            AppendReportAction( state, frame, action.type, action.text, nullptr, true, "key press injected" );
            action.processed = true;
            break;
        case RunInteractionAutomationActionType::CaptureEditorSelectionState:
        {
            const int slot = static_cast<int>( action.numberValue );
            const EditorSelectionFingerprint fingerprint = BuildEditorSelectionFingerprint( runtimeTools, scene );
            state.editorSelectionCaptureFingerprints[slot] = fingerprint.hash;
            state.editorSelectionCaptureValid[slot] = fingerprint.valid;
            if ( !fingerprint.valid )
            {
                FailAutomation( state, "failed to capture editor selection state" );
            }
            char detail[128] = {};
            sprintf_s( detail,
                       sizeof( detail ),
                       "slot=%d fingerprint=%s terrain=%d",
                       slot,
                       FormatPredictionHash( fingerprint.hash ).c_str(),
                       fingerprint.hasTerrain ? 1 : 0 );
            AppendReportAction( state, frame, action.type, "selection", nullptr, fingerprint.valid, detail );
            action.processed = true;
            break;
        }
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
                                                          result.replayIntent,
                                                          replayView,
                                                          action,
                                                          frame );
            action.processed = true;
            break;
        case RunInteractionAutomationActionType::ScrubReplaySolverTrack:
            ApplyInteractionAutomationSolverTrackScrub( state,
                                                        window,
                                                        config,
                                                        timers,
                                                        result.replayIntent,
                                                        replayView,
                                                        action,
                                                        frame );
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
        case RunInteractionAutomationActionType::ClickPoint:
            state.mouseClientPosition = action.mouse;
            state.hasMouseClientPosition = true;
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
            AppendReportAction( state, frame, action.type, nullptr, &action.mouse, true, "mouse press injected" );
            action.processed = true;
            break;
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
    // Automation owns a synthetic device frame, including focus. Requiring the
    // desktop foreground window would make a deterministic CLI probe depend on
    // which application the operator touched; LoseFocus remains the explicit
    // script-controlled negative lane.
    inputState.overrideAppFocused = true;
    inputState.appFocused = state.unfocusedInputFrames == 0;
    inputState.hasMouseClientPosition = state.hasMouseClientPosition;
    inputState.mouseClientPosition = state.mouseClientPosition;
    inputState.leftMouseDown = state.leftMouseDown;
    inputState.rightMouseDown = state.rightMouseDown;
    inputState.keyVirtualKey = state.keyVirtualKey;
    inputState.keyDown = state.keyDown;
    inputState.controlDown = state.controlDown;
    Input::SetAutomationState( inputState );
    if ( state.unfocusedInputFrames > 0 )
    {
        --state.unfocusedInputFrames;
    }
    return result;
}

InteractionAutomationFrameResult
SkullbonezCore::Runtime::TickInteractionAutomationAfterRender( InteractionAutomationController& state,
                                                               RuntimeFrameInteractionView& interactionOwners,
                                                               RuntimeFrameSceneView& sceneOwners,
                                                               const ReplayAutomationView& replayView,
                                                               CaptureController& capture,
                                                               Rendering::IRenderCaptureBackend& captureBackend )
{
    SceneController& scene = sceneOwners.sceneController;
    RuntimeTools& runtimeTools = interactionOwners.runtimeTools;
    RuntimeInteractionController& interaction = interactionOwners.interaction;
    InputRouter& inputRouter = interactionOwners.inputRouter;
    RunCameraState& camera = interactionOwners.camera;
    UI::InGameUI& ui = interactionOwners.ui;
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
                const SkullbonezCore::Core::SbResult captureResult =
                    capture.SaveScreenshot( captureBackend, action.path );
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
            state,
            replayView,
            interaction,
            inputRouter,
            camera,
            scene,
            scene.Entities(),
            ui,
            action,
            [&]()
            { return runtimeTools.InspectGizmoInteractionActive( camera.mode, replayView.input.inspectionActive ); } );

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

    if ( state.replayVisualFidelityCaptureEnabled && replayView.prediction.build.generationBeginCount > 1u )
    {
        // Probe assertion lane: setup changes must be coalesced before the
        // sole Predict action. Stop immediately if a later target, horizon, or
        // dirty event starts a replacement future, even before reveal begins.
        FailAutomation( state, "replay visual fidelity attempted a duplicate prediction generation" );
        state.replayVisualFidelityCaptureEnabled = false;
        return result;
    }
    if ( state.replayVisualFidelityCaptureEnabled && state.replayVisualFidelityStartFrame >= 0 &&
         state.replayVisualFidelityTicks.size() < replayView.prediction.simulation.frames.size() )
    {
        const SkullbonezCore::Core::MainMemoryReplayTrajectorySubmissionStats& submission =
            replayView.visualPacket.submission;
        const ReplayVisualPacket& packet = replayView.visualPacket;
        const ReplayVisualPacketFingerprint packetFingerprint =
            BuildReplayVisualPacketFingerprint( packet, state.replayVisualTrajectoryDigests );
        const ReplayVisualPacketBufferFacts bufferFacts = BuildReplayVisualPacketBufferFacts( packet );
        ReplayVisualFidelityReportTick tick;
        tick.sceneFrame = frame;
        tick.revealFrame = replayView.prediction.revealClock.presentedFrame;
        tick.sourceFrame = packet.header.sourceFrame;
        tick.semanticHash = packetFingerprint.semanticHash;
        tick.headerStateHash = packetFingerprint.headerStateHash;
        tick.trajectoryStateHash = packetFingerprint.trajectoryStateHash;
        tick.topologyStateHash = packetFingerprint.topologyStateHash;
        tick.markerStateHash = packetFingerprint.markerStateHash;
        tick.ghostStateHash = packetFingerprint.ghostStateHash;
        tick.visualStateHash = packetFingerprint.visualStateHash;
        tick.exactPacketHash = packetFingerprint.exactHash;
        tick.schemaVersion = packet.header.schemaVersion;
        tick.targetId = packet.header.targetId.value;
        tick.branchId = packet.header.branchId;
        tick.eventCursor = packet.header.eventCursor;
        tick.topologyVersion = packet.header.topologyVersion;
        tick.publishedFrameCount = packet.header.publishedFrameCount;
        tick.predictionEnabled = packet.header.predictionEnabled;
        tick.predictionBuilding = packet.header.predictionBuilding;
        tick.predictionComplete = packet.header.predictionComplete;
        tick.cameraEye = packet.header.cameraEye;
        tick.cameraUp = packet.header.cameraUp;
        tick.trajectoryRecordCount = static_cast<uint32_t>( packet.trajectoryRecords.size() );
        tick.futureNodeCount = packet.header.futureNodeCount;
        tick.retainedMarkerCount = static_cast<uint32_t>( packet.retainedMarkers.size() );
        tick.ghostRequestCount = packet.header.ghostRequestCount;
        tick.replayReserveGrowthEvents = packet.header.replayReserveGrowthEvents;
        tick.hasGeometry = bufferFacts.hasGeometry;
        tick.combinedLineHash = bufferFacts.combinedLineHash;
        tick.combinedLineBytes = bufferFacts.combinedLineBytes;
        tick.combinedLineVertexCount = static_cast<uint32_t>( packet.combinedLines.size() / 6u );
        for ( uint64_t dropped : packet.trajectoryDiagnostics.droppedSegments )
        {
            tick.droppedSegmentCount += dropped;
        }
        if ( packet.header.futureNodeCount != packet.futureNodes.size() ||
             packet.header.ghostRequestCount != packet.ghostRequests.size() )
        {
            FailAutomation( state, "replay visual packet header/span count mismatch" );
        }
        if ( const char* mismatch = FindReplayVisualPacketSubmissionSpanMismatch( packet ) )
        {
            char message[256] = {};
            sprintf_s( message, sizeof( message ), "replay visual packet/submission mismatch: %s", mismatch );
            FailAutomation( state, message );
        }
        // Invariant: there is one reveal generation. A reset, duplicate, or
        // skipped presentation tick is a probe failure even when later buffers
        // happen to converge to the approved final image.
        const uint64_t expectedRevealFrame = static_cast<uint64_t>( state.replayVisualFidelityTicks.size() );
        if ( tick.revealFrame != expectedRevealFrame )
        {
            char message[256] = {};
            sprintf_s( message,
                       sizeof( message ),
                       "replay visual reveal restarted or skipped: expected=%llu actual=%llu",
                       static_cast<unsigned long long>( expectedRevealFrame ),
                       static_cast<unsigned long long>( tick.revealFrame ) );
            FailAutomation( state, message );
        }
        tick.ordinaryLineHash = bufferFacts.ordinaryLineHash;
        tick.priorityLineHash = bufferFacts.priorityLineHash;
        tick.priorityLineCanonicalHash = submission.priorityLineCanonicalHash;
        tick.ordinaryRibbonHash = bufferFacts.ordinaryRibbonHash;
        tick.priorityRibbonHash = bufferFacts.priorityRibbonHash;
        tick.priorityRibbonCanonicalHash = submission.priorityRibbonCanonicalHash;
        tick.vertexHash = bufferFacts.expandedVertexHash;
        tick.ordinaryVertexHash = bufferFacts.ordinaryExpandedVertexHash;
        tick.ordinaryLineBytes = bufferFacts.ordinaryLineBytes;
        tick.priorityLineBytes = bufferFacts.priorityLineBytes;
        tick.ordinaryRibbonBytes = bufferFacts.ordinaryRibbonBytes;
        tick.priorityRibbonBytes = bufferFacts.priorityRibbonBytes;
        tick.vertexBytes = bufferFacts.expandedVertexBytes;
        tick.ordinaryVertexBytes = bufferFacts.ordinaryExpandedVertexBytes;
        tick.ordinaryLineVertexCount = submission.ordinaryLineVertexCount;
        tick.priorityLineVertexCount = submission.priorityLineVertexCount;
        tick.ordinaryRibbonSegmentCount = submission.ordinaryRibbonSegmentCount;
        tick.priorityRibbonSegmentCount = submission.priorityRibbonSegmentCount;
        tick.vertexCount = submission.vertexCount;
        tick.ordinaryVertexCount = submission.ordinaryVertexCount;
        tick.segmentCount = submission.segmentCount;
        state.replayVisualFidelityTicks.push_back( tick );
        state.replayCausalProofTicks.push_back( BuildReplayCausalProofTick( replayView.visualPacket ) );

        const std::vector<RunReplayPredictionFrame>& predictionFrames = replayView.prediction.simulation.frames;
        if ( state.replayVisualFidelityTicks.size() == predictionFrames.size() && !predictionFrames.empty() )
        {
            const PredictionTrajectoryFingerprint revealFingerprint =
                BuildPredictionTrajectoryFingerprint( replayView );
            state.replayVisualFidelityTrajectoryHash = revealFingerprint.hash;
            state.replayVisualFidelityTrajectoryRecordCount = revealFingerprint.recordCount;
            state.replayVisualFidelityTrajectoryPointCount = revealFingerprint.pointCount;
            state.replayVisualFidelityTrajectoryCaptured = revealFingerprint.Ready();
            if ( !BuildReplayPredictionArchive( replayView.path,
                                                replayView.prediction,
                                                state.replayVisualPredictionArchive ) )
            {
                FailAutomation( state, "replay visual fidelity probe could not freeze prediction presentation state" );
            }
            state.replayCausalTopology.reserve( packet.futureNodes.size() );
            for ( const RunReplayPathTraceNode& node : packet.futureNodes )
            {
                state.replayCausalTopology.push_back( ReplayCausalTopologyNodeReport{ node.id.value,
                                                                                      node.parentId.value,
                                                                                      node.firstFrame,
                                                                                      node.depth,
                                                                                      node.contactDerived } );
            }
        }
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
        if ( !state.failed && state.replayVisualFidelityCaptureEnabled && !state.replayVisualOfflineProjectionComplete )
        {
            // This is after the final reveal screenshot while live physics still
            // holds the seed pose used by root markers. The CPU-only loop cannot
            // become a second presented visual pass.
            (void)VerifyReplayVisualOfflineProjection( state,
                                                       interactionOwners,
                                                       sceneOwners,
                                                       replayView.latestSolverSample );
        }
        if ( !state.failed && state.replayVisualFidelityCaptureEnabled &&
             state.replayVisualFidelityTicks.size() != replayView.prediction.simulation.frames.size() )
        {
            ClearInteractionAutomationInput( state );
            return result;
        }
        if ( !state.failed && replayView.prediction.build.building )
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
        const SkullbonezCore::Core::SbResult reportResult =
            WriteInteractionAutomationReport( state, scene, runtimeTools, replayView, interaction, camera, ui );
        if ( result.status.ok )
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
        if ( !action.processed && action.frame == frame &&
             action.type == RunInteractionAutomationActionType::Screenshot )
        {
            return true;
        }
    }
    return false;
}

SkullbonezCore::Core::SbResult
SkullbonezCore::Runtime::WriteInteractionAutomationReport( InteractionAutomationController& state,
                                                           const SceneController& scene,
                                                           const RuntimeTools& runtimeTools,
                                                           const ReplayAutomationView& replay,
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
    std::string replayArtifactPath;
    ReplayV2SaveResult replayArtifactResult;
    bool replayArtifactSaved = false;
    if ( state.replayVisualFidelityCaptureEnabled && state.replayVisualOfflineProjectionComplete && !state.failed )
    {
        replayArtifactPath = state.reportPath;
        const std::size_t extensionOffset = replayArtifactPath.find_last_of( '.' );
        if ( extensionOffset != std::string::npos )
        {
            replayArtifactPath.resize( extensionOffset );
        }
        replayArtifactPath += ".skreplay";
        std::vector<ReplayVisualArchiveSample> visualPackets;
        visualPackets.reserve( state.replayVisualFidelityTicks.size() );
        for ( const ReplayVisualFidelityReportTick& tick : state.replayVisualFidelityTicks )
        {
            visualPackets.push_back( BuildReplayVisualArchiveSample( tick ) );
        }
        // Lane R: the artifact is cold validation IO. Its failure belongs in
        // the machine-readable automation result, never in runtime ownership.
        char archiveReason[192] = {};
        if ( !VerifyReplayPredictionArchiveRoundTrip( state.replayVisualPredictionArchive,
                                                      archiveReason,
                                                      sizeof( archiveReason ) ) )
        {
            char message[320] = {};
            sprintf_s( message,
                       sizeof( message ),
                       "replay visual prediction archive failed offline round-trip: %s",
                       archiveReason[0] != '\0' ? archiveReason : "unknown archive failure" );
            FailAutomation( state, message );
        }
        if ( !state.failed )
        {
            replayArtifactSaved =
                ReplayV2Artifact::SavePresentationWithSolverHashes( replay.presentationRecorder,
                                                                    replay.solverRecorder,
                                                                    replay.eventRecorder,
                                                                    visualPackets,
                                                                    state.replayVisualPredictionArchive,
                                                                    replayArtifactPath.c_str(),
                                                                    &replayArtifactResult );
        }
        if ( replayArtifactSaved )
        {
            std::vector<uint8_t> loadedPredictionArchive;
            char loadedArchiveReason[192] = {};
            const bool loadedArchiveVerified =
                ReplayV2Artifact::LoadVisualPredictionState( replayArtifactPath.c_str(), loadedPredictionArchive ) &&
                loadedPredictionArchive == state.replayVisualPredictionArchive &&
                VerifyReplayPredictionArchiveRoundTrip( loadedPredictionArchive,
                                                        loadedArchiveReason,
                                                        sizeof( loadedArchiveReason ) );
            if ( !loadedArchiveVerified )
            {
                replayArtifactSaved = false;
                char message[320] = {};
                sprintf_s( message,
                           sizeof( message ),
                           "saved replay prediction archive failed offline readback: %s",
                           loadedArchiveReason[0] != '\0' ? loadedArchiveReason : "byte mismatch or missing RVPD" );
                FailAutomation( state, message );
            }
        }
        if ( !state.failed && !replayArtifactSaved )
        {
            FailAutomation( state, "replay visual fidelity probe failed to save its durable presentation artifact" );
        }
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

    Json replayVisualFidelityTicks = Json::array();
    for ( const ReplayVisualFidelityReportTick& tick : state.replayVisualFidelityTicks )
    {
        replayVisualFidelityTicks.push_back(
            Json{ { "sceneFrame", tick.sceneFrame },
                  { "revealFrame", tick.revealFrame },
                  { "sourceFrame", tick.sourceFrame },
                  { "semanticHash", FormatPredictionHash( tick.semanticHash ) },
                  { "headerStateHash", FormatPredictionHash( tick.headerStateHash ) },
                  { "trajectoryStateHash", FormatPredictionHash( tick.trajectoryStateHash ) },
                  { "topologyStateHash", FormatPredictionHash( tick.topologyStateHash ) },
                  { "markerStateHash", FormatPredictionHash( tick.markerStateHash ) },
                  { "ghostStateHash", FormatPredictionHash( tick.ghostStateHash ) },
                  { "visualStateHash", FormatPredictionHash( tick.visualStateHash ) },
                  { "exactPacketHash", FormatPredictionHash( tick.exactPacketHash ) },
                  { "schemaVersion", tick.schemaVersion },
                  { "targetId", tick.targetId },
                  { "branchId", tick.branchId },
                  { "eventCursor", tick.eventCursor },
                  { "topologyVersion", tick.topologyVersion },
                  { "publishedFrameCount", tick.publishedFrameCount },
                  { "predictionEnabled", tick.predictionEnabled },
                  { "predictionBuilding", tick.predictionBuilding },
                  { "predictionComplete", tick.predictionComplete },
                  { "cameraEye", { tick.cameraEye.x, tick.cameraEye.y, tick.cameraEye.z } },
                  { "cameraUp", { tick.cameraUp.x, tick.cameraUp.y, tick.cameraUp.z } },
                  { "trajectoryRecordCount", tick.trajectoryRecordCount },
                  { "futureNodeCount", tick.futureNodeCount },
                  { "retainedMarkerCount", tick.retainedMarkerCount },
                  { "ghostRequestCount", tick.ghostRequestCount },
                  { "droppedSegmentCount", tick.droppedSegmentCount },
                  { "replayReserveGrowthEvents", tick.replayReserveGrowthEvents },
                  { "hasGeometry", tick.hasGeometry },
                  { "combinedLineHash", FormatPredictionHash( tick.combinedLineHash ) },
                  { "combinedLineBytes", tick.combinedLineBytes },
                  { "combinedLineVertexCount", tick.combinedLineVertexCount },
                  { "ordinaryLineHash", FormatPredictionHash( tick.ordinaryLineHash ) },
                  { "priorityLineHash", FormatPredictionHash( tick.priorityLineHash ) },
                  { "priorityLineCanonicalHash", FormatPredictionHash( tick.priorityLineCanonicalHash ) },
                  { "ordinaryRibbonHash", FormatPredictionHash( tick.ordinaryRibbonHash ) },
                  { "priorityRibbonHash", FormatPredictionHash( tick.priorityRibbonHash ) },
                  { "priorityRibbonCanonicalHash", FormatPredictionHash( tick.priorityRibbonCanonicalHash ) },
                  { "vertexHash", FormatPredictionHash( tick.vertexHash ) },
                  { "ordinaryVertexHash", FormatPredictionHash( tick.ordinaryVertexHash ) },
                  { "ordinaryLineBytes", tick.ordinaryLineBytes },
                  { "priorityLineBytes", tick.priorityLineBytes },
                  { "ordinaryRibbonBytes", tick.ordinaryRibbonBytes },
                  { "priorityRibbonBytes", tick.priorityRibbonBytes },
                  { "vertexBytes", tick.vertexBytes },
                  { "ordinaryVertexBytes", tick.ordinaryVertexBytes },
                  { "ordinaryLineVertexCount", tick.ordinaryLineVertexCount },
                  { "priorityLineVertexCount", tick.priorityLineVertexCount },
                  { "ordinaryRibbonSegmentCount", tick.ordinaryRibbonSegmentCount },
                  { "priorityRibbonSegmentCount", tick.priorityRibbonSegmentCount },
                  { "vertexCount", tick.vertexCount },
                  { "ordinaryVertexCount", tick.ordinaryVertexCount },
                  { "segmentCount", tick.segmentCount } } );
    }

    Json replayCausalTicks = Json::array();
    for ( const ReplayCausalProofTick& tick : state.replayCausalProofTicks )
    {
        replayCausalTicks.push_back( Json{ { "revealFrame", tick.revealFrame },
                                           { "activeTopologyHash", FormatPredictionHash( tick.activeTopologyHash ) },
                                           { "activeNodeCount", tick.activeNodeCount },
                                           { "revealedRecordCount", tick.revealedRecordCount },
                                           { "revealedPointCount", tick.revealedPointCount },
                                           { "revealedSegmentCount", tick.revealedSegmentCount },
                                           { "entryMarkerCount", tick.entryMarkerCount },
                                           { "restMarkerCount", tick.restMarkerCount },
                                           { "horizonMarkerCount", tick.horizonMarkerCount },
                                           { "ghostRequestCount", tick.ghostRequestCount } } );
    }

    Json replayCausalTopology = Json::array();
    for ( const ReplayCausalTopologyNodeReport& node : state.replayCausalTopology )
    {
        replayCausalTopology.push_back( Json{ { "id", node.id },
                                              { "parentId", node.parentId },
                                              { "firstFrame", node.firstFrame },
                                              { "depth", node.depth },
                                              { "contactDerived", node.contactDerived } } );
    }

    const int selectedIndex = PeekSelectedEditorModelIndex( runtimeTools.Editor(), scene.BodyStore() );
    const char* selectedName = "";
    if ( selectedIndex >= 0 && selectedIndex < scene.SceneEntityCount() )
    {
        selectedName = scene.Entities().At( selectedIndex ).displayName;
    }
    const bool gizmoVisible =
        selectedIndex >= 0 &&
        ( runtimeTools.Editor().editorModeEnabled ||
          runtimeTools.InspectGizmoInteractionActive( camera.mode, replay.input.inspectionActive ) );
    const bool replayPastPathVisible = replay.path.hasTarget && replay.path.pastPathVisible;
    const std::size_t predictionVisibleFrameCount = VisiblePredictionFrameCount( replay );
    const bool predictionPathVisible = ReplayPredictionPathVisible( replay );
    const bool predictionContactsIncomplete = ReplayPredictionContactsIncomplete( replay );
    uint64_t predictionSourceSolverHash = 0;
    uint64_t liveSolverHash = 0;
    const bool liveSolverHashStableAcrossPrediction =
        LiveSolverHashStableAcrossPrediction( replay, &predictionSourceSolverHash, &liveSolverHash );
    const float replaySolverTrackPosition = replay.solverTrackPosition;
    const float replaySolverPresentTrackPosition = replay.solverPresentTrackPosition;
    const bool replaySolverTrackAtPresent =
        ReplayAtPresentTrackPosition( replaySolverTrackPosition, replaySolverPresentTrackPosition );
    const bool predictionScrubFrameActive = replay.currentPredictionFrame != nullptr;
    bool predictionTargetDisplacementValid = false;
    Vector3 predictionTargetFirst = ZERO_VECTOR;
    Vector3 predictionTargetLast = ZERO_VECTOR;
    float predictionTargetDisplacement = 0.0f;
    predictionTargetDisplacementValid = TryPredictionTargetDisplacement( replay,
                                                                         predictionTargetDisplacement,
                                                                         &predictionTargetFirst,
                                                                         &predictionTargetLast );
    const RunReplayPredictionState& predictionState = replay.prediction;
    const ReplayPredictionBaselineSnapshot& predictionBaseline = predictionState.baseline;
    const bool predictionBaselineVisible = predictionBaseline.valid && predictionBaseline.comparisonActive;
    int predictionAuthoredWallBrickCount = 0;
    int predictionAffectedWallBrickCount = 0;
    int predictionMovedWallBrickCount = 0;
    int predictionToppledWallBrickCount = 0;
    int predictionSustainedToppledWallBrickCount = 0;
    int predictionSettledWallBrickCount = 0;
    const RunReplayPredictionFrame* predictionFirstFrame =
        predictionState.simulation.frames.empty() ? nullptr : &predictionState.simulation.frames.front();
    const RunReplayPredictionFrame* predictionLastFrame =
        predictionState.simulation.frames.empty() ? nullptr : &predictionState.simulation.frames.back();
    const auto findPredictionBodyByModelRow = []( const RunReplayPredictionFrame* sample,
                                                  int modelIndex ) -> const RunReplayPredictionBodySample*
    {
        if ( !sample )
        {
            return nullptr;
        }
        for ( const RunReplayPredictionBodySample& body : sample->bodies )
        {
            if ( body.modelRow.value == modelIndex )
            {
                return &body;
            }
        }
        return nullptr;
    };
    const auto orientationDeltaSquared =
        []( const RunReplayPredictionBodySample& first, const RunReplayPredictionBodySample& second )
    {
        float firstX = 0.0f;
        float firstY = 0.0f;
        float firstZ = 0.0f;
        float firstW = 1.0f;
        float secondX = 0.0f;
        float secondY = 0.0f;
        float secondZ = 0.0f;
        float secondW = 1.0f;
        first.orientation.GetComponents( firstX, firstY, firstZ, firstW );
        second.orientation.GetComponents( secondX, secondY, secondZ, secondW );
        const float directDelta =
            ( firstX - secondX ) * ( firstX - secondX ) + ( firstY - secondY ) * ( firstY - secondY ) +
            ( firstZ - secondZ ) * ( firstZ - secondZ ) + ( firstW - secondW ) * ( firstW - secondW );
        const float antipodalDelta =
            ( firstX + secondX ) * ( firstX + secondX ) + ( firstY + secondY ) * ( firstY + secondY ) +
            ( firstZ + secondZ ) * ( firstZ + secondZ ) + ( firstW + secondW ) * ( firstW + secondW );
        return (std::min)( directDelta, antipodalDelta );
    };
    for ( int modelIndex = 0; modelIndex < scene.SceneEntityCount(); ++modelIndex )
    {
        const SceneEntityRecord& entity = scene.Entities().At( modelIndex );
        if ( strncmp( entity.displayName, "prediction_wall_brick_", 22u ) != 0 )
        {
            continue;
        }
        ++predictionAuthoredWallBrickCount;
        const bool affected =
            std::any_of( predictionState.futureNodeCache.futureNodes.begin(),
                         predictionState.futureNodeCache.futureNodes.end(),
                         [&]( const RunReplayPathTraceNode& node ) { return node.modelRow.value == modelIndex; } );
        predictionAffectedWallBrickCount += affected ? 1 : 0;
        if ( !predictionFirstFrame || !predictionLastFrame )
        {
            continue;
        }
        const RunReplayPredictionBodySample* firstBody =
            findPredictionBodyByModelRow( predictionFirstFrame, modelIndex );
        const RunReplayPredictionBodySample* lastBody = findPredictionBodyByModelRow( predictionLastFrame, modelIndex );
        const Physics::PhysicsColliderHandle colliderHandle = scene.Colliders().HandleForModelIndex( modelIndex );
        const Physics::ColliderRecord* collider = scene.Colliders().RecordForHandle( colliderHandle );
        const SkullbonezCore::Math::CollisionDetection::BoundingBox* wallBrickShape =
            collider ? std::get_if<SkullbonezCore::Math::CollisionDetection::BoundingBox>( &collider->shape ) : nullptr;
        const auto grounded = [&]( const RunReplayPredictionBodySample& body )
        {
            if ( !wallBrickShape )
            {
                return false;
            }
            // The 200-box fixture owns a flat y=0 terrain plane. A box is on
            // that plane when its oriented support point is within the contact
            // tolerance; a sleeper resting on another brick does not qualify.
            constexpr float GROUND_CONTACT_TOLERANCE = 0.05f;
            SkullbonezCore::Math::Orientation::Quaternion orientation = body.orientation;
            const RotationMatrix rotation = orientation.GetOrientationMatrix();
            const float verticalExtent = rotation.SupportExtentY( wallBrickShape->GetHalfExtents() );
            return body.position.y - verticalExtent <= GROUND_CONTACT_TOLERANCE;
        };
        if ( firstBody && lastBody && VectorMagSquared( lastBody->position - firstBody->position ) > 0.000001f )
        {
            ++predictionMovedWallBrickCount;
        }
        const bool toppledAtEnd = lastBody && lastBody->sleeping && grounded( *lastBody );
        predictionToppledWallBrickCount += toppledAtEnd ? 1 : 0;
        bool toppledThroughoutFinalSecond = toppledAtEnd;
        bool settledThroughoutFinalSecond = firstBody && lastBody;
        const std::size_t finalSecondStart =
            predictionState.simulation.frames.size() > 120u ? predictionState.simulation.frames.size() - 121u : 0u;
        for ( std::size_t frameIndex = finalSecondStart;
              ( toppledThroughoutFinalSecond || settledThroughoutFinalSecond ) &&
              frameIndex < predictionState.simulation.frames.size();
              ++frameIndex )
        {
            const RunReplayPredictionBodySample* finalSecondBody =
                findPredictionBodyByModelRow( &predictionState.simulation.frames[frameIndex], modelIndex );
            if ( toppledThroughoutFinalSecond )
            {
                toppledThroughoutFinalSecond =
                    finalSecondBody && finalSecondBody->sleeping && grounded( *finalSecondBody );
            }
            if ( settledThroughoutFinalSecond )
            {
                // Invariant: horizon completeness requires the whole authored
                // wall to be motionless for its final second. The one-micron
                // position bound is only a completion predicate; frame-exact
                // packet hashes remain the visual-parity oracle.
                constexpr float ONE_MICRON_SQUARED = 0.000000000001f;
                settledThroughoutFinalSecond =
                    finalSecondBody &&
                    VectorMagSquared( finalSecondBody->position - lastBody->position ) <= ONE_MICRON_SQUARED &&
                    VectorMagSquared( finalSecondBody->linearVelocity ) <= ONE_MICRON_SQUARED &&
                    orientationDeltaSquared( *finalSecondBody, *lastBody ) <= ONE_MICRON_SQUARED;
            }
        }
        predictionSustainedToppledWallBrickCount += toppledThroughoutFinalSecond ? 1 : 0;
        predictionSettledWallBrickCount += settledThroughoutFinalSecond ? 1 : 0;
    }
    PredictionTrajectoryFingerprint predictionTrajectoryFingerprint = BuildPredictionTrajectoryFingerprint( replay );
    if ( state.replayVisualFidelityTrajectoryCaptured )
    {
        // The V0 oracle describes the completed prediction reveal. Report the
        // fingerprint frozen at its last presented tick rather than any later
        // non-presenting verification scratch state.
        predictionTrajectoryFingerprint.hash = state.replayVisualFidelityTrajectoryHash;
        predictionTrajectoryFingerprint.recordCount =
            static_cast<std::size_t>( state.replayVisualFidelityTrajectoryRecordCount );
        predictionTrajectoryFingerprint.pointCount =
            static_cast<std::size_t>( state.replayVisualFidelityTrajectoryPointCount );
    }
    const ReplayTrajectorySubmissionProbeStats& predictionSubmissionProbe = replay.trajectorySubmission;
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
    const SkullbonezCore::Core::MainMemoryReplayStats replayMemoryStats = replay.memoryStats;
    uint64_t trajectoryDroppedTotal = 0;
    for ( std::size_t laneIndex = 0; laneIndex < SkullbonezCore::Core::MAIN_MEMORY_REPLAY_TRAJECTORY_LANE_COUNT;
          ++laneIndex )
    {
        trajectoryDroppedTotal += replayMemoryStats.trajectory.droppedSegments[laneIndex];
    }
    Json report;
    report["ok"] = !state.failed;
    report["scene"] = scenePath ? *scenePath : "";
    report["script"] = state.scriptPath;
    report["framesRun"] = scene.State().currentFrame;
    report["actions"] = actions;
    report["assertions"] = assertions;
    report["screenshots"] = screenshots;
    report["replayVisualFidelity"] = Json{ { "schemaVersion", 2 },
                                           { "startFrame", state.replayVisualFidelityStartFrame },
                                           { "tickCount", state.replayVisualFidelityTicks.size() },
                                           { "offlineProjectionComplete", state.replayVisualOfflineProjectionComplete },
                                           { "ticks", replayVisualFidelityTicks } };
    report["replayCausalProof"] = Json{ { "schemaVersion", 2 },
                                        { "singleRevealGeneration", true },
                                        { "singlePresentedCascade", true },
                                        { "targetId", replay.visualPacket.header.targetId.value },
                                        { "tickCount", state.replayCausalProofTicks.size() },
                                        { "topologyCount", state.replayCausalTopology.size() },
                                        { "topology", replayCausalTopology },
                                        { "ticks", replayCausalTicks } };
    report["replayArtifact"] =
        Json{ { "schemaVersion", 4 },
              { "saved", replayArtifactSaved },
              { "path", replayArtifactPath },
              { "sampleCount", replayArtifactResult.sampleCount },
              { "bodyDictionaryCount", replayArtifactResult.bodyDictionaryCount },
              { "solverHashCount", replayArtifactResult.solverHashCount },
              { "solverCheckpointCount", replayArtifactResult.solverCheckpointCount },
              { "eventCount", replayArtifactResult.eventCount },
              { "eventCursorCount", replayArtifactResult.eventCursorCount },
              { "visualPacketCount", replayArtifactResult.visualPacketCount },
              { "visualPredictionHash", FormatPredictionHash( replayArtifactResult.visualPredictionHash ) },
              { "fileBytes", replayArtifactResult.fileBytes } };
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
        { "predictionBuildMode", ReplayPredictionBuildModeName( predictionState.build.buildMode ) },
        { "predictionMeasuredTicksPerMs",
          predictionState.simulation.measuredTicksPerMs.load( std::memory_order_acquire ) },
        { "predictionLastBuildWallMs", predictionState.build.lastBuildWallMs },
        { "predictionPendingLatestRestart", predictionState.build.pendingLatestRestart },
        { "predictionSupersededRestartCount", predictionState.build.supersededRestartCount },
        { "predictionLatestRestartBeginCount", predictionState.build.latestRestartBeginCount },
        { "replayPathTarget", replay.path.hasTarget ? replay.path.targetName : "" },
        { "replayPathTargetCount", static_cast<int>( replay.path.targets.size() ) },
        { "replayPastTrajectoryFullRebuildCount", replay.path.pastTrajectory.fullRebuildCount },
        { "replayPastTrajectoryIncrementalTrimCount", replay.path.pastTrajectory.incrementalTrimCount },
        { "replayPastTrajectoryPublishedPointCount",
          static_cast<int>( ReplayPastTrajectoryPublishedPointCount( replay ) ) },
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
        { "predictionTrajectoryDroppedSegmentCount", trajectoryDroppedTotal },
        { "predictionTrajectoryDroppedSegments",
          Json{ { "pastRoot",
                  replayMemoryStats.trajectory.droppedSegments[static_cast<std::size_t>(
                      SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::PastRoot )] },
                { "futureRoot",
                  replayMemoryStats.trajectory.droppedSegments[static_cast<std::size_t>(
                      SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureRoot )] },
                { "futureChildIncoming",
                  replayMemoryStats.trajectory.droppedSegments[static_cast<std::size_t>(
                      SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureChildIncoming )] },
                { "futureChildOutgoing",
                  replayMemoryStats.trajectory.droppedSegments[static_cast<std::size_t>(
                      SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureChildOutgoing )] },
                { "retainedTrail",
                  replayMemoryStats.trajectory.droppedSegments[static_cast<std::size_t>(
                      SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::RetainedTrail )] },
                { "baselineRoot",
                  replayMemoryStats.trajectory.droppedSegments[static_cast<std::size_t>(
                      SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::BaselineRoot )] },
                { "causalMarker",
                  replayMemoryStats.trajectory.droppedSegments[static_cast<std::size_t>(
                      SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::CausalMarker )] },
                { "auxiliaryTrail",
                  replayMemoryStats.trajectory.droppedSegments[static_cast<std::size_t>(
                      SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::AuxiliaryTrail )] } } },
        { "predictionTrajectorySubmissionFirstFrame", predictionSubmissionProbe.firstFrame },
        { "predictionTrajectorySubmissionLastFrame", predictionSubmissionProbe.lastFrame },
        { "predictionTrajectorySteadyStateNoReserveGrowth", predictionSubmissionProbe.noReserveGrowth },
        { "predictionTrajectoryReserveGrowthEventsAtStart", predictionSubmissionProbe.reserveGrowthEventsAtStart },
        { "predictionTrajectoryReserveGrowthEventsAtEnd", predictionSubmissionProbe.reserveGrowthEventsAtEnd },
        { "predictionFutureNodeCount", static_cast<int>( predictionState.futureNodeCache.futureNodes.size() ) },
        { "predictionAuthoredWallBrickCount", predictionAuthoredWallBrickCount },
        { "predictionAffectedWallBrickCount", predictionAffectedWallBrickCount },
        { "predictionMovedWallBrickCount", predictionMovedWallBrickCount },
        { "predictionToppledWallBrickCount", predictionToppledWallBrickCount },
        { "predictionSustainedToppledWallBrickCount", predictionSustainedToppledWallBrickCount },
        { "predictionSettledWallBrickCount", predictionSettledWallBrickCount },
        { "predictionGenerationCount", predictionState.build.generationBeginCount },
        { "predictionFutureNodeBuildFrameCount",
          static_cast<int>( predictionState.futureNodeCache.futureNodesBuiltFrameCount ) },
        { "predictionRetainedEntryMarkerCount", static_cast<int>( predictionRetainedEntryMarkerCount ) },
        { "predictionRetainedRestMarkerCount", static_cast<int>( predictionRetainedRestMarkerCount ) },
        { "predictionRetainedHorizonMarkerCount", static_cast<int>( predictionRetainedHorizonMarkerCount ) },
        { "replayActiveTrack", ReplayTrackName( replay.scrubber.activeTrack ) },
        { "replayHistoricalSamplePaused", replay.scrubber.historicalSamplePaused },
        { "replaySolverTrackPosition", replaySolverTrackPosition },
        { "replaySolverPresentTrackPosition", replaySolverPresentTrackPosition },
        { "replaySolverTrackAtPresent", replaySolverTrackAtPresent },
        { "predictionScrubFrameActive", predictionScrubFrameActive },
        { "replayFutureNodeCount", static_cast<int>( replay.path.futureNodes.size() ) } };

    std::ofstream output;
    if ( !RuntimeFileWriter::OpenTextFile( state.reportPath, output ) )
    {
        state.reportWritten = true;
        state.failed = true;
        strcpy_s( state.failure, sizeof( state.failure ), "failed to open interaction report path" );
        return SkullbonezCore::Core::SbResult::Failure( "InteractionAutomation", state.failure );
    }
    output << report.dump( 2 ) << "\n";
    output.close();
    state.reportWritten = true;
    printf( "[interaction] Report written: %s ok=%d\n", state.reportPath, state.failed ? 0 : 1 );
    return InteractionAutomationResult( state );
}
