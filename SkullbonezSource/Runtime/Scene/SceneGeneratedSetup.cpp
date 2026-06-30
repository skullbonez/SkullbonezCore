/*
File: SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.cpp
Purpose:
  Implements deterministic generated/demo scene population.

Mental model:
  Generated setup is scene lifecycle behavior, not app-shell behavior. It
  receives explicit borrowed context from Run and fills the existing camera and
  model subsystems without changing spawn order or RNG consumption.

Glossary:
  Generated scene: Demo scene built from deterministic code rather than a
    `.scene.json` file.
  RNG (Random Number Generator): Local MSVC-compatible generator used for
    stable object placement.
  Solver object: Exact-count generated ball or box used by solver validation.

Invariants:
  - RNG consumption is part of generated scene determinism.
  - Generated object count and ordering feed replay, physics baselines, and
    camera tracking.
  - Setup borrows live model/camera/world storage and does not own it.

Related:
  - SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h
  - SkullbonezSource/Runtime/Scene/RunScene.cpp
  - Agentic/Plans/runtime-run-decomposition-plan.md
*/
#include "SceneGeneratedSetup.h"
#include "SceneRuntime.h"
#include "../CameraCollection.h"
#include "../../GameObjects/GameModel.h"
#include "../../GameObjects/GameModelCollection.h"
#include "../../Maths/Vector3.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../World/Terrain.h"
#include "../../World/WorldEnvironment.h"

#include <algorithm>
#include <utility>

namespace SkullbonezCore
{
namespace Basics
{
namespace
{
using SkullbonezCore::Math::Vector::Vector3;

int NextSceneRand( unsigned int& state )
{
    // Invariant: Match the MSVC CRT sequence so seeded scene layouts stay
    // stable while avoiding global RNG state.
    state = state * 214013u + 2531011u;
    return static_cast<int>( ( state >> 16 ) & 0x7fffu );
}
} // namespace


void SceneGeneratedSetup::SetUpCameras( SceneGeneratedCameraContext context )
{
    context.cameras.AddCamera( Vector3( 321.0f, 110.0f, 557.0f ), // Position
                               Vector3( 581.0f, 40.0f, 633.0f ),  // View
                               Vector3( 0.0f, 1.0f, 0.0f ),       // Up
                               CAMERA_GAME_MODEL_1 );

    context.cameras.AddCamera( Vector3( 730.0f, 100.0f, 380.0f ), // Position
                               Vector3( 709.0f, 92.0f, 482.0f ),  // View
                               Vector3( 0.0f, 1.0f, 0.0f ),       // Up
                               CAMERA_GAME_MODEL_2 );

    context.cameras.AddCamera( Vector3( 900.0f, 110.0f, 900.0f ), // Position
                               Vector3( 313.0f, 31.0f, 282.0f ),  // View
                               Vector3( 0.0f, 1.0f, 0.0f ),       // Up
                               CAMERA_FREE );

    context.cameras.SetCameraXZBounds( context.terrain.GetXZBounds() );
    context.cameras.SetTerrain( &context.terrain );
    context.cameras.SetLockedMode( true );
}


void SceneGeneratedSetup::SetUpGameModels( SceneGeneratedModelContext context, int count )
{
    // Concept: Generated demos consume one deterministic RNG stream. Keep object
    // family decisions and per-object random draws in the same order unless
    // every generated-scene baseline is intentionally refreshed.
    context.scene.modelCount = count;
    context.scene.solverBallCount = 0;
    context.scene.solverBoxCount = 0;

    const EngineConfig& cfg = context.config;

    auto randFloat = [&]( float base, int range )
    { return base + static_cast<float>( NextSceneRand( context.scene.rngState ) % range ); };
    auto randSigned = [&]( int range ) -> float
    {
        float mag = 1.0f + static_cast<float>( NextSceneRand( context.scene.rngState ) % range );
        return ( NextSceneRand( context.scene.rngState ) % 2 == 0 ) ? mag : -mag;
    };
    auto randSign = [&]() -> float { return ( NextSceneRand( context.scene.rngState ) % 2 == 0 ) ? 1.0f : -1.0f; };

    for ( int x = 0; x < context.scene.modelCount; ++x )
    {
        float posX = randFloat( cfg.spawnXBase, cfg.spawnXRange );
        float posY = randFloat( cfg.spawnYBase, cfg.spawnYRange );
        float posZ = randFloat( cfg.spawnZBase, cfg.spawnZRange );
        float mass = randFloat( cfg.ballMassMin, cfg.ballMassRange );
        float restitution =
            cfg.ballRestitutionMin +
            static_cast<float>( NextSceneRand( context.scene.rngState ) % cfg.ballRestitutionRange ) / 10.0f;
        Vector3 force( randSigned( cfg.ballForceRange ),
                       randSigned( cfg.ballForceRange ),
                       randSigned( cfg.ballForceRange ) );
        Vector3 forcePos( randSign(), randSign(), randSign() );

        bool makeBox = false;
        if ( context.objectTypeOverride == GeneratedObjectTypeOverride::AllBoxes )
        {
            makeBox = true;
        }
        else if ( context.objectTypeOverride == GeneratedObjectTypeOverride::AllBalls )
        {
            makeBox = false;
        }
        else
        {
            // ~30% of generated objects are boxes, giving the default demo a
            // mixed collision workload without requiring explicit scene bodies.
            makeBox = ( NextSceneRand( context.scene.rngState ) % 10 ) < 3;
        }

        if ( makeBox )
        {
            float halfExtent = ( 1.0f + static_cast<float>( NextSceneRand( context.scene.rngState ) % 3 ) ) * 0.6f;
            float hx = halfExtent * ( 0.7f + static_cast<float>( NextSceneRand( context.scene.rngState ) % 4 ) * 0.2f );
            float hy = halfExtent;
            float hz = halfExtent * ( 0.7f + static_cast<float>( NextSceneRand( context.scene.rngState ) % 4 ) * 0.2f );

            // Box inertia: I = m/3 * (hy^2 + hz^2) etc.
            float hx2 = hx * hx;
            float hy2 = hy * hy;
            float hz2 = hz * hz;
            float m3 = mass / 3.0f;
            Vector3 inertia( m3 * ( hy2 + hz2 ), m3 * ( hx2 + hz2 ), m3 * ( hx2 + hy2 ) );

            GameObjects::GameModel gameModel( &context.world, Vector3( posX, posY, posZ ), inertia, mass );
            gameModel.SetCoefficientRestitution( restitution );
            gameModel.SetTerrain( context.terrain );
            gameModel.AddBoundingBox( Vector3( hx, hy, hz ) );

            const int modelIndex = context.models.GetModelCount();
            context.models.AddGameModel( std::move( gameModel ) );
            context.models.SetPendingBodyImpulse( modelIndex, force, forcePos );
        }
        else
        {
            float moment = randFloat( cfg.ballMomentMin, cfg.ballMomentRange );
            float radius =
                ( 1.0f + static_cast<float>( NextSceneRand( context.scene.rngState ) % cfg.ballRadiusRange ) ) * 0.5f;

            GameObjects::GameModel gameModel( &context.world,
                                              Vector3( posX, posY, posZ ),
                                              Vector3( moment, moment, moment ),
                                              mass );
            gameModel.SetCoefficientRestitution( restitution );
            gameModel.SetTerrain( context.terrain );
            gameModel.AddBoundingSphere( radius );

            const int modelIndex = context.models.GetModelCount();
            context.models.AddGameModel( std::move( gameModel ) );
            context.models.SetPendingBodyImpulse( modelIndex, force, forcePos );
        }
    }
}


void SceneGeneratedSetup::SetUpSolverObjects( SceneGeneratedModelContext context, int balls, int boxes )
{
    balls = (std::max)( 0, balls );
    boxes = (std::max)( 0, boxes );
    const int totalObjects = balls + boxes;
    if ( context.objectTypeOverride == GeneratedObjectTypeOverride::AllBalls )
    {
        balls = totalObjects;
        boxes = 0;
    }
    else if ( context.objectTypeOverride == GeneratedObjectTypeOverride::AllBoxes )
    {
        balls = 0;
        boxes = totalObjects;
    }

    context.scene.modelCount = balls + boxes;
    context.scene.solverBallCount = balls;
    context.scene.solverBoxCount = boxes;

    const EngineConfig& cfg = context.config;

    auto randFloat = [&]( float base, int range )
    { return base + static_cast<float>( NextSceneRand( context.scene.rngState ) % range ); };
    auto randSigned = [&]( int range ) -> float
    {
        float mag = 1.0f + static_cast<float>( NextSceneRand( context.scene.rngState ) % range );
        return ( NextSceneRand( context.scene.rngState ) % 2 == 0 ) ? mag : -mag;
    };
    auto randSign = [&]() -> float { return ( NextSceneRand( context.scene.rngState ) % 2 == 0 ) ? 1.0f : -1.0f; };

    // --- Sphere pass ---
    for ( int i = 0; i < balls; ++i )
    {
        float posX = randFloat( cfg.spawnXBase, cfg.spawnXRange );
        float posY = randFloat( cfg.spawnYBase, cfg.spawnYRange );
        float posZ = randFloat( cfg.spawnZBase, cfg.spawnZRange );
        float mass = randFloat( cfg.ballMassMin, cfg.ballMassRange );
        float restitution =
            cfg.ballRestitutionMin +
            static_cast<float>( NextSceneRand( context.scene.rngState ) % cfg.ballRestitutionRange ) / 10.0f;
        float moment = randFloat( cfg.ballMomentMin, cfg.ballMomentRange );
        float radius =
            ( 1.0f + static_cast<float>( NextSceneRand( context.scene.rngState ) % cfg.ballRadiusRange ) ) * 0.5f;
        Vector3 force( randSigned( cfg.ballForceRange ),
                       randSigned( cfg.ballForceRange ),
                       randSigned( cfg.ballForceRange ) );
        Vector3 forcePos( randSign(), randSign(), randSign() );

        GameObjects::GameModel gameModel( &context.world,
                                          Vector3( posX, posY, posZ ),
                                          Vector3( moment, moment, moment ),
                                          mass );
        gameModel.SetCoefficientRestitution( restitution );
        gameModel.SetTerrain( context.terrain );
        gameModel.AddBoundingSphere( radius );
        const int modelIndex = context.models.GetModelCount();
        context.models.AddGameModel( std::move( gameModel ) );
        context.models.SetPendingBodyImpulse( modelIndex, force, forcePos );
    }

    // --- Box pass ---
    // Box inertia tensor (solid cuboid about centre of mass):
    //   Ix = m/12 * (hy^2 + hz^2),  Iy = m/12 * (hx^2 + hz^2),  Iz = m/12 * (hx^2 + hy^2)
    // where hx, hy, hz are the full extents (2 * half-extents).
    // The spawn code uses half-extents internally, so the factor is m/3 (= m/12 * 4).
    for ( int i = 0; i < boxes; ++i )
    {
        float posX = randFloat( cfg.spawnXBase, cfg.spawnXRange );
        float posY = randFloat( cfg.spawnYBase, cfg.spawnYRange );
        float posZ = randFloat( cfg.spawnZBase, cfg.spawnZRange );
        float mass = randFloat( cfg.ballMassMin, cfg.ballMassRange );
        float restitution =
            cfg.ballRestitutionMin +
            static_cast<float>( NextSceneRand( context.scene.rngState ) % cfg.ballRestitutionRange ) / 10.0f;
        Vector3 force( randSigned( cfg.ballForceRange ),
                       randSigned( cfg.ballForceRange ),
                       randSigned( cfg.ballForceRange ) );
        Vector3 forcePos( randSign(), randSign(), randSign() );

        float halfExtent = ( 1.0f + static_cast<float>( NextSceneRand( context.scene.rngState ) % 3 ) ) * 0.6f;
        float hx = halfExtent * ( 0.7f + static_cast<float>( NextSceneRand( context.scene.rngState ) % 4 ) * 0.2f );
        float hy = halfExtent;
        float hz = halfExtent * ( 0.7f + static_cast<float>( NextSceneRand( context.scene.rngState ) % 4 ) * 0.2f );

        float hx2 = hx * hx, hy2 = hy * hy, hz2 = hz * hz;
        float m3 = mass / 3.0f;
        Vector3 inertia( m3 * ( hy2 + hz2 ), m3 * ( hx2 + hz2 ), m3 * ( hx2 + hy2 ) );

        GameObjects::GameModel gameModel( &context.world, Vector3( posX, posY, posZ ), inertia, mass );
        gameModel.SetCoefficientRestitution( restitution );
        gameModel.SetTerrain( context.terrain );
        gameModel.AddBoundingBox( Vector3( hx, hy, hz ) );
        const int modelIndex = context.models.GetModelCount();
        context.models.AddGameModel( std::move( gameModel ) );
        context.models.SetPendingBodyImpulse( modelIndex, force, forcePos );
    }

    context.scene.modelCount = balls + boxes;
}


bool SceneGeneratedSetup::TrySetUpRequestedModels( SceneGeneratedModelContext context,
                                                   const SceneGeneratedPopulationRequest& request,
                                                   bool useDefaultWhenNoRequest )
{
    // Concept: Generated population policy belongs beside the deterministic
    // spawn algorithms. Run supplies state; this helper decides which generated
    // mode is authoritative for this load.
    if ( request.uiSolverBallCountOverride >= 0 || request.uiSolverBoxCountOverride >= 0 )
    {
        SetUpSolverObjects( context,
                            (std::max)( 0, request.uiSolverBallCountOverride ),
                            (std::max)( 0, request.uiSolverBoxCountOverride ) );
        return true;
    }

    if ( request.uiModelCountOverride >= 0 )
    {
        SetUpGameModels( context, request.uiModelCountOverride );
        return true;
    }

    if ( request.sceneSolverBallCount > 0 || request.sceneSolverBoxCount > 0 )
    {
        SetUpSolverObjects( context, request.sceneSolverBallCount, request.sceneSolverBoxCount );
        return true;
    }

    if ( useDefaultWhenNoRequest )
    {
        SetUpGameModels( context, request.defaultModelCount );
        return true;
    }

    return false;
}

} // namespace Basics
} // namespace SkullbonezCore
