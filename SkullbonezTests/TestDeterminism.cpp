//
// File: SkullbonezTests/TestDeterminism.cpp
// Purpose:
//   Lock a fast PhysicsEngine determinism property and replay snapshot restore contract.
//
// Mental model:
//   A minimal authored physics world can be seeded directly through
//   PhysicsEngine without GameModelCollection or scene-load plumbing. Fixed-step
//   determinism means two engines with identical body/collider rows produce the
//   same byte-level kinematic state at the same tick boundaries.
//
// Glossary:
//   Micro-world: Tiny unit-test physics scene with a few authored bodies and
//     colliders, deliberately below every worker-dispatch threshold.
//   Solver snapshot: Replay-owned PhysicsWorld state captured separately from
//     body-store poses and velocities.
//   Body replay state: Store-owned body row fields restored through
//     PhysicsEngine::RestoreReplayBodyState.
//   Null render resource factory: Inert backend capability needed only to
//     satisfy the Terrain constructor signature in this focused harness.
//
// Invariants:
//   - The micro-world stays serial; worker fan-out starts far above this body count.
//   - Snapshot losslessness needs both solver state and body replay state.
//   - Kinematic comparisons are byte-exact, not epsilon-based.
//   - Terrain queries are real flat-plane queries; render resources must stay unused.
//
// Related:
//   - SkullbonezSource/Physics/PhysicsEngine.h
//   - SkullbonezSource/Runtime/Replay/ReplaySolverSnapshot.h
//   - fable_plans/01-unit-test-pyramid-progress.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Assets/AssetSystem.h"
#include "../SkullbonezSource/Core/Common.h"
#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/Core/WorkerPool.h"
#include "../SkullbonezSource/Physics/BoundingSphere.h"
#include "../SkullbonezSource/Physics/PhysicsApi.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Physics/PhysicsEngine.h"
#include "../SkullbonezSource/Physics/PhysicsWorldForces.h"
#include "../SkullbonezSource/Rendering/IRenderResourceFactory.h"
#include "../SkullbonezSource/Runtime/Replay/ReplaySolverSnapshot.h"
#include "../SkullbonezSource/World/Terrain.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>

using SkullbonezCore::Basics::EngineConfig;
using SkullbonezCore::Basics::ReplaySolverWorldSnapshot;
using SkullbonezCore::Geometry::Terrain;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::CollisionShape;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::MakeColliderCreateDesc;
using SkullbonezCore::Physics::MakePhysicsBodyCreateDesc;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyMotionKind;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsEngine;
using SkullbonezCore::Physics::PhysicsSceneObjectId;
using SkullbonezCore::Physics::PhysicsWorldForces;
using SkullbonezCore::Threading::WorkerPool;

namespace
{
constexpr int kMicroBodyCount = 3;
constexpr int kSnapshotFrame = 120;
constexpr int kReplayWindowTicks = 60;
constexpr int kTotalDeterminismTicks = 240;

// Why: Terrain is still part of the real physics step, but the unit test only
// needs its collision plane. Resource methods return inert handles because any
// render call here would mean the fixture crossed out of its physics boundary.
class NullRenderResourceFactory final : public SkullbonezCore::Rendering::IRenderResourceFactory
{
  public:
    std::unique_ptr<SkullbonezCore::Rendering::IShader> CreateShader( const char* ) override
    {
        return nullptr;
    }

    std::unique_ptr<SkullbonezCore::Rendering::IMesh> CreateMesh( const float*, int, bool, bool ) override
    {
        return nullptr;
    }

    std::unique_ptr<SkullbonezCore::Rendering::IFramebuffer>
    CreateFramebuffer( int, int, SkullbonezCore::Rendering::FramebufferColorFormat ) override
    {
        return nullptr;
    }

    uint32_t CreateTexture2D( const uint8_t*, int, int, int, bool, bool ) override
    {
        return 0u;
    }

    void DeleteTexture( uint32_t ) override
    {
    }

    uint32_t CreateDynamicVB( const int*, int, int ) override
    {
        return 0u;
    }

    void DestroyDynamicVB( uint32_t ) override
    {
    }

    uint32_t CreateInstancedMesh( const float*,
                                  int,
                                  int,
                                  int,
                                  int,
                                  int,
                                  const int*,
                                  int,
                                  const int*,
                                  int ) override
    {
        return 0u;
    }

    void DestroyInstancedMesh( uint32_t ) override
    {
    }
};

struct BodyReplayState
{
    PhysicsBodyHandle handle;
    uint32_t replayBodyId = 0;
    bool fixed = false;
    Vector3 position;
    Quaternion orientation;
    Vector3 linearVelocity;
    Vector3 angularVelocity;
    float mass = 0.0f;
    float inverseMass = 0.0f;
    Vector3 rotationalInertia;
    Vector3 inverseRotationalInertia;
};

struct MicroWorldSnapshot
{
    ReplaySolverWorldSnapshot solver;
    std::array<BodyReplayState, kMicroBodyCount> bodies;
};

EngineConfig MakeDeterministicConfig()
{
    EngineConfig config;
    config.physicsParallel = false;
    config.physicsParallelApplyForces = false;
    config.physicsParallelTornadoField = false;
    config.physicsParallelNarrowphase = false;
    config.physicsParallelTerrainDetect = false;
    config.physicsParallelIntegrate = false;
    config.fluidDensity = 0.0f;
    config.gasDensity = 0.0f;
    config.gravity = -9.8f;
    config.velocityLimit = 1000.0f;
    config.broadphaseCell = 32.0f;
    config.physicsSleepFrames = 1000000;
    return config;
}

PhysicsWorldForces DeterministicForces()
{
    PhysicsWorldForces forces;
    forces.gravity = -9.8f;
    forces.fluidSurfaceHeight = -1000.0f;
    forces.fluidDensity = 0.0f;
    forces.gasDensity = 0.0f;
    forces.angularDragMultiplier = 0.0f;
    return forces;
}

Terrain& FlatTestTerrain()
{
    // Lifetime: bodies borrow this Terrain pointer for every step. Static
    // storage keeps the borrowed terrain and config valid across repeated
    // engine resets without depending on process-global configuration.
    static EngineConfig config = MakeDeterministicConfig();
    static SkullbonezCore::Assets::AssetSystem assets;
    static NullRenderResourceFactory resources;
    static Terrain terrain( 0.0f, 0.0f, 0.0f, config, assets, resources );
    return terrain;
}

CollisionShape MakeSphereShape( float radius )
{
    return CollisionShape( BoundingSphere( radius, Vector3( 0.0f, 0.0f, 0.0f ), 0.0f ) );
}

void AddMicroBody( PhysicsEngine& engine,
                   uint32_t sceneObjectId,
                   const Vector3& position,
                   const Vector3& linearVelocity )
{
    const float radius = 1.0f;
    const float mass = 2.0f;
    const float inertia = 0.4f * mass * radius * radius;
    const CollisionShape shape = MakeSphereShape( radius );
    auto bodyDesc = MakePhysicsBodyCreateDesc( PhysicsSceneObjectId{ sceneObjectId },
                                               shape,
                                               position,
                                               SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION,
                                               linearVelocity,
                                               Vector3( 0.03f * static_cast<float>( sceneObjectId ), 0.0f, 0.0f ),
                                               Vector3( inertia, inertia, inertia ),
                                               mass,
                                               0.0f,
                                               PhysicsBodyMotionKind::Dynamic,
                                               &FlatTestTerrain(),
                                               "unit-determinism-body" );
    bodyDesc.angularVelocityLimit = 1000.0f;
    const PhysicsBodyHandle body = engine.RegisterAuthoredBody( bodyDesc );
    auto colliderDesc = MakeColliderCreateDesc( shape, 0.0f, 0u, "unit" );
    colliderDesc.body = body;
    colliderDesc.sceneObjectId = bodyDesc.sceneObjectId;
    (void)engine.RegisterAuthoredCollider( colliderDesc );
}

void SeedMicroWorld( PhysicsEngine& engine )
{
    EngineConfig config = MakeDeterministicConfig();
    engine.Clear();
    engine.ApplyRuntimeConfig( config );
    engine.SetSleepEnabled( false );
    AddMicroBody( engine, 101u, Vector3( 100.0f, 30.0f, 100.0f ), Vector3( 1.5f, 0.0f, 0.0f ) );
    AddMicroBody( engine, 102u, Vector3( 112.0f, 40.0f, 100.0f ), Vector3( 0.0f, 0.5f, 0.0f ) );
    AddMicroBody( engine, 103u, Vector3( 124.0f, 50.0f, 100.0f ), Vector3( -1.0f, 0.0f, 0.0f ) );
    REQUIRE( engine.BodyStore().Count() == kMicroBodyCount );
    REQUIRE( engine.Colliders().Count() == kMicroBodyCount );
}

void StepMicroWorld( PhysicsEngine& engine, int ticks )
{
    EngineConfig config = MakeDeterministicConfig();
    const PhysicsWorldForces forces = DeterministicForces();
    WorkerPool workerPool;
    for ( int tick = 0; tick < ticks; ++tick )
    {
        engine.Step( PHYSICS_FIXED_DT,
                     config,
                     forces,
                     workerPool,
                     nullptr,
                     0,
                     SkullbonezCore::Physics::PhysicsDiagnosticsCsvWriter{} );
    }
}

BodyReplayState CaptureBodyReplayState( const PhysicsBodyRecord& record )
{
    BodyReplayState state;
    state.handle = record.handle;
    state.replayBodyId = record.replayBodyId;
    state.fixed = record.isFixed;
    state.position = record.position;
    state.orientation = record.orientation;
    state.linearVelocity = record.linearVelocity;
    state.angularVelocity = record.angularVelocity;
    state.mass = record.mass;
    state.inverseMass = record.invMass;
    state.rotationalInertia = record.rotationalInertia;
    state.inverseRotationalInertia = record.invRotationalInertia;
    return state;
}

MicroWorldSnapshot CaptureMicroWorldSnapshot( const PhysicsEngine& engine )
{
    MicroWorldSnapshot snapshot;
    engine.CaptureReplaySolverSnapshot( snapshot.solver, kMicroBodyCount );
    for ( int i = 0; i < kMicroBodyCount; ++i )
    {
        const PhysicsBodyRecord* record = engine.BodyStore().RecordForModelIndex( i );
        REQUIRE( record != nullptr );
        snapshot.bodies[static_cast<std::size_t>( i )] = CaptureBodyReplayState( *record );
    }
    return snapshot;
}

void RestoreMicroWorldSnapshot( PhysicsEngine& engine, const MicroWorldSnapshot& snapshot )
{
    REQUIRE( engine.RestoreReplaySolverSnapshot( snapshot.solver, kMicroBodyCount ) );
    for ( const BodyReplayState& state : snapshot.bodies )
    {
        REQUIRE( engine.RestoreReplayBodyState( state.handle,
                                                state.replayBodyId,
                                                state.fixed,
                                                state.position,
                                                state.orientation,
                                                state.linearVelocity,
                                                state.angularVelocity,
                                                state.mass,
                                                state.inverseMass,
                                                state.rotationalInertia,
                                                state.inverseRotationalInertia ) );
    }
}

void CheckVectorBytesEqual( const Vector3& lhs, const Vector3& rhs )
{
    CHECK( std::memcmp( &lhs, &rhs, sizeof( Vector3 ) ) == 0 );
}

void CheckQuaternionBytesEqual( const Quaternion& lhs, const Quaternion& rhs )
{
    float left[4] = {};
    float right[4] = {};
    lhs.GetComponents( left[0], left[1], left[2], left[3] );
    rhs.GetComponents( right[0], right[1], right[2], right[3] );
    CHECK( std::memcmp( left, right, sizeof( left ) ) == 0 );
}

void CheckEngineKinematicsEqual( const PhysicsEngine& lhs, const PhysicsEngine& rhs )
{
    REQUIRE( lhs.BodyStore().Count() == rhs.BodyStore().Count() );
    for ( int i = 0; i < lhs.BodyStore().Count(); ++i )
    {
        const PhysicsBodyRecord* left = lhs.BodyStore().RecordForModelIndex( i );
        const PhysicsBodyRecord* right = rhs.BodyStore().RecordForModelIndex( i );
        REQUIRE( left != nullptr );
        REQUIRE( right != nullptr );
        CheckVectorBytesEqual( left->position, right->position );
        CheckQuaternionBytesEqual( left->orientation, right->orientation );
        CheckVectorBytesEqual( left->linearVelocity, right->linearVelocity );
        CheckVectorBytesEqual( left->angularVelocity, right->angularVelocity );
    }
}
} // namespace


TEST_CASE( "PhysicsEngine determinism: micro-world matches at fixed tick intervals" )
{
    static PhysicsEngine first;
    static PhysicsEngine second;
    SeedMicroWorld( first );
    SeedMicroWorld( second );

    for ( int tick = 60; tick <= kTotalDeterminismTicks; tick += 60 )
    {
        StepMicroWorld( first, 60 );
        StepMicroWorld( second, 60 );
        CheckEngineKinematicsEqual( first, second );
    }
}


TEST_CASE( "PhysicsEngine determinism: solver snapshot plus body state restores losslessly" )
{
    static PhysicsEngine interrupted;
    static PhysicsEngine restored;
    SeedMicroWorld( interrupted );
    SeedMicroWorld( restored );

    StepMicroWorld( interrupted, kSnapshotFrame );
    StepMicroWorld( restored, kSnapshotFrame );
    const MicroWorldSnapshot snapshot = CaptureMicroWorldSnapshot( restored );

    StepMicroWorld( interrupted, kReplayWindowTicks );
    StepMicroWorld( restored, kReplayWindowTicks );
    RestoreMicroWorldSnapshot( restored, snapshot );
    StepMicroWorld( restored, kReplayWindowTicks );

    CheckEngineKinematicsEqual( interrupted, restored );
}
