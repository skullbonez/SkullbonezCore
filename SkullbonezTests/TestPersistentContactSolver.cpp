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
//   Complete-solve cases measure energy and momentum across sphere, box,
//   friction, bias, local CCD contact intervals, and matching-cache matrices,
//   including planted failures. Stable-support cases pin the boundary between
//   non-energetic terrain overlap correction and restitution-bearing impacts;
//   rolling/spin cases pin the coupled angular-row/tangent-friction ordering.
//   Duplicate-feature cases pin sorted-and-unique cache publication before
//   replay capture applies its transactional restore contract.
//
// Glossary:
//   Contact row: Solver constraint row that applies one normal impulse and two
//     tangent impulses at a contact point.

//   Friction cone: Two tangent impulses clamped as one vector budget so diagonal
//     sliding cannot exceed the authored friction limit.
//   Restitution: Bounce response from a closing contact velocity.

//   Sleep support edge: Directed relationship used later to propagate grounded
//     sleep eligibility through an object stack.
//   Solver step policy: Once-per-solve normalized contact limits shared by
//     object and terrain rows.
//   Convergence trace: Bounded per-iteration record of the maximum per-row
//     stopping metric plus diagnostic summed attribution.
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
//   - Position cleanup reduces each manifold to its deepest row, accumulates the
//     inverse-mass shares per body, and publishes each body position once.
//   - Terrain restitution targets are independent of retained manifold row count;
//     symmetric one-row and four-row impacts agree within sequential-solver residue.
//   - Terrain rest policy preserves quiet residual motion; only the sleep owner
//     zeros velocity after its configurable quiet-frame transition.
//   - Convergence stops on the maximum row delta; diagnostic sums never make
//     independently quiet contacts consume another global iteration.
//   - CCD contact rows scale Baumgarte and constant friction from the dynamic
//     participants' post-impact remainder, not from the whole fixed step;
//     authored-fixed and sleeping solver-static participants contribute no clock.
//   - Closed energy cases disable external work and compare the whole solve;
//     Baumgarte cases name their explicit separation-work allowance instead.
//   - Quiet stable terrain support cancels closing speed without commanding a
//     rebound; unstable overlap and material impacts retain distinct paths.
//   - Rolling resistance projects angular speed into the contact tangent plane;
//     spin resistance owns the normal axis, and both precede sliding friction.
//   - Equal contact-feature keys publish one cache row: lower-bound lookup's
//     first observable impulse survives and unreachable duplicates do not.
//
// Related:
//   - SkullbonezSource/Physics/PersistentContactSolver.cpp
//   - SkullbonezSource/Physics/ContactEnergyOracle.h
//   - SkullbonezSource/Physics/TerrainContactManifold.h
//   - Agentic/Reference/engine-glossary.md
//

#include "../ThirdPtySource/doctest/doctest.h"
#include "TestColliderStoreFixtures.h"
#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#include "TestFixedSeed.h"
#include "../SkullbonezSource/Physics/PhysicsTimestep.h"

#include "../SkullbonezSource/Core/Common.h"
#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/Maths/Quaternion.h"
#include "../SkullbonezSource/Physics/BoundingBox.h"
#include "../SkullbonezSource/Physics/BoundingSphere.h"
#include "../SkullbonezSource/Physics/BuoyancySystem.h"
#include "../SkullbonezSource/Physics/ColliderStore.h"
#include "../SkullbonezSource/Physics/ContactEnergyOracle.h"
#include "../SkullbonezSource/Physics/ContactSolverCommon.h"
#include "../SkullbonezSource/Physics/ObjectContactManifold.h"
#include "../SkullbonezSource/Physics/PersistentContactSolver.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Physics/PhysicsMass.h"
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
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Math::Vector::ZERO_VECTOR;
using SkullbonezCore::Physics::BuildObjectContactManifold;
using SkullbonezCore::Physics::BuildTerrainContactManifold;
using SkullbonezCore::Physics::BuoyancyBodyFacts;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderShapeKind;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::ContactEnergyMeasurement;
using SkullbonezCore::Physics::MAX_SLEEP_SUPPORT_EDGES;
using SkullbonezCore::Physics::ObjectContactBodyView;
using SkullbonezCore::Physics::ObjectContactManifold;
using SkullbonezCore::Physics::PersistentContact;
using SkullbonezCore::Physics::PersistentContactCacheEntry;
using SkullbonezCore::Physics::PersistentContactSolverStepPolicy;
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

namespace SkullbonezCore::Physics
{
// Why: the production transaction keeps row ownership private. This seam lets
// the test inject exact face rows while still executing the guarded correction
// phase and authoritative body-store writeback.
struct PersistentContactPositionCorrectionTestAccess
{
    static void Apply( PhysicsContactSolverStage& stage, PhysicsBodyStore& bodyStore,
                       const PersistentContactSolverStepPolicy& policy, std::span<const uint8_t> sleepState,
                       std::span<const PersistentContact> contacts )
    {
        stage.m_persistentContacts.clear();

        for ( const PersistentContact& contact : contacts )
        {
            stage.m_persistentContacts.push_back( contact );
        }

        stage.m_persistentContactSolverStats = PersistentContactSolverStats();
        stage.m_sideEffects.pipelineEventCount = 0u;
        PersistentContactSolveTransaction& transaction = stage.m_solveTransaction;
        transaction.Clear();
        transaction.ResetBodies( static_cast<std::size_t>( bodyStore.Count() ) );
        transaction.BeginEntryPolicySetup();

        using Phase = PersistentContactSolvePhaseCursor::Phase;
        constexpr std::array phasesToCorrection { Phase::BodySetup,         Phase::BuildManifolds,
                                                  Phase::TerrainRows,       Phase::Precompute,
                                                  Phase::SolveRows,         Phase::PointSupportInstability,
                                                  Phase::TerrainRestPolicy, Phase::WriteBack,
                                                  Phase::DebugContacts };

        for ( const Phase phase : phasesToCorrection )
        {
            transaction.AdvanceOrFatal( phase, "PositionCorrectionTestAccess" );
        }

        transaction.CorrectPositions<false>( stage, bodyStore, policy, sleepState, 0u, nullptr );
    }
};
} // namespace SkullbonezCore::Physics

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
    std::vector<float> timeRemaining;
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
        body.hot.inverseRotationalInertia = isFixed ? ZERO_VECTOR
                                                    : Vector3( 1.0f / inertia, 1.0f / inertia, 1.0f / inertia );
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
        timeRemaining.assign( static_cast<std::size_t>( bodyStore.Count() ), kSolverDt );
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
        timeRemaining.assign( static_cast<std::size_t>( bodyStore.Count() ), kSolverDt );
        sleepSupportedThisFrame.assign( static_cast<std::size_t>( bodyStore.Count() ), 0u );
    }

    void AddMovingBox( const Vector3& position, const Vector3& halfExtents, const Vector3& rotationAxis,
                       float rotationRadians, const Vector3& linearVelocity, const Vector3& angularVelocity, float mass,
                       float restitution, bool isFixed )
    {

        // Why: this fixture uses the production box-inertia derivation and world-
        // framed response so anisotropic tests exercise the same frame transform
        // as solver impulse writeback.
        const Vector3 inertia = SkullbonezCore::Physics::CalculateBoxInertiaForHalfExtents( halfExtents, mass );

        PhysicsBodyCreateRecord body;
        body.hot.position = position;
        body.hot.linearVelocity = linearVelocity;
        body.hot.angularVelocity = angularVelocity;
        body.hot.orientation.RotateAboutAxis( rotationAxis, rotationRadians );
        body.cold.rotationalInertia = inertia;
        body.hot.inverseRotationalInertia = isFixed ? Vector3( 0.0f, 0.0f, 0.0f )
                                                    : Vector3( 1.0f / inertia.x, 1.0f / inertia.y, 1.0f / inertia.z );

        body.cold.mass = mass;
        body.hot.inverseMass = isFixed ? 0.0f : 1.0f / mass;
        body.hot.boundingRadius = sqrtf( halfExtents.x * halfExtents.x + halfExtents.y * halfExtents.y +
                                         halfExtents.z * halfExtents.z );

        body.hot.fixed = isFixed;
        body.cold.usesWorldInertia = true;
        body.cold.angularVelocityLimit = 1000.0f;
        (void)bodyStore.CreateBodyRecord( body );

        ColliderRecord collider;
        const CollisionShape shape( BoundingBox( halfExtents, Vector3( 0.0f, 0.0f, 0.0f ) ) );
        collider.shapeKind = ColliderShapeKind::Box;
        collider.boundingRadius = body.hot.boundingRadius;
        collider.restitution = restitution;
        collider.friction = config.material.terrainFrictionCoefficient;
        (void)SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliderStore, collider, shape );

        sleepState.assign( static_cast<std::size_t>( bodyStore.Count() ), 0u );
        timeRemaining.assign( static_cast<std::size_t>( bodyStore.Count() ), kSolverDt );
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
        solver.Solve( bodyStore, colliderStore, policy, candidatePairs, sleepState, timeRemaining, sleepSupportEdges,
                      terrainContactManifolds, terrainRestApplied, sleepSupportedThisFrame, diagnostics, kSolverDt,
                      nullptr );
    }
};

struct PositionCorrectionResult
{
    Vector3 bodyAPosition = ZERO_VECTOR;
    Vector3 bodyBPosition = ZERO_VECTOR;
    SkullbonezCore::Physics::PersistentContactSolverStats stats;
};

PositionCorrectionResult ApplySyntheticFacePositionCorrection( std::span<const float> penetrations )
{
    REQUIRE( !penetrations.empty() );
    REQUIRE( penetrations.size() <= 4u );

    SolverFixture fixture;
    fixture.AddDynamicSphere( Vector3( -1.0f, 0.0f, 0.0f ), ZERO_VECTOR );
    fixture.AddDynamicSphere( Vector3( 1.0f, 0.0f, 0.0f ), ZERO_VECTOR );

    std::array<PersistentContact, 4u> contacts;

    for ( std::size_t rowIndex = 0u; rowIndex < penetrations.size(); ++rowIndex )
    {
        PersistentContact& contact = contacts[rowIndex];
        contact.bodyA = 0;
        contact.bodyB = 1;
        contact.featureId = static_cast<uint32_t>( rowIndex + 1u );
        contact.normal = Vector3( 1.0f, 0.0f, 0.0f );
        contact.rA = Vector3( 1.0f, rowIndex < 2u ? -1.0f : 1.0f, rowIndex % 2u == 0u ? -1.0f : 1.0f );
        contact.rB = contact.rA * -1.0f;
        contact.penetration = penetrations[rowIndex];
        contact.manifoldPointCount = static_cast<uint8_t>( penetrations.size() );
    }

    PersistentContactSolverStepPolicy policy;
    policy.objectSlop = 0.1f;
    policy.objectPositionCorrectionPercent = 0.35f;
    SkullbonezCore::Physics::PersistentContactPositionCorrectionTestAccess::
        Apply( fixture.solver, fixture.bodyStore, policy, fixture.sleepState,
               std::span<const PersistentContact>( contacts.data(), penetrations.size() ) );

    const auto hotFields = fixture.bodyStore.HotFields();
    PositionCorrectionResult result;
    result.bodyAPosition = PhysicsBodyPosition( hotFields, 0u );
    result.bodyBPosition = PhysicsBodyPosition( hotFields, 1u );
    result.stats = fixture.solver.GetStats();
    return result;
}

TEST_CASE( "Persistent contact solver: face position correction uses one deepest manifold row" )
{
    constexpr std::array oneRowPenetrations { 0.6f };
    constexpr std::array fourRowPenetrations { 0.2f, 0.4f, 0.6f, 0.3f };
    const PositionCorrectionResult oneRow = ApplySyntheticFacePositionCorrection( oneRowPenetrations );
    const PositionCorrectionResult fourRows = ApplySyntheticFacePositionCorrection( fourRowPenetrations );

    // One 0.6-deep manifold with 0.1 slop and 35% correction separates two
    // equal inverse masses by 0.175, shared evenly along the manifold normal.
    constexpr float expectedBodyAX = -1.0875f;
    constexpr float expectedBodyBX = 1.0875f;
    CHECK( oneRow.bodyAPosition.x == doctest::Approx( expectedBodyAX ) );
    CHECK( oneRow.bodyBPosition.x == doctest::Approx( expectedBodyBX ) );
    CHECK( fourRows.bodyAPosition.x == doctest::Approx( oneRow.bodyAPosition.x ) );
    CHECK( fourRows.bodyBPosition.x == doctest::Approx( oneRow.bodyBPosition.x ) );
    CHECK( fourRows.bodyAPosition.y == doctest::Approx( 0.0f ) );
    CHECK( fourRows.bodyAPosition.z == doctest::Approx( 0.0f ) );
    CHECK( fourRows.stats.positionCorrectionRows == 1 );
    CHECK( fourRows.stats.positionCorrectionTotal == doctest::Approx( 0.175f ) );
}

void ConfigureClosedSolve( SolverFixture& fixture )
{
    fixture.config.worldForces.gravity = 0.0f;
    fixture.config.solver.slop = 0.2f;
    fixture.config.solver.baumgarteBeta = 0.0f;
    fixture.config.solver.positionCorrectionPercent = 0.0f;
    fixture.config.body.contactRestitutionThreshold = 0.0f;
    fixture.config.terrain.baumgarteBeta = 0.0f;
    fixture.config.terrain.maxBaumgarteBias = 0.0f;
}

bool EnergyWithinClosedBound( const ContactEnergyMeasurement& before, const ContactEnergyMeasurement& after )
{
    return after.TotalKineticEnergy() <=
           before.TotalKineticEnergy() +
               SkullbonezCore::Physics::ContactEnergyPrecisionTolerance( before.TotalKineticEnergy() );
}

void CheckDynamicMomentumConserved( const ContactEnergyMeasurement& before, const ContactEnergyMeasurement& after )
{
    const auto componentMatches = []( double expected, double actual, double scale )
    { return std::abs( actual - expected ) <= SkullbonezCore::Physics::ContactMomentumPrecisionTolerance( scale ); };

    CHECK( componentMatches( before.linearMomentum.x, after.linearMomentum.x, before.linearMomentumScale.x ) );
    CHECK( componentMatches( before.linearMomentum.y, after.linearMomentum.y, before.linearMomentumScale.y ) );
    CHECK( componentMatches( before.linearMomentum.z, after.linearMomentum.z, before.linearMomentumScale.z ) );
    CHECK( componentMatches( before.angularMomentum.x, after.angularMomentum.x, before.angularMomentumScale.x ) );
    CHECK( componentMatches( before.angularMomentum.y, after.angularMomentum.y, before.angularMomentumScale.y ) );
    CHECK( componentMatches( before.angularMomentum.z, after.angularMomentum.z, before.angularMomentumScale.z ) );
}

double ExplicitSeparationWorkBudget( const SolverFixture& fixture )
{
    double budget = 0.0;

    for ( const auto& contact : fixture.solver.GetPersistentContacts() )
    {
        budget += (std::max)( 0.0, static_cast<double>( contact.bias ) * static_cast<double>( contact.accN ) );
    }

    return budget;
}

void ApplyPlantedSphereImpulse( SolverFixture& fixture, const Vector3& impulse, const Vector3& rA, const Vector3& rB )
{
    auto hot = fixture.bodyStore.MutableHotFields();
    const Vector3 angularImpulseA = CrossProduct( rA, impulse );
    const Vector3 angularImpulseB = CrossProduct( rB, impulse );

    hot.linearVelocityX[0] -= impulse.x * hot.inverseMass[0];
    hot.linearVelocityY[0] -= impulse.y * hot.inverseMass[0];
    hot.linearVelocityZ[0] -= impulse.z * hot.inverseMass[0];
    hot.linearVelocityX[1] += impulse.x * hot.inverseMass[1];
    hot.linearVelocityY[1] += impulse.y * hot.inverseMass[1];
    hot.linearVelocityZ[1] += impulse.z * hot.inverseMass[1];
    hot.angularVelocityX[0] -= angularImpulseA.x * hot.inverseInertiaX[0];
    hot.angularVelocityY[0] -= angularImpulseA.y * hot.inverseInertiaY[0];
    hot.angularVelocityZ[0] -= angularImpulseA.z * hot.inverseInertiaZ[0];
    hot.angularVelocityX[1] += angularImpulseB.x * hot.inverseInertiaX[1];
    hot.angularVelocityY[1] += angularImpulseB.y * hot.inverseInertiaY[1];
    hot.angularVelocityZ[1] += angularImpulseB.z * hot.inverseInertiaZ[1];
}

} // namespace


TEST_CASE( "Contact energy oracle: sphere restitution matrix bounds complete solves" )
{
    constexpr std::array<float, 3> restitutionValues = { 0.0f, 0.5f, 1.0f };

    for ( float restitution : restitutionValues )
    {
        SolverFixture dynamicPair;
        ConfigureClosedSolve( dynamicPair );
        dynamicPair.config.material.terrainFrictionCoefficient = 0.0f;
        dynamicPair.AddDynamicSphere( Vector3( -0.95f, 0.0f, 0.0f ), Vector3( 2.0f, 0.0f, 0.0f ), restitution );
        dynamicPair.AddDynamicSphere( Vector3( 0.95f, 0.0f, 0.0f ), Vector3( -1.0f, 0.0f, 0.0f ), restitution );
        dynamicPair.candidatePairs.emplace_back( 0, 1 );

        const ContactEnergyMeasurement dynamicBefore = SkullbonezCore::Physics::MeasureContactEnergy(
            dynamicPair.bodyStore );
        dynamicPair.Solve();
        const ContactEnergyMeasurement dynamicAfter = SkullbonezCore::Physics::MeasureContactEnergy( dynamicPair.bodyStore );

        CHECK( SkullbonezCore::Physics::ContactEnergyIsFinite( dynamicAfter ) );
        CHECK( EnergyWithinClosedBound( dynamicBefore, dynamicAfter ) );
        CheckDynamicMomentumConserved( dynamicBefore, dynamicAfter );

        if ( restitution == 1.0f )
        {
            CHECK( std::abs( dynamicAfter.TotalKineticEnergy() - dynamicBefore.TotalKineticEnergy() ) <=
                   SkullbonezCore::Physics::ContactEnergyPrecisionTolerance( dynamicBefore.TotalKineticEnergy() ) );
        }

        SolverFixture fixedPair;
        ConfigureClosedSolve( fixedPair );
        fixedPair.config.material.terrainFrictionCoefficient = 0.0f;
        fixedPair.AddDynamicSphere( Vector3( -0.95f, 0.0f, 0.0f ), Vector3( 2.0f, 0.0f, 0.0f ), restitution );
        fixedPair.AddDynamicSphere( Vector3( 0.95f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), restitution, true );
        fixedPair.candidatePairs.emplace_back( 0, 1 );

        const ContactEnergyMeasurement fixedBefore = SkullbonezCore::Physics::MeasureContactEnergy( fixedPair.bodyStore );
        fixedPair.Solve();
        const ContactEnergyMeasurement fixedAfter = SkullbonezCore::Physics::MeasureContactEnergy( fixedPair.bodyStore );

        CHECK( fixedBefore.dynamicBodyCount == 1u );
        CHECK( fixedAfter.dynamicBodyCount == 1u );
        CHECK( SkullbonezCore::Physics::ContactEnergyIsFinite( fixedAfter ) );
        CHECK( EnergyWithinClosedBound( fixedBefore, fixedAfter ) );
    }
}


TEST_CASE( "Contact energy oracle: box face off-center friction and anisotropic solves stay bounded" )
{
    const Vector3 unitHalfExtents( 1.0f, 1.0f, 1.0f );
    const Vector3 xAxis( 1.0f, 0.0f, 0.0f );

    SolverFixture face;
    ConfigureClosedSolve( face );
    face.config.material.terrainFrictionCoefficient = 0.0f;
    face.AddMovingBox( Vector3( -0.99f, 0.0f, 0.0f ), unitHalfExtents, xAxis, 0.0f, Vector3( 2.0f, 0.0f, 0.0f ),
                       Vector3( 0.0f, 0.0f, 0.0f ), 2.0f, 0.5f, false );

    face.AddMovingBox( Vector3( 0.99f, 0.0f, 0.0f ), unitHalfExtents, xAxis, 0.0f, Vector3( -1.0f, 0.0f, 0.0f ),
                       Vector3( 0.0f, 0.0f, 0.0f ), 2.0f, 0.5f, false );

    face.candidatePairs.emplace_back( 0, 1 );
    const ContactEnergyMeasurement faceBefore = SkullbonezCore::Physics::MeasureContactEnergy( face.bodyStore );
    face.Solve();
    const ContactEnergyMeasurement faceAfter = SkullbonezCore::Physics::MeasureContactEnergy( face.bodyStore );
    CHECK( EnergyWithinClosedBound( faceBefore, faceAfter ) );
    CheckDynamicMomentumConserved( faceBefore, faceAfter );

    SolverFixture offCenter;
    ConfigureClosedSolve( offCenter );
    offCenter.config.material.terrainFrictionCoefficient = 0.0f;
    offCenter.AddMovingBox( Vector3( 0.0f, 0.0f, 0.0f ), unitHalfExtents, xAxis, 0.0f, Vector3( 0.0f, 0.0f, 0.0f ),
                            Vector3( 0.0f, 0.0f, 0.0f ), 2.0f, 0.0f, true );

    offCenter.AddMovingBox( Vector3( 1.9f, 0.65f, 0.0f ), unitHalfExtents, xAxis, 0.0f, Vector3( -3.0f, 0.0f, 0.0f ),
                            Vector3( 0.0f, 0.0f, 0.0f ), 2.0f, 0.0f, false );

    offCenter.candidatePairs.emplace_back( 0, 1 );
    const ContactEnergyMeasurement offCenterBefore = SkullbonezCore::Physics::MeasureContactEnergy( offCenter.bodyStore );
    offCenter.Solve();
    const ContactEnergyMeasurement offCenterAfter = SkullbonezCore::Physics::MeasureContactEnergy( offCenter.bodyStore );
    CHECK( offCenterAfter.rotationalKineticEnergy > 0.0 );
    CHECK( EnergyWithinClosedBound( offCenterBefore, offCenterAfter ) );

    SolverFixture friction;
    ConfigureClosedSolve( friction );
    friction.config.material.terrainFrictionCoefficient = 0.6f;
    friction.AddDynamicSphere( Vector3( -0.95f, 0.0f, 0.0f ), Vector3( 2.0f, 0.0f, 3.0f ) );
    friction.AddDynamicSphere( Vector3( 0.95f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), 0.0f, true );
    friction.candidatePairs.emplace_back( 0, 1 );
    const ContactEnergyMeasurement frictionBefore = SkullbonezCore::Physics::MeasureContactEnergy( friction.bodyStore );
    friction.Solve();
    const ContactEnergyMeasurement frictionAfter = SkullbonezCore::Physics::MeasureContactEnergy( friction.bodyStore );
    CHECK( frictionAfter.TotalKineticEnergy() < frictionBefore.TotalKineticEnergy() );
    CHECK( EnergyWithinClosedBound( frictionBefore, frictionAfter ) );

    SolverFixture anisotropic;
    ConfigureClosedSolve( anisotropic );
    anisotropic.config.material.terrainFrictionCoefficient = 0.0f;
    anisotropic.AddMovingBox( Vector3( 0.0f, 2.0f, 0.0f ), Vector3( 1.0f, 2.0f, 3.0f ), Vector3( 0.0f, 0.0f, 1.0f ), 0.45f,
                              Vector3( 0.0f, -3.0f, 0.0f ), Vector3( 0.3f, -0.2f, 0.4f ), 4.0f, 0.5f, false );

    anisotropic.AddTerrainContactAtOffset( 0, 701u, 0.0f, Vector3( 0.6f, -2.0f, 0.5f ), false, false );
    const ContactEnergyMeasurement anisotropicBefore = SkullbonezCore::Physics::MeasureContactEnergy(
        anisotropic.bodyStore );
    anisotropic.Solve();
    const ContactEnergyMeasurement anisotropicAfter = SkullbonezCore::Physics::MeasureContactEnergy( anisotropic.bodyStore );
    CHECK( anisotropicBefore.rotationalKineticEnergy > 0.0 );
    CHECK( anisotropicAfter.rotationalKineticEnergy > 0.0 );
    CHECK( EnergyWithinClosedBound( anisotropicBefore, anisotropicAfter ) );
}


TEST_CASE( "Contact energy oracle: matching two-frame cache stays within the complete-solve bound" )
{
    SolverFixture first;
    ConfigureClosedSolve( first );
    first.config.material.terrainFrictionCoefficient = 0.0f;
    first.AddDynamicSphere( Vector3( -0.95f, 0.0f, 0.0f ), Vector3( 2.0f, 0.0f, 0.0f ), 0.5f );
    first.AddDynamicSphere( Vector3( 0.95f, 0.0f, 0.0f ), Vector3( -1.0f, 0.0f, 0.0f ), 0.5f );
    first.candidatePairs.emplace_back( 0, 1 );
    first.Solve();
    REQUIRE_FALSE( first.solver.GetPersistentContactCache().empty() );

    const auto firstHot = first.bodyStore.HotFields();
    SolverFixture second;
    ConfigureClosedSolve( second );
    second.config.material.terrainFrictionCoefficient = 0.0f;
    second.AddDynamicSphere( Vector3( -0.95f, 0.0f, 0.0f ), PhysicsBodyLinearVelocity( firstHot, 0u ), 0.5f );
    second.AddDynamicSphere( Vector3( 0.95f, 0.0f, 0.0f ), PhysicsBodyLinearVelocity( firstHot, 1u ), 0.5f );
    second.candidatePairs.emplace_back( 0, 1 );
    second.CopySolverStateFrom( first );

    const ContactEnergyMeasurement before = SkullbonezCore::Physics::MeasureContactEnergy( second.bodyStore );
    second.Solve();
    const ContactEnergyMeasurement after = SkullbonezCore::Physics::MeasureContactEnergy( second.bodyStore );

    CHECK( second.solver.GetStats().cacheHits > 0 );
    CHECK( second.solver.GetStats().warmStartedRows > 0 );
    CHECK( EnergyWithinClosedBound( before, after ) );
    CheckDynamicMomentumConserved( before, after );
}


TEST_CASE( "Contact energy oracle: Baumgarte solve exposes an explicit separation-work budget" )
{
    SolverFixture biased;
    ConfigureClosedSolve( biased );
    biased.config.solver.slop = 0.0f;
    biased.config.solver.baumgarteBeta = 0.2f;
    biased.config.solver.positionCorrectionPercent = 0.2f;
    biased.config.terrain.maxBaumgarteBias = 6.0f;
    biased.AddDynamicSphere( Vector3( -0.8f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) );
    biased.AddDynamicSphere( Vector3( 0.8f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) );
    biased.candidatePairs.emplace_back( 0, 1 );

    const ContactEnergyMeasurement before = SkullbonezCore::Physics::MeasureContactEnergy( biased.bodyStore );
    biased.Solve();
    const ContactEnergyMeasurement after = SkullbonezCore::Physics::MeasureContactEnergy( biased.bodyStore );
    const double separationWork = ExplicitSeparationWorkBudget( biased );
    const double tolerance = SkullbonezCore::Physics::ContactBiasedEnergyTolerance( before.TotalKineticEnergy(),
                                                                                    separationWork );

    CHECK( separationWork > 0.0 );
    CHECK( after.TotalKineticEnergy() > before.TotalKineticEnergy() );
    CHECK( after.TotalKineticEnergy() <= before.TotalKineticEnergy() + separationWork + tolerance );
    CHECK( biased.solver.GetStats().positionCorrectionRows > 0 );
    CHECK( biased.solver.GetStats().positionCorrectionTotal > 0.0f );
    CheckDynamicMomentumConserved( before, after );
}


TEST_CASE( "Contact energy oracle: planted restitution impulse and stale-geometry controls fail" )
{
    SolverFixture oversized;
    ConfigureClosedSolve( oversized );
    oversized.AddDynamicSphere( Vector3( -0.95f, 0.0f, 0.0f ), Vector3( 1.0f, 0.0f, 0.0f ) );
    oversized.AddDynamicSphere( Vector3( 0.95f, 0.0f, 0.0f ), Vector3( -1.0f, 0.0f, 0.0f ) );
    const ContactEnergyMeasurement oversizedBefore = SkullbonezCore::Physics::MeasureContactEnergy( oversized.bodyStore );
    ApplyPlantedSphereImpulse( oversized, Vector3( 4.04f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ),
                               Vector3( 0.0f, 0.0f, 0.0f ) );

    const ContactEnergyMeasurement oversizedAfter = SkullbonezCore::Physics::MeasureContactEnergy( oversized.bodyStore );
    CHECK_FALSE( EnergyWithinClosedBound( oversizedBefore, oversizedAfter ) );
    CheckDynamicMomentumConserved( oversizedBefore, oversizedAfter );

    SolverFixture staleGeometry;
    ConfigureClosedSolve( staleGeometry );
    staleGeometry.AddDynamicSphere( Vector3( 0.0f, -0.95f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) );
    staleGeometry.AddDynamicSphere( Vector3( 0.0f, 0.95f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) );
    const ContactEnergyMeasurement staleBefore = SkullbonezCore::Physics::MeasureContactEnergy( staleGeometry.bodyStore );

    // Hazard: this deliberately applies a cached scalar through contact arms
    // that do not belong to the new normal/geometry. A validity check must reject
    // the resulting unaccounted translational and rotational work.
    ApplyPlantedSphereImpulse( staleGeometry, Vector3( 0.0f, 4.0f, 0.0f ), Vector3( 1.0f, 0.0f, 0.0f ),
                               Vector3( -1.0f, 0.0f, 0.0f ) );

    const ContactEnergyMeasurement staleAfter = SkullbonezCore::Physics::MeasureContactEnergy( staleGeometry.bodyStore );
    CHECK_FALSE( EnergyWithinClosedBound( staleBefore, staleAfter ) );

    SolverFixture overRestitution;
    ConfigureClosedSolve( overRestitution );
    overRestitution.config.material.terrainFrictionCoefficient = 0.0f;
    overRestitution.AddDynamicSphere( Vector3( -0.95f, 0.0f, 0.0f ), Vector3( 1.0f, 0.0f, 0.0f ), 1.1f );
    overRestitution.AddDynamicSphere( Vector3( 0.95f, 0.0f, 0.0f ), Vector3( -1.0f, 0.0f, 0.0f ), 1.1f );
    overRestitution.candidatePairs.emplace_back( 0, 1 );
    const ContactEnergyMeasurement restitutionBefore = SkullbonezCore::Physics::MeasureContactEnergy(
        overRestitution.bodyStore );
    overRestitution.Solve();
    const ContactEnergyMeasurement restitutionAfter = SkullbonezCore::Physics::MeasureContactEnergy(
        overRestitution.bodyStore );
    CHECK_FALSE( EnergyWithinClosedBound( restitutionBefore, restitutionAfter ) );
    CheckDynamicMomentumConserved( restitutionBefore, restitutionAfter );
}


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


TEST_CASE( "Pending gameplay impulse matches the contact path for a rotated anisotropic box" )
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
    body.hot.inverseRotationalInertia = Vector3( 1.0f / rotationalInertia.x, 1.0f / rotationalInertia.y,
                                                 1.0f / rotationalInertia.z );

    body.hot.boundingRadius = SkullbonezCore::Math::CollisionDetection::GetShapeBoundingRadius( shape );
    const auto bodyHandle = fixture.bodyStore.CreateBodyRecord( body );
    REQUIRE( bodyHandle.IsValid() );

    ColliderRecord collider;
    collider.body = bodyHandle;
    collider.shapeKind = ColliderShapeKind::Box;
    collider.boundingRadius = body.hot.boundingRadius;
    REQUIRE(
        SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( fixture.colliderStore, collider, shape ).IsValid() );

    const Vector3 worldImpulse( 3.0f, 5.0f, -2.0f );
    const Vector3 worldApplicationOffset( 0.75f, -0.4f, 1.1f );
    REQUIRE( fixture.bodyStore.SetPendingBodyImpulse( bodyHandle, worldImpulse, worldApplicationOffset ) );

    PhysicsWorldForces noForces;
    noForces.angularDragMultiplier = 0.0f;
    const BuoyancyBodyFacts noBuoyancy;
    REQUIRE( fixture.bodyStore.ApplyForces( noForces, fixture.colliderStore, {}, noBuoyancy, 0, kSolverDt ) );
    const Vector3
        gameplayAngularVelocity = SkullbonezCore::Physics::PhysicsBodyAngularVelocity( fixture.bodyStore.HotFields(), 0u );

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

    // Invariant: both paths share the same world/body inertia-frame conversion
    // for this world-space application offset and impulse; this cross-path
    // oracle catches a deterministic frame error that byte baselines could
    // otherwise preserve.
    CHECK( gameplayAngularVelocity.x == doctest::Approx( contactAngularVelocity.x ) );
    CHECK( gameplayAngularVelocity.y == doctest::Approx( contactAngularVelocity.y ) );
    CHECK( gameplayAngularVelocity.z == doctest::Approx( contactAngularVelocity.z ) );
}

TEST_CASE( "Pending gameplay impulse preserves the exact isotropic sphere response" )
{
    SolverFixture fixture;
    const Vector3 isotropicInertia( 4.0f, 4.0f, 4.0f );

    PhysicsBodyCreateRecord body;
    body.cold.mass = 2.0f;
    body.cold.rotationalInertia = isotropicInertia;
    body.cold.angularVelocityLimit = 1000.0f;
    body.cold.usesWorldInertia = false;
    body.hot.orientation.RotateAboutAxis( Vector3( 0.0f, 1.0f, 0.0f ), 0.73f );
    body.hot.inverseMass = 1.0f / body.cold.mass;
    body.hot.inverseRotationalInertia = Vector3( 0.25f, 0.25f, 0.25f );
    body.hot.boundingRadius = 1.0f;
    const auto bodyHandle = fixture.bodyStore.CreateBodyRecord( body );
    REQUIRE( bodyHandle.IsValid() );

    ColliderRecord collider;
    collider.body = bodyHandle;
    collider.shapeKind = ColliderShapeKind::Sphere;
    collider.boundingRadius = body.hot.boundingRadius;
    const CollisionShape shape( BoundingSphere( body.hot.boundingRadius, Vector3( 0.0f, 0.0f, 0.0f ) ) );
    REQUIRE(
        SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( fixture.colliderStore, collider, shape ).IsValid() );

    const Vector3 worldImpulse( 3.0f, 5.0f, -2.0f );
    const Vector3 worldApplicationOffset( 0.75f, -0.4f, 1.1f );
    REQUIRE( fixture.bodyStore.SetPendingBodyImpulse( bodyHandle, worldImpulse, worldApplicationOffset ) );

    PhysicsWorldForces noForces;
    noForces.angularDragMultiplier = 0.0f;
    const BuoyancyBodyFacts noBuoyancy;
    REQUIRE( fixture.bodyStore.ApplyForces( noForces, fixture.colliderStore, {}, noBuoyancy, 0, kSolverDt ) );

    const Vector3 actual = SkullbonezCore::Physics::PhysicsBodyAngularVelocity( fixture.bodyStore.HotFields(), 0u );
    Vector3 expected;
    REQUIRE( CrossProduct( worldApplicationOffset, worldImpulse ).TryDivided( isotropicInertia, expected ) );
    CHECK( actual.x == expected.x );
    CHECK( actual.y == expected.y );
    CHECK( actual.z == expected.z );
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

TEST_CASE( "Persistent contact solver: stable terrain rows do not turn overlap into separating velocity" )
{
    SolverFixture fixture;
    fixture.AddDynamicSphere( Vector3( 0.0f, 1.0f, 0.0f ), ZERO_VECTOR );
    fixture.AddTerrainContact( 0, 901u, 0.02f );
    const float contactInterval = kSolverDt * 0.25f;
    fixture.timeRemaining[0] = contactInterval;
    fixture.Solve();

    REQUIRE( fixture.solver.GetPersistentContacts().size() == 1u );
    const PersistentContact& contact = fixture.solver.GetPersistentContacts()[0];
    const float expectedSupportImpulse = fixture.bodyStore.Records()[0].mass * fabsf( fixture.config.worldForces.gravity ) *
                                         contactInterval;
    CHECK( contact.bias == 0.0f );
    CHECK( contact.separationBias == 0.0f );
    CHECK( contact.terrainWarmStart == doctest::Approx( expectedSupportImpulse ).epsilon( 0.0001 ) );
    CHECK(
        contact.frictionLimit ==
        doctest::Approx( fixture.config.material.terrainFrictionCoefficient * expectedSupportImpulse ).epsilon( 0.0001 ) );
    CHECK( fixture.bodyStore.HotFields().positionY[0] > 1.0f );
    CHECK( fabsf( fixture.bodyStore.HotFields().linearVelocityY[0] ) < 0.0001f );
}

TEST_CASE( "Persistent contact solver: stable terrain max-bias witness remains supported without rebound" )
{
    SolverFixture fixture;
    constexpr float closingSpeed = 1.945360f;
    fixture.config.body.contactRestitutionThreshold = 2.0f;
    fixture.AddDynamicSphere( Vector3( 0.0f, 1.0f, 0.0f ), Vector3( 0.0f, -closingSpeed, 0.0f ) );
    fixture.AddTerrainContact( 0, 838u, 0.337921f );
    fixture.Solve();

    REQUIRE( fixture.solver.GetPersistentContacts().size() == 1u );
    const PersistentContact& contact = fixture.solver.GetPersistentContacts()[0];
    REQUIRE( contact.supportsRestingPolicy );
    CHECK( contact.preSolveClosingSpeed == doctest::Approx( closingSpeed ).epsilon( 0.0001 ) );
    CHECK( contact.preSolveClosingSpeed < fixture.config.body.contactRestitutionThreshold );
    CHECK( contact.bias == 0.0f );
    CHECK( contact.separationBias == 0.0f );
    CHECK( contact.accN > 0.0f );
    CHECK( fixture.bodyStore.HotFields().positionY[0] > 1.0f );
    CHECK( fabsf( fixture.bodyStore.HotFields().linearVelocityY[0] ) < 0.0001f );
}

TEST_CASE( "Persistent contact solver: unstable terrain overlap retains bounded velocity recovery" )
{
    SolverFixture fixture;
    fixture.AddDynamicSphere( Vector3( 0.0f, 1.0f, 0.0f ), ZERO_VECTOR );
    fixture.AddTerrainContact( 0, 839u, 0.337921f, false );
    fixture.Solve();

    REQUIRE( fixture.solver.GetPersistentContacts().size() == 1u );
    const PersistentContact& contact = fixture.solver.GetPersistentContacts()[0];
    REQUIRE_FALSE( contact.supportsRestingPolicy );
    CHECK( contact.bias > 0.0f );
    CHECK( contact.bias <= fixture.config.terrain.maxBaumgarteBias );
    CHECK( contact.separationBias == contact.bias );
    CHECK( fixture.bodyStore.HotFields().linearVelocityY[0] > 0.0f );
}

TEST_CASE( "Persistent contact solver: equal dynamic remainders own the object contact interval" )
{
    SolverFixture fixture;
    fixture.config.solver.slop = 0.0f;
    fixture.AddDynamicSphere( Vector3( 0.0f, 0.0f, 0.0f ), ZERO_VECTOR );
    fixture.AddDynamicSphere( Vector3( 1.98f, 0.0f, 0.0f ), ZERO_VECTOR );
    fixture.candidatePairs.emplace_back( 0, 1 );
    const float contactInterval = kSolverDt * 0.5f;
    fixture.timeRemaining[0] = contactInterval;
    fixture.timeRemaining[1] = contactInterval;
    fixture.Solve();

    REQUIRE( fixture.solver.GetPersistentContacts().size() == 1u );
    const PersistentContact& contact = fixture.solver.GetPersistentContacts()[0];
    REQUIRE_FALSE( contact.normalCoupledFriction );
    const float expectedBias = fixture.config.solver.baumgarteBeta * contact.penetration / contactInterval;
    const float expectedFrictionLimit = fixture.config.material.objectFrictionCoefficient *
                                        fixture.bodyStore.Records()[0].mass * fabsf( fixture.config.worldForces.gravity ) *
                                        contactInterval;
    CHECK( contact.bias == doctest::Approx( expectedBias ).epsilon( 0.0001 ) );
    CHECK( contact.frictionLimit == doctest::Approx( expectedFrictionLimit ).epsilon( 0.0001 ) );
}

TEST_CASE( "Persistent contact solver: fixed bodies do not shorten a dynamic contact interval" )
{
    SolverFixture fixture;
    fixture.config.solver.slop = 0.0f;
    fixture.AddDynamicSphere( Vector3( 0.0f, 0.0f, 0.0f ), ZERO_VECTOR, 0.0f, true );
    fixture.AddDynamicSphere( Vector3( 1.98f, 0.0f, 0.0f ), ZERO_VECTOR );
    fixture.candidatePairs.emplace_back( 0, 1 );
    const float dynamicInterval = kSolverDt * 0.75f;
    fixture.timeRemaining[0] = TOLERANCE * 0.5f;
    fixture.timeRemaining[1] = dynamicInterval;
    fixture.Solve();

    REQUIRE( fixture.solver.GetPersistentContacts().size() == 1u );
    const PersistentContact& contact = fixture.solver.GetPersistentContacts()[0];
    REQUIRE_FALSE( contact.normalCoupledFriction );
    const float expectedBias = fixture.config.solver.baumgarteBeta * contact.penetration / dynamicInterval;
    const float expectedFrictionLimit = fixture.config.material.objectFrictionCoefficient *
                                        fixture.bodyStore.Records()[1].mass * fabsf( fixture.config.worldForces.gravity ) *
                                        dynamicInterval;
    CHECK( contact.bias == doctest::Approx( expectedBias ).epsilon( 0.0001 ) );
    CHECK( contact.frictionLimit == doctest::Approx( expectedFrictionLimit ).epsilon( 0.0001 ) );
}

TEST_CASE( "Persistent contact solver: sleeping anchors do not shorten an awake contact interval" )
{
    SolverFixture fixture;
    fixture.config.solver.slop = 0.0f;
    fixture.AddDynamicSphere( Vector3( 0.0f, 0.0f, 0.0f ), ZERO_VECTOR );
    fixture.AddDynamicSphere( Vector3( 1.98f, 0.0f, 0.0f ), ZERO_VECTOR );
    fixture.candidatePairs.emplace_back( 0, 1 );
    const float awakeInterval = kSolverDt * 0.5f;
    fixture.sleepState[0] = 1u;
    fixture.timeRemaining[0] = 0.0f;
    fixture.timeRemaining[1] = awakeInterval;
    fixture.Solve();

    REQUIRE( fixture.solver.GetPersistentContacts().size() == 1u );
    const PersistentContact& contact = fixture.solver.GetPersistentContacts()[0];
    REQUIRE_FALSE( contact.normalCoupledFriction );
    const float expectedBias = fixture.config.solver.baumgarteBeta * contact.penetration / awakeInterval;
    const float expectedFrictionLimit = fixture.config.material.objectFrictionCoefficient *
                                        fixture.bodyStore.Records()[1].mass * fabsf( fixture.config.worldForces.gravity ) *
                                        awakeInterval;
    CHECK( contact.bias == doctest::Approx( expectedBias ).epsilon( 0.0001 ) );
    CHECK( contact.frictionLimit == doctest::Approx( expectedFrictionLimit ).epsilon( 0.0001 ) );
}

TEST_CASE( "Persistent contact solver: unequal dynamic remainders use their shared minimum" )
{
    SolverFixture fixture;
    fixture.config.solver.slop = 0.0f;
    fixture.AddDynamicSphere( Vector3( 0.0f, 0.0f, 0.0f ), ZERO_VECTOR );
    fixture.AddDynamicSphere( Vector3( 1.98f, 0.0f, 0.0f ), ZERO_VECTOR );
    fixture.candidatePairs.emplace_back( 0, 1 );
    const float shorterInterval = kSolverDt * 0.25f;
    fixture.timeRemaining[0] = kSolverDt * 0.75f;
    fixture.timeRemaining[1] = shorterInterval;
    fixture.Solve();

    REQUIRE( fixture.solver.GetPersistentContacts().size() == 1u );
    const PersistentContact& contact = fixture.solver.GetPersistentContacts()[0];
    REQUIRE_FALSE( contact.normalCoupledFriction );
    const float expectedBias = fixture.config.solver.baumgarteBeta * contact.penetration / shorterInterval;
    const float expectedFrictionLimit = fixture.config.material.objectFrictionCoefficient *
                                        fixture.bodyStore.Records()[0].mass * fabsf( fixture.config.worldForces.gravity ) *
                                        shorterInterval;
    CHECK( contact.bias == doctest::Approx( expectedBias ).epsilon( 0.0001 ) );
    CHECK( contact.frictionLimit == doctest::Approx( expectedFrictionLimit ).epsilon( 0.0001 ) );
}

TEST_CASE( "Persistent contact solver: near-zero remainder disables Baumgarte but retains impact restitution" )
{
    SolverFixture fixture;
    constexpr float restitution = 0.25f;
    fixture.config.solver.slop = 0.0f;
    fixture.AddDynamicSphere( Vector3( 0.0f, 0.0f, 0.0f ), ZERO_VECTOR, restitution, true );
    fixture.AddDynamicSphere( Vector3( 1.98f, 0.0f, 0.0f ), Vector3( -3.0f, 0.0f, 0.0f ), restitution );
    fixture.candidatePairs.emplace_back( 0, 1 );
    fixture.timeRemaining[1] = TOLERANCE * 0.5f;
    fixture.Solve();

    REQUIRE( fixture.solver.GetPersistentContacts().size() == 1u );
    const PersistentContact& contact = fixture.solver.GetPersistentContacts()[0];
    REQUIRE_FALSE( contact.normalCoupledFriction );
    CHECK( contact.separationBias == 0.0f );
    CHECK( contact.frictionLimit == 0.0f );
    CHECK( contact.bias == doctest::Approx( restitution * 3.0f ).epsilon( 0.0001 ) );
    CHECK( fixture.bodyStore.HotFields().linearVelocityX[1] > 0.0f );
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

TEST_CASE( "Persistent contact solver: duplicate features publish one warm-start cache key" )
{
    SolverFixture fixture;
    fixture.AddDynamicSphere( Vector3( 0.0f, 1.0f, 0.0f ), Vector3( 0.0f, -0.1f, 0.0f ) );
    fixture.AddTerrainContact( 0, 42u, 0.05f );
    fixture.AddTerrainContact( 0, 42u, 0.05f );
    fixture.Solve();

    REQUIRE( fixture.solver.GetPersistentContacts().size() == 2u );
    REQUIRE( fixture.solver.GetPersistentContactCache().size() == 1u );

    PhysicsSolverSnapshot snapshot;
    fixture.solver.CaptureReplayState( snapshot );
    REQUIRE( snapshot.persistentContactCache.size() == 1u );
    CHECK( snapshot.persistentContactCache[0].key == fixture.solver.GetPersistentContactCache()[0].key );
}

TEST_CASE( "Persistent contact solver: restitution follows loaded contact-feature lifetime" )
{
    auto buildImpact = []( SolverFixture& fixture )
    {
        constexpr float restitution = 0.75f;
        fixture.config.solver.slop = 0.0f;
        fixture.config.solver.baumgarteBeta = 0.2f;
        fixture.AddDynamicSphere( Vector3( 0.0f, 0.0f, 0.0f ), ZERO_VECTOR, restitution, true );
        fixture.AddDynamicSphere( Vector3( 0.0f, 1.99f, 0.0f ), Vector3( 0.0f, -6.0f, 0.0f ), restitution );
        fixture.candidatePairs.emplace_back( 0, 1 );
    };

    SolverFixture fresh;
    buildImpact( fresh );
    fresh.Solve();

    REQUIRE( fresh.solver.GetPersistentContactCache().size() == 1u );
    REQUIRE( fresh.solver.GetPersistentContacts().size() == 1u );
    CHECK( fresh.solver.GetPersistentContacts()[0].separationBias == 0.0f );
    CHECK( fresh.bodyStore.HotFields().linearVelocityY[1] > 0.0f );
    const float freshSeparatingSpeed = fresh.bodyStore.HotFields().linearVelocityY[1];

    SolverFixture persistent;
    buildImpact( persistent );
    persistent.CopySolverStateFrom( fresh );
    persistent.Solve();

    // Invariant: an exact cached feature proves both compatible warm-start
    // geometry and continuous load. It suppresses renewed restitution while
    // still allowing Baumgarte bias to repair the deliberate overlap.
    REQUIRE( persistent.solver.GetStats().cacheHits == 1 );
    REQUIRE( persistent.solver.GetStats().cacheMisses == 0 );
    REQUIRE( persistent.solver.GetPersistentContacts().size() == 1u );
    const auto& persistentContact = persistent.solver.GetPersistentContacts()[0];
    const float expectedBaumgarteBias = persistent.config.solver.baumgarteBeta * persistentContact.penetration / kSolverDt;
    CHECK( persistentContact.bias == doctest::Approx( expectedBaumgarteBias ).epsilon( 0.0001 ) );
    CHECK( persistentContact.separationBias == doctest::Approx( expectedBaumgarteBias ).epsilon( 0.0001 ) );
    CHECK( persistent.bodyStore.HotFields().linearVelocityY[1] ==
           doctest::Approx( expectedBaumgarteBias ).epsilon( 0.0001 ) );
    CHECK( persistent.bodyStore.HotFields().linearVelocityY[1] < freshSeparatingSpeed );

    PhysicsSolverSnapshot changedFeatureSnapshot;
    fresh.solver.CaptureReplayState( changedFeatureSnapshot );
    REQUIRE( changedFeatureSnapshot.persistentContactCache.size() == 1u );
    changedFeatureSnapshot.persistentContactCache[0].key ^= 0x5a5a5a5a;

    SolverFixture changedFeature;
    buildImpact( changedFeature );
    changedFeature.solver.RestoreReplayState( changedFeatureSnapshot );
    changedFeature.Solve();

    // A feature miss is fresh geometry: the solver neither reuses the stale
    // impulse nor lets another row under the body-pair prefix suppress impact.
    CHECK( changedFeature.solver.GetStats().cacheHits == 0 );
    CHECK( changedFeature.solver.GetStats().cacheMisses == 1 );
    CHECK( changedFeature.bodyStore.HotFields().linearVelocityY[1] ==
           doctest::Approx( freshSeparatingSpeed ).epsilon( 0.0001 ) );

    SolverFixture gap;
    buildImpact( gap );
    gap.CopySolverStateFrom( fresh );
    gap.candidatePairs.clear();
    gap.Solve();
    CHECK( gap.solver.GetPersistentContactCache().empty() );

    SolverFixture reimpact;
    buildImpact( reimpact );
    reimpact.CopySolverStateFrom( gap );
    reimpact.Solve();

    // A complete no-contact frame ends the contact-feature lifetime.
    // Restitution is therefore available again when the same body identities
    // genuinely meet.
    CHECK( reimpact.bodyStore.HotFields().linearVelocityY[1] == doctest::Approx( freshSeparatingSpeed ).epsilon( 0.0001 ) );

    auto buildElasticImpact = [&]( SolverFixture& fixture )
    {
        buildImpact( fixture );
        fixture.worldForces.mutualGravity.enabled = true;
        fixture.worldForces.mutualGravity.elasticCollisions = true;
    };

    SolverFixture elasticFresh;
    buildElasticImpact( elasticFresh );
    elasticFresh.Solve();
    const float elasticFreshSpeed = elasticFresh.bodyStore.HotFields().linearVelocityY[1];

    SolverFixture elasticPersistent;
    buildElasticImpact( elasticPersistent );
    elasticPersistent.CopySolverStateFrom( elasticFresh );
    elasticPersistent.Solve();

    // Mutual-gravity elastic space intentionally has no resting warm-start
    // policy. Its repeated contact solve remains perfectly elastic even when a
    // prior cache row exists for replay continuity.
    CHECK( elasticPersistent.solver.GetStats().cacheHits == 0 );
    CHECK( elasticPersistent.bodyStore.HotFields().linearVelocityY[1] ==
           doctest::Approx( elasticFreshSpeed ).epsilon( 0.0001 ) );
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
    // Invariant: this independent literal pins the deliberately retained
    // empirical policy rather than following the production constant. The
    // linked restoration report records the rejected removal experiment.
    constexpr float shorelineScale = 0.35f;
    constexpr float tiltedEdgeRadians = 0.75f;
    // Invariant: derive the center from the same normalized quaternion and
    // rotation-matrix path as AddBox. This keeps the lowest corners exactly on
    // the plane even when the axis-angle implementation changes numerically.
    Quaternion tiltedEdgeOrientation;
    tiltedEdgeOrientation.RotateAboutAxis( Vector3( 1.0f, 0.0f, 0.0f ), tiltedEdgeRadians );
    const float tiltedBoxCenterY = tiltedEdgeOrientation.GetOrientationMatrix().SupportExtentY(
        Vector3( 1.0f, 1.0f, 1.0f ) );

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
    CHECK( fixture.diagnostics.GetDebugContacts()[0].separationBias == 0.0f );
    CHECK( fixture.bodyStore.HotFields().linearVelocityY[0] > 0.0f );
    CHECK( fixture.bodyStore.HotFields().linearVelocityY[0] <= 6.0f * 0.75f + 0.0001f );
}


TEST_CASE( "Persistent contact solver: terrain restitution ignores manifold row count" )
{
    const std::array onePoint { Vector3( 0.0f, -1.0f, 0.0f ) };
    const std::array fourPoints { Vector3( -0.75f, -1.0f, -0.75f ), Vector3( 0.75f, -1.0f, -0.75f ),
                                  Vector3( -0.75f, -1.0f, 0.75f ), Vector3( 0.75f, -1.0f, 0.75f ) };

    auto measureBounceSpeed = []( auto contactOffsets )
    {
        SolverFixture fixture;
        fixture.config.material.terrainFrictionCoefficient = 0.0f;
        fixture.AddMovingBox( Vector3( 0.0f, 1.0f, 0.0f ), Vector3( 1.0f, 1.0f, 1.0f ), Vector3( 0.0f, 1.0f, 0.0f ), 0.0f,
                              Vector3( 0.0f, -6.0f, 0.0f ), ZERO_VECTOR, 1.0f, 0.75f, false );
        fixture.AddTerrainContactAtOffset( 0, 100u, 0.0f, contactOffsets[0], true, false );

        TerrainContactManifold& manifold = fixture.terrainContactManifolds[0];
        manifold.pointCount = static_cast<uint8_t>( contactOffsets.size() );
        const auto hotFields = fixture.bodyStore.HotFields();

        for ( std::size_t pointIndex = 0; pointIndex < contactOffsets.size(); ++pointIndex )
        {
            manifold.points[pointIndex].featureId = 100u + static_cast<uint32_t>( pointIndex );
            manifold.points[pointIndex].rA = contactOffsets[pointIndex];
            manifold.points[pointIndex].point = PhysicsBodyPosition( hotFields, 0u ) + contactOffsets[pointIndex];
            manifold.points[pointIndex].penetration = 0.0f;
        }

        fixture.Solve();
        return fixture.bodyStore.HotFields().linearVelocityY[0];
    };

    const float onePointBounceSpeed = measureBounceSpeed( onePoint );
    const float fourPointBounceSpeed = measureBounceSpeed( fourPoints );

    // Invariant: restitution is a target separating speed for the physical
    // impact, not a budget divided among whichever contact rows terrain retained.
    // Symmetric rows may retain sub-percent sequential-solver residue, while the
    // retired point-count division reduced the four-row target by 75 percent.
    CHECK( onePointBounceSpeed == doctest::Approx( 6.0f * 0.75f ) );
    CHECK( fourPointBounceSpeed == doctest::Approx( onePointBounceSpeed ).epsilon( 0.01 ) );
}


TEST_CASE( "Persistent contact solver: terrain rest policy preserves quiet residual motion" )
{
    SolverFixture fixture;
    fixture.config.material.terrainFrictionCoefficient = 0.0f;
    fixture.config.material.rollingFrictionCoefficient = 0.0f;
    fixture.AddMovingBox( Vector3( 0.0f, 1.0f, 0.0f ), Vector3( 1.0f, 1.0f, 1.0f ), Vector3( 0.0f, 1.0f, 0.0f ), 0.0f,
                          Vector3( 0.04f, 0.0f, 0.0f ), Vector3( 0.01f, 0.0f, 0.0f ), 1.0f, 0.0f, false );
    fixture.AddTerrainContact( 0, 101u, 0.0f );

    fixture.Solve();

    const auto hotFields = fixture.bodyStore.HotFields();

    // Invariant: the contact transaction publishes physical residual motion.
    // PhysicsSleepController alone may quantize it to zero after the body has
    // satisfied configurable support and quiet-frame policy.
    CHECK( hotFields.linearVelocityX[0] == doctest::Approx( 0.04f ) );
    CHECK( hotFields.angularVelocityX[0] == doctest::Approx( 0.01f ) );
}


TEST_CASE( "Persistent contact solver: rolling resistance and tangent friction preserve no-slip coupling" )
{
    SolverFixture fixture;
    constexpr float initialSpeed = 4.0f;
    fixture.config.material.terrainFrictionCoefficient = 0.8f;
    fixture.config.material.rollingFrictionCoefficient = 0.02f;
    fixture.AddDynamicSphere( Vector3( 0.0f, 1.0f, 0.0f ), Vector3( initialSpeed, 0.0f, 0.0f ) );
    fixture.bodyStore.MutableHotFields().angularVelocityZ[0] = -initialSpeed;
    fixture.AddTerrainContact( 0, 201u, 0.0f );

    const float mass = fixture.bodyStore.Records()[0].mass;
    const float inertia = fixture.bodyStore.Records()[0].rotationalInertia.z;
    const float initialEnergy = 0.5f * mass * initialSpeed * initialSpeed + 0.5f * inertia * initialSpeed * initialSpeed;
    fixture.Solve();

    REQUIRE( fixture.solver.GetPersistentContacts().size() == 1u );
    const PersistentContact& contact = fixture.solver.GetPersistentContacts()[0];
    const auto hotFields = fixture.bodyStore.HotFields();
    const Vector3 linearVelocity = PhysicsBodyLinearVelocity( hotFields, 0u );
    const Vector3 angularVelocity = PhysicsBodyAngularVelocity( hotFields, 0u );
    const Vector3 contactVelocity = linearVelocity + CrossProduct( angularVelocity, contact.rA );
    const Vector3 tangentVelocity = contactVelocity - contact.normal * Dot( contactVelocity, contact.normal );
    const float rollingImpulse = sqrtf( contact.accRollingT1 * contact.accRollingT1 +
                                        contact.accRollingT2 * contact.accRollingT2 );
    const float rollingLimit = fixture.config.material.rollingFrictionCoefficient *
                               (std::max)( contact.accN, contact.terrainWarmStart ) * contact.rollingRadius;
    const float finalEnergy = 0.5f * mass * VectorMagSquared( linearVelocity ) +
                              0.5f * inertia * VectorMagSquared( angularVelocity );
    const auto convergence = fixture.solver.GetConvergenceTrace().Samples();

    // The retired post-PGS torque reduced omega after friction had finished and
    // left measurable contact-point slip. The angular row now consumes its own
    // normal-load budget first, then the ordinary tangent row restores rolling.
    CHECK( VectorMag( tangentVelocity ) < 0.0001f );
    CHECK( linearVelocity.x > 0.0f );
    CHECK( linearVelocity.x < initialSpeed );
    CHECK( angularVelocity.z < 0.0f );
    CHECK( fabsf( angularVelocity.z ) < initialSpeed );
    CHECK( finalEnergy < initialEnergy );
    CHECK( rollingImpulse > 0.0f );
    CHECK( rollingImpulse <= rollingLimit + 0.000001f );
    REQUIRE_FALSE( convergence.empty() );
    CHECK( convergence.front().tangentImpulseDeltaSq > 0.0f );
    CHECK( convergence.front().tangentChangedRowCount > 0 );
    CHECK( fixture.terrainRestApplied[0] == 1u );
}


TEST_CASE( "Persistent contact solver: separating touching spheres publish no speculative friction row" )
{
    SolverFixture fixture;
    fixture.config.material.terrainFrictionCoefficient = 0.8f;
    fixture.config.material.rollingFrictionCoefficient = 0.02f;
    fixture.AddDynamicSphere( Vector3( -1.0f, 0.0f, 0.0f ), Vector3( -1.0f, 0.0f, 10.0f ) );
    fixture.AddDynamicSphere( Vector3( 1.0f, 0.0f, 0.0f ), Vector3( 1.0f, 0.0f, -10.0f ) );
    fixture.candidatePairs.emplace_back( 0, 1 );

    fixture.Solve();

    const auto solved = fixture.bodyStore.HotFields();

    CHECK( fixture.solver.GetPersistentContacts().empty() );
    CHECK( solved.linearVelocityZ[0] == doctest::Approx( 10.0f ) );
    CHECK( solved.linearVelocityZ[1] == doctest::Approx( -10.0f ) );
}


TEST_CASE( "Persistent contact solver: terrain rolling resistance does not damp normal-axis spin" )
{
    SolverFixture fixture;
    constexpr float normalSpin = 3.0f;
    fixture.config.material.terrainFrictionCoefficient = 0.8f;
    fixture.config.material.rollingFrictionCoefficient = 0.5f;
    fixture.config.material.spinFrictionCoefficient = 0.0f;
    fixture.AddDynamicSphere( Vector3( 0.0f, 1.0f, 0.0f ), ZERO_VECTOR );
    fixture.bodyStore.MutableHotFields().angularVelocityY[0] = normalSpin;
    fixture.AddTerrainContact( 0, 202u, 0.0f );
    fixture.Solve();

    REQUIRE( fixture.solver.GetPersistentContacts().size() == 1u );
    const PersistentContact& contact = fixture.solver.GetPersistentContacts()[0];
    const auto hotFields = fixture.bodyStore.HotFields();

    // Spin around the terrain normal produces neither roll nor point slip. The
    // old magnitude-based post-pass damped it anyway; the tangent-plane row must
    // leave it exactly outside rolling-resistance authority.
    CHECK( hotFields.angularVelocityY[0] == doctest::Approx( normalSpin ) );
    CHECK( hotFields.angularVelocityX[0] == 0.0f );
    CHECK( hotFields.angularVelocityZ[0] == 0.0f );
    CHECK( contact.accRollingT1 == 0.0f );
    CHECK( contact.accRollingT2 == 0.0f );
}


TEST_CASE( "Persistent contact solver: terrain spin resistance uses its own normal-axis row" )
{
    SolverFixture fixture;
    constexpr float normalSpin = 3.0f;
    fixture.config.material.terrainFrictionCoefficient = 0.8f;
    fixture.config.material.rollingFrictionCoefficient = 0.0f;
    fixture.config.material.spinFrictionCoefficient = 0.3f;
    fixture.AddDynamicSphere( Vector3( 0.0f, 1.0f, 0.0f ), ZERO_VECTOR );
    fixture.bodyStore.MutableHotFields().angularVelocityY[0] = normalSpin;
    fixture.AddTerrainContact( 0, 204u, 0.0f );
    fixture.Solve();

    REQUIRE( fixture.solver.GetPersistentContacts().size() == 1u );
    const PersistentContact& contact = fixture.solver.GetPersistentContacts()[0];
    const auto hotFields = fixture.bodyStore.HotFields();
    const float spinImpulseLimit = fixture.config.material.spinFrictionCoefficient *
                                   (std::max)( contact.accN, contact.terrainWarmStart );

    // Spin friction is distinct from rolling resistance: its scalar row acts
    // only about the contact normal and cannot manufacture point slip.
    CHECK( hotFields.angularVelocityY[0] >= 0.0f );
    CHECK( hotFields.angularVelocityY[0] < normalSpin );
    CHECK( hotFields.angularVelocityX[0] == 0.0f );
    CHECK( hotFields.angularVelocityZ[0] == 0.0f );
    CHECK( fabsf( contact.accSpin ) > 0.0f );
    CHECK( fabsf( contact.accSpin ) <= spinImpulseLimit + 0.000001f );
}


TEST_CASE( "Persistent contact solver: rolling and spin resistance yield to material impact" )
{
    SolverFixture fixture;
    constexpr float normalSpin = 3.0f;
    fixture.config.material.terrainFrictionCoefficient = 0.8f;
    fixture.config.material.rollingFrictionCoefficient = 0.5f;
    fixture.config.material.spinFrictionCoefficient = 0.5f;
    fixture.config.body.contactRestitutionThreshold = 2.0f;
    fixture.AddDynamicSphere( Vector3( 0.0f, 1.0f, 0.0f ), Vector3( 0.0f, -6.0f, 0.0f ) );
    fixture.bodyStore.MutableHotFields().angularVelocityY[0] = normalSpin;
    fixture.AddTerrainContact( 0, 205u, 0.0f );
    fixture.Solve();

    REQUIRE( fixture.solver.GetPersistentContacts().size() == 1u );
    const PersistentContact& contact = fixture.solver.GetPersistentContacts()[0];
    const auto hotFields = fixture.bodyStore.HotFields();

    // Rest resistance cannot spend angular budget during a genuine impact;
    // restitution and the ordinary tangent row own that distinct event.
    CHECK( contact.preSolveClosingSpeed >= fixture.config.body.contactRestitutionThreshold );
    CHECK( contact.rollingRadius == 0.0f );
    CHECK( contact.accRollingT1 == 0.0f );
    CHECK( contact.accRollingT2 == 0.0f );
    CHECK( contact.accSpin == 0.0f );
    CHECK( hotFields.angularVelocityY[0] == doctest::Approx( normalSpin ) );
}


TEST_CASE( "Persistent contact solver: rolling resistance keeps a no-slip sphere moving down a slope" )
{
    SolverFixture fixture;
    constexpr float inverseSqrtTwo = 0.70710678118f;
    constexpr float initialSpeed = 4.0f;
    const Vector3 terrainNormal( 0.0f, inverseSqrtTwo, inverseSqrtTwo );
    const Vector3 downSlope( 0.0f, -inverseSqrtTwo, inverseSqrtTwo );
    const Vector3 rollingAxis = CrossProduct( terrainNormal, downSlope );
    fixture.config.material.terrainFrictionCoefficient = 0.8f;
    fixture.config.material.rollingFrictionCoefficient = 0.02f;
    fixture.AddDynamicSphere( terrainNormal, downSlope * initialSpeed );
    fixture.bodyStore.MutableHotFields().angularVelocityX[0] = rollingAxis.x * initialSpeed;
    fixture.AddTerrainContactAtOffset( 0, 203u, 0.0f, -terrainNormal, true, false );
    fixture.terrainContactManifolds[0].normal = terrainNormal;
    fixture.Solve();

    REQUIRE( fixture.solver.GetPersistentContacts().size() == 1u );
    const PersistentContact& contact = fixture.solver.GetPersistentContacts()[0];
    const auto hotFields = fixture.bodyStore.HotFields();
    const Vector3 linearVelocity = PhysicsBodyLinearVelocity( hotFields, 0u );
    const Vector3 angularVelocity = PhysicsBodyAngularVelocity( hotFields, 0u );
    const Vector3 contactVelocity = linearVelocity + CrossProduct( angularVelocity, contact.rA );
    const Vector3 tangentVelocity = contactVelocity - contact.normal * Dot( contactVelocity, contact.normal );

    CHECK( VectorMag( tangentVelocity ) < 0.0001f );
    CHECK( Dot( linearVelocity, downSlope ) > 3.9f );
    CHECK( Dot( linearVelocity, downSlope ) < initialSpeed );
    CHECK( fabsf( Dot( angularVelocity, rollingAxis ) ) > 3.9f );
    CHECK( fabsf( Dot( angularVelocity, rollingAxis ) ) < initialSpeed );
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
    // max-row stopping threshold; the cap is honest non-convergence, not merely
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


TEST_CASE( "Persistent contact solver: individually quiet rows stop independently of row count" )
{
    constexpr int quietContactCount = 8;
    SolverFixture fixture;
    fixture.config.worldForces.gravity = 0.0f;
    fixture.config.material.terrainFrictionCoefficient = 0.0f;
    fixture.config.solver.iterations = 4;

    for ( int bodyIndex = 0; bodyIndex < quietContactCount; ++bodyIndex )
    {
        fixture.AddDynamicSphere( Vector3( static_cast<float>( bodyIndex ) * 3.0f, 1.0f, 0.0f ),
                                  Vector3( 0.0f, -0.0003f, 0.0f ) );
        fixture.AddTerrainContact( bodyIndex, 200u + static_cast<uint32_t>( bodyIndex ), 0.0f );
    }

    fixture.Solve();

    const auto samples = fixture.solver.GetConvergenceTrace().Samples();
    REQUIRE( samples.size() == 1u );
    CHECK( samples.front().stoppingImpulseDeltaSq > 1.0e-6f );
    CHECK( samples.front().maxRowImpulseDeltaSq < 1.0e-6f );
    CHECK( samples.front().normalChangedRowCount == quietContactCount );
    CHECK( fixture.solver.GetStats().solverIterations == 1 );
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
