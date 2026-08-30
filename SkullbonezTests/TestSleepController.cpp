//   - Agentic/Reference/engine-glossary.md//
// File: SkullbonezTests/TestSleepController.cpp
// Purpose:
//   Locks PhysicsSleepController wake fan-out, underwater, support, and awake-list transitions.
//
// Summary:
//   Focused Physics-owned stores drive every public sleep wake path without a
//   PhysicsWorld or scene owner. Tests distinguish diagnostic ids, retained
//   contact/joint connectivity, automatic same-step wake,
//   underwater refusal/release, and the derived sorted awake index.
//
// Glossary:
//   Visual island: Positive diagnostic id shared by bodies that slept together.

// Invariants:
//   - Test owners reserve once in scene-load phase and reset all retained rows per case.
//   - Explicit wake reaches the retained contact/joint component while visual
//     ids and disconnected sleeping bodies cannot widen it.
//   - Automatic point-joint wake resets the step clock and applies current-step
//     forces exactly once before publishing sorted awake membership.
//   - Awake membership excludes fixed and sleeping rows after remove, add, and
//     same-count cold rebuild boundaries.
//   - The Debug-only awake-list classifier accepts only dynamic, awake rows;
//     focused tests exercise every fixed/sleeping truth-table combination.
//   - Point-joint support publishes both directed edges before fixed-point propagation.
//
// Related:
//   - SkullbonezSource/Physics/Stages/PhysicsSleepController.h
//   - SkullbonezSource/Physics/Stages/PhysicsSleepController.Wake.cpp
//   - SkullbonezSource/Physics/SleepIslandSystem.cpp
//

#include "../ThirdPtySource/doctest/doctest.h"
#include "TestColliderStoreFixtures.h"

#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#include "../SkullbonezSource/Physics/BoundingSphere.h"
#include "../SkullbonezSource/Physics/BuoyancySystem.h"
#include "../SkullbonezSource/Physics/ColliderStore.h"
#include "../SkullbonezSource/Physics/PhysicsApi.h"
#include "../SkullbonezSource/Physics/PhysicsSolverSnapshot.h"
#include "../SkullbonezSource/Physics/PhysicsWorldForces.h"
#include "../SkullbonezSource/Physics/Ragdoll.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsSleepController.h"

#include <array>
#include <initializer_list>
#include <memory>

using SkullbonezCore::Core::Allocation::RuntimeAllocationPhase;
using SkullbonezCore::Core::Allocation::RuntimeAllocationScope;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::CollisionShape;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::BuoyancyBodyFacts;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::MakePhysicsBodyCreateDesc;
using SkullbonezCore::Physics::MakePhysicsSceneObjectId;
using SkullbonezCore::Physics::PersistentContact;
using SkullbonezCore::Physics::PersistentContactCacheList;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyMotionKind;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsContactCacheWakeAccess;
using SkullbonezCore::Physics::PhysicsSleepController;
using SkullbonezCore::Physics::PhysicsSolverSnapshot;
using SkullbonezCore::Physics::PhysicsWorldForces;
using SkullbonezCore::Physics::PointJointConstraint;

namespace SkullbonezCore::Physics
{
struct PhysicsSleepControllerTestAccess
{
    static bool IsAwakeListEntryConsistent( bool fixed, bool sleeping )
    {
        return PhysicsSleepController::IsAwakeListEntryConsistent( fixed, sleeping );
    }

    static void SetSleepIslandVisualId( PhysicsSleepController& controller, int bodyIndex, int visualId )
    {
        controller.m_sleepIslandVisualId[static_cast<std::size_t>( bodyIndex )] = visualId;
    }
};
} // namespace SkullbonezCore::Physics

namespace
{
constexpr std::size_t kSleepTestBodyCapacity = 8u;

CollisionShape UnitSphere()
{
    return CollisionShape( BoundingSphere( 1.0f, SkullbonezCore::Math::Vector::ZERO_VECTOR, 0.0f ) );
}

class SleepTestOwners
{
  public:
    PhysicsBodyStore bodies;
    ColliderStore colliders;
    PhysicsSleepController controller;
    PersistentContactCacheList cache { "TestSleepController.cache",
                                       SkullbonezCore::Physics::PhysicsCapacityReason::ExplicitTestCapacity };

    SleepTestOwners()
    {
        RuntimeAllocationScope sceneLoad( RuntimeAllocationPhase::SceneLoad );

        bodies.ReserveCapacity( kSleepTestBodyCapacity );
        colliders.ReserveCapacity( kSleepTestBodyCapacity );
        colliders.ReserveShapeCapacity( kSleepTestBodyCapacity, 0u, 0u );
        controller.ReserveBodyCapacity( kSleepTestBodyCapacity, kSleepTestBodyCapacity );
        cache.Reserve( kSleepTestBodyCapacity );
    }

    void Reset()
    {
        bodies.Clear();
        colliders.Clear();
        controller.Clear();
        controller.SetPhysicsSleepEnabled( true );
        cache.clear();
    }
};

SleepTestOwners& ResetSleepTestOwners()
{

    // Lifetime: the large store owners live once on the test heap. Each case
    // resets their retained rows, avoiding both initialized-image pressure and
    // repeated scene-capacity growth noise in the complete harness.
    static const std::unique_ptr<SleepTestOwners> owners = std::make_unique<SleepTestOwners>();
    owners->Reset();
    return *owners;
}

class SleepFixture
{
  public:
    SleepTestOwners& owners = ResetSleepTestOwners();
    std::array<PhysicsBodyHandle, kSleepTestBodyCapacity> handles = {};
    CollisionShape sphere = UnitSphere();

    int AddSphere( const Vector3& position, PhysicsBodyMotionKind motionKind = PhysicsBodyMotionKind::Dynamic )
    {
        const int bodyIndex = owners.bodies.Count();
        const auto sceneId = MakePhysicsSceneObjectId( static_cast<uint64_t>( 1000 + bodyIndex ) );
        const auto desc = MakePhysicsBodyCreateDesc( sceneId, sphere, position,
                                                     SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION,
                                                     SkullbonezCore::Math::Vector::ZERO_VECTOR,
                                                     SkullbonezCore::Math::Vector::ZERO_VECTOR, Vector3( 1.0f, 1.0f, 1.0f ),
                                                     1.0f, 0.0f, motionKind );

        const PhysicsBodyHandle handle = owners.bodies.CreateBodyRecord( desc, true );
        handles[static_cast<std::size_t>( bodyIndex )] = handle;

        ColliderRecord collider;
        collider.body = handle;
        collider.sceneObjectId = sceneId;
        collider.boundingRadius = 1.0f;
        SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( owners.colliders, collider, sphere );
        return bodyIndex;
    }

    void Mirror()
    {
        REQUIRE( owners.controller.MirrorFlagsFrom( owners.bodies, owners.bodies.Count() ) );
    }

    void Sleep( int bodyIndex )
    {
        REQUIRE( bodyIndex >= 0 );
        REQUIRE( bodyIndex < owners.bodies.Count() );
        REQUIRE( owners.bodies.SeedBodyAsleep( handles[static_cast<std::size_t>( bodyIndex )] ) );
        owners.controller.SeedModelAsleep( owners.bodies, bodyIndex );
        REQUIRE( owners.controller.GetSleepStates()[static_cast<std::size_t>( bodyIndex )] == 1u );
    }

    PointJointConstraint Joint( int bodyA, int bodyB ) const
    {
        PointJointConstraint joint;
        joint.SetBodies( handles[static_cast<std::size_t>( bodyA )], handles[static_cast<std::size_t>( bodyB )] );
        return joint;
    }

    PhysicsContactCacheWakeAccess WakeAccess()
    {
        return PhysicsContactCacheWakeAccess( owners.cache );
    }
};

void CheckAwakeIndices( const PhysicsSleepController& controller, std::initializer_list<int> expected )
{
    const std::span<const int> actual = controller.GetAwakeBodyIndices();
    REQUIRE( actual.size() == expected.size() );
    std::size_t position = 0u;

    for ( int bodyIndex : expected )
    {
        CHECK( actual[position] == bodyIndex );
        ++position;
    }

    CHECK( controller.GetAwakeBodyCount() == static_cast<int>( expected.size() ) );
}

bool ContainsSupportEdge( std::span<const std::pair<int, int>> edges, int supporter, int supported )
{

    for ( const std::pair<int, int>& edge : edges )
    {

        if ( edge.first == supporter && edge.second == supported )
        {
            return true;
        }
    }

    return false;
}
} // namespace

TEST_CASE( "Physics sleep controller: visual ids cannot widen explicit simulation-island wake" )
{
    SleepFixture fixture;

    for ( int bodyIndex = 0; bodyIndex < 4; ++bodyIndex )
    {
        fixture.AddSphere( Vector3( static_cast<float>( bodyIndex ) * 100.0f, 0.0f, 0.0f ) );
    }

    fixture.Mirror();

    for ( int bodyIndex = 0; bodyIndex < 4; ++bodyIndex )
    {
        fixture.Sleep( bodyIndex );
    }

    PhysicsSolverSnapshot snapshot;
    fixture.owners.controller.CaptureReplayState( snapshot );
    snapshot.sleepIslandVisualId = { 7, 7, 9, 0 };
    fixture.owners.controller.RestoreReplayState( snapshot );
    REQUIRE( fixture.owners.controller.MirrorFlagsFrom( fixture.owners.bodies, 4 ) );

    fixture.owners.controller.WakeModel( fixture.owners.bodies, fixture.WakeAccess(), 0 );

    const std::span<const uint8_t> afterVisualWake = fixture.owners.controller.GetSleepStates();
    CHECK( afterVisualWake[0] == 0u );
    CHECK( afterVisualWake[1] == 1u );
    CHECK( afterVisualWake[2] == 1u );
    CHECK( afterVisualWake[3] == 1u );
    CheckAwakeIndices( fixture.owners.controller, { 0 } );

    // A zero visual id owns only the selected row as well.
    fixture.owners.controller.WakeModel( fixture.owners.bodies, fixture.WakeAccess(), 3 );
    CHECK( fixture.owners.controller.GetSleepStates()[2] == 1u );
    CHECK( fixture.owners.controller.GetSleepStates()[3] == 0u );
    CheckAwakeIndices( fixture.owners.controller, { 0, 3 } );
}

TEST_CASE( "Physics sleep controller: dormant contact topology ignores visual ids" )
{
    SleepFixture fixture;
    fixture.AddSphere( Vector3( 0.0f, 0.0f, 0.0f ) );
    fixture.AddSphere( Vector3( 1.0f, 0.0f, 0.0f ) );
    fixture.Mirror();
    fixture.Sleep( 0 );
    fixture.Sleep( 1 );

    PersistentContact contact;
    contact.bodyA = 0;
    contact.bodyB = 1;
    const std::array<PersistentContact, 1> contacts = { contact };
    const std::array<PersistentContact, 0> noContacts = {};
    const std::array<PointJointConstraint, 0> noJoints = {};
    fixture.owners.controller.RestoreSimulationIslandTopology( fixture.owners.bodies, contacts, noJoints );

    using SkullbonezCore::Physics::PhysicsSleepControllerTestAccess;
    PhysicsSleepControllerTestAccess::SetSleepIslandVisualId( fixture.owners.controller, 0, 7 );
    PhysicsSleepControllerTestAccess::SetSleepIslandVisualId( fixture.owners.controller, 1, 9 );

    // Sleeping contacts do not re-enter the solver. A second topology pass must
    // retain the exact dormant edge without consulting presentation identity.
    fixture.owners.controller.RestoreSimulationIslandTopology( fixture.owners.bodies, noContacts, noJoints );
    fixture.owners.controller.WakeModel( fixture.owners.bodies, fixture.WakeAccess(), 0 );

    CHECK( fixture.owners.controller.GetSleepStates()[0] == 0u );
    CHECK( fixture.owners.controller.GetSleepStates()[1] == 0u );
    CheckAwakeIndices( fixture.owners.controller, { 0, 1 } );
}

TEST_CASE( "Physics sleep controller: explicit point-joint wake reaches one complete component" )
{
    SleepFixture fixture;

    for ( int bodyIndex = 0; bodyIndex < 4; ++bodyIndex )
    {
        fixture.AddSphere( Vector3( static_cast<float>( bodyIndex ) * 100.0f, 0.0f, 0.0f ) );
    }

    fixture.Mirror();

    for ( int bodyIndex = 0; bodyIndex < 4; ++bodyIndex )
    {
        fixture.Sleep( bodyIndex );
    }

    const std::array<PointJointConstraint, 2> joints = { fixture.Joint( 0, 1 ), fixture.Joint( 1, 2 ) };
    const std::array<PersistentContact, 0> noContacts = {};
    fixture.owners.controller.RestoreSimulationIslandTopology( fixture.owners.bodies, noContacts, joints );
    fixture.owners.controller.WakeModel( fixture.owners.bodies, fixture.WakeAccess(), 0 );

    CHECK( fixture.owners.controller.GetSleepStates()[0] == 0u );
    CHECK( fixture.owners.controller.GetSleepStates()[1] == 0u );
    CHECK( fixture.owners.controller.GetSleepStates()[2] == 0u );
    CHECK( fixture.owners.controller.GetSleepStates()[3] == 1u );
    CheckAwakeIndices( fixture.owners.controller, { 0, 1, 2 } );
}

TEST_CASE( "Physics sleep controller: retained-contact wake traverses transitively" )
{
    SleepFixture fixture;

    for ( int bodyIndex = 0; bodyIndex < 4; ++bodyIndex )
    {
        fixture.AddSphere( Vector3( static_cast<float>( bodyIndex ) * 100.0f, 0.0f, 0.0f ) );
    }

    fixture.Mirror();

    for ( int bodyIndex = 0; bodyIndex < 4; ++bodyIndex )
    {
        fixture.Sleep( bodyIndex );
    }

    std::array<PersistentContact, 2> contacts = {};
    contacts[0].bodyA = 0;
    contacts[0].bodyB = 1;
    contacts[1].bodyA = 2;
    contacts[1].bodyB = 1;
    const std::array<PointJointConstraint, 0> noJoints = {};
    fixture.owners.controller.RestoreSimulationIslandTopology( fixture.owners.bodies, contacts, noJoints );
    fixture.owners.controller.WakeModel( fixture.owners.bodies, fixture.WakeAccess(), 0 );

    // The reversed 2->1 row proves retained contact edges are symmetric. Body
    // 2 is two hops from the selected row; body 3 is disconnected.
    CHECK( fixture.owners.controller.GetSleepStates()[0] == 0u );
    CHECK( fixture.owners.controller.GetSleepStates()[1] == 0u );
    CHECK( fixture.owners.controller.GetSleepStates()[2] == 0u );
    CHECK( fixture.owners.controller.GetSleepStates()[3] == 1u );
    CheckAwakeIndices( fixture.owners.controller, { 0, 1, 2 } );
}

TEST_CASE( "Physics sleep controller: automatic point-joint wake applies current-step force exactly once" )
{
    SleepFixture fixture;
    fixture.AddSphere( Vector3( 0.0f, 0.0f, 0.0f ) );
    fixture.AddSphere( Vector3( 10.0f, 0.0f, 0.0f ) );
    fixture.Mirror();
    fixture.Sleep( 1 );

    const std::array<PointJointConstraint, 1> joints = { fixture.Joint( 0, 1 ) };
    PhysicsWorldForces worldForces;
    worldForces.gravity = -12.0f;
    std::array<BuoyancyBodyFacts, 2> buoyancyFacts = {};
    std::array<float, 2> timeRemaining = { 0.0f, 0.0f };
    constexpr float dt = 0.25f;

    fixture.owners.controller.WakePointJointConnectedBodies( fixture.owners.bodies, fixture.owners.colliders, {},
                                                             worldForces, buoyancyFacts, timeRemaining, fixture.WakeAccess(),
                                                             joints, dt );

    CHECK( fixture.owners.controller.GetSleepStates()[1] == 0u );
    CHECK( fixture.owners.bodies.HotFields().awake[1] == 1u );
    CHECK( timeRemaining[1] == doctest::Approx( dt ) );
    CHECK( fixture.owners.bodies.HotFields().linearVelocityY[1] == doctest::Approx( -3.0f ) );
    CheckAwakeIndices( fixture.owners.controller, { 0, 1 } );

    const float velocityAfterWake = fixture.owners.bodies.HotFields().linearVelocityY[1];
    fixture.owners.controller.WakePointJointConnectedBodies( fixture.owners.bodies, fixture.owners.colliders, {},
                                                             worldForces, buoyancyFacts, timeRemaining, fixture.WakeAccess(),
                                                             joints, dt );

    CHECK( fixture.owners.bodies.HotFields().linearVelocityY[1] == velocityAfterWake );
}

TEST_CASE( "Physics sleep controller: underwater lock refuses wake and disable-release rebuilds membership" )
{
    SleepFixture fixture;
    fixture.AddSphere( Vector3( 0.0f, 0.0f, 0.0f ) );
    fixture.Mirror();
    fixture.Sleep( 0 );

    PhysicsWorldForces worldForces;
    worldForces.fluidSurfaceHeight = 10.0f;
    worldForces.fluidDensity = 1000.0f;
    std::array<BuoyancyBodyFacts, 1> buoyancyFacts = {};
    std::array<float, 1> timeRemaining = { 0.125f };
    fixture.owners.controller.LockUnderwaterSleeperIfReady( worldForces, fixture.owners.bodies, fixture.owners.colliders,
                                                            buoyancyFacts, timeRemaining, 0 );

    REQUIRE( fixture.owners.controller.GetUnderwaterSleepLocks()[0] == 1u );
    CHECK( timeRemaining[0] == 0.0f );
    CHECK( fixture.owners.controller.GetSleepStates()[0] == 1u );
    CHECK( fixture.owners.bodies.HotFields().awake[0] == 0u );

    fixture.owners.controller.WakeModel( fixture.owners.bodies, fixture.owners.colliders, worldForces, buoyancyFacts,
                                         timeRemaining, fixture.WakeAccess(), 0 );

    CHECK( fixture.owners.controller.GetUnderwaterSleepLocks()[0] == 1u );
    CHECK( fixture.owners.controller.GetSleepStates()[0] == 1u );
    CHECK( fixture.owners.bodies.HotFields().awake[0] == 0u );

    fixture.owners.controller.SetPhysicsSleepEnabled( false );
    CHECK( fixture.owners.controller.GetUnderwaterSleepLocks()[0] == 0u );
    CHECK( fixture.owners.controller.GetSleepStates()[0] == 0u );
    REQUIRE( fixture.owners.controller.MirrorFlagsFrom( fixture.owners.bodies, 1 ) );
    CHECK( fixture.owners.bodies.HotFields().awake[0] == 1u );
    CheckAwakeIndices( fixture.owners.controller, { 0 } );

    // Re-enable above the fluid and reseed once. The world-aware explicit path
    // refreshes submersion, observes no lock, and performs the ordinary wake.
    fixture.owners.controller.SetPhysicsSleepEnabled( true );
    worldForces.fluidSurfaceHeight = -10.0f;
    fixture.Sleep( 0 );
    fixture.owners.controller.WakeModel( fixture.owners.bodies, fixture.owners.colliders, worldForces, buoyancyFacts,
                                         timeRemaining, fixture.WakeAccess(), 0 );

    CHECK( fixture.owners.controller.GetUnderwaterSleepLocks()[0] == 0u );
    CHECK( fixture.owners.controller.GetSleepStates()[0] == 0u );
    CHECK( fixture.owners.bodies.HotFields().awake[0] == 1u );
    CheckAwakeIndices( fixture.owners.controller, { 0 } );
}

TEST_CASE( "Physics sleep controller: awake-list remove add and cold rebuild preserve exact membership" )
{
    SleepFixture fixture;

    for ( int bodyIndex = 0; bodyIndex < 5; ++bodyIndex )
    {
        fixture.AddSphere( Vector3( static_cast<float>( bodyIndex ) * 100.0f, 0.0f, 0.0f ) );
    }

    fixture.Mirror();
    CheckAwakeIndices( fixture.owners.controller, { 0, 1, 2, 3, 4 } );
    fixture.Sleep( 3 );
    fixture.Sleep( 1 );
    CheckAwakeIndices( fixture.owners.controller, { 0, 2, 4 } );

    fixture.owners.controller.WakeModel( fixture.owners.bodies, fixture.WakeAccess(), 3 );
    CheckAwakeIndices( fixture.owners.controller, { 0, 2, 3, 4 } );
    fixture.owners.controller.WakeModel( fixture.owners.bodies, fixture.WakeAccess(), 1 );
    CheckAwakeIndices( fixture.owners.controller, { 0, 1, 2, 3, 4 } );

    // A same-count topology edit bypasses incremental add/remove. The cold
    // boundary imports stable-handle sleep state and discards dense-row timers
    // because compaction may have replaced any row identity.
    fixture.owners.bodies.MutableHotFields().fixed[2] = 1u;
    REQUIRE( fixture.owners.bodies.SeedBodyAsleep( fixture.handles[4] ) );
    fixture.owners.controller.InvalidateBodyTopology();
    REQUIRE( fixture.owners.controller.MirrorFlagsFrom( fixture.owners.bodies, 5 ) );
    CheckAwakeIndices( fixture.owners.controller, { 0, 1, 3 } );
}

TEST_CASE( "Physics sleep controller: compacted queued wakes use stable handles only" )
{
    SleepFixture fixture;

    for ( int bodyIndex = 0; bodyIndex < 4; ++bodyIndex )
    {
        fixture.AddSphere( Vector3( static_cast<float>( bodyIndex ), 0.0f, 0.0f ) );
    }

    fixture.Mirror();
    for ( int bodyIndex = 0; bodyIndex < 4; ++bodyIndex )
    {
        fixture.Sleep( bodyIndex );
    }

    std::array<PersistentContact, 2> contacts;
    contacts[0].bodyA = 0;
    contacts[0].bodyB = 1;
    contacts[1].bodyA = 2;
    contacts[1].bodyB = 3;
    const std::array<PointJointConstraint, 0> noJoints = {};
    fixture.owners.controller.RestoreSimulationIslandTopology( fixture.owners.bodies, contacts, noJoints );
    fixture.owners.controller.QueueConstraintTopologyWake( fixture.handles[0], fixture.handles[2] );

    REQUIRE( fixture.owners.bodies.DestroyBodyRecord( fixture.handles[1] ) );
    fixture.owners.controller.InvalidateBodyTopology();
    REQUIRE( fixture.owners.controller.MirrorFlagsFrom( fixture.owners.bodies, 3 ) );

    REQUIRE( fixture.owners.bodies.ModelIndexForHandle( fixture.handles[3] ) == 1 );
    REQUIRE( fixture.owners.bodies.ModelIndexForHandle( fixture.handles[2] ) == 2 );
    CHECK( fixture.owners.controller.GetSleepStates()[0] == 0u );
    CHECK( fixture.owners.controller.GetSleepStates()[1] == 1u );
    CHECK( fixture.owners.controller.GetSleepStates()[2] == 0u );
    CheckAwakeIndices( fixture.owners.controller, { 0, 2 } );
}


TEST_CASE( "Physics sleep controller: awake-list Debug classifier rejects fixed and sleeping rows" )
{
    using SkullbonezCore::Physics::PhysicsSleepControllerTestAccess;
    CHECK( PhysicsSleepControllerTestAccess::IsAwakeListEntryConsistent( false, false ) );
    CHECK_FALSE( PhysicsSleepControllerTestAccess::IsAwakeListEntryConsistent( true, false ) );
    CHECK_FALSE( PhysicsSleepControllerTestAccess::IsAwakeListEntryConsistent( false, true ) );
    CHECK_FALSE( PhysicsSleepControllerTestAccess::IsAwakeListEntryConsistent( true, true ) );
}

TEST_CASE( "Physics sleep controller: point-joint support is bidirectional and reaches a fixed point" )
{
    SleepFixture fixture;
    fixture.AddSphere( Vector3( 0.0f, 0.0f, 0.0f ), PhysicsBodyMotionKind::Fixed );
    fixture.AddSphere( Vector3( 1.0f, 0.0f, 0.0f ) );
    fixture.AddSphere( Vector3( 2.0f, 0.0f, 0.0f ) );
    fixture.Mirror();

    const std::array<PointJointConstraint, 2> joints = { fixture.Joint( 0, 1 ), fixture.Joint( 1, 2 ) };
    fixture.owners.controller.AppendPointJointSupportEdges( fixture.owners.bodies, joints, 3 );
    const std::span<const std::pair<int, int>> edges = fixture.owners.controller.GetSleepSupportEdges();
    REQUIRE( edges.size() == 4u );
    CHECK( ContainsSupportEdge( edges, 0, 1 ) );
    CHECK( ContainsSupportEdge( edges, 1, 0 ) );
    CHECK( ContainsSupportEdge( edges, 1, 2 ) );
    CHECK( ContainsSupportEdge( edges, 2, 1 ) );

    fixture.owners.controller.PropagateSupport( fixture.owners.bodies );
    const std::span<const uint8_t> supported = fixture.owners.controller.GetSleepSupportedStates();
    REQUIRE( supported.size() == 3u );
    CHECK( supported[0] == 1u );
    CHECK( supported[1] == 1u );
    CHECK( supported[2] == 1u );
    CheckAwakeIndices( fixture.owners.controller, { 1, 2 } );
}
