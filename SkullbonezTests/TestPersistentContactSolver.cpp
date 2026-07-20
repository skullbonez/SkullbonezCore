//
// File: SkullbonezTests/TestPersistentContactSolver.cpp
// Purpose:
//   Lock direct behavioral coverage for persistent contact solver rows.
//
// Summary:
//   Most tests feed one deterministic terrain manifold directly into
//   PersistentContactSolver. The box sleep test additionally exercises exact
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
#include "../SkullbonezSource/Physics/PhysicsWorld.h"
#include "../SkullbonezSource/Physics/SleepIslandSystem.h"
#include "../SkullbonezSource/Physics/TerrainContactManifold.h"

#include <array>
#include <cmath>
#include <vector>

using SkullbonezCore::Math::CollisionDetection::BoundingBox;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::CollisionShape;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::BuildObjectContactManifold;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderRecordList;
using SkullbonezCore::Physics::ColliderShapeKind;
using SkullbonezCore::Physics::ObjectContactBodyView;
using SkullbonezCore::Physics::ObjectContactManifold;
using SkullbonezCore::Physics::PersistentContactCacheEntry;
using SkullbonezCore::Physics::PersistentContactSolver;
using SkullbonezCore::Physics::PersistentContactSolverContext;
using SkullbonezCore::Physics::PersistentContactSolverSideEffects;
using SkullbonezCore::Physics::PhysicsBodyCreateRecord;
using SkullbonezCore::Physics::PhysicsBodyOrientation;
using SkullbonezCore::Physics::PhysicsBodyPosition;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsDebugContact;
using SkullbonezCore::Physics::PhysicsWorld;
using SkullbonezCore::Physics::MAX_SLEEP_SUPPORT_EDGES;
using SkullbonezCore::Physics::SolverBodyState;
using SkullbonezCore::Physics::TerrainContactManifold;

namespace
{
constexpr float kSolverDt = PHYSICS_FIXED_DT;

PhysicsBodyStore& TestBodyStore()
{
    // Why: physics fixed lists own SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS rows. Static storage matches
    // runtime ownership and keeps this focused fixture off the doctest stack.
    static PhysicsBodyStore store;
    store.Clear();
    return store;
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
    PhysicsBodyStore& bodyStore;
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
    std::array<uint8_t, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> terrainRestApplied = {};
    std::vector<uint8_t> sleepSupportedThisFrame;
    PersistentContactSolverSideEffects sideEffects;
    SkullbonezCore::Physics::PhysicsRuntimeSettings config;
    PersistentContactSolver solver;

    SolverFixture() : bodyStore( TestBodyStore() ), colliderRecords( TestColliderRecords() )
    {
        // Runtime support edges are construction-reserved to their fixed cap;
        // mirror that owner precondition so focused solver tests cannot hide a
        // growth path behind their smaller fixture vector.
        sleepSupportEdges.reserve( MAX_SLEEP_SUPPORT_EDGES );
        config.execution.parallel = false;
        config.worldForces.gravity = -30.0f;
        config.material.terrainFrictionCoefficient = 0.2f;
        config.body.contactRestitutionThreshold = 0.1f;
        config.terrain.slop = 0.0f;
        config.terrain.baumgarteBeta = 0.25f;
        config.terrain.maxBaumgarteBias = 6.0f;
        config.solver.iterations = 12;
    }

    void AddDynamicSphere( const Vector3& position, const Vector3& linearVelocity, float restitution = 0.0f )
    {
        const float radius = 1.0f;
        const float mass = 2.0f;
        const float inertia = 0.4f * mass * radius * radius;

        PhysicsBodyCreateRecord body;
        body.hot.position = position;
        body.hot.linearVelocity = linearVelocity;
        body.cold.rotationalInertia = Vector3( inertia, inertia, inertia );
        body.hot.inverseRotationalInertia = Vector3( 1.0f / inertia, 1.0f / inertia, 1.0f / inertia );
        body.cold.mass = mass;
        body.hot.inverseMass = 1.0f / mass;
        body.hot.boundingRadius = radius;
        (void)bodyStore.CreateBodyRecord( body );

        ColliderRecord collider;
        collider.shape = CollisionShape( BoundingSphere( radius, Vector3( 0.0f, 0.0f, 0.0f ), 0.0f ) );
        collider.shapeKind = ColliderShapeKind::Sphere;
        collider.boundingRadius = radius;
        collider.restitution = restitution;
        collider.friction = config.material.terrainFrictionCoefficient;
        colliderRecords.push_back( collider );

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
        body.hot.inverseRotationalInertia =
            isFixed ? Vector3() : Vector3( 1.0f / inertia, 1.0f / inertia, 1.0f / inertia );
        body.cold.mass = mass;
        body.hot.inverseMass = isFixed ? 0.0f : 1.0f / mass;
        body.hot.boundingRadius = sqrtf( 3.0f );
        body.hot.fixed = isFixed;
        body.cold.usesWorldInertia = true;
        (void)bodyStore.CreateBodyRecord( body );

        ColliderRecord collider;
        collider.shape = CollisionShape( BoundingBox( halfExtents, Vector3() ) );
        collider.shapeKind = ColliderShapeKind::Box;
        collider.boundingRadius = body.hot.boundingRadius;
        collider.friction = config.material.terrainFrictionCoefficient;
        colliderRecords.push_back( collider );

        sleepState.assign( static_cast<std::size_t>( bodyStore.Count() ), 0u );
        sleepSupportedThisFrame.assign( static_cast<std::size_t>( bodyStore.Count() ), 0u );
    }

    void AddTerrainContact( int bodyIndex, uint32_t featureId, float penetration )
    {
        const auto hotFields = bodyStore.HotFields();
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
        manifold.points[0].rA = Vector3( 0.0f, -hotFields.boundingRadius[static_cast<std::size_t>( bodyIndex )], 0.0f );
        manifold.points[0].point =
            PhysicsBodyPosition( hotFields, static_cast<std::size_t>( bodyIndex ) ) + manifold.points[0].rA;
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
                                               bodyStore,
                                               bodyStore.MutableRecords(),
                                               bodyStore.MutableHotFields(),
                                               { colliderRecords.data(), colliderRecords.size() },
                                               bodyStore.Count(),
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

    auto policy = PersistentContactSolver::ResolveStepPolicy( settings );
    CHECK( policy.objectSlop == 0.0f );
    CHECK( policy.objectBaumgarteBeta == 0.0f );
    CHECK( policy.objectPositionCorrectionPercent == 0.0f );
    CHECK( policy.terrainSlop == 0.0f );
    CHECK( policy.terrainBaumgarteBeta == 0.0f );
    CHECK( policy.maxBaumgarteBias == 0.0f );
    CHECK( policy.iterations == 1 );

    settings.solver.slop = 0.15f;
    settings.solver.baumgarteBeta = 0.25f;
    settings.solver.positionCorrectionPercent = 1.5f;
    settings.solver.iterations = 19;
    settings.terrain.slop = 0.45f;
    settings.terrain.baumgarteBeta = 0.55f;
    settings.terrain.maxBaumgarteBias = 3.0f;
    policy = PersistentContactSolver::ResolveStepPolicy( settings );
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

    float signedT1 = -3.0f;
    float signedT2 = 4.0f;
    SkullbonezCore::Physics::ContactSolver::ClampFrictionVector( signedT1, signedT2, 2.0f );
    CHECK( signedT1 == doctest::Approx( -1.2f ).epsilon( 0.0001 ) );
    CHECK( signedT2 == doctest::Approx( 1.6f ).epsilon( 0.0001 ) );

    SolverFixture fixture;
    fixture.AddDynamicSphere( Vector3( 0.0f, 1.0f, 0.0f ), Vector3( 5.0f, -0.1f, 5.0f ) );
    fixture.AddTerrainContact( 0, 7u, 0.05f );
    fixture.Solve();

    REQUIRE( fixture.persistentContactCache.size() == 1u );
    const PersistentContactCacheEntry& cached = fixture.persistentContactCache[0];
    const float tangentMagnitude = sqrtf( cached.accT1 * cached.accT1 + cached.accT2 * cached.accT2 );
    const float terrainWarmStart =
        fixture.bodyStore.Records()[0].mass * fabsf( fixture.config.worldForces.gravity ) * kSolverDt;
    const float frictionLimit = fixture.config.material.terrainFrictionCoefficient *
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
    CHECK( fixture.debugContacts[0].preSolveClosingSpeed > fixture.config.body.contactRestitutionThreshold );
    CHECK( fixture.debugContacts[0].normalImpulse > 0.0f );
    CHECK( fixture.bodyStore.HotFields().linearVelocityY[0] > 0.0f );
    CHECK( fixture.bodyStore.HotFields().linearVelocityY[0] <= 6.0f * 0.75f + 0.0001f );
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
    REQUIRE( BuildObjectContactManifold( lowerView,
                                         edge.colliderRecords[0].shape,
                                         upperView,
                                         edge.colliderRecords[1].shape,
                                         0,
                                         1,
                                         edge.config.body.contactEpsilon,
                                         edgeManifold ) );
    REQUIRE( edgeManifold.pointCount <= 2u );
    CHECK( fabsf( edgeManifold.normal.y ) > 0.25f );
    edge.candidatePairs.emplace_back( 0, 1 );
    edge.Solve();

    REQUIRE_FALSE( edge.persistentContacts.empty() );
    CHECK( edge.contactCounts[1] > 0u );
    CHECK( edge.restingContactCounts[1] == 0u );
    CHECK( edge.sleepSupportEdges.empty() );

    SolverFixture face;
    face.AddBox( Vector3( 0.0f, 0.0f, 0.0f ), 0.0f, true );
    face.AddBox( Vector3( 0.0f, 2.0f - contactOverlap, 0.0f ), 0.0f, false );
    face.candidatePairs.emplace_back( 0, 1 );
    face.Solve();

    REQUIRE_FALSE( face.persistentContacts.empty() );
    CHECK( face.restingContactCounts[1] > 0u );
    CHECK( face.sleepSupportEdges.size() == 1u );
}
