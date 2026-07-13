//
// File: SkullbonezTests/TestPersistentContactSolver.cpp
// Purpose:
//   Lock direct behavioral coverage for persistent contact solver rows.
//
// Summary:
//   Terrain manifolds are already narrowphase output. These tests feed one
//   deterministic manifold into PersistentContactSolver so the row setup,
//   warm-start cache, friction clamp, restitution, and writeback can be checked
//   without running a full PhysicsEngine frame.
//
// Glossary:
//   Contact row: Solver constraint row that applies one normal impulse and two
//     tangent impulses at a contact point.
//   Warm starting: Reusing the previous frame's accumulated impulse so a stable
//     contact begins near its converged solution.
//   Friction cone: Two tangent impulses clamped as one vector budget so diagonal
//     sliding cannot exceed the authored friction limit.
//   Restitution: Bounce response from a closing contact velocity.
//   Determinism: Same fixture inputs produce the same row cache and body
//     writeback, matching the physics baseline contract.
//
// Invariants:
//   - The fixture bypasses broadphase and object narrowphase; each test owns the
//     exact manifold row it wants the persistent solver to consume.
//   - Static fixed lists mirror runtime storage and avoid allocating
//     SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS records on the doctest stack.
//
// Related:
//   - SkullbonezSource/Physics/PersistentContactSolver.cpp
//   - SkullbonezSource/Physics/TerrainContactManifold.h
//   - Agentic/Reports/behavioral_test_depth_closure_20260711.md
//

#include "../ThirdPtySource/doctest/doctest.h"
#include "../SkullbonezSource/Physics/PhysicsTimestep.h"

#include "../SkullbonezSource/Core/Common.h"
#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/Physics/BoundingSphere.h"
#include "../SkullbonezSource/Physics/ColliderStore.h"
#include "../SkullbonezSource/Physics/ContactSolverCommon.h"
#include "../SkullbonezSource/Physics/PersistentContactSolver.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Physics/PhysicsWorld.h"
#include "../SkullbonezSource/Physics/TerrainContactManifold.h"

#include <array>
#include <cmath>
#include <vector>

using SkullbonezCore::Core::EngineConfig;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::CollisionShape;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderRecordList;
using SkullbonezCore::Physics::ColliderShapeKind;
using SkullbonezCore::Physics::PersistentContactCacheEntry;
using SkullbonezCore::Physics::PersistentContactSolver;
using SkullbonezCore::Physics::PersistentContactSolverContext;
using SkullbonezCore::Physics::PersistentContactSolverSideEffects;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyRecordList;
using SkullbonezCore::Physics::PhysicsDebugContact;
using SkullbonezCore::Physics::PhysicsWorld;
using SkullbonezCore::Physics::SolverBodyState;
using SkullbonezCore::Physics::TerrainContactManifold;

namespace
{
constexpr float kSolverDt = PHYSICS_FIXED_DT;

PhysicsBodyRecordList& TestBodyRecords()
{
    // Why: physics fixed lists own SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS rows. Static storage matches
    // runtime ownership and keeps this focused fixture off the doctest stack.
    static PhysicsBodyRecordList records( "TestPersistentContactSolver.bodyRecords" );
    records.clear();
    return records;
}

ColliderRecordList& TestColliderRecords()
{
    // Why: collider records mirror runtime fixed storage. Clearing the same
    // static list keeps each case deterministic without stack-heavy fixtures.
    static ColliderRecordList records( "TestPersistentContactSolver.colliderRecords" );
    records.clear();
    return records;
}

struct SolverFixture
{
    PhysicsBodyRecordList& bodyRecords;
    ColliderRecordList& colliderRecords;
    std::vector<std::pair<int, int>> candidatePairs;
    std::vector<uint8_t> sleepState;
    std::vector<std::pair<int, int>> sleepSupportEdges;
    std::vector<PhysicsWorld::PersistentContact> persistentContacts;
    std::vector<PersistentContactCacheEntry> persistentContactCache;
    PhysicsWorld::PersistentContactSolverStats stats;
    std::vector<uint16_t> contactCounts;
    std::vector<uint16_t> restingContactCounts;
    std::vector<SolverBodyState> solverBodies;
    std::vector<PhysicsDebugContact> debugContacts;
    std::vector<TerrainContactManifold> terrainContactManifolds;
    std::array<uint8_t, SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS> terrainRestApplied = {};
    std::vector<uint8_t> sleepSupportedThisFrame;
    PersistentContactSolverSideEffects sideEffects;
    SkullbonezCore::Core::EngineConfig config;
    PersistentContactSolver solver;

    SolverFixture() : bodyRecords( TestBodyRecords() ), colliderRecords( TestColliderRecords() )
    {
        config.physicsExecution.parallel = false;
        config.worldForces.gravity = -30.0f;
        config.physicsMaterial.frictionCoeff = 0.2f;
        config.bodySimulation.contactRestitutionThreshold = 0.1f;
        config.terrainContact.slop = 0.0f;
        config.terrainContact.baumgarteBeta = 0.25f;
        config.terrainContact.maxBaumgarteBias = 6.0f;
        config.persistentContactSolver.iterations = 12;
    }

    void AddDynamicSphere( const Vector3& position, const Vector3& linearVelocity, float restitution = 0.0f )
    {
        const float radius = 1.0f;
        const float mass = 2.0f;
        const float inertia = 0.4f * mass * radius * radius;

        PhysicsBodyRecord body;
        body.position = position;
        body.linearVelocity = linearVelocity;
        body.rotationalInertia = Vector3( inertia, inertia, inertia );
        body.invRotationalInertia = Vector3( 1.0f / inertia, 1.0f / inertia, 1.0f / inertia );
        body.mass = mass;
        body.invMass = 1.0f / mass;
        body.boundingRadius = radius;
        bodyRecords.push_back( body );

        ColliderRecord collider;
        collider.shape = CollisionShape( BoundingSphere( radius, Vector3( 0.0f, 0.0f, 0.0f ), 0.0f ) );
        collider.shapeKind = ColliderShapeKind::Sphere;
        collider.boundingRadius = radius;
        collider.restitution = restitution;
        collider.friction = config.physicsMaterial.frictionCoeff;
        colliderRecords.push_back( collider );

        sleepState.assign( bodyRecords.size(), 0u );
        sleepSupportedThisFrame.assign( bodyRecords.size(), 0u );
    }

    void AddTerrainContact( int bodyIndex, uint32_t featureId, float penetration )
    {
        const PhysicsBodyRecord& body = bodyRecords[bodyIndex];
        TerrainContactManifold manifold;
        manifold.bodyA = bodyIndex;
        manifold.bodyB = -1;
        manifold.normal = Vector3( 0.0f, 1.0f, 0.0f );
        manifold.tangent1 = Vector3( 1.0f, 0.0f, 0.0f );
        manifold.tangent2 = Vector3( 0.0f, 0.0f, 1.0f );
        manifold.pointCount = 1;
        manifold.supportsRestingPolicy = true;
        manifold.allowsTangentFriction = true;
        manifold.points[0].featureId = featureId;
        manifold.points[0].rA = Vector3( 0.0f, -body.boundingRadius, 0.0f );
        manifold.points[0].point = body.position + manifold.points[0].rA;
        manifold.points[0].penetration = penetration;
        terrainContactManifolds.push_back( manifold );
    }

    PersistentContactSolverContext MakeContext()
    {
        return PersistentContactSolverContext{ candidatePairs,
                                               sleepState,
                                               sleepSupportEdges,
                                               persistentContacts,
                                               persistentContactCache,
                                               stats,
                                               contactCounts,
                                               restingContactCounts,
                                               solverBodies,
                                               debugContacts,
                                               terrainContactManifolds,
                                               terrainRestApplied,
                                               sleepSupportedThisFrame,
                                               sideEffects,
                                               { bodyRecords.data(), bodyRecords.size() },
                                               { colliderRecords.data(), colliderRecords.size() },
                                               static_cast<int>( bodyRecords.size() ),
                                               0,
                                               false,
                                               config };
    }

    void Solve()
    {
        PersistentContactSolverContext context = MakeContext();
        solver.Solve( context, kSolverDt );
    }
};
} // namespace


TEST_CASE( "Persistent contact solver: warm-start cache is reused on a matching terrain row" )
{
    SolverFixture first;
    first.AddDynamicSphere( Vector3( 0.0f, 1.0f, 0.0f ), Vector3( 0.0f, -0.1f, 0.0f ) );
    first.AddTerrainContact( 0, 42u, 0.05f );
    first.Solve();

    REQUIRE( first.persistentContactCache.size() == 1u );
    CHECK( first.stats.cachePreviousRows == 0 );
    CHECK( first.stats.cacheMisses == 1 );
    CHECK( first.persistentContactCache[0].accN > 0.0f );

    SolverFixture second;
    second.AddDynamicSphere( Vector3( 0.0f, 1.0f, 0.0f ), Vector3( 0.0f, -0.1f, 0.0f ) );
    second.AddTerrainContact( 0, 42u, 0.05f );
    second.persistentContactCache = first.persistentContactCache;
    second.Solve();

    CHECK( second.stats.cachePreviousRows == 1 );
    CHECK( second.stats.cacheHits == 1 );
    CHECK( second.stats.warmStartedRows == 1 );
    CHECK( second.stats.solverIterations <= first.stats.solverIterations );
}


TEST_CASE( "Persistent contact solver: friction cone clamps diagonal tangent impulse" )
{
    float accT1 = 3.0f;
    float accT2 = 4.0f;
    SkullbonezCore::Physics::ContactSolver::ClampFrictionVector( accT1, accT2, 2.0f );
    CHECK( accT1 == doctest::Approx( 1.2f ).epsilon( 0.0001 ) );
    CHECK( accT2 == doctest::Approx( 1.6f ).epsilon( 0.0001 ) );

    SolverFixture fixture;
    fixture.AddDynamicSphere( Vector3( 0.0f, 1.0f, 0.0f ), Vector3( 5.0f, -0.1f, 5.0f ) );
    fixture.AddTerrainContact( 0, 7u, 0.05f );
    fixture.Solve();

    REQUIRE( fixture.persistentContactCache.size() == 1u );
    const PersistentContactCacheEntry& cached = fixture.persistentContactCache[0];
    const float tangentMagnitude = sqrtf( cached.accT1 * cached.accT1 + cached.accT2 * cached.accT2 );
    const float terrainWarmStart =
        fixture.bodyRecords[0].mass * fabsf( fixture.config.worldForces.gravity ) * kSolverDt;
    const float frictionLimit = fixture.config.physicsMaterial.frictionCoeff *
                                ( ( cached.accN > terrainWarmStart ) ? cached.accN : terrainWarmStart );
    CHECK( tangentMagnitude > 0.0f );
    CHECK( tangentMagnitude <= frictionLimit + 0.0001f );
}


TEST_CASE( "Persistent contact solver: restitution creates separating terrain velocity" )
{
    SolverFixture fixture;
    fixture.AddDynamicSphere( Vector3( 0.0f, 1.0f, 0.0f ), Vector3( 0.0f, -6.0f, 0.0f ), 0.75f );
    fixture.AddTerrainContact( 0, 99u, 0.0f );
    fixture.Solve();

    REQUIRE( fixture.debugContacts.size() == 1u );
    CHECK( fixture.debugContacts[0].preSolveClosingSpeed > fixture.config.bodySimulation.contactRestitutionThreshold );
    CHECK( fixture.debugContacts[0].normalImpulse > 0.0f );
    CHECK( fixture.bodyRecords[0].linearVelocity.y > 0.0f );
}
