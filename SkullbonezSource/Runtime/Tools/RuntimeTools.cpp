/*
File: SkullbonezSource/Runtime/Tools/RuntimeTools.cpp
Purpose:
  Provides the runtime tool state ownership boundary.

Summary:
  Run routes input and passes borrowed world services here. RuntimeTools mutates
  only tool-owned state, launcher-created projectiles, and explicit physics
  targets selected by the current tool action.

Glossary:
  Launcher ray: Camera-centered tool ray used for laser impulses, projectile
    aim, and raycast visualization.

Invariants:
  - Raycast and laser histories are fixed-capacity replay state; preserve cursor
    wrap behavior and restore clamping.
  - Terrain/model hit tests pick the closest valid hit without changing world
    state.
  - Projectile creation must respect the active model capacity before adding to
    SceneWorld.
  - Launcher physics mutation enters PhysicsEngine through body handles: ray
    hits resolve model indices at the tool boundary, while spawned projectiles
    use the handle returned by creation.

Related:
  - SkullbonezSource/Runtime/Tools/RuntimeTools.h
  - SkullbonezSource/Runtime/Tools/LauncherTools.cpp
  - SkullbonezSource/Runtime/Replay/ReplayPresentation.h
  - Agentic/Reference/engine-glossary.md
*/
#include "RuntimeTools.h"
#include "../../Assets/AssetKeys.h"

#include "../../Core/Common.h"
#include "../../Core/Log.h"

#include "../Scene/SceneWorld.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/CollisionShape.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsEngine.h"
#include "../Interaction/OperatorUiCommands.h"
#include "../../UI/UILayout.h"
#include "../Camera/CameraCollection.h"
#include "../Input/InputRouter.h"
#include "../Interaction/RuntimeInteractionCommands.h"
#include "../Interaction/RuntimeInteractionController.h"
#include "../Replay/ReplayToolPackets.h"
#include "../Scene/SceneSessionState.h"
#include "../../World/Terrain.h"
#include "../../World/WorldEnvironment.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace SkullbonezCore::Runtime
{
void RuntimeTools::ObserveSceneLifecycle( const SceneLifecyclePacket& packet, InputRouter& inputRouter,
                                          RuntimeInteractionController& interaction )
{
    if ( !m_sceneLifecycleObserver.ShouldApply( packet, SceneRuntimeLifecycleEvent::AfterSceneCleared ) )
    {
        return;
    }

    // Invariant: clear typed gesture/capture state before the interaction owner
    // publishes its new-scene workspace policy.
    CancelMousePickup( inputRouter, interaction );
    ClearRayCastTestLines();
}


void RuntimeTools::CancelMousePickup( InputRouter& inputRouter, RuntimeInteractionController& interaction )
{
    // Invariant: the controller gesture is the sole capture fact. Clear native
    // presentation only when this owner actually held the typed pickup gesture.
    if ( interaction.Gesture().kind == RuntimeInteractionGestureKind::MousePickupDrag )
    {
        inputRouter.ReleaseNativeCapture();
        RuntimeGestureCommand command;
        command.action = RuntimeGestureCommandAction::End;
        command.gesture.kind = RuntimeInteractionGestureKind::MousePickupDrag;
        command.reason = InteractionExitReason::EndGesture;
        RuntimeGestureEvent event;
        (void)interaction.ApplyGestureCommand( command, event );
    }

    m_mousePickup = RunMousePickupState {};
}


namespace
{
constexpr float RAY_CAST_TEST_MAX_DISTANCE = 5000.0f;
constexpr float RAY_CAST_TEST_VISUAL_MISS_DISTANCE = 360.0f;
constexpr float LAUNCHER_PROJECTILE_RADIUS = 0.85f;
constexpr float LAUNCHER_PROJECTILE_MASS = 6.0f;
constexpr float LAUNCHER_PROJECTILE_RESTITUTION = 0.42f;
constexpr float LAUNCHER_PROJECTILE_SPAWN_LEAD = 3.2f;
constexpr float LAUNCHER_PROJECTILE_SPAWN_DOWN_OFFSET = 0.28f;

// Why: launcher tools borrow already-prepared physics stores from the Run owner.
// A count mismatch means the caller has not completed the cold topology repair
// edge, so tool code fails closed instead of rebuilding model-owned descriptors.
bool LauncherPhysicsStoresReady( const Physics::PhysicsEngine& physics, int modelCount )
{
    return SkullbonezCore::Physics::PhysicsEngine::ReadBodies( physics ).Count() == modelCount &&
           SkullbonezCore::Physics::PhysicsEngine::ReadColliders( physics ).Count() == modelCount;
}


// Why: launcher ray hits still identify targets by model index, but the physics
// mutation should run on the already-resolved body handle.
void ApplyLauncherPhysicsImpulse( Physics::PhysicsEngine& physics, Physics::PhysicsBodyHandle body,
                                  const Math::Vector::Vector3& impulse, const Math::Vector::Vector3& worldApplicationOffset )
{
    if ( !body.IsValid() )
    {
        return;
    }

    physics.ApplyBodyImpulse( body, impulse, worldApplicationOffset );
}


bool IntersectRaySphere( const Math::Vector::Vector3& rayOrigin, const Math::Vector::Vector3& rayDirection,
                         const Math::Vector::Vector3& center, float radius, float& outT )
{
    const Math::Vector::Vector3 m = rayOrigin - center;
    const float b = Dot( m, rayDirection );
    const float c = ( Dot( m, m ) ) - radius * radius;

    if ( c > 0.0f && b > 0.0f )
    {
        return false;
    }

    const float discriminant = b * b - c;

    if ( discriminant < 0.0f )
    {
        return false;
    }

    outT = -b - sqrtf( discriminant );

    if ( outT < 0.0f )
    {
        outT = 0.0f;
    }

    return true;
}
} // namespace

RunRayCastTestState& RuntimeTools::RayCastTest()
{
    return m_rayCastTest;
}


bool RuntimeTools::ApplyRayCastVisualizationUICommand( const UI::UIPhysicsCommands& commands )
{
    if ( !commands.toggleRayCastVisualization )
    {
        return false;
    }

    m_rayCastTest.visualizeRays = !m_rayCastTest.visualizeRays;
    return true;
}

RayCastLauncherTuningUICommandResult
RuntimeTools::ApplyRayCastLauncherTuningUICommands( const UI::UIPhysicsCommands& commands )
{
    RayCastLauncherTuningUICommandResult result;

    if ( commands.requestRayCastImpulseStrength )
    {
        const float previousImpulseStrength = m_rayCastTest.impulseStrength;
        m_rayCastTest.impulseStrength = std::clamp( commands.requestedRayCastImpulseStrength, UI::Layout::UI_RAY_IMPULSE_MIN,
                                                    UI::Layout::UI_RAY_IMPULSE_MAX );

        result.setImpulseStrength = true;
        result.impulseConfigChangedFlags = previousImpulseStrength != m_rayCastTest.impulseStrength ? 1u : 0u;
        result.impulseConfigImpulseStrength = m_rayCastTest.impulseStrength;
        result.impulseConfigProjectileSpeed = m_rayCastTest.projectileSpeed;
    }

    if ( commands.requestLauncherProjectileSpeed )
    {
        const float previousProjectileSpeed = m_rayCastTest.projectileSpeed;
        m_rayCastTest.projectileSpeed = std::clamp( commands.requestedLauncherProjectileSpeed,
                                                    UI::Layout::UI_LAUNCHER_PROJECTILE_SPEED_MIN,
                                                    UI::Layout::UI_LAUNCHER_PROJECTILE_SPEED_MAX );

        result.setProjectileSpeed = true;
        result.projectileConfigChangedFlags = previousProjectileSpeed != m_rayCastTest.projectileSpeed ? 2u : 0u;
        result.projectileConfigImpulseStrength = m_rayCastTest.impulseStrength;
        result.projectileConfigProjectileSpeed = m_rayCastTest.projectileSpeed;
    }

    return result;
}

void RuntimeTools::ClearRayCastTestLines()
{
    m_rayCastTest.lines = {};
    m_rayCastTest.nextLine = 0;
}

void RuntimeTools::AddRayCastTestLine( const Math::Vector::Vector3& start, const Math::Vector::Vector3& end, bool hit )
{
    if ( !m_rayCastTest.visualizeRays )
    {
        return;
    }

    // Invariant: The line history is intentionally bounded; replay restores the
    // same cursor so overwriting the oldest slot is observable tool state.
    RunRayCastTestLine&
        line = m_rayCastTest.lines[static_cast<std::size_t>( m_rayCastTest.nextLine ) % RunRayCastTestState::MAX_LINES];

    line.start = start;
    line.end = end;
    line.ageSeconds = 0.0f;
    line.active = true;
    line.hit = hit;
    m_rayCastTest.nextLine = ( m_rayCastTest.nextLine + 1 ) % static_cast<int>( RunRayCastTestState::MAX_LINES );
}

void RuntimeTools::TickRayCastTestLines( float dt )
{
    if ( dt <= 0.0f )
    {
        return;
    }

    for ( RunRayCastTestLine& line : m_rayCastTest.lines )
    {
        if ( line.active )
        {
            line.ageSeconds += dt;
        }
    }
}

bool RuntimeTools::HasLingeredRayCastLine( float maxAgeSeconds ) const
{
    if ( maxAgeSeconds <= 0.0f )
    {
        return false;
    }

    for ( const RunRayCastTestLine& line : m_rayCastTest.lines )
    {
        if ( line.active && line.ageSeconds < maxAgeSeconds )
        {
            return true;
        }
    }

    return false;
}

bool RuntimeTools::HasSelectionOverlayWork( const ToolEditorOverlayValues& editor, int modelCount,
                                            RunCameraMode cameraMode ) const
{
    const bool selectedModelValid = editor.selectionCount > 0 && modelCount >= 0;

    const bool placementPreview = editor.editorModeEnabled && editor.placementModeEnabled && editor.placementPreviewVisible;

    const bool editorSelection = editor.editorModeEnabled && !editor.placementModeEnabled && selectedModelValid;
    const bool inspectSelection = !editor.editorModeEnabled && cameraMode == RunCameraMode::Inspect && selectedModelValid;

    const bool attachSelection = !editor.editorModeEnabled && cameraMode == RunCameraMode::Attach && selectedModelValid;

    return placementPreview || editorSelection || inspectSelection || attachSelection;
}

bool RuntimeTools::HasMousePickupOverlayWork( const RuntimeInteractionGesture& gesture ) const
{
    return gesture.kind == RuntimeInteractionGestureKind::MousePickupDrag && m_mousePickup.body.IsValid();
}

bool RuntimeTools::HasLauncherShots() const
{
    return m_laser.HasActiveShots();
}

const char* RuntimeTools::LauncherFireModeLabel() const
{
    return m_rayCastTest.fireMode == RunLauncherFireMode::Projectile ? "PROJECTILE" : "LASER";
}

void RuntimeTools::BuildReplayLauncherVisualSample( ReplayLauncherVisualSample& outSample ) const
{
    // Concept: Replay captures visible launcher/tool feedback separately from
    // physics state so scrubbed frames can redraw rays and laser afterimages
    // without re-firing the tool.
    outSample.rayLines.clear();
    outSample.laserShots.clear();
    outSample.nextRayLine = m_rayCastTest.nextLine;
    outSample.fireMode = m_rayCastTest.fireMode == RunLauncherFireMode::Projectile ? ReplayLauncherFireMode::Projectile
                                                                                   : ReplayLauncherFireMode::Laser;

    outSample.visualizeRays = m_rayCastTest.visualizeRays;
    outSample.impulseStrength = m_rayCastTest.impulseStrength;
    outSample.projectileSpeed = m_rayCastTest.projectileSpeed;
    outSample.rayLines.reserve( RunRayCastTestState::MAX_LINES );

    for ( const RunRayCastTestLine& line : m_rayCastTest.lines )
    {
        ReplayRayCastLineSample sample;
        sample.start = line.start;
        sample.end = line.end;
        sample.ageSeconds = line.ageSeconds;
        sample.active = line.active;
        sample.hit = line.hit;
        outSample.rayLines.push_back( sample );
    }

    m_laser.CaptureShots( outSample.laserShots, outSample.nextLaserShot );
}

void RuntimeTools::RestoreReplayLauncherVisualSample( const ReplayLauncherVisualSample& sample )
{
    m_rayCastTest.lines = {};
    const std::size_t lineCount = (std::min)( sample.rayLines.size(), RunRayCastTestState::MAX_LINES );

    for ( std::size_t i = 0; i < lineCount; ++i )
    {
        RunRayCastTestLine& line = m_rayCastTest.lines[i];
        line.start = sample.rayLines[i].start;
        line.end = sample.rayLines[i].end;
        line.ageSeconds = sample.rayLines[i].ageSeconds;
        line.active = sample.rayLines[i].active;
        line.hit = sample.rayLines[i].hit;
    }

    // Invariant: Older or malformed replay samples may carry out-of-range
    // cursors; clamp by modulo instead of trusting serialized state.
    m_rayCastTest.nextLine = sample.nextRayLine % static_cast<int>( RunRayCastTestState::MAX_LINES );

    if ( m_rayCastTest.nextLine < 0 )
    {
        m_rayCastTest.nextLine += static_cast<int>( RunRayCastTestState::MAX_LINES );
    }

    m_rayCastTest.fireMode = sample.fireMode == ReplayLauncherFireMode::Projectile ? RunLauncherFireMode::Projectile
                                                                                   : RunLauncherFireMode::Laser;

    m_rayCastTest.visualizeRays = sample.visualizeRays;
    m_rayCastTest.impulseStrength = sample.impulseStrength;
    m_rayCastTest.projectileSpeed = sample.projectileSpeed;
    m_laser.RestoreShots( sample.laserShots, sample.nextLaserShot );
}

bool RuntimeTools::TryRayCastTestHit( const Physics::PhysicsBodyStore& bodyStore,
                                      const Physics::ColliderStore& colliderStore, const Math::Vector::Vector3& rayOrigin,
                                      const Math::Vector::Vector3& rayDirection, float maxDistance, int& outIndex,
                                      float& outT ) const
{
    outIndex = -1;
    outT = maxDistance;

    // Concept: launcher body picking uses collider bounding spheres, not exact
    // mesh intersections. The broad deterministic hit result is enough for
    // impulse placement and visual feedback, and it stays on store records.
    const int hitCount = (std::min)( bodyStore.Count(), colliderStore.Count() );
    const auto hotFields = bodyStore.HotFields();
    const auto colliders = colliderStore.Records();

    for ( int i = 0; i < hitCount; ++i )
    {
        const std::size_t index = static_cast<std::size_t>( i );
        const Physics::ColliderRecord& collider = colliders[index];
        const float radius = (std::max)( collider.boundingRadius, 1.0f );
        float rayT = 0.0f;

        if ( IntersectRaySphere( rayOrigin, rayDirection, Physics::PhysicsBodyPosition( hotFields, index ), radius, rayT ) &&
             rayT <= maxDistance && rayT < outT )
        {
            outIndex = i;
            outT = rayT;
        }
    }

    return outIndex >= 0;
}

bool RuntimeTools::TryLauncherTerrainHit( Geometry::Terrain* terrain, const Math::Vector::Vector3& rayOrigin,
                                          const Math::Vector::Vector3& rayDirection, float maxDistance, float& outT ) const
{
    outT = maxDistance;

    if ( !terrain )
    {
        return false;
    }

    constexpr int RAY_STEPS = 192;
    bool hasPrevious = false;
    float previousT = 0.0f;
    float previousDiff = 0.0f;

    // Why: Terrain has no dedicated runtime ray query here, so the launcher
    // walks the ray and bisects the first terrain crossing to keep laser and
    // projectile aim consistent.
    for ( int step = 0; step <= RAY_STEPS; ++step )
    {
        const float t = maxDistance * static_cast<float>( step ) / static_cast<float>( RAY_STEPS );
        const Math::Vector::Vector3 sample = rayOrigin + rayDirection * t;

        if ( !terrain->IsInBounds( sample.x, sample.z ) )
        {
            continue;
        }

        const float terrainY = terrain->GetTerrainHeightAt( sample.x, sample.z );
        const float diff = sample.y - terrainY;

        if ( fabsf( diff ) <= 0.01f )
        {
            outT = t;
            return true;
        }

        if ( hasPrevious && previousDiff > 0.0f && diff <= 0.0f )
        {
            float lowT = previousT;
            float highT = t;

            for ( int refine = 0; refine < 12; ++refine )
            {
                const float midT = ( lowT + highT ) * 0.5f;
                const Math::Vector::Vector3 mid = rayOrigin + rayDirection * midT;

                if ( !terrain->IsInBounds( mid.x, mid.z ) )
                {
                    lowT = midT;
                    continue;
                }

                const float midTerrainY = terrain->GetTerrainHeightAt( mid.x, mid.z );
                const float midDiff = mid.y - midTerrainY;

                if ( midDiff > 0.0f )
                {
                    lowT = midT;
                }
                else
                {
                    highT = midT;
                }
            }

            outT = highT;
            return true;
        }

        hasPrevious = true;
        previousT = t;
        previousDiff = diff;
    }

    return false;
}

bool RuntimeTools::TryBuildLauncherCameraRay( Environment::CameraCollection* cameras, Math::Vector::Vector3& outOrigin,
                                              Math::Vector::Vector3& outDirection, Math::Vector::Vector3& outCameraUp ) const
{
    if ( !cameras )
    {
        return false;
    }

    outOrigin = cameras->GetCameraTranslation();
    outDirection = cameras->GetCameraView() - outOrigin;
    const float dirLenSq = Math::Vector::VectorMagSquared( outDirection );

    if ( dirLenSq <= TOLERANCE * TOLERANCE )
    {
        return false;
    }

    outDirection = outDirection * ( 1.0f / sqrtf( dirLenSq ) );
    outCameraUp = cameras->GetCameraUp();
    return true;
}

bool RuntimeTools::FireLauncherRay( SceneWorld& world, SceneSessionState& scene, int activeModelCapacity,
                                    const Math::Vector::Vector3& rayOrigin, const Math::Vector::Vector3& rayDirection,
                                    const Math::Vector::Vector3& cameraUp )
{
    Physics::PhysicsEngine& physics = world.Physics();
    Geometry::Terrain* terrain = world.Terrain().Get();
    const int modelCount = world.SceneEntityCount();

    if ( !LauncherPhysicsStoresReady( physics, modelCount ) )
    {
        return false;
    }

    if ( m_rayCastTest.fireMode == RunLauncherFireMode::Projectile )
    {
        return FireLauncherProjectile( world, scene, activeModelCapacity, modelCount, rayOrigin, rayDirection, cameraUp );
    }

    FireLauncherLaser( physics, modelCount, terrain, rayOrigin, rayDirection, cameraUp );
    return false;
}


LauncherPointerResult RuntimeTools::RouteLauncherPointer( const LauncherPointerInput& input, SceneWorld& world,
                                                          SceneSessionState& scene )
{
    LauncherPointerResult result;

    if ( !input.launcherMode || !input.leftPressed || input.suppressWorldAction || input.uiWantsNativeCursor )
    {
        return result;
    }

    result.consumed = true;
    result.enteredInteractive = true;

    Math::Vector::Vector3 rayOrigin;
    Math::Vector::Vector3 rayDirection;
    Math::Vector::Vector3 cameraUp;

    if ( !TryBuildLauncherCameraRay( &world.Cameras(), rayOrigin, rayDirection, cameraUp ) )
    {
        return result;
    }

    const int modelCountBefore = world.SceneEntityCount();
    result.replayEvent = ReplayEventCommandOperations::BuildLauncherFire( rayOrigin, rayDirection, cameraUp,
                                                                          m_rayCastTest.fireMode ==
                                                                              RunLauncherFireMode::Projectile,
                                                                          m_rayCastTest.impulseStrength,
                                                                          m_rayCastTest.projectileSpeed, modelCountBefore );

    result.recordReplayEvent = true;

    // Why: the launcher is a cold input action, so it repairs any construction-
    // time world/store drift before entering handle-based physics queries.
    if ( world.RepairPhysicsBodyAndColliderTopology() &&
         FireLauncherRay( world, scene, input.activeModelCapacity, rayOrigin, rayDirection, cameraUp ) )
    {
        scene.modelCount = world.SceneEntityCount();
    }

    return result;
}

void RuntimeTools::FireLauncherLaser( Physics::PhysicsEngine& physics, int modelCount, Geometry::Terrain* terrain,
                                      const Math::Vector::Vector3& rayOrigin, const Math::Vector::Vector3& rayDirection,
                                      const Math::Vector::Vector3& cameraUp )
{
    int modelHitIndex = -1;
    float modelHitT = RAY_CAST_TEST_MAX_DISTANCE;
    const bool modelHit = TryRayCastTestHit( SkullbonezCore::Physics::PhysicsEngine::ReadBodies( physics ),
                                             SkullbonezCore::Physics::PhysicsEngine::ReadColliders( physics ), rayOrigin,
                                             rayDirection, RAY_CAST_TEST_MAX_DISTANCE, modelHitIndex, modelHitT );

    float terrainHitT = RAY_CAST_TEST_MAX_DISTANCE;
    const bool terrainHit = TryLauncherTerrainHit( terrain, rayOrigin, rayDirection, RAY_CAST_TEST_MAX_DISTANCE,
                                                   terrainHitT );

    const bool terrainIsClosest = terrainHit && ( !modelHit || terrainHitT < modelHitT );
    const bool hit = modelHit || terrainHit;
    const float hitT = terrainIsClosest ? terrainHitT : ( modelHit ? modelHitT : RAY_CAST_TEST_VISUAL_MISS_DISTANCE );
    const Math::Vector::Vector3 visualEnd = rayOrigin + rayDirection * hitT;

    // Concept: The laser always records visual feedback; only a closest model
    // hit below turns into a physics impulse.
    m_laser.Fire( rayOrigin, rayDirection, cameraUp, hitT, hit );
    AddRayCastTestLine( rayOrigin, visualEnd, hit );

    if ( terrainIsClosest || !modelHit || modelHitIndex < 0 || modelHitIndex >= modelCount )
    {
        return;
    }

    const Physics::PhysicsBodyStore& bodyStore = SkullbonezCore::Physics::PhysicsEngine::ReadBodies( physics );
    const Physics::PhysicsBodyHandle body = bodyStore.HandleForModelIndex( modelHitIndex );
    const Physics::PhysicsBodyRecord* bodyRecord = bodyStore.RecordForHandle( body );

    if ( !bodyRecord )
    {
        return;
    }

    const Math::Vector::Vector3 hitPoint = rayOrigin + rayDirection * hitT;
    const Math::Vector::Vector3 worldApplicationOffset = hitPoint - Physics::PhysicsBodyPosition( bodyStore.HotFields(),
                                                                                                  static_cast<std::size_t>( modelHitIndex ) );

    const float mass = (std::max)( 0.001f, bodyRecord->mass );
    const float releaseSpeed = std::clamp( m_rayCastTest.impulseStrength / mass, 1.5f, 36.0f );

    if ( !physics.ReleaseFixedBodyAndAttachedTreeParts( body, m_rayCastTest.impulseStrength, rayDirection * releaseSpeed,
                                                        Math::Vector::ZERO_VECTOR ) )
    {
        return;
    }

    ApplyLauncherPhysicsImpulse( physics, body, rayDirection * m_rayCastTest.impulseStrength, worldApplicationOffset );
}

bool RuntimeTools::FireLauncherProjectile( SceneWorld& world, SceneSessionState& scene, int activeModelCapacity,
                                           int modelCount, const Math::Vector::Vector3& rayOrigin,
                                           const Math::Vector::Vector3& rayDirection, const Math::Vector::Vector3& cameraUp )
{
    Physics::PhysicsEngine& physics = world.Physics();
    Geometry::Terrain* terrain = world.Terrain().Get();

    if ( !terrain || modelCount >= activeModelCapacity )
    {
        return false;
    }

    int modelHitIndex = -1;
    float modelHitT = RAY_CAST_TEST_MAX_DISTANCE;
    const bool modelHit = TryRayCastTestHit( SkullbonezCore::Physics::PhysicsEngine::ReadBodies( physics ),
                                             SkullbonezCore::Physics::PhysicsEngine::ReadColliders( physics ), rayOrigin,
                                             rayDirection, RAY_CAST_TEST_MAX_DISTANCE, modelHitIndex, modelHitT );

    float terrainHitT = RAY_CAST_TEST_MAX_DISTANCE;
    const bool terrainHit = TryLauncherTerrainHit( terrain, rayOrigin, rayDirection, RAY_CAST_TEST_MAX_DISTANCE,
                                                   terrainHitT );

    const float hitT = terrainHit && ( !modelHit || terrainHitT < modelHitT )
                           ? terrainHitT
                           : ( modelHit ? modelHitT : RAY_CAST_TEST_VISUAL_MISS_DISTANCE );

    const Math::Vector::Vector3 aimPoint = rayOrigin + rayDirection * hitT;

    // Concept: Projectiles spawn slightly in front of and below the camera,
    // then aim at the same closest model/terrain point that the laser would
    // visualize.
    Math::Vector::Vector3 up = cameraUp;
    const float upLenSq = Math::Vector::VectorMagSquared( up );
    up = upLenSq > TOLERANCE * TOLERANCE ? up * ( 1.0f / sqrtf( upLenSq ) ) : Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
    const Math::Vector::Vector3 spawn = rayOrigin + rayDirection * LAUNCHER_PROJECTILE_SPAWN_LEAD -
                                        up * LAUNCHER_PROJECTILE_SPAWN_DOWN_OFFSET;

    Math::Vector::Vector3 velocityDir = aimPoint - spawn;
    const float velocityDirLenSq = Math::Vector::VectorMagSquared( velocityDir );

    if ( velocityDirLenSq <= TOLERANCE * TOLERANCE )
    {
        velocityDir = rayDirection;
    }
    else
    {
        velocityDir = velocityDir * ( 1.0f / sqrtf( velocityDirLenSq ) );
    }

    const float moment = 0.4f * LAUNCHER_PROJECTILE_MASS * LAUNCHER_PROJECTILE_RADIUS * LAUNCHER_PROJECTILE_RADIUS;
    SceneEntityCreateDesc projectile;
    projectile.SetRenderTint( 0.72f, 0.88f, 1.0f, 1.0f );
    projectile.SetName( "launcher_projectile" );

    const Math::CollisionDetection::BoundingSphere projectileShape( LAUNCHER_PROJECTILE_RADIUS,
                                                                    Math::Vector::Vector3( 0.0f, 0.0f, 0.0f ) );
    const Physics::PhysicsSceneObjectId sceneObjectId = scene.AllocateSceneObjectId();
    projectile.sceneObjectId = sceneObjectId;
    const auto
        appendResult = world.TryCreateSceneEntity( std::move( projectile ),
                                                   Physics::
                                                       MakePhysicsBodyCreateDesc( sceneObjectId, projectileShape, spawn,
                                                                                  Math::Orientation::IDENTITY_QUATERNION,
                                                                                  velocityDir *
                                                                                      m_rayCastTest.projectileSpeed,
                                                                                  Math::Vector::Vector3( 0.0f, 0.0f, 0.0f ),
                                                                                  Math::Vector::Vector3( moment, moment,
                                                                                                         moment ),
                                                                                  LAUNCHER_PROJECTILE_MASS,
                                                                                  LAUNCHER_PROJECTILE_RESTITUTION,
                                                                                  Physics::PhysicsBodyMotionKind::Dynamic,
                                                                                  "launcher_projectile" ),
                                                   Physics::MakeColliderCreateDesc( projectileShape,
                                                                                    LAUNCHER_PROJECTILE_RESTITUTION,
                                                                                    HashStr( "default" ) ) );

    if ( !appendResult.status.Ok() )
    {
        fprintf( stderr, "[runtime-tools] launcher projectile creation failed: %s\n", appendResult.status.ErrorMessage() );

        return false;
    }

    const Physics::PhysicsBodyHandle projectileBody = appendResult.body;

    if ( projectileBody.IsValid() )
    {
        physics.WakeBody( projectileBody );
    }

    return true;
}

LauncherLaser& RuntimeTools::Laser()
{
    return m_laser;
}


const LauncherLaser& RuntimeTools::Laser() const
{
    return m_laser;
}


RunMousePickupState& RuntimeTools::MousePickup()
{
    return m_mousePickup;
}


void RuntimeTools::PrepareOverlayTrace( SceneWorld& world, const ToolEditorOverlayValues& editor,
                                        const ToolOverlayBuildInput& input )
{
    m_editorTracer.Clear();
    const float rayLinger = (std::max)( 0.0f, input.rayLingerSeconds );
    for ( const RunRayCastTestLine& line : m_rayCastTest.lines )
    {
        if ( line.active && line.ageSeconds < rayLinger )
        {
            m_editorTracer.AddRayCastTestLine( line.start, line.end, 1.0f - line.ageSeconds / rayLinger, line.hit );
        }
    }

    if ( editor.editorModeEnabled && editor.placementModeEnabled && editor.placementPreviewVisible )
    {
        m_editorTracer.AddPlacementRay( editor.placementRayOrigin, editor.placementRayHit );
    }

    const Physics::PhysicsBodyStore& bodyStore = world.BodyStore();
    const Physics::ColliderStore& colliderStore = world.Colliders();
    if ( ( editor.editorModeEnabled || input.inspectGizmoActive ) && !editor.placementModeEnabled &&
         editor.selectionCount > 0 )
    {
        const auto hotFields = bodyStore.HotFields();
        Math::Vector::Vector3 origin = Math::Vector::ZERO_VECTOR;
        std::array<const Physics::ColliderRecord*, ToolEditorOverlayValues::SELECTION_CAPACITY> colliders = {};
        std::size_t count = 0;
        for ( ; count < editor.selectionCount && count < colliders.size(); ++count )
        {
            const Physics::PhysicsBodyRecord* body = bodyStore.RecordForHandle( editor.selectionBodies[count] );
            const Physics::ColliderRecord* collider = colliderStore.RecordForHandle( editor.selectionColliders[count] );
            const int modelIndex = bodyStore.ModelIndexForHandle( editor.selectionBodies[count] );
            if ( !body || !collider || modelIndex < 0 || modelIndex >= world.SceneEntityCount() ||
                 collider->body != editor.selectionBodies[count] )
            {
                count = 0;
                break;
            }
            colliders[count] = collider;
            origin += Physics::PhysicsBodyPosition( hotFields, static_cast<std::size_t>( modelIndex ) );
        }

        if ( count > 0 )
        {
            origin /= static_cast<float>( count );
            float radius = 1.0f;
            for ( std::size_t i = 0; i < count; ++i )
            {
                const int modelIndex = bodyStore.ModelIndexForHandle( editor.selectionBodies[i] );
                const Math::Vector::Vector3 position =
                    Physics::PhysicsBodyPosition( hotFields, static_cast<std::size_t>( modelIndex ) );
                const Physics::ColliderRecord& collider = *colliders[i];
                const float colliderRadius =
                    (std::max)( collider.boundingRadius > 0.0f ? collider.boundingRadius
                                                              : Math::CollisionDetection::GetShapeBoundingRadius( collider.shape ),
                                1.0f );
                radius = (std::max)( radius, Math::Vector::Distance( position, origin ) + colliderRadius );
                m_editorTracer.AddSelectionOutline(
                    position, Physics::PhysicsBodyOrientation( hotFields, static_cast<std::size_t>( modelIndex ) ),
                    collider.shape );
            }
            const bool dragActive = input.gesture.kind == RuntimeInteractionGestureKind::GizmoDrag;
            const bool scaleActive = dragActive && input.gesture.gizmoKind == RuntimeGizmoDragKind::Scale;
            const bool rotateActive = dragActive && input.gesture.gizmoKind == RuntimeGizmoDragKind::Rotate;
            m_editorTracer.AddGizmo( origin, radius, editor.hotGizmoAxis, editor.hotRotationAxis,
                                     dragActive ? input.gesture.axis : -1, rotateActive,
                                     scaleActive || input.scaleMode, scaleActive );
        }
    }

    if ( input.gesture.kind == RuntimeInteractionGestureKind::MousePickupDrag && m_mousePickup.body.IsValid() )
    {
        const Physics::PhysicsBodyRecord* body = bodyStore.RecordForHandle( m_mousePickup.body );
        const Physics::PhysicsColliderHandle colliderHandle = colliderStore.HandleForBodyHandle( m_mousePickup.body );
        const Physics::ColliderRecord* collider = colliderStore.RecordForHandle( colliderHandle );
        const int modelIndex = bodyStore.ModelIndexForHandle( m_mousePickup.body );
        if ( body && collider && modelIndex >= 0 && modelIndex < world.SceneEntityCount() &&
             collider->body == m_mousePickup.body )
        {
            const auto hotFields = bodyStore.HotFields();
            const std::size_t bodyIndex = static_cast<std::size_t>( modelIndex );
            const Math::Vector::Vector3 bodyPosition = Physics::PhysicsBodyPosition( hotFields, bodyIndex );
            const Math::Vector::Vector3 grabPoint = bodyPosition + m_mousePickup.grabOffset;
            m_editorTracer.AddSelectionOutline( bodyPosition, Physics::PhysicsBodyOrientation( hotFields, bodyIndex ),
                                                collider->shape );
            m_editorTracer.AddReplayPathSegment( grabPoint, m_mousePickup.targetPoint, 0.1f, 0.95f, 1.0f );
            m_editorTracer.AddReplayContactMarker( m_mousePickup.targetPoint, m_mousePickup.planeNormal, 0.1f, 0.95f,
                                                   1.0f );
            m_editorTracer.AddReplayImpulseVector( grabPoint, m_mousePickup.lastImpulse, 0.1f, 0.95f, 1.0f );
        }
    }

    if ( input.attachedCameraTargetIndex >= 0 && input.attachedCameraTargetIndex < world.SceneEntityCount() )
    {
        const Physics::PhysicsBodyHandle bodyHandle = bodyStore.HandleForModelIndex( input.attachedCameraTargetIndex );
        const Physics::PhysicsBodyRecord* body = bodyStore.RecordForHandle( bodyHandle );
        const Physics::PhysicsColliderHandle colliderHandle = colliderStore.HandleForBodyHandle( bodyHandle );
        const Physics::ColliderRecord* collider = colliderStore.RecordForHandle( colliderHandle );
        if ( body && collider && collider->body == bodyHandle )
        {
            const auto hotFields = bodyStore.HotFields();
            const std::size_t bodyIndex = static_cast<std::size_t>( input.attachedCameraTargetIndex );
            const float radius = (std::max)( collider->boundingRadius, 1.0f ) * 1.24f;
            m_editorTracer.AddAttachedCameraTargetMarker( Physics::PhysicsBodyPosition( hotFields, bodyIndex ),
                                                          Physics::PhysicsBodyOrientation( hotFields, bodyIndex ),
                                                          collider->shape, radius, input.attachedCameraActiveFollow );
        }
    }
}


EditorTracer& RuntimeTools::Tracer()
{
    return m_editorTracer;
}


} // namespace SkullbonezCore::Runtime
