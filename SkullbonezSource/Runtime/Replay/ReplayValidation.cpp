/*
File: SkullbonezSource/Runtime/Replay/ReplayValidation.cpp
Purpose:
  Owns transactional v2 target restore and the thin startup dispatch into
  configuration-specific replay diagnostics.

Summary:
  ReplayRuntime restores production artifacts, verifies target hashes, and
  rolls back recoverable failures. Debug probe implementations live in
  ReplayValidation.Probes.cpp and call these same product operations.

Glossary:
  Replay probe: CLI/debug validation path that proves scrub, save/load, or
    solver restore behavior without throwing exceptions.
  V2 target restore: Hash-gated replay restore that starts from a saved solver
    checkpoint and replays saved timeline events to a target frame.
  Event cursor: Monotonic sequence marker stored on checkpoints so restore can
    resume timeline events without replaying old side effects.

Invariants:
  - Restore failures report Lane R results or bounded reason strings; they do
    not throw. A rollback failure is a fatal replay invariant.
  - Replay restore uses PhysicsBodyStore and ColliderStore rows as authority.
  - Target restore must keep solver hashes byte-exact against saved v2 hashes.

Related:
  - SkullbonezSource/Runtime/RunFrame.cpp
  - SkullbonezSource/Runtime/Replay/ReplayValidation.Probes.cpp
  - SkullbonezSource/Runtime/Replay/ReplayValidation.Internal.h
  - SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "ReplayPresentation.h"
#include "ReplayOverlayLayout.h"
#include "ReplayScrubber.h"
#include "ReplayTimeline.h"
#include "ReplayRuntime.h"
#include "../InputRouter.h"
#include "../Diagnostics/DiagnosticsRuntime.h"
#include "../Scene/SceneController.h"
#include "../../Assets/AssetSystem.h"
#include "../../Core/WorkerPool.h"
#include "../RuntimeTuning.h"
#include "../Editor/EditorTools.h"
#include "ReplayRestoreService.h"
#include "ReplayRestoreTransactions.h"
#include "ReplayPredictionArchive.h"
#include "ReplayValidation.Internal.h"
#include "ReplayV2Artifact.h"

#include "../../Core/FatalError.h"
#include "../../Core/Profiler.h"
#include "../../Physics/SimulationSystem.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsTimestep.h"
#include "../../Rendering/RenderInstanceStore.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::ReplayScrubberOperations;
using namespace SkullbonezCore::Runtime::ReplayValidationInternal;
using namespace SkullbonezCore::Math::CollisionDetection;

using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Runtime::RunInternal;
using SkullbonezCore::Math::Vector::Vector3;

namespace SkullbonezCore::Runtime::ReplayValidationInternal
{
const PhysicsBodyRecord* TryGetReplayProbeBodyRecord( const SceneController& collection, int modelIndex )
{
    const PhysicsBodyStore& bodyStore = collection.BodyStore();
    const PhysicsBodyHandle bodyHandle = bodyStore.HandleForModelIndex( modelIndex );
    const PhysicsBodyRecord* body = bodyStore.RecordForHandle( bodyHandle );
    if ( !body || bodyStore.ModelIndexForHandle( bodyHandle ) != modelIndex )
    {
        return nullptr;
    }
    return body;
}

// Why: editor transform replay still mutates authoring data, but the shape it
// scales from is already owned by ColliderStore. Reading the store row here
// avoids treating presentation data as collision authority.
const ColliderRecord* TryGetEditorTransformColliderRecord( const SceneController& collection,
                                                           PhysicsColliderHandle colliderHandle,
                                                           int modelIndex,
                                                           uint32_t replayBodyId )
{
    const ColliderStore& colliderStore = collection.Colliders();
    const PhysicsBodyStore& bodyStore = collection.BodyStore();
    const PhysicsBodyHandle bodyHandle = replayBodyId != 0u
                                             ? bodyStore.HandleForReplayBodyId( replayBodyId, modelIndex )
                                             : bodyStore.HandleForModelIndex( modelIndex );
    const PhysicsColliderHandle resolvedHandle =
        colliderHandle.IsValid() ? colliderHandle : colliderStore.HandleForBodyHandle( bodyHandle );
    const ColliderRecord* collider = colliderStore.RecordForHandle( resolvedHandle );
    if ( !collider || colliderStore.ModelIndexForHandle( resolvedHandle ) != modelIndex )
    {
        return nullptr;
    }
    if ( replayBodyId != 0 && collider->replayBodyId != replayBodyId )
    {
        return nullptr;
    }
    return collider;
}
} // namespace SkullbonezCore::Runtime::ReplayValidationInternal

namespace
{
// Debug-only probe implementations live in ReplayValidation.Probes.cpp.


// Concept: replay flags are compact wire-format fields. Keep these masks local
// to decode logic so new replay payload versions do not inherit accidental UI
// or runtime enum values.
SceneGeneratedModelContext BuildSceneGeneratedModelContext( RunSceneState& scene,
                                                            const SkullbonezCore::Core::EngineConfig& config,
                                                            SkullbonezCore::Environment::WorldEnvironment& world,
                                                            SkullbonezCore::Geometry::Terrain* terrain,
                                                            SkullbonezCore::Runtime::SceneController& models,
                                                            SkullbonezCore::Physics::PhysicsEngine& physics,
                                                            GeneratedObjectTypeOverride objectTypeOverride )
{
    return SceneGeneratedModelContext{ scene, config, world, terrain, models, physics, objectTypeOverride };
}

float ReplayEventFloatFromBits( int32_t signedBits )
{
    uint32_t bits = 0;
    float value = 0.0f;
    static_assert( sizeof( bits ) == sizeof( signedBits ), "Replay event float bits must be 32-bit." );
    static_assert( sizeof( bits ) == sizeof( value ), "Replay event float payloads assume 32-bit floats." );
    std::memcpy( &bits, &signedBits, sizeof( bits ) );
    std::memcpy( &value, &bits, sizeof( value ) );
    return value;
}

int ReplayHexNibble( char value )
{
    if ( value >= '0' && value <= '9' )
    {
        return value - '0';
    }
    if ( value >= 'a' && value <= 'f' )
    {
        return value - 'a' + 10;
    }
    if ( value >= 'A' && value <= 'F' )
    {
        return value - 'A' + 10;
    }
    return -1;
}

bool ReadReplayHexFloat( const char*& cursor, float& outValue )
{
    uint32_t bits = 0;
    for ( int i = 0; i < 8; ++i )
    {
        const int nibble = ReplayHexNibble( cursor[i] );
        if ( nibble < 0 )
        {
            return false;
        }
        bits = ( bits << 4 ) | static_cast<uint32_t>( nibble );
    }
    cursor += 8;
    std::memcpy( &outValue, &bits, sizeof( outValue ) );
    return true;
}

bool DecodeReplayRay9Payload( const ReplayEventSample& event,
                              Vector3& outOrigin,
                              Vector3& outDirection,
                              Vector3& outCameraUp )
{
    constexpr char prefix[] = "ray9:";
    if ( std::strncmp( event.text, prefix, sizeof( prefix ) - 1 ) != 0 )
    {
        return false;
    }

    const char* cursor = event.text + sizeof( prefix ) - 1;
    float values[9] = {};
    for ( float& value : values )
    {
        if ( !ReadReplayHexFloat( cursor, value ) )
        {
            return false;
        }
    }

    outOrigin = Vector3( values[0], values[1], values[2] );
    outDirection = Vector3( values[3], values[4], values[5] );
    outCameraUp = Vector3( values[6], values[7], values[8] );
    return true;
}

bool DecodeReplayPlacePayload( const ReplayEventSample& event,
                               Vector3& outTerrainPoint,
                               Vector3& outPlacementScale,
                               float& outPlacementYawRadians )
{
    constexpr char prefix6[] = "place6:";
    constexpr char prefix7[] = "place7:";
    int valueCount = 0;
    const char* cursor = nullptr;
    if ( std::strncmp( event.text, prefix7, sizeof( prefix7 ) - 1 ) == 0 )
    {
        valueCount = 7;
        cursor = event.text + sizeof( prefix7 ) - 1;
    }
    else if ( std::strncmp( event.text, prefix6, sizeof( prefix6 ) - 1 ) == 0 )
    {
        valueCount = 6;
        cursor = event.text + sizeof( prefix6 ) - 1;
    }
    else
    {
        return false;
    }

    float values[7] = {};
    for ( int i = 0; i < valueCount; ++i )
    {
        if ( !ReadReplayHexFloat( cursor, values[i] ) )
        {
            return false;
        }
    }

    outTerrainPoint = Vector3( values[0], values[1], values[2] );
    outPlacementScale = Vector3( values[3], values[4], values[5] );
    outPlacementYawRadians = valueCount >= 7 ? values[6] : 0.0f;
    return true;
}


bool DecodeReplayTransformPayload( const ReplayEventSample& event,
                                   Vector3& outPosition,
                                   Quaternion& outOrientation,
                                   float& outScaleFactor,
                                   bool& outHasScaleFactor )
{
    constexpr char prefix7[] = "xform7:";
    constexpr char prefix8[] = "xform8:";
    int valueCount = 0;
    const char* cursor = nullptr;
    if ( std::strncmp( event.text, prefix7, sizeof( prefix7 ) - 1 ) == 0 )
    {
        valueCount = 7;
        cursor = event.text + sizeof( prefix7 ) - 1;
        outHasScaleFactor = false;
    }
    else if ( std::strncmp( event.text, prefix8, sizeof( prefix8 ) - 1 ) == 0 )
    {
        valueCount = 8;
        cursor = event.text + sizeof( prefix8 ) - 1;
        outHasScaleFactor = true;
    }
    else
    {
        return false;
    }

    float values[8] = {};
    for ( int i = 0; i < valueCount; ++i )
    {
        if ( !ReadReplayHexFloat( cursor, values[i] ) )
        {
            return false;
        }
    }

    outPosition = Vector3( values[0], values[1], values[2] );
    outOrientation = Quaternion( values[3], values[4], values[5], values[6] );
    outOrientation.Normalise();
    outScaleFactor = outHasScaleFactor ? values[7] : 1.0f;
    return true;
}


const ReplayV2SolverHashSample* FindReplaySolverHashForFrame( const std::vector<ReplayV2SolverHashSample>& hashes,
                                                              ReplayFrameIndex frameIndex )
{
    for ( const ReplayV2SolverHashSample& hash : hashes )
    {
        if ( hash.frameIndex == frameIndex )
        {
            return &hash;
        }
    }
    return nullptr;
}

const ReplayPresentationSample* FindReplayPresentationForFrame( const std::vector<ReplayPresentationSample>& samples,
                                                                ReplayFrameIndex frameIndex )
{
    for ( const ReplayPresentationSample& sample : samples )
    {
        if ( sample.frameIndex == frameIndex )
        {
            return &sample;
        }
    }
    return nullptr;
}

void WriteReplayProbeReason( char* outReason, std::size_t reasonSize, const char* reason )
{
    if ( outReason && reasonSize > 0 )
    {
        strncpy_s( outReason, reasonSize, reason ? reason : "event replay failed", _TRUNCATE );
    }
}

void LogReplayV2TargetRestoreDiagnostic( DiagnosticsRuntime& diagnosticsRuntime,
                                         RunSceneState& scene,
                                         const char* restoreSource,
                                         ReplayFrameIndex requestedFrame,
                                         ReplayFrameIndex latestNonCheckpointTarget,
                                         const char* failureReason,
                                         const ReplayV2SolverHashSample* diagnosticTarget,
                                         const ReplaySolverFrameSample* diagnosticCheckpoint,
                                         uint64_t restoredSolverHash,
                                         uint64_t restoredPresentationHash,
                                         std::size_t restoredBodyCount,
                                         bool hashCaptured,
                                         bool hashMatched,
                                         bool fallbackAttempted,
                                         bool fallbackRestored )
{
#ifdef _DEBUG
    const ReplayFrameIndex targetFrame = diagnosticTarget
                                             ? diagnosticTarget->frameIndex
                                             : ( requestedFrame == latestNonCheckpointTarget ? 0 : requestedFrame );
    diagnosticsRuntime.LogReplayRestoreResult( scene,
                                               restoreSource,
                                               targetFrame,
                                               diagnosticTarget ? diagnosticTarget->sceneFrame : scene.currentFrame,
                                               diagnosticCheckpoint ? diagnosticCheckpoint->frameIndex : 0,
                                               diagnosticTarget ? diagnosticTarget->solverHash : 0,
                                               diagnosticTarget ? diagnosticTarget->presentationHash : 0,
                                               diagnosticTarget ? diagnosticTarget->bodyCount : 0,
                                               restoredSolverHash,
                                               restoredPresentationHash,
                                               restoredBodyCount,
                                               diagnosticCheckpoint ? diagnosticCheckpoint->contactCount : 0,
                                               diagnosticCheckpoint ? diagnosticCheckpoint->pipelineRecordCount : 0,
                                               diagnosticCheckpoint ? diagnosticCheckpoint->checkpointBoundary : false,
                                               hashCaptured,
                                               hashMatched,
                                               fallbackAttempted,
                                               fallbackRestored,
                                               failureReason );
#else
    (void)diagnosticsRuntime;
    (void)scene;
    (void)restoreSource;
    (void)requestedFrame;
    (void)latestNonCheckpointTarget;
    (void)failureReason;
    (void)diagnosticTarget;
    (void)diagnosticCheckpoint;
    (void)restoredSolverHash;
    (void)restoredPresentationHash;
    (void)restoredBodyCount;
    (void)hashCaptured;
    (void)hashMatched;
    (void)fallbackAttempted;
    (void)fallbackRestored;
#endif
}

struct ReplayRestoreEventContext
{
    RuntimeTools& runtimeTools;
    RunSceneState& scene;
    SkullbonezCore::Assets::AssetSystem& assets;
    SceneTerrain& terrain;
    SkullbonezCore::Environment::WorldEnvironment& world;
    SkullbonezCore::Runtime::SceneController& models;
    PhysicsEngine& physics;
    int gameModelCapacity = 0;
};

bool TryApplyReplayRestoreWorldLauncherEvent( ReplayRestoreEventContext& context,
                                              const ReplayEventSample& event,
                                              char* eventOutReason,
                                              std::size_t eventReasonSize,
                                              bool& handled )
{
    handled = true;
    switch ( event.kind )
    {
    case ReplayEventKind::WorldOverride:
        if ( event.flags & REPLAY_WORLD_OVERRIDE_GRAVITY_CHANGED )
        {
            context.world.SetGravity( ReplayEventFloatFromBits( event.value0 ) );
        }
        if ( event.flags & REPLAY_WORLD_OVERRIDE_FLUID_HEIGHT_CHANGED )
        {
            context.world.SetFluidSurfaceHeight( ReplayEventFloatFromBits( event.value1 ) );
        }
        if ( event.flags & REPLAY_WORLD_OVERRIDE_FLUID_DENSITY_CHANGED )
        {
            context.world.SetFluidDensity( ReplayEventFloatFromBits( event.value2 ) );
        }
        WriteReplayProbeReason( eventOutReason, eventReasonSize, "applied world override" );
        return true;
    case ReplayEventKind::LauncherConfig:
        context.runtimeTools.RayCastTest().impulseStrength = ReplayEventFloatFromBits( event.value0 );
        context.runtimeTools.RayCastTest().projectileSpeed = ReplayEventFloatFromBits( event.value1 );
        WriteReplayProbeReason( eventOutReason, eventReasonSize, "applied launcher config" );
        return true;
    case ReplayEventKind::LauncherFire:
    {
        Vector3 rayOrigin;
        Vector3 rayDirection;
        Vector3 cameraUp;
        if ( !DecodeReplayRay9Payload( event, rayOrigin, rayDirection, cameraUp ) )
        {
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "invalid launcher fire payload" );
            return false;
        }
        context.runtimeTools.RayCastTest().fireMode = ( event.flags & REPLAY_LAUNCHER_FIRE_PROJECTILE ) != 0
                                                          ? RunLauncherFireMode::Projectile
                                                          : RunLauncherFireMode::Laser;
        context.runtimeTools.RayCastTest().impulseStrength = ReplayEventFloatFromBits( event.value1 );
        context.runtimeTools.RayCastTest().projectileSpeed = ReplayEventFloatFromBits( event.value2 );
        // Why: RuntimeTools now fails closed unless Run has completed the cold
        // collection-to-store topology repair at the owner boundary.
        const bool launcherStoresReady = context.models.RepairPhysicsBodyAndColliderTopology();
        if ( launcherStoresReady && context.runtimeTools.FireLauncherRay( context.models,
                                                                          context.physics,
                                                                          context.scene,
                                                                          context.terrain.Get(),
                                                                          context.gameModelCapacity,
                                                                          rayOrigin,
                                                                          rayDirection,
                                                                          cameraUp ) )
        {
            context.scene.modelCount = context.models.SceneEntityCount();
        }
        WriteReplayProbeReason( eventOutReason, eventReasonSize, "applied launcher fire" );
        return true;
    }
    case ReplayEventKind::GeneratedSceneConfig:
        if ( context.scene.modelCount != event.value0 || context.scene.solverBallCount != event.value1 ||
             context.scene.solverBoxCount != event.value2 ||
             static_cast<int32_t>( context.scene.rngSeed ) != event.value3 )
        {
            WriteReplayProbeReason( eventOutReason,
                                    eventReasonSize,
                                    "generated scene config event does not match live state" );
            return false;
        }
        WriteReplayProbeReason( eventOutReason, eventReasonSize, "verified generated scene config" );
        return true;
    default:
        handled = false;
        return false;
    }
}

// Lifetime: restore borrows live editor/scene owners for one decoded event.
// The request callback remains separate because it is operation sequencing,
// not part of the editor-place value payload.
struct ReplayRestoreEditorPlaceEventDesc
{
    RuntimeTools& runtimeTools;
    SkullbonezCore::Runtime::SceneController& models;
    PhysicsEngine& physics;
    RunSceneState& scene;
    SkullbonezCore::Environment::WorldEnvironment& world;
    SkullbonezCore::Assets::AssetSystem& assets;
    SceneTerrain& terrain;
    int gameModelCapacity = 0;
    const ReplayEventSample& event;
    char* eventOutReason = nullptr;
    std::size_t eventReasonSize = 0;
};

template <typename RequestInteractiveScene>
bool ApplyReplayRestoreEditorPlaceEvent( const ReplayRestoreEditorPlaceEventDesc& desc,
                                         RequestInteractiveScene requestInteractiveScene )
{
    RuntimeTools& runtimeTools = desc.runtimeTools;
    SkullbonezCore::Runtime::SceneController& models = desc.models;
    PhysicsEngine& physics = desc.physics;
    RunSceneState& scene = desc.scene;
    SkullbonezCore::Environment::WorldEnvironment& world = desc.world;
    SkullbonezCore::Assets::AssetSystem& assets = desc.assets;
    SceneTerrain& terrain = desc.terrain;
    const ReplayEventSample& event = desc.event;
    char* eventOutReason = desc.eventOutReason;
    const std::size_t eventReasonSize = desc.eventReasonSize;
    Vector3 terrainPoint;
    Vector3 placementScale;
    float placementYawRadians = 0.0f;
    if ( !DecodeReplayPlacePayload( event, terrainPoint, placementScale, placementYawRadians ) )
    {
        WriteReplayProbeReason( eventOutReason, eventReasonSize, "invalid editor placement payload" );
        return false;
    }

    const int modelCountBefore = models.SceneEntityCount();
    if ( event.value3 != modelCountBefore )
    {
        WriteReplayProbeReason( eventOutReason, eventReasonSize, "editor placement model count precondition mismatch" );
        return false;
    }

    const Vector3 previousPlacementScale = runtimeTools.Editor().placementScale;
    const bool previousTerrainAlign = runtimeTools.Editor().autoTerrainAlign;
    const float previousPlacementYawRadians = runtimeTools.Editor().placementYawRadians;
    runtimeTools.Editor().placementScale = placementScale;
    runtimeTools.Editor().autoTerrainAlign = ( event.flags & REPLAY_EDITOR_PLACE_TERRAIN_ALIGN ) != 0;
    runtimeTools.Editor().placementYawRadians = placementYawRadians;
    EditorObjectPlacementContext placementContext{ runtimeTools.Editor(),
                                                   models,
                                                   physics,
                                                   scene,
                                                   world,
                                                   terrain.Get(),
                                                   assets,
                                                   desc.gameModelCapacity };
    EditorObjectPlacementRequest placementRequest{ event.value0,
                                                   ( event.flags & REPLAY_EDITOR_PLACE_FIXED ) != 0,
                                                   terrainPoint };
    EditorObjectPlacementResult placementResult;
    bool placed = false;
    if ( CanPlaceEditorObjectAtTerrainPoint( placementContext, placementRequest ) )
    {
        requestInteractiveScene();
        placed = PlaceEditorObjectAtTerrainPoint( placementContext, placementRequest, placementResult );
    }
    runtimeTools.Editor().placementScale = previousPlacementScale;
    runtimeTools.Editor().autoTerrainAlign = previousTerrainAlign;
    runtimeTools.Editor().placementYawRadians = previousPlacementYawRadians;
    if ( !placed )
    {
        WriteReplayProbeReason( eventOutReason, eventReasonSize, "failed to replay editor placement" );
        return false;
    }
    WriteReplayProbeReason( eventOutReason, eventReasonSize, "applied editor placement" );
    return true;
}

bool ApplyReplayRestoreEditorTransformEvent( SkullbonezCore::Runtime::SceneController& models,
                                             PhysicsEngine& physics,
                                             const ReplayEventSample& event,
                                             char* eventOutReason,
                                             std::size_t eventReasonSize )
{
    // Concept: v2 restore replays editor transforms by editing the authoritative
    // PhysicsBodyStore and ColliderStore rows. Presentation samples are only
    // validation targets; they are not allowed to become collision authority.
    if ( event.flags == 0 || ( event.flags & ~REPLAY_EDITOR_TRANSFORM_SUPPORTED ) != 0 )
    {
        WriteReplayProbeReason( eventOutReason, eventReasonSize, "unsupported editor transform flags" );
        return false;
    }

    Vector3 position;
    Quaternion orientation;
    float scaleFactor = 1.0f;
    bool hasScaleFactor = false;
    if ( !DecodeReplayTransformPayload( event, position, orientation, scaleFactor, hasScaleFactor ) )
    {
        WriteReplayProbeReason( eventOutReason, eventReasonSize, "invalid editor transform payload" );
        return false;
    }
    if ( ( event.flags & REPLAY_EDITOR_TRANSFORM_SCALE ) != 0 &&
         ( !hasScaleFactor || event.value3 < 0 || event.value3 > 2 || !std::isfinite( scaleFactor ) ||
           scaleFactor <= 0.0f ) )
    {
        WriteReplayProbeReason( eventOutReason, eventReasonSize, "invalid editor transform scale payload" );
        return false;
    }

    if ( event.value2 != models.SceneEntityCount() )
    {
        WriteReplayProbeReason( eventOutReason, eventReasonSize, "editor transform model count precondition mismatch" );
        return false;
    }
    if ( event.value0 < 0 || event.value0 >= models.SceneEntityCount() )
    {
        WriteReplayProbeReason( eventOutReason, eventReasonSize, "editor transform model index is out of range" );
        return false;
    }

    const PhysicsBodyStore& bodyStoreBeforeEdit = models.BodyStore();
    const PhysicsBodyHandle eventBody =
        bodyStoreBeforeEdit.HandleForReplayBodyId( static_cast<uint32_t>( event.value1 ), event.value0 );
    const PhysicsBodyRecord* eventBodyRecord = bodyStoreBeforeEdit.RecordForHandle( eventBody );
    if ( !eventBodyRecord || bodyStoreBeforeEdit.ModelIndexForHandle( eventBody ) != event.value0 ||
         eventBodyRecord->replayBodyId != static_cast<uint32_t>( event.value1 ) )
    {
        WriteReplayProbeReason( eventOutReason, eventReasonSize, "editor transform replay body id mismatch" );
        return false;
    }

    PhysicsBodyUpdateDesc bodyEdit;
    bodyEdit.body = eventBody;
    const std::size_t eventBodyIndex = static_cast<std::size_t>( event.value0 );
    const auto hotFields = bodyStoreBeforeEdit.HotFields();
    bodyEdit.position = PhysicsBodyPosition( hotFields, eventBodyIndex );
    bodyEdit.orientation = PhysicsBodyOrientation( hotFields, eventBodyIndex );
    if ( event.flags & REPLAY_EDITOR_TRANSFORM_TRANSLATE )
    {
        bodyEdit.updateMask |= PHYSICS_BODY_UPDATE_POSE;
        bodyEdit.position = position;
    }
    if ( event.flags & REPLAY_EDITOR_TRANSFORM_ROTATE )
    {
        bodyEdit.updateMask |= PHYSICS_BODY_UPDATE_POSE;
        bodyEdit.orientation = orientation;
    }
    PhysicsColliderCreateDesc editedColliderDesc;
    bool hasEditedColliderDesc = false;
    if ( event.flags & REPLAY_EDITOR_TRANSFORM_SCALE )
    {
        const ColliderRecord* colliderBeforeScale =
            TryGetEditorTransformColliderRecord( models,
                                                 PhysicsColliderHandle{},
                                                 event.value0,
                                                 eventBodyRecord->replayBodyId );
        if ( !colliderBeforeScale )
        {
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "editor transform collider row missing" );
            return false;
        }
        const CollisionShape baseShape = colliderBeforeScale->shape;
        CollisionShape scaledShape;
        if ( !ScaleShapeAxisFromBase( baseShape, event.value3, scaleFactor, scaledShape ) )
        {
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "failed to replay editor transform scale" );
            return false;
        }
        // Invariant: restore reuses the previous collider material and replaces
        // only the decoded scale shape, keeping replay payload semantics
        // independent from legacy model-side recapture.
        editedColliderDesc = MakeColliderCreateDesc( std::move( scaledShape ),
                                                     colliderBeforeScale->restitution,
                                                     colliderBeforeScale->contactMaterialId );
        hasEditedColliderDesc = true;
    }
    bodyEdit.updateMask |= PHYSICS_BODY_UPDATE_VELOCITY;
    bodyEdit.linearVelocity = Vector3( 0.0f, 0.0f, 0.0f );
    bodyEdit.angularVelocity = Vector3( 0.0f, 0.0f, 0.0f );
    if ( hasEditedColliderDesc )
    {
        if ( !physics.UpdateAuthoredBodyAndCollider( bodyEdit, std::move( editedColliderDesc ) ) )
        {
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "editor transform body/collider update failed" );
            return false;
        }
    }
    else if ( !physics.UpdateAuthoredBody( bodyEdit ) )
    {
        WriteReplayProbeReason( eventOutReason, eventReasonSize, "editor transform body update failed" );
        return false;
    }
    // Why: the edited-state commit has already refreshed the edited body row.
    // The wake decision should read the committed PhysicsBodyStore record, not
    // presentation/authored pose data.
    const PhysicsBodyStore& bodyStore = SkullbonezCore::Physics::PhysicsEngine::ReadBodies( physics );
    const PhysicsBodyHandle body =
        bodyStore.HandleForReplayBodyId( static_cast<uint32_t>( event.value1 ), event.value0 );
    const PhysicsBodyRecord* bodyRecord = bodyStore.RecordForHandle( body );
    const int bodyIndex = bodyStore.ModelIndexForHandle( body );
    if ( bodyRecord && bodyIndex >= 0 && bodyStore.HotFields().fixed[static_cast<std::size_t>( bodyIndex )] == 0u )
    {
        physics.WakeBody( body );
    }
    WriteReplayProbeReason( eventOutReason, eventReasonSize, "applied editor transform" );
    return true;
}

// Concept: target restore replays only solver-relevant timeline events. Runtime
// commands that would change scenes stay rejected here, while editor placement
// emits an application-mode request to the owning replay transaction.
template <typename RequestInteractiveScene>
bool ApplyReplayRestoreEventForTarget( ReplayRestoreEventContext& context,
                                       const ReplayEventSample& event,
                                       char* eventOutReason,
                                       std::size_t eventReasonSize,
                                       RequestInteractiveScene requestInteractiveScene )
{
    if ( event.payloadVersion != 1 )
    {
        WriteReplayProbeReason( eventOutReason, eventReasonSize, "unsupported replay event payload version" );
        return false;
    }

    bool restoreEventHandled = false;
    if ( TryApplyReplayRestoreWorldLauncherEvent( context,
                                                  event,
                                                  eventOutReason,
                                                  eventReasonSize,
                                                  restoreEventHandled ) )
    {
        return true;
    }
    if ( restoreEventHandled )
    {
        return false;
    }

    switch ( event.kind )
    {
    case ReplayEventKind::TimelineStart:
        WriteReplayProbeReason( eventOutReason, eventReasonSize, "ignored" );
        return true;
    case ReplayEventKind::OwnerAction:
    {
        const ReplayOwnerEventCode ownerEvent = static_cast<ReplayOwnerEventCode>( event.value0 );
        switch ( ownerEvent )
        {
        case ReplayOwnerEventCode::CaptureScreenshot:
        case ReplayOwnerEventCode::SceneSaveDefaults:
        case ReplayOwnerEventCode::RenderSaveOrdinaryDefaults:
        case ReplayOwnerEventCode::RenderSaveCinematicDefaults:
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "ignored non-solver owner action" );
            return true;
        case ReplayOwnerEventCode::SceneReset:
        case ReplayOwnerEventCode::SceneLoadBrowserIndex:
        case ReplayOwnerEventCode::SceneLoadDemo:
        case ReplayOwnerEventCode::SceneCreate:
        default:
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "unsupported scene-owner timeline mutation" );
            return false;
        }
    }
    case ReplayEventKind::BranchRestore:
        WriteReplayProbeReason( eventOutReason, eventReasonSize, "unsupported timeline mutation event" );
        return false;
    case ReplayEventKind::EditorPlace:
        return ApplyReplayRestoreEditorPlaceEvent(
            ReplayRestoreEditorPlaceEventDesc{ .runtimeTools = context.runtimeTools,
                                               .models = context.models,
                                               .physics = context.physics,
                                               .scene = context.scene,
                                               .world = context.world,
                                               .assets = context.assets,
                                               .terrain = context.terrain,
                                               .gameModelCapacity = context.gameModelCapacity,
                                               .event = event,
                                               .eventOutReason = eventOutReason,
                                               .eventReasonSize = eventReasonSize },
            requestInteractiveScene );
    case ReplayEventKind::EditorTransform:
        return ApplyReplayRestoreEditorTransformEvent( context.models,
                                                       context.physics,
                                                       event,
                                                       eventOutReason,
                                                       eventReasonSize );
    default:
        WriteReplayProbeReason( eventOutReason, eventReasonSize, "unsupported replay event kind" );
        return false;
    }
}

struct ReplayRestoreArtifactData
{
    std::vector<ReplaySolverFrameSample> checkpoints;
    std::vector<ReplayV2SolverHashSample> hashes;
    std::vector<ReplayEventSample> events;
    std::vector<ReplayPresentationSample> presentationSamples;
    ReplayV2SolverCheckpointLoadResult checkpointResult;
    ReplayV2SolverHashLoadResult hashResult;
    ReplayV2EventLoadResult eventResult;
    ReplayV2LoadResult presentationResult;
};

bool LoadReplayRestoreArtifactData( const char* path,
                                    ReplayRestoreArtifactData& artifact,
                                    char* outReason,
                                    std::size_t reasonSize )
{
    if ( !ReplayV2Artifact::LoadSolverCheckpoints( path, artifact.checkpoints, &artifact.checkpointResult ) )
    {
        WriteReplayProbeReason( outReason, reasonSize, "failed to load v2 solver checkpoints" );
        return false;
    }
    if ( !ReplayV2Artifact::LoadSolverHashes( path, artifact.hashes, &artifact.hashResult ) )
    {
        WriteReplayProbeReason( outReason, reasonSize, "failed to load v2 solver hashes" );
        return false;
    }
    if ( !ReplayV2Artifact::LoadEvents( path, artifact.events, &artifact.eventResult ) )
    {
        WriteReplayProbeReason( outReason, reasonSize, "failed to load v2 events" );
        return false;
    }
    if ( !ReplayV2Artifact::LoadPresentation( path, artifact.presentationSamples, &artifact.presentationResult ) )
    {
        WriteReplayProbeReason( outReason, reasonSize, "failed to load v2 presentation frames" );
        return false;
    }
    WriteReplayProbeReason( outReason, reasonSize, "" );
    return true;
}

bool SelectReplayRestoreTargetAndCheckpoint( const ReplayRestoreArtifactData& artifact,
                                             ReplayFrameIndex requestedFrame,
                                             ReplayFrameIndex latestNonCheckpointTarget,
                                             const ReplayV2SolverHashSample*& outTarget,
                                             const ReplaySolverFrameSample*& outCheckpoint,
                                             char* outReason,
                                             std::size_t reasonSize )
{
    outTarget = nullptr;
    outCheckpoint = nullptr;
    if ( requestedFrame == latestNonCheckpointTarget )
    {
        for ( auto it = artifact.hashes.rbegin(); it != artifact.hashes.rend(); ++it )
        {
            if ( !it->checkpointBoundary )
            {
                outTarget = &*it;
                break;
            }
        }
        if ( !outTarget )
        {
            WriteReplayProbeReason( outReason, reasonSize, "found no saved non-checkpoint target hash" );
            return false;
        }
    }
    else
    {
        for ( const ReplayV2SolverHashSample& hash : artifact.hashes )
        {
            if ( hash.frameIndex == requestedFrame )
            {
                outTarget = &hash;
                break;
            }
        }
        if ( !outTarget )
        {
            char message[192] = {};
            sprintf_s( message,
                       sizeof( message ),
                       "found no saved hash for requested target frame %llu",
                       static_cast<unsigned long long>( requestedFrame ) );
            WriteReplayProbeReason( outReason, reasonSize, message );
            return false;
        }
    }

    for ( const ReplaySolverFrameSample& candidate : artifact.checkpoints )
    {
        if ( candidate.frameIndex <= outTarget->frameIndex &&
             ( !outCheckpoint || candidate.frameIndex > outCheckpoint->frameIndex ) )
        {
            outCheckpoint = &candidate;
        }
    }
    if ( !outCheckpoint )
    {
        WriteReplayProbeReason( outReason, reasonSize, "found no checkpoint before target hash" );
        return false;
    }
    if ( outCheckpoint->frameIndex > outTarget->frameIndex )
    {
        WriteReplayProbeReason( outReason, reasonSize, "selected checkpoint after target frame" );
        return false;
    }
    if ( outCheckpoint->eventCursor == 0 )
    {
        WriteReplayProbeReason( outReason, reasonSize, "loaded a checkpoint without an event cursor" );
        return false;
    }
    if ( outTarget->frameIndex - outCheckpoint->frameIndex >
         static_cast<ReplayFrameIndex>( artifact.hashes.size() + artifact.events.size() + 1u ) )
    {
        WriteReplayProbeReason( outReason, reasonSize, "selected an implausibly distant target frame" );
        return false;
    }
    WriteReplayProbeReason( outReason, reasonSize, "" );
    return true;
}

bool PrepareReplayRestoreArtifactSelection( const char* path,
                                            ReplayFrameIndex requestedFrame,
                                            ReplayFrameIndex latestNonCheckpointTarget,
                                            ReplayRestoreArtifactData& artifact,
                                            const ReplayV2SolverHashSample*& outTarget,
                                            const ReplaySolverFrameSample*& outCheckpoint,
                                            char* outReason,
                                            std::size_t reasonSize )
{
    if ( !path || path[0] == '\0' )
    {
        WriteReplayProbeReason( outReason, reasonSize, "replay v2 target restore requires a v2 artifact path" );
        return false;
    }
    if ( !LoadReplayRestoreArtifactData( path, artifact, outReason, reasonSize ) )
    {
        return false;
    }
    return SelectReplayRestoreTargetAndCheckpoint( artifact,
                                                   requestedFrame,
                                                   latestNonCheckpointTarget,
                                                   outTarget,
                                                   outCheckpoint,
                                                   outReason,
                                                   reasonSize );
}

bool ReplayCheckpointTopologyMatchesLive( const ReplaySolverFrameSample& checkpoint,
                                          const SkullbonezCore::Runtime::SceneController& models )
{
    const int liveModelCount = models.SceneEntityCount();
    if ( checkpoint.bodies.size() > static_cast<std::size_t>( liveModelCount ) )
    {
        return false;
    }
    for ( const ReplaySolverBodySample& body : checkpoint.bodies )
    {
        if ( body.modelRow.value < 0 || body.modelRow.value >= liveModelCount )
        {
            return false;
        }
        const PhysicsBodyRecord* bodyRecord = TryGetReplayProbeBodyRecord( models, body.modelRow.value );
        if ( !bodyRecord || bodyRecord->replayBodyId != body.id.value )
        {
            return false;
        }
    }
    return true;
}

const ReplayEventSample* FindReplayGeneratedSceneConfigBeforeCheckpoint( const std::vector<ReplayEventSample>& events,
                                                                         const ReplaySolverFrameSample& checkpoint )
{
    const ReplayEventSample* generatedConfig = nullptr;
    for ( const ReplayEventSample& event : events )
    {
        if ( event.kind != ReplayEventKind::GeneratedSceneConfig || event.frameIndex > checkpoint.frameIndex ||
             event.sequence >= checkpoint.eventCursor )
        {
            continue;
        }
        if ( event.branch.branchId != checkpoint.branch.branchId )
        {
            continue;
        }
        generatedConfig = &event;
    }
    return generatedConfig;
}

class ScopedReplayProbeProfilerFrame
{
  public:
    explicit ScopedReplayProbeProfilerFrame( SkullbonezCore::Core::Profiler* profiler ) : m_profiler( profiler )
    {
        PROFILE_FRAME_BEGIN( m_profiler );
    }
    ~ScopedReplayProbeProfilerFrame()
    {
        PROFILE_FRAME_END( m_profiler );
    }

  private:
    SkullbonezCore::Core::Profiler* m_profiler;
};

void FormatReplayRestoreDivergenceMessage( char* message,
                                           std::size_t messageSize,
                                           ReplayFrameIndex currentFrame,
                                           uint64_t restoredSolverHash,
                                           uint64_t restoredPresentationHash,
                                           std::size_t restoredBodyCount,
                                           const ReplayV2SolverHashSample& expectedHash,
                                           const std::vector<ReplayPresentationSample>& presentationSamples,
                                           const SkullbonezCore::Runtime::SceneController& models,
                                           std::size_t eventsApplied )
{
    const ReplayPresentationSample* expectedPresentation =
        FindReplayPresentationForFrame( presentationSamples, currentFrame );
    const PhysicsBodyRecord* restoredBody = TryGetReplayProbeBodyRecord( models, 0 );
    if ( expectedPresentation && !expectedPresentation->bodies.empty() && restoredBody )
    {
        const ReplayBodyPresentationSample& expectedBody = expectedPresentation->bodies[0];
        const auto hotFields = models.BodyStore().HotFields();
        const Vector3 restoredPosition = PhysicsBodyPosition( hotFields, 0u );
        const Vector3 restoredVelocity = PhysicsBodyLinearVelocity( hotFields, 0u );
        float restoredQx = 0.0f;
        float restoredQy = 0.0f;
        float restoredQz = 0.0f;
        float restoredQw = 1.0f;
        PhysicsBodyOrientation( hotFields, 0u ).GetComponents( restoredQx, restoredQy, restoredQz, restoredQw );

        // Why: body 0 gives replay-restore failures a stable first mismatch to
        // compare against the saved presentation track without dumping the full
        // checkpoint payload into the validation log.
        sprintf_s( message,
                   messageSize,
                   "replay restore target probe diverged at frame %llu: restored=0x%016llX "
                   "expected=0x%016llX restored_presentation=0x%016llX expected_presentation=0x%016llX "
                   "restored_pos=(%.6f,%.6f,%.6f) expected_pos=(%.6f,%.6f,%.6f) "
                   "restored_vel=(%.6f,%.6f,%.6f) restored_q=(%.6f,%.6f,%.6f,%.6f) "
                   "expected_q=(%.6f,%.6f,%.6f,%.6f) restored_body_id=%u expected_body_id=%u "
                   "events_applied=%llu",
                   static_cast<unsigned long long>( currentFrame ),
                   static_cast<unsigned long long>( restoredSolverHash ),
                   static_cast<unsigned long long>( expectedHash.solverHash ),
                   static_cast<unsigned long long>( restoredPresentationHash ),
                   static_cast<unsigned long long>( expectedHash.presentationHash ),
                   restoredPosition.x,
                   restoredPosition.y,
                   restoredPosition.z,
                   expectedBody.position.x,
                   expectedBody.position.y,
                   expectedBody.position.z,
                   restoredVelocity.x,
                   restoredVelocity.y,
                   restoredVelocity.z,
                   restoredQx,
                   restoredQy,
                   restoredQz,
                   restoredQw,
                   expectedBody.orientation[0],
                   expectedBody.orientation[1],
                   expectedBody.orientation[2],
                   expectedBody.orientation[3],
                   restoredBody->replayBodyId,
                   expectedBody.id.value,
                   static_cast<unsigned long long>( eventsApplied ) );
    }
    else
    {
        sprintf_s( message,
                   messageSize,
                   "replay restore target probe diverged at frame %llu: restored=0x%016llX "
                   "expected=0x%016llX restored_presentation=0x%016llX expected_presentation=0x%016llX "
                   "restored_bodies=%llu expected_bodies=%u events_applied=%llu",
                   static_cast<unsigned long long>( currentFrame ),
                   static_cast<unsigned long long>( restoredSolverHash ),
                   static_cast<unsigned long long>( expectedHash.solverHash ),
                   static_cast<unsigned long long>( restoredPresentationHash ),
                   static_cast<unsigned long long>( expectedHash.presentationHash ),
                   static_cast<unsigned long long>( restoredBodyCount ),
                   expectedHash.bodyCount,
                   static_cast<unsigned long long>( eventsApplied ) );
    }
}

struct ReplayRestoreStepContext
{
    RuntimeTools& runtimeTools;
    SceneController& sceneController;
    RunSceneState& scene;
    const SkullbonezCore::Core::EngineConfig& config;
    SkullbonezCore::Assets::AssetSystem& assets;
    SkullbonezCore::Threading::WorkerPool& workerPool;
    SkullbonezCore::Environment::WorldEnvironment& world;
    SkullbonezCore::Runtime::SceneController& models;
    ReplayRestoreEventContext& eventContext;
    const ReplayRestoreArtifactData& artifact;
    const ReplaySolverFrameSample& checkpoint;
    const ReplayV2SolverHashSample& target;
};

struct ReplayRestoreStepResult
{
    ReplayFrameIndex currentFrame = 0;
    int currentSceneFrame = 0;
    uint32_t eventCursor = 0;
    std::size_t eventsApplied = 0;
    std::size_t unsupportedEvents = 0;
};

struct ReplayRestoreStepFailure
{
    char message[1024] = {};
    const ReplayV2SolverHashSample* diagnosticTarget = nullptr;
    uint64_t restoredSolverHash = 0;
    uint64_t restoredPresentationHash = 0;
    std::size_t restoredBodyCount = 0;
    bool hashCaptured = false;
};

void WriteReplayRestoreStepFailure( ReplayRestoreStepFailure& failure,
                                    const char* message,
                                    const ReplayV2SolverHashSample* diagnosticTarget,
                                    uint64_t restoredSolverHash = 0,
                                    uint64_t restoredPresentationHash = 0,
                                    std::size_t restoredBodyCount = 0,
                                    bool hashCaptured = false )
{
    strncpy_s( failure.message, message ? message : "replay restore step failed", _TRUNCATE );
    failure.diagnosticTarget = diagnosticTarget;
    failure.restoredSolverHash = restoredSolverHash;
    failure.restoredPresentationHash = restoredPresentationHash;
    failure.restoredBodyCount = restoredBodyCount;
    failure.hashCaptured = hashCaptured;
}

// Concept: replay target restore rebuilds solver state by starting from a
// checkpoint and replaying only the saved branch events before each fixed
// physics step. The helper reports failure facts to ReplayRuntime so live-state
// rollback and diagnostic logging stay in the owning transaction.
template <typename CaptureCurrentReplaySolverHash, typename RequestInteractiveScene>
bool StepReplayRestoreTarget( ReplayRestoreStepContext& context,
                              SkullbonezCore::Core::Profiler* profiler,
                              ReplayRestoreStepResult& result,
                              ReplayRestoreStepFailure& failure,
                              CaptureCurrentReplaySolverHash captureCurrentReplaySolverHash,
                              RequestInteractiveScene requestInteractiveScene )
{
    result.currentFrame = context.checkpoint.frameIndex;
    result.currentSceneFrame = context.checkpoint.sceneFrame;
    result.eventCursor = context.checkpoint.eventCursor;
    result.eventsApplied = 0;
    result.unsupportedEvents = 0;
    context.scene.currentFrame = result.currentSceneFrame;

    ScopedReplayProbeProfilerFrame profilerFrame( profiler );
    while ( result.currentFrame < context.target.frameIndex )
    {
        const ReplayFrameIndex nextFrame = result.currentFrame + 1u;

        for ( const ReplayEventSample& event : context.artifact.events )
        {
            if ( event.frameIndex != nextFrame || event.sequence < result.eventCursor )
            {
                continue;
            }
            if ( event.branch.branchId != context.checkpoint.branch.branchId )
            {
                ++result.unsupportedEvents;
                continue;
            }

            char eventReason[160] = {};
            if ( !ApplyReplayRestoreEventForTarget( context.eventContext,
                                                    event,
                                                    eventReason,
                                                    sizeof( eventReason ),
                                                    requestInteractiveScene ) )
            {
                char message[320] = {};
                sprintf_s( message,
                           sizeof( message ),
                           "replay restore target probe failed to apply event sequence %u at frame %llu: %s",
                           event.sequence,
                           static_cast<unsigned long long>( event.frameIndex ),
                           eventReason[0] != '\0' ? eventReason : "unknown event replay failure" );
                WriteReplayRestoreStepFailure( failure, message, &context.target );
                return false;
            }
            result.eventCursor = (std::max)( result.eventCursor, event.sequence + 1u );
            ++result.eventsApplied;
        }

        context.runtimeTools.TickRayCastTestLines( PHYSICS_FIXED_DT );
        context.runtimeTools.Laser().Update( PHYSICS_FIXED_DT );
        context.models.EndCollisionVisualFrame();
        ++result.currentSceneFrame;
        context.scene.currentFrame = result.currentSceneFrame;
        context.models.BeginCollisionVisualFrame();

        const auto physicsWorldForces = context.world.GetPhysicsWorldForces();
        context.sceneController.StepPhysics( PHYSICS_FIXED_DT, context.config, physicsWorldForces, context.workerPool );
        result.currentFrame = nextFrame;

        const ReplayV2SolverHashSample* expectedHash =
            FindReplaySolverHashForFrame( context.artifact.hashes, result.currentFrame );
        if ( !expectedHash )
        {
            WriteReplayRestoreStepFailure( failure, "could not find stepped hash metadata", &context.target );
            return false;
        }

        ReplaySolverFrameSample stepReference;
        stepReference.frameIndex = expectedHash->frameIndex;
        stepReference.branch = context.checkpoint.branch;
        stepReference.eventCursor = result.eventCursor;
        stepReference.sceneFrame = expectedHash->sceneFrame;
        stepReference.simulationSeconds = expectedHash->simulationSeconds;
        stepReference.physicsDt = PHYSICS_FIXED_DT;

        uint64_t stepSolverHash = 0;
        uint64_t stepPresentationHash = 0;
        std::size_t stepBodyCount = 0;
        if ( !captureCurrentReplaySolverHash( stepReference, stepSolverHash, stepPresentationHash, stepBodyCount ) )
        {
            WriteReplayRestoreStepFailure( failure, "failed to capture stepped hash", expectedHash );
            return false;
        }
        if ( stepBodyCount != expectedHash->bodyCount || stepSolverHash != expectedHash->solverHash )
        {
            char message[1024] = {};
            FormatReplayRestoreDivergenceMessage( message,
                                                  sizeof( message ),
                                                  result.currentFrame,
                                                  stepSolverHash,
                                                  stepPresentationHash,
                                                  stepBodyCount,
                                                  *expectedHash,
                                                  context.artifact.presentationSamples,
                                                  context.models,
                                                  result.eventsApplied );
            WriteReplayRestoreStepFailure( failure,
                                           message,
                                           expectedHash,
                                           stepSolverHash,
                                           stepPresentationHash,
                                           stepBodyCount,
                                           true );
            return false;
        }
    }

    return true;
}

struct ReplayRestoreTargetHashResult
{
    uint64_t solverHash = 0;
    uint64_t presentationHash = 0;
    std::size_t bodyCount = 0;
};

struct ReplayRestoreTargetHashFailure
{
    char message[320] = {};
    ReplayRestoreTargetHashResult restored;
    bool hashCaptured = false;
};

template <typename CaptureCurrentReplaySolverHash>
bool CaptureAndValidateReplayRestoreTargetHash( const ReplayV2SolverHashSample& target,
                                                const ReplaySolverFrameSample& checkpoint,
                                                uint32_t eventCursor,
                                                ReplayRestoreTargetHashResult& result,
                                                ReplayRestoreTargetHashFailure& failure,
                                                CaptureCurrentReplaySolverHash captureCurrentReplaySolverHash )
{
    ReplaySolverFrameSample reference;
    reference.frameIndex = target.frameIndex;
    reference.branch = checkpoint.branch;
    reference.eventCursor = eventCursor;
    reference.sceneFrame = target.sceneFrame;
    reference.simulationSeconds = target.simulationSeconds;
    reference.physicsDt = PHYSICS_FIXED_DT;

    if ( !captureCurrentReplaySolverHash( reference, result.solverHash, result.presentationHash, result.bodyCount ) )
    {
        strncpy_s( failure.message, "failed to capture target hash", _TRUNCATE );
        failure.hashCaptured = false;
        return false;
    }
    failure.restored = result;
    failure.hashCaptured = true;
    if ( result.bodyCount != target.bodyCount )
    {
        sprintf_s( failure.message,
                   sizeof( failure.message ),
                   "replay restore target probe body count mismatch: restored=%llu expected=%u",
                   static_cast<unsigned long long>( result.bodyCount ),
                   target.bodyCount );
        return false;
    }
    if ( result.solverHash != target.solverHash )
    {
        sprintf_s( failure.message,
                   sizeof( failure.message ),
                   "replay restore target probe solver hash mismatch: restored=0x%016llX expected=0x%016llX",
                   static_cast<unsigned long long>( result.solverHash ),
                   static_cast<unsigned long long>( target.solverHash ) );
        return false;
    }
    return true;
}

void PopulateReplayRestoreTargetResult( RunReplayV2TargetRestoreResult& outResult,
                                        const ReplayRestoreArtifactData& artifact,
                                        const ReplaySolverFrameSample& checkpoint,
                                        const ReplayV2SolverHashSample& target,
                                        const ReplayRestoreStepResult& stepResult,
                                        const ReplayRestoreTargetHashResult& targetHash,
                                        bool generatedTopologyRebuilt )
{
    outResult.checkpointCount = artifact.checkpointResult.checkpointCount;
    outResult.eventCount = artifact.eventResult.eventCount;
    outResult.hashCount = artifact.hashResult.hashCount;
    outResult.eventsApplied = stepResult.eventsApplied;
    outResult.bodyCount = targetHash.bodyCount;
    outResult.fileBytes = artifact.hashResult.fileBytes;
    outResult.checkpointFrame = checkpoint.frameIndex;
    outResult.targetFrame = target.frameIndex;
    outResult.eventCursor = stepResult.eventCursor;
    outResult.solverHash = targetHash.solverHash;
    outResult.presentationHash = targetHash.presentationHash;
    outResult.generatedTopologyRebuilt = generatedTopologyRebuilt;
}

void LogReplayRestoreTargetSuccess( DiagnosticsRuntime& diagnosticsRuntime,
                                    RunSceneState& scene,
                                    const char* restoreSource,
                                    ReplayFrameIndex requestedFrame,
                                    ReplayFrameIndex latestNonCheckpointTarget,
                                    const ReplayV2SolverHashSample& target,
                                    const ReplaySolverFrameSample& checkpoint,
                                    const ReplayRestoreTargetHashResult& targetHash )
{
    LogReplayV2TargetRestoreDiagnostic( diagnosticsRuntime,
                                        scene,
                                        restoreSource,
                                        requestedFrame,
                                        latestNonCheckpointTarget,
                                        "",
                                        &target,
                                        &checkpoint,
                                        targetHash.solverHash,
                                        targetHash.presentationHash,
                                        targetHash.bodyCount,
                                        true,
                                        true,
                                        false,
                                        false );
}

struct ReplayRestoreOwnerContext
{
    RuntimeTools& runtimeTools;
    SimulationSystem& simulation;
    SceneController& sceneController;
    RunSceneState& scene;
    const SkullbonezCore::Core::EngineConfig& config;
    SkullbonezCore::Assets::AssetSystem& assets;
    SkullbonezCore::Threading::WorkerPool& workerPool;
    SkullbonezCore::Environment::WorldEnvironment& world;
    SkullbonezCore::Runtime::SceneController& models;
    GeneratedObjectTypeOverride& generatedObjectTypeOverride;
    int gameModelCapacity = 0;
};

bool RebuildReplayGeneratedSceneTopology( ReplayRestoreOwnerContext& context,
                                          const ReplayEventSample& event,
                                          const ReplaySolverFrameSample& checkpoint,
                                          char* rebuildReason,
                                          std::size_t rebuildReasonSize )
{
    if ( event.value0 < 0 || event.value1 < 0 || event.value2 < 0 || event.value3 <= 0 )
    {
        WriteReplayProbeReason( rebuildReason, rebuildReasonSize, "generated scene config contains invalid counts" );
        return false;
    }

    const uint32_t overrideBits =
        ( event.flags & REPLAY_GENERATED_SCENE_OVERRIDE_MASK ) >> REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT;
    if ( overrideBits > static_cast<uint32_t>( GeneratedObjectTypeOverride::AllBoxes ) )
    {
        WriteReplayProbeReason( rebuildReason, rebuildReasonSize, "generated scene config has invalid override bits" );
        return false;
    }

    const bool exactSolverCounts = ( event.flags & REPLAY_GENERATED_SCENE_EXACT_SOLVER_COUNTS ) != 0;
    const bool uiSolverCounts = ( event.flags & REPLAY_GENERATED_SCENE_UI_SOLVER_COUNTS ) != 0;
    const bool uiModelCount = ( event.flags & REPLAY_GENERATED_SCENE_UI_MODEL_COUNT ) != 0;
    if ( exactSolverCounts && event.value1 + event.value2 != event.value0 )
    {
        WriteReplayProbeReason( rebuildReason, rebuildReasonSize, "generated solver counts do not match model count" );
        return false;
    }
    if ( event.value0 > context.gameModelCapacity )
    {
        WriteReplayProbeReason( rebuildReason,
                                rebuildReasonSize,
                                "generated scene model count exceeds active capacity" );
        return false;
    }

    context.models.Clear();
    // Invariant: a restore-side generated rebuild is a fresh scene population,
    // even though it does not enter the full scene-load path. Reset the
    // scene-owned id cursor after the clear so regenerated
    // PhysicsSceneObjectId/replay ids match the checkpoint topology.
    context.scene.ResetSceneObjectIdCursor( context.models.BodyStore() );
    context.runtimeTools.ClearRayCastTestLines();
    context.simulation.Reset();
    context.scene.rngSeed = static_cast<unsigned int>( event.value3 );
    context.scene.rngState = static_cast<unsigned int>( event.value3 );
    context.generatedObjectTypeOverride = static_cast<GeneratedObjectTypeOverride>( overrideBits );
    context.sceneController.UIOverrides().modelCountOverride = uiModelCount ? event.value0 : -1;
    context.sceneController.UIOverrides().solverBallCountOverride =
        uiSolverCounts || exactSolverCounts ? event.value1 : -1;
    context.sceneController.UIOverrides().solverBoxCountOverride =
        uiSolverCounts || exactSolverCounts ? event.value2 : -1;

    if ( exactSolverCounts || uiSolverCounts )
    {
        const SkullbonezCore::Core::SbResult setupResult = SceneGeneratedSetup::SetUpSolverObjects(
            BuildSceneGeneratedModelContext( context.scene,
                                             context.config,
                                             context.world,
                                             context.sceneController.Terrain().Get(),
                                             context.models,
                                             context.sceneController.Physics(),
                                             context.generatedObjectTypeOverride ),
            event.value1,
            event.value2 );
        if ( !setupResult.ok )
        {
            WriteReplayProbeReason( rebuildReason, rebuildReasonSize, setupResult.error.message );
            return false;
        }
    }
    else
    {
        const SkullbonezCore::Core::SbResult setupResult = SceneGeneratedSetup::SetUpSceneEntities(
            BuildSceneGeneratedModelContext( context.scene,
                                             context.config,
                                             context.world,
                                             context.sceneController.Terrain().Get(),
                                             context.models,
                                             context.sceneController.Physics(),
                                             context.generatedObjectTypeOverride ),
            event.value0 );
        if ( !setupResult.ok )
        {
            WriteReplayProbeReason( rebuildReason, rebuildReasonSize, setupResult.error.message );
            return false;
        }
    }
    if ( !ReplayCheckpointTopologyMatchesLive( checkpoint, context.models ) )
    {
        WriteReplayProbeReason( rebuildReason,
                                rebuildReasonSize,
                                "rebuilt generated topology still mismatches checkpoint" );
        return false;
    }
    WriteReplayProbeReason( rebuildReason, rebuildReasonSize, "rebuilt generated topology" );
    return true;
}

bool EnsureReplayRestoreCheckpointTopology( ReplayRestoreOwnerContext& context,
                                            const ReplayRestoreArtifactData& artifact,
                                            const ReplaySolverFrameSample& checkpoint,
                                            bool& generatedTopologyRebuilt,
                                            bool& stateMutated,
                                            char* outReason,
                                            std::size_t reasonSize )
{
    generatedTopologyRebuilt = false;
    if ( ReplayCheckpointTopologyMatchesLive( checkpoint, context.models ) )
    {
        WriteReplayProbeReason( outReason, reasonSize, "" );
        return true;
    }

    const ReplayEventSample* generatedConfig =
        FindReplayGeneratedSceneConfigBeforeCheckpoint( artifact.events, checkpoint );
    if ( !generatedConfig )
    {
        WriteReplayProbeReason( outReason,
                                reasonSize,
                                "checkpoint topology does not match live scene and no generated config was saved" );
        return false;
    }

    char rebuildReason[160] = {};
    stateMutated = true;
    if ( !RebuildReplayGeneratedSceneTopology( context,
                                               *generatedConfig,
                                               checkpoint,
                                               rebuildReason,
                                               sizeof( rebuildReason ) ) )
    {
        sprintf_s( outReason,
                   reasonSize,
                   "failed to rebuild generated scene topology: %s",
                   rebuildReason[0] != '\0' ? rebuildReason : "unknown rebuild failure" );
        return false;
    }
    generatedTopologyRebuilt = true;
    WriteReplayProbeReason( outReason, reasonSize, "" );
    return true;
}

// Why: restore owns scene, world, and model side effects while stepping toward a
// target. ReplayRuntime remains the transaction coordinator and these borrowed
// contexts exist only for the duration of that cold restore command.
template <typename CaptureCurrentReplaySolverHash, typename RequestInteractiveScene>
bool RunReplayRestoreTargetStep( ReplayRestoreOwnerContext& context,
                                 SkullbonezCore::Core::Profiler* profiler,
                                 const ReplayRestoreArtifactData& artifact,
                                 const ReplaySolverFrameSample& checkpoint,
                                 const ReplayV2SolverHashSample& target,
                                 ReplayRestoreStepResult& stepResult,
                                 ReplayRestoreStepFailure& stepFailure,
                                 CaptureCurrentReplaySolverHash captureCurrentReplaySolverHash,
                                 RequestInteractiveScene requestInteractiveScene )
{
    ReplayRestoreEventContext restoreEventContext{ context.runtimeTools,
                                                   context.scene,
                                                   context.assets,
                                                   context.sceneController.Terrain(),
                                                   context.world,
                                                   context.models,
                                                   context.sceneController.Physics(),
                                                   context.gameModelCapacity };
    ReplayRestoreStepContext stepContext{ context.runtimeTools,
                                          context.sceneController,
                                          context.scene,
                                          context.config,
                                          context.assets,
                                          context.workerPool,
                                          context.world,
                                          context.models,
                                          restoreEventContext,
                                          artifact,
                                          checkpoint,
                                          target };
    if ( !StepReplayRestoreTarget( stepContext,
                                   profiler,
                                   stepResult,
                                   stepFailure,
                                   captureCurrentReplaySolverHash,
                                   requestInteractiveScene ) )
    {
        return false;
    }
    if ( stepResult.unsupportedEvents != 0 )
    {
        WriteReplayRestoreStepFailure( stepFailure, "encountered unsupported branch events before target", &target );
        return false;
    }
    return true;
}

template <typename ApplyReplaySolverSampleState>
bool ApplyReplayRestoreCheckpointSample( const ReplaySolverFrameSample& checkpoint,
                                         char* outReason,
                                         std::size_t reasonSize,
                                         ApplyReplaySolverSampleState applyReplaySolverSampleState )
{
    char applyReason[192] = {};
    if ( applyReplaySolverSampleState( checkpoint, applyReason, sizeof( applyReason ) ) )
    {
        WriteReplayProbeReason( outReason, reasonSize, "" );
        return true;
    }
    sprintf_s( outReason,
               reasonSize,
               "failed to apply checkpoint: %s",
               applyReason[0] != '\0' ? applyReason : "unknown restore failure" );
    return false;
}
} // namespace

bool ReplayProbeRunner::Configure( const ReplayStartupRequest& request )
{
    m_startup = ReplayStartupWorkflowState{};
    auto copyPath = []( char* destination, std::size_t destinationSize, const char* source )
    {
        if ( source && source[0] != '\0' )
        {
            strncpy_s( destination, destinationSize, source, _TRUNCATE );
        }
    };
    copyPath( m_startup.loadPath, sizeof( m_startup.loadPath ), request.loadPath );
    m_startup.loadProbe = request.loadProbe;
#ifdef _DEBUG
    ConfigureDebug( request );
#endif
    return !request.loadProbe;
}


void ReplayRuntime::ConfigureStartupWorkflows( const ReplayStartupRequest& request )
{
    // Invariant: load-probe capability is decided by the probe owner and
    // enforced by the prediction owner before any startup workflow executes.
    m_predictionOwner.SetGenerationPermitted( m_probeRunner.Configure( request ) );
}


ReplayStartupResult ReplayRuntime::RunStartupWorkflows( const ReplayStartupLoadInput& loadInput
#ifdef _DEBUG
                                                        ,
                                                        const ReplayRestoreTransaction& probeTransaction,
                                                        const ReplayArtifactTopologyOwners& probeTopology,
                                                        RunMousePickupState& probeMousePickup,
                                                        RunCameraMode probeNormalizedCurrentMode,
                                                        double probeNow
#endif
)
{
    ReplayStartupResult result;
    const ReplayStartupWorkflowState& startup = m_probeRunner.Startup();
#ifdef _DEBUG
    result.status = m_probeRunner.CurrentFailure();
    if ( !result.status.ok )
    {
        return result;
    }
#endif
    if ( startup.loadPath[0] != '\0' && !m_timeline.LoadPresentationArtifact( startup.loadPath ) )
    {
        result.status = SkullbonezCore::Core::SbResult::Failure( "Runtime/ReplayLoad",
                                                                 "failed to load replay v2 presentation artifact" );
        return result;
    }
    if ( startup.loadPath[0] != '\0' )
    {
        ActivateLoadedPresentationScrubber( loadInput.now,
                                            loadInput.timelineOwners.inputRouter,
                                            loadInput.timelineOwners.interaction,
                                            loadInput.cameras,
                                            loadInput.timelineOwners.terrain,
                                            loadInput.timelineOwners.camera,
                                            loadInput.mousePickup,
                                            loadInput.normalizedCurrentMode,
                                            loadInput.timelineOwners.normalizedRestoreMode,
                                            loadInput.timelineOwners.attachedFollow,
                                            loadInput.timelineOwners.directorGrabbed );
    }

#ifdef _DEBUG
    result = RunStartupProbeWorkflows( startup,
                                       result,
                                       probeTransaction,
                                       probeTopology,
                                       probeMousePickup,
                                       probeNormalizedCurrentMode,
                                       probeNow );
#else
    if ( startup.loadProbe )
    {
        result.status =
            SkullbonezCore::Core::SbResult::Failure( "Runtime/ReplayLoad", "replay load probe requires a Debug build" );
    }
#endif
    return result;
}


bool ReplayRuntime::RestoreV2ArtifactTargetState( const ReplayRestoreTransaction& transaction,
                                                  const ReplayArtifactTopologyOwners& topologyOwners,
                                                  const char* path,
                                                  ReplayFrameIndex requestedFrame,
                                                  bool makeLiveBranch,
                                                  RunReplayV2TargetRestoreResult& outResult,
                                                  char* outReason,
                                                  std::size_t reasonSize )
{
    return RestoreV2ArtifactTargetStateImpl( transaction,
                                             topologyOwners,
                                             path,
                                             requestedFrame,
                                             makeLiveBranch,
                                             false,
                                             outResult,
                                             outReason,
                                             reasonSize );
}


bool ReplayRuntime::RestoreV2ArtifactTargetStateImpl( const ReplayRestoreTransaction& transaction,
                                                      const ReplayArtifactTopologyOwners& topologyOwners,
                                                      const char* path,
                                                      ReplayFrameIndex requestedFrame,
                                                      bool makeLiveBranch,
                                                      bool injectTargetHashMismatchForProbe,
                                                      RunReplayV2TargetRestoreResult& outResult,
                                                      char* outReason,
                                                      std::size_t reasonSize )
{
    outResult = RunReplayV2TargetRestoreResult();
    auto writeReason = [outReason, reasonSize]( const char* reason )
    { WriteReplayProbeReason( outReason, reasonSize, reason ); };
    constexpr ReplayFrameIndex LATEST_NON_CHECKPOINT_TARGET = ( std::numeric_limits<ReplayFrameIndex>::max )();
    const char* restoreSource = makeLiveBranch ? "v2_file_branch" : "v2_file_target";
    const ReplayV2SolverHashSample* target = nullptr;
    const ReplaySolverFrameSample* checkpoint = nullptr;

    auto failWithDiagnostic = [&]( const char* message,
                                   const ReplayV2SolverHashSample* diagnosticTarget,
                                   const ReplaySolverFrameSample* diagnosticCheckpoint,
                                   uint64_t restoredSolverHash = 0,
                                   uint64_t restoredPresentationHash = 0,
                                   std::size_t restoredBodyCount = 0,
                                   bool hashCaptured = false,
                                   bool hashMatched = false,
                                   bool fallbackAttempted = false,
                                   bool fallbackRestored = false ) -> bool
    {
        LogReplayV2TargetRestoreDiagnostic( transaction.diagnostics,
                                            transaction.sampleOwners.scene,
                                            restoreSource,
                                            requestedFrame,
                                            LATEST_NON_CHECKPOINT_TARGET,
                                            message,
                                            diagnosticTarget,
                                            diagnosticCheckpoint,
                                            restoredSolverHash,
                                            restoredPresentationHash,
                                            restoredBodyCount,
                                            hashCaptured,
                                            hashMatched,
                                            fallbackAttempted,
                                            fallbackRestored );
        writeReason( message );
        return false;
    };

    ReplayRestoreArtifactData artifact;
    char restoreSetupReason[192] = {};
    if ( !PrepareReplayRestoreArtifactSelection( path,
                                                 requestedFrame,
                                                 LATEST_NON_CHECKPOINT_TARGET,
                                                 artifact,
                                                 target,
                                                 checkpoint,
                                                 restoreSetupReason,
                                                 sizeof( restoreSetupReason ) ) )
    {
        return failWithDiagnostic( restoreSetupReason, target, checkpoint );
    }

#ifdef _DEBUG
    ReplayV2SolverHashSample injectedTarget;
    if ( injectTargetHashMismatchForProbe )
    {
        // Why: the Debug failure probe owns this private seam so the named v2
        // gate can force a post-mutation verification failure and prove rollback.
        // Delete it when target verification accepts an independently testable
        // value program that can supply a mismatched expected hash directly.
        injectedTarget = *target;
        injectedTarget.solverHash ^= 1ull;
        target = &injectedTarget;
    }
#else
    (void)injectTargetHashMismatchForProbe;
#endif

    ReplaySolverFrameSample liveBackup;
    if ( !ReplayRestoreService::CaptureCurrentSolverSample( transaction.sampleOwners, *checkpoint, liveBackup ) )
    {
        return failWithDiagnostic( "failed to capture live state before restore", target, checkpoint );
    }
    const bool hasLiveBackup = true;
    bool stateMutated = false;

    auto failAfterMutation = [&]( const char* message,
                                  const ReplayV2SolverHashSample* diagnosticTarget,
                                  uint64_t restoredSolverHash = 0,
                                  uint64_t restoredPresentationHash = 0,
                                  std::size_t restoredBodyCount = 0,
                                  bool hashCaptured = false,
                                  bool hashMatched = false ) -> bool
    {
        bool fallbackRestored = false;
        if ( stateMutated )
        {
            if ( !hasLiveBackup )
            {
                SB_FATAL( "Runtime/ReplayRestore",
                          "V2 restore mutated live state without retaining a rollback sample" );
            }
            char fallbackReason[128] = {};
            fallbackRestored = ReplayRestoreService::ApplySolverSampleState( transaction.sampleOwners,
                                                                             liveBackup,
                                                                             fallbackReason,
                                                                             sizeof( fallbackReason ) );
            // Hazard: recoverable artifact errors must not return control with
            // a partially rebuilt scene. Failure to reapply the retained live
            // sample is a Lane F replay invariant, not a usable runtime state.
            if ( !fallbackRestored )
            {
                SB_FATAL( "Runtime/ReplayRestore",
                          "V2 restore rollback failed after live state mutation: %s",
                          fallbackReason[0] != '\0' ? fallbackReason : "unknown rollback failure" );
            }

            uint64_t rollbackSolverHash = 0;
            uint64_t rollbackPresentationHash = 0;
            std::size_t rollbackBodyCount = 0;
            if ( !ReplayRestoreService::CaptureCurrentSolverHash( transaction.sampleOwners,
                                                                  liveBackup,
                                                                  rollbackSolverHash,
                                                                  rollbackPresentationHash,
                                                                  rollbackBodyCount ) ||
                 rollbackSolverHash != liveBackup.solverHash )
            {
                SB_FATAL( "Runtime/ReplayRestore",
                          "V2 restore rollback hash mismatch: restored=0x%016llX expected=0x%016llX",
                          static_cast<unsigned long long>( rollbackSolverHash ),
                          static_cast<unsigned long long>( liveBackup.solverHash ) );
            }
        }
        return failWithDiagnostic( message,
                                   diagnosticTarget,
                                   checkpoint,
                                   restoredSolverHash,
                                   restoredPresentationHash,
                                   restoredBodyCount,
                                   hashCaptured,
                                   hashMatched,
                                   stateMutated && hasLiveBackup,
                                   fallbackRestored );
    };

    bool generatedTopologyRebuilt = false;
    char topologyReason[320] = {};
    ReplayRestoreOwnerContext restoreOwnerContext{ transaction.sampleOwners.runtimeTools,
                                                   topologyOwners.simulation,
                                                   transaction.sampleOwners.sceneController,
                                                   transaction.sampleOwners.scene,
                                                   topologyOwners.config,
                                                   topologyOwners.assets,
                                                   topologyOwners.workerPool,
                                                   transaction.sampleOwners.sceneController.World(),
                                                   transaction.sampleOwners.sceneController,
                                                   topologyOwners.generatedObjectTypeOverride,
                                                   topologyOwners.gameModelCapacity };
    if ( !EnsureReplayRestoreCheckpointTopology( restoreOwnerContext,
                                                 artifact,
                                                 *checkpoint,
                                                 generatedTopologyRebuilt,
                                                 stateMutated,
                                                 topologyReason,
                                                 sizeof( topologyReason ) ) )
    {
        return failAfterMutation( topologyReason, target );
    }

    char checkpointReason[288] = {};
    if ( !ApplyReplayRestoreCheckpointSample(
             *checkpoint,
             checkpointReason,
             sizeof( checkpointReason ),
             [&transaction]( const ReplaySolverFrameSample& sample, char* reason, std::size_t reasonSize )
             {
                 return ReplayRestoreService::ApplySolverSampleState( transaction.sampleOwners,
                                                                      sample,
                                                                      reason,
                                                                      reasonSize );
             } ) )
    {
        return failWithDiagnostic( checkpointReason, target, checkpoint );
    }
    stateMutated = true;

    auto captureCurrentReplaySolverHash = [&transaction]( const ReplaySolverFrameSample& reference,
                                                          uint64_t& solverHash,
                                                          uint64_t& presentationHash,
                                                          std::size_t& bodyCount )
    {
        return ReplayRestoreService::CaptureCurrentSolverHash( transaction.sampleOwners,
                                                               reference,
                                                               solverHash,
                                                               presentationHash,
                                                               bodyCount );
    };
    auto requestInteractiveSceneRun = [&outResult]() { outResult.enterInteractiveRequested = true; };

    ReplayRestoreStepResult stepResult;
    ReplayRestoreStepFailure stepFailure;
    if ( !RunReplayRestoreTargetStep( restoreOwnerContext,
                                      m_profiler,
                                      artifact,
                                      *checkpoint,
                                      *target,
                                      stepResult,
                                      stepFailure,
                                      captureCurrentReplaySolverHash,
                                      requestInteractiveSceneRun ) )
    {
        return failAfterMutation( stepFailure.message,
                                  stepFailure.diagnosticTarget,
                                  stepFailure.restoredSolverHash,
                                  stepFailure.restoredPresentationHash,
                                  stepFailure.restoredBodyCount,
                                  stepFailure.hashCaptured );
    }

    ReplayRestoreTargetHashResult targetHash;
    ReplayRestoreTargetHashFailure targetHashFailure;
    if ( !CaptureAndValidateReplayRestoreTargetHash( *target,
                                                     *checkpoint,
                                                     stepResult.eventCursor,
                                                     targetHash,
                                                     targetHashFailure,
                                                     captureCurrentReplaySolverHash ) )
    {
        return failAfterMutation( targetHashFailure.message,
                                  target,
                                  targetHashFailure.restored.solverHash,
                                  targetHashFailure.restored.presentationHash,
                                  targetHashFailure.restored.bodyCount,
                                  targetHashFailure.hashCaptured );
    }

    PopulateReplayRestoreTargetResult( outResult,
                                       artifact,
                                       *checkpoint,
                                       *target,
                                       stepResult,
                                       targetHash,
                                       generatedTopologyRebuilt );
    LogReplayRestoreTargetSuccess( transaction.diagnostics,
                                   transaction.sampleOwners.scene,
                                   restoreSource,
                                   requestedFrame,
                                   LATEST_NON_CHECKPOINT_TARGET,
                                   *target,
                                   *checkpoint,
                                   targetHash );

    if ( makeLiveBranch )
    {
        // Why: install branch ancestry before resetting the replay-owned
        // timeline so preserveBranchMetadata retains the new live lineage.
        const uint32_t parentBranchId =
            m_authoring.BeginRestoredBranch( checkpoint->branch, target->frameIndex, target->solverHash );
        ReplaySceneTimelineResetInput reset = transaction.timelineReset;
        reset.preserveBranchMetadata = true;
        ResetSceneTimeline( reset, transaction.timelineOwners );
        SubmitEvent( ReplayEventCommandOperations::BuildCommand( ReplayEventKind::BranchRestore,
                                                                 0,
                                                                 false,
                                                                 0,
                                                                 static_cast<int32_t>( parentBranchId ),
                                                                 target->sceneFrame,
                                                                 0,
                                                                 0,
                                                                 target->solverHash,
                                                                 "hash-verified v2 file restore" ) );
        outResult.branchId = m_authoring.Branch().branchId;
        outResult.parentBranchId = parentBranchId;
        outResult.madeLiveBranch = true;
    }

    writeReason( "restored hash match" );
    return true;
}
