/*
File: SkullbonezSource/Runtime/Tools/RuntimeTools.cpp
Purpose:
  Provides the runtime tool state ownership boundary.

Mental model:
  Run routes input and passes borrowed world services here. RuntimeTools mutates
  only tool-owned state, launcher-created projectiles, and explicit physics
  targets selected by the current tool action.

Glossary:
  Launcher ray: Camera-centered tool ray used for laser impulses, projectile
    aim, and raycast visualization.
  Replay visual sample: Snapshot of launcher lines and laser shots used by
    replay scrubbing.
  Contact release: Rule that lets selected fixed authored props become dynamic
    after a strong enough launcher impulse.

Invariants:
  - Raycast and laser histories are fixed-capacity replay state; preserve cursor
    wrap behavior and restore clamping.
  - Terrain/model hit tests pick the closest valid hit without changing world
    state.
  - Projectile creation must respect the active model capacity before adding to
    GameModelCollection.

Related:
  - SkullbonezSource/Runtime/Tools/RuntimeTools.h
  - SkullbonezSource/Runtime/Editor/LauncherTools.cpp
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp
*/
#include "RuntimeTools.h"

#include "../../GameObjects/GameModel.h"
#include "../../GameObjects/GameModelCollection.h"
#include "../../Physics/CollisionShape.h"
#include "../CameraCollection.h"
#include "../Replay/ReplayRecorder.h"
#include "../../World/Terrain.h"
#include "../../World/WorldEnvironment.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace SkullbonezCore::Basics
{
namespace
{
constexpr float RAY_CAST_TEST_MAX_DISTANCE = 5000.0f;
constexpr float RAY_CAST_TEST_VISUAL_MISS_DISTANCE = 360.0f;
constexpr float LAUNCHER_PROJECTILE_RADIUS = 0.85f;
constexpr float LAUNCHER_PROJECTILE_MASS = 6.0f;
constexpr float LAUNCHER_PROJECTILE_RESTITUTION = 0.42f;
constexpr float LAUNCHER_PROJECTILE_SPAWN_LEAD = 3.2f;
constexpr float LAUNCHER_PROJECTILE_SPAWN_DOWN_OFFSET = 0.28f;

float LauncherModelRadius( const GameObjects::GameModel& model )
{
    return (std::max)( Math::CollisionDetection::GetShapeBoundingRadius( model.GetCollisionShape() ), 1.0f );
}

bool IntersectRaySphere( const Math::Vector::Vector3& rayOrigin,
                         const Math::Vector::Vector3& rayDirection,
                         const Math::Vector::Vector3& center,
                         float radius,
                         float& outT )
{
    const Math::Vector::Vector3 m = rayOrigin - center;
    const float b = m * rayDirection;
    const float c = ( m * m ) - radius * radius;
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

const RunRayCastTestState& RuntimeTools::RayCastTest() const
{
    return m_rayCastTest;
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
    RunRayCastTestLine& line =
        m_rayCastTest.lines[static_cast<std::size_t>( m_rayCastTest.nextLine ) % RunRayCastTestState::MAX_LINES];
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

bool RuntimeTools::HasSelectionOverlayWork( int modelCount, RunCameraMode cameraMode ) const
{
    const bool selectedModelValid = m_editor.selectedModelIndex >= 0 && m_editor.selectedModelIndex < modelCount;
    const bool placementPreview =
        m_editor.editorModeEnabled && m_editor.placementModeEnabled && m_editor.placementPreviewVisible;
    const bool editorSelection = m_editor.editorModeEnabled && !m_editor.placementModeEnabled && selectedModelValid;
    const bool inspectSelection =
        !m_editor.editorModeEnabled && cameraMode == RunCameraMode::Inspect && selectedModelValid;
    const bool attachSelection =
        !m_editor.editorModeEnabled && cameraMode == RunCameraMode::Attach && selectedModelValid;
    return placementPreview || editorSelection || inspectSelection || attachSelection;
}

bool RuntimeTools::HasMousePickupOverlayWork( int modelCount ) const
{
    return m_mousePickup.active && m_mousePickup.modelIndex >= 0 && m_mousePickup.modelIndex < modelCount;
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
    outSample = ReplayLauncherVisualSample();
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

bool RuntimeTools::TryRayCastTestHit( const std::vector<GameObjects::GameModel>& models,
                                      const Math::Vector::Vector3& rayOrigin,
                                      const Math::Vector::Vector3& rayDirection,
                                      float maxDistance,
                                      int& outIndex,
                                      float& outT ) const
{
    outIndex = -1;
    outT = maxDistance;

    // Concept: Launcher model picking uses bounding spheres, not exact mesh
    // intersections. It is the broad, deterministic tool hit result used for
    // impulses and visual feedback.
    for ( int i = 0; i < static_cast<int>( models.size() ); ++i )
    {
        const GameObjects::GameModel& model = models[static_cast<size_t>( i )];
        const float radius = LauncherModelRadius( model );
        float rayT = 0.0f;
        if ( IntersectRaySphere( rayOrigin, rayDirection, model.GetPosition(), radius, rayT ) && rayT <= maxDistance &&
             rayT < outT )
        {
            outIndex = i;
            outT = rayT;
        }
    }

    return outIndex >= 0;
}

bool RuntimeTools::TryLauncherTerrainHit( Geometry::Terrain* terrain,
                                          const Math::Vector::Vector3& rayOrigin,
                                          const Math::Vector::Vector3& rayDirection,
                                          float maxDistance,
                                          float& outT ) const
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

bool RuntimeTools::TryBuildLauncherCameraRay( Environment::CameraCollection* cameras,
                                              Math::Vector::Vector3& outOrigin,
                                              Math::Vector::Vector3& outDirection,
                                              Math::Vector::Vector3& outCameraUp ) const
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

bool RuntimeTools::FireLauncherRay( GameObjects::GameModelCollection& collection,
                                    Environment::WorldEnvironment& world,
                                    Geometry::Terrain* terrain,
                                    int activeModelCapacity,
                                    const Math::Vector::Vector3& rayOrigin,
                                    const Math::Vector::Vector3& rayDirection,
                                    const Math::Vector::Vector3& cameraUp )
{
    if ( m_rayCastTest.fireMode == RunLauncherFireMode::Projectile )
    {
        return FireLauncherProjectile( collection,
                                       world,
                                       terrain,
                                       activeModelCapacity,
                                       rayOrigin,
                                       rayDirection,
                                       cameraUp );
    }

    FireLauncherLaser( collection, terrain, rayOrigin, rayDirection, cameraUp );
    return false;
}

void RuntimeTools::FireLauncherLaser( GameObjects::GameModelCollection& collection,
                                      Geometry::Terrain* terrain,
                                      const Math::Vector::Vector3& rayOrigin,
                                      const Math::Vector::Vector3& rayDirection,
                                      const Math::Vector::Vector3& cameraUp )
{
    int modelHitIndex = -1;
    float modelHitT = RAY_CAST_TEST_MAX_DISTANCE;
    const bool modelHit = TryRayCastTestHit( collection.Models(),
                                             rayOrigin,
                                             rayDirection,
                                             RAY_CAST_TEST_MAX_DISTANCE,
                                             modelHitIndex,
                                             modelHitT );

    float terrainHitT = RAY_CAST_TEST_MAX_DISTANCE;
    const bool terrainHit =
        TryLauncherTerrainHit( terrain, rayOrigin, rayDirection, RAY_CAST_TEST_MAX_DISTANCE, terrainHitT );

    const bool terrainIsClosest = terrainHit && ( !modelHit || terrainHitT < modelHitT );
    const bool hit = modelHit || terrainHit;
    const float hitT = terrainIsClosest ? terrainHitT : ( modelHit ? modelHitT : RAY_CAST_TEST_VISUAL_MISS_DISTANCE );
    const Math::Vector::Vector3 visualEnd = rayOrigin + rayDirection * hitT;
    // Concept: The laser always records visual feedback; only a closest model
    // hit below turns into a physics impulse.
    m_laser.Fire( rayOrigin, rayDirection, cameraUp, hitT, hit );
    AddRayCastTestLine( rayOrigin, visualEnd, hit );

    if ( terrainIsClosest || !modelHit || modelHitIndex < 0 || modelHitIndex >= collection.GetModelCount() )
    {
        return;
    }

    GameObjects::GameModel& model = collection.GetModelAtIndex( modelHitIndex );
    if ( model.IsFixed() )
    {
        // Hazard: Fixed authored props are released only through their contact
        // release policy; bypassing this gate would make decorative assets
        // unexpectedly dynamic.
        if ( !model.ReleasesFromFixedOnContact() ||
             m_rayCastTest.impulseStrength < model.GetContactReleaseImpulseThreshold() )
        {
            return;
        }
        model.SetFixed( false );
    }

    const Math::Vector::Vector3 hitPoint = rayOrigin + rayDirection * hitT;
    collection.GetPhysicsEngine().ApplyBodyImpulse( collection,
                                                    modelHitIndex,
                                                    rayDirection * m_rayCastTest.impulseStrength,
                                                    hitPoint - model.GetPosition() );
    const float mass = (std::max)( 0.001f, model.GetMass() );
    const float releaseSpeed = std::clamp( m_rayCastTest.impulseStrength / mass, 1.5f, 36.0f );
    collection.ReleaseAttachedFixedTreeParts( modelHitIndex, rayDirection * releaseSpeed, Math::Vector::ZERO_VECTOR );
}

bool RuntimeTools::FireLauncherProjectile( GameObjects::GameModelCollection& collection,
                                           Environment::WorldEnvironment& world,
                                           Geometry::Terrain* terrain,
                                           int activeModelCapacity,
                                           const Math::Vector::Vector3& rayOrigin,
                                           const Math::Vector::Vector3& rayDirection,
                                           const Math::Vector::Vector3& cameraUp )
{
    if ( !terrain || collection.GetModelCount() >= activeModelCapacity )
    {
        return false;
    }

    int modelHitIndex = -1;
    float modelHitT = RAY_CAST_TEST_MAX_DISTANCE;
    const bool modelHit = TryRayCastTestHit( collection.Models(),
                                             rayOrigin,
                                             rayDirection,
                                             RAY_CAST_TEST_MAX_DISTANCE,
                                             modelHitIndex,
                                             modelHitT );

    float terrainHitT = RAY_CAST_TEST_MAX_DISTANCE;
    const bool terrainHit =
        TryLauncherTerrainHit( terrain, rayOrigin, rayDirection, RAY_CAST_TEST_MAX_DISTANCE, terrainHitT );

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
    const Math::Vector::Vector3 spawn =
        rayOrigin + rayDirection * LAUNCHER_PROJECTILE_SPAWN_LEAD - up * LAUNCHER_PROJECTILE_SPAWN_DOWN_OFFSET;
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
    GameObjects::GameModel projectile( &world,
                                       spawn,
                                       Math::Vector::Vector3( moment, moment, moment ),
                                       LAUNCHER_PROJECTILE_MASS );
    projectile.SetTerrain( terrain );
    projectile.SetCoefficientRestitution( LAUNCHER_PROJECTILE_RESTITUTION );
    projectile.AddBoundingSphere( LAUNCHER_PROJECTILE_RADIUS );
    projectile.SetLinearVelocity( velocityDir * m_rayCastTest.projectileSpeed );
    projectile.SetRenderTint( 0.72f, 0.88f, 1.0f, 1.0f );
    projectile.SetName( "launcher_projectile" );

    const int projectileIndex = collection.GetModelCount();
    collection.AddGameModel( std::move( projectile ) );
    collection.GetPhysicsEngine().WakeBody( collection, projectileIndex );
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

const RunMousePickupState& RuntimeTools::MousePickup() const
{
    return m_mousePickup;
}

RunEditorPlacementState& RuntimeTools::Editor()
{
    return m_editor;
}

const RunEditorPlacementState& RuntimeTools::Editor() const
{
    return m_editor;
}

RunEditorTracer& RuntimeTools::EditorTracer()
{
    return m_editorTracer;
}

const RunEditorTracer& RuntimeTools::EditorTracer() const
{
    return m_editorTracer;
}
} // namespace SkullbonezCore::Basics
