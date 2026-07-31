//
// File: SkullbonezTests/TestPersistentContactSolver.cpp
// Purpose:
//   Lock direct behavioral coverage for persistent contact solver rows.
//
// Summary:
//   Most tests feed one deterministic terrain manifold directly into
//   PhysicsContactSolverStage. The box sleep test additionally exercises exact
//   object narrowphase so support classification is checked on real manifold
//   geometry without running a full PhysicsEngine frame. Convergence cases pin
//   the diagnostic cap, replay exclusion, and a normal-row saturation cause.
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
//   Convergence trace: Bounded per-iteration attribution of the solver's
//     stopping metric.
//
// Invariants:
//   - The fixture always bypasses broadphase. Terrain cases own their exact row;
//     object cases deliberately use exact narrowphase before solver ingestion.
//   - Static fixed lists mirror runtime storage and avoid allocating
//     SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS records on the doctest stack.
//   - Raw stamped settings normalize once at the solver boundary; direct value
//     tests pin every lower and upper bound used by contact rows.
//   - Diagnostic samples observe solver work but never enter replay state.
//   - Full and count-only pipeline lanes produce identical logical event counts
//     and byte-identical body writeback; only full mode retains payload rows.
//
// Related:
//   - SkullbonezSource/Physics/PersistentContactSolver.cpp
//   - SkullbonezSource/Physics/TerrainContactManifold.h
//   - Agentic/Reports/2026-07-31/pre-536-physics-oracle-restoration.md
//   - Agentic/Reports/2026-07-29/persistent-contact-convergence-early-out-ce1.md
//   - Agentic/Reports/behavioral_test_depth_closure_20260711.md
//

#include "../ThirdPtySource/doctest/doctest.h"
#include "TestColliderStoreFixtures.h"
#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#include "TestFixedSeed.h"
#include "../SkullbonezSource/Physics/PhysicsTimestep.h"

#include "../SkullbonezSource/Core/Common.h"
#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/Physics/BoundingBox.h"
#include "../SkullbonezSource/Physics/BoundingSphere.h"
#include "../SkullbonezSource/Physics/BuoyancySystem.h"
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
using SkullbonezCore::Physics::BuoyancyBodyFacts;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderShapeKind;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::MAX_SLEEP_SUPPORT_EDGES;
using SkullbonezCore::Physics::ObjectContactBodyView;
using SkullbonezCore::Physics::ObjectContactManifold;
using SkullbonezCore::Physics::PersistentContactCacheEntry;
using SkullbonezCore::Physics::PersistentContactSolveTransaction;
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
        (void)SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliderStore, collider, shape );

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
        (void)SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliderStore, collider, shape );

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

    void CopySolverStateFrom( const SolverFixture& source )
    {
        PhysicsSolverSnapshot snapshot;
        source.solver.CaptureReplayState( snapshot );
        solver.RestoreReplayState( snapshot );
    }

    void Solve( bool collectConvergenceDiagnostics = true, bool retainPipelineRecords = true )
    {
        worldForces.gravity = config.worldForces.gravity;
        diagnostics.SetPipelineTraceFullRecordConsumerActive( retainPipelineRecords );
        diagnostics.BeginStep( bodyStore.Count() );
        auto policy = PhysicsContactSolverStage::ResolveStepPolicy( config, worldForces );
        policy.collectConvergenceDiagnostics = collectConvergenceDiagnostics;
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


TEST_CASE( "Pending gameplay impulse matches the contact path for a rotated anisotropic box" *
           doctest::should_fail() )
{
    SolverFixture fixture;
    const Vector3 halfExtents( 1.0f, 2.0f, 3.0f );
    const CollisionShape shape( BoundingBox( halfExtents, Vector3( 0.0f, 0.0f, 0.0f ) ) );
    const Vector3 rotationalInertia( 2.0f, 5.0f, 9.0f );

    PhysicsBodyCreateRecord body;
    body.cold.mass = 4.0f;
    body.cold.rotationalInertia = rotationalInertia;
    body.cold.angularVelocityLimit = 1000.0f;
    body.cold.usesWorldInertia = true;
    body.hot.orientation.RotateAboutAxis( Vector3( 0.0f, 0.0f, 1.0f ), 0.65f );
    body.hot.inverseMass = 1.0f / body.cold.mass;
    body.hot.inverseRotationalInertia =
        Vector3( 1.0f / rotationalInertia.x, 1.0f / rotationalInertia.y, 1.0f / rotationalInertia.z );
    body.hot.boundingRadius = SkullbonezCore::Math::CollisionDetection::GetShapeBoundingRadius( shape );
    const auto bodyHandle = fixture.bodyStore.CreateBodyRecord( body );
    REQUIRE( bodyHandle.IsValid() );

    ColliderRecord collider;
    collider.body = bodyHandle;
    collider.shapeKind = ColliderShapeKind::Box;
    collider.boundingRadius = body.hot.boundingRadius;
    REQUIRE( SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( fixture.colliderStore, collider, shape )
                 .IsValid() );

    const Vector3 worldImpulse( 3.0f, 5.0f, -2.0f );
    const Vector3 worldApplicationOffset( 0.75f, -0.4f, 1.1f );
    REQUIRE( fixture.bodyStore.SetPendingBodyImpulse( bodyHandle, worldImpulse, worldApplicationOffset ) );

    PhysicsWorldForces noForces;
    noForces.angularDragMultiplier = 0.0f;
    const BuoyancyBodyFacts noBuoyancy;
    REQUIRE( fixture.bodyStore.ApplyForces( noForces, fixture.colliderStore, {}, noBuoyancy, 0, kSolverDt ) );
    const Vector3 gameplayAngularVelocity =
        SkullbonezCore::Physics::PhysicsBodyAngularVelocity( fixture.bodyStore.HotFields(), 0u );

    PersistentContactSolveTransaction contactPath;
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        contactPath.ReserveSceneCapacity( 1u );
    }
    contactPath.ResetBodies( 1u );
    auto& contactBody = contactPath.Body( 0u );
    contactBody.invInertia = body.hot.inverseRotationalInertia;
    contactBody.orientation = body.hot.orientation.GetOrientationMatrix();
    contactBody.useWorldInertia = true;

    SkullbonezCore::Physics::PersistentContact contact;
    contact.bodyA = 0;
    contact.bodyB = -1;
    contact.rA = worldApplicationOffset;

    // Body A receives the impulse opposite the solver-row direction. Negating
    // the row impulse therefore applies the same world impulse as gameplay.
    contactPath.ApplyImpulse( contact, -worldImpulse );
    const Vector3 contactAngularVelocity = contactPath.Body( 0u ).angularVelocity;

    // AI0 red characterization: the pending path currently divides the
    // world-space torque by body-space diagonal inertia without rotating it.
    // AI2 removes should_fail only after gameplay shares this contact arithmetic.
    CHECK( gameplayAngularVelocity.x == doctest::Approx( contactAngularVelocity.x ) );
    CHECK( gameplayAngularVelocity.y == doctest::Approx( contactAngularVelocity.y ) );
    CHECK( gameplayAngularVelocity.z == doctest::Approx( contactAngularVelocity.z ) );
}


TEST_CASE( "Persistent contact solver: convergence diagnostics stay bounded and outside replay state" )
{
    SkullbonezCore::Physics::PersistentContactConvergenceTrace trace;
    SkullbonezCore::Physics::PersistentContactIterationDiagnostics sample;

    for ( std::size_t iteration = 0u; iteration < SkullbonezCore::Physics::PersistentContactConvergenceTrace::CAPACITY + 3u;
          ++iteration )
    {
        sample.iteration = static_cast<int>( iteration + 1u );
        trace.Append( sample );
    }

    REQUIRE( trace.Samples().size() == SkullbonezCore::Physics::PersistentContactConvergenceTrace::CAPACITY );
    CHECK( trace.DroppedIterationCount() == 3u );
    CHECK( trace.Samples().front().iteration == 1 );
    CHECK( trace.Samples().back().iteration ==
           static_cast<int>( SkullbonezCore::Physics::PersistentContactConvergenceTrace::CAPACITY ) );

    SolverFixture source;
    source.AddDynamicSphere( Vector3( 0.0f, 1.0f, 0.0f ), Vector3( 0.0f, -0.1f, 0.0f ) );
    source.AddTerrainContact( 0, 42u, 0.05f );
    source.Solve();
    REQUIRE_FALSE( source.solver.GetConvergenceTrace().Samples().empty() );

    PhysicsSolverSnapshot snapshot;
    source.solver.CaptureReplayState( snapshot );
    SolverFixture restored;
    restored.AddDynamicSphere( Vector3( 0.0f, 1.0f, 0.0f ), Vector3( 0.0f, -0.1f, 0.0f ) );
    restored.AddTerrainContact( 0, 7u, 0.05f );
    restored.Solve();
    REQUIRE_FALSE( restored.solver.GetConvergenceTrace().Samples().empty() );
    restored.solver.RestoreReplayState( snapshot );

    // Invariant: convergence rows describe one live solve; replay owns neither
    // their values nor their retention. Restore clears any stale live trace.
    CHECK( restored.solver.GetConvergenceTrace().Samples().empty() );
    CHECK( restored.solver.GetConvergenceTrace().DroppedIterationCount() == 0u );

    SolverFixture disabled;
    disabled.AddDynamicSphere( Vector3( 0.0f, 1.0f, 0.0f ), Vector3( 0.0f, -0.1f, 0.0f ) );
    disabled.AddTerrainContact( 0, 9u, 0.05f );
    disabled.Solve( false );
    CHECK( disabled.solver.GetConvergenceTrace().Samples().empty() );
}

TEST_CASE( "Persistent contact solver: count-only specialization matches the full-record event count" )
{
    SolverFixture full;
    full.AddDynamicSphere( Vector3( 0.0f, 1.0f, 0.0f ), Vector3( 0.1f, -0.2f, 0.3f ) );
    full.AddTerrainContact( 0, 91u, 0.08f );
    full.Solve( true, true );

    const auto& fullEffects = full.solver.GetSideEffects();
    REQUIRE_FALSE( fullEffects.pipelineRecords.empty() );
    CHECK( fullEffects.pipelineEventCount == 0u );
    const std::size_t fullEventCount = fullEffects.pipelineRecords.size();
    const auto fullHotFields = full.bodyStore.HotFields();
    const Vector3 fullPosition = PhysicsBodyPosition( fullHotFields, 0u );
    const Vector3 fullLinearVelocity = PhysicsBodyLinearVelocity( fullHotFields, 0u );
    const Vector3 fullAngularVelocity = PhysicsBodyAngularVelocity( fullHotFields, 0u );

    SolverFixture countOnly;
    countOnly.AddDynamicSphere( Vector3( 0.0f, 1.0f, 0.0f ), Vector3( 0.1f, -0.2f, 0.3f ) );
    countOnly.AddTerrainContact( 0, 91u, 0.08f );
    countOnly.Solve( true, false );

    const auto& countOnlyEffects = countOnly.solver.GetSideEffects();
    CHECK( countOnlyEffects.pipelineRecords.empty() );
    CHECK( countOnlyEffects.pipelineEventCount == fullEventCount );
    const auto countOnlyHotFields = countOnly.bodyStore.HotFields();
    CHECK( PhysicsBodyPosition( countOnlyHotFields, 0u ) == fullPosition );
    CHECK( PhysicsBodyLinearVelocity( countOnlyHotFields, 0u ) == fullLinearVelocity );
    CHECK( PhysicsBodyAngularVelocity( countOnlyHotFields, 0u ) == fullAngularVelocity );
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

    SolverFixture second;
    second.AddDynamicSphere( Vector3( 0.0f, 1.0f, 0.0f ), Vector3( 0.0f, -0.1f, 0.0f ) );
    second.AddTerrainContact( 0, 42u, 0.05f );
    second.CopySolverStateFrom( first );
    second.Solve();

    CHECK( second.solver.GetStats().cachePreviousRows == 1 );
    CHECK( second.solver.GetStats().cacheHits == 1 );
    CHECK( second.solver.GetStats().warmStartedRows == 1 );
    CHECK( second.solver.GetStats().solverIterations <= first.solver.GetStats().solverIterations );
}


TEST_CASE( "Persistent contact solver: full terrain seed prevents first-frame resting sink" )
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
    CHECK( fixture.solver.GetStats().warmStartedRows == 1 );
    CHECK( fixture.terrainRestApplied[0] == 1u );
    CHECK( fixture.bodyStore.HotFields().linearVelocityY[0] >= -0.00001f );
}


TEST_CASE( "Persistent contact solver: shoreline seed prevents one-frame edge bob without becoming cached support" )
{
    constexpr float shorelineScale = 0.35f;
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
    const float shorelineResidualSpeed = gravityStepSpeed * shorelineScale;
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
    const float expectedShorelineImpulse = shoreline.bodyStore.Records()[0].mass * shorelineResidualSpeed;
    float shorelineWarmStart = 0.0f;

    for ( const auto& contact : shoreline.solver.GetPersistentContacts() )
    {
        shorelineWarmStart += contact.terrainWarmStart;
        CHECK( contact.terrainWarmStart ==
               doctest::Approx( expectedShorelineImpulse / static_cast<float>( shorelineManifold.pointCount ) )
                   .epsilon( 0.00001 ) );
    }

    CHECK( shorelineWarmStart == doctest::Approx( expectedShorelineImpulse ).epsilon( 0.00001 ) );
    CHECK( shoreline.solver.GetStats().warmStartedRows == shorelineManifold.pointCount );
    CHECK( shoreline.solver.GetPersistentContactCache().empty() );
    CHECK( shoreline.terrainRestApplied[0] == 0u );
    const float shorelineFinalVerticalVelocity = shoreline.bodyStore.HotFields().linearVelocityY[0];
    CHECK( fabsf( shorelineFinalVerticalVelocity ) <= 0.0002f );

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
    CHECK( fabsf( shorelineIteration->scalarA ) <= 0.00001f );

    SolverFixture unseeded;
    unseeded.config.solver.iterations = 1;
    TerrainContactManifold unseededManifold = buildUnsupportedTerrainEdge( unseeded, shorelineResidualSpeed );
    unseededManifold.inhibitsSleep = false;
    unseeded.terrainContactManifolds.push_back( unseededManifold );
    unseeded.Solve();

    REQUIRE( unseeded.solver.GetPersistentContacts().size() == unseededManifold.pointCount );

    for ( const auto& contact : unseeded.solver.GetPersistentContacts() )
    {
        CHECK( contact.terrainWarmStart == 0.0f );
    }

    CHECK( unseeded.solver.GetStats().warmStartedRows == 0 );

    const auto& unseededPipeline = unseeded.solver.GetSideEffects().pipelineRecords;
    const auto
        unseededIteration = std::find_if( unseededPipeline.begin(), unseededPipeline.end(),
                                          [&]( const SkullbonezCore::Physics::PhysicsPipelineRecord& record )
                                          {
                                              return record.stage ==
                                                         SkullbonezCore::Physics::PhysicsPipelineStage::SolverIteration &&
                                                     record.featureId == unseededManifold.points[0].featureId &&
                                                     record.iteration == 0;
                                          } );
    REQUIRE( unseededIteration != unseededPipeline.end() );
    CHECK( unseededIteration->scalarA == 0.0f );
    const float unseededFinalVerticalVelocity = unseeded.bodyStore.HotFields().linearVelocityY[0];
    CHECK( unseededFinalVerticalVelocity == doctest::Approx( -shorelineResidualSpeed ).epsilon( 0.00001 ) );
    CHECK( fabsf( shorelineFinalVerticalVelocity ) < fabsf( unseededFinalVerticalVelocity ) );
}


TEST_CASE( "Persistent contact solver: terrain seed strength bounds one-iteration three-box stack sink" )
{
    struct StackMeasurement
    {
        std::array<float, 3> verticalVelocity = {};
        float terrainWarmStart = 0.0f;
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
        result.terrainWarmStart = terrainContact->terrainWarmStart;
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

    CHECK( full.terrainWarmStart > 0.0f );
    CHECK( shoreline.terrainWarmStart == doctest::Approx( full.terrainWarmStart * 0.35f ).epsilon( 0.00001 ) );
    CHECK( zero.terrainWarmStart == 0.0f );
    CHECK( full.verticalVelocity[0] == doctest::Approx( 0.0f ).epsilon( 0.00001 ) );
    CHECK( full.verticalVelocity[1] == doctest::Approx( -0.249842f ).epsilon( 0.00001 ) );
    CHECK( full.verticalVelocity[2] == doctest::Approx( 0.011951f ).epsilon( 0.00001 ) );
    CHECK( shoreline.verticalVelocity[0] == doctest::Approx( 0.0f ).epsilon( 0.00001 ) );
    CHECK( shoreline.verticalVelocity[1] == doctest::Approx( -0.291092f ).epsilon( 0.00001 ) );
    CHECK( shoreline.verticalVelocity[2] == doctest::Approx( -0.0166234f ).epsilon( 0.00001 ) );
    CHECK( zero.verticalVelocity[0] == doctest::Approx( 0.0f ).epsilon( 0.00001 ) );
    CHECK( zero.verticalVelocity[1] == doctest::Approx( -0.313303f ).epsilon( 0.00001 ) );
    CHECK( zero.verticalVelocity[2] == doctest::Approx( -0.0320095f ).epsilon( 0.00001 ) );
    CHECK( full.verticalVelocity[1] > shoreline.verticalVelocity[1] );
    CHECK( shoreline.verticalVelocity[1] > zero.verticalVelocity[1] );
    CHECK( full.verticalVelocity[2] > shoreline.verticalVelocity[2] );
    CHECK( shoreline.verticalVelocity[2] > zero.verticalVelocity[2] );
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
    const float terrainWarmStart = fixture.bodyStore.Records()[0].mass * fabsf( fixture.config.worldForces.gravity ) *
                                   kSolverDt;

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

    REQUIRE( fixture.diagnostics.GetDebugContacts().size() == 1u );
    CHECK( fixture.diagnostics.GetDebugContacts()[0].preSolveClosingSpeed >
           fixture.config.body.contactRestitutionThreshold );

    CHECK( fixture.diagnostics.GetDebugContacts()[0].normalImpulse > 0.0f );
    CHECK( fixture.bodyStore.HotFields().linearVelocityY[0] > 0.0f );
    CHECK( fixture.bodyStore.HotFields().linearVelocityY[0] <= 6.0f * 0.75f + 0.0001f );
}


TEST_CASE( "Persistent contact solver: object support chain exposes honest normal-row non-convergence" )
{
    constexpr float contactOverlap = 0.02f;
    constexpr int dynamicBoxCount = 3;
    SolverFixture fixture;
    fixture.config.solver.slop = 0.0f;
    fixture.config.solver.baumgarteBeta = 0.2f;
    fixture.AddBox( Vector3( 0.0f, 0.0f, 0.0f ), 0.0f, true );

    for ( int bodyIndex = 1; bodyIndex <= dynamicBoxCount; ++bodyIndex )
    {
        const float height = static_cast<float>( bodyIndex ) * ( 2.0f - contactOverlap );
        fixture.AddBox( Vector3( 0.0f, height, 0.0f ), 0.0f, false );
        fixture.candidatePairs.emplace_back( bodyIndex - 1, bodyIndex );
    }

    fixture.Solve();

    const auto samples = fixture.solver.GetConvergenceTrace().Samples();
    REQUIRE( samples.size() == static_cast<std::size_t>( fixture.config.solver.iterations ) );
    CHECK( fixture.solver.GetConvergenceTrace().DroppedIterationCount() == 0u );
    CHECK( samples.front().stoppingImpulseDeltaSq > samples.back().stoppingImpulseDeltaSq );

    const auto& finalIteration = samples.back();

    // Invariant: this aligned object-only chain has negligible tangent demand.
    // If it reaches the configured cap, a single normal row still exceeds the
    // broad stopping threshold; the cap is honest non-convergence, not merely
    // the sum of many individually quiet rows or stale terrain/friction work.
    CHECK( finalIteration.iteration == fixture.config.solver.iterations );
    CHECK( finalIteration.stoppingImpulseDeltaSq > 1.0e-6f );
    CHECK( finalIteration.maxRowImpulseDeltaSq > 1.0e-6f );
    CHECK( finalIteration.maxRowNormalImpulseDeltaSq > 1.0e-6f );
    CHECK( finalIteration.maxRowTangentImpulseDeltaSq < 1.0e-6f );
    CHECK( finalIteration.normalImpulseDeltaSq ==
           doctest::Approx( finalIteration.stoppingImpulseDeltaSq ).epsilon( 0.00001 ) );
    CHECK( finalIteration.tangentImpulseDeltaSq < 1.0e-6f );
    CHECK( finalIteration.normalChangedRowCount > 0 );
    CHECK( finalIteration.normalImpulseDeltaSq > finalIteration.tangentImpulseDeltaSq * 1000.0f );
    CHECK_FALSE( finalIteration.maxRowIsTerrain );
    CHECK( finalIteration.maxRowBodyA >= 0 );
    CHECK( finalIteration.maxRowBodyB >= 0 );
    CHECK( fixture.solver.GetStats().solverIterations == fixture.config.solver.iterations );
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
