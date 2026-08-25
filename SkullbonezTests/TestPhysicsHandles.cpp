// Purpose:
//   Lock body/collider handle semantics, replay topology identity, and aligned
//   buoyancy-row lifecycle.

// Invariants:
//   - HandleForModelIndex() and ModelIndexForHandle() are inverse for live rows.
//   - Destroying a middle row moves the final row down and updates its handle map.
//   - Collider hot and authoring rows compact together under the same handle.
//   - Shareable hull identities reuse one stable row until store clear; unique
//     identities retain independent rows through collider deletion.
//   - Buoyancy facts remain a compact five-float row and compact by swap-last.
//   - Reused handle slots must increment generation before accepting new records.
//   - Hot state has one authority: aligned SoA arrays; cold records do not
//     duplicate pose, velocity, inertia, motion-kind, or sleep fields.
//   - Sleep-state export admits exactly the live body count, and collider
//     refresh mismatch leaves the pre-existing identity/shape row untouched.
//   - Partially submerged box and convex-hull forces remain finite and produce
//     an upward velocity response.
//   - Version 4 replay snapshots reject malformed motion-state size/bits before
//     mutation; legacy version 2 restores cold motion classification.
//   - The production Replay prediction reserve adapter preserves committed
//     scene capacity, owns an independent collider graph after source
//     destruction, copies point-joint warm-start state, retains a per-test
//     terrain owner through both engine lifetimes, and advances identical
//     seeded state bit-for-bit.
//   - Replay prefix trim stably compacts point-joint rows before retiring body
//     handles, so checkpoint topology ordinals remain valid and no doomed row leaks.
//   - Replay restore rejects malformed dense counts, references, contact keys,
//     cache order/uniqueness, terrain flags, and capacities transactionally;
//     every prior owner snapshot remains byte-for-byte unchanged.
//   - The exact reserve census includes every registered SpatialGrid owner and
//     rejects any unregistered growth or capacity-reporting row.

#include "../ThirdPtySource/doctest/doctest.h"
#include "TestColliderStoreFixtures.h"
#include "TestResultLoadFixtures.h"
#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#include "../SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h"
#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/Core/WorkerPool.h"
#include "TestCollisionShapeFixtures.h"
#include "TestFixedSeed.h"

#include "../SkullbonezSource/Physics/ColliderStore.h"
#include "../SkullbonezSource/Physics/BuoyancySystem.h"
#include "../SkullbonezSource/Physics/ConvexHullShape.h"
#include "../SkullbonezSource/Physics/PhysicsApi.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Physics/PhysicsEngine.h"
#include "../SkullbonezSource/Physics/PhysicsFixedList.h"
#include "../SkullbonezSource/Physics/PersistentContactSolver.h"
#include "../SkullbonezSource/Physics/PhysicsTimestep.h"
#include "../SkullbonezSource/Physics/PhysicsWorld.h"
#include "../SkullbonezSource/Physics/PhysicsWorldForces.h"
#include "../SkullbonezSource/Physics/Stages/ExternalForceStage.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsForceStage.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsSleepController.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsTerrainStage.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPredictionRetainedMemory.h"
#include "../SkullbonezSource/Runtime/App/ReplayRestoreOperations.h"
#include "../SkullbonezSource/World/Terrain.h"

#include <cstddef>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <limits>
#include <memory>
#include <type_traits>
#include "../SkullbonezSource/Core/SbDiagnosticStore.h"

namespace
{
SkullbonezCore::Core::SbDiagnosticStore diagnostics;
}

using SkullbonezCore::Geometry::Terrain;
using SkullbonezCore::Math::CollisionDetection::BoundingBox;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::CollisionShape;
using SkullbonezCore::Math::CollisionDetection::GetShapeIf;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::BuoyancyBodyFacts;
using SkullbonezCore::Physics::BuoyancySystem;
using SkullbonezCore::Physics::ColliderAuthoringRecord;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderShapeKind;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::HullShapeIdentity;
using SkullbonezCore::Physics::MakePhysicsSceneObjectId;
using SkullbonezCore::Physics::MakeShareableHullShapeIdentity;
using SkullbonezCore::Physics::ModelRowHint;
using SkullbonezCore::Physics::PhysicsBodyCreateDesc;
using SkullbonezCore::Physics::PhysicsBodyCreateRecord;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyHotState;
using SkullbonezCore::Physics::PhysicsBodyPosition;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsColliderHandle;
using SkullbonezCore::Physics::PhysicsEngine;
using SkullbonezCore::Physics::PhysicsWorldForces;
using SkullbonezCore::Runtime::ReplayRestoreOperations;
using SkullbonezCore::Runtime::ReplaySolverBodySample;
using SkullbonezCore::Runtime::ReplaySolverFrameSample;
using SkullbonezCore::Threading::LockOrderValidator;
using SkullbonezCore::Threading::WorkerPool;
using SkullbonezTests::CollisionShapeFixtures::BoxShape;

namespace
{
template <typename T>
inline constexpr bool PHYSICS_OWNER_IS_NON_TRANSFERABLE = !std::is_copy_constructible_v<T> &&
                                                          !std::is_copy_assignable_v<T> &&
                                                          !std::is_move_constructible_v<T> && !std::is_move_assignable_v<T>;

using PhysicsFixedListTransferProbe = SkullbonezCore::Physics::PhysicsFixedList<int, 2>;
static_assert( PHYSICS_OWNER_IS_NON_TRANSFERABLE<PhysicsFixedListTransferProbe> );
static_assert( PHYSICS_OWNER_IS_NON_TRANSFERABLE<PhysicsEngine> );
static_assert( PHYSICS_OWNER_IS_NON_TRANSFERABLE<PhysicsBodyStore> );
static_assert( PHYSICS_OWNER_IS_NON_TRANSFERABLE<ColliderStore> );
static_assert( PHYSICS_OWNER_IS_NON_TRANSFERABLE<BuoyancySystem> );
static_assert( PHYSICS_OWNER_IS_NON_TRANSFERABLE<SkullbonezCore::Physics::PhysicsWorld> );
static_assert( PHYSICS_OWNER_IS_NON_TRANSFERABLE<SkullbonezCore::Physics::PhysicsForceStage> );
static_assert( PHYSICS_OWNER_IS_NON_TRANSFERABLE<SkullbonezCore::Physics::ExternalForceStage> );
static_assert( PHYSICS_OWNER_IS_NON_TRANSFERABLE<SkullbonezCore::Physics::PhysicsBroadphaseStage> );
static_assert( PHYSICS_OWNER_IS_NON_TRANSFERABLE<SkullbonezCore::Physics::PhysicsNarrowphaseStage> );
static_assert( PHYSICS_OWNER_IS_NON_TRANSFERABLE<SkullbonezCore::Physics::PhysicsTerrainStage> );
static_assert( PHYSICS_OWNER_IS_NON_TRANSFERABLE<SkullbonezCore::Physics::PhysicsContactSolverStage> );
static_assert( PHYSICS_OWNER_IS_NON_TRANSFERABLE<SkullbonezCore::Physics::PhysicsSleepController> );
static_assert( PHYSICS_OWNER_IS_NON_TRANSFERABLE<SkullbonezCore::Physics::PhysicsStepDiagnostics> );

PhysicsBodyCreateRecord MakeBodyRecord( uint32_t sceneObjectIdValue, const Vector3& position )
{
    PhysicsBodyCreateRecord record;
    record.cold.sceneObjectId = MakePhysicsSceneObjectId( sceneObjectIdValue );
    record.hot.position = position;
    record.cold.mass = 1.0f;
    record.hot.inverseMass = 1.0f;
    return record;
}

ColliderRecord MakeColliderRecord( PhysicsBodyHandle body, uint32_t sceneObjectIdValue, float radius )
{
    ColliderRecord record;
    record.body = body;
    record.sceneObjectId = MakePhysicsSceneObjectId( sceneObjectIdValue );
    record.boundingRadius = radius;
    return record;
}

CollisionShape MakeColliderShape( float radius )
{
    return BoundingSphere( radius, Vector3( 0.0f, 0.0f, 0.0f ), 0.0f );
}

bool FloatBitsEqual( float left, float right )
{
    return std::memcmp( &left, &right, sizeof( left ) ) == 0;
}

void CheckVectorBitsEqual( const Vector3& left, const Vector3& right )
{
    CHECK( FloatBitsEqual( left.x, right.x ) );
    CHECK( FloatBitsEqual( left.y, right.y ) );
    CHECK( FloatBitsEqual( left.z, right.z ) );
}

void CheckHotStateBitsEqual( const PhysicsBodyHotState& left, const PhysicsBodyHotState& right )
{
    CheckVectorBitsEqual( left.position, right.position );
    CheckVectorBitsEqual( left.linearVelocity, right.linearVelocity );
    CheckVectorBitsEqual( left.angularVelocity, right.angularVelocity );
    CheckVectorBitsEqual( left.inverseRotationalInertia, right.inverseRotationalInertia );

    float leftOrientation[4] = {};
    float rightOrientation[4] = {};
    left.orientation.GetComponents( leftOrientation[0], leftOrientation[1], leftOrientation[2], leftOrientation[3] );
    right.orientation.GetComponents( rightOrientation[0], rightOrientation[1], rightOrientation[2], rightOrientation[3] );

    for ( std::size_t component = 0; component < std::size( leftOrientation ); ++component )
    {
        CHECK( FloatBitsEqual( leftOrientation[component], rightOrientation[component] ) );
    }

    CHECK( FloatBitsEqual( left.inverseMass, right.inverseMass ) );
    CHECK( FloatBitsEqual( left.boundingRadius, right.boundingRadius ) );
    CHECK( left.fixed == right.fixed );
    CHECK( left.awake == right.awake );
}

ColliderAuthoringRecord MakeColliderAuthoringRecord( const char* contactMaterialName )
{
    ColliderAuthoringRecord record;
    strncpy_s( record.contactMaterialName, contactMaterialName, _TRUNCATE );
    return record;
}

PhysicsBodyStore& TestBodyStore()
{

    // Why: PhysicsBodyStore owns runtime-reserved arrays; static storage keeps
    // the focused unit fixture off the doctest thread stack.
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

    // Why: ColliderStore mirrors runtime fixed storage, so tests reuse one
    // static fixture and Clear() it between cases instead of stack-allocating it.
    static ColliderStore store;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        store.ReserveCapacity( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
        store.ReserveShapeCapacity( 16u, 4u, 4u );
    }
    store.Clear();
    return store;
}

ColliderShapeKind ShapeKind( const CollisionShape& shape )
{

    if ( std::holds_alternative<BoundingBox>( shape ) )
    {
        return ColliderShapeKind::Box;
    }

    if ( std::holds_alternative<SkullbonezCore::Math::CollisionDetection::ConvexHullShape>( shape ) )
    {
        return ColliderShapeKind::ConvexHull;
    }

    return ColliderShapeKind::Sphere;
}

void CheckUnderwaterForcePath( const CollisionShape& shape, uint32_t sceneId )
{
    PhysicsBodyStore& bodies = TestBodyStore();
    ColliderStore& colliders = TestColliderStore();

    PhysicsBodyCreateRecord body;
    BuoyancyBodyFacts buoyancyFacts;
    body.cold.sceneObjectId = SkullbonezCore::Physics::PhysicsSceneObjectId { sceneId };
    body.cold.mass = 4.0f;
    buoyancyFacts.volume = SkullbonezCore::Math::CollisionDetection::GetShapeVolume( shape );
    buoyancyFacts.projectedSurfaceArea = SkullbonezCore::Math::CollisionDetection::GetShapeProjectedSurfaceArea( shape );
    buoyancyFacts.dragCoefficient = 0.4f;
    body.cold.rotationalInertia = Vector3( 8.0f, 2.0f, 6.0f );
    body.cold.angularVelocityLimit = 100.0f;
    body.cold.usesWorldInertia = true;
    body.hot.position = Vector3( 20.0f, 0.25f, 20.0f );
    body.hot.orientation.RotateAboutAxis( Vector3( 0.0f, 0.0f, 1.0f ), 0.35f );
    body.hot.linearVelocity = Vector3( 3.0f, -1.0f, 2.0f );
    body.hot.angularVelocity = Vector3( 1.0f, 2.0f, -1.0f );
    body.hot.inverseMass = 0.25f;
    body.hot.inverseRotationalInertia = Vector3( 0.125f, 0.5f, 1.0f / 6.0f );
    body.hot.boundingRadius = SkullbonezCore::Math::CollisionDetection::GetShapeBoundingRadius( shape );
    const auto handle = bodies.CreateBodyRecord( body );
    REQUIRE( handle.IsValid() );

    ColliderRecord collider;
    collider.body = handle;
    collider.sceneObjectId = body.cold.sceneObjectId;
    collider.shapeKind = ShapeKind( shape );
    collider.boundingRadius = body.hot.boundingRadius;
    collider.projectedSurfaceArea = buoyancyFacts.projectedSurfaceArea;
    collider.dragCoefficient = buoyancyFacts.dragCoefficient;
    REQUIRE( SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliders, collider, shape ).IsValid() );

    PhysicsWorldForces forces;
    forces.gravity = -9.8f;
    forces.fluidSurfaceHeight = 2.0f;
    forces.fluidDensity = 1000.0f;
    forces.gasDensity = 0.05f;
    forces.angularDragMultiplier = 2.0f;
    const Vector3 mutualForce( 1.0f, 0.0f, -0.5f );
    REQUIRE( bodies.ApplyForces( forces, colliders, {}, buoyancyFacts, 0, 1.0f / 120.0f, &mutualForce ) );
    const auto hot = bodies.HotFields();
    CHECK( std::isfinite( hot.linearVelocityX[0] ) );
    CHECK( std::isfinite( hot.linearVelocityY[0] ) );
    CHECK( std::isfinite( hot.angularVelocityZ[0] ) );
    CHECK( hot.linearVelocityY[0] > body.hot.linearVelocity.y );
}
} // namespace


TEST_CASE( "Physics body cold record: vector metadata retains its 16-byte boundary" )
{
    CHECK_EQ( offsetof( PhysicsBodyRecord, rotationalInertia ), 16u );
    CHECK_EQ( sizeof( BuoyancyBodyFacts ), sizeof( float ) * 5u );
}


TEST_CASE( "Buoyancy facts: refresh, swap-last erase, trim, and clear preserve dense row semantics" )
{

    // Why: the fixed-step owner relies on row equality with body/collider
    // stores. This focused lifecycle test catches field drift and compaction
    // mistakes without reaching through PhysicsEngine internals.
    static BuoyancySystem buoyancy;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        buoyancy.ReserveCapacity( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    }
    buoyancy.Clear();

    PhysicsBodyCreateDesc first;
    first.volume = 1.0f;
    first.projectedSurfaceArea = 2.0f;
    first.dragCoefficient = 3.0f;
    first.contactEpsilon = 0.01f;
    PhysicsBodyCreateDesc middle = first;
    middle.volume = 11.0f;
    middle.projectedSurfaceArea = 12.0f;
    middle.dragCoefficient = 13.0f;
    middle.contactEpsilon = 0.02f;
    PhysicsBodyCreateDesc last = first;
    last.volume = 21.0f;
    last.projectedSurfaceArea = 22.0f;
    last.dragCoefficient = 23.0f;
    last.contactEpsilon = 0.03f;

    REQUIRE( buoyancy.AppendBodyFacts( first ) );
    REQUIRE( buoyancy.AppendBodyFacts( middle ) );
    REQUIRE( buoyancy.AppendBodyFacts( last ) );
    REQUIRE( buoyancy.Count() == 3 );
    buoyancy.MutableFacts()[0].submergedVolumePercent = 0.75f;

    first.volume = 4.0f;
    REQUIRE( buoyancy.RefreshBodyFacts( 0, first ) );
    CHECK( buoyancy.Facts()[0].volume == doctest::Approx( 4.0f ) );
    CHECK( buoyancy.Facts()[0].submergedVolumePercent == doctest::Approx( 0.0f ) );

    REQUIRE( buoyancy.EraseBodyFactsSwapLast( 1 ) );
    REQUIRE( buoyancy.Count() == 2 );
    CHECK( buoyancy.Facts()[1].volume == doctest::Approx( 21.0f ) );
    CHECK( buoyancy.Facts()[1].projectedSurfaceArea == doctest::Approx( 22.0f ) );
    CHECK( buoyancy.Facts()[1].dragCoefficient == doctest::Approx( 23.0f ) );
    CHECK( buoyancy.Facts()[1].contactEpsilon == doctest::Approx( 0.03f ) );

    REQUIRE( buoyancy.TrimToCount( 1 ) );
    CHECK( buoyancy.Count() == 1 );
    buoyancy.Clear();
    CHECK( buoyancy.Facts().empty() );
}


TEST_CASE( "Physics handles: body store resolves fresh handles and scene object ids" )
{
    PhysicsBodyStore& store = TestBodyStore();
    const PhysicsBodyHandle first = store.CreateBodyRecord( MakeBodyRecord( 101u, Vector3( 1.0f, 0.0f, 0.0f ) ) );
    const PhysicsBodyHandle second = store.CreateBodyRecord( MakeBodyRecord( 202u, Vector3( 2.0f, 0.0f, 0.0f ) ) );

    CHECK( first.IsValid() );
    CHECK( second.IsValid() );
    CHECK( store.Count() == 2 );
    CHECK( store.HandleForModelIndex( 0 ) == first );
    CHECK( store.HandleForModelIndex( 1 ) == second );
    CHECK( store.ModelIndexForHandle( first ) == 0 );
    CHECK( store.ModelIndexForHandle( second ) == 1 );
    REQUIRE( store.RecordForHandle( second ) != nullptr );
    CHECK( store.RecordForHandle( second )->sceneObjectId == MakePhysicsSceneObjectId( 202u ) );
    CHECK( store.HandleForSceneObjectId( MakePhysicsSceneObjectId( 202u ), 1 ) == second );
    CHECK( store.HandleForSceneObjectId( MakePhysicsSceneObjectId( 202u ), 0 ) == second );
}


TEST_CASE( "Physics body SoA: aligned hot fields are the sole hot-state authority" )
{
    PhysicsBodyStore& store = TestBodyStore();
    PhysicsBodyCreateRecord initial = MakeBodyRecord( 303u, Vector3( 1.25f, -2.5f, 3.75f ) );
    initial.hot.orientation = SkullbonezCore::Math::Orientation::Quaternion( 0.1f, 0.2f, 0.3f, 0.9f );
    initial.hot.linearVelocity = Vector3( 4.0f, 5.0f, 6.0f );
    initial.hot.angularVelocity = Vector3( 7.0f, 8.0f, 9.0f );
    initial.hot.inverseRotationalInertia = Vector3( 0.25f, 0.5f, 0.75f );
    initial.hot.boundingRadius = 2.25f;
    const PhysicsBodyHandle body = store.CreateBodyRecord( initial );

    const auto hot = static_cast<const PhysicsBodyStore&>( store ).HotFields();
    REQUIRE( hot.positionX.size() == 1u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.positionX.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.positionY.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.positionZ.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.orientationX.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.orientationY.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.orientationZ.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.orientationW.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.linearVelocityX.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.linearVelocityY.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.linearVelocityZ.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.angularVelocityX.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.angularVelocityY.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.angularVelocityZ.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.inverseMass.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.inverseInertiaX.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.inverseInertiaY.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.inverseInertiaZ.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.boundingRadius.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.fixed.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.awake.data() ) % 32u == 0u );
    CHECK( hot.positionX[0] == initial.hot.position.x );
    CHECK( hot.positionY[0] == initial.hot.position.y );
    CHECK( hot.positionZ[0] == initial.hot.position.z );
    CHECK( hot.linearVelocityZ[0] == initial.hot.linearVelocity.z );
    CHECK( hot.inverseInertiaY[0] == initial.hot.inverseRotationalInertia.y );

    auto mutableHot = store.MutableHotFields();
    mutableHot.positionX[0] = -11.5f;
    mutableHot.linearVelocityY[0] = 12.25f;
    mutableHot.awake[0] = 0u;
    REQUIRE( store.RecordForHandle( body ) != nullptr );
    CHECK( mutableHot.positionX[0] == -11.5f );
    CHECK( mutableHot.linearVelocityY[0] == 12.25f );
    CHECK( mutableHot.awake[0] == 0u );

    mutableHot.angularVelocityZ[0] = -6.5f;
    mutableHot.inverseMass[0] = 0.125f;
    const auto refreshedHot = static_cast<const PhysicsBodyStore&>( store ).HotFields();
    CHECK( refreshedHot.angularVelocityZ[0] == -6.5f );
    CHECK( refreshedHot.inverseMass[0] == 0.125f );
}


TEST_CASE( "Physics handles: descriptor reorder preserves handle-owned pending impulse" )
{
    PhysicsBodyStore& store = TestBodyStore();
    const PhysicsBodyHandle first = store.CreateBodyRecord( MakeBodyRecord( 101u, Vector3( 1.0f, 0.0f, 0.0f ) ) );
    const PhysicsBodyHandle second = store.CreateBodyRecord( MakeBodyRecord( 202u, Vector3( 2.0f, 0.0f, 0.0f ) ) );
    const Vector3 impulse( 7.0f, 8.0f, 9.0f );
    const Vector3 applicationPoint( 1.0f, 2.0f, 3.0f );
    REQUIRE( store.SetPendingBodyImpulse( second, impulse, applicationPoint ) );

    PhysicsBodyCreateDesc reordered[2];
    reordered[0].sceneObjectId = MakePhysicsSceneObjectId( 202u );
    reordered[0].position = Vector3( 20.0f, 0.0f, 0.0f );
    reordered[1].sceneObjectId = MakePhysicsSceneObjectId( 101u );
    reordered[1].position = Vector3( 10.0f, 0.0f, 0.0f );
    const uint8_t awakeRows[2] = {};

    store.LoadFromDescriptors( reordered, awakeRows );

    CHECK( store.HandleForModelIndex( 0 ) == second );
    CHECK( store.HandleForModelIndex( 1 ) == first );
    REQUIRE( store.RecordForHandle( second ) != nullptr );
    CHECK( store.RecordForHandle( second )->hasPendingImpulse );
    CHECK( store.RecordForHandle( second )->pendingImpulse == impulse );
    CHECK( store.RecordForHandle( second )->pendingImpulseWorldOffset == applicationPoint );
    REQUIRE( store.RecordForHandle( first ) != nullptr );
    CHECK_FALSE( store.RecordForHandle( first )->hasPendingImpulse );
}


TEST_CASE( "Replay restore: stable body ids override stale row hints" )
{
    PhysicsBodyStore& store = TestBodyStore();
    const PhysicsBodyHandle first = store.CreateBodyRecord( MakeBodyRecord( 101u, Vector3( 1.0f, 0.0f, 0.0f ) ) );
    const PhysicsBodyHandle second = store.CreateBodyRecord( MakeBodyRecord( 202u, Vector3( 2.0f, 0.0f, 0.0f ) ) );

    ReplaySolverFrameSample sample;
    ReplaySolverBodySample firstSample;
    firstSample.id.value = 101u;
    firstSample.modelRow = SkullbonezCore::Physics::MakeModelRowHint( 1 );
    sample.bodies.push_back( firstSample );
    ReplaySolverBodySample secondSample;
    secondSample.id.value = 202u;
    secondSample.modelRow = SkullbonezCore::Physics::MakeModelRowHint( 0 );
    sample.bodies.push_back( secondSample );

    ReplayRestoreOperations::ResolvedBodyTable resolved {};
    char reason[128] = {};
    REQUIRE( ReplayRestoreOperations::ResolveBodiesForRestore( store, sample, resolved, reason, sizeof( reason ) ) );
    CHECK( resolved[0] == first );
    CHECK( resolved[1] == second );

    sample.bodies[0].id.value = 999u;
    CHECK_FALSE( ReplayRestoreOperations::ResolveBodiesForRestore( store, sample, resolved, reason, sizeof( reason ) ) );
    REQUIRE( store.RecordForHandle( first ) != nullptr );
    REQUIRE( store.RecordForHandle( second ) != nullptr );
    CHECK( PhysicsBodyPosition( store.HotFields(), static_cast<std::size_t>( store.ModelIndexForHandle( first ) ) ).x ==
           1.0f );

    CHECK( PhysicsBodyPosition( store.HotFields(), static_cast<std::size_t>( store.ModelIndexForHandle( second ) ) ).x ==
           2.0f );

    sample.bodies[0].id.value = 101u;
    sample.bodies[1].id.value = 101u;
    CHECK_FALSE( ReplayRestoreOperations::ResolveBodiesForRestore( store, sample, resolved, reason, sizeof( reason ) ) );
    CHECK( std::strstr( reason, "duplicate body ids" ) != nullptr );
}


TEST_CASE( "Physics handles: body destroy moves dense rows and rejects stale generations" )
{
    PhysicsBodyStore& store = TestBodyStore();
    const PhysicsBodyHandle first = store.CreateBodyRecord( MakeBodyRecord( 101u, Vector3( 1.0f, 0.0f, 0.0f ) ) );
    const PhysicsBodyHandle middle = store.CreateBodyRecord( MakeBodyRecord( 202u, Vector3( 2.0f, 0.0f, 0.0f ) ) );
    const PhysicsBodyHandle last = store.CreateBodyRecord( MakeBodyRecord( 303u, Vector3( 3.0f, 0.0f, 0.0f ) ) );

    CHECK( store.DestroyBodyRecord( middle ) );

    CHECK( store.Count() == 2 );
    CHECK( store.Contains( first ) );
    CHECK( store.Contains( last ) );
    CHECK_FALSE( store.Contains( middle ) );
    CHECK( store.ModelIndexForHandle( middle ) == -1 );
    CHECK( store.RecordForHandle( middle ) == nullptr );
    CHECK( store.HandleForModelIndex( 1 ) == last );
    CHECK( store.ModelIndexForHandle( last ) == 1 );
    REQUIRE( store.RecordForHandle( last ) != nullptr );
    CHECK( store.RecordForHandle( last )->sceneObjectId == MakePhysicsSceneObjectId( 303u ) );

    const PhysicsBodyHandle replacement = store.CreateBodyRecord( MakeBodyRecord( 404u, Vector3( 4.0f, 0.0f, 0.0f ) ) );
    CHECK( replacement.index == middle.index );
    CHECK( replacement.generation != middle.generation );
    CHECK( store.Contains( replacement ) );
    CHECK_FALSE( store.Contains( middle ) );
}


TEST_CASE( "Physics handles: body row hints self-heal and invalidate stale handles" )
{
    PhysicsBodyStore& store = TestBodyStore();
    const PhysicsBodyHandle first = store.CreateBodyRecord( MakeBodyRecord( 101u, Vector3( 1.0f, 0.0f, 0.0f ) ) );
    const PhysicsBodyHandle middle = store.CreateBodyRecord( MakeBodyRecord( 202u, Vector3( 2.0f, 0.0f, 0.0f ) ) );
    const PhysicsBodyHandle last = store.CreateBodyRecord( MakeBodyRecord( 303u, Vector3( 3.0f, 0.0f, 0.0f ) ) );

    ModelRowHint wrongHint;
    wrongHint.value = 2;
    CHECK( store.ResolveModelRow( first, wrongHint ) == 0 );
    CHECK( wrongHint.value == 0 );

    ModelRowHint movedHint;
    movedHint.value = 2;
    CHECK( store.DestroyBodyRecord( middle ) );
    CHECK( store.ResolveModelRow( last, movedHint ) == 1 );
    CHECK( movedHint.value == 1 );

    ModelRowHint staleHint;
    staleHint.value = 0;
    CHECK( store.DestroyBodyRecord( first ) );
    CHECK( store.ResolveModelRow( first, staleHint ) == -1 );
    CHECK( staleHint.value == -1 );
}


TEST_CASE( "Physics handles: collider store resolves body, scene, and model handles" )
{
    ColliderStore& store = TestColliderStore();
    PhysicsBodyHandle body;
    body.index = 7u;
    body.generation = 1u;

    const PhysicsColliderHandle
        collider = SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( store,
                                                                                 MakeColliderRecord( body, 707u, 3.0f ),
                                                                                 MakeColliderShape( 3.0f ) );

    CHECK( collider.IsValid() );
    CHECK( store.Count() == 1 );
    CHECK( store.HandleForModelIndex( 0 ) == collider );
    CHECK( store.ModelIndexForHandle( collider ) == 0 );
    CHECK( store.HandleForBodyHandle( body ) == collider );
    CHECK( store.HandleForSceneObjectId( MakePhysicsSceneObjectId( 707u ) ) == collider );
    REQUIRE( store.RecordForHandle( collider ) != nullptr );
    CHECK( store.RecordForHandle( collider )->boundingRadius == 3.0f );
}


TEST_CASE( "Physics handles: collider destroy moves rows and rejects stale handles" )
{
    ColliderStore& store = TestColliderStore();
    PhysicsBodyHandle bodyA { 11u, 1u };
    PhysicsBodyHandle bodyB { 12u, 1u };
    PhysicsBodyHandle bodyC { 13u, 1u };
    const PhysicsColliderHandle
        first = SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( store, MakeColliderRecord( bodyA, 111u, 1.0f ),
                                                                              MakeColliderShape( 1.0f ),
                                                                              MakeColliderAuthoringRecord( "stone" ) );

    const PhysicsColliderHandle
        middle = SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( store,
                                                                               MakeColliderRecord( bodyB, 222u, 2.0f ),
                                                                               MakeColliderShape( 2.0f ),
                                                                               MakeColliderAuthoringRecord( "metal" ) );

    const PhysicsColliderHandle
        last = SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( store, MakeColliderRecord( bodyC, 333u, 3.0f ),
                                                                             MakeColliderShape( 3.0f ),
                                                                             MakeColliderAuthoringRecord( "wood" ) );

    CHECK( store.DestroyColliderRecord( middle ) );

    CHECK( store.Count() == 2 );
    CHECK( store.Contains( first ) );
    CHECK( store.Contains( last ) );
    CHECK_FALSE( store.Contains( middle ) );
    CHECK( store.HandleForModelIndex( 1 ) == last );
    CHECK( store.ModelIndexForHandle( last ) == 1 );
    CHECK( store.HandleForBodyHandle( bodyC ) == last );
    REQUIRE( store.AuthoringRecordForHandle( last ) != nullptr );
    CHECK( std::strcmp( store.AuthoringRecordForHandle( last )->contactMaterialName, "wood" ) == 0 );

    const PhysicsColliderHandle replacement = SkullbonezTests::ColliderStoreFixtures::
        CreateColliderRecord( store, MakeColliderRecord( PhysicsBodyHandle { 14u, 1u }, 444u, 4.0f ),
                              MakeColliderShape( 4.0f ) );

    CHECK( replacement.index == middle.index );
    CHECK( replacement.generation != middle.generation );
    CHECK( store.Contains( replacement ) );
    CHECK_FALSE( store.Contains( middle ) );
}


TEST_CASE( "Physics handles: collider rows realign to compacted body handles" )
{
    PhysicsBodyStore& bodies = TestBodyStore();
    ColliderStore& colliders = TestColliderStore();
    const PhysicsBodyHandle first = bodies.CreateBodyRecord( MakeBodyRecord( 111u, Vector3( 1.0f, 0.0f, 0.0f ) ) );
    const PhysicsBodyHandle middle = bodies.CreateBodyRecord( MakeBodyRecord( 222u, Vector3( 2.0f, 0.0f, 0.0f ) ) );
    const PhysicsBodyHandle last = bodies.CreateBodyRecord( MakeBodyRecord( 333u, Vector3( 3.0f, 0.0f, 0.0f ) ) );
    const PhysicsColliderHandle
        firstCollider = SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliders,
                                                                                      MakeColliderRecord( first, 111u,
                                                                                                          1.0f ),
                                                                                      MakeColliderShape( 1.0f ) );

    const PhysicsColliderHandle
        middleCollider = SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliders,
                                                                                       MakeColliderRecord( middle, 222u,
                                                                                                           2.0f ),
                                                                                       MakeColliderShape( 2.0f ) );

    const PhysicsColliderHandle
        lastCollider = SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliders,
                                                                                     MakeColliderRecord( last, 333u, 3.0f ),
                                                                                     MakeColliderShape( 3.0f ) );

    REQUIRE( bodies.DestroyBodyRecord( middle ) );
    REQUIRE( colliders.DestroyColliderRecord( middleCollider ) );
    REQUIRE( colliders.RefreshBodyBindings( bodies ) );

    CHECK( colliders.HandleForModelIndex( 0 ) == firstCollider );
    CHECK( colliders.HandleForModelIndex( 1 ) == lastCollider );
    REQUIRE( colliders.Count() == 2 );
    CHECK( colliders.Records()[1].body == last );
    CHECK( colliders.Records()[1].sceneObjectId == MakePhysicsSceneObjectId( 333u ) );
}


TEST_CASE( "Physics handles: collider binding mismatch preserves every existing row" )
{
    PhysicsBodyStore& bodies = TestBodyStore();
    ColliderStore& colliders = TestColliderStore();
    const PhysicsBodyHandle firstBody = bodies.CreateBodyRecord( MakeBodyRecord( 111u, Vector3( 1.0f, 0.0f, 0.0f ) ) );
    bodies.CreateBodyRecord( MakeBodyRecord( 222u, Vector3( 2.0f, 0.0f, 0.0f ) ) );
    const PhysicsColliderHandle
        collider = SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliders,
                                                                                 MakeColliderRecord( firstBody, 111u, 1.0f ),
                                                                                 MakeColliderShape( 1.0f ) );
    REQUIRE( colliders.RecordForHandle( collider ) != nullptr );
    const ColliderRecord before = *colliders.RecordForHandle( collider );

    CHECK_FALSE( colliders.RefreshBodyBindings( bodies ) );
    REQUIRE( colliders.Count() == 1 );
    REQUIRE( colliders.RecordForHandle( collider ) != nullptr );
    const ColliderRecord& after = *colliders.RecordForHandle( collider );
    CHECK( after.handle == before.handle );
    CHECK( after.body == before.body );
    CHECK( after.sceneObjectId == before.sceneObjectId );
    CHECK( after.shape.StorageIndex() == before.shape.StorageIndex() );
}


TEST_CASE( "Physics handles: sphere compaction preserves first and final removal boundaries" )
{
    ColliderStore& store = TestColliderStore();
    const PhysicsColliderHandle
        first = SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( store,
                                                                              MakeColliderRecord( PhysicsBodyHandle { 1u,
                                                                                                                      1u },
                                                                                                  101u, 1.0f ),
                                                                              MakeColliderShape( 1.0f ) );
    const PhysicsColliderHandle
        middle = SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( store,
                                                                               MakeColliderRecord( PhysicsBodyHandle { 2u,
                                                                                                                       1u },
                                                                                                   202u, 2.0f ),
                                                                               MakeColliderShape( 2.0f ) );
    const PhysicsColliderHandle
        last = SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( store,
                                                                             MakeColliderRecord( PhysicsBodyHandle { 3u,
                                                                                                                     1u },
                                                                                                 303u, 3.0f ),
                                                                             MakeColliderShape( 3.0f ) );

    REQUIRE( store.DestroyColliderRecord( first ) );
    REQUIRE( store.SphereShapeCount() == 2u );
    REQUIRE( store.RecordForHandle( last ) != nullptr );
    REQUIRE( GetShapeIf<BoundingSphere>( &store.RecordForHandle( last )->shape ) != nullptr );
    CHECK( store.RecordForHandle( last )->shape.StorageIndex() == 0u );
    CHECK( GetShapeIf<BoundingSphere>( &store.RecordForHandle( last )->shape )->GetRadius() == doctest::Approx( 3.0f ) );

    REQUIRE( store.DestroyColliderRecord( last ) );
    REQUIRE( store.SphereShapeCount() == 1u );
    REQUIRE( store.RecordForHandle( middle ) != nullptr );
    CHECK( store.RecordForHandle( middle )->shape.StorageIndex() == 0u );
    REQUIRE( store.DestroyColliderRecord( middle ) );
    CHECK( store.SphereShapeCount() == 0u );
}


TEST_CASE( "Physics handles: sleep-state copy admits the exact live body count" )
{
    PhysicsBodyStore& bodies = TestBodyStore();
    const PhysicsBodyHandle awake = bodies.CreateBodyRecord( MakeBodyRecord( 111u, Vector3( 0.0f, 0.0f, 0.0f ) ) );
    const PhysicsBodyHandle sleeping = bodies.CreateBodyRecord( MakeBodyRecord( 222u, Vector3( 0.0f, 0.0f, 0.0f ) ) );
    REQUIRE( awake.IsValid() );
    REQUIRE( bodies.SeedBodyAsleep( sleeping ) );
    std::array<uint8_t, 2> sleepStates {};

    bodies.CopySleepStatesTo( sleepStates );

    CHECK( ( sleepStates == std::array<uint8_t, 2> { 0u, 1u } ) );
}

TEST_CASE( "Collider shape stores: hot rows stay compact and zero-hull scenes commit no hull payload" )
{
    CHECK( sizeof( ColliderRecord ) == 88u );

    auto store = std::make_unique<ColliderStore>();
    const CollisionShape sphereOne = MakeColliderShape( 1.0f );
    const CollisionShape sphereTwo = MakeColliderShape( 2.0f );
    const CollisionShape sphereThree = MakeColliderShape( 3.0f );
    PhysicsColliderHandle first;
    PhysicsColliderHandle middle;
    PhysicsColliderHandle last;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        store->ReserveCapacity( 3u );
        store->ReserveShapeCapacity( 3u, 1u, 0u );
        first = SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( *store,
                                                                              MakeColliderRecord( PhysicsBodyHandle { 1u,
                                                                                                                      1u },
                                                                                                  101u, 1.0f ),
                                                                              sphereOne );
        middle = SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( *store,
                                                                               MakeColliderRecord( PhysicsBodyHandle { 2u,
                                                                                                                       1u },
                                                                                                   202u, 2.0f ),
                                                                               sphereTwo );
        last = SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( *store,
                                                                             MakeColliderRecord( PhysicsBodyHandle { 3u,
                                                                                                                     1u },
                                                                                                 303u, 3.0f ),
                                                                             sphereThree );
    }

    REQUIRE( first.IsValid() );
    REQUIRE( middle.IsValid() );
    REQUIRE( last.IsValid() );
    CHECK( store->SphereShapeCount() == 3u );
    CHECK( store->BoxShapeCount() == 0u );
    CHECK( store->HullShapeCount() == 0u );
    CHECK( store->HullShapeCapacity() == 0u );
    REQUIRE( GetShapeIf<BoundingSphere>( &store->RecordForHandle( last )->shape ) != nullptr );
    CHECK( GetShapeIf<BoundingSphere>( &store->RecordForHandle( last )->shape )->GetRadius() == doctest::Approx( 3.0f ) );

    const BoundingSphere* firstSphereBeforeGrowth = GetShapeIf<BoundingSphere>( &store->RecordForHandle( first )->shape );
    const BoundingSphere* lastSphereBeforeGrowth = GetShapeIf<BoundingSphere>( &store->RecordForHandle( last )->shape );
    REQUIRE( firstSphereBeforeGrowth != nullptr );
    REQUIRE( lastSphereBeforeGrowth != nullptr );

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        store->ReserveShapeCapacity( 6u, 1u, 0u );
    }

    const BoundingSphere* firstSphereAfterGrowth = GetShapeIf<BoundingSphere>( &store->RecordForHandle( first )->shape );
    const BoundingSphere* lastSphereAfterGrowth = GetShapeIf<BoundingSphere>( &store->RecordForHandle( last )->shape );
    REQUIRE( firstSphereAfterGrowth != nullptr );
    REQUIRE( lastSphereAfterGrowth != nullptr );
    CHECK( store->SphereShapeCapacity() == 6u );
    CHECK( firstSphereAfterGrowth != firstSphereBeforeGrowth );
    CHECK( lastSphereAfterGrowth != lastSphereBeforeGrowth );
    CHECK( firstSphereAfterGrowth->GetRadius() == doctest::Approx( 1.0f ) );
    CHECK( lastSphereAfterGrowth->GetRadius() == doctest::Approx( 3.0f ) );
    CHECK( store->HullShapeCapacity() == 0u );

    REQUIRE( store->DestroyColliderRecord( middle ) );
    CHECK( store->SphereShapeCount() == 2u );
    REQUIRE( store->RecordForHandle( last ) != nullptr );
    REQUIRE( GetShapeIf<BoundingSphere>( &store->RecordForHandle( last )->shape ) != nullptr );
    CHECK( GetShapeIf<BoundingSphere>( &store->RecordForHandle( last )->shape )->GetRadius() == doctest::Approx( 3.0f ) );

    ColliderRecord replacement = *store->RecordForHandle( first );
    const CollisionShape box = BoundingBox( Vector3( 4.0f, 5.0f, 6.0f ), Vector3( 0.0f, 0.0f, 0.0f ) );
    REQUIRE( SkullbonezTests::ColliderStoreFixtures::UpdateRecordForHandle( *store, first, replacement, box ) );
    CHECK( store->SphereShapeCount() == 1u );
    CHECK( store->BoxShapeCount() == 1u );
    REQUIRE( GetShapeIf<BoundingBox>( &store->RecordForHandle( first )->shape ) != nullptr );
    CHECK( GetShapeIf<BoundingBox>( &store->RecordForHandle( first )->shape )->GetHalfExtents() ==
           Vector3( 4.0f, 5.0f, 6.0f ) );

    CHECK( store->HullShapeCapacity() == 0u );
}


TEST_CASE( "Collider hull shape store: canonical identities share stable scene-lifetime rows" )
{
    auto store = std::make_unique<ColliderStore>();
    SkullbonezCore::Math::CollisionDetection::ConvexHullShape hullShape;
    REQUIRE( SkullbonezTests::ResultLoadFixtures::TryLoadConvexHull( diagnostics, "SkullbonezData/hulls/pyramid.hull",
                                                                     hullShape ) );
    const CollisionShape hull = hullShape;
    const Vector3 unitScale( 1.0f, 1.0f, 1.0f );
    const HullShapeIdentity canonical = MakeShareableHullShapeIdentity( "SkullbonezData/hulls/pyramid.hull", unitScale );
    const HullShapeIdentity alternateSpelling = MakeShareableHullShapeIdentity( "SKULLBONEZDATA\\HULLS\\PYRAMID.HULL",
                                                                                unitScale );
    const HullShapeIdentity adjacentScale = MakeShareableHullShapeIdentity( "SkullbonezData/hulls/pyramid.hull",
                                                                            Vector3( std::nextafter( 1.0f, 2.0f ), 1.0f,
                                                                                     1.0f ) );
    char overlongPath[HullShapeIdentity::MAX_PATH_BYTES + 1u] = {};
    std::memset( overlongPath, 'a', HullShapeIdentity::MAX_PATH_BYTES );
    const HullShapeIdentity rejectedOverlong = MakeShareableHullShapeIdentity( overlongPath, unitScale );
    const HullShapeIdentity
        rejectedNonFinite = MakeShareableHullShapeIdentity( "SkullbonezData/hulls/pyramid.hull",
                                                            Vector3( ( std::numeric_limits<float>::infinity )(), 1.0f,
                                                                     1.0f ) );

    REQUIRE( canonical.shareable );
    REQUIRE( alternateSpelling.shareable );
    REQUIRE( adjacentScale.shareable );
    CHECK( std::strcmp( canonical.normalizedResolvedPath, "skullbonezdata/hulls/pyramid.hull" ) == 0 );
    CHECK( std::strcmp( alternateSpelling.normalizedResolvedPath, canonical.normalizedResolvedPath ) == 0 );
    CHECK( alternateSpelling == canonical );
    CHECK_FALSE( adjacentScale == canonical );
    CHECK( adjacentScale.scaleXBits != canonical.scaleXBits );
    CHECK( canonical.scaleYBits == SkullbonezCore::Physics::HullScaleBits( 1.0f ) );
    CHECK_FALSE( rejectedOverlong.shareable );
    CHECK_FALSE( rejectedNonFinite.shareable );

    PhysicsColliderHandle sharedFirst;
    PhysicsColliderHandle sharedSecond;
    PhysicsColliderHandle scaled;
    PhysicsColliderHandle uniqueFirst;
    PhysicsColliderHandle uniqueSecond;
    PhysicsColliderHandle overlongUnique;
    PhysicsColliderHandle nonFiniteUnique;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        store->ReserveCapacity( 8u );
        store->ReserveShapeCapacity( 0u, 0u, 8u );
        sharedFirst = store->CreateColliderRecord( MakeColliderRecord( PhysicsBodyHandle { 1u, 1u }, 1001u, 2.0f ), hull, {},
                                                   canonical );
        sharedSecond = store->CreateColliderRecord( MakeColliderRecord( PhysicsBodyHandle { 2u, 1u }, 1002u, 2.0f ), hull,
                                                    {}, alternateSpelling );
        scaled = store->CreateColliderRecord( MakeColliderRecord( PhysicsBodyHandle { 3u, 1u }, 1003u, 2.0f ), hull, {},
                                              adjacentScale );
        uniqueFirst = store->CreateColliderRecord( MakeColliderRecord( PhysicsBodyHandle { 4u, 1u }, 1004u, 2.0f ), hull, {},
                                                   HullShapeIdentity {} );
        uniqueSecond = store->CreateColliderRecord( MakeColliderRecord( PhysicsBodyHandle { 5u, 1u }, 1005u, 2.0f ), hull,
                                                    {}, HullShapeIdentity {} );
        overlongUnique = store->CreateColliderRecord( MakeColliderRecord( PhysicsBodyHandle { 6u, 1u }, 1006u, 2.0f ), hull,
                                                      {}, rejectedOverlong );
        nonFiniteUnique = store->CreateColliderRecord( MakeColliderRecord( PhysicsBodyHandle { 7u, 1u }, 1007u, 2.0f ), hull,
                                                       {}, rejectedNonFinite );
    }

    REQUIRE( sharedFirst.IsValid() );
    REQUIRE( sharedSecond.IsValid() );
    REQUIRE( scaled.IsValid() );
    REQUIRE( uniqueFirst.IsValid() );
    REQUIRE( uniqueSecond.IsValid() );
    REQUIRE( overlongUnique.IsValid() );
    REQUIRE( nonFiniteUnique.IsValid() );
    REQUIRE( store->RecordForHandle( sharedFirst ) != nullptr );
    REQUIRE( store->RecordForHandle( sharedSecond ) != nullptr );
    CHECK( store->HullShapeCount() == 6u );
    CHECK( store->RecordForHandle( sharedFirst )->shape.StorageIndex() == 0u );
    CHECK( store->RecordForHandle( sharedSecond )->shape.StorageIndex() == 0u );

    const auto* sharedPointer = GetShapeIf<SkullbonezCore::Math::CollisionDetection::ConvexHullShape>(
        &store->RecordForHandle( sharedSecond )->shape );
    const auto* scaledPointer = GetShapeIf<SkullbonezCore::Math::CollisionDetection::ConvexHullShape>(
        &store->RecordForHandle( scaled )->shape );
    const auto* uniquePointer = GetShapeIf<SkullbonezCore::Math::CollisionDetection::ConvexHullShape>(
        &store->RecordForHandle( uniqueSecond )->shape );
    const auto* overlongPointer = GetShapeIf<SkullbonezCore::Math::CollisionDetection::ConvexHullShape>(
        &store->RecordForHandle( overlongUnique )->shape );
    const auto* nonFinitePointer = GetShapeIf<SkullbonezCore::Math::CollisionDetection::ConvexHullShape>(
        &store->RecordForHandle( nonFiniteUnique )->shape );
    REQUIRE( sharedPointer != nullptr );
    REQUIRE( scaledPointer != nullptr );
    REQUIRE( uniquePointer != nullptr );
    REQUIRE( overlongPointer != nullptr );
    REQUIRE( nonFinitePointer != nullptr );

    // Invariant: hull rows live until Clear(), so dense collider deletion cannot
    // invalidate another collider's shared or unique cold geometry reference.
    REQUIRE( store->DestroyColliderRecord( sharedFirst ) );
    REQUIRE( store->DestroyColliderRecord( uniqueFirst ) );
    CHECK( store->HullShapeCount() == 6u );
    REQUIRE( store->RecordForHandle( sharedSecond ) != nullptr );
    REQUIRE( store->RecordForHandle( scaled ) != nullptr );
    REQUIRE( store->RecordForHandle( uniqueSecond ) != nullptr );
    CHECK( GetShapeIf<SkullbonezCore::Math::CollisionDetection::ConvexHullShape>(
               &store->RecordForHandle( sharedSecond )->shape ) == sharedPointer );
    CHECK( GetShapeIf<SkullbonezCore::Math::CollisionDetection::ConvexHullShape>(
               &store->RecordForHandle( scaled )->shape ) == scaledPointer );
    CHECK( GetShapeIf<SkullbonezCore::Math::CollisionDetection::ConvexHullShape>(
               &store->RecordForHandle( uniqueSecond )->shape ) == uniquePointer );
    CHECK( GetShapeIf<SkullbonezCore::Math::CollisionDetection::ConvexHullShape>(
               &store->RecordForHandle( overlongUnique )->shape ) == overlongPointer );
    CHECK( GetShapeIf<SkullbonezCore::Math::CollisionDetection::ConvexHullShape>(
               &store->RecordForHandle( nonFiniteUnique )->shape ) == nonFinitePointer );

    PhysicsColliderHandle recreated;
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        recreated = store->CreateColliderRecord( MakeColliderRecord( PhysicsBodyHandle { 8u, 1u }, 1008u, 2.0f ), hull, {},
                                                 alternateSpelling );
    }
    REQUIRE( recreated.IsValid() );
    REQUIRE( store->RecordForHandle( recreated ) != nullptr );
    CHECK( store->HullShapeCount() == 6u );
    CHECK( store->RecordForHandle( recreated )->shape.StorageIndex() == 0u );
    CHECK( GetShapeIf<SkullbonezCore::Math::CollisionDetection::ConvexHullShape>(
               &store->RecordForHandle( recreated )->shape ) == sharedPointer );
}


TEST_CASE( "Scene physics capacity commit is monotonic and grows each fixed owner once" )
{
    using SkullbonezCore::Core::Allocation::RuntimeAllocationPhase;
    using SkullbonezCore::Core::Allocation::RuntimeAllocationScope;
    using SkullbonezCore::Core::Allocation::RuntimeReserveAllocator;
    using SkullbonezCore::Core::Allocation::RuntimeReserveGrowthEventView;
    using SkullbonezCore::Core::Allocation::RuntimeReserveOwnerStatsView;
    using SkullbonezCore::Core::Allocation::RuntimeReserveSubsystem;

    auto engine = std::make_unique<PhysicsEngine>();

    {
        RuntimeAllocationScope sceneLoadScope( RuntimeAllocationPhase::SceneLoad );
        engine->ReserveAuthoredBodyCapacity( 300u, 150u, 150u, 0u, 12u );
    }

    CHECK( PhysicsEngine::ReadBodies( *engine ).RecordCapacity() == 300u );
    CHECK( PhysicsEngine::ReadColliders( *engine ).RecordCapacity() == 300u );
    CHECK( PhysicsEngine::ReadColliders( *engine ).SphereShapeCapacity() == 150u );
    CHECK( PhysicsEngine::ReadColliders( *engine ).BoxShapeCapacity() == 150u );
    CHECK( PhysicsEngine::ReadColliders( *engine ).HullShapeCapacity() == 0u );
    CHECK( PhysicsEngine::ReadPointJointConstraints( *engine ).capacity() == 12u );
    CHECK( PhysicsEngine::ReadPointJointCapacity( *engine ) == 12u );

    RuntimeReserveAllocator::ResetCounters();

    {
        RuntimeAllocationScope sceneLoadScope( RuntimeAllocationPhase::SceneLoad );
        engine->ReserveAuthoredBodyCapacity( 200u, 100u, 100u, 0u, 8u );
    }

    CHECK( RuntimeReserveAllocator::GrowthEventCount() == 0u );
    CHECK( PhysicsEngine::ReadBodies( *engine ).RecordCapacity() == 300u );
    CHECK( PhysicsEngine::ReadColliders( *engine ).SphereShapeCapacity() == 150u );
    CHECK( PhysicsEngine::ReadPointJointConstraints( *engine ).capacity() == 12u );
    CHECK( PhysicsEngine::ReadPointJointCapacity( *engine ) == 8u );

    RuntimeReserveAllocator::ResetCounters();

    {
        RuntimeAllocationScope sceneLoadScope( RuntimeAllocationPhase::SceneLoad );
        engine->ReserveAuthoredBodyCapacity( 2000u, 1000u, 1000u, 0u, 24u );
    }

    RuntimeReserveGrowthEventView events[128] = {};
    const int eventCount = RuntimeReserveAllocator::CopyRecentGrowthEvents( events, 128 );
#if defined( _DEBUG )
    REQUIRE( eventCount == 98 );
#else
    REQUIRE( eventCount == 98 );
#endif
    CHECK( static_cast<uint64_t>( eventCount ) == RuntimeReserveAllocator::GrowthEventCount() );

    for ( int eventIndex = 0; eventIndex < eventCount; ++eventIndex )
    {
        CHECK( events[eventIndex].granted );
        RuntimeReserveOwnerStatsView ownerStats = {};
        REQUIRE( RuntimeReserveAllocator::CopyOwnerStatsByName( events[eventIndex].ownerName, ownerStats ) );
        CHECK( ownerStats.subsystem == RuntimeReserveSubsystem::Physics );
        REQUIRE( ownerStats.capacityReason != nullptr );
        CHECK( ownerStats.capacityReason[0] != '\0' );
        CHECK( std::strcmp( ownerStats.capacityReason, "Scene-sized PhysicsFixedList backing storage" ) != 0 );
        CHECK( std::strcmp( ownerStats.capacityReason, "unspecified" ) != 0 );

        for ( int earlierIndex = 0; earlierIndex < eventIndex; ++earlierIndex )
        {
            CHECK( std::strcmp( events[eventIndex].ownerName, events[earlierIndex].ownerName ) != 0 );
        }
    }

    struct ExpectedFixedRowGrowth
    {
        const char* ownerName;
        int requestedCapacity;
    };
    const ExpectedFixedRowGrowth expectedFixedRowGrowth[] = {
        { "PhysicsEngine.m_authoredBodyDescs", 2000 },
        { "PhysicsWorld.timeRemaining", 2000 },
        { "PhysicsWorld.pointJointConstraints", 24 },
        { "PhysicsForceStage.m_mutualGravityForces", 2000 },
        { "PhysicsForceStage.m_mutualGravityPairForces", 130816 },
        { "ExternalForceStage.fixedTreeReleaseWakeScratch", 2000 },
        { "ExternalForceStage.releaseWakeBodies", 2000 },
        { "PhysicsMotionEligibilityStage.state", 2000 },
        { "PhysicsMotionEligibilityStage.linearTravelSquared", 2000 },
        { "PhysicsMotionEligibilityStage.linearDirectionalBoundary", 2000 },
        { "PhysicsMotionEligibilityStage.angularTravelSquared", 2000 },
        { "PhysicsMotionEligibilityStage.angularBroadphaseExpansion", 2000 },
        { "SpatialGrid.entries", 17024 },
        { "SpatialGrid.overlayEntries", 16000 },
        { "SpatialGrid.sweptFallbackBodies", 2000 },
        { "SpatialGrid.bodyMemberships", 2000 },
        { "SpatialGrid.pairSeen", 31235 },
        { "SpatialGrid.candidatePairHeads", 2000 },
        { "SpatialGrid.cellObjectSeen", 2000 },
        { "PhysicsNarrowphaseStage.islands", 2000 },
        { "PhysicsNarrowphaseStage.islandWriteOffsets", 2000 },
        { "PhysicsNarrowphaseStage.parent", 2000 },
        { "PhysicsNarrowphaseStage.rank", 2000 },
        { "PhysicsNarrowphaseStage.rootToIsland", 2000 },
        { "PhysicsContactSolverStage.persistentContacts", 147072 },
        { "PhysicsContactSolverStage.persistentContactCache", 147072 },
        { "PhysicsContactSolverStage.persistentContactCounts", 2000 },
        { "PhysicsContactSolverStage.persistentRestingContactCounts", 2000 },
        { "PhysicsContactSolverStage.solverBodies", 2000 },
        { "PhysicsContactSolverStage.fixedContactBodies", 147072 },
        { "PhysicsContactSolverStage.releaseWakeBodies", 2000 },
        { "PhysicsContactSolverStage.fixedTreeReleases", 2000 },
        { "PhysicsStepDiagnostics.collisionVisualContacts", 2000 },
        { "PhysicsStepDiagnostics.physicsDebugContacts", 147072 },
        { "PhysicsSleepController.m_sleepSupportedThisFrame", 2000 },
        { "PhysicsSleepController.m_sleepInhibitedThisFrame", 2000 },
        { "PhysicsSleepController.m_sleepState", 2000 },
        { "PhysicsSleepController.m_sleepCounter", 2000 },
        { "PhysicsSleepController.m_underwaterSleepLocked", 2000 },
        { "PhysicsSleepController.m_sleepIslandVisualId", 2000 },
        { "PhysicsSleepController.m_sleepIslandAssignedVisualId", 2000 },
        { "PhysicsSleepController.m_sleepIslandParent", 2000 },
        { "PhysicsSleepController.m_sleepIslandRank", 2000 },
        { "PhysicsSleepController.m_sleepIslandHasAwake", 2000 },
        { "PhysicsSleepController.m_sleepIslandHasSupportAnchor", 2000 },
        { "PhysicsSleepController.m_sleepIslandEligible", 2000 },
        { "PhysicsSleepController.m_sleepIslandCanSleep", 2000 },
        { "PhysicsSleepController.m_sleepScratchFlags", 2000 },
        { "PhysicsSleepController.m_sleepVisualIslandIds", 2000 },
        { "PhysicsSleepController.m_sleepVisualIslandBodies", 2000 },
        { "PhysicsSleepController.m_restingWakeQueueScratch", 2000 },
        { "PhysicsTerrainStage.detectionCandidates", 2000 },
        { "PhysicsTerrainStage.contactManifolds", 2000 },
    };

    for ( const ExpectedFixedRowGrowth& expected : expectedFixedRowGrowth )
    {
        int matchingEvents = 0;

        for ( int eventIndex = 0; eventIndex < eventCount; ++eventIndex )
        {

            if ( std::strcmp( events[eventIndex].ownerName, expected.ownerName ) == 0 )
            {
                ++matchingEvents;
                CHECK( events[eventIndex].requestedCapacity == expected.requestedCapacity );
            }
        }

        CHECK( matchingEvents == 1 );
    }

    struct ExpectedRegisteredWithoutGrowth
    {
        const char* ownerName;
        const char* capacityReason;
    };
    const ExpectedRegisteredWithoutGrowth expectedRegisteredWithoutGrowth[] = {
        { "ColliderStore.hullShapes", SkullbonezCore::Physics::PhysicsCapacityReason::HullColliders },
        { "PhysicsContactSolverStage.pipelineRecords", SkullbonezCore::Physics::PhysicsCapacityReason::PipelineRecords },
        { "PhysicsStepDiagnostics.physicsPipelineTrace", SkullbonezCore::Physics::PhysicsCapacityReason::PipelineRecords },
        { "PhysicsSleepController.m_sleepSupportEdges", SkullbonezCore::Physics::PhysicsCapacityReason::CandidatePairs },
        { "PhysicsBroadphaseStage.candidatePairs", SkullbonezCore::Physics::PhysicsCapacityReason::CandidatePairs },
        { "PhysicsBroadphaseStage.collisionCellKeys", SkullbonezCore::Physics::PhysicsCapacityReason::CandidatePairs },
        { "SpatialGrid.candidatePairNodes", SkullbonezCore::Physics::PhysicsCapacityReason::SpatialGridCandidatePairNodes },
        { "SpatialGrid.candidatePairSortKeys",
          SkullbonezCore::Physics::PhysicsCapacityReason::SpatialGridCandidatePairSortKeys },
        { "SpatialGrid.candidatePairSortScratch",
          SkullbonezCore::Physics::PhysicsCapacityReason::SpatialGridCandidatePairSortScratch },
#if defined( _DEBUG )
        { "PhysicsBroadphaseStage.sleepPrunedPairs", SkullbonezCore::Physics::PhysicsCapacityReason::CandidatePairs },
#endif
        { "PhysicsNarrowphaseStage.events", SkullbonezCore::Physics::PhysicsCapacityReason::CandidatePairs },
        { "PhysicsNarrowphaseStage.islandPairIndices", SkullbonezCore::Physics::PhysicsCapacityReason::CandidatePairs },
        { "PhysicsContactSolverStage.collisionVisualBodies",
          SkullbonezCore::Physics::PhysicsCapacityReason::CollisionVisualBodies },
    };

#if defined( _DEBUG )
    CHECK( eventCount + static_cast<int>( std::size( expectedRegisteredWithoutGrowth ) ) == 111 );
#else
    CHECK( eventCount + static_cast<int>( std::size( expectedRegisteredWithoutGrowth ) ) == 110 );
#endif

    for ( const ExpectedRegisteredWithoutGrowth& expected : expectedRegisteredWithoutGrowth )
    {
        RuntimeReserveOwnerStatsView ownerStats = {};
        REQUIRE( RuntimeReserveAllocator::CopyOwnerStatsByName( expected.ownerName, ownerStats ) );
        CHECK( ownerStats.subsystem == RuntimeReserveSubsystem::Physics );
        CHECK( std::strcmp( ownerStats.capacityReason, expected.capacityReason ) == 0 );
    }

    const std::span<const SkullbonezCore::Core::Allocation::RuntimeReserveCapacityView>
        capacityRows = RuntimeReserveAllocator::CapacityRows();
    int physicsCapacityRowCount = 0;
    const char* productionPhysicsOwnerPrefixes[] = {
        "BuoyancySystem.",
        "ColliderStore.",
        "ExternalForceStage.",
        "PhysicsBodyStore.",
        "PhysicsBroadphaseStage.",
        "PhysicsContactSolverStage.",
        "PhysicsEngine",
        "PhysicsForceStage.",
        "PhysicsNarrowphaseStage.",
        "PhysicsSleepController.",
        "PhysicsStepDiagnostics.",
        "PhysicsTerrainStage.",
        "PhysicsWorld.",
        "SpatialGrid.",
    };

    for ( const SkullbonezCore::Core::Allocation::RuntimeReserveCapacityView& row : capacityRows )
    {

        if ( row.subsystem != RuntimeReserveSubsystem::Physics )
        {
            continue;
        }

        bool isProductionPhysicsOwner = false;

        for ( const char* prefix : productionPhysicsOwnerPrefixes )
        {
            const size_t prefixLength = std::strlen( prefix );

            if ( std::strncmp( row.ownerName, prefix, prefixLength ) == 0 )
            {
                isProductionPhysicsOwner = true;
                break;
            }
        }

        if ( !isProductionPhysicsOwner )
        {
            continue;
        }

        ++physicsCapacityRowCount;
        REQUIRE( row.ownerName != nullptr );
        REQUIRE( row.capacityReason != nullptr );
        CHECK( row.elementSizeBytes > 0 );
        CHECK( row.currentCapacity >= row.liveCount );
        CHECK( row.sessionHighWater >= row.liveCount );
        CHECK( row.residentBytes ==
               static_cast<uint64_t>( row.currentCapacity ) * static_cast<uint64_t>( row.elementSizeBytes ) );
    }

#if defined( _DEBUG )
    CHECK( physicsCapacityRowCount == 107 );
#else
    CHECK( physicsCapacityRowCount == 106 );
#endif

    CHECK( PhysicsEngine::ReadBodies( *engine ).RecordCapacity() == 2000u );
    CHECK( PhysicsEngine::ReadColliders( *engine ).RecordCapacity() == 2000u );
    CHECK( PhysicsEngine::ReadColliders( *engine ).SphereShapeCapacity() == 1000u );
    CHECK( PhysicsEngine::ReadColliders( *engine ).BoxShapeCapacity() == 1000u );
    CHECK( PhysicsEngine::ReadColliders( *engine ).HullShapeCapacity() == 0u );
    CHECK( PhysicsEngine::ReadPointJointConstraints( *engine ).capacity() == 24u );
    CHECK( PhysicsEngine::ReadPointJointCapacity( *engine ) == 24u );
}


TEST_CASE( "Broadphase owning memory total includes registered grid backing exactly once" )
{
    SkullbonezCore::Physics::PhysicsBroadphaseStage stage;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        stage.ReserveSceneCapacity( 3u );
    }

    constexpr uint64_t candidateCapacity = 3u;
    constexpr uint64_t productionStageBytes = candidateCapacity * ( sizeof( std::pair<int, int> ) + sizeof( int64_t ) );
#if defined( _DEBUG )
    constexpr uint64_t debugDiagnosticBytes = candidateCapacity * sizeof( std::pair<int, int> );
#else
    constexpr uint64_t debugDiagnosticBytes = 0u;
#endif

    const uint64_t gridBackingBytes = stage.GetSpatialGrid().CollectDynamicMemoryBytes();
    const uint64_t owningDynamicBytes = stage.CollectDynamicMemoryBytes();
    CHECK( owningDynamicBytes == gridBackingBytes + productionStageBytes + debugDiagnosticBytes );
    CHECK( stage.CollectDebugAndBroadphaseMemoryBytes() ==
           static_cast<uint64_t>( sizeof( SkullbonezCore::Math::CollisionDetection::SpatialGrid ) ) + owningDynamicBytes );
}


TEST_CASE( "External force fixed release uses scene-committed body scratch" )
{
    using SkullbonezCore::Core::Allocation::RuntimeAllocationPhase;
    using SkullbonezCore::Core::Allocation::RuntimeAllocationScope;
    using SkullbonezCore::Physics::ExternalCylindricalForceField;
    using SkullbonezCore::Physics::ExternalForceFrameInput;
    using SkullbonezCore::Physics::ExternalForceStage;

    PhysicsBodyStore& bodies = TestBodyStore();
    PhysicsBodyCreateRecord body = MakeBodyRecord( 909u, Vector3( 1.0f, 1.0f, 0.0f ) );
    body.cold.releasesFromFixedOnContact = true;
    body.cold.contactReleaseImpulseThreshold = 0.0f;
    body.hot.fixed = true;
    REQUIRE( bodies.CreateBodyRecord( body ).IsValid() );

    ExternalForceStage stage;

    {
        RuntimeAllocationScope sceneLoadScope( RuntimeAllocationPhase::SceneLoad );
        stage.ReserveBodyCapacity( 1u );
    }

    ExternalCylindricalForceField field;
    field.center = Vector3( 0.0f, 0.0f, 0.0f );
    field.radiusMeters = 10.0f;
    field.heightMeters = 10.0f;
    field.inwardAccelerationMetersPerSecondSquared = 100.0f;
    field.maxDeltaVelocityMetersPerSecond = 100.0f;

    ExternalForceFrameInput input;
    input.fields = std::span<const ExternalCylindricalForceField>( &field, 1u );

    const std::span<const int> released = stage.ReleaseFixedBodies( input, bodies );
    REQUIRE( released.size() == 1u );
    CHECK( released.front() == 0 );
    CHECK( bodies.HotFields().fixed[0] == 0u );
    CHECK( stage.CollectMemoryBytes() == sizeof( int ) * 2u );
}


TEST_CASE( "Prediction physics seed uses the production reserve owner and survives source destruction" )
{
    constexpr int bodyCount = 4;
    SkullbonezCore::Core::EngineConfig config;
    config.physicsExecution.parallel = false;
    config.physicsExecution.parallelApplyForces = false;
    config.physicsExecution.parallelMutualGravity = false;
    config.physicsExecution.parallelExternalForceFields = false;
    config.physicsExecution.parallelNarrowphase = false;
    config.physicsExecution.parallelTerrainDetect = false;
    config.physicsExecution.parallelIntegrate = false;
    config.worldForces.fluidDensity = 0.0f;
    config.worldForces.gasDensity = 0.0f;
    config.worldForces.gravity = -9.8f;
    config.bodySimulation.velocityLimit = 1000.0f;
    config.physicsSleep.frames = 1000000;

    // Lifetime: both heap engines retain this view. Declaration order keeps
    // prediction and source destruction ahead of terrain and its config.
    Terrain terrain( -100000.0f, 0.0f, 0.0f, config );

    PhysicsWorldForces forces;
    forces.gravity = config.worldForces.gravity;
    forces.fluidSurfaceHeight = -1000.0f;
    forces.fluidDensity = config.worldForces.fluidDensity;
    forces.gasDensity = config.worldForces.gasDensity;
    forces.angularDragMultiplier = 0.0f;

    SkullbonezCore::Math::CollisionDetection::ConvexHullShape sharedHullShape;
    REQUIRE( SkullbonezTests::ResultLoadFixtures::TryLoadConvexHull( diagnostics, "SkullbonezData/hulls/pyramid.hull",
                                                                     sharedHullShape ) );
    const CollisionShape sharedHull = sharedHullShape;
    const HullShapeIdentity sharedHullIdentity = MakeShareableHullShapeIdentity( "SKULLBONEZDATA\\HULLS\\PYRAMID.HULL",
                                                                                 Vector3( 1.0f, 1.0f, 1.0f ) );
    const CollisionShape shapes[bodyCount] = { MakeColliderShape( 1.25f ), BoxShape( Vector3( 1.5f, 2.0f, 2.5f ) ),
                                               sharedHull, sharedHull };

    auto liveEngine = std::make_unique<PhysicsEngine>();
    liveEngine->ApplyRuntimeConfig( config );
    liveEngine->SetTerrainView( terrain.PhysicsView() );
    SkullbonezCore::Physics::PhysicsAuthoredBodyRegistration registrations[bodyCount] = {};

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        liveEngine->ReserveAuthoredBodyCapacity( bodyCount, 1u, 1u, 1u, 1u );

        for ( int row = 0; row < bodyCount; ++row )
        {
            const float mass = 2.0f + static_cast<float>( row );
            auto body = SkullbonezCore::Physics::
                MakePhysicsBodyCreateDesc( MakePhysicsSceneObjectId( 700u + static_cast<uint32_t>( row ) ), shapes[row],
                                           Vector3( static_cast<float>( row ) * 12.0f, 30.0f + static_cast<float>( row ),
                                                    0.0f ),
                                           SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION,
                                           Vector3( 0.25f * static_cast<float>( row + 1 ), -0.5f, 0.125f ),
                                           Vector3( 0.01f, 0.02f * static_cast<float>( row + 1 ), 0.03f ),
                                           Vector3( mass, mass + 1.0f, mass + 2.0f ), mass,
                                           0.1f * static_cast<float>( row + 1 ),
                                           SkullbonezCore::Physics::PhysicsBodyMotionKind::Dynamic,
                                           "prediction-seed-clone-body" );

            body.angularVelocityLimit = 1000.0f;
            auto collider = SkullbonezCore::Physics::MakeColliderCreateDesc( shapes[row], body.restitution,
                                                                             100u + static_cast<uint32_t>( row ),
                                                                             row == 0   ? "seed-sphere"
                                                                             : row == 1 ? "seed-box"
                                                                                        : "seed-hull",
                                                                             row >= 2 ? sharedHullIdentity
                                                                                      : HullShapeIdentity {} );

            collider.sceneObjectId = body.sceneObjectId;
            collider.friction = 0.2f * static_cast<float>( row + 1 );
            registrations[row] = liveEngine->RegisterAuthoredBody( body, collider );
            REQUIRE( registrations[row].IsValid() );
        }

        SkullbonezCore::Physics::PhysicsPointJointCreateDesc joint;
        joint.bodyA = registrations[0].body;
        joint.bodyB = registrations[1].body;
        joint.localAnchorA = Vector3( 0.25f, 0.5f, 0.75f );
        joint.localAnchorB = Vector3( -0.25f, -0.5f, -0.75f );
        joint.slack = 0.4f;
        joint.stiffness = 0.3f;
        joint.damping = 0.2f;
        joint.groupId = 77u;
        REQUIRE( liveEngine->CreatePointJoint( joint ).IsValid() );
    }

    LockOrderValidator lockOrderValidator;
    WorkerPool workerPool( lockOrderValidator );
    liveEngine->Step( PHYSICS_FIXED_DT, forces, workerPool, SkullbonezCore::Physics::PhysicsDiagnosticsCsvWriter {} );

    SkullbonezCore::Physics::PhysicsSolverSnapshot solverSnapshot;
    liveEngine->CaptureReplaySolverSnapshot( solverSnapshot,
                                             SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt( bodyCount ) );

    for ( std::size_t cacheIndex = 1u; cacheIndex < solverSnapshot.persistentContactCache.size(); ++cacheIndex )
    {
        CHECK( solverSnapshot.persistentContactCache[cacheIndex - 1u].key <
               solverSnapshot.persistentContactCache[cacheIndex].key );
    }

    std::unique_ptr<PhysicsEngine> predictionEngineOwner;
    int reservedBytes = 0;
    REQUIRE(
        SkullbonezCore::Runtime::ReplayPredictionReserveOperations::SeedReplayPredictionEngineStorage( predictionEngineOwner,
                                                                                                       *liveEngine, 0,
                                                                                                       reservedBytes ) );

    REQUIRE( predictionEngineOwner != nullptr );

    REQUIRE( reservedBytes ==
             SkullbonezCore::Runtime::ReplayPredictionReserveOperations::ReplayPredictionEngineReserveBytes( *liveEngine ) );

    CHECK( predictionEngineOwner
               ->CanRestoreReplaySolverSnapshot( solverSnapshot,
                                                 SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt(
                                                     bodyCount ) ) );

    REQUIRE(
        predictionEngineOwner->RestoreReplaySolverSnapshot( solverSnapshot,
                                                            SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt(
                                                                bodyCount ) ) );

    PhysicsEngine& predictionEngine = *predictionEngineOwner;

    CHECK( PhysicsEngine::ReadBodies( predictionEngine ).RecordCapacity() ==
           PhysicsEngine::ReadBodies( *liveEngine ).RecordCapacity() );

    CHECK( PhysicsEngine::ReadColliders( predictionEngine ).RecordCapacity() ==
           PhysicsEngine::ReadColliders( *liveEngine ).RecordCapacity() );

    CHECK( PhysicsEngine::ReadColliders( predictionEngine ).SphereShapeCapacity() == 1u );
    CHECK( PhysicsEngine::ReadColliders( predictionEngine ).BoxShapeCapacity() == 1u );
    CHECK( PhysicsEngine::ReadColliders( predictionEngine ).HullShapeCapacity() == 1u );
    CHECK( PhysicsEngine::ReadPointJointCapacity( predictionEngine ) == 1u );
    CHECK( predictionEngine.AuthoredBodyDescriptorCount().value == liveEngine->AuthoredBodyDescriptorCount().value );
    CHECK( predictionEngine.CollectPhysicsWorldMemoryBytes() == liveEngine->CollectPhysicsWorldMemoryBytes() );
    CHECK( predictionEngine.CollectDebugAndBroadphaseMemoryBytes() == liveEngine->CollectDebugAndBroadphaseMemoryBytes() );
    CHECK( predictionEngine.CollectSceneSizedStoreMemoryBytes() == liveEngine->CollectSceneSizedStoreMemoryBytes() );

    const PhysicsBodyStore& sourceBodies = PhysicsEngine::ReadBodies( *liveEngine );
    const PhysicsBodyStore& clonedBodies = PhysicsEngine::ReadBodies( predictionEngine );
    const ColliderStore& sourceColliders = PhysicsEngine::ReadColliders( *liveEngine );
    const ColliderStore& clonedColliders = PhysicsEngine::ReadColliders( predictionEngine );
    const auto sourceBuoyancy = PhysicsEngine::ReadBuoyancyFacts( *liveEngine );
    const auto clonedBuoyancy = PhysicsEngine::ReadBuoyancyFacts( predictionEngine );
    REQUIRE( sourceBodies.Count() == bodyCount );
    REQUIRE( clonedBodies.Count() == bodyCount );
    REQUIRE( sourceColliders.Count() == bodyCount );
    REQUIRE( clonedColliders.Count() == bodyCount );
    REQUIRE( sourceBuoyancy.size() == bodyCount );
    REQUIRE( clonedBuoyancy.size() == bodyCount );
    CHECK( sourceColliders.HullShapeCount() == 1u );
    CHECK( clonedColliders.HullShapeCount() == 1u );
    const HullShapeIdentity* sourceHullIdentity = sourceColliders.HullIdentityForHandle( registrations[2].collider );
    const HullShapeIdentity* clonedHullIdentity = clonedColliders.HullIdentityForHandle( registrations[2].collider );
    REQUIRE( sourceHullIdentity != nullptr );
    REQUIRE( clonedHullIdentity != nullptr );
    CHECK( *sourceHullIdentity == sharedHullIdentity );
    CHECK( *clonedHullIdentity == sharedHullIdentity );

    for ( int row = 0; row < bodyCount; ++row )
    {
        const std::size_t index = static_cast<std::size_t>( row );
        const PhysicsBodyRecord* sourceBody = sourceBodies.RecordForModelIndex( row );
        const PhysicsBodyRecord* clonedBody = clonedBodies.RecordForModelIndex( row );
        const ColliderRecord* sourceCollider = sourceColliders.RecordForHandle( registrations[row].collider );
        const ColliderRecord* clonedCollider = clonedColliders.RecordForHandle( registrations[row].collider );
        REQUIRE( sourceBody != nullptr );
        REQUIRE( clonedBody != nullptr );
        REQUIRE( sourceCollider != nullptr );
        REQUIRE( clonedCollider != nullptr );
        CHECK( sourceBody->handle == clonedBody->handle );
        CHECK( sourceBody->sceneObjectId == clonedBody->sceneObjectId );
        CheckVectorBitsEqual( sourceBody->rotationalInertia, clonedBody->rotationalInertia );
        CheckVectorBitsEqual( sourceBody->pendingImpulse, clonedBody->pendingImpulse );
        CheckVectorBitsEqual( sourceBody->pendingImpulseWorldOffset, clonedBody->pendingImpulseWorldOffset );
        CHECK( FloatBitsEqual( sourceBody->mass, clonedBody->mass ) );
        CHECK( FloatBitsEqual( sourceBody->contactReleaseImpulseThreshold, clonedBody->contactReleaseImpulseThreshold ) );
        CHECK( FloatBitsEqual( sourceBody->angularVelocityLimit, clonedBody->angularVelocityLimit ) );
        CHECK( sourceBody->fixedTreeReleaseRootIndex == clonedBody->fixedTreeReleaseRootIndex );
        CHECK( sourceBody->usesWorldInertia == clonedBody->usesWorldInertia );
        CHECK( sourceBody->releasesFromFixedOnContact == clonedBody->releasesFromFixedOnContact );
        CHECK( sourceBody->hasPendingImpulse == clonedBody->hasPendingImpulse );
        CheckHotStateBitsEqual( SkullbonezCore::Physics::LoadPhysicsBodyHotState( sourceBodies.HotFields(), index ),
                                SkullbonezCore::Physics::LoadPhysicsBodyHotState( clonedBodies.HotFields(), index ) );

        CHECK( sourceCollider->handle == clonedCollider->handle );
        CHECK( sourceCollider->body == clonedCollider->body );
        CHECK( sourceCollider->sceneObjectId == clonedCollider->sceneObjectId );
        CHECK( sourceCollider->shapeKind == clonedCollider->shapeKind );
        CHECK( FloatBitsEqual( sourceCollider->boundingRadius, clonedCollider->boundingRadius ) );
        CHECK( FloatBitsEqual( sourceCollider->restitution, clonedCollider->restitution ) );
        CHECK( FloatBitsEqual( sourceCollider->friction, clonedCollider->friction ) );
        CHECK( sourceCollider->contactMaterialId == clonedCollider->contactMaterialId );
        CHECK( FloatBitsEqual( sourceCollider->projectedSurfaceArea, clonedCollider->projectedSurfaceArea ) );
        CHECK( FloatBitsEqual( sourceCollider->dragCoefficient, clonedCollider->dragCoefficient ) );
        const ColliderAuthoringRecord* sourceAuthoring = sourceColliders.AuthoringRecordForModelIndex( row );
        const ColliderAuthoringRecord* clonedAuthoring = clonedColliders.AuthoringRecordForModelIndex( row );
        REQUIRE( sourceAuthoring != nullptr );
        REQUIRE( clonedAuthoring != nullptr );
        CHECK( std::strcmp( sourceAuthoring->contactMaterialName, clonedAuthoring->contactMaterialName ) == 0 );

        CHECK( FloatBitsEqual( sourceBuoyancy[index].volume, clonedBuoyancy[index].volume ) );
        CHECK( FloatBitsEqual( sourceBuoyancy[index].projectedSurfaceArea, clonedBuoyancy[index].projectedSurfaceArea ) );
        CHECK( FloatBitsEqual( sourceBuoyancy[index].dragCoefficient, clonedBuoyancy[index].dragCoefficient ) );
        CHECK(
            FloatBitsEqual( sourceBuoyancy[index].submergedVolumePercent, clonedBuoyancy[index].submergedVolumePercent ) );
        CHECK( FloatBitsEqual( sourceBuoyancy[index].contactEpsilon, clonedBuoyancy[index].contactEpsilon ) );
    }

    const BoundingSphere* sourceSphere = GetShapeIf<BoundingSphere>(
        &sourceColliders.RecordForHandle( registrations[0].collider )->shape );
    const BoundingSphere* clonedSphere = GetShapeIf<BoundingSphere>(
        &clonedColliders.RecordForHandle( registrations[0].collider )->shape );
    const BoundingBox* sourceBox = GetShapeIf<BoundingBox>(
        &sourceColliders.RecordForHandle( registrations[1].collider )->shape );
    const BoundingBox* clonedBox = GetShapeIf<BoundingBox>(
        &clonedColliders.RecordForHandle( registrations[1].collider )->shape );
    const auto* sourceHull = GetShapeIf<SkullbonezCore::Math::CollisionDetection::ConvexHullShape>(
        &sourceColliders.RecordForHandle( registrations[2].collider )->shape );
    const auto* sourceSharedHull = GetShapeIf<SkullbonezCore::Math::CollisionDetection::ConvexHullShape>(
        &sourceColliders.RecordForHandle( registrations[3].collider )->shape );
    const auto* clonedHull = GetShapeIf<SkullbonezCore::Math::CollisionDetection::ConvexHullShape>(
        &clonedColliders.RecordForHandle( registrations[2].collider )->shape );
    const auto* clonedSharedHull = GetShapeIf<SkullbonezCore::Math::CollisionDetection::ConvexHullShape>(
        &clonedColliders.RecordForHandle( registrations[3].collider )->shape );
    REQUIRE( sourceSphere != nullptr );
    REQUIRE( clonedSphere != nullptr );
    REQUIRE( sourceBox != nullptr );
    REQUIRE( clonedBox != nullptr );
    REQUIRE( sourceHull != nullptr );
    REQUIRE( sourceSharedHull != nullptr );
    REQUIRE( clonedHull != nullptr );
    REQUIRE( clonedSharedHull != nullptr );
    CHECK( sourceSphere != clonedSphere );
    CHECK( sourceBox != clonedBox );
    CHECK( sourceHull != clonedHull );
    CHECK( sourceHull == sourceSharedHull );
    CHECK( clonedHull == clonedSharedHull );
    CHECK( sourceColliders.RecordForHandle( registrations[2].collider )->shape.StorageIndex() == 0u );
    CHECK( sourceColliders.RecordForHandle( registrations[3].collider )->shape.StorageIndex() == 0u );
    CHECK( clonedColliders.RecordForHandle( registrations[2].collider )->shape.StorageIndex() == 0u );
    CHECK( clonedColliders.RecordForHandle( registrations[3].collider )->shape.StorageIndex() == 0u );
    CHECK( FloatBitsEqual( sourceSphere->GetRadius(), clonedSphere->GetRadius() ) );
    CheckVectorBitsEqual( sourceBox->GetHalfExtents(), clonedBox->GetHalfExtents() );
    CHECK( sourceHull->GetVertexCount() == clonedHull->GetVertexCount() );
    CHECK( sourceHull->GetFaceCount() == clonedHull->GetFaceCount() );
    CHECK( sourceHull->GetEdgeCount() == clonedHull->GetEdgeCount() );

    const auto& sourceJoints = PhysicsEngine::ReadPointJointConstraints( *liveEngine );
    const auto& clonedJoints = PhysicsEngine::ReadPointJointConstraints( predictionEngine );
    REQUIRE( sourceJoints.size() == 1u );
    REQUIRE( clonedJoints.size() == 1u );
    CHECK( sourceJoints[0].handle == clonedJoints[0].handle );
    CHECK( sourceJoints[0].bodyA == clonedJoints[0].bodyA );
    CHECK( sourceJoints[0].bodyB == clonedJoints[0].bodyB );
    CheckVectorBitsEqual( sourceJoints[0].localAnchorA, clonedJoints[0].localAnchorA );
    CheckVectorBitsEqual( sourceJoints[0].localAnchorB, clonedJoints[0].localAnchorB );
    CHECK( FloatBitsEqual( sourceJoints[0].slack, clonedJoints[0].slack ) );
    CHECK( FloatBitsEqual( sourceJoints[0].stiffness, clonedJoints[0].stiffness ) );
    CHECK( FloatBitsEqual( sourceJoints[0].damping, clonedJoints[0].damping ) );
    CHECK( FloatBitsEqual( sourceJoints[0].accumulatedImpulse, clonedJoints[0].accumulatedImpulse ) );
    CHECK( sourceJoints[0].accumulatedImpulse != 0.0f );
    CHECK( sourceJoints[0].groupId == clonedJoints[0].groupId );
    CHECK( sourceJoints[0].flags == clonedJoints[0].flags );

    liveEngine->Step( PHYSICS_FIXED_DT, forces, workerPool, SkullbonezCore::Physics::PhysicsDiagnosticsCsvWriter {} );
    predictionEngine.Step( PHYSICS_FIXED_DT, forces, workerPool, SkullbonezCore::Physics::PhysicsDiagnosticsCsvWriter {} );

    for ( int row = 0; row < bodyCount; ++row )
    {
        const std::size_t index = static_cast<std::size_t>( row );
        CheckHotStateBitsEqual( SkullbonezCore::Physics::LoadPhysicsBodyHotState( PhysicsEngine::ReadBodies( *liveEngine )
                                                                                      .HotFields(),
                                                                                  index ),
                                SkullbonezCore::Physics::LoadPhysicsBodyHotState( PhysicsEngine::ReadBodies(
                                                                                      predictionEngine )
                                                                                      .HotFields(),
                                                                                  index ) );
    }

    REQUIRE( solverSnapshot.pointJoints.size() == 1u );
    const float historicalJointImpulse = solverSnapshot.pointJoints[0].accumulatedImpulse;
    const float advancedJointImpulse = PhysicsEngine::ReadPointJointConstraints( predictionEngine )[0].accumulatedImpulse;
    REQUIRE_FALSE( FloatBitsEqual( historicalJointImpulse, advancedJointImpulse ) );

    SkullbonezCore::Physics::PhysicsSolverSnapshot mismatchedTopology = solverSnapshot;
    mismatchedTopology.pointJoints[0].localAnchorA.x += 0.125f;
    const int preflightBodyCount = PhysicsEngine::ReadBodies( predictionEngine ).Count();
    const int preflightColliderCount = PhysicsEngine::ReadColliders( predictionEngine ).Count();
    const auto preflightJointHandle = PhysicsEngine::ReadPointJointConstraints( predictionEngine )[0].handle;
    CHECK_FALSE(
        predictionEngine.CanRestoreReplaySolverSnapshot( mismatchedTopology,
                                                         SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt(
                                                             bodyCount ) ) );
    CHECK( PhysicsEngine::ReadBodies( predictionEngine ).Count() == preflightBodyCount );
    CHECK( PhysicsEngine::ReadColliders( predictionEngine ).Count() == preflightColliderCount );
    REQUIRE( PhysicsEngine::ReadPointJointConstraints( predictionEngine ).size() == 1u );
    CHECK( PhysicsEngine::ReadPointJointConstraints( predictionEngine )[0].handle == preflightJointHandle );
    CHECK( FloatBitsEqual( PhysicsEngine::ReadPointJointConstraints( predictionEngine )[0].accumulatedImpulse,
                           advancedJointImpulse ) );
    CHECK_FALSE(
        predictionEngine.RestoreReplaySolverSnapshot( mismatchedTopology,
                                                      SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt(
                                                          bodyCount ) ) );
    CHECK( FloatBitsEqual( PhysicsEngine::ReadPointJointConstraints( predictionEngine )[0].accumulatedImpulse,
                           advancedJointImpulse ) );

    const auto checkRejectedSnapshotLeavesStateUntouched = [&]( const auto& malformedSnapshot )
    {
        SkullbonezCore::Physics::PhysicsSolverSnapshot beforeReject;
        predictionEngine.CaptureReplaySolverSnapshot( beforeReject,
                                                      SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt(
                                                          bodyCount ) );

        CHECK_FALSE(
            predictionEngine.CanRestoreReplaySolverSnapshot( malformedSnapshot,
                                                             SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt(
                                                                 bodyCount ) ) );
        CHECK_FALSE(
            predictionEngine.RestoreReplaySolverSnapshot( malformedSnapshot,
                                                          SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt(
                                                              bodyCount ) ) );

        SkullbonezCore::Physics::PhysicsSolverSnapshot afterReject;
        predictionEngine.CaptureReplaySolverSnapshot( afterReject,
                                                      SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt(
                                                          bodyCount ) );
        CHECK( afterReject.timeRemaining == beforeReject.timeRemaining );
        CHECK( afterReject.motionEligibilityState == beforeReject.motionEligibilityState );
        CHECK( afterReject.sleepState == beforeReject.sleepState );
        CHECK( afterReject.sleepSupportEdges == beforeReject.sleepSupportEdges );
        CHECK( afterReject.persistentContactCounts == beforeReject.persistentContactCounts );
        CHECK( afterReject.collisionCellKeys == beforeReject.collisionCellKeys );
        REQUIRE( PhysicsEngine::ReadPointJointConstraints( predictionEngine ).size() == 1u );
        CHECK( FloatBitsEqual( PhysicsEngine::ReadPointJointConstraints( predictionEngine )[0].accumulatedImpulse,
                               advancedJointImpulse ) );
    };

    auto malformedDenseRows = solverSnapshot;
    REQUIRE_FALSE( malformedDenseRows.sleepState.empty() );
    malformedDenseRows.sleepState.pop_back();
    checkRejectedSnapshotLeavesStateUntouched( malformedDenseRows );

    auto malformedEligibilityRows = solverSnapshot;
    REQUIRE_FALSE( malformedEligibilityRows.motionEligibilityState.empty() );
    malformedEligibilityRows.motionEligibilityState.pop_back();
    checkRejectedSnapshotLeavesStateUntouched( malformedEligibilityRows );

    auto malformedEligibilityBits = solverSnapshot;
    REQUIRE_FALSE( malformedEligibilityBits.motionEligibilityState.empty() );
    malformedEligibilityBits.motionEligibilityState[0] = 0x80u;
    checkRejectedSnapshotLeavesStateUntouched( malformedEligibilityBits );

    auto malformedSupportEdge = solverSnapshot;
    malformedSupportEdge.sleepSupportEdges.emplace_back( 0, bodyCount );
    checkRejectedSnapshotLeavesStateUntouched( malformedSupportEdge );

    auto malformedIslandParent = solverSnapshot;
    REQUIRE_FALSE( malformedIslandParent.sleepIslandParent.empty() );
    malformedIslandParent.sleepIslandParent[0] = bodyCount;
    checkRejectedSnapshotLeavesStateUntouched( malformedIslandParent );

    auto malformedContactCache = solverSnapshot;
    SkullbonezCore::Physics::PhysicsSolverContactCacheSample foreignCache;
    foreignCache.key = static_cast<int64_t>( static_cast<uint64_t>( bodyCount ) << 32 );
    malformedContactCache.persistentContactCache.push_back( foreignCache );
    checkRejectedSnapshotLeavesStateUntouched( malformedContactCache );

    auto malformedContactCounts = solverSnapshot;
    REQUIRE_FALSE( malformedContactCounts.persistentContactCounts.empty() );
    ++malformedContactCounts.persistentContactCounts[0];
    checkRejectedSnapshotLeavesStateUntouched( malformedContactCounts );

    REQUIRE( bodyCount >= 2 );
    auto malformedRestingContactCounts = solverSnapshot;
    SkullbonezCore::Physics::PhysicsSolverPersistentContactSample missingRestingCount;
    missingRestingCount.bodyA = 0;
    missingRestingCount.bodyB = 1;
    missingRestingCount.featureId = 16u;
    missingRestingCount.key = SkullbonezCore::Physics::MakePersistentContactCacheKey( 0, 1, 16u );
    missingRestingCount.supportsRestingPolicy = true;
    malformedRestingContactCounts.persistentContacts.push_back( missingRestingCount );
    ++malformedRestingContactCounts.persistentContactCounts[0];
    ++malformedRestingContactCounts.persistentContactCounts[1];
    checkRejectedSnapshotLeavesStateUntouched( malformedRestingContactCounts );

    auto malformedContactKey = solverSnapshot;
    SkullbonezCore::Physics::PhysicsSolverPersistentContactSample incoherentContact;
    incoherentContact.bodyA = 0;
    incoherentContact.bodyB = 1;
    incoherentContact.featureId = 17u;
    incoherentContact.key = SkullbonezCore::Physics::MakePersistentContactCacheKey( 0, 1, 18u );
    incoherentContact.supportsRestingPolicy = false;
    malformedContactKey.persistentContacts.push_back( incoherentContact );
    ++malformedContactKey.persistentContactCounts[0];
    ++malformedContactKey.persistentContactCounts[1];
    checkRejectedSnapshotLeavesStateUntouched( malformedContactKey );

    auto malformedTerrainFlag = solverSnapshot;
    SkullbonezCore::Physics::PhysicsSolverPersistentContactSample incoherentTerrainFlag;
    incoherentTerrainFlag.bodyA = 0;
    incoherentTerrainFlag.bodyB = 1;
    incoherentTerrainFlag.featureId = 19u;
    incoherentTerrainFlag.key = SkullbonezCore::Physics::MakePersistentContactCacheKey( 0, 1, 19u );
    incoherentTerrainFlag.isTerrain = true;
    incoherentTerrainFlag.supportsRestingPolicy = false;
    malformedTerrainFlag.persistentContacts.push_back( incoherentTerrainFlag );
    ++malformedTerrainFlag.persistentContactCounts[0];
    ++malformedTerrainFlag.persistentContactCounts[1];
    checkRejectedSnapshotLeavesStateUntouched( malformedTerrainFlag );

    auto unsortedContactCache = solverSnapshot;
    unsortedContactCache.persistentContactCache.clear();
    SkullbonezCore::Physics::PhysicsSolverContactCacheSample laterCache;
    laterCache.key = SkullbonezCore::Physics::MakePersistentContactCacheKey( 0, 1, 2u );
    SkullbonezCore::Physics::PhysicsSolverContactCacheSample earlierCache;
    earlierCache.key = SkullbonezCore::Physics::MakePersistentContactCacheKey( 0, 1, 1u );
    unsortedContactCache.persistentContactCache.push_back( laterCache );
    unsortedContactCache.persistentContactCache.push_back( earlierCache );
    checkRejectedSnapshotLeavesStateUntouched( unsortedContactCache );

    auto duplicateContactCache = solverSnapshot;
    duplicateContactCache.persistentContactCache.clear();
    duplicateContactCache.persistentContactCache.push_back( earlierCache );
    duplicateContactCache.persistentContactCache.push_back( earlierCache );
    checkRejectedSnapshotLeavesStateUntouched( duplicateContactCache );

    auto overCommittedCapacity = solverSnapshot;
    overCommittedCapacity.collisionCellKeys.resize( 17u, 42 );
    checkRejectedSnapshotLeavesStateUntouched( overCommittedCapacity );

    SkullbonezCore::Physics::PhysicsSolverSnapshot legacySnapshot = solverSnapshot;
    legacySnapshot.version = 2u;
    legacySnapshot.pointJoints.clear();
    legacySnapshot.motionEligibilityState.clear();
    REQUIRE( predictionEngine.RestoreReplaySolverSnapshot( legacySnapshot,
                                                           SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt(
                                                               bodyCount ) ) );
    CHECK( PhysicsEngine::ReadPointJointConstraints( predictionEngine )[0].accumulatedImpulse == 0.0f );

    // Invariant: restoring historical solver state replaces a later cache on
    // both the live and prediction engines. The following fixed step therefore
    // consumes the same warm-start input instead of whichever impulse happened
    // to be live when prediction topology was cloned.
    REQUIRE( liveEngine->RestoreReplaySolverSnapshot( solverSnapshot,
                                                      SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt(
                                                          bodyCount ) ) );
    REQUIRE( predictionEngine.RestoreReplaySolverSnapshot( solverSnapshot,
                                                           SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt(
                                                               bodyCount ) ) );
    CHECK( FloatBitsEqual( PhysicsEngine::ReadPointJointConstraints( *liveEngine )[0].accumulatedImpulse,
                           historicalJointImpulse ) );
    CHECK( FloatBitsEqual( PhysicsEngine::ReadPointJointConstraints( predictionEngine )[0].accumulatedImpulse,
                           historicalJointImpulse ) );

    liveEngine->Step( PHYSICS_FIXED_DT, forces, workerPool, SkullbonezCore::Physics::PhysicsDiagnosticsCsvWriter {} );
    predictionEngine.Step( PHYSICS_FIXED_DT, forces, workerPool, SkullbonezCore::Physics::PhysicsDiagnosticsCsvWriter {} );

    for ( int row = 0; row < bodyCount; ++row )
    {
        const std::size_t index = static_cast<std::size_t>( row );
        CheckHotStateBitsEqual( SkullbonezCore::Physics::LoadPhysicsBodyHotState( PhysicsEngine::ReadBodies( *liveEngine )
                                                                                      .HotFields(),
                                                                                  index ),
                                SkullbonezCore::Physics::LoadPhysicsBodyHotState( PhysicsEngine::ReadBodies(
                                                                                      predictionEngine )
                                                                                      .HotFields(),
                                                                                  index ) );
    }

    SkullbonezCore::Physics::PhysicsPointJointUpdateDesc solverUpdate;
    solverUpdate.constraint = sourceJoints[0].handle;
    solverUpdate.updateMask = SkullbonezCore::Physics::PHYSICS_POINT_JOINT_UPDATE_SOLVER;
    solverUpdate.slack = sourceJoints[0].slack;
    solverUpdate.stiffness = sourceJoints[0].stiffness + 0.01f;
    solverUpdate.damping = sourceJoints[0].damping;
    REQUIRE( liveEngine->UpdatePointJoint( solverUpdate ) );
    // Invariant: an authored solver-policy change keeps handle identity but
    // invalidates the scalar impulse produced under the previous policy.
    CHECK( sourceJoints[0].accumulatedImpulse == 0.0f );

    // Lifetime: ColliderStore cloned every shape into destination-owned backing.
    // Destroying the live engine must not invalidate prediction references.
    liveEngine.reset();
    const ColliderStore& survivingColliders = PhysicsEngine::ReadColliders( predictionEngine );
    const BoundingSphere* survivingSphere = GetShapeIf<BoundingSphere>(
        &survivingColliders.RecordForHandle( registrations[0].collider )->shape );
    const BoundingBox* survivingBox = GetShapeIf<BoundingBox>(
        &survivingColliders.RecordForHandle( registrations[1].collider )->shape );
    const auto* survivingHull = GetShapeIf<SkullbonezCore::Math::CollisionDetection::ConvexHullShape>(
        &survivingColliders.RecordForHandle( registrations[2].collider )->shape );
    const auto* survivingSharedHull = GetShapeIf<SkullbonezCore::Math::CollisionDetection::ConvexHullShape>(
        &survivingColliders.RecordForHandle( registrations[3].collider )->shape );
    REQUIRE( survivingSphere != nullptr );
    REQUIRE( survivingBox != nullptr );
    REQUIRE( survivingHull != nullptr );
    REQUIRE( survivingSharedHull != nullptr );
    CHECK( FloatBitsEqual( survivingSphere->GetRadius(), 1.25f ) );
    CheckVectorBitsEqual( survivingBox->GetHalfExtents(), Vector3( 1.5f, 2.0f, 2.5f ) );
    CHECK( survivingHull->GetVertexCount() > 0u );
    CHECK( survivingHull == survivingSharedHull );
    CHECK( survivingHull == clonedHull );
    CHECK( survivingSharedHull == clonedSharedHull );
    CHECK( survivingColliders.RecordForHandle( registrations[2].collider )->shape.StorageIndex() == 0u );
    CHECK( survivingColliders.RecordForHandle( registrations[3].collider )->shape.StorageIndex() == 0u );
    REQUIRE( survivingColliders.HullIdentityForHandle( registrations[3].collider ) != nullptr );
    CHECK( *survivingColliders.HullIdentityForHandle( registrations[3].collider ) == sharedHullIdentity );
}


TEST_CASE( "Replay body-prefix trim removes doomed point joints and preserves survivor order" )
{
    constexpr int bodyCount = 4;
    constexpr int restoredBodyCount = 3;
    PhysicsEngine engine;
    const CollisionShape shape = MakeColliderShape( 0.5f );
    SkullbonezCore::Physics::PhysicsAuthoredBodyRegistration registrations[bodyCount] = {};

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        engine.ReserveAuthoredBodyCapacity( bodyCount, bodyCount, 0u, 0u, 3u );

        for ( int row = 0; row < bodyCount; ++row )
        {
            const auto sceneObjectId = MakePhysicsSceneObjectId( 810u + static_cast<uint32_t>( row ) );
            auto body = SkullbonezCore::Physics::
                MakePhysicsBodyCreateDesc( sceneObjectId, shape, Vector3( static_cast<float>( row ), 10.0f, 0.0f ),
                                           SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION,
                                           Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ),
                                           Vector3( 1.0f, 1.0f, 1.0f ), 1.0f, 0.0f,
                                           SkullbonezCore::Physics::PhysicsBodyMotionKind::Dynamic,
                                           "replay-trim-joint-body" );
            auto collider = SkullbonezCore::Physics::MakeColliderCreateDesc( shape, 0.0f, 0u, "replay-trim-joint" );
            collider.sceneObjectId = sceneObjectId;
            registrations[row] = engine.RegisterAuthoredBody( body, collider );
            REQUIRE( registrations[row].IsValid() );
        }
    }

    const auto createJoint = [&]( int bodyA, int bodyB )
    {
        SkullbonezCore::Physics::PhysicsPointJointCreateDesc joint;
        joint.bodyA = registrations[bodyA].body;
        joint.bodyB = registrations[bodyB].body;
        joint.localAnchorA = Vector3( static_cast<float>( bodyA ), 0.0f, 0.0f );
        joint.localAnchorB = Vector3( static_cast<float>( bodyB ), 0.0f, 0.0f );
        return engine.CreatePointJoint( joint );
    };

    // The doomed row is deliberately first. A swap-pop removal would reverse
    // the two survivor rows and invalidate the preflight's filtered ordinals.
    const auto doomed = createJoint( 3, 0 );
    const auto survivorA = createJoint( 0, 1 );
    const auto survivorB = createJoint( 1, 2 );
    REQUIRE( doomed.IsValid() );
    REQUIRE( survivorA.IsValid() );
    REQUIRE( survivorB.IsValid() );

    SkullbonezCore::Physics::PhysicsSolverSnapshot snapshot;
    engine.CaptureReplaySolverSnapshot( snapshot, SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt(
                                                      restoredBodyCount ) );
    REQUIRE( snapshot.pointJoints.size() == 2u );
    CHECK( snapshot.pointJoints[0].topologyOrdinal == 0u );
    CHECK( snapshot.pointJoints[1].topologyOrdinal == 1u );
    REQUIRE(
        engine.CanRestoreReplaySolverSnapshot( snapshot, SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt(
                                                             restoredBodyCount ) ) );

    REQUIRE(
        engine.TrimBodiesToCount( SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt( restoredBodyCount ) ) );
    const auto& survivors = PhysicsEngine::ReadPointJointConstraints( engine );
    REQUIRE( survivors.size() == snapshot.pointJoints.size() );
    CHECK( survivors[0].handle == survivorA );
    CHECK( survivors[1].handle == survivorB );
    REQUIRE( engine.RestoreReplaySolverSnapshot( snapshot, SkullbonezCore::Physics::MakePhysicsBodyCountFromNonNegativeInt(
                                                               restoredBodyCount ) ) );
}


TEST_CASE( "Coverage floor contract: box and hull buoyancy stay finite under partial submersion" )
{
    CheckUnderwaterForcePath( BoxShape( Vector3( 2.0f, 0.5f, 1.0f ) ), 601u );
    SkullbonezCore::Math::CollisionDetection::ConvexHullShape hull;
    REQUIRE(
        SkullbonezTests::ResultLoadFixtures::TryLoadConvexHull( diagnostics, "SkullbonezData/hulls/pyramid.hull", hull ) );
    CheckUnderwaterForcePath( hull, 602u );
}
