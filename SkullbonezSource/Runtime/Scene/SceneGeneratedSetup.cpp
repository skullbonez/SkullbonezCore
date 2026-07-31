/*
File: SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.cpp
Purpose:
  Implements deterministic generated/demo scene population.

Summary:
  Generated setup is scene lifecycle behavior, not app-shell behavior. It
  receives one borrowed SceneWorld and fills its camera, entity, physics, and
  terrain-backed model stores without changing spawn order or RNG consumption.

Glossary:
  RNG (Random Number Generator): Local MSVC-compatible generator used for
    stable object placement.

Invariants:
  - RNG consumption is part of generated scene determinism.
  - Generated object count and ordering feed replay, physics baselines, and
    camera tracking.
  - Setup borrows SceneWorld synchronously and does not retain store references.

Related:
  - SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
  - Agentic/Reference/engine-glossary.md
*/
#include "SceneGeneratedSetup.h"
#include "../../Assets/AssetKeys.h"
#include "SceneSessionState.h"
#include "../Camera/CameraCollection.h"
#include "../../Core/Common.h"
#include "SceneController.h"
#include "../../Maths/Vector3.h"
#include "../../Physics/CollisionShape.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../World/Terrain.h"
#include "../../World/WorldEnvironment.h"

#include <algorithm>
#include <utility>

namespace SkullbonezCore
{
namespace Runtime
{
namespace
{
using SkullbonezCore::Math::CollisionDetection::BoundingBox;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::CollisionShape;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::MakeColliderCreateDesc;
using SkullbonezCore::Physics::MakePhysicsBodyCreateDesc;
using SkullbonezCore::Physics::PhysicsBodyCreateDesc;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyMotionKind;
using SkullbonezCore::Physics::PhysicsColliderCreateDesc;

int NextSceneRand( unsigned int& state )
{

    // Invariant: Match the MSVC CRT sequence so seeded scene layouts stay
    // stable while avoiding global RNG state.
    state = state * 214013u + 2531011u;
    return static_cast<int>( ( state >> 16 ) & 0x7fffu );
}

PhysicsColliderCreateDesc MakeGeneratedColliderDesc( CollisionShape shape, float restitution )
{

    // Why: generated setup already owns the exact shape parameters at spawn
    // time. Passing this value into physics avoids cold model-side collider
    // recapture and keeps store rows descriptor-owned.
    return MakeColliderCreateDesc( std::move( shape ), restitution, HashStr( "default" ) );
}

PhysicsColliderCreateDesc MakeGeneratedSphereColliderDesc( float radius, float restitution )
{
    return MakeGeneratedColliderDesc( BoundingSphere( radius, Vector3( 0.0f, 0.0f, 0.0f ) ), restitution );
}

PhysicsColliderCreateDesc MakeGeneratedBoxColliderDesc( const Vector3& halfExtents, float restitution )
{
    return MakeGeneratedColliderDesc( BoundingBox( halfExtents, Vector3( 0.0f, 0.0f, 0.0f ) ), restitution );
}

PhysicsBodyCreateDesc MakeGeneratedBodyDesc( Physics::PhysicsSceneObjectId sceneObjectId, const CollisionShape& shape,
                                             const Vector3& position, const Vector3& rotationalInertia, float mass,
                                             float restitution )
{
    return MakePhysicsBodyCreateDesc( sceneObjectId, shape, position, Math::Orientation::IDENTITY_QUATERNION,
                                      Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), rotationalInertia, mass,
                                      restitution, PhysicsBodyMotionKind::Dynamic );
}

// Concept: one transient sample is the cohesive output of exactly one generated
// object RNG step. It owns no capacity or identity authority; the preflight
// pass discards it after counting shape kind, and the live pass consumes the
// same fields immediately to publish one body/collider pair.
//
// Invariant: SampleGeneratedObject is the only implementation of mixed-object
// RNG consumption so count preflight and population cannot drift.
struct GeneratedObjectSample
{
    Vector3 position;
    Vector3 force;
    Vector3 forcePosition;
    Vector3 boxHalfExtents;
    Vector3 rotationalInertia;
    float mass = 0.0f;
    float restitution = 0.0f;
    float sphereRadius = 0.0f;
    bool makeBox = false;
};

GeneratedObjectSample SampleGeneratedObject( unsigned int& rngState, const SkullbonezCore::Core::EngineConfig& config,
                                             GeneratedObjectTypeOverride objectTypeOverride )
{
    const auto randFloat = [&]( float base, int range )
    { return base + static_cast<float>( NextSceneRand( rngState ) % range ); };

    const auto randSigned = [&]( int range ) -> float
    {
        const float magnitude = 1.0f + static_cast<float>( NextSceneRand( rngState ) % range );

        return ( NextSceneRand( rngState ) % 2 == 0 ) ? magnitude : -magnitude;
    };
    const auto randSign = [&]() -> float { return ( NextSceneRand( rngState ) % 2 == 0 ) ? 1.0f : -1.0f; };

    GeneratedObjectSample sample;
    const float posX = randFloat( config.generatedScene.spawnXBase, config.generatedScene.spawnXRange );
    const float posY = randFloat( config.generatedScene.spawnYBase, config.generatedScene.spawnYRange );
    const float posZ = randFloat( config.generatedScene.spawnZBase, config.generatedScene.spawnZRange );
    sample.position = Vector3( posX, posY, posZ );
    sample.mass = randFloat( config.generatedScene.ballMassMin, config.generatedScene.ballMassRange );
    sample.restitution = config.generatedScene.ballRestitutionMin +
                         static_cast<float>( NextSceneRand( rngState ) % config.generatedScene.ballRestitutionRange ) /
                             10.0f;

    sample.force = Vector3( randSigned( config.generatedScene.ballForceRange ),
                            randSigned( config.generatedScene.ballForceRange ),
                            randSigned( config.generatedScene.ballForceRange ) );

    sample.forcePosition = Vector3( randSign(), randSign(), randSign() );

    if ( objectTypeOverride == GeneratedObjectTypeOverride::AllBoxes )
    {
        sample.makeBox = true;
    }
    else if ( objectTypeOverride == GeneratedObjectTypeOverride::AllBalls )
    {
        sample.makeBox = false;
    }
    else
    {
        sample.makeBox = ( NextSceneRand( rngState ) % 10 ) < 3;
    }

    if ( sample.makeBox )
    {
        const float halfExtent = ( 1.0f + static_cast<float>( NextSceneRand( rngState ) % 3 ) ) * 0.6f;
        const float hx = halfExtent * ( 0.7f + static_cast<float>( NextSceneRand( rngState ) % 4 ) * 0.2f );
        const float hy = halfExtent;
        const float hz = halfExtent * ( 0.7f + static_cast<float>( NextSceneRand( rngState ) % 4 ) * 0.2f );
        const float hx2 = hx * hx;
        const float hy2 = hy * hy;
        const float hz2 = hz * hz;
        const float massThird = sample.mass / 3.0f;
        sample.boxHalfExtents = Vector3( hx, hy, hz );
        sample.rotationalInertia = Vector3( massThird * ( hy2 + hz2 ), massThird * ( hx2 + hz2 ),
                                            massThird * ( hx2 + hy2 ) );
    }
    else
    {
        const float moment = randFloat( config.generatedScene.ballMomentMin, config.generatedScene.ballMomentRange );
        sample.sphereRadius = ( 1.0f +
                                static_cast<float>( NextSceneRand( rngState ) % config.generatedScene.ballRadiusRange ) ) *
                              0.5f;

        sample.rotationalInertia = Vector3( moment, moment, moment );
    }

    return sample;
}
} // namespace


void SceneGeneratedSetup::SetUpCameras( SceneWorld& sceneWorld )
{
    sceneWorld.Cameras().AddCamera( Vector3( 321.0f, 110.0f, 557.0f ), // Position
                                    Vector3( 581.0f, 40.0f, 633.0f ),  // View
                                    Vector3( 0.0f, 1.0f, 0.0f ),       // Up
                                    CAMERA_SCENE_OBJECT_1 );

    sceneWorld.Cameras().AddCamera( Vector3( 730.0f, 100.0f, 380.0f ), // Position
                                    Vector3( 709.0f, 92.0f, 482.0f ),  // View
                                    Vector3( 0.0f, 1.0f, 0.0f ),       // Up
                                    CAMERA_SCENE_OBJECT_2 );

    sceneWorld.Cameras().AddCamera( Vector3( 900.0f, 110.0f, 900.0f ), // Position
                                    Vector3( 313.0f, 31.0f, 282.0f ),  // View
                                    Vector3( 0.0f, 1.0f, 0.0f ),       // Up
                                    CAMERA_FREE );

    sceneWorld.Cameras().SetCameraXZBounds( sceneWorld.Terrain().Get()->GetXZBounds() );
    sceneWorld.Cameras().SetTerrain( sceneWorld.Terrain().Get() );
    sceneWorld.Cameras().SetLockedMode( true );
}


SkullbonezCore::Core::SbResult
SceneGeneratedSetup::SetUpSceneEntities( SceneSessionState& scene, const SkullbonezCore::Core::EngineConfig& config,
                                         SceneWorld& sceneWorld, GeneratedObjectTypeOverride objectTypeOverride, int count )
{
    int sphereCapacity = 0;
    int boxCapacity = 0;
    unsigned int capacityRngState = scene.rngState;

    // Preflight a copy of the deterministic stream through the same sampling
    // function used by population. The live RNG state and output sequence stay
    // unchanged while each concrete shape store receives its exact count.

    for ( int index = 0; index < count; ++index )
    {
        const GeneratedObjectSample sample = SampleGeneratedObject( capacityRngState, config, objectTypeOverride );

        sphereCapacity += sample.makeBox ? 0 : 1;
        boxCapacity += sample.makeBox ? 1 : 0;
    }

    const SkullbonezCore::Core::SbResult capacityCommit = sceneWorld.CommitPhysicsSceneCapacity( count, sphereCapacity,
                                                                                                 boxCapacity, 0, 0, 0 );

    if ( !capacityCommit.Ok() )
    {
        return capacityCommit;
    }

    // Concept: Generated demos consume one deterministic RNG stream. Keep object
    // family decisions and per-object random draws in the same order unless
    // every generated-scene baseline is intentionally refreshed.
    scene.modelCount = count;
    scene.solverBallCount = 0;
    scene.solverBoxCount = 0;

    for ( int x = 0; x < scene.modelCount; ++x )
    {
        const GeneratedObjectSample sample = SampleGeneratedObject( scene.rngState, config, objectTypeOverride );

        if ( sample.makeBox )
        {
            SceneEntityCreateDesc gameModel;

            const Physics::PhysicsSceneObjectId sceneObjectId = scene.AllocateSceneObjectId();
            gameModel.sceneObjectId = sceneObjectId;
            const BoundingBox shape( sample.boxHalfExtents, Vector3( 0.0f, 0.0f, 0.0f ) );
            const auto appendResult = sceneWorld.TryCreateSceneEntity( std::move( gameModel ),
                                                                       MakeGeneratedBodyDesc( sceneObjectId, shape,
                                                                                              sample.position,
                                                                                              sample.rotationalInertia,
                                                                                              sample.mass,
                                                                                              sample.restitution ),
                                                                       MakeGeneratedColliderDesc( shape,
                                                                                                  sample.restitution ) );

            if ( !appendResult.status.Ok() )
            {
                return appendResult.status;
            }

            const PhysicsBodyHandle body = appendResult.body;
            sceneWorld.Physics().SetPendingBodyImpulse( body, sample.force, sample.forcePosition );
        }
        else
        {
            SceneEntityCreateDesc gameModel;

            const Physics::PhysicsSceneObjectId sceneObjectId = scene.AllocateSceneObjectId();
            gameModel.sceneObjectId = sceneObjectId;
            const BoundingSphere shape( sample.sphereRadius, Vector3( 0.0f, 0.0f, 0.0f ) );
            const auto appendResult = sceneWorld.TryCreateSceneEntity( std::move( gameModel ),
                                                                       MakeGeneratedBodyDesc( sceneObjectId, shape,
                                                                                              sample.position,
                                                                                              sample.rotationalInertia,
                                                                                              sample.mass,
                                                                                              sample.restitution ),
                                                                       MakeGeneratedColliderDesc( shape,
                                                                                                  sample.restitution ) );

            if ( !appendResult.status.Ok() )
            {
                return appendResult.status;
            }

            const PhysicsBodyHandle body = appendResult.body;
            sceneWorld.Physics().SetPendingBodyImpulse( body, sample.force, sample.forcePosition );
        }
    }

    return SkullbonezCore::Core::SbResult::Success();
}


SkullbonezCore::Core::SbResult SceneGeneratedSetup::SetUpSolverObjects( SceneSessionState& scene,
                                                                        const SkullbonezCore::Core::EngineConfig& config,
                                                                        SceneWorld& sceneWorld,
                                                                        GeneratedObjectTypeOverride objectTypeOverride,
                                                                        int balls, int boxes )
{
    balls = (std::max)( 0, balls );
    boxes = (std::max)( 0, boxes );
    const int totalObjects = balls + boxes;

    if ( objectTypeOverride == GeneratedObjectTypeOverride::AllBalls )
    {
        balls = totalObjects;
        boxes = 0;
    }
    else if ( objectTypeOverride == GeneratedObjectTypeOverride::AllBoxes )
    {
        balls = 0;
        boxes = totalObjects;
    }

    const SkullbonezCore::Core::SbResult capacityCommit = sceneWorld.CommitPhysicsSceneCapacity( balls + boxes, balls, boxes,
                                                                                                 0, 0, 0 );

    if ( !capacityCommit.Ok() )
    {
        return capacityCommit;
    }

    scene.modelCount = balls + boxes;
    scene.solverBallCount = balls;
    scene.solverBoxCount = boxes;

    auto randFloat = [&]( float base, int range )
    { return base + static_cast<float>( NextSceneRand( scene.rngState ) % range ); };

    auto randSigned = [&]( int range ) -> float
    {
        float mag = 1.0f + static_cast<float>( NextSceneRand( scene.rngState ) % range );

        return ( NextSceneRand( scene.rngState ) % 2 == 0 ) ? mag : -mag;
    };

    auto randSign = [&]() -> float { return ( NextSceneRand( scene.rngState ) % 2 == 0 ) ? 1.0f : -1.0f; };

    // --- Sphere pass ---

    for ( int i = 0; i < balls; ++i )
    {
        float posX = randFloat( config.generatedScene.spawnXBase, config.generatedScene.spawnXRange );
        float posY = randFloat( config.generatedScene.spawnYBase, config.generatedScene.spawnYRange );
        float posZ = randFloat( config.generatedScene.spawnZBase, config.generatedScene.spawnZRange );
        float mass = randFloat( config.generatedScene.ballMassMin, config.generatedScene.ballMassRange );
        float restitution = config.generatedScene.ballRestitutionMin +
                            static_cast<float>( NextSceneRand( scene.rngState ) %
                                                config.generatedScene.ballRestitutionRange ) /
                                10.0f;

        float moment = randFloat( config.generatedScene.ballMomentMin, config.generatedScene.ballMomentRange );
        float radius = ( 1.0f +
                         static_cast<float>( NextSceneRand( scene.rngState ) % config.generatedScene.ballRadiusRange ) ) *
                       0.5f;

        Vector3 force( randSigned( config.generatedScene.ballForceRange ),
                       randSigned( config.generatedScene.ballForceRange ),
                       randSigned( config.generatedScene.ballForceRange ) );

        Vector3 forcePos( randSign(), randSign(), randSign() );

        SceneEntityCreateDesc gameModel;
        const Physics::PhysicsSceneObjectId sceneObjectId = scene.AllocateSceneObjectId();
        gameModel.sceneObjectId = sceneObjectId;
        const BoundingSphere shape( radius, Vector3( 0.0f, 0.0f, 0.0f ) );
        const auto appendResult = sceneWorld.TryCreateSceneEntity( std::move( gameModel ),
                                                                   MakeGeneratedBodyDesc( sceneObjectId, shape,
                                                                                          Vector3( posX, posY, posZ ),
                                                                                          Vector3( moment, moment, moment ),
                                                                                          mass, restitution ),
                                                                   MakeGeneratedColliderDesc( shape, restitution ) );

        if ( !appendResult.status.Ok() )
        {
            return appendResult.status;
        }

        const PhysicsBodyHandle body = appendResult.body;
        sceneWorld.Physics().SetPendingBodyImpulse( body, force, forcePos );
    }

    // --- Box pass ---
    // Box inertia tensor (solid cuboid about centre of mass):
    //   Ix = m/12 * (hy^2 + hz^2),  Iy = m/12 * (hx^2 + hz^2),  Iz = m/12 * (hx^2 + hy^2)
    // where hx, hy, hz are the full extents (2 * half-extents).
    // The spawn code uses half-extents internally, so the factor is m/3 (= m/12 * 4).

    for ( int i = 0; i < boxes; ++i )
    {
        float posX = randFloat( config.generatedScene.spawnXBase, config.generatedScene.spawnXRange );
        float posY = randFloat( config.generatedScene.spawnYBase, config.generatedScene.spawnYRange );
        float posZ = randFloat( config.generatedScene.spawnZBase, config.generatedScene.spawnZRange );
        float mass = randFloat( config.generatedScene.ballMassMin, config.generatedScene.ballMassRange );
        float restitution = config.generatedScene.ballRestitutionMin +
                            static_cast<float>( NextSceneRand( scene.rngState ) %
                                                config.generatedScene.ballRestitutionRange ) /
                                10.0f;

        Vector3 force( randSigned( config.generatedScene.ballForceRange ),
                       randSigned( config.generatedScene.ballForceRange ),
                       randSigned( config.generatedScene.ballForceRange ) );

        Vector3 forcePos( randSign(), randSign(), randSign() );

        float halfExtent = ( 1.0f + static_cast<float>( NextSceneRand( scene.rngState ) % 3 ) ) * 0.6f;
        float hx = halfExtent * ( 0.7f + static_cast<float>( NextSceneRand( scene.rngState ) % 4 ) * 0.2f );
        float hy = halfExtent;
        float hz = halfExtent * ( 0.7f + static_cast<float>( NextSceneRand( scene.rngState ) % 4 ) * 0.2f );

        float hx2 = hx * hx, hy2 = hy * hy, hz2 = hz * hz;
        float m3 = mass / 3.0f;
        Vector3 inertia( m3 * ( hy2 + hz2 ), m3 * ( hx2 + hz2 ), m3 * ( hx2 + hy2 ) );

        SceneEntityCreateDesc gameModel;
        const Physics::PhysicsSceneObjectId sceneObjectId = scene.AllocateSceneObjectId();
        gameModel.sceneObjectId = sceneObjectId;
        const BoundingBox shape( Vector3( hx, hy, hz ), Vector3( 0.0f, 0.0f, 0.0f ) );
        const auto appendResult = sceneWorld.TryCreateSceneEntity( std::move( gameModel ),
                                                                   MakeGeneratedBodyDesc( sceneObjectId, shape,
                                                                                          Vector3( posX, posY, posZ ),
                                                                                          inertia, mass, restitution ),
                                                                   MakeGeneratedColliderDesc( shape, restitution ) );

        if ( !appendResult.status.Ok() )
        {
            return appendResult.status;
        }

        const PhysicsBodyHandle body = appendResult.body;
        sceneWorld.Physics().SetPendingBodyImpulse( body, force, forcePos );
    }

    scene.modelCount = balls + boxes;
    return SkullbonezCore::Core::SbResult::Success();
}


SceneGeneratedSetupResult
SceneGeneratedSetup::TrySetUpRequestedModels( SceneSessionState& scene, const SkullbonezCore::Core::EngineConfig& config,
                                              SceneWorld& sceneWorld, GeneratedObjectTypeOverride objectTypeOverride,
                                              GeneratedPopulationMode mode, int modelCount, int balls, int boxes )
{

    // Concept: Scene load resolves which request source is authoritative. This
    // helper dispatches that resolved mode beside the deterministic spawn
    // algorithms and reports whether generated setup owned population.

    if ( mode == GeneratedPopulationMode::Solver )
    {
        return { SetUpSolverObjects( scene, config, sceneWorld, objectTypeOverride, (std::max)( 0, balls ),
                                     (std::max)( 0, boxes ) ),
                 true };
    }

    if ( mode == GeneratedPopulationMode::Models )
    {
        return { SetUpSceneEntities( scene, config, sceneWorld, objectTypeOverride, modelCount ), true };
    }

    // Why: authored scene loading asks generated setup first so UI/exact solver
    // overrides can win. A successful "not applied" result hands population
    // back to authored scene sections.
    return { SkullbonezCore::Core::SbResult::Success(), false };
}

} // namespace Runtime
} // namespace SkullbonezCore
