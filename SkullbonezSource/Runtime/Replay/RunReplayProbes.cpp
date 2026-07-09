/*
File: SkullbonezSource/Runtime/Replay/RunReplayProbes.cpp
Purpose:
  Owns replay validation probes and v2 target restore work triggered by Run.

Mental model:
  Run still owns process-lifetime scene, input, physics, and replay state. This
  file keeps replay-specific validation and restore transactions in the Replay
  folder while borrowing the same owners through existing Run methods.

Glossary:
  Replay probe: CLI/debug validation path that proves scrub, save/load, or
    solver restore behavior without throwing exceptions.
  V2 target restore: Hash-gated replay restore that starts from a saved solver
    checkpoint and replays saved timeline events to a target frame.
  Event cursor: Monotonic sequence marker stored on checkpoints so restore can
    resume timeline events without replaying old side effects.

Invariants:
  - Restore failures report Lane R results or bounded reason strings; they do
    not throw.
  - Replay restore uses PhysicsBodyStore and ColliderStore rows as authority.
  - Target restore must keep solver hashes byte-exact against saved v2 hashes.

Related:
  - SkullbonezSource/Runtime/RunFrame.cpp
  - SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "../RunInternal.h"
#include "../RuntimeTuning.h"
#include "../Editor/EditorTools.h"
#include "ReplayV2Artifact.h"

#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsTimestep.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;
using SkullbonezCore::Math::Vector::Vector3;

namespace
{
#ifdef _DEBUG
constexpr const char* REPLAY_PROBE_OWNER = "ReplayProbe";

SbResult ReplayProbeFailure( const char* message )
{
    return SbResult::Failure( REPLAY_PROBE_OWNER, "%s", message );
}
#endif

Vector3 RenderProbeMatrixTranslation( const Matrix4& matrix )
{
    return Vector3( matrix.m[12], matrix.m[13], matrix.m[14] );
}

bool TryPrepareReplayProbeRenderPosition( SkullbonezCore::GameObjects::GameModelCollection& collection,
                                          int modelIndex,
                                          Vector3& outPosition )
{
    const auto& instances = collection.RenderInstances().Records();
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( instances.size() ) )
    {
        return false;
    }

    outPosition = RenderProbeMatrixTranslation( instances[static_cast<std::size_t>( modelIndex )].modelMatrix );
    return true;
}

bool ApplyReplayProbePresentationSampleForRender( SkullbonezCore::GameObjects::GameModelCollection& collection,
                                                  ReplayRuntime& replayRuntime,
                                                  const ReplayPresentationSample& sample )
{
    // Why: probes consume replay scrub poses exactly where the renderer consumes
    // them: after the live render snapshot refresh and before draw submission.
    // This proves presentation overrides do not mutate live body rows.
    collection.PrepareRenderInstances();
    return replayRuntime.ApplyPresentationSampleForRender( collection, sample );
}

void RestoreReplayProbeRenderInstances( SkullbonezCore::GameObjects::GameModelCollection& collection )
{
    collection.PrepareRenderInstances();
}

const PhysicsBodyRecord*
TryGetReplayProbeBodyRecord( const SkullbonezCore::GameObjects::GameModelCollection& collection, int modelIndex )
{
    const PhysicsBodyStore& bodyStore = collection.GetPhysicsEngine().BodyStore();
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
const ColliderRecord*
TryGetEditorTransformColliderRecord( const SkullbonezCore::GameObjects::GameModelCollection& collection,
                                     PhysicsColliderHandle colliderHandle,
                                     int modelIndex,
                                     uint32_t replayBodyId )
{
    const ColliderStore& colliderStore = collection.Colliders();
    const PhysicsBodyStore& bodyStore = collection.GetPhysicsEngine().BodyStore();
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

constexpr uint32_t REPLAY_WORLD_OVERRIDE_GRAVITY_CHANGED = 1u;
constexpr uint32_t REPLAY_WORLD_OVERRIDE_FLUID_HEIGHT_CHANGED = 2u;
constexpr uint32_t REPLAY_WORLD_OVERRIDE_FLUID_DENSITY_CHANGED = 4u;
constexpr uint32_t REPLAY_LAUNCHER_FIRE_PROJECTILE = 1u;
constexpr uint32_t REPLAY_EDITOR_PLACE_FIXED = 1u;
constexpr uint32_t REPLAY_EDITOR_PLACE_TERRAIN_ALIGN = 2u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_TRANSLATE = 1u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_ROTATE = 2u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_SCALE = 4u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_SUPPORTED =
    REPLAY_EDITOR_TRANSFORM_TRANSLATE | REPLAY_EDITOR_TRANSFORM_ROTATE | REPLAY_EDITOR_TRANSFORM_SCALE;
constexpr uint32_t REPLAY_GENERATED_SCENE_EXACT_SOLVER_COUNTS = 1u;
constexpr uint32_t REPLAY_GENERATED_SCENE_UI_MODEL_COUNT = 2u;
constexpr uint32_t REPLAY_GENERATED_SCENE_UI_SOLVER_COUNTS = 4u;
constexpr uint32_t REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT = 8u;
constexpr uint32_t REPLAY_GENERATED_SCENE_OVERRIDE_MASK = 3u << REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT;

#ifdef _DEBUG
float ReplaySaveProbeDistanceSquared( const Vector3& a, const Vector3& b )
{
    const Vector3 delta = a - b;
    return delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
}


struct ReplaySaveProbeEventCoverageContext
{
    RunReplaySaveProbeState& saveProbe;
    ReplayRuntime& replayRuntime;
    RuntimeTools& runtimeTools;
    RunSceneState& scene;
    RunSubsystemState& systems;
    SkullbonezCore::Environment::WorldEnvironment& world;
    SkullbonezCore::GameObjects::GameModelCollection& models;
    int gameModelCapacity = 0;
};


template <typename EnterInteractiveSceneRun>
SbResult InjectReplaySaveProbeEventCoverage( ReplaySaveProbeEventCoverageContext& context,
                                             EnterInteractiveSceneRun enterInteractiveSceneRun )
{
    context.saveProbe.eventCoverageInjected = true;
    const float currentGravity = context.world.GetGravity();
    const float probeGravity = currentGravity != 0.0f ? currentGravity * 0.95f : -0.25f;
    ApplyUIWorldOverride( context.world,
                          context.replayRuntime,
                          probeGravity,
                          context.world.GetFluidSurfaceHeight(),
                          context.world.GetFluidDensity() );
    context.runtimeTools.Editor().placementScale = Vector3( 2.0f, 2.0f, 2.0f );
    context.runtimeTools.Editor().autoTerrainAlign = false;
    const int modelCountBeforePlace = context.models.SceneEntityCount();
    EditorObjectPlacementContext placementContext{ context.runtimeTools.Editor(),
                                                   context.models,
                                                   context.scene,
                                                   context.world,
                                                   context.systems.terrain.get(),
                                                   context.systems.assets,
                                                   context.gameModelCapacity };
    EditorObjectPlacementRequest placementRequest{ SkullbonezCore::UI::EditorTab::OBJECT_BOX,
                                                   true,
                                                   Vector3( 18.0f, 0.0f, 18.0f ) };
    EditorObjectPlacementResult placementResult;
    if ( CanPlaceEditorObjectAtTerrainPoint( placementContext, placementRequest ) )
    {
        enterInteractiveSceneRun();
        PlaceEditorObjectAtTerrainPoint( placementContext, placementRequest, placementResult );
    }
    if ( placementResult.placed )
    {
        context.replayRuntime.RecordEditorPlaceEvent( placementResult.objectType,
                                                      placementResult.fixedObject,
                                                      placementResult.autoTerrainAlign,
                                                      placementResult.modelCountBefore,
                                                      placementResult.terrainPoint,
                                                      placementResult.placementScale,
                                                      placementResult.placementYawRadians );
        const PhysicsBodyRecord* placedBodyBeforeEdit =
            context.models.GetPhysicsEngine().BodyStore().RecordForHandle( placementResult.placedBody );
        if ( !placedBodyBeforeEdit )
        {
            return ReplayProbeFailure( "replay save probe failed to resolve placed body record" );
        }
        // Why: placement has already registered a PhysicsBodyHandle. Use the
        // authoritative body row as the starting transform, then commit the
        // edited descriptor back into the stores below.
        SkullbonezCore::GameObjects::PhysicsBodyStateEdit placedBodyEdit;
        placedBodyEdit.hasPosition = true;
        placedBodyEdit.position = placedBodyBeforeEdit->position + Vector3( 4.0f, 0.0f, 0.0f );
        Quaternion placedOrientation = placedBodyBeforeEdit->orientation;
        placedOrientation.RotateAboutAxis( Vector3( 0.0f, 1.0f, 0.0f ), 0.25f );
        placedBodyEdit.hasOrientation = true;
        placedBodyEdit.orientation = placedOrientation;
        const ColliderRecord* placedColliderBeforeEdit =
            TryGetEditorTransformColliderRecord( context.models,
                                                 placementResult.placedCollider,
                                                 modelCountBeforePlace,
                                                 placedBodyBeforeEdit->replayBodyId );
        if ( !placedColliderBeforeEdit )
        {
            return ReplayProbeFailure( "replay save probe failed to resolve placed collider record" );
        }
        const CollisionShape placedShapeBeforeScale = placedColliderBeforeEdit->shape;
        constexpr int PROBE_SCALE_AXIS = 0;
        constexpr float PROBE_SCALE_FACTOR = 1.5f;
        CollisionShape placedShapeAfterScale;
        if ( !ScaleShapeAxisFromBase( placedShapeBeforeScale,
                                      PROBE_SCALE_AXIS,
                                      PROBE_SCALE_FACTOR,
                                      placedShapeAfterScale ) )
        {
            return ReplayProbeFailure( "replay save probe failed to apply editor transform scale" );
        }
        placedBodyEdit.hasLinearVelocity = true;
        placedBodyEdit.linearVelocity = Vector3( 0.0f, 0.0f, 0.0f );
        placedBodyEdit.hasAngularVelocity = true;
        placedBodyEdit.angularVelocity = Vector3( 0.0f, 0.0f, 0.0f );
        // Invariant: the replay probe exercises the same explicit collider
        // edit command as the editor instead of relying on a model recapture.
        context.models.ApplyPhysicsBodyColliderEdit(
            modelCountBeforePlace,
            placedBodyEdit,
            MakeColliderCreateDesc( std::move( placedShapeAfterScale ),
                                    placedColliderBeforeEdit->restitution,
                                    placedColliderBeforeEdit->contactMaterialId ) );
        const PhysicsBodyRecord* placedBodyAfterEdit =
            context.models.GetPhysicsEngine().BodyStore().RecordForModelIndex( modelCountBeforePlace );
        if ( !placedBodyAfterEdit || placedBodyAfterEdit->replayBodyId == 0 )
        {
            return ReplayProbeFailure( "replay save probe failed to capture edited body record" );
        }
        context.replayRuntime.RecordEditorTransformEvent(
            modelCountBeforePlace,
            REPLAY_EDITOR_TRANSFORM_TRANSLATE | REPLAY_EDITOR_TRANSFORM_ROTATE | REPLAY_EDITOR_TRANSFORM_SCALE,
            placedBodyAfterEdit->replayBodyId,
            placedBodyAfterEdit->position,
            placedBodyAfterEdit->orientation,
            context.models.SceneEntityCount(),
            PROBE_SCALE_AXIS,
            PROBE_SCALE_FACTOR );
    }
    context.runtimeTools.RayCastTest().projectileSpeed += 1.0f;
    context.replayRuntime.RecordLauncherConfigEvent( 2u,
                                                     context.runtimeTools.RayCastTest().impulseStrength,
                                                     context.runtimeTools.RayCastTest().projectileSpeed );
    Vector3 rayOrigin;
    Vector3 rayDirection;
    Vector3 cameraUp;
    if ( context.runtimeTools.TryBuildLauncherCameraRay( context.systems.cameras, rayOrigin, rayDirection, cameraUp ) )
    {
        context.replayRuntime.RecordLauncherFireEvent(
            rayOrigin,
            rayDirection,
            cameraUp,
            context.runtimeTools.RayCastTest().fireMode == RunLauncherFireMode::Projectile,
            context.runtimeTools.RayCastTest().impulseStrength,
            context.runtimeTools.RayCastTest().projectileSpeed,
            context.models.SceneEntityCount() );
        // Why: RuntimeTools now fails closed unless Run has completed the cold
        // collection-to-store topology repair at the owner boundary.
        const bool launcherStoresReady = context.models.RepairPhysicsBodyAndColliderTopology();
        if ( launcherStoresReady && context.runtimeTools.FireLauncherRay( context.models,
                                                                          context.scene,
                                                                          context.systems.terrain.get(),
                                                                          context.gameModelCapacity,
                                                                          rayOrigin,
                                                                          rayDirection,
                                                                          cameraUp ) )
        {
            context.scene.modelCount = context.models.SceneEntityCount();
        }
    }
    return SbResult::Success();
}


struct ReplaySaveProbeArtifactContext
{
    RunReplaySaveProbeState& saveProbe;
    ReplayRuntime& replayRuntime;
    SkullbonezCore::GameObjects::GameModelCollection& models;
};


SbResult ValidateReplaySaveProbeArtifact( ReplaySaveProbeArtifactContext& context )
{
    ReplayV2SaveResult result;
    if ( !context.replayRuntime.SavePresentationWithSolverHashes( context.saveProbe.path, &result ) )
    {
        return ReplayProbeFailure( "replay save probe failed to write v2 presentation artifact" );
    }
    if ( result.solverHashCount < result.sampleCount )
    {
        return ReplayProbeFailure( "replay save probe wrote v2 artifact without a full solver hash track" );
    }
    if ( result.solverCheckpointCount == 0 )
    {
        return ReplayProbeFailure( "replay save probe wrote v2 artifact without solver checkpoint chunks" );
    }
    if ( result.eventCount == 0 )
    {
        return ReplayProbeFailure( "replay save probe wrote v2 artifact without event chunks" );
    }
    if ( result.eventCursorCount == 0 )
    {
        return ReplayProbeFailure( "replay save probe wrote v2 artifact without checkpoint event cursors" );
    }

    std::vector<ReplayPresentationSample> loadedSamples;
    ReplayV2LoadResult loadResult;
    if ( !ReplayV2Artifact::LoadPresentation( context.saveProbe.path, loadedSamples, &loadResult ) )
    {
        return ReplayProbeFailure( "replay save probe failed to reload v2 presentation artifact" );
    }
    if ( loadedSamples.size() < 2 )
    {
        return ReplayProbeFailure( "replay save probe loaded too few v2 presentation samples" );
    }

    const std::size_t selectedIndex = (std::min)( loadedSamples.size() / 4, loadedSamples.size() - 2 );
    const ReplayPresentationSample& selected = loadedSamples[selectedIndex];
    const ReplayPresentationSample& live = loadedSamples.back();
    if ( selected.frameIndex >= live.frameIndex )
    {
        return ReplayProbeFailure( "replay save probe could not seek to an older loaded v2 sample" );
    }

    const ReplayBodyPresentationSample* selectedBody = nullptr;
    const ReplayBodyPresentationSample* liveBody = nullptr;
    float bestDistanceSquared = 0.0f;
    for ( const ReplayBodyPresentationSample& candidate : selected.bodies )
    {
        for ( const ReplayBodyPresentationSample& liveCandidate : live.bodies )
        {
            if ( liveCandidate.id.value != candidate.id.value )
            {
                continue;
            }

            const float candidateDistanceSquared =
                ReplaySaveProbeDistanceSquared( liveCandidate.position, candidate.position );
            if ( candidateDistanceSquared > bestDistanceSquared )
            {
                bestDistanceSquared = candidateDistanceSquared;
                selectedBody = &candidate;
                liveBody = &liveCandidate;
            }
            break;
        }
    }
    if ( !selectedBody || !liveBody || bestDistanceSquared < 0.0001f )
    {
        return ReplayProbeFailure( "replay save probe did not find a moved body in the loaded v2 artifact" );
    }

    const int probedModelIndex = liveBody->modelIndex;
    const PhysicsBodyRecord* probedBody = TryGetReplayProbeBodyRecord( context.models, probedModelIndex );
    if ( !probedBody )
    {
        return ReplayProbeFailure( "replay save probe loaded an invalid live body index" );
    }

    const Vector3 preApplyPosition = probedBody->position;
    const float preLiveDeltaSquared = ReplaySaveProbeDistanceSquared( preApplyPosition, liveBody->position );
    if ( preLiveDeltaSquared > 0.0001f )
    {
        return ReplayProbeFailure( "replay save probe live body did not match the loaded v2 live sample" );
    }

    const bool applied = ApplyReplayProbePresentationSampleForRender( context.models, context.replayRuntime, selected );
    if ( !applied )
    {
        return ReplayProbeFailure( "replay save probe failed to apply the loaded v2 presentation sample" );
    }
    const PhysicsBodyRecord* appliedBody = TryGetReplayProbeBodyRecord( context.models, probedModelIndex );
    if ( !appliedBody )
    {
        RestoreReplayProbeRenderInstances( context.models );
        return ReplayProbeFailure( "replay save probe lost the selected live body after applying the v2 sample" );
    }
    const Vector3 liveAfterApplyPosition = appliedBody->position;
    const float livePreservedDeltaSquared = ReplaySaveProbeDistanceSquared( liveAfterApplyPosition, preApplyPosition );
    if ( livePreservedDeltaSquared > 0.0001f )
    {
        RestoreReplayProbeRenderInstances( context.models );
        return ReplayProbeFailure( "replay save probe mutated the live body while applying the v2 sample" );
    }

    Vector3 appliedRenderPosition;
    if ( !TryPrepareReplayProbeRenderPosition( context.models, probedModelIndex, appliedRenderPosition ) )
    {
        RestoreReplayProbeRenderInstances( context.models );
        return ReplayProbeFailure( "replay save probe lost the selected render instance after applying the v2 sample" );
    }
    const float appliedDeltaSquared = ReplaySaveProbeDistanceSquared( appliedRenderPosition, selectedBody->position );
    if ( appliedDeltaSquared > 0.0001f )
    {
        RestoreReplayProbeRenderInstances( context.models );
        return ReplayProbeFailure( "replay save probe did not move the render instance to the loaded v2 sample" );
    }

    RestoreReplayProbeRenderInstances( context.models );
    const PhysicsBodyRecord* restoredBody = TryGetReplayProbeBodyRecord( context.models, probedModelIndex );
    if ( !restoredBody )
    {
        return ReplayProbeFailure( "replay save probe lost the selected live body after restoring the v2 sample" );
    }
    const Vector3 restoredPosition = restoredBody->position;
    const float restoredDeltaSquared = ReplaySaveProbeDistanceSquared( restoredPosition, preApplyPosition );
    if ( restoredDeltaSquared > 0.0001f )
    {
        return ReplayProbeFailure( "replay save probe live body changed after applying the loaded v2 sample" );
    }

    context.saveProbe.completed = true;
    printf( "[replay] Save probe wrote: path=%s samples=%llu bodies=%llu solver_hashes=%llu "
            "solver_checkpoints=%llu events=%llu event_cursors=%llu bytes=%llu\n",
            context.saveProbe.path,
            static_cast<unsigned long long>( result.sampleCount ),
            static_cast<unsigned long long>( result.bodyDictionaryCount ),
            static_cast<unsigned long long>( result.solverHashCount ),
            static_cast<unsigned long long>( result.solverCheckpointCount ),
            static_cast<unsigned long long>( result.eventCount ),
            static_cast<unsigned long long>( result.eventCursorCount ),
            static_cast<unsigned long long>( result.fileBytes ) );
    printf( "[replay] Save probe loaded: samples=%llu bodies=%llu first_frame=%llu selected_frame=%llu "
            "latest_frame=%llu body_id=%u distance_sq=%.6f\n",
            static_cast<unsigned long long>( loadResult.sampleCount ),
            static_cast<unsigned long long>( loadResult.bodyDictionaryCount ),
            static_cast<unsigned long long>( loadResult.firstFrame ),
            static_cast<unsigned long long>( selected.frameIndex ),
            static_cast<unsigned long long>( live.frameIndex ),
            selectedBody->id.value,
            bestDistanceSquared );
    PostQuitMessage( 0 );
    return SbResult::Success();
}
#endif
// Concept: replay flags are compact wire-format fields. Keep these masks local
// to decode logic so new replay payload versions do not inherit accidental UI
// or runtime enum values.
SceneGeneratedModelContext BuildSceneGeneratedModelContext( RunSceneState& scene,
                                                            const EngineConfig& config,
                                                            SkullbonezCore::Environment::WorldEnvironment& world,
                                                            SkullbonezCore::Geometry::Terrain* terrain,
                                                            SkullbonezCore::GameObjects::GameModelCollection& models,
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
    RunSubsystemState& systems;
    SkullbonezCore::Environment::WorldEnvironment& world;
    SkullbonezCore::GameObjects::GameModelCollection& models;
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
                                                                          context.scene,
                                                                          context.systems.terrain.get(),
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

template <typename EnterInteractiveSceneRun>
bool ApplyReplayRestoreEditorPlaceEvent( RuntimeTools& runtimeTools,
                                         SkullbonezCore::GameObjects::GameModelCollection& models,
                                         RunSceneState& scene,
                                         SkullbonezCore::Environment::WorldEnvironment& world,
                                         RunSubsystemState& systems,
                                         int gameModelCapacity,
                                         const ReplayEventSample& event,
                                         char* eventOutReason,
                                         std::size_t eventReasonSize,
                                         EnterInteractiveSceneRun enterInteractiveSceneRun )
{
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
                                                   scene,
                                                   world,
                                                   systems.terrain.get(),
                                                   systems.assets,
                                                   gameModelCapacity };
    EditorObjectPlacementRequest placementRequest{ event.value0,
                                                   ( event.flags & REPLAY_EDITOR_PLACE_FIXED ) != 0,
                                                   terrainPoint };
    EditorObjectPlacementResult placementResult;
    bool placed = false;
    if ( CanPlaceEditorObjectAtTerrainPoint( placementContext, placementRequest ) )
    {
        enterInteractiveSceneRun();
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

bool ApplyReplayRestoreEditorTransformEvent( SkullbonezCore::GameObjects::GameModelCollection& models,
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

    const PhysicsBodyStore& bodyStoreBeforeEdit = models.GetPhysicsEngine().BodyStore();
    const PhysicsBodyHandle eventBody =
        bodyStoreBeforeEdit.HandleForReplayBodyId( static_cast<uint32_t>( event.value1 ), event.value0 );
    const PhysicsBodyRecord* eventBodyRecord = bodyStoreBeforeEdit.RecordForHandle( eventBody );
    if ( !eventBodyRecord || bodyStoreBeforeEdit.ModelIndexForHandle( eventBody ) != event.value0 ||
         eventBodyRecord->replayBodyId != static_cast<uint32_t>( event.value1 ) )
    {
        WriteReplayProbeReason( eventOutReason, eventReasonSize, "editor transform replay body id mismatch" );
        return false;
    }

    SkullbonezCore::GameObjects::PhysicsBodyStateEdit bodyEdit;
    if ( event.flags & REPLAY_EDITOR_TRANSFORM_TRANSLATE )
    {
        bodyEdit.hasPosition = true;
        bodyEdit.position = position;
    }
    if ( event.flags & REPLAY_EDITOR_TRANSFORM_ROTATE )
    {
        bodyEdit.hasOrientation = true;
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
    bodyEdit.hasLinearVelocity = true;
    bodyEdit.linearVelocity = Vector3( 0.0f, 0.0f, 0.0f );
    bodyEdit.hasAngularVelocity = true;
    bodyEdit.angularVelocity = Vector3( 0.0f, 0.0f, 0.0f );
    if ( hasEditedColliderDesc )
    {
        models.ApplyPhysicsBodyColliderEdit( event.value0, bodyEdit, std::move( editedColliderDesc ) );
    }
    else
    {
        models.ApplyPhysicsBodyEdit( event.value0, bodyEdit );
    }
    // Why: the edited-state commit has already refreshed the edited body row.
    // The wake decision should read the committed PhysicsBodyStore record, not
    // presentation/authored pose data.
    PhysicsEngine& physics = models.GetPhysicsEngine();
    const PhysicsBodyStore& bodyStore = physics.BodyStore();
    const PhysicsBodyHandle body =
        bodyStore.HandleForReplayBodyId( static_cast<uint32_t>( event.value1 ), event.value0 );
    const PhysicsBodyRecord* bodyRecord = bodyStore.RecordForHandle( body );
    if ( bodyRecord && !bodyRecord->isFixed )
    {
        physics.WakeBody( body );
    }
    WriteReplayProbeReason( eventOutReason, eventReasonSize, "applied editor transform" );
    return true;
}

// Concept: target restore replays only solver-relevant timeline events. Runtime
// commands that would change scenes stay rejected here, while editor placement
// borrows Run's interactive-mode transition through a narrow caller callback.
template <typename EnterInteractiveSceneRun>
bool ApplyReplayRestoreEventForTarget( ReplayRestoreEventContext& context,
                                       const ReplayEventSample& event,
                                       char* eventOutReason,
                                       std::size_t eventReasonSize,
                                       EnterInteractiveSceneRun enterInteractiveSceneRun )
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
    case ReplayEventKind::RuntimeCommand:
    {
        const RuntimeCommandType commandType = static_cast<RuntimeCommandType>( event.value0 );
        switch ( commandType )
        {
        case RuntimeCommandType::SaveScreenshot:
        case RuntimeCommandType::SaveSceneDefaults:
        case RuntimeCommandType::SaveRenderDefaults:
        case RuntimeCommandType::SaveSkyDefaults:
        case RuntimeCommandType::Quit:
        case RuntimeCommandType::None:
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "ignored non-solver runtime command" );
            return true;
        case RuntimeCommandType::ResetCurrentScene:
        case RuntimeCommandType::LoadSceneIndex:
        case RuntimeCommandType::LoadDemoScene:
        case RuntimeCommandType::CreateScene:
        case RuntimeCommandType::AdvanceScene:
        default:
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "unsupported runtime timeline mutation event" );
            return false;
        }
    }
    case ReplayEventKind::BranchRestore:
        WriteReplayProbeReason( eventOutReason, eventReasonSize, "unsupported timeline mutation event" );
        return false;
    case ReplayEventKind::EditorPlace:
        return ApplyReplayRestoreEditorPlaceEvent( context.runtimeTools,
                                                   context.models,
                                                   context.scene,
                                                   context.world,
                                                   context.systems,
                                                   context.gameModelCapacity,
                                                   event,
                                                   eventOutReason,
                                                   eventReasonSize,
                                                   enterInteractiveSceneRun );
    case ReplayEventKind::EditorTransform:
        return ApplyReplayRestoreEditorTransformEvent( context.models, event, eventOutReason, eventReasonSize );
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
                                          const SkullbonezCore::GameObjects::GameModelCollection& models )
{
    const int liveModelCount = models.SceneEntityCount();
    if ( checkpoint.bodies.size() > static_cast<std::size_t>( liveModelCount ) )
    {
        return false;
    }
    for ( const ReplaySolverBodySample& body : checkpoint.bodies )
    {
        if ( body.modelIndex < 0 || body.modelIndex >= liveModelCount )
        {
            return false;
        }
        const PhysicsBodyRecord* bodyRecord = TryGetReplayProbeBodyRecord( models, body.modelIndex );
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
    ScopedReplayProbeProfilerFrame()
    {
        PROFILE_FRAME_BEGIN();
    }
    ~ScopedReplayProbeProfilerFrame()
    {
        PROFILE_FRAME_END();
    }
};

void FormatReplayRestoreDivergenceMessage( char* message,
                                           std::size_t messageSize,
                                           ReplayFrameIndex currentFrame,
                                           uint64_t restoredSolverHash,
                                           uint64_t restoredPresentationHash,
                                           std::size_t restoredBodyCount,
                                           const ReplayV2SolverHashSample& expectedHash,
                                           const std::vector<ReplayPresentationSample>& presentationSamples,
                                           const SkullbonezCore::GameObjects::GameModelCollection& models,
                                           std::size_t eventsApplied )
{
    const ReplayPresentationSample* expectedPresentation =
        FindReplayPresentationForFrame( presentationSamples, currentFrame );
    const PhysicsBodyRecord* restoredBody = TryGetReplayProbeBodyRecord( models, 0 );
    if ( expectedPresentation && !expectedPresentation->bodies.empty() && restoredBody )
    {
        const ReplayBodyPresentationSample& expectedBody = expectedPresentation->bodies[0];
        const Vector3& restoredPosition = restoredBody->position;
        const Vector3& restoredVelocity = restoredBody->linearVelocity;
        float restoredQx = 0.0f;
        float restoredQy = 0.0f;
        float restoredQz = 0.0f;
        float restoredQw = 1.0f;
        restoredBody->orientation.GetComponents( restoredQx, restoredQy, restoredQz, restoredQw );

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
    RunSceneState& scene;
    const EngineConfig& config;
    RunSubsystemState& systems;
    SkullbonezCore::Environment::WorldEnvironment& world;
    SkullbonezCore::GameObjects::GameModelCollection& models;
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
// physics step. The helper reports failure facts back to Run so the live-state
// fallback and diagnostic logging stay in the owning method.
template <typename CaptureCurrentReplaySolverHash, typename EnterInteractiveSceneRun>
bool StepReplayRestoreTarget( ReplayRestoreStepContext& context,
                              ReplayRestoreStepResult& result,
                              ReplayRestoreStepFailure& failure,
                              CaptureCurrentReplaySolverHash captureCurrentReplaySolverHash,
                              EnterInteractiveSceneRun enterInteractiveSceneRun )
{
    result.currentFrame = context.checkpoint.frameIndex;
    result.currentSceneFrame = context.checkpoint.sceneFrame;
    result.eventCursor = context.checkpoint.eventCursor;
    result.eventsApplied = 0;
    result.unsupportedEvents = 0;
    context.scene.currentFrame = result.currentSceneFrame;

    ScopedReplayProbeProfilerFrame profilerFrame;
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
                                                    enterInteractiveSceneRun ) )
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
        StepRuntimePhysicsTick( context.models,
                                PHYSICS_FIXED_DT,
                                context.config,
                                physicsWorldForces,
                                *context.systems.workerPool );
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

// Why: making a restored file state live still has one Run-owned side effect:
// resetting the active timeline. Keep that as a caller callback while this
// helper owns only branch metadata and event recording on ReplayRuntime.
template <typename ResetReplayTimeline>
void ApplyReplayRestoreLiveBranch( ReplayRuntime& replayRuntime,
                                   const ReplaySolverFrameSample& checkpoint,
                                   const ReplayV2SolverHashSample& target,
                                   RunReplayV2TargetRestoreResult& outResult,
                                   ResetReplayTimeline resetReplayTimeline )
{
    const uint32_t parentBranchId =
        checkpoint.branch.branchId != 0
            ? checkpoint.branch.branchId
            : ( replayRuntime.Branch().branchId != 0 ? replayRuntime.Branch().branchId : 1u );
    ReplayBranchInfo restoredBranch;
    restoredBranch.branchId = (std::max)( replayRuntime.Branch().branchId, parentBranchId ) + 1u;
    restoredBranch.parentBranchId = parentBranchId;
    restoredBranch.startFrame = 0;
    restoredBranch.sourceFrame = target.frameIndex;
    restoredBranch.sourceSolverHash = target.solverHash;
    replayRuntime.Branch() = restoredBranch;
    resetReplayTimeline();
    replayRuntime.RecordEvent( ReplayEventKind::BranchRestore,
                               0,
                               0,
                               static_cast<int32_t>( parentBranchId ),
                               target.sceneFrame,
                               0,
                               0,
                               target.solverHash,
                               "hash-verified v2 file restore" );
    outResult.branchId = restoredBranch.branchId;
    outResult.parentBranchId = parentBranchId;
    outResult.madeLiveBranch = true;
}

struct ReplayRestoreOwnerContext
{
    RuntimeTools& runtimeTools;
    SimulationSystem& simulation;
    SceneController& sceneController;
    RunSceneState& scene;
    const EngineConfig& config;
    RunSubsystemState& systems;
    SkullbonezCore::Environment::WorldEnvironment& world;
    SkullbonezCore::GameObjects::GameModelCollection& models;
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
    context.scene.ResetSceneObjectIdCursor( context.models.GetPhysicsEngine().BodyStore() );
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
        const SbResult setupResult = SceneGeneratedSetup::SetUpSolverObjects(
            BuildSceneGeneratedModelContext( context.scene,
                                             context.config,
                                             context.world,
                                             context.systems.terrain.get(),
                                             context.models,
                                             context.models.GetPhysicsEngine(),
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
        const SbResult setupResult = SceneGeneratedSetup::SetUpGameModels(
            BuildSceneGeneratedModelContext( context.scene,
                                             context.config,
                                             context.world,
                                             context.systems.terrain.get(),
                                             context.models,
                                             context.models.GetPhysicsEngine(),
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
// target. Keeping the event and step contexts here leaves Run's method as a
// transaction coordinator instead of another replay loop owner.
template <typename CaptureCurrentReplaySolverHash, typename EnterInteractiveSceneRun>
bool RunReplayRestoreTargetStep( ReplayRestoreOwnerContext& context,
                                 const ReplayRestoreArtifactData& artifact,
                                 const ReplaySolverFrameSample& checkpoint,
                                 const ReplayV2SolverHashSample& target,
                                 ReplayRestoreStepResult& stepResult,
                                 ReplayRestoreStepFailure& stepFailure,
                                 CaptureCurrentReplaySolverHash captureCurrentReplaySolverHash,
                                 EnterInteractiveSceneRun enterInteractiveSceneRun )
{
    ReplayRestoreEventContext restoreEventContext{ context.runtimeTools,
                                                   context.scene,
                                                   context.systems,
                                                   context.world,
                                                   context.models,
                                                   context.gameModelCapacity };
    ReplayRestoreStepContext stepContext{ context.runtimeTools,
                                          context.scene,
                                          context.config,
                                          context.systems,
                                          context.world,
                                          context.models,
                                          restoreEventContext,
                                          artifact,
                                          checkpoint,
                                          target };
    if ( !StepReplayRestoreTarget( stepContext,
                                   stepResult,
                                   stepFailure,
                                   captureCurrentReplaySolverHash,
                                   enterInteractiveSceneRun ) )
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

#ifdef _DEBUG
SbResult Run::TickReplayScrubProbe()
{
    auto distanceSquared = []( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b ) -> float
    {
        const Math::Vector::Vector3 delta = a - b;
        return delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    };

    if ( !m_replayProbes.scrub.enabled || m_replayProbes.scrub.completed )
    {
        return SbResult::Success();
    }

    const ReplayRecorderStats stats = m_replayRuntime.Presentation().GetStats();
    if ( stats.sampleCount < static_cast<std::size_t>( m_replayProbes.scrub.minSampleCount ) )
    {
        return SbResult::Success();
    }

    const ReplayPresentationSample* selected =
        m_replayRuntime.Presentation().SampleAtNormalized( m_replayProbes.scrub.normalized );
    const ReplayPresentationSample* live = m_replayRuntime.Presentation().LatestSample();
    if ( !selected || !live || selected->frameIndex >= live->frameIndex )
    {
        return ReplayProbeFailure( "replay scrub probe could not select an older replay sample" );
    }

    const ReplayBodyPresentationSample* selectedBody = nullptr;
    const ReplayBodyPresentationSample* liveBody = nullptr;
    float bestDistanceSquared = 0.0f;
    for ( const ReplayBodyPresentationSample& candidate : selected->bodies )
    {
        if ( candidate.fixed )
        {
            continue;
        }

        for ( const ReplayBodyPresentationSample& liveCandidate : live->bodies )
        {
            if ( liveCandidate.id.value != candidate.id.value )
            {
                continue;
            }

            const float candidateDistanceSquared = distanceSquared( liveCandidate.position, candidate.position );
            if ( candidateDistanceSquared > bestDistanceSquared )
            {
                bestDistanceSquared = candidateDistanceSquared;
                selectedBody = &candidate;
                liveBody = &liveCandidate;
            }
            break;
        }
    }

    if ( !selectedBody || !liveBody || bestDistanceSquared < m_replayProbes.scrub.minDistanceSquared )
    {
        return ReplayProbeFailure( "replay scrub probe did not find a moved body in the selected replay window" );
    }

    const int probedModelIndex = liveBody->modelIndex;
    const PhysicsBodyRecord* probedBody = TryGetReplayProbeBodyRecord( m_cGameModelCollection, probedModelIndex );
    if ( !probedBody )
    {
        return ReplayProbeFailure( "replay scrub probe selected an invalid live body index" );
    }

    // Why: scrub probes prove presentation overrides do not mutate live
    // simulation state. Read that state from PhysicsBodyStore so the proof does
    // not depend on temporary presentation rows.
    const Math::Vector::Vector3 preApplyPosition = probedBody->position;
    const float preLiveDeltaSquared = distanceSquared( preApplyPosition, liveBody->position );
    if ( preLiveDeltaSquared > m_replayProbes.scrub.minDistanceSquared )
    {
        return ReplayProbeFailure(
            "replay scrub probe live body did not match the current replay sample before applying scrub state" );
    }

    const bool applied =
        ApplyReplayProbePresentationSampleForRender( m_cGameModelCollection, m_replayRuntime, *selected );
    if ( !applied )
    {
        return ReplayProbeFailure( "replay scrub probe failed to apply the selected presentation sample" );
    }
    const PhysicsBodyRecord* appliedBody = TryGetReplayProbeBodyRecord( m_cGameModelCollection, probedModelIndex );
    if ( !appliedBody )
    {
        RestoreReplayProbeRenderInstances( m_cGameModelCollection );
        return ReplayProbeFailure( "replay scrub probe lost the selected live body after applying scrub state" );
    }
    const Math::Vector::Vector3 liveAfterApplyPosition = appliedBody->position;
    const float livePreservedDeltaSquared = distanceSquared( liveAfterApplyPosition, preApplyPosition );
    if ( livePreservedDeltaSquared > m_replayProbes.scrub.minDistanceSquared )
    {
        RestoreReplayProbeRenderInstances( m_cGameModelCollection );
        return ReplayProbeFailure( "replay scrub probe mutated the live body while applying scrub state" );
    }

    Math::Vector::Vector3 appliedRenderPosition;
    if ( !TryPrepareReplayProbeRenderPosition( m_cGameModelCollection, probedModelIndex, appliedRenderPosition ) )
    {
        RestoreReplayProbeRenderInstances( m_cGameModelCollection );
        return ReplayProbeFailure( "replay scrub probe lost the selected render instance after applying scrub state" );
    }
    const float appliedDeltaSquared = distanceSquared( appliedRenderPosition, selectedBody->position );
    if ( appliedDeltaSquared > m_replayProbes.scrub.minDistanceSquared )
    {
        RestoreReplayProbeRenderInstances( m_cGameModelCollection );
        return ReplayProbeFailure(
            "replay scrub probe did not move the render instance to the selected replay sample" );
    }

    RestoreReplayProbeRenderInstances( m_cGameModelCollection );
    const PhysicsBodyRecord* restoredBody = TryGetReplayProbeBodyRecord( m_cGameModelCollection, probedModelIndex );
    if ( !restoredBody )
    {
        return ReplayProbeFailure( "replay scrub probe lost the selected live body after restoring scrub state" );
    }
    const Math::Vector::Vector3 restoredPosition = restoredBody->position;
    const float restoredDeltaSquared = distanceSquared( restoredPosition, preApplyPosition );
    const bool restored = restoredDeltaSquared <= m_replayProbes.scrub.minDistanceSquared;
    if ( !restored )
    {
        return ReplayProbeFailure(
            "replay scrub probe did not restore the live model after applying the selected sample" );
    }

    m_diagnosticsRuntime.LogReplayScrubProbe( SceneState(),
                                              *selected,
                                              *live,
                                              *selectedBody,
                                              *liveBody,
                                              m_replayProbes.scrub.normalized,
                                              bestDistanceSquared,
                                              applied,
                                              restored,
                                              preLiveDeltaSquared,
                                              appliedDeltaSquared,
                                              restoredDeltaSquared );

    m_replayProbes.scrub.completed = true;
    printf(
        "[replay] Scrub probe passed: selected_replay_frame=%llu live_replay_frame=%llu body_id=%u distance_sq=%.6f\n",
        static_cast<unsigned long long>( selected->frameIndex ),
        static_cast<unsigned long long>( live->frameIndex ),
        selectedBody->id.value,
        bestDistanceSquared );
    PostQuitMessage( 0 );
    return SbResult::Success();
}

SbResult Run::TickReplayRestoreProbe()
{
    if ( !m_replayProbes.restore.enabled || m_replayProbes.restore.completed )
    {
        return SbResult::Success();
    }

    const ReplayRecorderStats stats = m_replayRuntime.Solver().GetStats();
    if ( stats.sampleCount < static_cast<std::size_t>( m_replayProbes.restore.minSampleCount ) )
    {
        return SbResult::Success();
    }

    const ReplaySolverFrameSample* selectedSample =
        m_replayRuntime.Solver().SampleAtNormalized( m_replayProbes.restore.normalized );
    const ReplaySolverFrameSample* latestSample = m_replayRuntime.Solver().LatestSample();
    if ( !selectedSample || !latestSample )
    {
        return ReplayProbeFailure( "replay restore probe could not select retained solver samples" );
    }
    if ( selectedSample->frameIndex >= latestSample->frameIndex )
    {
        return ReplayProbeFailure( "replay restore probe did not select an older solver sample" );
    }

    const ReplaySolverFrameSample selected = *selectedSample;
    const ReplayFrameIndex latestFrame = latestSample->frameIndex;
    const uint64_t selectedHash = selected.solverHash;
    char reason[160] = {};
    const bool restored = RestoreReplaySolverSampleAsLive( selected, reason, sizeof( reason ) );
    if ( !restored )
    {
        return SbResult::Failure( REPLAY_PROBE_OWNER,
                                  "replay restore probe failed: %s",
                                  reason[0] != '\0' ? reason : "unknown restore failure" );
    }

    m_replayProbes.restore.completed = true;
    printf( "[replay] Restore probe passed: target_replay_frame=%llu previous_live_replay_frame=%llu "
            "solver_hash=0x%016llX\n",
            static_cast<unsigned long long>( selected.frameIndex ),
            static_cast<unsigned long long>( latestFrame ),
            static_cast<unsigned long long>( selectedHash ) );
    PostQuitMessage( 0 );
    return SbResult::Success();
}

SbResult Run::TickReplaySaveProbe()
{
    if ( !m_replayProbes.save.enabled || m_replayProbes.save.completed )
    {
        return SbResult::Success();
    }

    const ReplayRecorderStats stats = m_replayRuntime.Presentation().GetStats();
    if ( !m_replayProbes.save.runtimeResetCoverageInjected && stats.sampleCount >= 4 )
    {
        m_replayProbes.save.runtimeResetCoverageInjected = true;
        m_replayProbes.save.eventCoverageInjected = false;
        m_runtimeCommands.Push( RuntimeCommand{ RuntimeCommandType::ResetCurrentScene } );
        return SbResult::Success();
    }

    if ( !m_replayProbes.save.eventCoverageInjected && stats.sampleCount >= 4 )
    {
        ReplaySaveProbeEventCoverageContext eventCoverageContext{ m_replayProbes.save,
                                                                  m_replayRuntime,
                                                                  m_runtimeTools,
                                                                  SceneState(),
                                                                  m_systems,
                                                                  m_cWorldEnvironment,
                                                                  m_cGameModelCollection,
                                                                  m_startup.gameModelCapacity };
        const SbResult eventCoverageResult =
            InjectReplaySaveProbeEventCoverage( eventCoverageContext, [this]() { EnterInteractiveSceneRun(); } );
        if ( !eventCoverageResult.ok )
        {
            return eventCoverageResult;
        }
    }
    if ( stats.sampleCount < static_cast<std::size_t>( m_replayProbes.save.minSampleCount ) )
    {
        return SbResult::Success();
    }

    ReplaySaveProbeArtifactContext artifactContext{ m_replayProbes.save, m_replayRuntime, m_cGameModelCollection };
    return ValidateReplaySaveProbeArtifact( artifactContext );
}

SbResult Run::VerifyLoadedReplayPresentationProbe( float normalized )
{
    auto distanceSquared = []( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b ) -> float
    {
        const Math::Vector::Vector3 delta = a - b;
        return delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    };

    if ( !m_replayRuntime.HasLoadedPresentation() )
    {
        return ReplayProbeFailure( "replay load probe requires a loaded v2 presentation artifact" );
    }

    if ( m_replayRuntime.Scrubber().liveAdvanceHeld )
    {
        m_replayRuntime.SetLiveAdvanceHeld( false );
    }
    CancelReplayToolDragState();

    m_replayRuntime.ClearCameraFocusForRestore();
    ExitReplayInspectionCamera();
    const bool armed = m_replayRuntime.ArmLoadedPresentationScrubber( std::clamp( normalized, 0.0f, 1.0f ),
                                                                      m_timers.simulationTimer.GetTotalTime() );
    if ( !armed )
    {
        return ReplayProbeFailure( "replay load probe could not arm the loaded presentation scrubber" );
    }
    SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayScrub,
                                                        InteractionExitReason::EnterReplay );
    if ( m_replayRuntime.ShouldUseInspectionCamera() )
    {
        EnterReplayInspectionCamera();
    }
    else
    {
        ExitReplayInspectionCamera();
    }
    const ReplayPresentationSample* selected = m_replayRuntime.CurrentScrubSample();
    const ReplayPresentationSample* latest = m_replayRuntime.LoadedPresentationLatestSample();
    if ( !selected || !latest )
    {
        return ReplayProbeFailure( "replay load probe could not select a loaded presentation sample" );
    }
    if ( selected->frameIndex >= latest->frameIndex )
    {
        return ReplayProbeFailure( "replay load probe did not select an older v2 presentation sample" );
    }

    const ReplayBodyPresentationSample* selectedBody = nullptr;
    const ReplayBodyPresentationSample* latestBody = nullptr;
    float bestDistanceSquared = 0.0f;
    for ( const ReplayBodyPresentationSample& candidate : selected->bodies )
    {
        for ( const ReplayBodyPresentationSample& latestCandidate : latest->bodies )
        {
            if ( latestCandidate.id.value != candidate.id.value )
            {
                continue;
            }

            const float candidateDistanceSquared = distanceSquared( latestCandidate.position, candidate.position );
            if ( candidateDistanceSquared > bestDistanceSquared )
            {
                bestDistanceSquared = candidateDistanceSquared;
                selectedBody = &candidate;
                latestBody = &latestCandidate;
            }
            break;
        }
    }
    if ( !selectedBody || !latestBody || bestDistanceSquared < 0.0001f )
    {
        return ReplayProbeFailure( "replay load probe did not find a moved body in the loaded v2 artifact" );
    }

    const int probedModelIndex = selectedBody->modelIndex;
    const PhysicsBodyRecord* probedBody = TryGetReplayProbeBodyRecord( m_cGameModelCollection, probedModelIndex );
    if ( !probedBody )
    {
        return ReplayProbeFailure( "replay load probe loaded an invalid body index" );
    }

    const Math::Vector::Vector3 preApplyPosition = probedBody->position;
    const bool applied =
        ApplyReplayProbePresentationSampleForRender( m_cGameModelCollection, m_replayRuntime, *selected );
    if ( !applied )
    {
        return ReplayProbeFailure( "replay load probe failed to apply the selected loaded v2 sample" );
    }

    const PhysicsBodyRecord* appliedBody = TryGetReplayProbeBodyRecord( m_cGameModelCollection, probedModelIndex );
    if ( !appliedBody )
    {
        RestoreReplayProbeRenderInstances( m_cGameModelCollection );
        return ReplayProbeFailure( "replay load probe lost the selected body after applying the v2 sample" );
    }
    const Math::Vector::Vector3 liveAfterApplyPosition = appliedBody->position;
    const float livePreservedDeltaSquared = distanceSquared( liveAfterApplyPosition, preApplyPosition );
    if ( livePreservedDeltaSquared > 0.0001f )
    {
        RestoreReplayProbeRenderInstances( m_cGameModelCollection );
        return ReplayProbeFailure( "replay load probe mutated the live body while applying the v2 sample" );
    }

    Math::Vector::Vector3 appliedRenderPosition;
    if ( !TryPrepareReplayProbeRenderPosition( m_cGameModelCollection, probedModelIndex, appliedRenderPosition ) )
    {
        RestoreReplayProbeRenderInstances( m_cGameModelCollection );
        return ReplayProbeFailure( "replay load probe lost the selected render instance after applying the v2 sample" );
    }
    const float appliedDeltaSquared = distanceSquared( appliedRenderPosition, selectedBody->position );
    if ( appliedDeltaSquared > 0.0001f )
    {
        RestoreReplayProbeRenderInstances( m_cGameModelCollection );
        return ReplayProbeFailure(
            "replay load probe did not move the render instance to the selected loaded v2 sample" );
    }

    RestoreReplayProbeRenderInstances( m_cGameModelCollection );
    const PhysicsBodyRecord* restoredBody = TryGetReplayProbeBodyRecord( m_cGameModelCollection, probedModelIndex );
    if ( !restoredBody )
    {
        return ReplayProbeFailure( "replay load probe lost the selected body after restoring the v2 sample" );
    }
    const Math::Vector::Vector3 restoredPosition = restoredBody->position;
    const float restoredDeltaSquared = distanceSquared( restoredPosition, preApplyPosition );
    if ( restoredDeltaSquared > 0.0001f )
    {
        return ReplayProbeFailure( "replay load probe live body changed after applying the selected loaded v2 sample" );
    }

    printf( "[replay] Load probe passed: path=%s samples=%llu bodies=%llu first_frame=%llu selected_frame=%llu "
            "latest_frame=%llu body_id=%u distance_sq=%.6f\n",
            m_replayRuntime.LoadedPresentation().path,
            static_cast<unsigned long long>( m_replayRuntime.LoadedPresentation().samples.size() ),
            static_cast<unsigned long long>( m_replayRuntime.LoadedPresentation().bodyDictionaryCount ),
            static_cast<unsigned long long>( m_replayRuntime.LoadedPresentation().firstFrame ),
            static_cast<unsigned long long>( selected->frameIndex ),
            static_cast<unsigned long long>( latest->frameIndex ),
            selectedBody->id.value,
            bestDistanceSquared );
    return SbResult::Success();
}

SbResult Run::VerifyReplaySolverCheckpointFileProbe( const char* path )
{
    if ( !path || path[0] == '\0' )
    {
        return ReplayProbeFailure( "replay restore file probe requires a v2 artifact path" );
    }

    std::vector<ReplaySolverFrameSample> checkpoints;
    ReplayV2SolverCheckpointLoadResult result;
    if ( !ReplayV2Artifact::LoadSolverCheckpoints( path, checkpoints, &result ) )
    {
        return ReplayProbeFailure( "replay restore file probe failed to load v2 solver checkpoints" );
    }
    if ( checkpoints.empty() )
    {
        return ReplayProbeFailure( "replay restore file probe found no v2 solver checkpoints" );
    }

    const ReplaySolverFrameSample& checkpoint = checkpoints.front();
    if ( checkpoint.eventCursor == 0 )
    {
        return ReplayProbeFailure( "replay restore file probe loaded a checkpoint without an event cursor" );
    }
    char reason[160] = {};
    if ( !RestoreReplaySolverSampleAsLive( checkpoint, reason, sizeof( reason ) ) )
    {
        return SbResult::Failure( REPLAY_PROBE_OWNER,
                                  "replay restore file probe failed: %s",
                                  reason[0] != '\0' ? reason : "unknown restore failure" );
    }

    printf( "[replay] Restore file probe passed: path=%s checkpoints=%llu first_frame=%llu target_frame=%llu "
            "event_cursor=%u bodies=%llu solver_hash=0x%016llX bytes=%llu\n",
            path,
            static_cast<unsigned long long>( result.checkpointCount ),
            static_cast<unsigned long long>( result.firstFrame ),
            static_cast<unsigned long long>( checkpoint.frameIndex ),
            checkpoint.eventCursor,
            static_cast<unsigned long long>( checkpoint.bodies.size() ),
            static_cast<unsigned long long>( checkpoint.solverHash ),
            static_cast<unsigned long long>( result.fileBytes ) );
    return SbResult::Success();
}
#endif

bool Run::RestoreReplayV2ArtifactTargetState( const char* path,
                                              ReplayFrameIndex requestedFrame,
                                              bool makeLiveBranch,
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
        LogReplayV2TargetRestoreDiagnostic( m_diagnosticsRuntime,
                                            SceneState(),
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

    ReplaySolverFrameSample liveBackup;
    bool hasLiveBackup = false;
    if ( const ReplaySolverFrameSample* latest = m_replayRuntime.Solver().LatestSample() )
    {
        liveBackup = *latest;
        hasLiveBackup = true;
    }
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
        if ( stateMutated && hasLiveBackup )
        {
            char fallbackReason[128] = {};
            fallbackRestored = ApplyReplaySolverSampleState( liveBackup, fallbackReason, sizeof( fallbackReason ) );
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
    ReplayRestoreOwnerContext restoreOwnerContext{ m_runtimeTools,
                                                   m_simulation,
                                                   m_sceneController,
                                                   SceneState(),
                                                   m_config,
                                                   m_systems,
                                                   m_cWorldEnvironment,
                                                   m_cGameModelCollection,
                                                   m_launchOptions.generatedObjectTypeOverride,
                                                   m_startup.gameModelCapacity };
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
             [this]( const ReplaySolverFrameSample& sample, char* reason, std::size_t reasonSize )
             { return ApplyReplaySolverSampleState( sample, reason, reasonSize ); } ) )
    {
        return failWithDiagnostic( checkpointReason, target, checkpoint );
    }
    stateMutated = true;

    auto captureCurrentReplaySolverHash = [this]( const ReplaySolverFrameSample& reference,
                                                  uint64_t& solverHash,
                                                  uint64_t& presentationHash,
                                                  std::size_t& bodyCount )
    { return CaptureCurrentReplaySolverHash( reference, solverHash, presentationHash, bodyCount ); };
    auto enterInteractiveSceneRun = [this]() { EnterInteractiveSceneRun(); };

    ReplayRestoreStepResult stepResult;
    ReplayRestoreStepFailure stepFailure;
    if ( !RunReplayRestoreTargetStep( restoreOwnerContext,
                                      artifact,
                                      *checkpoint,
                                      *target,
                                      stepResult,
                                      stepFailure,
                                      captureCurrentReplaySolverHash,
                                      enterInteractiveSceneRun ) )
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
    LogReplayRestoreTargetSuccess( m_diagnosticsRuntime,
                                   SceneState(),
                                   restoreSource,
                                   requestedFrame,
                                   LATEST_NON_CHECKPOINT_TARGET,
                                   *target,
                                   *checkpoint,
                                   targetHash );

    if ( makeLiveBranch )
    {
        ApplyReplayRestoreLiveBranch( m_replayRuntime,
                                      *checkpoint,
                                      *target,
                                      outResult,
                                      [this]() { ResetReplayTimelineForActiveScene( true ); } );
    }

    writeReason( "restored hash match" );
    return true;
}

#ifdef _DEBUG
SbResult Run::VerifyReplaySolverTargetFileProbe( const char* path )
{
    RunReplayV2TargetRestoreResult result;
    char reason[256] = {};
    if ( !RestoreReplayV2ArtifactTargetState( path,
                                              ( std::numeric_limits<ReplayFrameIndex>::max )(),
                                              false,
                                              result,
                                              reason,
                                              sizeof( reason ) ) )
    {
        return SbResult::Failure( REPLAY_PROBE_OWNER,
                                  "replay restore target probe failed: %s",
                                  reason[0] != '\0' ? reason : "unknown restore failure" );
    }

    printf( "[replay] Restore target probe passed: path=%s checkpoints=%llu events=%llu hashes=%llu "
            "checkpoint_frame=%llu target_frame=%llu event_cursor=%u events_applied=%llu bodies=%llu "
            "generated_topology_rebuilt=%d "
            "solver_hash=0x%016llX presentation_hash=0x%016llX bytes=%llu\n",
            path,
            static_cast<unsigned long long>( result.checkpointCount ),
            static_cast<unsigned long long>( result.eventCount ),
            static_cast<unsigned long long>( result.hashCount ),
            static_cast<unsigned long long>( result.checkpointFrame ),
            static_cast<unsigned long long>( result.targetFrame ),
            result.eventCursor,
            static_cast<unsigned long long>( result.eventsApplied ),
            static_cast<unsigned long long>( result.bodyCount ),
            result.generatedTopologyRebuilt ? 1 : 0,
            static_cast<unsigned long long>( result.solverHash ),
            static_cast<unsigned long long>( result.presentationHash ),
            static_cast<unsigned long long>( result.fileBytes ) );
    PostQuitMessage( 0 );
    return SbResult::Success();
}

SbResult Run::VerifyReplaySolverFailureFileProbe( const char* path )
{
    constexpr ReplayFrameIndex MISSING_TARGET_FRAME = 999999999u;
    RunReplayV2TargetRestoreResult result;
    char reason[256] = {};
    if ( RestoreReplayV2ArtifactTargetState( path, MISSING_TARGET_FRAME, false, result, reason, sizeof( reason ) ) )
    {
        return ReplayProbeFailure( "replay restore failure probe unexpectedly restored a missing target frame" );
    }
    if ( strstr( reason, "found no saved hash for requested target frame" ) == nullptr )
    {
        return SbResult::Failure( REPLAY_PROBE_OWNER,
                                  "replay restore failure probe produced an unexpected reason: %s",
                                  reason[0] != '\0' ? reason : "unknown restore failure" );
    }

    printf( "[replay] Restore failure probe passed: path=%s missing_frame=%llu reason=\"%s\"\n",
            path,
            static_cast<unsigned long long>( MISSING_TARGET_FRAME ),
            reason );
    return SbResult::Success();
}

SbResult Run::VerifyReplaySolverBranchFileProbe( const char* path )
{
    if ( !LoadReplayPresentationArtifact( path, true ) )
    {
        return ReplayProbeFailure( "replay restore branch probe failed to load v2 presentation scrub source" );
    }
    m_replayRuntime.Scrubber().historicalSamplePaused = true;
    m_replayRuntime.Scrubber().activeTrack = RunReplayTrack::Presentation;
    m_replayRuntime.SetTrackPosition( RunReplayTrack::Presentation, 1.0f );

    RunReplayV2TargetRestoreResult result;
    char reason[256] = {};
    if ( !RestoreReplayScrubberSelectionAsLive( m_timers.simulationTimer.GetTotalTime(),
                                                &result,
                                                reason,
                                                sizeof( reason ) ) )
    {
        return SbResult::Failure( REPLAY_PROBE_OWNER,
                                  "replay restore branch probe failed: %s",
                                  reason[0] != '\0' ? reason : "unknown restore failure" );
    }
    if ( !result.madeLiveBranch || result.branchId == 0 )
    {
        return ReplayProbeFailure( "replay restore branch probe did not create a scrubber live branch" );
    }

    printf( "[replay] Restore branch probe passed: path=%s checkpoints=%llu events=%llu hashes=%llu "
            "checkpoint_frame=%llu target_frame=%llu event_cursor=%u events_applied=%llu bodies=%llu "
            "generated_topology_rebuilt=%d "
            "branch_id=%u parent_branch_id=%u solver_hash=0x%016llX presentation_hash=0x%016llX bytes=%llu\n",
            path,
            static_cast<unsigned long long>( result.checkpointCount ),
            static_cast<unsigned long long>( result.eventCount ),
            static_cast<unsigned long long>( result.hashCount ),
            static_cast<unsigned long long>( result.checkpointFrame ),
            static_cast<unsigned long long>( result.targetFrame ),
            result.eventCursor,
            static_cast<unsigned long long>( result.eventsApplied ),
            static_cast<unsigned long long>( result.bodyCount ),
            result.generatedTopologyRebuilt ? 1 : 0,
            result.branchId,
            result.parentBranchId,
            static_cast<unsigned long long>( result.solverHash ),
            static_cast<unsigned long long>( result.presentationHash ),
            static_cast<unsigned long long>( result.fileBytes ) );
    return SbResult::Success();
}
#endif
