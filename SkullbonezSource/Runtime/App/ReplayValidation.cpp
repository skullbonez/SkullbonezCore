/*
File: SkullbonezSource/Runtime/App/ReplayValidation.cpp
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
  Contact presentation output: Bounded solver event rows applied to the render
    store after each restored physics step.

Invariants:
  - Restore failures report Lane R results or bounded reason strings; they do
    not throw. A rollback failure is a fatal replay invariant.
  - Replay restore uses PhysicsBodyStore and ColliderStore rows as authority.
  - Target restore must keep solver hashes byte-exact against saved v2 hashes.
  - Target stepping consumes post-step presentation outputs in live-frame order.

Related:
  - SkullbonezSource/Runtime/App/RunFrame.cpp
  - SkullbonezSource/Runtime/App/ReplayValidation.Probes.cpp
  - SkullbonezSource/Runtime/App/ReplayValidation.Internal.h
  - SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "../Replay/ReplayPresentation.h"
#include "../Replay/ReplayOverlayLayout.h"
#include "../Replay/ReplayScrubber.h"
#include "../Replay/ReplayTimeline.h"
#include "ReplayRuntime.h"
#include "../Input/InputRouter.h"
#include "../Diagnostics/RuntimeDiagnostics.h"
#include "../Diagnostics/DiagnosticsRuntime.h"
#include "../Scene/SceneController.h"
#include "../../Assets/AssetSystem.h"
#include "../../Core/WorkerPool.h"
#include "../Interaction/OperatorCommandApplier.h"
#include "../Editor/EditorTools.h"
#include "../Replay/ReplayRestoreService.h"
#include "../Replay/ReplayRestoreTransactions.h"
#include "ReplayValidation.Internal.h"
#include "../Replay/ReplayV2Artifact.h"

#include "../../Core/FatalError.h"
#include "../../Core/Profiler.h"
#include "../Simulation/SimulationSystem.h"
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
const PhysicsBodyRecord* TryGetReplayProbeBodyRecord( const SceneWorld& world, int modelIndex )
{
    const PhysicsBodyStore& bodyStore = world.BodyStore();
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
const ColliderRecord* TryGetEditorTransformColliderRecord( const SceneWorld& world, PhysicsColliderHandle colliderHandle,
                                                           int modelIndex, PhysicsSceneObjectId sceneObjectId )
{
    const ColliderStore& colliderStore = world.Colliders();
    const PhysicsBodyStore& bodyStore = world.BodyStore();
    const PhysicsBodyHandle bodyHandle = sceneObjectId.IsValid()
                                             ? bodyStore.HandleForSceneObjectId( sceneObjectId, modelIndex )
                                             : bodyStore.HandleForModelIndex( modelIndex );

    const PhysicsColliderHandle resolvedHandle = colliderHandle.IsValid() ? colliderHandle
                                                                          : colliderStore.HandleForBodyHandle( bodyHandle );

    const ColliderRecord* collider = colliderStore.RecordForHandle( resolvedHandle );

    if ( !collider || colliderStore.ModelIndexForHandle( resolvedHandle ) != modelIndex )
    {
        return nullptr;
    }

    if ( sceneObjectId.IsValid() && collider->sceneObjectId != sceneObjectId )
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

bool DecodeReplayRay9Payload( const ReplayEventSample& event, Vector3& outOrigin, Vector3& outDirection,
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

bool DecodeReplayPlacePayload( const ReplayEventSample& event, Vector3& outTerrainPoint, Vector3& outPlacementScale,
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


bool DecodeReplayTransformPayload( const ReplayEventSample& event, Vector3& outPosition, Quaternion& outOrientation,
                                   float& outScaleFactor, bool& outHasScaleFactor )
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

bool TryApplyReplayRestoreWorldLauncherEvent( RuntimeTools& runtimeTools, SceneSessionState& scene, SceneWorld& world,
                                              int sceneObjectCapacity, const ReplayEventSample& event, char* eventOutReason,
                                              std::size_t eventReasonSize, bool& handled )
{
    handled = true;

    switch ( event.kind )
    {
    case ReplayEventKind::WorldOverride:

        if ( event.flags & REPLAY_WORLD_OVERRIDE_GRAVITY_CHANGED )
        {
            world.Environment().SetGravity( ReplayEventFloatFromBits( event.value0 ) );
        }

        if ( event.flags & REPLAY_WORLD_OVERRIDE_FLUID_HEIGHT_CHANGED )
        {
            world.Environment().SetFluidSurfaceHeight( ReplayEventFloatFromBits( event.value1 ) );
        }

        if ( event.flags & REPLAY_WORLD_OVERRIDE_FLUID_DENSITY_CHANGED )
        {
            world.Environment().SetFluidDensity( ReplayEventFloatFromBits( event.value2 ) );
        }

        WriteReplayProbeReason( eventOutReason, eventReasonSize, "applied world override" );
        return true;
    case ReplayEventKind::LauncherConfig:
        runtimeTools.RayCastTest().impulseStrength = ReplayEventFloatFromBits( event.value0 );
        runtimeTools.RayCastTest().projectileSpeed = ReplayEventFloatFromBits( event.value1 );
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

        runtimeTools.RayCastTest().fireMode = ( event.flags & REPLAY_LAUNCHER_FIRE_PROJECTILE ) != 0
                                                  ? RunLauncherFireMode::Projectile
                                                  : RunLauncherFireMode::Laser;

        runtimeTools.RayCastTest().impulseStrength = ReplayEventFloatFromBits( event.value1 );
        runtimeTools.RayCastTest().projectileSpeed = ReplayEventFloatFromBits( event.value2 );

        // Why: RuntimeTools now fails closed unless Run has completed the cold
        // world-to-store topology repair at the owner boundary.
        const bool launcherStoresReady = world.RepairPhysicsBodyAndColliderTopology();

        if ( launcherStoresReady &&
             runtimeTools.FireLauncherRay( world, scene, sceneObjectCapacity, rayOrigin, rayDirection, cameraUp ) )
        {
            scene.modelCount = world.SceneEntityCount();
        }

        WriteReplayProbeReason( eventOutReason, eventReasonSize, "applied launcher fire" );
        return true;
    }
    case ReplayEventKind::GeneratedSceneConfig:

        if ( scene.modelCount != event.value0 || scene.solverBallCount != event.value1 ||
             scene.solverBoxCount != event.value2 || static_cast<int32_t>( scene.rngSeed ) != event.value3 )
        {
            WriteReplayProbeReason( eventOutReason, eventReasonSize,
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

bool ApplyReplayRestoreEditorPlaceEvent( RuntimeTools& runtimeTools, SceneSessionState& scene,
                                         SkullbonezCore::Assets::AssetSystem& assets, SceneWorld& world,
                                         int sceneObjectCapacity, const ReplayEventSample& event, char* eventOutReason,
                                         std::size_t eventReasonSize, bool& requestInteractiveScene )
{

    // Lifetime: scene/editor owners are synchronous borrows for this decoded
    // event. The event and reason buffer stay explicit because they belong
    // only to this operation.
    Vector3 terrainPoint;
    Vector3 placementScale;
    float placementYawRadians = 0.0f;

    if ( !DecodeReplayPlacePayload( event, terrainPoint, placementScale, placementYawRadians ) )
    {
        WriteReplayProbeReason( eventOutReason, eventReasonSize, "invalid editor placement payload" );
        return false;
    }

    const int modelCountBefore = world.SceneEntityCount();

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
    EditorObjectPlacementContext placementContext { runtimeTools.Editor(), world, scene, assets, sceneObjectCapacity };

    EditorObjectPlacementRequest placementRequest { event.value0, ( event.flags & REPLAY_EDITOR_PLACE_FIXED ) != 0,
                                                    terrainPoint };

    EditorObjectPlacementResult placementResult;
    bool placed = false;

    if ( CanPlaceEditorObjectAtTerrainPoint( placementContext, placementRequest ) )
    {
        requestInteractiveScene = true;
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

bool ApplyReplayRestoreEditorTransformEvent( SceneWorld& world, const ReplayEventSample& event, char* eventOutReason,
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

    if ( ( event.flags & REPLAY_EDITOR_TRANSFORM_SCALE ) != 0 && ( !hasScaleFactor || event.value3 < 0 || event.value3 > 2 ||
                                                                   !std::isfinite( scaleFactor ) || scaleFactor <= 0.0f ) )
    {
        WriteReplayProbeReason( eventOutReason, eventReasonSize, "invalid editor transform scale payload" );
        return false;
    }

    if ( event.value2 != world.SceneEntityCount() )
    {
        WriteReplayProbeReason( eventOutReason, eventReasonSize, "editor transform model count precondition mismatch" );
        return false;
    }

    if ( event.value0 < 0 || event.value0 >= world.SceneEntityCount() )
    {
        WriteReplayProbeReason( eventOutReason, eventReasonSize, "editor transform model index is out of range" );
        return false;
    }

    PhysicsEngine& physics = world.Physics();
    const PhysicsBodyStore& bodyStoreBeforeEdit = world.BodyStore();
    const PhysicsSceneObjectId eventSceneObjectId { static_cast<uint32_t>( event.value1 ) };

    const PhysicsBodyHandle eventBody = bodyStoreBeforeEdit.HandleForSceneObjectId( eventSceneObjectId, event.value0 );
    const PhysicsBodyRecord* eventBodyRecord = bodyStoreBeforeEdit.RecordForHandle( eventBody );

    if ( !eventBodyRecord || bodyStoreBeforeEdit.ModelIndexForHandle( eventBody ) != event.value0 ||
         eventBodyRecord->sceneObjectId != eventSceneObjectId )
    {
        WriteReplayProbeReason( eventOutReason, eventReasonSize, "editor transform scene object id mismatch" );
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
        const ColliderRecord* colliderBeforeScale = TryGetEditorTransformColliderRecord( world, PhysicsColliderHandle {},
                                                                                         event.value0,
                                                                                         eventBodyRecord->sceneObjectId );

        if ( !colliderBeforeScale )
        {
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "editor transform collider row missing" );
            return false;
        }

        const CollisionShapeReference& baseShape = colliderBeforeScale->shape;
        CollisionShape scaledShape;

        if ( !ScaleShapeAxisFromBase( baseShape, event.value3, scaleFactor, scaledShape ) )
        {
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "failed to replay editor transform scale" );
            return false;
        }

        // Invariant: restore reuses the previous collider material and replaces
        // only the decoded scale shape, keeping replay payload semantics
        // independent from legacy model-side recapture.
        editedColliderDesc = MakeColliderCreateDesc( std::move( scaledShape ), colliderBeforeScale->restitution,
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
    const PhysicsBodyHandle body = bodyStore.HandleForSceneObjectId( eventSceneObjectId, event.value0 );
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
bool ApplyReplayRestoreEventForTarget( RuntimeTools& runtimeTools, SceneSessionState& scene,
                                       SkullbonezCore::Assets::AssetSystem& assets, SceneWorld& world,
                                       int sceneObjectCapacity, const ReplayEventSample& event, char* eventOutReason,
                                       std::size_t eventReasonSize, bool& requestInteractiveScene )
{

    if ( event.payloadVersion != 1 )
    {
        WriteReplayProbeReason( eventOutReason, eventReasonSize, "unsupported replay event payload version" );
        return false;
    }

    bool restoreEventHandled = false;

    if ( TryApplyReplayRestoreWorldLauncherEvent( runtimeTools, scene, world, sceneObjectCapacity, event, eventOutReason,
                                                  eventReasonSize, restoreEventHandled ) )
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
        return ApplyReplayRestoreEditorPlaceEvent( runtimeTools, scene, assets, world, sceneObjectCapacity, event,
                                                   eventOutReason, eventReasonSize, requestInteractiveScene );
    case ReplayEventKind::EditorTransform:
        return ApplyReplayRestoreEditorTransformEvent( world, event, eventOutReason, eventReasonSize );
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

bool LoadReplayRestoreArtifactData( const char* path, ReplayRestoreArtifactData& artifact, char* outReason,
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

bool SelectReplayRestoreTargetAndCheckpoint( const ReplayRestoreArtifactData& artifact, ReplayFrameIndex requestedFrame,
                                             ReplayFrameIndex latestNonCheckpointTarget,
                                             const ReplayV2SolverHashSample*& outTarget,
                                             const ReplaySolverFrameSample*& outCheckpoint, char* outReason,
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
            sprintf_s( message, sizeof( message ), "found no saved hash for requested target frame %llu",
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

bool PrepareReplayRestoreArtifactSelection( const char* path, ReplayFrameIndex requestedFrame,
                                            ReplayFrameIndex latestNonCheckpointTarget, ReplayRestoreArtifactData& artifact,
                                            const ReplayV2SolverHashSample*& outTarget,
                                            const ReplaySolverFrameSample*& outCheckpoint, char* outReason,
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

    return SelectReplayRestoreTargetAndCheckpoint( artifact, requestedFrame, latestNonCheckpointTarget, outTarget,
                                                   outCheckpoint, outReason, reasonSize );
}

bool ReplayCheckpointTopologyMatchesLive( const ReplaySolverFrameSample& checkpoint, const SceneWorld& world )
{
    const int liveModelCount = world.SceneEntityCount();

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

        const PhysicsBodyRecord* bodyRecord = TryGetReplayProbeBodyRecord( world, body.modelRow.value );

        if ( !bodyRecord || bodyRecord->sceneObjectId != body.id )
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

void FormatReplayRestoreDivergenceMessage( char* message, std::size_t messageSize, ReplayFrameIndex currentFrame,
                                           uint64_t restoredSolverHash, uint64_t restoredPresentationHash,
                                           std::size_t restoredBodyCount, const ReplayV2SolverHashSample& expectedHash,
                                           const std::vector<ReplayPresentationSample>& presentationSamples,
                                           const SceneWorld& world, std::size_t eventsApplied )
{
    const ReplayPresentationSample* expectedPresentation = FindReplayPresentationForFrame( presentationSamples,
                                                                                           currentFrame );

    const PhysicsBodyRecord* restoredBody = TryGetReplayProbeBodyRecord( world, 0 );

    if ( expectedPresentation && !expectedPresentation->bodies.empty() && restoredBody )
    {
        const ReplayBodyPresentationSample& expectedBody = expectedPresentation->bodies[0];
        const auto hotFields = world.BodyStore().HotFields();
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
        sprintf_s( message, messageSize,
                   "replay restore target probe diverged at frame %llu: restored=0x%016llX "
                   "expected=0x%016llX restored_presentation=0x%016llX expected_presentation=0x%016llX "
                   "restored_pos=(%.6f,%.6f,%.6f) expected_pos=(%.6f,%.6f,%.6f) "
                   "restored_vel=(%.6f,%.6f,%.6f) restored_q=(%.6f,%.6f,%.6f,%.6f) "
                   "expected_q=(%.6f,%.6f,%.6f,%.6f) restored_body_id=%u expected_body_id=%u "
                   "events_applied=%llu",
                   static_cast<unsigned long long>( currentFrame ), static_cast<unsigned long long>( restoredSolverHash ),
                   static_cast<unsigned long long>( expectedHash.solverHash ),
                   static_cast<unsigned long long>( restoredPresentationHash ),
                   static_cast<unsigned long long>( expectedHash.presentationHash ), restoredPosition.x, restoredPosition.y,
                   restoredPosition.z, expectedBody.position.x, expectedBody.position.y, expectedBody.position.z,
                   restoredVelocity.x, restoredVelocity.y, restoredVelocity.z, restoredQx, restoredQy, restoredQz,
                   restoredQw, expectedBody.orientation[0], expectedBody.orientation[1], expectedBody.orientation[2],
                   expectedBody.orientation[3], restoredBody->sceneObjectId.value, expectedBody.id.value,
                   static_cast<unsigned long long>( eventsApplied ) );
    }
    else
    {
        sprintf_s( message, messageSize,
                   "replay restore target probe diverged at frame %llu: restored=0x%016llX "
                   "expected=0x%016llX restored_presentation=0x%016llX expected_presentation=0x%016llX "
                   "restored_bodies=%llu expected_bodies=%u events_applied=%llu",
                   static_cast<unsigned long long>( currentFrame ), static_cast<unsigned long long>( restoredSolverHash ),
                   static_cast<unsigned long long>( expectedHash.solverHash ),
                   static_cast<unsigned long long>( restoredPresentationHash ),
                   static_cast<unsigned long long>( expectedHash.presentationHash ),
                   static_cast<unsigned long long>( restoredBodyCount ), expectedHash.bodyCount,
                   static_cast<unsigned long long>( eventsApplied ) );
    }
}

// Concept: replay target restore rebuilds solver state by starting from a
// checkpoint and replaying only the saved branch events before each fixed
// physics step. The invariant owner records progress and failure values while
// every concrete runtime owner remains a synchronous method borrow.
bool StepReplayRestoreTarget( ReplayRestoreTransaction& transaction, SceneWorld& world, SceneSessionState& scene,
                              OverlayDebugState& debug, RuntimeTools& runtimeTools,
                              SkullbonezCore::Assets::AssetSystem& assets, SkullbonezCore::Threading::WorkerPool& workerPool,
                              int sceneObjectCapacity, const ReplayRestoreArtifactData& artifact,
                              const ReplaySolverFrameSample& checkpoint, const ReplayV2SolverHashSample& target,
                              SkullbonezCore::Core::Profiler* profiler )
{
    ReplayFrameIndex currentFrame = checkpoint.frameIndex;
    int currentSceneFrame = checkpoint.sceneFrame;
    uint32_t eventCursor = checkpoint.eventCursor;
    std::size_t eventsApplied = 0;
    std::size_t unsupportedEvents = 0;
    scene.currentFrame = currentSceneFrame;

    // Invariant: world.StepPhysics enters named profiler zones. The cold
    // restore loop must establish the same frame lifetime as a live frame.
    ScopedReplayProbeProfilerFrame profilerFrame( profiler );

    while ( currentFrame < target.frameIndex )
    {
        const ReplayFrameIndex nextFrame = currentFrame + 1u;

        for ( const ReplayEventSample& event : artifact.events )
        {

            if ( event.frameIndex != nextFrame || event.sequence < eventCursor )
            {
                continue;
            }

            if ( event.branch.branchId != checkpoint.branch.branchId )
            {
                ++unsupportedEvents;
                continue;
            }

            char eventReason[160] = {};
            bool requestInteractiveScene = false;
            const bool eventApplied = ApplyReplayRestoreEventForTarget( runtimeTools, scene, assets, world,
                                                                        sceneObjectCapacity, event, eventReason,
                                                                        sizeof( eventReason ), requestInteractiveScene );

            if ( requestInteractiveScene )
            {
                transaction.RequestInteractiveScene();
            }

            if ( !eventApplied )
            {
                char message[320] = {};

                sprintf_s( message, sizeof( message ),
                           "replay restore target probe failed to apply event sequence %u at frame %llu: %s", event.sequence,
                           static_cast<unsigned long long>( event.frameIndex ),
                           eventReason[0] != '\0' ? eventReason : "unknown event replay failure" );

                transaction.RecordFailure( message );
                return false;
            }

            eventCursor = (std::max)( eventCursor, event.sequence + 1u );
            ++eventsApplied;
        }

        runtimeTools.TickRayCastTestLines( PHYSICS_FIXED_DT );
        runtimeTools.Laser().Update( PHYSICS_FIXED_DT );
        world.EndCollisionVisualFrame();
        ++currentSceneFrame;
        scene.currentFrame = currentSceneFrame;
        world.BeginCollisionVisualFrame();

        const auto physicsWorldForces = world.Environment().GetPhysicsWorldForces();
        SkullbonezCore::Rendering::RenderInstanceStore& contactPresentation = world.MutableRenderInstances();
        contactPresentation.TickContactFeedback( world.SceneEntityCount(), PHYSICS_FIXED_DT );
        const ScenePhysicsPostStepOutput postStep = world.StepPhysics( PHYSICS_FIXED_DT, physicsWorldForces, workerPool );

        // Replay target stepping consumes the same bounded presentation events
        // as the live frame so presentation hashes cannot drift by call path.

        for ( int modelIndex : postStep.fixedContactModelIndices )
        {
            contactPresentation.NotifyFixedContact( modelIndex, 0.5f );
        }

        currentFrame = nextFrame;

        const ReplayV2SolverHashSample* expectedHash = FindReplaySolverHashForFrame( artifact.hashes, currentFrame );

        if ( !expectedHash )
        {
            transaction.RecordFailure( "could not find stepped hash metadata" );
            return false;
        }

        ReplaySolverFrameSample stepReference;
        stepReference.frameIndex = expectedHash->frameIndex;
        stepReference.branch = checkpoint.branch;
        stepReference.eventCursor = eventCursor;
        stepReference.sceneFrame = expectedHash->sceneFrame;
        stepReference.simulationSeconds = expectedHash->simulationSeconds;
        stepReference.physicsDt = PHYSICS_FIXED_DT;

        uint64_t stepSolverHash = 0;
        uint64_t stepPresentationHash = 0;
        std::size_t stepBodyCount = 0;

        if ( !ReplayRestoreService::CaptureCurrentSolverHash( world, scene, debug, runtimeTools, stepReference,
                                                              stepSolverHash, stepPresentationHash, stepBodyCount ) )
        {
            transaction.RecordFailure( "failed to capture stepped hash" );
            return false;
        }

        if ( stepBodyCount != expectedHash->bodyCount || stepSolverHash != expectedHash->solverHash )
        {
            char message[1024] = {};

            FormatReplayRestoreDivergenceMessage( message, sizeof( message ), currentFrame, stepSolverHash,
                                                  stepPresentationHash, stepBodyCount, *expectedHash,
                                                  artifact.presentationSamples, world, eventsApplied );

            transaction.RecordFailure( message );
            return false;
        }
    }

    if ( unsupportedEvents != 0 )
    {
        transaction.RecordFailure( "encountered unsupported branch events before target" );
        return false;
    }

    transaction.MarkTargetStepped( currentFrame, eventCursor, eventsApplied );
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

bool CaptureAndValidateReplayRestoreTargetHash( const ReplayV2SolverHashSample& target,
                                                const ReplaySolverFrameSample& checkpoint, uint32_t eventCursor,
                                                SceneWorld& world, const SceneSessionState& scene,
                                                const OverlayDebugState& debug, RuntimeTools& runtimeTools,
                                                ReplayRestoreTargetHashResult& result,
                                                ReplayRestoreTargetHashFailure& failure )
{
    ReplaySolverFrameSample reference;
    reference.frameIndex = target.frameIndex;
    reference.branch = checkpoint.branch;
    reference.eventCursor = eventCursor;
    reference.sceneFrame = target.sceneFrame;
    reference.simulationSeconds = target.simulationSeconds;
    reference.physicsDt = PHYSICS_FIXED_DT;

    if ( !ReplayRestoreService::CaptureCurrentSolverHash( world, scene, debug, runtimeTools, reference, result.solverHash,
                                                          result.presentationHash, result.bodyCount ) )
    {
        strncpy_s( failure.message, "failed to capture target hash", _TRUNCATE );
        failure.hashCaptured = false;
        return false;
    }

    failure.restored = result;
    failure.hashCaptured = true;

    if ( result.bodyCount != target.bodyCount )
    {
        sprintf_s( failure.message, sizeof( failure.message ),
                   "replay restore target probe body count mismatch: restored=%llu expected=%u",
                   static_cast<unsigned long long>( result.bodyCount ), target.bodyCount );

        return false;
    }

    if ( result.solverHash != target.solverHash )
    {
        sprintf_s( failure.message, sizeof( failure.message ),
                   "replay restore target probe solver hash mismatch: restored=0x%016llX expected=0x%016llX",
                   static_cast<unsigned long long>( result.solverHash ),
                   static_cast<unsigned long long>( target.solverHash ) );

        return false;
    }

    return true;
}

void PopulateReplayRestoreTargetResult( RunReplayV2TargetRestoreResult& outResult, const ReplayRestoreArtifactData& artifact,
                                        const ReplaySolverFrameSample& checkpoint, const ReplayV2SolverHashSample& target,
                                        const ReplayRestoreTransaction& transaction,
                                        const ReplayRestoreTargetHashResult& targetHash, bool generatedTopologyRebuilt )
{
    outResult.checkpointCount = artifact.checkpointResult.checkpointCount;
    outResult.eventCount = artifact.eventResult.eventCount;
    outResult.hashCount = artifact.hashResult.hashCount;
    outResult.eventsApplied = transaction.EventsApplied();
    outResult.bodyCount = targetHash.bodyCount;
    outResult.fileBytes = artifact.hashResult.fileBytes;
    outResult.checkpointFrame = checkpoint.frameIndex;
    outResult.targetFrame = target.frameIndex;
    outResult.eventCursor = transaction.EventCursor();
    outResult.solverHash = targetHash.solverHash;
    outResult.presentationHash = targetHash.presentationHash;
    outResult.generatedTopologyRebuilt = generatedTopologyRebuilt;
}

void RecordReplayRestoreTargetSuccess( ReplayRestoreTransaction& transaction, const char* restoreSource,
                                       const ReplayV2SolverHashSample& target, const ReplaySolverFrameSample& checkpoint,
                                       const ReplayRestoreTargetHashResult& targetHash )
{
#ifdef _DEBUG
    ReplayRestoreResultDiagnostic result;
    result.restoreSource = restoreSource;
    result.targetReplayFrame = target.frameIndex;
    result.targetSceneFrame = target.sceneFrame;
    result.checkpointReplayFrame = checkpoint.frameIndex;
    result.targetSolverHash = target.solverHash;
    result.targetPresentationHash = target.presentationHash;
    result.targetBodyCount = target.bodyCount;
    result.restoredSolverHash = targetHash.solverHash;
    result.restoredPresentationHash = targetHash.presentationHash;
    result.restoredBodyCount = targetHash.bodyCount;
    result.contactCount = checkpoint.contactCount;
    result.pipelineRecordCount = checkpoint.pipelineRecordCount;
    result.checkpointBoundary = checkpoint.checkpointBoundary;
    result.hashCaptured = true;
    result.hashMatched = true;
    result.failureReason = "";
    transaction.RecordRestoreResultDiagnostic( result );
#else
    (void)transaction;
    (void)restoreSource;
    (void)target;
    (void)checkpoint;
    (void)targetHash;
#endif
}

bool RebuildReplayGeneratedSceneTopology( RuntimeTools& runtimeTools, SimulationSystem& simulation, SceneSessionState& scene,
                                          const SkullbonezCore::Core::EngineConfig& config, SceneWorld& world,
                                          SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides,
                                          GeneratedObjectTypeOverride& generatedObjectTypeOverride, int sceneObjectCapacity,
                                          const ReplayEventSample& event, const ReplaySolverFrameSample& checkpoint,
                                          char* rebuildReason, std::size_t rebuildReasonSize )
{

    if ( event.value0 < 0 || event.value1 < 0 || event.value2 < 0 || event.value3 <= 0 )
    {
        WriteReplayProbeReason( rebuildReason, rebuildReasonSize, "generated scene config contains invalid counts" );
        return false;
    }

    const uint32_t overrideBits = ( event.flags & REPLAY_GENERATED_SCENE_OVERRIDE_MASK ) >>
                                  REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT;

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

    if ( event.value0 > sceneObjectCapacity )
    {
        WriteReplayProbeReason( rebuildReason, rebuildReasonSize, "generated scene model count exceeds active capacity" );

        return false;
    }

    world.Clear();

    // Invariant: a restore-side generated rebuild is a fresh scene population,
    // even though it does not enter the full scene-load path. Reset the
    // scene-owned id cursor after the clear so regenerated
    // PhysicsSceneObjectId/scene object ids match the checkpoint topology.
    scene.ResetSceneObjectIdCursor( world.BodyStore() );
    runtimeTools.ClearRayCastTestLines();
    simulation.Reset();
    scene.rngSeed = static_cast<unsigned int>( event.value3 );
    scene.rngState = static_cast<unsigned int>( event.value3 );
    generatedObjectTypeOverride = static_cast<GeneratedObjectTypeOverride>( overrideBits );
    uiOverrides.modelCountOverride = uiModelCount ? event.value0 : -1;
    uiOverrides.solverBallCountOverride = uiSolverCounts || exactSolverCounts ? event.value1 : -1;
    uiOverrides.solverBoxCountOverride = uiSolverCounts || exactSolverCounts ? event.value2 : -1;

    if ( exactSolverCounts || uiSolverCounts )
    {
        const SkullbonezCore::Core::SbResult
            setupResult = SceneGeneratedSetup::SetUpSolverObjects( scene, config, world, generatedObjectTypeOverride,
                                                                   event.value1, event.value2 );

        if ( !setupResult.ok )
        {
            WriteReplayProbeReason( rebuildReason, rebuildReasonSize, setupResult.error.message );
            return false;
        }
    }
    else
    {
        const SkullbonezCore::Core::SbResult
            setupResult = SceneGeneratedSetup::SetUpSceneEntities( scene, config, world, generatedObjectTypeOverride,
                                                                   event.value0 );

        if ( !setupResult.ok )
        {
            WriteReplayProbeReason( rebuildReason, rebuildReasonSize, setupResult.error.message );
            return false;
        }
    }

    if ( !ReplayCheckpointTopologyMatchesLive( checkpoint, world ) )
    {
        WriteReplayProbeReason( rebuildReason, rebuildReasonSize, "rebuilt generated topology still mismatches checkpoint" );

        return false;
    }

    WriteReplayProbeReason( rebuildReason, rebuildReasonSize, "rebuilt generated topology" );
    return true;
}
} // namespace

bool ReplayProbeRunner::Configure( const ReplayStartupRequest& request )
{
    m_startup = ReplayStartupWorkflowState {};
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
                                                        SceneController& sceneController, DiagnosticsRuntime& diagnosticsRuntime, OverlayDebugState& debug,
                                                        RuntimeTools& runtimeTools, SimulationSystem& simulation, const SkullbonezCore::Core::EngineConfig& config,
                                                        SkullbonezCore::Assets::AssetSystem& assets, SkullbonezCore::Threading::WorkerPool& workerPool,
                                                        SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides, GeneratedObjectTypeOverride& generatedObjectTypeOverride
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

        if ( BeginLoadedPresentationActivationScrubber( HasLoadedPresentation(), loadInput.inputRouter,
                                                        loadInput.interaction ) )
        {
            ExitInspectionCamera( loadInput.cameras, loadInput.terrain, loadInput.camera, loadInput.normalizedRestoreMode,
                                  loadInput.attachedFollow, loadInput.directorGrabbed, loadInput.interaction,
                                  loadInput.inputRouter );

            ArmLoadedPresentationScrubber( 0.25f, loadInput.now, loadInput.interaction );
            EnterInspectionCamera( loadInput.cameras, loadInput.camera, loadInput.normalizedCurrentMode,
                                   loadInput.interaction, loadInput.inputRouter, loadInput.mousePickup );
        }
    }

#ifdef _DEBUG
    result = RunStartupProbeWorkflows( startup, loadInput, sceneController, diagnosticsRuntime, debug, runtimeTools,
                                       simulation, config, assets, workerPool, uiOverrides, generatedObjectTypeOverride );
#else

    if ( startup.loadProbe )
    {
        result.status = SkullbonezCore::Core::SbResult::Failure( "Runtime/ReplayLoad",
                                                                 "replay load probe requires a Debug build" );
    }
#endif
    return result;
}


bool ReplayRuntime::RestoreV2ArtifactTargetState( ReplayRestoreTransaction& transaction, const ReplayLiveRestoreRequest& request, SceneWorld& world,
                                                  SceneSessionState& scene, OverlayDebugState& debug, RuntimeTools& runtimeTools, SimulationSystem& simulation,
                                                  const SkullbonezCore::Core::EngineConfig& config, SkullbonezCore::Assets::AssetSystem& assets,
                                                  SkullbonezCore::Threading::WorkerPool& workerPool, SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides,
                                                  GeneratedObjectTypeOverride& generatedObjectTypeOverride )
{
    return RestoreV2ArtifactTargetStateImpl( transaction, request, world, scene, debug, runtimeTools, simulation, config,
                                             assets, workerPool, uiOverrides, generatedObjectTypeOverride );
}


bool ReplayRuntime::RestoreV2ArtifactTargetStateImpl( ReplayRestoreTransaction& transaction, const ReplayLiveRestoreRequest& request, SceneWorld& world,
                                                      SceneSessionState& scene, OverlayDebugState& debug, RuntimeTools& runtimeTools, SimulationSystem& simulation,
                                                      const SkullbonezCore::Core::EngineConfig& config, SkullbonezCore::Assets::AssetSystem& assets,
                                                      SkullbonezCore::Threading::WorkerPool& workerPool, SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides,
                                                      GeneratedObjectTypeOverride& generatedObjectTypeOverride )
{
    RunReplayV2TargetRestoreResult& outResult = transaction.Result();
    outResult = RunReplayV2TargetRestoreResult();

    constexpr ReplayFrameIndex LATEST_NON_CHECKPOINT_TARGET = ( std::numeric_limits<ReplayFrameIndex>::max )();
    const char* restoreSource = request.makeLiveBranch ? "v2_file_branch" : "v2_file_target";
    const ReplayV2SolverHashSample* target = nullptr;
    const ReplaySolverFrameSample* checkpoint = nullptr;
    auto recordFailureDiagnostic = [&]( const char* message, const ReplayV2SolverHashSample* diagnosticTarget,
                                        const ReplaySolverFrameSample* diagnosticCheckpoint, uint64_t restoredSolverHash = 0,
                                        uint64_t restoredPresentationHash = 0, std::size_t restoredBodyCount = 0,
                                        bool hashCaptured = false, bool hashMatched = false, bool fallbackAttempted = false,
                                        bool fallbackRestored = false )
    {
#ifdef _DEBUG
        ReplayRestoreResultDiagnostic diagnostic;
        diagnostic.restoreSource = restoreSource;
        diagnostic.targetReplayFrame = diagnosticTarget ? diagnosticTarget->frameIndex
                                                        : ( request.requestedFrame == LATEST_NON_CHECKPOINT_TARGET
                                                                ? 0
                                                                : request.requestedFrame );

        diagnostic.targetSceneFrame = diagnosticTarget ? diagnosticTarget->sceneFrame : scene.currentFrame;
        diagnostic.checkpointReplayFrame = diagnosticCheckpoint ? diagnosticCheckpoint->frameIndex : 0;
        diagnostic.targetSolverHash = diagnosticTarget ? diagnosticTarget->solverHash : 0;
        diagnostic.targetPresentationHash = diagnosticTarget ? diagnosticTarget->presentationHash : 0;
        diagnostic.targetBodyCount = diagnosticTarget ? diagnosticTarget->bodyCount : 0;
        diagnostic.restoredSolverHash = restoredSolverHash;
        diagnostic.restoredPresentationHash = restoredPresentationHash;
        diagnostic.restoredBodyCount = restoredBodyCount;
        diagnostic.contactCount = diagnosticCheckpoint ? diagnosticCheckpoint->contactCount : 0;
        diagnostic.pipelineRecordCount = diagnosticCheckpoint ? diagnosticCheckpoint->pipelineRecordCount : 0;
        diagnostic.checkpointBoundary = diagnosticCheckpoint ? diagnosticCheckpoint->checkpointBoundary : false;
        diagnostic.hashCaptured = hashCaptured;
        diagnostic.hashMatched = hashMatched;
        diagnostic.fallbackAttempted = fallbackAttempted;
        diagnostic.fallbackRestored = fallbackRestored;
        diagnostic.failureReason = message;
        transaction.RecordRestoreResultDiagnostic( diagnostic );
#else
        (void)message;
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
    };

    ReplayRestoreArtifactData artifact;
    char restoreSetupReason[192] = {};

    if ( !PrepareReplayRestoreArtifactSelection( request.path, request.requestedFrame, LATEST_NON_CHECKPOINT_TARGET,
                                                 artifact, target, checkpoint, restoreSetupReason,
                                                 sizeof( restoreSetupReason ) ) )
    {
        recordFailureDiagnostic( restoreSetupReason, target, checkpoint );
        transaction.FailBeforeMutation( restoreSetupReason );
        return false;
    }

    transaction.SelectArtifact( static_cast<std::size_t>( checkpoint - artifact.checkpoints.data() ),
                                static_cast<std::size_t>( target - artifact.hashes.data() ) );

#ifdef _DEBUG
    ReplayV2SolverHashSample injectedTarget;

    if ( request.injectTargetHashMismatchForProbe )
    {

        // Why: the Debug failure probe owns this private seam so the named v2
        // gate can force a post-mutation verification failure and prove rollback.
        // Delete it when target verification accepts an independently testable
        // value program that can supply a mismatched expected hash directly.
        injectedTarget = *target;
        injectedTarget.solverHash ^= 1ull;
        target = &injectedTarget;
    }
#endif

    ReplaySolverFrameSample liveBackup;

    if ( !ReplayRestoreService::CaptureCurrentSolverSample( world, scene, debug, runtimeTools, *checkpoint, liveBackup ) )
    {
        recordFailureDiagnostic( "failed to capture live state before restore", target, checkpoint );
        transaction.FailBeforeMutation( "failed to capture live state before restore" );
        return false;
    }

    transaction.CaptureLiveBackup( std::move( liveBackup ) );

    auto failAfterMutation = [&]( const char* message, const ReplayV2SolverHashSample* diagnosticTarget,
                                  uint64_t restoredSolverHash = 0, uint64_t restoredPresentationHash = 0,
                                  std::size_t restoredBodyCount = 0, bool hashCaptured = false,
                                  bool hashMatched = false ) -> bool
    {
        bool fallbackRestored = false;

        if ( transaction.StateMutated() )
        {

            if ( !transaction.HasLiveBackup() )
            {
                SB_FATAL( "Runtime/ReplayRestore", "V2 restore mutated live state without retaining a rollback sample" );
            }

            char fallbackReason[128] = {};

            fallbackRestored = ReplayRestoreService::ApplySolverSampleState( world, scene, debug, runtimeTools,
                                                                             transaction.LiveBackup(), fallbackReason,
                                                                             sizeof( fallbackReason ) );

            // Hazard: recoverable artifact errors must not return control with
            // a partially rebuilt scene. Failure to reapply the retained live
            // sample is a Lane F replay invariant, not a usable runtime state.

            if ( !fallbackRestored )
            {
                SB_FATAL( "Runtime/ReplayRestore", "V2 restore rollback failed after live state mutation: %s",
                          fallbackReason[0] != '\0' ? fallbackReason : "unknown rollback failure" );
            }

            uint64_t rollbackSolverHash = 0;
            uint64_t rollbackPresentationHash = 0;
            std::size_t rollbackBodyCount = 0;

            if ( !ReplayRestoreService::CaptureCurrentSolverHash( world, scene, debug, runtimeTools,
                                                                  transaction.LiveBackup(), rollbackSolverHash,
                                                                  rollbackPresentationHash, rollbackBodyCount ) ||
                 rollbackSolverHash != transaction.LiveBackup().solverHash )
            {
                SB_FATAL( "Runtime/ReplayRestore",
                          "V2 restore rollback hash mismatch: restored=0x%016llX expected=0x%016llX",
                          static_cast<unsigned long long>( rollbackSolverHash ),
                          static_cast<unsigned long long>( transaction.LiveBackup().solverHash ) );
            }
        }

        recordFailureDiagnostic( message, diagnosticTarget, checkpoint, restoredSolverHash, restoredPresentationHash,
                                 restoredBodyCount, hashCaptured, hashMatched,
                                 transaction.StateMutated() && transaction.HasLiveBackup(), fallbackRestored );

        transaction.MarkRolledBack( message );
        return false;
    };

    bool generatedTopologyRebuilt = false;
    char topologyReason[320] = {};

    if ( ReplayCheckpointTopologyMatchesLive( *checkpoint, world ) )
    {
        transaction.MarkTopologyPrepared( false, false );
    }
    else
    {
        const ReplayEventSample* generatedConfig = FindReplayGeneratedSceneConfigBeforeCheckpoint( artifact.events,
                                                                                                   *checkpoint );

        if ( !generatedConfig )
        {
            recordFailureDiagnostic( "checkpoint topology does not match live scene and no generated config was saved",
                                     target, checkpoint );

            transaction.FailBeforeMutation( "checkpoint topology does not match live scene and no generated config was saved" );
            return false;
        }

        transaction.MarkTopologyPrepared( false, true );
        char rebuildReason[160] = {};

        if ( !RebuildReplayGeneratedSceneTopology( runtimeTools, simulation, scene, config, world, uiOverrides,
                                                   generatedObjectTypeOverride,
                                                   SkullbonezCore::Core::ActiveSceneObjectCapacity( config ),
                                                   *generatedConfig, *checkpoint, rebuildReason, sizeof( rebuildReason ) ) )
        {
            sprintf_s( topologyReason, sizeof( topologyReason ), "failed to rebuild generated scene topology: %s",
                       rebuildReason[0] != '\0' ? rebuildReason : "unknown rebuild failure" );

            return failAfterMutation( topologyReason, target );
        }

        generatedTopologyRebuilt = true;
    }

    char checkpointReason[288] = {};

    if ( !ReplayRestoreService::ApplySolverSampleState( world, scene, debug, runtimeTools, *checkpoint, checkpointReason,
                                                        sizeof( checkpointReason ) ) )
    {

        if ( transaction.StateMutated() )
        {
            return failAfterMutation( checkpointReason, target );
        }

        recordFailureDiagnostic( checkpointReason, target, checkpoint );
        transaction.FailBeforeMutation( checkpointReason );
        return false;
    }

    transaction.MarkCheckpointApplied();

    if ( !StepReplayRestoreTarget( transaction, world, scene, debug, runtimeTools, assets, workerPool,
                                   SkullbonezCore::Core::ActiveSceneObjectCapacity( config ), artifact, *checkpoint, *target,
                                   m_profiler ) )
    {
        return failAfterMutation( transaction.FailureReason(), target );
    }

    ReplayRestoreTargetHashResult targetHash;
    ReplayRestoreTargetHashFailure targetHashFailure;

    if ( !CaptureAndValidateReplayRestoreTargetHash( *target, *checkpoint, transaction.EventCursor(), world, scene, debug,
                                                     runtimeTools, targetHash, targetHashFailure ) )
    {
        return failAfterMutation( targetHashFailure.message, target, targetHashFailure.restored.solverHash,
                                  targetHashFailure.restored.presentationHash, targetHashFailure.restored.bodyCount,
                                  targetHashFailure.hashCaptured );
    }

    PopulateReplayRestoreTargetResult( outResult, artifact, *checkpoint, *target, transaction, targetHash,
                                       generatedTopologyRebuilt );

    outResult.enterInteractiveRequested = transaction.EnterInteractiveRequested();
    transaction.MarkTargetVerified();
    RecordReplayRestoreTargetSuccess( transaction, restoreSource, *target, *checkpoint, targetHash );

    if ( request.makeLiveBranch )
    {

        // Why: install ancestry before the caller applies the detached timeline
        // reset output, so preserveBranchMetadata retains the new live lineage.
        const uint32_t parentBranchId = m_authoring.BeginRestoredBranch( checkpoint->branch, target->frameIndex,
                                                                         target->solverHash );

        transaction.PrepareTimelineReset( parentBranchId, target->sceneFrame, target->solverHash );
        outResult.branchId = m_authoring.Branch().branchId;
        outResult.parentBranchId = parentBranchId;
        outResult.madeLiveBranch = true;
    }
    else
    {
        transaction.Complete();
    }

    return true;
}
