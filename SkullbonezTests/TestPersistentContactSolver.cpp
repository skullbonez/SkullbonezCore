//
// File: SkullbonezTests/TestPersistentContactSolver.cpp
// Purpose:
//   Lock direct behavioral coverage for persistent contact solver rows.
//
// Summary:
//   Most tests feed one deterministic terrain manifold directly into
//   PhysicsContactSolverStage. The box sleep test additionally exercises exact
//   object narrowphase so support classification is checked on real manifold
//   geometry without running a full PhysicsEngine frame.
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
//   Sleep support edge: Directed relationship used later to propagate grounded
//     sleep eligibility through an object stack.
//   Solver step policy: Once-per-solve normalized contact limits shared by
//     object and terrain rows.
//
// Invariants:
//   - The fixture always bypasses broadphase. Terrain cases own their exact row;
//     object cases deliberately use exact narrowphase before solver ingestion.
//   - Static fixed lists mirror runtime storage and avoid allocating
//     SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS records on the doctest stack.
//   - Raw stamped settings normalize once at the solver boundary; direct value
//     tests pin every lower and upper bound used by contact rows.
//
// Related:
//   - SkullbonezSource/Physics/PersistentContactSolver.cpp
//   - SkullbonezSource/Physics/TerrainContactManifold.h
//   - Agentic/Reports/behavioral_test_depth_closure_20260711.md
//

#include "../ThirdPtySource/doctest/doctest.h"
#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#include "TestFixedSeed.h"
#include "../SkullbonezSource/Physics/PhysicsTimestep.h"

#include "../SkullbonezSource/Core/Common.h"
#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/Physics/BoundingBox.h"
#include "../SkullbonezSource/Physics/BoundingSphere.h"
#include "../SkullbonezSource/Physics/ColliderStore.h"
#include "../SkullbonezSource/Physics/ContactSolverCommon.h"
#include "../SkullbonezSource/Physics/ObjectContactManifold.h"
#include "../SkullbonezSource/Physics/PersistentContactSolver.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Physics/PhysicsWorldForces.h"
#include "../SkullbonezSource/Physics/SleepIslandSystem.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsTerrainStage.h"
#include "../SkullbonezSource/Physics/TerrainContactManifold.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

using SkullbonezCore::Math::CollisionDetection::BoundingBox;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::CollisionShape;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::BuildObjectContactManifold;
using SkullbonezCore::Physics::BuildTerrainContactManifold;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderShapeKind;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::MAX_SLEEP_SUPPORT_EDGES;
using SkullbonezCore::Physics::ObjectContactBodyView;
using SkullbonezCore::Physics::ObjectContactManifold;
using SkullbonezCore::Physics::PersistentContactCacheEntry;
using SkullbonezCore::Physics::PhysicsBodyCreateRecord;
using SkullbonezCore::Physics::PhysicsBodyLinearVelocity;
using SkullbonezCore::Physics::PhysicsBodyOrientation;
using SkullbonezCore::Physics::PhysicsBodyPosition;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsContactSolverStage;
using SkullbonezCore::Physics::PhysicsSolverSnapshot;
using SkullbonezCore::Physics::PhysicsStepDiagnostics;
using SkullbonezCore::Physics::PhysicsTerrainStage;
using SkullbonezCore::Physics::PhysicsTerrainView;
using SkullbonezCore::Physics::PhysicsWorldForces;
using SkullbonezCore::Physics::PreparedTerrainCandidateCommit;
using SkullbonezCore::Physics::TerrainContactBodyView;
using SkullbonezCore::Physics::TerrainContactManifold;
using SkullbonezCore::Physics::TerrainContactSweepResult;

namespace
{
constexpr float kSolverDt = PHYSICS_FIXED_DT;

PhysicsBodyStore& TestBodyStore()
{

    // Why: physics fixed lists own SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS rows. Static storage matches
    // runtime ownership and keeps this focused fixture off the doctest stack.
    static PhysicsBodyStore store;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        store.ReserveCapacity( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    }

    store.Clear();
    return store;
}

ColliderStore& TestColliderStore()
{

    // Why: collider records mirror runtime fixed storage. Clearing the same
    // static list keeps each case deterministic without stack-heavy fixtures.
    static ColliderStore store;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        store.ReserveCapacity( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
        store.ReserveShapeCapacity( 16u, 16u, 4u );
    }
    store.Clear();
    return store;
}

PhysicsStepDiagnostics& TestStepDiagnostics()
{

    // Why: Debug diagnostics own a fixed collision-time event table sized from
    // scene capacity. Static storage mirrors the runtime owner and keeps two
    // simultaneous warm-start fixtures within the doctest stack budget.
    static PhysicsStepDiagnostics diagnostics;
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        diagnostics.ReserveSceneCapacity( 16u );
    }
    diagnostics.Clear();
    return diagnostics;
}

struct SolverFixture
{
    PhysicsBodyStore& bodyStore;
    ColliderStore& colliderStore;
    std::vector<std::pair<int, int>> candidatePairs;
    std::vector<uint8_t> sleepState;
    SkullbonezCore::Physics::PhysicsCandidatePairList
        sleepSupportEdges { "TestPersistentContactSolver.sleepSupportEdges",
                            SkullbonezCore::Physics::PhysicsCapacityReason::ExplicitTestCapacity };
    SkullbonezCore::Physics::PhysicsBodyRowList<TerrainContactManifold>
        terrainContactManifolds { "TestPersistentContactSolver.terrainContactManifolds",
                                  SkullbonezCore::Physics::PhysicsCapacityReason::ExplicitTestCapacity };
    std::array<uint8_t, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> terrainRestApplied = {};
    std::vector<uint8_t> sleepSupportedThisFrame;
    SkullbonezCore::Physics::PhysicsRuntimeSettings config;
    PhysicsWorldForces worldForces;
    PhysicsContactSolverStage solver;
    PhysicsStepDiagnostics& diagnostics;

    SolverFixture()
        : bodyStore( TestBodyStore() ), colliderStore( TestColliderStore() ), diagnostics( TestStepDiagnostics() )
    {
        {
            SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
                SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );

            sleepSupportEdges.Reserve( MAX_SLEEP_SUPPORT_EDGES );
            terrainContactManifolds.Reserve( 16u );
            solver.ReserveSceneCapacity( 16u );
        }

        config.execution.parallel = false;
        config.worldForces.gravity = -30.0f;
        config.material.terrainFrictionCoefficient = 0.2f;
        config.body.contactRestitutionThreshold = 0.1f;
        config.terrain.slop = 0.0f;
        config.terrain.baumgarteBeta = 0.25f;
        config.terrain.maxBaumgarteBias = 6.0f;
        config.solver.iterations = 12;
    }

    void AddDynamicSphere( const Vector3& position, const Vector3& linearVelocity, float restitution = 0.0f,
                           bool isFixed = false )
    {
        const float radius = 1.0f;
        const float mass = 2.0f;
        const float inertia = 0.4f * mass * radius * radius;

        PhysicsBodyCreateRecord body;
        body.hot.position = position;
        body.hot.linearVelocity = linearVelocity;
        body.cold.rotationalInertia = Vector3( inertia, inertia, inertia );
        body.hot.inverseRotationalInertia = isFixed ? Vector3() : Vector3( 1.0f / inertia, 1.0f / inertia, 1.0f / inertia );
        body.cold.mass = mass;
        body.hot.inverseMass = isFixed ? 0.0f : 1.0f / mass;
        body.hot.boundingRadius = radius;
        body.hot.fixed = isFixed;
        (void)bodyStore.CreateBodyRecord( body );

        ColliderRecord collider;
        const CollisionShape shape( BoundingSphere( radius, Vector3( 0.0f, 0.0f, 0.0f ), 0.0f ) );
        collider.shapeKind = ColliderShapeKind::Sphere;
        collider.boundingRadius = radius;
        collider.restitution = restitution;
        collider.friction = config.material.terrainFrictionCoefficient;
        (void)colliderStore.CreateColliderRecord( collider, shape );

        sleepState.assign( static_cast<std::size_t>( bodyStore.Count() ), 0u );
        sleepSupportedThisFrame.assign( static_cast<std::size_t>( bodyStore.Count() ), 0u );
    }

    void AddBox( const Vector3& position, float xRotationRadians, bool isFixed )
    {
        constexpr float halfExtent = 1.0f;
        constexpr float mass = 2.0f;

        // Invariant: a two-unit cube with mass two has diagonal inertia 4/3.
        constexpr float inertia = 4.0f / 3.0f;
        const Vector3 halfExtents( halfExtent, halfExtent, halfExtent );

        PhysicsBodyCreateRecord body;
        body.hot.position = position;
        body.hot.orientation.RotateAboutAxis( Vector3( 1.0f, 0.0f, 0.0f ), xRotationRadians );
        body.cold.rotationalInertia = Vector3( inertia, inertia, inertia );
        body.hot.inverseRotationalInertia = isFixed ? Vector3( 0.0f, 0.0f, 0.0f )
                                                    : Vector3( 1.0f / inertia, 1.0f / inertia, 1.0f / inertia );

        body.cold.mass = mass;
        body.hot.inverseMass = isFixed ? 0.0f : 1.0f / mass;
        body.hot.boundingRadius = sqrtf( 3.0f );
        body.hot.fixed = isFixed;
        body.cold.usesWorldInertia = true;
        (void)bodyStore.CreateBodyRecord( body );

        ColliderRecord collider;

        // Hazard: Debug poisons a default-constructed Vector3. Shape-local
        // offsets must spell zero explicitly or exact narrowphase receives NaN.
        const CollisionShape shape( BoundingBox( halfExtents, Vector3( 0.0f, 0.0f, 0.0f ) ) );
        collider.shapeKind = ColliderShapeKind::Box;
        collider.boundingRadius = body.hot.boundingRadius;
        collider.friction = config.material.terrainFrictionCoefficient;
        (void)colliderStore.CreateColliderRecord( collider, shape );

        sleepState.assign( static_cast<std::size_t>( bodyStore.Count() ), 0u );
        sleepSupportedThisFrame.assign( static_cast<std::size_t>( bodyStore.Count() ), 0u );
    }

    void AddTerrainContactAtOffset( int bodyIndex, uint32_t featureId, float penetration, const Vector3& contactOffset,
                                    bool supportsRestingPolicy, bool inhibitsSleep )
    {
        const auto hotFields = bodyStore.HotFields();
        TerrainContactManifold manifold;
        manifold.bodyA = bodyIndex;
        manifold.bodyB = -1;
        manifold.normal = Vector3( 0.0f, 1.0f, 0.0f );
        manifold.tangent1 = Vector3( 1.0f, 0.0f, 0.0f );
        manifold.tangent2 = Vector3( 0.0f, 0.0f, 1.0f );
        manifold.pointCount = 1;
        manifold.supportsRestingPolicy = supportsRestingPolicy;
        manifold.allowsTangentFriction = supportsRestingPolicy;
        manifold.inhibitsSleep = inhibitsSleep;
        manifold.points[0].featureId = featureId;
        manifold.points[0].rA = contactOffset;
        manifold.points[0].point = PhysicsBodyPosition( hotFields, static_cast<std::size_t>( bodyIndex ) ) +
                                   manifold.points[0].rA;

        manifold.points[0].penetration = penetration;
        terrainContactManifolds.push_back( manifold );
    }

    void AddTerrainContact( int bodyIndex, uint32_t featureId, float penetration, bool supportsRestingPolicy = true,
                            bool inhibitsSleep = false )
    {
        const float radius = bodyStore.HotFields().boundingRadius[static_cast<std::size_t>( bodyIndex )];
        AddTerrainContactAtOffset( bodyIndex, featureId, penetration, Vector3( 0.0f, -radius, 0.0f ), supportsRestingPolicy,
                                   inhibitsSleep );
    }

    void AddTerrainFaceContact( int bodyIndex, uint32_t firstFeatureId, float penetration, uint8_t pointCount )
    {
        const auto hotFields = bodyStore.HotFields();
        const float radius = hotFields.boundingRadius[static_cast<std::size_t>( bodyIndex )];
        TerrainContactManifold manifold;
        manifold.bodyA = bodyIndex;
        manifold.bodyB = -1;
        manifold.normal = Vector3( 0.0f, 1.0f, 0.0f );
        manifold.tangent1 = Vector3( 1.0f, 0.0f, 0.0f );
        manifold.tangent2 = Vector3( 0.0f, 0.0f, 1.0f );
        manifold.pointCount = pointCount;
        manifold.supportsRestingPolicy = true;
        manifold.allowsTangentFriction = true;

        // Invariant: all rows carry the same build-time penetration while the
        // symmetric arms model distinct points on one four-corner footprint.
        for ( uint8_t pointIndex = 0; pointIndex < pointCount; ++pointIndex )
        {
            auto& point = manifold.points[pointIndex];
            const float x = ( pointIndex & 1u ) != 0u ? 0.25f : -0.25f;
            const float z = ( pointIndex & 2u ) != 0u ? 0.25f : -0.25f;
            point.featureId = firstFeatureId + pointIndex;
            point.rA = Vector3( x, -radius, z );
            point.point = PhysicsBodyPosition( hotFields, static_cast<std::size_t>( bodyIndex ) ) + point.rA;
            point.penetration = penetration;
        }

        terrainContactManifolds.push_back( manifold );
    }

    void CopySolverStateFrom( const SolverFixture& source )
    {
        PhysicsSolverSnapshot snapshot;
        source.solver.CaptureReplayState( snapshot );
        solver.RestoreReplayState( snapshot );
    }

    void Solve()
    {
        worldForces.gravity = config.worldForces.gravity;
        diagnostics.BeginStep( bodyStore.Count() );
        const auto policy = PhysicsContactSolverStage::ResolveStepPolicy( config, worldForces );
        solver.Solve( bodyStore, colliderStore, policy, candidatePairs, sleepState, sleepSupportEdges,
                      terrainContactManifolds, terrainRestApplied, sleepSupportedThisFrame, diagnostics, kSolverDt,
                      nullptr );
    }
};

} // namespace


TEST_CASE( "Persistent contact solver: use-site guards clamp invalid stamped settings exactly once" )
{

    // Invariant: the cold config stamp does not normalize values. The solver's
    // historical guards remain the one place that defines effective policy;
    // drift here can change the byte-exact physics regression baseline.
    SkullbonezCore::Physics::PhysicsRuntimeSettings settings;
    settings.solver.slop = -0.25f;
    settings.solver.baumgarteBeta = -0.5f;
    settings.solver.positionCorrectionPercent = -1.5f;
    settings.solver.iterations = -7;
    settings.terrain.slop = -0.35f;
    settings.terrain.baumgarteBeta = -0.6f;
    settings.terrain.maxBaumgarteBias = -2.0f;
    settings.sleep.linearSpeed = -0.7f;
    settings.sleep.angularSpeed = -0.8f;
    settings.body.contactRestitutionThreshold = 0.9f;

    PhysicsWorldForces worldForces;
    worldForces.gravity = 12.5f;
    worldForces.mutualGravity.enabled = true;
    worldForces.mutualGravity.elasticCollisions = true;
    auto policy = PhysicsContactSolverStage::ResolveStepPolicy( settings, worldForces );
    CHECK( policy.objectSlop == 0.0f );
    CHECK( policy.objectBaumgarteBeta == 0.0f );
    CHECK( policy.objectPositionCorrectionPercent == 0.0f );
    CHECK( policy.terrainSlop == 0.0f );
    CHECK( policy.terrainBaumgarteBeta == 0.0f );
    CHECK( policy.maxBaumgarteBias == 0.0f );
    CHECK( policy.iterations == 1 );
    CHECK( policy.sleepLinearSpeed == settings.sleep.linearSpeed );
    CHECK( policy.sleepAngularSpeed == settings.sleep.angularSpeed );
    CHECK( policy.nonNegativeSleepLinearSpeed == 0.0f );
    CHECK( policy.nonNegativeSleepAngularSpeed == 0.0f );
    CHECK( policy.rawContactRestitutionThreshold == settings.body.contactRestitutionThreshold );
    CHECK( policy.contactRestitutionThreshold == 0.0f );
    CHECK( policy.gravityAcceleration.x == 0.0f );
    CHECK( policy.gravityAcceleration.y == worldForces.gravity );
    CHECK( policy.gravityAcceleration.z == 0.0f );

    settings.solver.slop = 0.15f;
    settings.solver.baumgarteBeta = 0.25f;
    settings.solver.positionCorrectionPercent = 1.5f;
    settings.solver.iterations = 19;
    settings.terrain.slop = 0.45f;
    settings.terrain.baumgarteBeta = 0.55f;
    settings.terrain.maxBaumgarteBias = 3.0f;
    policy = PhysicsContactSolverStage::ResolveStepPolicy( settings, worldForces );
    CHECK( policy.objectSlop == settings.solver.slop );
    CHECK( policy.objectBaumgarteBeta == settings.solver.baumgarteBeta );
    CHECK( policy.objectPositionCorrectionPercent == 1.0f );
    CHECK( policy.terrainSlop == settings.terrain.slop );
    CHECK( policy.terrainBaumgarteBeta == settings.terrain.baumgarteBeta );
    CHECK( policy.maxBaumgarteBias == settings.terrain.maxBaumgarteBias );
    CHECK( policy.iterations == settings.solver.iterations );
}


TEST_CASE( "Persistent contact solver: warm-start cache is reused on a matching terrain row" )
{
    SolverFixture first;
    first.AddDynamicSphere( Vector3( 0.0f, 1.0f, 0.0f ), Vector3( 0.0f, -0.1f, 0.0f ) );
    first.AddTerrainContact( 0, 42u, 0.05f );
    first.Solve();

    REQUIRE( first.solver.GetPersistentContactCache().size() == 1u );
    CHECK( first.solver.GetStats().cachePreviousRows == 0 );
    CHECK( first.solver.GetStats().cacheMisses == 1 );
    CHECK( first.solver.GetPersistentContactCache()[0].accN > 0.0f );
    PhysicsSolverSnapshot reducedCache;
    first.solver.CaptureReplayState( reducedCache );
    REQUIRE( reducedCache.persistentContactCache.size() == 1u );
    reducedCache.persistentContactCache[0].accN *= 0.25f;
    reducedCache.persistentContactCache[0].accT1 = 0.0f;
    reducedCache.persistentContactCache[0].accT2 = 0.0f;
    const float reducedCachedNormalImpulse = reducedCache.persistentContactCache[0].accN;

    SolverFixture second;
    second.AddDynamicSphere( Vector3( 0.0f, 1.0f, 0.0f ), Vector3( 0.0f, -0.1f, 0.0f ) );
    second.AddTerrainContact( 0, 42u, 0.05f );
    second.solver.RestoreReplayState( reducedCache );
    second.Solve();

    CHECK( second.solver.GetStats().cachePreviousRows == 1 );
    CHECK( second.solver.GetStats().cacheHits == 1 );
    CHECK( second.solver.GetStats().warmStartedRows == 1 );
    CHECK( second.solver.GetStats().solverIterations <= first.solver.GetStats().solverIterations );
    REQUIRE( second.solver.GetPersistentContacts().size() == 1u );
    CHECK( second.solver.GetPersistentContacts()[0].terrainWarmStart > reducedCachedNormalImpulse );

    const auto& pipeline = second.solver.GetSideEffects().pipelineRecords;
    const auto warmStart = std::find_if(
        pipeline.begin(), pipeline.end(), []( const SkullbonezCore::Physics::PhysicsPipelineRecord& record )
        { return record.stage == SkullbonezCore::Physics::PhysicsPipelineStage::WarmStart && record.featureId == 42u; } );
    REQUIRE( warmStart != pipeline.end() );
    CHECK( warmStart->scalarB == doctest::Approx( reducedCachedNormalImpulse ).epsilon( 0.00001 ) );
}


TEST_CASE( "Persistent contact solver: manifold rows share one position-correction budget" )
{
    SolverFixture onePoint;
    onePoint.AddDynamicSphere( Vector3( 0.0f, 1.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) );
    const float onePointInitialY = onePoint.bodyStore.HotFields().positionY[0];
    onePoint.AddTerrainFaceContact( 0, 520u, 0.2f, 1u );
    onePoint.Solve();

    SolverFixture fourPoints;
    fourPoints.AddDynamicSphere( Vector3( 0.0f, 1.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) );
    const float fourPointInitialY = fourPoints.bodyStore.HotFields().positionY[0];
    fourPoints.AddTerrainFaceContact( 0, 530u, 0.2f, 4u );
    fourPoints.Solve();

    const auto& onePointStats = onePoint.solver.GetStats();
    const auto& fourPointStats = fourPoints.solver.GetStats();
    const float onePointDisplacement = fabsf( onePoint.bodyStore.HotFields().positionY[0] - onePointInitialY );
    const float fourPointDisplacement = fabsf( fourPoints.bodyStore.HotFields().positionY[0] - fourPointInitialY );

    CHECK( onePointStats.positionCorrectionRows == 1 );
    CHECK( fourPointStats.positionCorrectionRows == 4 );
    CHECK( onePointStats.positionCorrectionTotal == doctest::Approx( 0.16f ).epsilon( 0.00001 ) );
    CHECK( onePointDisplacement == doctest::Approx( 0.08f ).epsilon( 0.00001 ) );
    CHECK( fourPointStats.positionCorrectionTotal ==
           doctest::Approx( onePointStats.positionCorrectionTotal ).epsilon( 0.00001 ) );
    CHECK( fourPointStats.positionCorrectionMax ==
           doctest::Approx( onePointStats.positionCorrectionMax * 0.25f ).epsilon( 0.00001 ) );
    CHECK( fourPointDisplacement == doctest::Approx( onePointDisplacement ).epsilon( 0.00001 ) );
}


TEST_CASE( "Persistent contact solver: terrain row uses row-derived first-touch support instead of a fixed weight seed" )
{
    SolverFixture fixture;
    fixture.config.solver.iterations = 1;
    const float gravityStepSpeed = fabsf( fixture.config.worldForces.gravity ) * kSolverDt;
    fixture.AddDynamicSphere( Vector3( 0.0f, 1.0f, 0.0f ), Vector3( 0.0f, -gravityStepSpeed, 0.0f ) );
    fixture.AddTerrainContact( 0, 501u, 0.0f );
    fixture.Solve();

    REQUIRE( fixture.solver.GetPersistentContacts().size() == 1u );
    const auto& contact = fixture.solver.GetPersistentContacts()[0];
    const float expectedWeightImpulse = fixture.bodyStore.Records()[0].mass * gravityStepSpeed;
    CHECK( contact.terrainWarmStart == doctest::Approx( expectedWeightImpulse ).epsilon( 0.00001 ) );
    CHECK( contact.accN == doctest::Approx( expectedWeightImpulse ).epsilon( 0.00001 ) );
    CHECK( fixture.solver.GetStats().warmStartedRows == 1 );
    CHECK( fixture.solver.GetPersistentContactCounts()[0] == 1u );
    CHECK( fixture.terrainRestApplied[0] == 1u );
    CHECK( fixture.bodyStore.HotFields().linearVelocityY[0] >= -0.00001f );
}


TEST_CASE( "Persistent contact solver: terrain first-touch load is shared across every body contact row" )
{
    SolverFixture fixture;
    fixture.config.solver.iterations = 1;
    const float gravityStepSpeed = fabsf( fixture.config.worldForces.gravity ) * kSolverDt;
    fixture.AddDynamicSphere( Vector3( 0.0f, 1.0f, 0.0f ), Vector3( 0.0f, -gravityStepSpeed, 0.0f ) );
    fixture.AddTerrainContact( 0, 511u, 0.0f );
    fixture.AddTerrainContact( 0, 512u, 0.0f );
    fixture.Solve();

    REQUIRE( fixture.solver.GetPersistentContacts().size() == 2u );
    CHECK( fixture.solver.GetPersistentContactCounts()[0] == 2u );
    const float expectedTotalImpulse = fixture.bodyStore.Records()[0].mass * gravityStepSpeed;
    const float expectedRowImpulse = expectedTotalImpulse * 0.5f;
    float totalFirstTouchImpulse = 0.0f;

    for ( const auto& contact : fixture.solver.GetPersistentContacts() )
    {
        CHECK( contact.terrainWarmStart == doctest::Approx( expectedRowImpulse ).epsilon( 0.00001 ) );
        totalFirstTouchImpulse += contact.terrainWarmStart;
    }

    CHECK( totalFirstTouchImpulse == doctest::Approx( expectedTotalImpulse ).epsilon( 0.00001 ) );
    CHECK( fixture.bodyStore.HotFields().linearVelocityY[0] >= -0.00001f );
}


TEST_CASE( "Persistent contact solver: shoreline rows cache row-derived impulses without becoming sleep support" )
{
    constexpr float tiltedEdgeRadians = 0.75f;
    const float tiltedBoxCenterY = cosf( tiltedEdgeRadians ) + sinf( tiltedEdgeRadians );

    auto buildUnsupportedTerrainEdge = [&]( SolverFixture& fixture, float downwardSpeed )
    {
        fixture.AddBox( Vector3( 0.0f, tiltedBoxCenterY, 0.0f ), tiltedEdgeRadians, false );
        fixture.bodyStore.MutableHotFields().linearVelocityY[0] = -downwardSpeed;

        PhysicsTerrainView terrain;
        terrain.flatSlope = true;
        terrain.flatSlopeExtent = 1000.0f;
        terrain.slopeBaseY = 0.0f;
        terrain.slopeX = 0.0f;
        terrain.slopeZ = 0.0f;
        terrain.flatSlopePlane.m_normal = Vector3( 0.0f, 1.0f, 0.0f );
        terrain.flatSlopePlane.m_distance = 0.0f;

        const auto hotFields = fixture.bodyStore.HotFields();
        TerrainContactBodyView body;
        body.position = PhysicsBodyPosition( hotFields, 0u );
        body.orientation = PhysicsBodyOrientation( hotFields, 0u );
        body.linearVelocity = PhysicsBodyLinearVelocity( hotFields, 0u );
        body.terrain = terrain;
        body.boundingRadius = hotFields.boundingRadius[0];
        body.contactEpsilon = 0.05f;
        body.terrainContactThreshold = 0.15f;
        body.restitutionThreshold = 0.1f;

        TerrainContactSweepResult sweep;
        sweep.hit = true;
        sweep.collisionTime = 0.0f;
        sweep.collidedPlane.m_normal = Vector3( 0.0f, 1.0f, 0.0f );
        sweep.collidedPlane.m_distance = 0.0f;

        TerrainContactManifold manifold;
        REQUIRE(
            BuildTerrainContactManifold( body, fixture.colliderStore.Records()[0].shape, 0, sweep, kSolverDt, manifold ) );
        REQUIRE( manifold.pointCount == 2u );
        CHECK_FALSE( manifold.supportsRestingPolicy );
        CHECK( manifold.allowsTangentFriction );
        CHECK( manifold.inhibitsSleep );
        return manifold;
    };

    SolverFixture shoreline;
    shoreline.config.solver.iterations = 1;
    const float gravityStepSpeed = fabsf( shoreline.config.worldForces.gravity ) * kSolverDt;
    const float shorelineResidualSpeed = gravityStepSpeed * 0.35f;
    TerrainContactManifold shorelineManifold = buildUnsupportedTerrainEdge( shoreline, shorelineResidualSpeed );

    PhysicsTerrainStage terrainStage;
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        terrainStage.ReserveSceneCapacity( 1u );
    }

    PreparedTerrainCandidateCommit commit;
    commit.bodyIndex = 0;
    commit.hit = 1;
    commit.hasManifold = 1;
    commit.manifold = shorelineManifold;
    std::array<uint8_t, 1> stageSupported = {};
    std::array<uint8_t, 1> stageInhibited = {};
    terrainStage.CommitCandidate( commit, stageSupported, stageInhibited );
    REQUIRE( terrainStage.GetContactManifolds().size() == 1u );
    CHECK( stageSupported[0] == 0u );
    CHECK( stageInhibited[0] == 1u );

    shoreline.terrainContactManifolds.push_back( shorelineManifold );
    shoreline.Solve();

    REQUIRE( shoreline.solver.GetPersistentContacts().size() == shorelineManifold.pointCount );
    float solvedNormalImpulse = 0.0f;

    for ( const auto& contact : shoreline.solver.GetPersistentContacts() )
    {
        solvedNormalImpulse += contact.accN;
        CHECK( contact.terrainWarmStart > 0.0f );
        CHECK( contact.accN >= 0.0f );
    }

    CHECK( solvedNormalImpulse > 0.0f );
    CHECK( shoreline.solver.GetStats().warmStartedRows == shorelineManifold.pointCount );
    CHECK( shoreline.solver.GetStats().cacheMisses == shorelineManifold.pointCount );
    REQUIRE( shoreline.solver.GetPersistentContactCache().size() == shorelineManifold.pointCount );
    CHECK( shoreline.solver.GetPersistentContactCounts()[0] == shorelineManifold.pointCount );
    CHECK( shoreline.terrainRestApplied[0] == 0u );
    const float shorelineFinalVerticalVelocity = shoreline.bodyStore.HotFields().linearVelocityY[0];
    CHECK( fabsf( shorelineFinalVerticalVelocity ) <= shorelineResidualSpeed * 0.085f );

    const auto& shorelinePipeline = shoreline.solver.GetSideEffects().pipelineRecords;
    const auto
        shorelineIteration = std::find_if( shorelinePipeline.begin(), shorelinePipeline.end(),
                                           [&]( const SkullbonezCore::Physics::PhysicsPipelineRecord& record )
                                           {
                                               return record.stage ==
                                                          SkullbonezCore::Physics::PhysicsPipelineStage::SolverIteration &&
                                                      record.featureId == shorelineManifold.points[0].featureId &&
                                                      record.iteration == 0;
                                           } );
    REQUIRE( shorelineIteration != shorelinePipeline.end() );
    CHECK( shorelineIteration->scalarA == 0.0f );

    SolverFixture cached;
    cached.config.solver.iterations = 1;
    TerrainContactManifold cachedManifold = buildUnsupportedTerrainEdge( cached, shorelineResidualSpeed );
    cached.terrainContactManifolds.push_back( cachedManifold );
    cached.CopySolverStateFrom( shoreline );
    cached.Solve();

    REQUIRE( cached.solver.GetPersistentContacts().size() == cachedManifold.pointCount );

    for ( const auto& contact : cached.solver.GetPersistentContacts() )
    {
        CHECK( contact.terrainWarmStart > 0.0f );
        CHECK( contact.accN >= 0.0f );
    }

    CHECK( cached.solver.GetStats().cachePreviousRows == shorelineManifold.pointCount );
    CHECK( cached.solver.GetStats().cacheHits == cachedManifold.pointCount );
    CHECK( cached.solver.GetStats().cacheMisses == 0 );
    CHECK( cached.solver.GetStats().warmStartedRows == cachedManifold.pointCount );
    REQUIRE( cached.solver.GetPersistentContactCache().size() == cachedManifold.pointCount );
    CHECK( cached.solver.GetPersistentContactCounts()[0] == cachedManifold.pointCount );
    CHECK( cached.terrainRestApplied[0] == 0u );
    const float cachedFinalVerticalVelocity = cached.bodyStore.HotFields().linearVelocityY[0];
    CHECK( fabsf( cachedFinalVerticalVelocity ) <= shorelineResidualSpeed * 0.085f );
    CHECK( fabsf( cachedFinalVerticalVelocity ) < fabsf( shorelineFinalVerticalVelocity ) );

    const auto& cachedPipeline = cached.solver.GetSideEffects().pipelineRecords;
    const auto
        cachedIteration = std::find_if( cachedPipeline.begin(), cachedPipeline.end(),
                                        [&]( const SkullbonezCore::Physics::PhysicsPipelineRecord& record )
                                        {
                                            return record.stage ==
                                                       SkullbonezCore::Physics::PhysicsPipelineStage::SolverIteration &&
                                                   record.featureId == cachedManifold.points[0].featureId &&
                                                   record.iteration == 0;
                                        } );
    REQUIRE( cachedIteration != cachedPipeline.end() );
    CHECK( cachedIteration->scalarA == 0.0f );
}


TEST_CASE( "Persistent contact solver: terrain support variants share row-derived load without first-touch over-push" )
{
    struct StackMeasurement
    {
        std::array<float, 3> verticalVelocity = {};
        float terrainNormalImpulse = 0.0f;
        float firstTouchEstimate = 0.0f;
        float gravityStepSpeed = 0.0f;
    };

    auto measureStack = []( bool supportsRestingPolicy, bool inhibitsSleep, uint32_t featureId )
    {
        SolverFixture fixture;

        fixture.config.solver.iterations = 1;
        fixture.AddBox( Vector3( 0.0f, 1.0f, 0.0f ), 0.0f, false );
        fixture.AddBox( Vector3( 0.0f, 2.98f, 0.0f ), 0.0f, false );
        fixture.AddBox( Vector3( 0.0f, 4.96f, 0.0f ), 0.0f, false );

        const float gravityStepSpeed = fabsf( fixture.config.worldForces.gravity ) * kSolverDt;
        auto hotFields = fixture.bodyStore.MutableHotFields();

        for ( std::size_t bodyIndex = 0; bodyIndex < 3u; ++bodyIndex )
        {
            hotFields.linearVelocityY[bodyIndex] = -gravityStepSpeed;
        }

        fixture.candidatePairs.emplace_back( 0, 1 );
        fixture.candidatePairs.emplace_back( 1, 2 );
        fixture.AddTerrainContactAtOffset( 0, featureId, 0.0f, Vector3( 0.0f, -1.0f, 0.0f ), supportsRestingPolicy,
                                           inhibitsSleep );

        fixture.Solve();

        REQUIRE_FALSE( fixture.solver.GetPersistentContacts().empty() );
        const auto terrainContact = std::find_if( fixture.solver.GetPersistentContacts().begin(),
                                                  fixture.solver.GetPersistentContacts().end(),
                                                  []( const SkullbonezCore::Physics::PersistentContact& contact )
                                                  { return contact.isTerrain; } );

        REQUIRE( terrainContact != fixture.solver.GetPersistentContacts().end() );

        StackMeasurement result;
        result.terrainNormalImpulse = terrainContact->accN;
        result.firstTouchEstimate = terrainContact->terrainWarmStart;
        result.gravityStepSpeed = gravityStepSpeed;
        const auto solvedHotFields = fixture.bodyStore.HotFields();

        for ( std::size_t bodyIndex = 0; bodyIndex < result.verticalVelocity.size(); ++bodyIndex )
        {
            result.verticalVelocity[bodyIndex] = solvedHotFields.linearVelocityY[bodyIndex];
        }

        return result;
    };

    const StackMeasurement full = measureStack( true, false, 601u );
    const StackMeasurement shoreline = measureStack( false, true, 602u );
    const StackMeasurement zero = measureStack( false, false, 603u );

    CHECK( full.firstTouchEstimate > 0.0f );
    CHECK( shoreline.firstTouchEstimate > 0.0f );
    CHECK( zero.firstTouchEstimate > 0.0f );
    CHECK( full.firstTouchEstimate == doctest::Approx( shoreline.firstTouchEstimate ).epsilon( 0.00001 ) );
    CHECK( shoreline.firstTouchEstimate == doctest::Approx( zero.firstTouchEstimate ).epsilon( 0.00001 ) );
    CHECK( full.terrainNormalImpulse > 0.0f );
    CHECK( shoreline.terrainNormalImpulse > 0.0f );
    CHECK( zero.terrainNormalImpulse > 0.0f );
    CHECK( full.terrainNormalImpulse == doctest::Approx( shoreline.terrainNormalImpulse ).epsilon( 0.00001 ) );
    CHECK( shoreline.terrainNormalImpulse == doctest::Approx( zero.terrainNormalImpulse ).epsilon( 0.00001 ) );
    CHECK( fabsf( full.verticalVelocity[2] ) < full.gravityStepSpeed );
    CHECK( fabsf( shoreline.verticalVelocity[2] ) < shoreline.gravityStepSpeed );
    CHECK( fabsf( zero.verticalVelocity[2] ) < zero.gravityStepSpeed );
    CHECK( full.verticalVelocity[2] <= 0.00001f );
    CHECK( shoreline.verticalVelocity[2] <= 0.00001f );
    CHECK( zero.verticalVelocity[2] <= 0.00001f );
    CHECK( full.firstTouchEstimate == doctest::Approx( 0.1f ).epsilon( 0.00001 ) );
    CHECK( full.terrainNormalImpulse == doctest::Approx( 0.852343f ).epsilon( 0.00001 ) );
    CHECK( full.verticalVelocity[0] == doctest::Approx( 0.0f ).epsilon( 0.00001 ) );
    CHECK( full.verticalVelocity[1] == doctest::Approx( -0.300611f ).epsilon( 0.00001 ) );
    CHECK( full.verticalVelocity[2] == doctest::Approx( -0.0232174f ).epsilon( 0.00001 ) );
    CHECK( shoreline.verticalVelocity[0] == doctest::Approx( full.verticalVelocity[0] ).epsilon( 0.00001 ) );
    CHECK( shoreline.verticalVelocity[1] == doctest::Approx( full.verticalVelocity[1] ).epsilon( 0.00001 ) );
    CHECK( shoreline.verticalVelocity[2] == doctest::Approx( full.verticalVelocity[2] ).epsilon( 0.00001 ) );
    CHECK( zero.verticalVelocity[0] == doctest::Approx( full.verticalVelocity[0] ).epsilon( 0.00001 ) );
    CHECK( zero.verticalVelocity[1] == doctest::Approx( full.verticalVelocity[1] ).epsilon( 0.00001 ) );
    CHECK( zero.verticalVelocity[2] == doctest::Approx( full.verticalVelocity[2] ).epsilon( 0.00001 ) );
}


TEST_CASE( "Persistent contact solver: friction cone clamps diagonal tangent impulse" )
{
    float accT1 = 3.0f;
    float accT2 = 4.0f;
    SkullbonezCore::Physics::ContactSolver::ClampFrictionVector( accT1, accT2, 2.0f );
    CHECK( accT1 == doctest::Approx( 1.2f ).epsilon( 0.0001 ) );
    CHECK( accT2 == doctest::Approx( 1.6f ).epsilon( 0.0001 ) );

    float signedT1 = -3.0f;
    float signedT2 = 4.0f;
    SkullbonezCore::Physics::ContactSolver::ClampFrictionVector( signedT1, signedT2, 2.0f );
    CHECK( signedT1 == doctest::Approx( -1.2f ).epsilon( 0.0001 ) );
    CHECK( signedT2 == doctest::Approx( 1.6f ).epsilon( 0.0001 ) );

    SolverFixture fixture;
    fixture.AddDynamicSphere( Vector3( 0.0f, 1.0f, 0.0f ), Vector3( 5.0f, -0.1f, 5.0f ) );
    fixture.AddTerrainContact( 0, 7u, 0.05f );
    fixture.Solve();

    REQUIRE( fixture.solver.GetPersistentContactCache().size() == 1u );
    const PersistentContactCacheEntry& cached = fixture.solver.GetPersistentContactCache()[0];
    const float tangentMagnitude = sqrtf( cached.accT1 * cached.accT1 + cached.accT2 * cached.accT2 );
    const float frictionLimit = fixture.config.material.terrainFrictionCoefficient * cached.accN;

    CHECK( tangentMagnitude > 0.0f );
    CHECK( tangentMagnitude <= frictionLimit + 0.0001f );
}


TEST_CASE( "Persistent contact solver: restitution creates separating terrain velocity" )
{
    SolverFixture fixture;
    fixture.AddDynamicSphere( Vector3( 0.0f, 1.0f, 0.0f ), Vector3( 0.0f, -6.0f, 0.0f ), 0.75f );
    fixture.AddTerrainContact( 0, 99u, 0.0f );
    fixture.Solve();

    REQUIRE( fixture.diagnostics.GetDebugContacts().size() == 1u );
    CHECK( fixture.diagnostics.GetDebugContacts()[0].preSolveClosingSpeed >
           fixture.config.body.contactRestitutionThreshold );

    CHECK( fixture.diagnostics.GetDebugContacts()[0].normalImpulse > 0.0f );
    CHECK( fixture.bodyStore.HotFields().linearVelocityY[0] > 0.0f );
    CHECK( fixture.bodyStore.HotFields().linearVelocityY[0] <= 6.0f * 0.75f + 0.0001f );
}


TEST_CASE( "Persistent contact solver: object restitution applies only to a fresh contact" )
{
    auto buildImpact = []( SolverFixture& fixture )
    {
        constexpr float restitution = 0.75f;
        fixture.config.solver.slop = 0.0f;
        fixture.config.solver.baumgarteBeta = 0.2f;
        fixture.AddDynamicSphere( Vector3( 0.0f, 0.0f, 0.0f ), Vector3(), restitution, true );
        fixture.AddDynamicSphere( Vector3( 0.0f, 1.99f, 0.0f ), Vector3( 0.0f, -6.0f, 0.0f ), restitution );
        fixture.candidatePairs.emplace_back( 0, 1 );
    };

    SolverFixture fresh;
    buildImpact( fresh );
    fresh.Solve();

    REQUIRE( fresh.solver.GetPersistentContactCache().size() == 1u );
    REQUIRE( fresh.diagnostics.GetDebugContacts().size() == 1u );
    CHECK( fresh.diagnostics.GetDebugContacts()[0].preSolveClosingSpeed > fresh.config.body.contactRestitutionThreshold );
    CHECK( fresh.bodyStore.HotFields().linearVelocityY[1] > 0.0f );
    const float freshSeparatingSpeed = fresh.bodyStore.HotFields().linearVelocityY[1];

    SolverFixture persistent;
    buildImpact( persistent );
    persistent.CopySolverStateFrom( fresh );
    persistent.Solve();

    // Invariant: cached normal load marks support carried from the previous
    // frame, so the same high closing speed gets Baumgarte repair but no new
    // restitution energy. Cache reach is pinned separately against the exact
    // resting-footprint classifier rather than inferred from feature shape.
    REQUIRE( persistent.solver.GetStats().cacheHits == 1 );
    REQUIRE( persistent.diagnostics.GetDebugContacts().size() == 1u );
    REQUIRE( persistent.solver.GetPersistentContacts().size() == 1u );
    CHECK( persistent.diagnostics.GetDebugContacts()[0].preSolveClosingSpeed >
           persistent.config.body.contactRestitutionThreshold );
    const float expectedBaumgarteBias = persistent.config.solver.baumgarteBeta *
                                        persistent.solver.GetPersistentContacts()[0].penetration / kSolverDt;
    CHECK( persistent.solver.GetPersistentContacts()[0].bias == doctest::Approx( expectedBaumgarteBias ).epsilon( 0.0001 ) );
    CHECK( persistent.bodyStore.HotFields().linearVelocityY[1] ==
           doctest::Approx( expectedBaumgarteBias ).epsilon( 0.0001 ) );
    CHECK( persistent.bodyStore.HotFields().linearVelocityY[1] < freshSeparatingSpeed );
}


TEST_CASE( "Persistent contact solver: restitution suppression follows exact resting-footprint cache reach" )
{
    constexpr float thinContactRotation = 0.75f;
    constexpr float contactOverlap = 0.02f;
    const float thinContactDistance = 1.0f + cosf( thinContactRotation ) + sinf( thinContactRotation ) - contactOverlap;

    auto buildBoxPair = [&]( SolverFixture& fixture, const Vector3& dynamicPosition )
    {
        fixture.config.solver.slop = 0.0f;
        fixture.config.solver.baumgarteBeta = 0.2f;
        fixture.AddBox( Vector3( 0.0f, 0.0f, 0.0f ), 0.0f, true );
        fixture.AddBox( dynamicPosition, thinContactRotation, false );
        fixture.candidatePairs.emplace_back( 0, 1 );
    };

    SolverFixture verticalEdge;
    buildBoxPair( verticalEdge, Vector3( 0.0f, thinContactDistance, -0.5f ) );
    verticalEdge.Solve();

    REQUIRE_FALSE( verticalEdge.solver.GetPersistentContacts().empty() );
    CHECK( std::none_of( verticalEdge.solver.GetPersistentContacts().begin(),
                         verticalEdge.solver.GetPersistentContacts().end(),
                         []( const SkullbonezCore::Physics::PersistentContact& contact )
                         { return contact.supportsRestingPolicy; } ) );
    CHECK( verticalEdge.solver.GetPersistentContactCache().empty() );

    SolverFixture lateralFresh;
    buildBoxPair( lateralFresh, Vector3( 0.0f, -0.5f, thinContactDistance ) );
    lateralFresh.Solve();

    REQUIRE_FALSE( lateralFresh.solver.GetPersistentContacts().empty() );
    CHECK( std::all_of( lateralFresh.solver.GetPersistentContacts().begin(),
                        lateralFresh.solver.GetPersistentContacts().end(),
                        []( const SkullbonezCore::Physics::PersistentContact& contact )
                        { return contact.supportsRestingPolicy; } ) );
    REQUIRE_FALSE( lateralFresh.solver.GetPersistentContactCache().empty() );

    SolverFixture lateralPersistent;
    buildBoxPair( lateralPersistent, Vector3( 0.0f, -0.5f, thinContactDistance ) );
    lateralPersistent.bodyStore.MutableHotFields().linearVelocityZ[1] = -6.0f;
    lateralPersistent.CopySolverStateFrom( lateralFresh );
    lateralPersistent.Solve();

    REQUIRE( lateralPersistent.solver.GetStats().cacheHits > 0 );
    const auto reusedRow = std::find_if( lateralPersistent.solver.GetPersistentContacts().begin(),
                                         lateralPersistent.solver.GetPersistentContacts().end(),
                                         []( const SkullbonezCore::Physics::PersistentContact& contact )
                                         { return contact.warmStarted; } );
    REQUIRE( reusedRow != lateralPersistent.solver.GetPersistentContacts().end() );
    const float expectedBaumgarteBias = lateralPersistent.config.solver.baumgarteBeta * reusedRow->penetration / kSolverDt;
    CHECK( reusedRow->bias == doctest::Approx( expectedBaumgarteBias ).epsilon( 0.0001 ) );

    // Invariant: `supportsRestingPolicy`, not edge/corner vocabulary, defines
    // BV1 reach. Vertical box edge-only support is excluded; this lateral thin
    // box contact is admitted and suppresses restitution after cached load.
}


TEST_CASE( "Property invariant: friction and restitution outputs stay bounded [seed 0x16C0111D]" )
{
    SkullbonezTests::FixedSeed random( 0x16C0111Du );
    constexpr int kContactSamples = 12;

    // Invariant: tangent impulse magnitude never exceeds its Coulomb budget,
    // and terrain bounce never returns more normal speed than restitution allows.
    // Why: each sample runs the complete persistent row solver. Twelve spread
    // values preserve a bounded property corpus without multiplying the coverage
    // collector's instruction-tracing cost into a multi-minute per-case outlier.

    for ( int sample = 0; sample < kContactSamples; ++sample )
    {
        float tangent1 = random.Float( -20.0f, 20.0f );
        float tangent2 = random.Float( -20.0f, 20.0f );
        const float frictionLimit = random.Float( 0.0f, 8.0f );
        SkullbonezCore::Physics::ContactSolver::ClampFrictionVector( tangent1, tangent2, frictionLimit );
        CHECK( sqrtf( tangent1 * tangent1 + tangent2 * tangent2 ) <= frictionLimit + 0.00002f );

        const float closingSpeed = random.Float( 0.5f, 12.0f );
        const float restitution = random.Float( 0.0f, 1.0f );
        SolverFixture fixture;
        fixture.config.body.contactRestitutionThreshold = 0.0f;
        fixture.AddDynamicSphere( Vector3( 0.0f, 1.0f, 0.0f ), Vector3( 0.0f, -closingSpeed, 0.0f ), restitution );
        fixture.AddTerrainContact( 0, static_cast<uint32_t>( sample + 1 ), 0.0f );
        fixture.Solve();

        const float outputSpeed = fixture.bodyStore.HotFields().linearVelocityY[0];
        CHECK( outputSpeed >= -0.00002f );
        CHECK( outputSpeed <= closingSpeed * restitution + 0.0001f );
    }
}


TEST_CASE( "Persistent contact solver: a box gains sleep support only after toppling from its edge" )
{
    constexpr float edgeRotation = 0.75f;
    constexpr float contactOverlap = 0.02f;
    const float edgeContactHeight = 1.0f + cosf( edgeRotation ) + sinf( edgeRotation ) - contactOverlap;

    SolverFixture edge;
    edge.AddBox( Vector3( 0.0f, 0.0f, 0.0f ), 0.0f, true );

    // Why: derive the center height from the tilted cube's vertical support so
    // the requested overlap is real; a fixed low center deeply interpenetrates
    // the boxes and lets tiny code-generation shifts select a four-point face.
    edge.AddBox( Vector3( 0.0f, edgeContactHeight, -0.5f ), edgeRotation, false );
    ObjectContactBodyView lowerView;
    lowerView.position = PhysicsBodyPosition( edge.bodyStore.HotFields(), 0u );
    lowerView.orientation = PhysicsBodyOrientation( edge.bodyStore.HotFields(), 0u );
    ObjectContactBodyView upperView;
    upperView.position = PhysicsBodyPosition( edge.bodyStore.HotFields(), 1u );
    upperView.orientation = PhysicsBodyOrientation( edge.bodyStore.HotFields(), 1u );
    ObjectContactManifold edgeManifold;
    REQUIRE( BuildObjectContactManifold( lowerView, edge.colliderStore.Records()[0].shape, upperView,
                                         edge.colliderStore.Records()[1].shape, 0, 1, edge.config.body.contactEpsilon,
                                         edgeManifold ) );

    REQUIRE( edgeManifold.pointCount <= 2u );
    CHECK( fabsf( edgeManifold.normal.y ) > 0.25f );
    edge.candidatePairs.emplace_back( 0, 1 );
    edge.Solve();

    REQUIRE_FALSE( edge.solver.GetPersistentContacts().empty() );
    CHECK( edge.solver.GetPersistentContactCounts()[1] > 0u );
    CHECK( edge.solver.GetPersistentRestingContactCounts()[1] == 0u );
    CHECK( edge.sleepSupportEdges.empty() );

    SolverFixture face;
    face.AddBox( Vector3( 0.0f, 0.0f, 0.0f ), 0.0f, true );
    face.AddBox( Vector3( 0.0f, 2.0f - contactOverlap, 0.0f ), 0.0f, false );
    face.candidatePairs.emplace_back( 0, 1 );
    face.Solve();

    REQUIRE_FALSE( face.solver.GetPersistentContacts().empty() );
    CHECK( face.solver.GetPersistentRestingContactCounts()[1] > 0u );
    CHECK( face.sleepSupportEdges.size() == 1u );
}
