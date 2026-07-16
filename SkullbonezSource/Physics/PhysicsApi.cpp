/*
File: SkullbonezSource/Physics/PhysicsApi.cpp
Purpose:
  Implements the model-free public physics API slice and standalone smoke sample.

Summary:
  PhysicsStandaloneWorld is the first isolated owner for public physics handles.
  It does not route through Run, renderer setup, scene parsing, or runtime
  collection owners. The world is intentionally small: it proves deterministic
  create/update/delete/query/step ownership while collision and solver
  authority stays in store-owned cold records and aligned hot-field arrays.

Glossary:
  Activation command: Handle-based request to wake a body, seed it asleep, or
    toggle the standalone world's sleep gate.
  Standalone world: Public physics owner that can be constructed without runtime
    or scene/game-object storage.
  Handle generation: Counter paired with a slot index so stale handles fail
    after deletion and reuse.
  Point joint: Constraint record that keeps two local body anchors associated
    without exposing legacy model indices.
  Contact row: Immutable public collision record produced after Step(); it is
    diagnostic/replay data, not the future hot solver manifold.
  Island: Deterministic group of bodies connected by contacts or constraints;
    island rows feed sleep/support diagnostics without exposing solver storage.
  Narrowphase: Shape-specific overlap test that turns broadphase candidates into
    concrete contact points, normals, and penetration depths.
  Ray cast: Query that reports the closest collider candidate along a directed
    segment.
  Broadphase query: Cheap AABB query that returns candidate bodies in slot order.
  Restitution: Bounce response copied from collider material data.
  Friction: Sliding resistance copied from collider material data.
  Sleep gate: World policy deciding whether sleeping body flags are honored by
    steps and queries.
  Supported island: Island containing a fixed body, used as the first standalone
    public support-state signal before full support propagation migrates.
  Union-find: Compact parent/rank arrays used to merge connected body rows into
    deterministic islands without heap-allocated graph nodes.
  Determinism: Same fixed-step inputs produce the same final state and hash.

Invariants:
  - Standalone handles pair a slot index with a nonzero generation value.
  - Step mutates only alive, dynamic, awake hot-field rows.
  - Contact rows are rebuilt from body/collider stores after Step() and cleared
    on geometry mutations, so public views never point into model storage.
  - Island rows are rebuilt after contact rows and borrow body-handle spans from
    a flat buffer owned by PhysicsStandaloneWorld.
  - The smoke sample uses binary-exact fixed-step values so validation can check
    exact final state without tolerance drift.

Related:
  - SkullbonezSource/Physics/PhysicsApi.h
  - Agentic/Reports/2026-07-11/physics-authority-and-identity-closure-review.md
*/
#include "PhysicsApi.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <variant>

using SkullbonezCore::Math::CollisionDetection::BoundingBox;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::ConvexHullShape;
using SkullbonezCore::Math::CollisionDetection::GetShapeBoundingRadius;
using SkullbonezCore::Math::CollisionDetection::GetShapePosition;
using SkullbonezCore::Math::Transformation::RotationMatrix;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Math::Vector::VectorMag;
using SkullbonezCore::Math::Vector::VectorMagSquared;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderShapeKind;
using SkullbonezCore::Physics::ConstPhysicsBodyHotFields;
using SkullbonezCore::Physics::PHYSICS_HANDLE_INITIAL_GENERATION;
using SkullbonezCore::Physics::PhysicsActivationCommand;
using SkullbonezCore::Physics::PhysicsActivationCommandKind;
using SkullbonezCore::Physics::PhysicsBodyAngularVelocity;
using SkullbonezCore::Physics::PhysicsBodyCreateDesc;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyHotFieldsConstView;
using SkullbonezCore::Physics::PhysicsBodyInverseInertia;
using SkullbonezCore::Physics::PhysicsBodyLinearVelocity;
using SkullbonezCore::Physics::PhysicsBodyMotionKind;
using SkullbonezCore::Physics::PhysicsBodyOrientation;
using SkullbonezCore::Physics::PhysicsBodyPosition;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyUpdateDesc;
using SkullbonezCore::Physics::PhysicsBodyView;
using SkullbonezCore::Physics::PhysicsBroadphaseCellQueryDesc;
using SkullbonezCore::Physics::PhysicsBroadphaseQueryResultView;
using SkullbonezCore::Physics::PhysicsColliderCreateDesc;
using SkullbonezCore::Physics::PhysicsColliderHandle;
using SkullbonezCore::Physics::PhysicsColliderUpdateDesc;
using SkullbonezCore::Physics::PhysicsColliderView;
using SkullbonezCore::Physics::PhysicsConstraintHandle;
using SkullbonezCore::Physics::PhysicsContactCollectionView;
using SkullbonezCore::Physics::PhysicsContactView;
using SkullbonezCore::Physics::PhysicsIslandCollectionView;
using SkullbonezCore::Physics::PhysicsIslandView;
using SkullbonezCore::Physics::PhysicsPointJointCreateDesc;
using SkullbonezCore::Physics::PhysicsPointJointUpdateDesc;
using SkullbonezCore::Physics::PhysicsPointJointView;
using SkullbonezCore::Physics::PhysicsRayCastDesc;
using SkullbonezCore::Physics::PhysicsRayCastHit;
using SkullbonezCore::Physics::PhysicsSceneObjectId;
using SkullbonezCore::Physics::PhysicsStandaloneSmokeResult;
using SkullbonezCore::Physics::PhysicsStandaloneStepDesc;
using SkullbonezCore::Physics::PhysicsStandaloneWorld;

namespace
{
constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ull;
constexpr uint64_t FNV_PRIME = 1099511628211ull;
constexpr uint32_t INVALID_ISLAND_ROW = ( std::numeric_limits<uint32_t>::max )();

uint64_t HashBytes( uint64_t hash, const void* bytes, std::size_t byteCount )
{
    const uint8_t* cursor = static_cast<const uint8_t*>( bytes );
    for ( std::size_t i = 0; i < byteCount; ++i )
    {
        hash ^= static_cast<uint64_t>( cursor[i] );
        hash *= FNV_PRIME;
    }
    return hash;
}

uint64_t HashU32( uint64_t hash, uint32_t value )
{
    return HashBytes( hash, &value, sizeof( value ) );
}

uint64_t HashU64( uint64_t hash, uint64_t value )
{
    return HashBytes( hash, &value, sizeof( value ) );
}

uint64_t HashFloat( uint64_t hash, float value )
{
    uint32_t bits = 0;
    std::memcpy( &bits, &value, sizeof( bits ) );
    return HashU32( hash, bits );
}

uint64_t HashVector( uint64_t hash, const Vector3& value )
{
    hash = HashFloat( hash, value.x );
    hash = HashFloat( hash, value.y );
    return HashFloat( hash, value.z );
}

uint32_t ContactFeatureId( PhysicsColliderHandle colliderA, PhysicsColliderHandle colliderB )
{
    uint64_t hash = FNV_OFFSET_BASIS;
    hash = HashU32( hash, colliderA.index );
    hash = HashU32( hash, colliderA.generation );
    hash = HashU32( hash, colliderB.index );
    hash = HashU32( hash, colliderB.generation );
    return static_cast<uint32_t>( hash ^ ( hash >> 32u ) );
}

uint64_t HashContactView( uint64_t hash, const PhysicsContactView& contact )
{
    hash = HashU32( hash, contact.bodyA.index );
    hash = HashU32( hash, contact.bodyA.generation );
    hash = HashU32( hash, contact.bodyB.index );
    hash = HashU32( hash, contact.bodyB.generation );
    hash = HashU32( hash, contact.colliderA.index );
    hash = HashU32( hash, contact.colliderA.generation );
    hash = HashU32( hash, contact.colliderB.index );
    hash = HashU32( hash, contact.colliderB.generation );
    hash = HashVector( hash, contact.point );
    hash = HashVector( hash, contact.normal );
    hash = HashFloat( hash, contact.penetrationDepth );
    hash = HashFloat( hash, contact.normalImpulse );
    hash = HashFloat( hash, contact.restitutionA );
    hash = HashFloat( hash, contact.restitutionB );
    hash = HashFloat( hash, contact.frictionA );
    hash = HashFloat( hash, contact.frictionB );
    hash = HashU32( hash, contact.contactMaterialAId );
    hash = HashU32( hash, contact.contactMaterialBId );
    hash = HashU32( hash, contact.featureId );
    return HashU32( hash, contact.touching ? 1u : 0u );
}

uint64_t HashContactCollection( PhysicsContactCollectionView view )
{
    uint64_t hash = FNV_OFFSET_BASIS;
    hash = HashU32( hash, view.contactCount );
    for ( uint32_t i = 0; i < view.contactCount; ++i )
    {
        hash = HashContactView( hash, view.contacts[i] );
    }
    return hash;
}

uint64_t HashIslandView( uint64_t hash, const SkullbonezCore::Physics::PhysicsIslandView& island )
{
    hash = HashU32( hash, island.islandId );
    hash = HashU32( hash, island.bodyCount );
    for ( uint32_t i = 0; i < island.bodyCount; ++i )
    {
        hash = HashU32( hash, island.bodies[i].index );
        hash = HashU32( hash, island.bodies[i].generation );
    }
    hash = HashU32( hash, island.sleeping ? 1u : 0u );
    return HashU32( hash, island.supported ? 1u : 0u );
}

uint64_t HashIslandCollection( SkullbonezCore::Physics::PhysicsIslandCollectionView view )
{
    uint64_t hash = FNV_OFFSET_BASIS;
    hash = HashU32( hash, view.islandCount );
    for ( uint32_t i = 0; i < view.islandCount; ++i )
    {
        hash = HashIslandView( hash, view.islands[i] );
    }
    return hash;
}

float ClampFloat( float value, float minValue, float maxValue )
{
    return value < minValue ? minValue : ( value > maxValue ? maxValue : value );
}

void CopyContactMaterialPayload( PhysicsContactView& contact,
                                 const ColliderRecord& colliderA,
                                 const ColliderRecord& colliderB )
{
    contact.restitutionA = colliderA.restitution;
    contact.restitutionB = colliderB.restitution;
    contact.frictionA = colliderA.friction;
    contact.frictionB = colliderB.friction;
    contact.contactMaterialAId = colliderA.contactMaterialId;
    contact.contactMaterialBId = colliderB.contactMaterialId;
    contact.featureId = ContactFeatureId( colliderA.handle, colliderB.handle );
}

uint32_t FindIslandRoot( std::vector<uint32_t>& parents, uint32_t row )
{
    uint32_t root = row;
    while ( parents[root] != root )
    {
        root = parents[root];
    }
    while ( parents[row] != row )
    {
        const uint32_t parent = parents[row];
        parents[row] = root;
        row = parent;
    }
    return root;
}

void UnionIslandRows( std::vector<uint32_t>& parents, std::vector<uint32_t>& ranks, uint32_t rowA, uint32_t rowB )
{
    uint32_t rootA = FindIslandRoot( parents, rowA );
    uint32_t rootB = FindIslandRoot( parents, rowB );
    if ( rootA == rootB )
    {
        return;
    }
    if ( ranks[rootA] < ranks[rootB] || ( ranks[rootA] == ranks[rootB] && rootB < rootA ) )
    {
        const uint32_t swap = rootA;
        rootA = rootB;
        rootB = swap;
    }
    parents[rootB] = rootA;
    if ( ranks[rootA] == ranks[rootB] )
    {
        ++ranks[rootA];
    }
}

uint64_t HashSmokeResult( const PhysicsStandaloneSmokeResult& result )
{
    uint64_t hash = FNV_OFFSET_BASIS;
    hash = HashU32( hash, result.body.index );
    hash = HashU32( hash, result.body.generation );
    hash = HashU32( hash, result.secondaryBody.index );
    hash = HashU32( hash, result.secondaryBody.generation );
    hash = HashU32( hash, result.collider.index );
    hash = HashU32( hash, result.collider.generation );
    hash = HashU32( hash, result.constraint.index );
    hash = HashU32( hash, result.constraint.generation );
    hash = HashU32( hash, result.bodyCount );
    hash = HashU32( hash, result.colliderCount );
    hash = HashU32( hash, result.pointJointCount );
    hash = HashU32( hash, result.contactCount );
    hash = HashU64( hash, result.contactHash );
    hash = HashU32( hash, result.islandCount );
    hash = HashU64( hash, result.islandHash );
    hash = HashU32( hash, result.broadphaseQueryCount );
    hash = HashU32( hash, result.stepCount );
    hash = HashU32( hash, result.activationCommandsPassed ? 1u : 0u );
    hash = HashU32( hash, result.rayCastHit ? 1u : 0u );
    hash = HashU32( hash, result.secondaryBodyAdvanced ? 1u : 0u );
    hash = HashVector( hash, result.finalPosition );
    hash = HashVector( hash, result.finalLinearVelocity );
    hash = HashVector( hash, result.secondaryFinalPosition );
    return HashVector( hash, result.secondaryFinalLinearVelocity );
}

Vector3 InvertNonZeroComponents( const Vector3& value )
{
    return Vector3( value.x != 0.0f ? 1.0f / value.x : 0.0f,
                    value.y != 0.0f ? 1.0f / value.y : 0.0f,
                    value.z != 0.0f ? 1.0f / value.z : 0.0f );
}

float ComputeInverseMass( PhysicsBodyMotionKind motionKind, float mass )
{
    return motionKind == PhysicsBodyMotionKind::Fixed || mass <= 0.0f ? 0.0f : 1.0f / mass;
}

bool BodyPassesQueryFilters( const PhysicsBodyHotFieldsConstView& hotFields,
                             std::size_t bodyIndex,
                             bool includeFixedBodies,
                             bool includeSleepingBodies,
                             bool sleepEnabled )
{
    if ( !includeFixedBodies && hotFields.fixed[bodyIndex] != 0u )
    {
        return false;
    }
    return includeSleepingBodies || !sleepEnabled || hotFields.awake[bodyIndex] != 0u;
}

float ConservativeShapeRadius( const SkullbonezCore::Math::CollisionDetection::CollisionShape& shape )
{
    // Invariant: a broadphase sphere centered on the body origin must include
    // local shape offset. Otherwise offset colliders can be missed before
    // narrowphase ever sees them.
    return GetShapeBoundingRadius( shape ) + VectorMag( GetShapePosition( shape ) );
}

float ConservativeBroadphaseRadius( float requestedRadius,
                                    const SkullbonezCore::Math::CollisionDetection::CollisionShape& shape )
{
    const float minimumRadius = ConservativeShapeRadius( shape );
    return requestedRadius > minimumRadius ? requestedRadius : minimumRadius;
}

ColliderShapeKind ColliderShapeKindForShape( const SkullbonezCore::Math::CollisionDetection::CollisionShape& shape )
{
    if ( std::holds_alternative<BoundingBox>( shape ) )
    {
        return ColliderShapeKind::Box;
    }
    if ( std::holds_alternative<ConvexHullShape>( shape ) )
    {
        return ColliderShapeKind::ConvexHull;
    }
    return ColliderShapeKind::Sphere;
}

float EffectiveColliderRadius( const ColliderRecord& collider )
{
    return ConservativeBroadphaseRadius( collider.boundingRadius, collider.shape );
}

Vector3 ColliderWorldCenter( const PhysicsBodyHotFieldsConstView& hotFields,
                             std::size_t bodyIndex,
                             const ColliderRecord& collider )
{
    // Why: local collider offsets live in body space. Rotate them through the
    // body orientation before doing any world-space query math so conservative
    // candidates match the narrowphase coordinate convention.
    auto orientation = PhysicsBodyOrientation( hotFields, bodyIndex );
    const RotationMatrix rotation = orientation.GetOrientationMatrix();
    return PhysicsBodyPosition( hotFields, bodyIndex ) + rotation * GetShapePosition( collider.shape );
}

bool IntersectRaySphere( const Vector3& rayOrigin,
                         const Vector3& rayDirection,
                         const Vector3& center,
                         float radius,
                         float& outDistance )
{
    const Vector3 m = rayOrigin - center;
    const float b = m * rayDirection;
    const float c = ( m * m ) - radius * radius;
    if ( c > 0.0f && b > 0.0f )
    {
        return false;
    }

    const float discriminant = b * b - c;
    if ( discriminant < 0.0f )
    {
        return false;
    }

    outDistance = -b - sqrtf( discriminant );
    if ( outDistance < 0.0f )
    {
        outDistance = 0.0f;
    }
    return true;
}

bool SphereOverlapsAabb( const Vector3& center, float radius, const Vector3& min, const Vector3& max )
{
    const float closestX = center.x < min.x ? min.x : ( center.x > max.x ? max.x : center.x );
    const float closestY = center.y < min.y ? min.y : ( center.y > max.y ? max.y : center.y );
    const float closestZ = center.z < min.z ? min.z : ( center.z > max.z ? max.z : center.z );
    const Vector3 closest( closestX, closestY, closestZ );
    return SkullbonezCore::Math::Vector::DistanceSquared( center, closest ) <= radius * radius;
}

uint32_t NextStandaloneInitialGeneration( uint32_t current )
{
    ++current;
    if ( current == 0u )
    {
        return PHYSICS_HANDLE_INITIAL_GENERATION;
    }
    return current;
}
} // namespace


void PhysicsStandaloneWorld::Clear()
{
    m_bodyStore.Clear();
    InvalidateBodyViews();
    m_colliderStore.Clear();
    m_colliderViewScratch.clear();
    m_contacts.clear();
    m_islands.clear();
    m_islandBodyScratch.clear();
    m_islandParentScratch.clear();
    m_islandRankScratch.clear();
    m_islandHandleRowScratch.clear();
    m_islandRootSlotScratch.clear();
    m_islandBodyOffsetScratch.clear();
    m_pointJoints.clear();
    m_constraintGenerations.clear();
    m_constraintAlive.clear();
    m_freeConstraintIndices.clear();
    m_pointJointViewScratch.clear();
    m_broadphaseQueryScratch.clear();
    m_nextInitialGeneration = NextStandaloneInitialGeneration( m_nextInitialGeneration );
    m_sleepEnabled = true;
}


PhysicsBodyHandle PhysicsStandaloneWorld::CreateBody( const PhysicsBodyCreateDesc& desc )
{
    const PhysicsBodyHandle body = m_bodyStore.CreateBodyRecord( desc, m_sleepEnabled );
    InvalidateBodyViews();
    ClearContacts();
    return body;
}


bool PhysicsStandaloneWorld::UpdateBody( const PhysicsBodyUpdateDesc& desc )
{
    PhysicsBodyRecord* body = MutableBodyRecord( desc.body );
    if ( !body )
    {
        return false;
    }
    const int modelIndex = m_bodyStore.ModelIndexForHandle( desc.body );
    if ( modelIndex < 0 )
    {
        return false;
    }
    const std::size_t hotIndex = static_cast<std::size_t>( modelIndex );
    const PhysicsBodyHotFieldsView hotFields = m_bodyStore.MutableHotFields();
    PhysicsBodyHotState hot = LoadPhysicsBodyHotState( hotFields, hotIndex );

    if ( desc.updateMask & PHYSICS_BODY_UPDATE_POSE )
    {
        hot.position = desc.position;
        hot.orientation = desc.orientation;
    }
    if ( desc.updateMask & PHYSICS_BODY_UPDATE_VELOCITY )
    {
        hot.linearVelocity = desc.linearVelocity;
        hot.angularVelocity = desc.angularVelocity;
    }
    const bool updatesMotionKind = ( desc.updateMask & PHYSICS_BODY_UPDATE_MOTION_KIND ) != 0;
    const bool updatesMass = ( desc.updateMask & PHYSICS_BODY_UPDATE_MASS ) != 0;
    if ( updatesMotionKind )
    {
        hot.fixed = desc.motionKind == PhysicsBodyMotionKind::Fixed;
    }
    if ( updatesMass )
    {
        body->mass = desc.mass;
        hot.inverseMass =
            ComputeInverseMass( hot.fixed ? PhysicsBodyMotionKind::Fixed : PhysicsBodyMotionKind::Dynamic, body->mass );
        body->rotationalInertia = desc.rotationalInertia;
        hot.inverseRotationalInertia =
            hot.fixed ? Vector3( 0.0f, 0.0f, 0.0f ) : InvertNonZeroComponents( desc.rotationalInertia );
    }
    else if ( updatesMotionKind )
    {
        hot.inverseMass =
            ComputeInverseMass( hot.fixed ? PhysicsBodyMotionKind::Fixed : PhysicsBodyMotionKind::Dynamic, body->mass );
        if ( hot.fixed )
        {
            hot.inverseRotationalInertia = Vector3( 0.0f, 0.0f, 0.0f );
        }
    }
    if ( desc.updateMask & PHYSICS_BODY_UPDATE_SLEEP_STATE )
    {
        hot.awake = !( m_sleepEnabled && desc.sleeping );
    }
    StorePhysicsBodyHotState( hotFields, hotIndex, hot );
    InvalidateBodyViews();
    ClearContacts();
    return true;
}


bool PhysicsStandaloneWorld::DestroyBody( PhysicsBodyHandle body )
{
    if ( !IsAlive( body ) )
    {
        return false;
    }

    // Invariant: body lifetime owns child collider and connected constraint
    // validity. Standalone callers should see dependent handles fail
    // immediately after their body is deleted.
    std::vector<PhysicsColliderHandle> childColliders;
    for ( const ColliderRecord& collider : m_colliderStore.Records() )
    {
        if ( collider.body == body )
        {
            childColliders.push_back( collider.handle );
        }
    }
    for ( PhysicsColliderHandle collider : childColliders )
    {
        m_colliderStore.DestroyColliderRecord( collider );
    }
    for ( std::size_t i = 0; i < m_pointJoints.size(); ++i )
    {
        if ( m_constraintAlive[i] && ( m_pointJoints[i].bodyA == body || m_pointJoints[i].bodyB == body ) )
        {
            TombstoneConstraintSlot( static_cast<uint32_t>( i ) );
        }
    }
    const bool destroyed = m_bodyStore.DestroyBodyRecord( body );
    if ( destroyed )
    {
        InvalidateBodyViews();
        ClearContacts();
    }
    return destroyed;
}


bool PhysicsStandaloneWorld::SetPendingBodyImpulse( PhysicsBodyHandle body,
                                                    const Vector3& impulse,
                                                    const Vector3& localApplicationPoint )
{
    if ( !m_bodyStore.SetPendingBodyImpulse( body, impulse, localApplicationPoint ) )
    {
        return false;
    }

    InvalidateBodyViews();
    ClearIslands();
    return true;
}


bool PhysicsStandaloneWorld::ApplyBodyImpulse( PhysicsBodyHandle body,
                                               const Vector3& impulse,
                                               const Vector3& localApplicationPoint )
{
    if ( !m_bodyStore.ApplyBodyImpulse( body, impulse, localApplicationPoint ) )
    {
        return false;
    }

    InvalidateBodyViews();
    ClearIslands();
    return true;
}


PhysicsColliderHandle PhysicsStandaloneWorld::CreateCollider( const PhysicsColliderCreateDesc& desc )
{
    const PhysicsBodyRecord* body = BodyRecord( desc.body );
    if ( !body )
    {
        return PhysicsColliderHandle{};
    }

    const PhysicsColliderHandle collider = m_colliderStore.CreateColliderRecord( MakeColliderRecord( desc ) );
    ClearContacts();
    return collider;
}


bool PhysicsStandaloneWorld::UpdateCollider( const PhysicsColliderUpdateDesc& desc )
{
    ColliderRecord* collider = m_colliderStore.MutableRecordForHandle( desc.collider );
    if ( !collider || !IsAlive( collider->body ) )
    {
        return false;
    }

    const bool updatesShape = ( desc.updateMask & PHYSICS_COLLIDER_UPDATE_SHAPE ) != 0;
    const bool updatesBroadphase = ( desc.updateMask & PHYSICS_COLLIDER_UPDATE_BROADPHASE ) != 0;
    if ( updatesShape )
    {
        collider->shape = desc.shape;
        collider->shapeKind = ColliderShapeKindForShape( desc.shape );
    }
    if ( desc.updateMask & PHYSICS_COLLIDER_UPDATE_RESPONSE )
    {
        collider->restitution = desc.restitution;
        collider->friction = desc.friction;
        collider->contactMaterialId = desc.contactMaterialId;
        if ( desc.contactMaterialName[0] != '\0' )
        {
            strncpy_s( collider->contactMaterialName,
                       sizeof( collider->contactMaterialName ),
                       desc.contactMaterialName,
                       _TRUNCATE );
        }
    }
    if ( updatesBroadphase )
    {
        collider->boundingRadius = desc.boundingRadius;
        collider->projectedSurfaceArea = desc.projectedSurfaceArea;
        collider->dragCoefficient = desc.dragCoefficient;
    }
    if ( updatesShape || updatesBroadphase )
    {
        collider->boundingRadius = ConservativeBroadphaseRadius( collider->boundingRadius, collider->shape );
    }
    ClearContacts();
    return true;
}


bool PhysicsStandaloneWorld::DestroyCollider( PhysicsColliderHandle collider )
{
    if ( !IsAlive( collider ) )
    {
        return false;
    }

    const bool destroyed = m_colliderStore.DestroyColliderRecord( collider );
    if ( destroyed )
    {
        ClearContacts();
    }
    return destroyed;
}


PhysicsConstraintHandle PhysicsStandaloneWorld::CreatePointJoint( const PhysicsPointJointCreateDesc& desc )
{
    if ( !IsAlive( desc.bodyA ) || !IsAlive( desc.bodyB ) || desc.bodyA == desc.bodyB )
    {
        return PhysicsConstraintHandle{};
    }

    uint32_t index = 0;
    if ( !m_freeConstraintIndices.empty() )
    {
        index = m_freeConstraintIndices.back();
        m_freeConstraintIndices.pop_back();
    }
    else
    {
        index = static_cast<uint32_t>( m_pointJoints.size() );
        m_pointJoints.push_back( PhysicsPointJointView{} );
        m_constraintGenerations.push_back( m_nextInitialGeneration );
        m_constraintAlive.push_back( 0 );
    }

    PhysicsConstraintHandle handle;
    handle.index = index;
    handle.generation = m_constraintGenerations[index];

    m_pointJoints[index] = MakePointJointView( desc, handle );
    m_constraintAlive[index] = 1;
    ClearIslands();
    return handle;
}


bool PhysicsStandaloneWorld::UpdatePointJoint( const PhysicsPointJointUpdateDesc& desc )
{
    if ( !IsAlive( desc.constraint ) )
    {
        return false;
    }
    if ( ( desc.updateMask & PHYSICS_POINT_JOINT_UPDATE_BODIES ) != 0 &&
         ( !IsAlive( desc.bodyA ) || !IsAlive( desc.bodyB ) || desc.bodyA == desc.bodyB ) )
    {
        return false;
    }

    PhysicsPointJointView& joint = m_pointJoints[desc.constraint.index];
    if ( desc.updateMask & PHYSICS_POINT_JOINT_UPDATE_BODIES )
    {
        joint.bodyA = desc.bodyA;
        joint.bodyB = desc.bodyB;
    }
    if ( desc.updateMask & PHYSICS_POINT_JOINT_UPDATE_ANCHORS )
    {
        joint.localAnchorA = desc.localAnchorA;
        joint.localAnchorB = desc.localAnchorB;
    }
    if ( desc.updateMask & PHYSICS_POINT_JOINT_UPDATE_SOLVER )
    {
        joint.slack = desc.slack;
        joint.stiffness = desc.stiffness;
        joint.damping = desc.damping;
    }
    if ( desc.updateMask & PHYSICS_POINT_JOINT_UPDATE_GROUP )
    {
        joint.groupId = desc.groupId;
        joint.flags = desc.flags;
    }
    ClearIslands();
    return true;
}


bool PhysicsStandaloneWorld::DestroyConstraint( PhysicsConstraintHandle constraint )
{
    if ( !IsAlive( constraint ) )
    {
        return false;
    }

    TombstoneConstraintSlot( constraint.index );
    ClearIslands();
    return true;
}


bool PhysicsStandaloneWorld::Step( const PhysicsStandaloneStepDesc& desc )
{
    if ( desc.deltaSeconds < 0.0f )
    {
        return false;
    }
    ClearContacts();
    if ( !desc.scenePhysicsEnabled )
    {
        return true;
    }

    bool mutated = false;
    if ( desc.deltaSeconds > 0.0f )
    {
        const auto bodies = m_bodyStore.Records();
        const auto hotFields = m_bodyStore.MutableHotFields();
        const auto hotRead = ConstPhysicsBodyHotFields( hotFields );
        for ( std::size_t bodyIndex = 0; bodyIndex < bodies.size(); ++bodyIndex )
        {
            if ( hotRead.fixed[bodyIndex] != 0u || ( m_sleepEnabled && hotRead.awake[bodyIndex] == 0u ) )
            {
                continue;
            }

            // Invariant: consume one-shot impulses as velocity edits before the
            // semi-implicit acceleration/position step. Reordering these operations
            // changes deterministic smoke and future replay samples.
            m_bodyStore.ConsumePendingBodyImpulse( static_cast<int>( bodyIndex ) );
            const Vector3 linearVelocity =
                PhysicsBodyLinearVelocity( hotRead, bodyIndex ) + desc.worldLinearAcceleration * desc.deltaSeconds;
            const Vector3 position = PhysicsBodyPosition( hotRead, bodyIndex ) + linearVelocity * desc.deltaSeconds;
            hotFields.linearVelocityX[bodyIndex] = linearVelocity.x;
            hotFields.linearVelocityY[bodyIndex] = linearVelocity.y;
            hotFields.linearVelocityZ[bodyIndex] = linearVelocity.z;
            hotFields.positionX[bodyIndex] = position.x;
            hotFields.positionY[bodyIndex] = position.y;
            hotFields.positionZ[bodyIndex] = position.z;
            mutated = true;
        }
    }
    if ( mutated )
    {
        InvalidateBodyViews();
    }
    GenerateStandaloneContacts();
    GenerateStandaloneIslands();
    return true;
}


void PhysicsStandaloneWorld::ClearContacts()
{
    m_contacts.clear();
    ClearIslands();
}


void PhysicsStandaloneWorld::ClearIslands()
{
    m_islands.clear();
    m_islandBodyScratch.clear();
}


void PhysicsStandaloneWorld::GenerateStandaloneContacts()
{
    const auto colliders = m_colliderStore.Records();
    const auto hotFields = m_bodyStore.HotFields();
    for ( std::size_t a = 0; a < colliders.size(); ++a )
    {
        const ColliderRecord& colliderA = colliders[a];
        const PhysicsBodyRecord* bodyA = BodyRecord( colliderA.body );
        const int bodyAIndex = m_bodyStore.ModelIndexForHandle( colliderA.body );
        if ( !bodyA || bodyAIndex < 0 )
        {
            continue;
        }

        for ( std::size_t b = a + 1u; b < colliders.size(); ++b )
        {
            const ColliderRecord& colliderB = colliders[b];
            if ( colliderA.body == colliderB.body )
            {
                continue;
            }

            const PhysicsBodyRecord* bodyB = BodyRecord( colliderB.body );
            const int bodyBIndex = m_bodyStore.ModelIndexForHandle( colliderB.body );
            if ( !bodyB || bodyBIndex < 0 ||
                 ( hotFields.fixed[static_cast<std::size_t>( bodyAIndex )] != 0u &&
                   hotFields.fixed[static_cast<std::size_t>( bodyBIndex )] != 0u ) )
            {
                continue;
            }

            if ( TryAppendSphereSphereContact( colliderA,
                                               static_cast<std::size_t>( bodyAIndex ),
                                               colliderB,
                                               static_cast<std::size_t>( bodyBIndex ) ) )
            {
                continue;
            }
            (void)TryAppendSphereBoxContact( colliderA,
                                             static_cast<std::size_t>( bodyAIndex ),
                                             colliderB,
                                             static_cast<std::size_t>( bodyBIndex ) );
        }
    }
}


bool PhysicsStandaloneWorld::TryAppendSphereSphereContact( const ColliderRecord& colliderA,
                                                           std::size_t bodyAIndex,
                                                           const ColliderRecord& colliderB,
                                                           std::size_t bodyBIndex )
{
    const BoundingSphere* sphereA = std::get_if<BoundingSphere>( &colliderA.shape );
    const BoundingSphere* sphereB = std::get_if<BoundingSphere>( &colliderB.shape );
    if ( !sphereA || !sphereB )
    {
        return false;
    }

    const float radiusA = sphereA->GetRadius();
    const float radiusB = sphereB->GetRadius();
    const float radiusSum = radiusA + radiusB;
    const auto hotFields = m_bodyStore.HotFields();
    const Vector3 centerA = ColliderWorldCenter( hotFields, bodyAIndex, colliderA );
    const Vector3 centerB = ColliderWorldCenter( hotFields, bodyBIndex, colliderB );
    const Vector3 delta = centerB - centerA;
    const float distance = VectorMag( delta );
    if ( distance > radiusSum )
    {
        return false;
    }

    const Vector3 normal = distance > 0.0f ? delta / distance : Vector3( 1.0f, 0.0f, 0.0f );
    const float penetration = radiusSum - distance;

    PhysicsContactView contact;
    contact.bodyA = colliderA.body;
    contact.bodyB = colliderB.body;
    contact.colliderA = colliderA.handle;
    contact.colliderB = colliderB.handle;
    contact.point = centerA + normal * ( radiusA - penetration * 0.5f );
    contact.normal = normal;
    contact.penetrationDepth = penetration;
    CopyContactMaterialPayload( contact, colliderA, colliderB );
    contact.touching = true;
    m_contacts.push_back( contact );
    return true;
}


bool PhysicsStandaloneWorld::TryAppendSphereBoxContact( const ColliderRecord& colliderA,
                                                        std::size_t bodyAIndex,
                                                        const ColliderRecord& colliderB,
                                                        std::size_t bodyBIndex )
{
    const BoundingSphere* sphere = std::get_if<BoundingSphere>( &colliderA.shape );
    const BoundingBox* box = std::get_if<BoundingBox>( &colliderB.shape );
    const ColliderRecord* sphereCollider = &colliderA;
    const ColliderRecord* boxCollider = &colliderB;
    std::size_t sphereBodyIndex = bodyAIndex;
    std::size_t boxBodyIndex = bodyBIndex;
    bool sphereIsColliderA = true;
    if ( !sphere || !box )
    {
        sphere = std::get_if<BoundingSphere>( &colliderB.shape );
        box = std::get_if<BoundingBox>( &colliderA.shape );
        sphereCollider = &colliderB;
        boxCollider = &colliderA;
        sphereBodyIndex = bodyBIndex;
        boxBodyIndex = bodyAIndex;
        sphereIsColliderA = false;
    }
    if ( !sphere || !box )
    {
        return false;
    }

    const auto hotFields = m_bodyStore.HotFields();
    auto boxOrientation = PhysicsBodyOrientation( hotFields, boxBodyIndex );
    const RotationMatrix boxRotation = boxOrientation.GetOrientationMatrix();
    const Vector3 boxCenter = ColliderWorldCenter( hotFields, boxBodyIndex, *boxCollider );
    const Vector3 sphereCenter = ColliderWorldCenter( hotFields, sphereBodyIndex, *sphereCollider );
    const Vector3 sphereCenterInBox = boxRotation.TransposeMultiply( sphereCenter - boxCenter );
    const Vector3 halfExtents = box->GetHalfExtents();
    Vector3 closestInBox( ClampFloat( sphereCenterInBox.x, -halfExtents.x, halfExtents.x ),
                          ClampFloat( sphereCenterInBox.y, -halfExtents.y, halfExtents.y ),
                          ClampFloat( sphereCenterInBox.z, -halfExtents.z, halfExtents.z ) );
    Vector3 boxToSphereNormalInBox = sphereCenterInBox - closestInBox;
    const float radius = sphere->GetRadius();
    const float distanceSquared = VectorMagSquared( boxToSphereNormalInBox );
    if ( distanceSquared > radius * radius )
    {
        return false;
    }

    float penetration = 0.0f;
    if ( distanceSquared > 0.0f )
    {
        const float distance = sqrtf( distanceSquared );
        boxToSphereNormalInBox /= distance;
        penetration = radius - distance;
    }
    else
    {
        // Concept: when the sphere center is inside the box, the closest point
        // clamp lands on the center. Pick the nearest face as the contact feature
        // so the row still has a deterministic outward normal and penetration.
        const float distanceToXFace = halfExtents.x - fabsf( sphereCenterInBox.x );
        const float distanceToYFace = halfExtents.y - fabsf( sphereCenterInBox.y );
        const float distanceToZFace = halfExtents.z - fabsf( sphereCenterInBox.z );
        boxToSphereNormalInBox = Vector3( sphereCenterInBox.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f );
        closestInBox.x = boxToSphereNormalInBox.x * halfExtents.x;
        penetration = radius + distanceToXFace;
        if ( distanceToYFace < distanceToXFace && distanceToYFace <= distanceToZFace )
        {
            boxToSphereNormalInBox = Vector3( 0.0f, sphereCenterInBox.y >= 0.0f ? 1.0f : -1.0f, 0.0f );
            closestInBox = sphereCenterInBox;
            closestInBox.y = boxToSphereNormalInBox.y * halfExtents.y;
            penetration = radius + distanceToYFace;
        }
        else if ( distanceToZFace < distanceToXFace && distanceToZFace < distanceToYFace )
        {
            boxToSphereNormalInBox = Vector3( 0.0f, 0.0f, sphereCenterInBox.z >= 0.0f ? 1.0f : -1.0f );
            closestInBox = sphereCenterInBox;
            closestInBox.z = boxToSphereNormalInBox.z * halfExtents.z;
            penetration = radius + distanceToZFace;
        }
    }

    const Vector3 boxToSphereNormal = boxRotation * boxToSphereNormalInBox;
    const Vector3 closestWorld = boxCenter + boxRotation * closestInBox;
    const Vector3 sphereSurface = sphereCenter - boxToSphereNormal * radius;

    PhysicsContactView contact;
    contact.bodyA = colliderA.body;
    contact.bodyB = colliderB.body;
    contact.colliderA = colliderA.handle;
    contact.colliderB = colliderB.handle;
    contact.point = ( closestWorld + sphereSurface ) * 0.5f;
    contact.normal = sphereIsColliderA ? -boxToSphereNormal : boxToSphereNormal;
    contact.penetrationDepth = penetration;
    CopyContactMaterialPayload( contact, colliderA, colliderB );
    contact.touching = true;
    m_contacts.push_back( contact );
    return true;
}


void PhysicsStandaloneWorld::GenerateStandaloneIslands()
{
    ClearIslands();

    const auto bodies = m_bodyStore.Records();
    const auto hotFields = m_bodyStore.HotFields();
    if ( bodies.empty() )
    {
        return;
    }

    uint32_t maxBodyHandleIndex = 0;
    for ( const PhysicsBodyRecord& body : bodies )
    {
        if ( body.handle.index > maxBodyHandleIndex )
        {
            maxBodyHandleIndex = body.handle.index;
        }
    }

    const uint32_t bodyCount = static_cast<uint32_t>( bodies.size() );
    m_islandParentScratch.resize( bodyCount );
    m_islandRankScratch.assign( bodyCount, 0u );
    m_islandHandleRowScratch.assign( static_cast<std::size_t>( maxBodyHandleIndex ) + 1u, INVALID_ISLAND_ROW );
    for ( uint32_t row = 0; row < bodyCount; ++row )
    {
        m_islandParentScratch[row] = row;
        m_islandHandleRowScratch[bodies[row].handle.index] = row;
    }

    auto rowForBody = [&]( PhysicsBodyHandle body ) -> uint32_t
    {
        if ( body.index >= m_islandHandleRowScratch.size() )
        {
            return INVALID_ISLAND_ROW;
        }
        const uint32_t row = m_islandHandleRowScratch[body.index];
        return row != INVALID_ISLAND_ROW && bodies[row].handle == body ? row : INVALID_ISLAND_ROW;
    };

    for ( const PhysicsContactView& contact : m_contacts )
    {
        if ( !contact.touching )
        {
            continue;
        }
        const uint32_t rowA = rowForBody( contact.bodyA );
        const uint32_t rowB = rowForBody( contact.bodyB );
        if ( rowA != INVALID_ISLAND_ROW && rowB != INVALID_ISLAND_ROW )
        {
            UnionIslandRows( m_islandParentScratch, m_islandRankScratch, rowA, rowB );
        }
    }

    for ( std::size_t i = 0; i < m_pointJoints.size(); ++i )
    {
        if ( !m_constraintAlive[i] )
        {
            continue;
        }
        const PhysicsPointJointView& joint = m_pointJoints[i];
        const uint32_t rowA = rowForBody( joint.bodyA );
        const uint32_t rowB = rowForBody( joint.bodyB );
        if ( rowA != INVALID_ISLAND_ROW && rowB != INVALID_ISLAND_ROW )
        {
            UnionIslandRows( m_islandParentScratch, m_islandRankScratch, rowA, rowB );
        }
    }

    m_islandRootSlotScratch.assign( bodyCount, INVALID_ISLAND_ROW );
    for ( uint32_t row = 0; row < bodyCount; ++row )
    {
        const uint32_t root = FindIslandRoot( m_islandParentScratch, row );
        uint32_t islandSlot = m_islandRootSlotScratch[root];
        if ( islandSlot == INVALID_ISLAND_ROW )
        {
            islandSlot = static_cast<uint32_t>( m_islands.size() );
            m_islandRootSlotScratch[root] = islandSlot;

            PhysicsIslandView island;
            island.islandId = islandSlot + 1u;
            island.sleeping = true;
            m_islands.push_back( island );
        }

        PhysicsIslandView& island = m_islands[islandSlot];
        ++island.bodyCount;
        island.supported = island.supported || hotFields.fixed[row] != 0u;
        if ( hotFields.fixed[row] == 0u )
        {
            island.sleeping = island.sleeping && m_sleepEnabled && hotFields.awake[row] == 0u;
        }
    }

    m_islandBodyOffsetScratch.resize( m_islands.size() );
    uint32_t bodyOffset = 0;
    for ( uint32_t islandSlot = 0; islandSlot < static_cast<uint32_t>( m_islands.size() ); ++islandSlot )
    {
        m_islandBodyOffsetScratch[islandSlot] = bodyOffset;
        bodyOffset += m_islands[islandSlot].bodyCount;
        m_islands[islandSlot].bodyCount = 0;
    }

    m_islandBodyScratch.resize( bodyOffset );
    for ( uint32_t row = 0; row < bodyCount; ++row )
    {
        const uint32_t root = FindIslandRoot( m_islandParentScratch, row );
        const uint32_t islandSlot = m_islandRootSlotScratch[root];
        PhysicsIslandView& island = m_islands[islandSlot];
        const uint32_t writeIndex = m_islandBodyOffsetScratch[islandSlot] + island.bodyCount;
        m_islandBodyScratch[writeIndex] = bodies[row].handle;
        ++island.bodyCount;
    }

    // Lifetime: island rows borrow from one flat body-handle buffer. Assign the
    // pointers after the buffer is fully resized so callers never see stale spans.
    for ( uint32_t islandSlot = 0; islandSlot < static_cast<uint32_t>( m_islands.size() ); ++islandSlot )
    {
        PhysicsIslandView& island = m_islands[islandSlot];
        island.bodies = island.bodyCount == 0u ? nullptr : &m_islandBodyScratch[m_islandBodyOffsetScratch[islandSlot]];
    }
}


bool PhysicsStandaloneWorld::ApplyActivationCommand( const PhysicsActivationCommand& command )
{
    if ( command.kind == PhysicsActivationCommandKind::SetSleepEnabled )
    {
        m_sleepEnabled = command.enabled;
        if ( !m_sleepEnabled )
        {
            // Invariant: disabling sleep makes Step() treat every live body as
            // awake, mirroring the legacy world path that clears sleep state.
            const auto hotFields = m_bodyStore.MutableHotFields();
            std::fill( hotFields.awake.begin(), hotFields.awake.end(), static_cast<uint8_t>( 1u ) );
            InvalidateBodyViews();
            ClearIslands();
        }
        return true;
    }

    const int bodyIndex = m_bodyStore.ModelIndexForHandle( command.body );
    if ( bodyIndex < 0 )
    {
        return false;
    }

    const std::size_t hotIndex = static_cast<std::size_t>( bodyIndex );
    const auto hotRead = m_bodyStore.HotFields();
    if ( hotRead.fixed[hotIndex] != 0u )
    {
        return false;
    }

    switch ( command.kind )
    {
    case PhysicsActivationCommandKind::WakeBody:
        (void)m_bodyStore.WakeBody( command.body );
        InvalidateBodyViews();
        ClearIslands();
        return true;
    case PhysicsActivationCommandKind::SeedBodyAsleep:
        if ( !m_sleepEnabled )
        {
            return false;
        }
        (void)m_bodyStore.SeedBodyAsleep( command.body );
        InvalidateBodyViews();
        ClearIslands();
        return true;
    case PhysicsActivationCommandKind::SetSleepEnabled:
        break;
    }

    return false;
}


bool PhysicsStandaloneWorld::SleepEnabled() const
{
    return m_sleepEnabled;
}


PhysicsRayCastHit PhysicsStandaloneWorld::RayCast( const PhysicsRayCastDesc& desc ) const
{
    PhysicsRayCastHit closestHit;
    if ( desc.maxDistance < 0.0f )
    {
        return closestHit;
    }

    const float directionLength = VectorMag( desc.direction );
    if ( directionLength == 0.0f )
    {
        return closestHit;
    }
    const Vector3 direction = desc.direction / directionLength;
    float closestDistance = ( std::numeric_limits<float>::max )();
    const auto hotFields = m_bodyStore.HotFields();

    // Concept: standalone ray casts use the same conservative broadphase
    // envelope as runtime tool rays. Exact shape-specific picks can replace
    // this later without changing the public handle-based query contract.
    for ( const ColliderRecord& collider : m_colliderStore.Records() )
    {
        const PhysicsBodyRecord* body = BodyRecord( collider.body );
        const int bodyIndex = m_bodyStore.ModelIndexForHandle( collider.body );
        if ( !body || bodyIndex < 0 ||
             !BodyPassesQueryFilters( hotFields,
                                      static_cast<std::size_t>( bodyIndex ),
                                      desc.includeFixedBodies,
                                      desc.includeSleepingBodies,
                                      m_sleepEnabled ) )
        {
            continue;
        }

        float distance = 0.0f;
        const float radius = EffectiveColliderRadius( collider );
        const Vector3 center = ColliderWorldCenter( hotFields, static_cast<std::size_t>( bodyIndex ), collider );
        if ( !IntersectRaySphere( desc.origin, direction, center, radius, distance ) || distance > desc.maxDistance ||
             distance >= closestDistance )
        {
            continue;
        }

        closestDistance = distance;
        closestHit.body = body->handle;
        closestHit.collider = collider.handle;
        closestHit.sceneObjectId = collider.sceneObjectId.IsValid() ? collider.sceneObjectId : body->sceneObjectId;
        closestHit.distance = distance;
        closestHit.point = desc.origin + direction * distance;
        closestHit.normal = closestHit.point - center;
        const float normalLength = VectorMag( closestHit.normal );
        closestHit.normal = normalLength > 0.0f ? closestHit.normal / normalLength : -direction;
        closestHit.hit = true;
    }

    return closestHit;
}


SkullbonezCore::Physics::PhysicsBroadphaseQueryResultView
PhysicsStandaloneWorld::QueryBroadphaseCells( const PhysicsBroadphaseCellQueryDesc& desc ) const
{
    m_broadphaseQueryScratch.clear();

    const auto bodies = m_bodyStore.Records();
    const auto hotFields = m_bodyStore.HotFields();
    for ( std::size_t bodyIndex = 0; bodyIndex < bodies.size(); ++bodyIndex )
    {
        const PhysicsBodyRecord& body = bodies[bodyIndex];
        if ( !BodyPassesQueryFilters( hotFields,
                                      bodyIndex,
                                      desc.includeFixedBodies,
                                      desc.includeSleepingBodies,
                                      m_sleepEnabled ) )
        {
            continue;
        }

        bool overlaps = hotFields.boundingRadius[bodyIndex] > 0.0f &&
                        SphereOverlapsAabb( PhysicsBodyPosition( hotFields, bodyIndex ),
                                            hotFields.boundingRadius[bodyIndex],
                                            desc.min,
                                            desc.max );
        for ( const ColliderRecord& collider : m_colliderStore.Records() )
        {
            if ( overlaps )
            {
                break;
            }
            if ( collider.body == body.handle )
            {
                overlaps = SphereOverlapsAabb( ColliderWorldCenter( hotFields, bodyIndex, collider ),
                                               EffectiveColliderRadius( collider ),
                                               desc.min,
                                               desc.max );
            }
        }

        if ( overlaps )
        {
            m_broadphaseQueryScratch.push_back( body.handle );
        }
    }

    SkullbonezCore::Physics::PhysicsBroadphaseQueryResultView view;
    view.bodies = m_broadphaseQueryScratch.empty() ? nullptr : m_broadphaseQueryScratch.data();
    view.bodyCount = static_cast<uint32_t>( m_broadphaseQueryScratch.size() );
    return view;
}


const PhysicsBodyView* PhysicsStandaloneWorld::Body( PhysicsBodyHandle body ) const
{
    const std::vector<PhysicsBodyView>& bodyViews = BodyViewCache();
    for ( const PhysicsBodyView& view : bodyViews )
    {
        if ( view.body == body )
        {
            return &view;
        }
    }
    return nullptr;
}


SkullbonezCore::Physics::PhysicsBodyCollectionView PhysicsStandaloneWorld::Bodies() const
{
    const std::vector<PhysicsBodyView>& bodyViews = BodyViewCache();

    SkullbonezCore::Physics::PhysicsBodyCollectionView view;
    view.bodies = bodyViews.empty() ? nullptr : bodyViews.data();
    view.bodyCount = static_cast<uint32_t>( bodyViews.size() );
    return view;
}


const PhysicsColliderView* PhysicsStandaloneWorld::Collider( PhysicsColliderHandle collider ) const
{
    const ColliderRecord* record = m_colliderStore.RecordForHandle( collider );
    if ( !record || !IsAlive( record->body ) )
    {
        return nullptr;
    }

    m_singleColliderViewScratch = MakeColliderView( *record );
    return &m_singleColliderViewScratch;
}


SkullbonezCore::Physics::PhysicsColliderCollectionView PhysicsStandaloneWorld::Colliders() const
{
    m_colliderViewScratch.clear();
    for ( const ColliderRecord& collider : m_colliderStore.Records() )
    {
        if ( IsAlive( collider.body ) )
        {
            m_colliderViewScratch.push_back( MakeColliderView( collider ) );
        }
    }

    SkullbonezCore::Physics::PhysicsColliderCollectionView view;
    view.colliders = m_colliderViewScratch.empty() ? nullptr : m_colliderViewScratch.data();
    view.colliderCount = static_cast<uint32_t>( m_colliderViewScratch.size() );
    return view;
}


const PhysicsPointJointView* PhysicsStandaloneWorld::PointJoint( PhysicsConstraintHandle constraint ) const
{
    return IsAlive( constraint ) ? &m_pointJoints[constraint.index] : nullptr;
}


SkullbonezCore::Physics::PhysicsPointJointCollectionView PhysicsStandaloneWorld::PointJoints() const
{
    m_pointJointViewScratch.clear();
    for ( std::size_t i = 0; i < m_pointJoints.size(); ++i )
    {
        if ( m_constraintAlive[i] )
        {
            m_pointJointViewScratch.push_back( m_pointJoints[i] );
        }
    }

    SkullbonezCore::Physics::PhysicsPointJointCollectionView view;
    view.pointJoints = m_pointJointViewScratch.empty() ? nullptr : m_pointJointViewScratch.data();
    view.pointJointCount = static_cast<uint32_t>( m_pointJointViewScratch.size() );
    return view;
}


SkullbonezCore::Physics::PhysicsContactCollectionView PhysicsStandaloneWorld::Contacts() const
{
    PhysicsContactCollectionView view;
    view.contacts = m_contacts.empty() ? nullptr : m_contacts.data();
    view.contactCount = static_cast<uint32_t>( m_contacts.size() );
    return view;
}


SkullbonezCore::Physics::PhysicsIslandCollectionView PhysicsStandaloneWorld::Islands() const
{
    PhysicsIslandCollectionView view;
    view.islands = m_islands.empty() ? nullptr : m_islands.data();
    view.islandCount = static_cast<uint32_t>( m_islands.size() );
    return view;
}


bool PhysicsStandaloneWorld::IsAlive( PhysicsBodyHandle body ) const
{
    return m_bodyStore.Contains( body );
}


bool PhysicsStandaloneWorld::IsAlive( PhysicsColliderHandle collider ) const
{
    const ColliderRecord* record = m_colliderStore.RecordForHandle( collider );
    return record && IsAlive( record->body );
}


bool PhysicsStandaloneWorld::IsAlive( PhysicsConstraintHandle constraint ) const
{
    return constraint.IsValid() && constraint.index < m_pointJoints.size() &&
           m_constraintAlive[constraint.index] != 0 &&
           m_constraintGenerations[constraint.index] == constraint.generation &&
           IsAlive( m_pointJoints[constraint.index].bodyA ) && IsAlive( m_pointJoints[constraint.index].bodyB );
}


PhysicsBodyRecord* PhysicsStandaloneWorld::MutableBodyRecord( PhysicsBodyHandle body )
{
    return m_bodyStore.MutableRecordForHandle( body );
}


const PhysicsBodyRecord* PhysicsStandaloneWorld::BodyRecord( PhysicsBodyHandle body ) const
{
    return m_bodyStore.RecordForHandle( body );
}


PhysicsBodyView PhysicsStandaloneWorld::MakeBodyView( const PhysicsBodyRecord& record, std::size_t bodyIndex ) const
{
    const auto hotFields = m_bodyStore.HotFields();
    PhysicsBodyView view;
    view.body = record.handle;
    view.sceneObjectId = record.sceneObjectId;
    view.position = PhysicsBodyPosition( hotFields, bodyIndex );
    view.orientation = PhysicsBodyOrientation( hotFields, bodyIndex );
    view.linearVelocity = PhysicsBodyLinearVelocity( hotFields, bodyIndex );
    view.angularVelocity = PhysicsBodyAngularVelocity( hotFields, bodyIndex );
    view.rotationalInertia = record.rotationalInertia;
    view.inverseRotationalInertia = PhysicsBodyInverseInertia( hotFields, bodyIndex );
    view.mass = record.mass;
    view.inverseMass = hotFields.inverseMass[bodyIndex];
    view.boundingRadius = hotFields.boundingRadius[bodyIndex];
    view.motionKind = hotFields.fixed[bodyIndex] != 0u ? PhysicsBodyMotionKind::Fixed : PhysicsBodyMotionKind::Dynamic;
    view.sleeping = hotFields.awake[bodyIndex] == 0u;
    return view;
}


void PhysicsStandaloneWorld::InvalidateBodyViews()
{
    m_bodyViewCacheDirty = true;
}


const std::vector<PhysicsBodyView>& PhysicsStandaloneWorld::BodyViewCache() const
{
    if ( !m_bodyViewCacheDirty )
    {
        return m_bodyViewCache;
    }

    // Why: public Body()/Bodies() return immutable view rows, not mutable store
    // records. Rebuild this cold cache only after body mutations so the hot step
    // keeps iterating PhysicsBodyStore without a parallel authoritative mirror.
    m_bodyViewCache.clear();
    const auto records = m_bodyStore.Records();
    for ( std::size_t bodyIndex = 0; bodyIndex < records.size(); ++bodyIndex )
    {
        m_bodyViewCache.push_back( MakeBodyView( records[bodyIndex], bodyIndex ) );
    }
    m_bodyViewCacheDirty = false;
    return m_bodyViewCache;
}


ColliderRecord PhysicsStandaloneWorld::MakeColliderRecord( const PhysicsColliderCreateDesc& desc ) const
{
    const PhysicsBodyRecord* body = BodyRecord( desc.body );

    ColliderRecord record;
    record.body = desc.body;
    record.sceneObjectId =
        desc.sceneObjectId.IsValid() ? desc.sceneObjectId : ( body ? body->sceneObjectId : PhysicsSceneObjectId{} );
    record.shape = desc.shape;
    record.shapeKind = ColliderShapeKindForShape( desc.shape );
    record.boundingRadius = ConservativeBroadphaseRadius( desc.boundingRadius, desc.shape );
    record.restitution = desc.restitution;
    record.friction = desc.friction;
    record.contactMaterialId = desc.contactMaterialId;
    strncpy_s( record.contactMaterialName, sizeof( record.contactMaterialName ), desc.contactMaterialName, _TRUNCATE );
    record.projectedSurfaceArea = desc.projectedSurfaceArea;
    record.dragCoefficient = desc.dragCoefficient;
    return record;
}


PhysicsColliderView PhysicsStandaloneWorld::MakeColliderView( const ColliderRecord& record ) const
{
    PhysicsColliderView view;
    view.collider = record.handle;
    view.body = record.body;
    view.sceneObjectId = record.sceneObjectId;
    view.shape = record.shape;
    view.boundingRadius = record.boundingRadius;
    view.restitution = record.restitution;
    view.friction = record.friction;
    view.contactMaterialId = record.contactMaterialId;
    view.projectedSurfaceArea = record.projectedSurfaceArea;
    view.dragCoefficient = record.dragCoefficient;
    return view;
}


PhysicsPointJointView PhysicsStandaloneWorld::MakePointJointView( const PhysicsPointJointCreateDesc& desc,
                                                                  PhysicsConstraintHandle constraint ) const
{
    PhysicsPointJointView view;
    view.constraint = constraint;
    view.bodyA = desc.bodyA;
    view.bodyB = desc.bodyB;
    view.localAnchorA = desc.localAnchorA;
    view.localAnchorB = desc.localAnchorB;
    view.slack = desc.slack;
    view.stiffness = desc.stiffness;
    view.damping = desc.damping;
    view.groupId = desc.groupId;
    view.flags = desc.flags;
    return view;
}


void PhysicsStandaloneWorld::TombstoneConstraintSlot( uint32_t index )
{
    if ( index >= m_pointJoints.size() || !m_constraintAlive[index] )
    {
        return;
    }

    m_constraintAlive[index] = 0;
    ++m_constraintGenerations[index];
    if ( m_constraintGenerations[index] == 0 )
    {
        m_constraintGenerations[index] = m_nextInitialGeneration;
    }
    m_freeConstraintIndices.push_back( index );
}


PhysicsStandaloneSmokeResult SkullbonezCore::Physics::RunPhysicsStandaloneSmoke()
{
    // Phase: cold validation probe. Each scenario world below owns several
    // SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS fixed stores and must not consume the launcher thread's
    // bounded stack merely to exercise the public API.
    auto worldOwner = std::make_unique<PhysicsStandaloneWorld>();
    PhysicsStandaloneWorld& world = *worldOwner;

    PhysicsBodyCreateDesc bodyDesc;
    bodyDesc.sceneObjectId = PhysicsSceneObjectId{ 7u };
    bodyDesc.position = Vector3( 1.0f, 10.0f, -2.0f );
    bodyDesc.linearVelocity = Vector3( 2.0f, 4.0f, 0.0f );
    bodyDesc.mass = 2.0f;
    bodyDesc.motionKind = PhysicsBodyMotionKind::Dynamic;
    bodyDesc.orientation.RotateAboutAxis( Vector3( 1.0f, 0.0f, 0.0f ), _HALF_PI );

    const PhysicsBodyHandle body = world.CreateBody( bodyDesc );

    PhysicsBodyCreateDesc secondaryDesc;
    secondaryDesc.sceneObjectId = PhysicsSceneObjectId{ 12u };
    secondaryDesc.position = Vector3( -3.0f, 12.0f, 1.0f );
    secondaryDesc.linearVelocity = Vector3( -1.0f, 1.0f, 0.5f );
    secondaryDesc.mass = 2.5f;
    secondaryDesc.motionKind = PhysicsBodyMotionKind::Dynamic;
    const PhysicsBodyHandle secondaryBody = world.CreateBody( secondaryDesc );

    PhysicsBodyCreateDesc transientDesc;
    transientDesc.sceneObjectId = PhysicsSceneObjectId{ 8u };
    transientDesc.position = Vector3( -1.0f, 2.0f, 0.0f );
    transientDesc.linearVelocity = Vector3( 5.0f, 0.0f, 0.0f );
    transientDesc.mass = 3.0f;
    const PhysicsBodyHandle transientBody = world.CreateBody( transientDesc );

    PhysicsBodyCreateDesc endpointDesc;
    endpointDesc.sceneObjectId = PhysicsSceneObjectId{ 9u };
    endpointDesc.position = Vector3( 0.0f, 4.0f, 1.0f );
    endpointDesc.mass = 5.0f;
    const PhysicsBodyHandle endpointBody = world.CreateBody( endpointDesc );

    PhysicsBodyCreateDesc activationDesc;
    activationDesc.sceneObjectId = PhysicsSceneObjectId{ 10u };
    activationDesc.position = Vector3( 4.0f, 6.0f, 2.0f );
    activationDesc.linearVelocity = Vector3( 1.0f, 2.0f, 3.0f );
    activationDesc.angularVelocity = Vector3( 0.1f, 0.2f, 0.3f );
    activationDesc.mass = 6.0f;
    const PhysicsBodyHandle activationBody = world.CreateBody( activationDesc );

    PhysicsBodyCreateDesc editableDesc;
    editableDesc.sceneObjectId = PhysicsSceneObjectId{ 13u };
    editableDesc.position = Vector3( 7.0f, 8.0f, 9.0f );
    editableDesc.mass = 1.5f;
    const PhysicsBodyHandle editableBody = world.CreateBody( editableDesc );

    const bool invalidBodyColliderRejected = !world.CreateCollider( PhysicsColliderCreateDesc{} ).IsValid();
    const bool invalidPointJointRejected = !world.CreatePointJoint( PhysicsPointJointCreateDesc{} ).IsValid();
    PhysicsPointJointCreateDesc selfPointJointDesc;
    selfPointJointDesc.bodyA = body;
    selfPointJointDesc.bodyB = body;
    const bool selfPointJointRejected = !world.CreatePointJoint( selfPointJointDesc ).IsValid();

    PhysicsBodyUpdateDesc transientUpdate;
    transientUpdate.body = transientBody;
    transientUpdate.updateMask = PHYSICS_BODY_UPDATE_MASS | PHYSICS_BODY_UPDATE_MOTION_KIND;
    transientUpdate.mass = 4.0f;
    transientUpdate.motionKind = PhysicsBodyMotionKind::Fixed;

    PhysicsBodyUpdateDesc editableUpdate;
    editableUpdate.body = editableBody;
    editableUpdate.updateMask = PHYSICS_BODY_UPDATE_POSE | PHYSICS_BODY_UPDATE_VELOCITY;
    editableUpdate.position = Vector3( -7.0f, 3.0f, 2.0f );
    editableUpdate.linearVelocity = Vector3( 0.5f, 1.5f, -0.5f );
    editableUpdate.angularVelocity = Vector3( 0.25f, 0.5f, 0.75f );
    const bool updatedEditableBody = world.UpdateBody( editableUpdate );
    const PhysicsBodyView* updatedEditableBodyView = world.Body( editableBody );
    const bool poseVelocityUpdateConsistent =
        updatedEditableBody && updatedEditableBodyView &&
        updatedEditableBodyView->position == editableUpdate.position &&
        updatedEditableBodyView->linearVelocity == editableUpdate.linearVelocity &&
        updatedEditableBodyView->angularVelocity == editableUpdate.angularVelocity;

    PhysicsColliderCreateDesc colliderDesc;
    colliderDesc.body = body;
    colliderDesc.sceneObjectId = PhysicsSceneObjectId{ 17u };
    colliderDesc.boundingRadius = 1.5f;
    colliderDesc.restitution = 0.1f;
    colliderDesc.friction = 0.2f;
    colliderDesc.contactMaterialId = 101u;
    colliderDesc.projectedSurfaceArea = 3.0f;
    colliderDesc.dragCoefficient = 0.4f;
    const PhysicsColliderHandle collider = world.CreateCollider( colliderDesc );

    const Vector3 localColliderOffset( 0.0f, 1.0f, 0.0f );
    constexpr float COLLIDER_SPHERE_RADIUS = 2.0f;
    constexpr float CONSERVATIVE_COLLIDER_RADIUS = 3.0f;

    PhysicsColliderUpdateDesc colliderUpdate;
    colliderUpdate.collider = collider;
    colliderUpdate.updateMask =
        PHYSICS_COLLIDER_UPDATE_SHAPE | PHYSICS_COLLIDER_UPDATE_RESPONSE | PHYSICS_COLLIDER_UPDATE_BROADPHASE;
    colliderUpdate.shape = BoundingSphere( COLLIDER_SPHERE_RADIUS, localColliderOffset );
    colliderUpdate.boundingRadius = 0.5f;
    colliderUpdate.restitution = 0.25f;
    colliderUpdate.friction = 0.35f;
    colliderUpdate.contactMaterialId = 202u;
    colliderUpdate.projectedSurfaceArea = 4.0f;
    colliderUpdate.dragCoefficient = 0.55f;
    const bool updatedCollider = world.UpdateCollider( colliderUpdate );
    const PhysicsColliderView* updatedColliderView = world.Collider( collider );
    const BoundingSphere* updatedColliderSphere =
        updatedColliderView ? std::get_if<BoundingSphere>( &updatedColliderView->shape ) : nullptr;
    const bool colliderUpdateConsistent =
        updatedColliderView && updatedColliderView->body == body &&
        updatedColliderView->sceneObjectId == PhysicsSceneObjectId{ 17u } && updatedColliderSphere &&
        updatedColliderSphere->GetRadius() == COLLIDER_SPHERE_RADIUS &&
        updatedColliderSphere->GetPosition() == localColliderOffset &&
        updatedColliderView->boundingRadius == CONSERVATIVE_COLLIDER_RADIUS &&
        updatedColliderView->restitution == 0.25f && updatedColliderView->friction == 0.35f &&
        updatedColliderView->contactMaterialId == 202u && updatedColliderView->projectedSurfaceArea == 4.0f &&
        updatedColliderView->dragCoefficient == 0.55f;

    PhysicsColliderCreateDesc directDestroyColliderDesc;
    directDestroyColliderDesc.body = transientBody;
    directDestroyColliderDesc.boundingRadius = 0.75f;
    const PhysicsColliderHandle directDestroyCollider = world.CreateCollider( directDestroyColliderDesc );
    const bool destroyedDirectCollider = world.DestroyCollider( directDestroyCollider );
    const bool staleDirectColliderRejected =
        world.Collider( directDestroyCollider ) == nullptr &&
        !world.UpdateCollider( PhysicsColliderUpdateDesc{ directDestroyCollider } ) &&
        !world.DestroyCollider( directDestroyCollider );

    PhysicsColliderCreateDesc transientColliderDesc;
    transientColliderDesc.body = transientBody;
    transientColliderDesc.boundingRadius = 1.0f;
    const PhysicsColliderHandle transientCollider = world.CreateCollider( transientColliderDesc );

    PhysicsPointJointCreateDesc pointJointDesc;
    pointJointDesc.bodyA = body;
    pointJointDesc.bodyB = transientBody;
    pointJointDesc.localAnchorA = Vector3( 0.0f, 0.5f, 0.0f );
    pointJointDesc.localAnchorB = Vector3( 0.0f, -0.5f, 0.0f );
    pointJointDesc.slack = 0.5f;
    pointJointDesc.stiffness = 0.4f;
    pointJointDesc.damping = 0.2f;
    pointJointDesc.groupId = 11u;
    pointJointDesc.flags = 3u;
    const PhysicsConstraintHandle constraint = world.CreatePointJoint( pointJointDesc );

    PhysicsPointJointUpdateDesc pointJointUpdate;
    pointJointUpdate.constraint = constraint;
    pointJointUpdate.updateMask =
        PHYSICS_POINT_JOINT_UPDATE_ANCHORS | PHYSICS_POINT_JOINT_UPDATE_SOLVER | PHYSICS_POINT_JOINT_UPDATE_GROUP;
    pointJointUpdate.localAnchorA = Vector3( 1.0f, 0.0f, 0.0f );
    pointJointUpdate.localAnchorB = Vector3( -1.0f, 0.0f, 0.0f );
    pointJointUpdate.slack = 0.75f;
    pointJointUpdate.stiffness = 0.6f;
    pointJointUpdate.damping = 0.45f;
    pointJointUpdate.groupId = 17u;
    pointJointUpdate.flags = 7u;
    const bool updatedPointJoint = world.UpdatePointJoint( pointJointUpdate );
    const PhysicsPointJointView* updatedPointJointView = world.PointJoint( constraint );
    const bool pointJointUpdateConsistent =
        updatedPointJointView && updatedPointJointView->bodyA == body &&
        updatedPointJointView->bodyB == transientBody &&
        updatedPointJointView->localAnchorA == Vector3( 1.0f, 0.0f, 0.0f ) &&
        updatedPointJointView->localAnchorB == Vector3( -1.0f, 0.0f, 0.0f ) && updatedPointJointView->slack == 0.75f &&
        updatedPointJointView->stiffness == 0.6f && updatedPointJointView->damping == 0.45f &&
        updatedPointJointView->groupId == 17u && updatedPointJointView->flags == 7u;

    PhysicsPointJointUpdateDesc invalidEndpointUpdate;
    invalidEndpointUpdate.constraint = constraint;
    invalidEndpointUpdate.updateMask = PHYSICS_POINT_JOINT_UPDATE_BODIES;
    invalidEndpointUpdate.bodyA = body;
    invalidEndpointUpdate.bodyB = body;
    const bool invalidEndpointUpdateRejected =
        !world.UpdatePointJoint( invalidEndpointUpdate ) && world.PointJoint( constraint ) &&
        world.PointJoint( constraint )->bodyA == body && world.PointJoint( constraint )->bodyB == transientBody;

    PhysicsPointJointUpdateDesc endpointUpdate;
    endpointUpdate.constraint = constraint;
    endpointUpdate.updateMask = PHYSICS_POINT_JOINT_UPDATE_BODIES;
    endpointUpdate.bodyA = body;
    endpointUpdate.bodyB = endpointBody;
    const bool updatedPointJointBodies = world.UpdatePointJoint( endpointUpdate );
    const PhysicsPointJointView* endpointUpdatedPointJointView = world.PointJoint( constraint );
    const bool pointJointEndpointUpdateConsistent =
        updatedPointJointBodies && endpointUpdatedPointJointView && endpointUpdatedPointJointView->bodyA == body &&
        endpointUpdatedPointJointView->bodyB == endpointBody &&
        endpointUpdatedPointJointView->localAnchorA == Vector3( 1.0f, 0.0f, 0.0f ) &&
        endpointUpdatedPointJointView->localAnchorB == Vector3( -1.0f, 0.0f, 0.0f ) &&
        endpointUpdatedPointJointView->groupId == 17u && endpointUpdatedPointJointView->flags == 7u;

    PhysicsPointJointCreateDesc directDestroyJointDesc;
    directDestroyJointDesc.bodyA = body;
    directDestroyJointDesc.bodyB = transientBody;
    const PhysicsConstraintHandle directDestroyJoint = world.CreatePointJoint( directDestroyJointDesc );
    const bool destroyedDirectConstraint = world.DestroyConstraint( directDestroyJoint );
    const bool staleDirectConstraintRejected =
        world.PointJoint( directDestroyJoint ) == nullptr &&
        !world.UpdatePointJoint( PhysicsPointJointUpdateDesc{ directDestroyJoint } ) &&
        !world.DestroyConstraint( directDestroyJoint );

    const bool updatedTransient = world.UpdateBody( transientUpdate );
    const PhysicsBodyView* updatedTransientBody = world.Body( transientBody );
    const bool fixedMassConsistent = updatedTransientBody &&
                                     updatedTransientBody->motionKind == PhysicsBodyMotionKind::Fixed &&
                                     updatedTransientBody->inverseMass == 0.0f;
    PhysicsActivationCommand fixedActivationCommand;
    fixedActivationCommand.body = transientBody;
    const bool fixedActivationRejected = !world.ApplyActivationCommand( fixedActivationCommand );
    const bool destroyedTransient = world.DestroyBody( transientBody );
    const bool staleHandleRejected = world.Body( transientBody ) == nullptr && !world.UpdateBody( transientUpdate ) &&
                                     !world.DestroyBody( transientBody );
    const bool childColliderStaleAfterBodyDestroy =
        world.Collider( transientCollider ) == nullptr &&
        !world.UpdateCollider( PhysicsColliderUpdateDesc{ transientCollider } ) &&
        !world.DestroyCollider( transientCollider );
    const bool movedConstraintSurvivedOldEndpointDestroy = world.PointJoint( constraint ) != nullptr;
    const bool destroyedEndpoint = world.DestroyBody( endpointBody );
    const bool connectedConstraintStaleAfterBodyDestroy =
        world.PointJoint( constraint ) == nullptr &&
        !world.UpdatePointJoint( PhysicsPointJointUpdateDesc{ constraint } ) && !world.DestroyConstraint( constraint );
    const bool staleEndpointHandleRejected =
        world.Body( endpointBody ) == nullptr && !world.DestroyBody( endpointBody );
    PhysicsColliderCreateDesc staleBodyColliderDesc;
    staleBodyColliderDesc.body = transientBody;
    const bool staleBodyColliderCreationRejected = !world.CreateCollider( staleBodyColliderDesc ).IsValid();
    PhysicsPointJointCreateDesc staleBodyPointJointDesc;
    staleBodyPointJointDesc.bodyA = body;
    staleBodyPointJointDesc.bodyB = transientBody;
    const bool staleBodyPointJointCreationRejected = !world.CreatePointJoint( staleBodyPointJointDesc ).IsValid();

    PhysicsActivationCommand invalidActivationCommand;
    const bool invalidActivationRejected = !world.ApplyActivationCommand( invalidActivationCommand );
    PhysicsActivationCommand seedActivationCommand;
    seedActivationCommand.kind = PhysicsActivationCommandKind::SeedBodyAsleep;
    seedActivationCommand.body = activationBody;
    const bool seededActivationBody = world.ApplyActivationCommand( seedActivationCommand );
    const PhysicsBodyView* seededActivationView = world.Body( activationBody );
    const bool seededActivationConsistent = seededActivationBody && seededActivationView &&
                                            seededActivationView->sleeping &&
                                            seededActivationView->linearVelocity == Vector3( 0.0f, 0.0f, 0.0f ) &&
                                            seededActivationView->angularVelocity == Vector3( 0.0f, 0.0f, 0.0f );

    PhysicsActivationCommand disableSleepCommand;
    disableSleepCommand.kind = PhysicsActivationCommandKind::SetSleepEnabled;
    disableSleepCommand.enabled = false;
    const bool disabledSleep = world.ApplyActivationCommand( disableSleepCommand ) && !world.SleepEnabled();
    const PhysicsBodyView* sleepDisabledActivationView = world.Body( activationBody );
    const bool disableSleepWokeBodies = sleepDisabledActivationView && !sleepDisabledActivationView->sleeping;
    const bool seedRejectedWhileSleepDisabled = !world.ApplyActivationCommand( seedActivationCommand ) &&
                                                world.Body( activationBody ) && !world.Body( activationBody )->sleeping;

    PhysicsActivationCommand enableSleepCommand;
    enableSleepCommand.kind = PhysicsActivationCommandKind::SetSleepEnabled;
    enableSleepCommand.enabled = true;
    const bool enabledSleep = world.ApplyActivationCommand( enableSleepCommand ) && world.SleepEnabled();
    const bool reseededActivationBody = world.ApplyActivationCommand( seedActivationCommand );
    const PhysicsBodyView* reseededActivationView = world.Body( activationBody );
    const bool reseededActivationConsistent =
        reseededActivationBody && reseededActivationView && reseededActivationView->sleeping;

    PhysicsActivationCommand wakeActivationCommand;
    wakeActivationCommand.kind = PhysicsActivationCommandKind::WakeBody;
    wakeActivationCommand.body = activationBody;
    const bool wokeActivationBody = world.ApplyActivationCommand( wakeActivationCommand );
    const PhysicsBodyView* wokeActivationView = world.Body( activationBody );
    const bool wokeActivationConsistent = wokeActivationBody && wokeActivationView && !wokeActivationView->sleeping;
    const bool destroyedActivationBody = world.DestroyBody( activationBody );
    const bool staleActivationRejected = !world.ApplyActivationCommand( wakeActivationCommand );
    const bool destroyedEditableBody = world.DestroyBody( editableBody );
    const bool staleEditableBodyRejected = world.Body( editableBody ) == nullptr && !world.UpdateBody( editableUpdate );

    auto sleepGateWorldOwner = std::make_unique<PhysicsStandaloneWorld>();
    PhysicsStandaloneWorld& sleepGateWorld = *sleepGateWorldOwner;
    PhysicsActivationCommand disableSleepGateCommand;
    disableSleepGateCommand.kind = PhysicsActivationCommandKind::SetSleepEnabled;
    disableSleepGateCommand.enabled = false;
    const bool sleepGateDisabled =
        sleepGateWorld.ApplyActivationCommand( disableSleepGateCommand ) && !sleepGateWorld.SleepEnabled();

    PhysicsBodyCreateDesc sleepGateBodyDesc;
    sleepGateBodyDesc.sceneObjectId = PhysicsSceneObjectId{ 11u };
    sleepGateBodyDesc.shape = BoundingSphere( 1.0f, Vector3( 0.0f, 0.0f, 0.0f ) );
    sleepGateBodyDesc.linearVelocity = Vector3( 1.0f, 0.0f, 0.0f );
    sleepGateBodyDesc.startsAsleep = true;
    const PhysicsBodyHandle sleepGateBody = sleepGateWorld.CreateBody( sleepGateBodyDesc );
    const PhysicsBodyView* sleepGateCreatedView = sleepGateWorld.Body( sleepGateBody );
    const bool sleepDisabledCreateAwake = sleepGateCreatedView && !sleepGateCreatedView->sleeping;

    PhysicsBodyUpdateDesc sleepGateUpdate;
    sleepGateUpdate.body = sleepGateBody;
    sleepGateUpdate.updateMask = PHYSICS_BODY_UPDATE_SLEEP_STATE;
    sleepGateUpdate.sleeping = true;
    const bool sleepDisabledUpdateAccepted = sleepGateWorld.UpdateBody( sleepGateUpdate );
    const PhysicsBodyView* sleepGateUpdatedView = sleepGateWorld.Body( sleepGateBody );
    const bool sleepDisabledUpdateStayedAwake =
        sleepDisabledUpdateAccepted && sleepGateUpdatedView && !sleepGateUpdatedView->sleeping;

    PhysicsStandaloneStepDesc sleepGateStep;
    sleepGateStep.deltaSeconds = 0.5f;
    sleepGateStep.fixedStep = true;
    const bool sleepDisabledStepSucceeded = sleepGateWorld.Step( sleepGateStep );
    const PhysicsBodyView* sleepGateSteppedView = sleepGateWorld.Body( sleepGateBody );
    const bool sleepDisabledStepIntegrated = sleepDisabledStepSucceeded && sleepGateSteppedView &&
                                             sleepGateSteppedView->position == Vector3( 0.5f, 0.0f, 0.0f );

    PhysicsBroadphaseCellQueryDesc sleepGateQuery;
    sleepGateQuery.min = Vector3( -1.0f, -1.0f, -1.0f );
    sleepGateQuery.max = Vector3( 2.0f, 1.0f, 1.0f );
    sleepGateQuery.includeSleepingBodies = false;
    const PhysicsBroadphaseQueryResultView sleepGateQueryView = sleepGateWorld.QueryBroadphaseCells( sleepGateQuery );
    const bool sleepDisabledQueryIncludesBody = sleepGateQueryView.bodyCount == 1u && sleepGateQueryView.bodies &&
                                                sleepGateQueryView.bodies[0] == sleepGateBody;
    const PhysicsIslandCollectionView sleepGateIslands = sleepGateWorld.Islands();
    const bool sleepDisabledIslandAwake =
        sleepGateIslands.islandCount == 1u && sleepGateIslands.islands && sleepGateIslands.islands[0].bodyCount == 1u &&
        sleepGateIslands.islands[0].bodies && sleepGateIslands.islands[0].bodies[0] == sleepGateBody &&
        !sleepGateIslands.islands[0].sleeping;

    auto impulseWorldOwner = std::make_unique<PhysicsStandaloneWorld>();
    PhysicsStandaloneWorld& impulseWorld = *impulseWorldOwner;
    PhysicsBodyCreateDesc impulseBodyDesc;
    impulseBodyDesc.sceneObjectId = PhysicsSceneObjectId{ 14u };
    impulseBodyDesc.mass = 4.0f;
    impulseBodyDesc.rotationalInertia = Vector3( 2.0f, 2.0f, 2.0f );
    impulseBodyDesc.startsAsleep = true;
    const PhysicsBodyHandle impulseBody = impulseWorld.CreateBody( impulseBodyDesc );
    const bool impulseApplied =
        impulseWorld.ApplyBodyImpulse( impulseBody, Vector3( 8.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) );
    PhysicsStandaloneStepDesc impulseStep;
    impulseStep.deltaSeconds = 0.5f;
    const bool impulseStepSucceeded = impulseWorld.Step( impulseStep );
    const PhysicsBodyView* impulseBodyView = impulseWorld.Body( impulseBody );
    const bool impulseCommandConsistent = impulseApplied && impulseStepSucceeded && impulseBodyView &&
                                          !impulseBodyView->sleeping &&
                                          impulseBodyView->linearVelocity == Vector3( 2.0f, 0.0f, 0.0f ) &&
                                          impulseBodyView->position == Vector3( 1.0f, 0.0f, 0.0f );
    const bool destroyedImpulseBody = impulseWorld.DestroyBody( impulseBody );
    const bool staleImpulseRejected =
        destroyedImpulseBody &&
        !impulseWorld.SetPendingBodyImpulse( impulseBody, Vector3( 1.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) ) &&
        !impulseWorld.ApplyBodyImpulse( impulseBody, Vector3( 1.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) );

    auto clearWorldOwner = std::make_unique<PhysicsStandaloneWorld>();
    PhysicsStandaloneWorld& clearWorld = *clearWorldOwner;
    PhysicsBodyCreateDesc clearBodyDesc;
    const PhysicsBodyHandle clearBodyA = clearWorld.CreateBody( clearBodyDesc );
    const PhysicsBodyHandle clearBodyB = clearWorld.CreateBody( clearBodyDesc );
    clearWorld.Clear();
    const PhysicsBodyHandle clearBodyAfterReset = clearWorld.CreateBody( clearBodyDesc );
    const bool clearReusesLowSlotWithNewGeneration =
        clearBodyA.index == 0u && clearBodyB.index == 1u && clearBodyAfterReset.index == 0u &&
        clearBodyAfterReset.generation != clearBodyA.generation && clearWorld.Body( clearBodyA ) == nullptr &&
        clearWorld.Body( clearBodyAfterReset ) != nullptr;

    const bool activationCommandsConsistent =
        invalidActivationRejected && fixedActivationRejected && seededActivationConsistent && disabledSleep &&
        disableSleepWokeBodies && seedRejectedWhileSleepDisabled && enabledSleep && reseededActivationConsistent &&
        wokeActivationConsistent && destroyedActivationBody && staleActivationRejected && sleepGateDisabled &&
        sleepDisabledCreateAwake && sleepDisabledUpdateStayedAwake && sleepDisabledStepIntegrated &&
        sleepDisabledQueryIncludesBody && sleepDisabledIslandAwake && clearReusesLowSlotWithNewGeneration;

    PhysicsStandaloneStepDesc stepDesc;
    stepDesc.deltaSeconds = 0.25f;
    stepDesc.fixedStep = true;
    stepDesc.worldLinearAcceleration = Vector3( 0.0f, -8.0f, 0.0f );

    constexpr uint32_t STEP_COUNT = 4u;
    bool stepped = true;
    for ( uint32_t i = 0; i < STEP_COUNT; ++i )
    {
        stepDesc.frameIndex = i;
        stepped = world.Step( stepDesc ) && stepped;
    }

    // Why: keep the existing lifecycle/raycast sample stable while proving that
    // standalone contact rows come from collider records, not staged model-side
    // collision state.
    auto contactWorldOwner = std::make_unique<PhysicsStandaloneWorld>();
    PhysicsStandaloneWorld& contactWorld = *contactWorldOwner;
    PhysicsBodyCreateDesc fixedContactBodyDesc;
    fixedContactBodyDesc.sceneObjectId = PhysicsSceneObjectId{ 31u };
    fixedContactBodyDesc.motionKind = PhysicsBodyMotionKind::Fixed;
    const PhysicsBodyHandle fixedContactBody = contactWorld.CreateBody( fixedContactBodyDesc );

    PhysicsBodyCreateDesc dynamicContactBodyDesc;
    dynamicContactBodyDesc.sceneObjectId = PhysicsSceneObjectId{ 32u };
    dynamicContactBodyDesc.position = Vector3( 1.5f, 0.0f, 0.0f );
    dynamicContactBodyDesc.motionKind = PhysicsBodyMotionKind::Dynamic;
    const PhysicsBodyHandle dynamicContactBody = contactWorld.CreateBody( dynamicContactBodyDesc );

    PhysicsColliderCreateDesc fixedContactColliderDesc;
    fixedContactColliderDesc.body = fixedContactBody;
    fixedContactColliderDesc.shape = BoundingSphere( 1.0f, Vector3( 0.0f, 0.0f, 0.0f ) );
    fixedContactColliderDesc.restitution = 0.6f;
    fixedContactColliderDesc.friction = 0.7f;
    fixedContactColliderDesc.contactMaterialId = 301u;
    const PhysicsColliderHandle fixedContactCollider = contactWorld.CreateCollider( fixedContactColliderDesc );

    PhysicsColliderCreateDesc dynamicContactColliderDesc;
    dynamicContactColliderDesc.body = dynamicContactBody;
    dynamicContactColliderDesc.shape = BoundingSphere( 1.0f, Vector3( 0.0f, 0.0f, 0.0f ) );
    dynamicContactColliderDesc.restitution = 0.2f;
    dynamicContactColliderDesc.friction = 0.3f;
    dynamicContactColliderDesc.contactMaterialId = 302u;
    const PhysicsColliderHandle dynamicContactCollider = contactWorld.CreateCollider( dynamicContactColliderDesc );

    PhysicsBodyCreateDesc fixedBoxContactBodyDesc;
    fixedBoxContactBodyDesc.sceneObjectId = PhysicsSceneObjectId{ 33u };
    fixedBoxContactBodyDesc.position = Vector3( 10.0f, 0.0f, 0.0f );
    fixedBoxContactBodyDesc.motionKind = PhysicsBodyMotionKind::Fixed;
    const PhysicsBodyHandle fixedBoxContactBody = contactWorld.CreateBody( fixedBoxContactBodyDesc );

    PhysicsColliderCreateDesc fixedBoxContactColliderDesc;
    fixedBoxContactColliderDesc.body = fixedBoxContactBody;
    fixedBoxContactColliderDesc.shape = BoundingBox( Vector3( 1.0f, 1.0f, 1.0f ), Vector3( 0.0f, 0.0f, 0.0f ) );
    fixedBoxContactColliderDesc.restitution = 0.4f;
    fixedBoxContactColliderDesc.friction = 0.5f;
    fixedBoxContactColliderDesc.contactMaterialId = 303u;
    const PhysicsColliderHandle fixedBoxContactCollider = contactWorld.CreateCollider( fixedBoxContactColliderDesc );

    PhysicsBodyCreateDesc dynamicSphereBoxContactBodyDesc;
    dynamicSphereBoxContactBodyDesc.sceneObjectId = PhysicsSceneObjectId{ 34u };
    dynamicSphereBoxContactBodyDesc.position = Vector3( 11.5f, 0.0f, 0.0f );
    dynamicSphereBoxContactBodyDesc.motionKind = PhysicsBodyMotionKind::Dynamic;
    const PhysicsBodyHandle dynamicSphereBoxContactBody = contactWorld.CreateBody( dynamicSphereBoxContactBodyDesc );

    PhysicsColliderCreateDesc dynamicSphereBoxContactColliderDesc;
    dynamicSphereBoxContactColliderDesc.body = dynamicSphereBoxContactBody;
    dynamicSphereBoxContactColliderDesc.shape = BoundingSphere( 1.0f, Vector3( 0.0f, 0.0f, 0.0f ) );
    dynamicSphereBoxContactColliderDesc.restitution = 0.8f;
    dynamicSphereBoxContactColliderDesc.friction = 0.9f;
    dynamicSphereBoxContactColliderDesc.contactMaterialId = 304u;
    const PhysicsColliderHandle dynamicSphereBoxContactCollider =
        contactWorld.CreateCollider( dynamicSphereBoxContactColliderDesc );

    PhysicsStandaloneStepDesc contactStepDesc;
    contactStepDesc.deltaSeconds = 0.125f;
    contactStepDesc.fixedStep = true;
    const bool contactStepSucceeded = contactWorld.Step( contactStepDesc );
    const PhysicsContactCollectionView contactView = contactWorld.Contacts();
    const PhysicsContactView* contact =
        contactView.contactCount == 2u && contactView.contacts ? &contactView.contacts[0] : nullptr;
    const PhysicsContactView* sphereBoxContact =
        contactView.contactCount == 2u && contactView.contacts ? &contactView.contacts[1] : nullptr;
    const bool contactSmokeConsistent =
        contactStepSucceeded && contact && contact->bodyA == fixedContactBody && contact->bodyB == dynamicContactBody &&
        contact->colliderA == fixedContactCollider && contact->colliderB == dynamicContactCollider &&
        contact->point == Vector3( 0.75f, 0.0f, 0.0f ) && contact->normal == Vector3( 1.0f, 0.0f, 0.0f ) &&
        contact->penetrationDepth == 0.5f && contact->normalImpulse == 0.0f && contact->restitutionA == 0.6f &&
        contact->restitutionB == 0.2f && contact->frictionA == 0.7f && contact->frictionB == 0.3f &&
        contact->contactMaterialAId == 301u && contact->contactMaterialBId == 302u && contact->featureId != 0u &&
        contact->touching && sphereBoxContact && sphereBoxContact->bodyA == fixedBoxContactBody &&
        sphereBoxContact->bodyB == dynamicSphereBoxContactBody &&
        sphereBoxContact->colliderA == fixedBoxContactCollider &&
        sphereBoxContact->colliderB == dynamicSphereBoxContactCollider &&
        sphereBoxContact->point == Vector3( 10.75f, 0.0f, 0.0f ) &&
        sphereBoxContact->normal == Vector3( 1.0f, 0.0f, 0.0f ) && sphereBoxContact->penetrationDepth == 0.5f &&
        sphereBoxContact->normalImpulse == 0.0f && sphereBoxContact->restitutionA == 0.4f &&
        sphereBoxContact->restitutionB == 0.8f && sphereBoxContact->frictionA == 0.5f &&
        sphereBoxContact->frictionB == 0.9f && sphereBoxContact->contactMaterialAId == 303u &&
        sphereBoxContact->contactMaterialBId == 304u && sphereBoxContact->featureId != 0u && sphereBoxContact->touching;

    auto islandWorldOwner = std::make_unique<PhysicsStandaloneWorld>();
    PhysicsStandaloneWorld& islandWorld = *islandWorldOwner;
    PhysicsBodyCreateDesc islandBodyADesc;
    islandBodyADesc.sceneObjectId = PhysicsSceneObjectId{ 41u };
    const PhysicsBodyHandle islandBodyA = islandWorld.CreateBody( islandBodyADesc );

    PhysicsBodyCreateDesc islandBodyBDesc;
    islandBodyBDesc.sceneObjectId = PhysicsSceneObjectId{ 42u };
    islandBodyBDesc.position = Vector3( 1.5f, 0.0f, 0.0f );
    const PhysicsBodyHandle islandBodyB = islandWorld.CreateBody( islandBodyBDesc );

    PhysicsBodyCreateDesc isolatedIslandBodyDesc;
    isolatedIslandBodyDesc.sceneObjectId = PhysicsSceneObjectId{ 43u };
    isolatedIslandBodyDesc.position = Vector3( 8.0f, 0.0f, 0.0f );
    const PhysicsBodyHandle isolatedIslandBody = islandWorld.CreateBody( isolatedIslandBodyDesc );

    PhysicsColliderCreateDesc islandColliderADesc;
    islandColliderADesc.body = islandBodyA;
    islandColliderADesc.shape = BoundingSphere( 1.0f, Vector3( 0.0f, 0.0f, 0.0f ) );
    const PhysicsColliderHandle islandColliderA = islandWorld.CreateCollider( islandColliderADesc );

    PhysicsColliderCreateDesc islandColliderBDesc;
    islandColliderBDesc.body = islandBodyB;
    islandColliderBDesc.shape = BoundingSphere( 1.0f, Vector3( 0.0f, 0.0f, 0.0f ) );
    const PhysicsColliderHandle islandColliderB = islandWorld.CreateCollider( islandColliderBDesc );

    PhysicsStandaloneStepDesc islandStepDesc;
    islandStepDesc.deltaSeconds = 0.125f;
    islandStepDesc.fixedStep = true;
    const bool islandStepSucceeded = islandWorld.Step( islandStepDesc );
    const PhysicsContactCollectionView islandContacts = islandWorld.Contacts();
    const PhysicsIslandCollectionView islandView = islandWorld.Islands();
    const bool islandSmokeConsistent =
        islandStepSucceeded && islandContacts.contactCount == 1u && islandContacts.contacts &&
        islandContacts.contacts[0].colliderA == islandColliderA &&
        islandContacts.contacts[0].colliderB == islandColliderB && islandView.islandCount == 2u && islandView.islands &&
        islandView.islands[0].bodyCount == 2u && islandView.islands[0].bodies &&
        islandView.islands[0].bodies[0] == islandBodyA && islandView.islands[0].bodies[1] == islandBodyB &&
        !islandView.islands[0].sleeping && !islandView.islands[0].supported && islandView.islands[1].bodyCount == 1u &&
        islandView.islands[1].bodies && islandView.islands[1].bodies[0] == isolatedIslandBody &&
        !islandView.islands[1].sleeping && !islandView.islands[1].supported;

    auto wakeIslandWorldOwner = std::make_unique<PhysicsStandaloneWorld>();
    PhysicsStandaloneWorld& wakeIslandWorld = *wakeIslandWorldOwner;
    const PhysicsBodyHandle wakeIslandBodyA = wakeIslandWorld.CreateBody( islandBodyADesc );
    const PhysicsBodyHandle wakeIslandBodyB = wakeIslandWorld.CreateBody( islandBodyBDesc );
    PhysicsColliderCreateDesc wakeIslandColliderADesc = islandColliderADesc;
    wakeIslandColliderADesc.body = wakeIslandBodyA;
    (void)wakeIslandWorld.CreateCollider( wakeIslandColliderADesc );
    PhysicsColliderCreateDesc wakeIslandColliderBDesc = islandColliderBDesc;
    wakeIslandColliderBDesc.body = wakeIslandBodyB;
    (void)wakeIslandWorld.CreateCollider( wakeIslandColliderBDesc );

    PhysicsActivationCommand seedWakeIslandA;
    seedWakeIslandA.kind = PhysicsActivationCommandKind::SeedBodyAsleep;
    seedWakeIslandA.body = wakeIslandBodyA;
    PhysicsActivationCommand seedWakeIslandB = seedWakeIslandA;
    seedWakeIslandB.body = wakeIslandBodyB;
    PhysicsActivationCommand wakeIslandA;
    wakeIslandA.kind = PhysicsActivationCommandKind::WakeBody;
    wakeIslandA.body = wakeIslandBodyA;
    const bool wakeIslandCommands = wakeIslandWorld.ApplyActivationCommand( seedWakeIslandA ) &&
                                    wakeIslandWorld.ApplyActivationCommand( seedWakeIslandB ) &&
                                    wakeIslandWorld.ApplyActivationCommand( wakeIslandA );
    const bool wakeIslandStepSucceeded = wakeIslandWorld.Step( islandStepDesc );
    const PhysicsIslandCollectionView wakeIslandView = wakeIslandWorld.Islands();
    const bool wakeIslandConsistent =
        wakeIslandCommands && wakeIslandStepSucceeded && wakeIslandView.islandCount == 1u && wakeIslandView.islands &&
        wakeIslandView.islands[0].bodyCount == 2u && wakeIslandView.islands[0].bodies &&
        wakeIslandView.islands[0].bodies[0] == wakeIslandBodyA &&
        wakeIslandView.islands[0].bodies[1] == wakeIslandBodyB && !wakeIslandView.islands[0].sleeping;

    auto staleIslandWorldOwner = std::make_unique<PhysicsStandaloneWorld>();
    PhysicsStandaloneWorld& staleIslandWorld = *staleIslandWorldOwner;
    const PhysicsBodyHandle liveIslandBody = staleIslandWorld.CreateBody( islandBodyADesc );
    const PhysicsBodyHandle staleIslandBody = staleIslandWorld.CreateBody( islandBodyBDesc );
    const bool destroyedStaleIslandBody = staleIslandWorld.DestroyBody( staleIslandBody );
    const bool staleIslandStepSucceeded = staleIslandWorld.Step( islandStepDesc );
    const PhysicsIslandCollectionView staleIslandView = staleIslandWorld.Islands();
    const bool staleIslandExcluded = destroyedStaleIslandBody && staleIslandStepSucceeded &&
                                     staleIslandView.islandCount == 1u && staleIslandView.islands &&
                                     staleIslandView.islands[0].bodyCount == 1u && staleIslandView.islands[0].bodies &&
                                     staleIslandView.islands[0].bodies[0] == liveIslandBody;

    // Why: these bodies have no colliders, so a merged island can only come
    // from the standalone point-joint endpoint path.
    auto constraintIslandWorldOwner = std::make_unique<PhysicsStandaloneWorld>();
    PhysicsStandaloneWorld& constraintIslandWorld = *constraintIslandWorldOwner;
    PhysicsBodyCreateDesc constraintIslandBodyADesc;
    constraintIslandBodyADesc.sceneObjectId = PhysicsSceneObjectId{ 44u };
    const PhysicsBodyHandle constraintIslandBodyA = constraintIslandWorld.CreateBody( constraintIslandBodyADesc );
    PhysicsBodyCreateDesc constraintIslandBodyBDesc;
    constraintIslandBodyBDesc.sceneObjectId = PhysicsSceneObjectId{ 45u };
    constraintIslandBodyBDesc.position = Vector3( 5.0f, 0.0f, 0.0f );
    const PhysicsBodyHandle constraintIslandBodyB = constraintIslandWorld.CreateBody( constraintIslandBodyBDesc );

    PhysicsPointJointCreateDesc constraintIslandJointDesc;
    constraintIslandJointDesc.bodyA = constraintIslandBodyA;
    constraintIslandJointDesc.bodyB = constraintIslandBodyB;
    const PhysicsConstraintHandle constraintIslandJoint =
        constraintIslandWorld.CreatePointJoint( constraintIslandJointDesc );
    const PhysicsPointJointCollectionView constraintIslandJoints = constraintIslandWorld.PointJoints();
    const bool constraintIslandJointViewConsistent =
        constraintIslandJoint.IsValid() && constraintIslandJoints.pointJointCount == 1u &&
        constraintIslandJoints.pointJoints && constraintIslandJoints.pointJoints[0].constraint == constraintIslandJoint;
    const bool constraintIslandStepSucceeded = constraintIslandWorld.Step( islandStepDesc );
    const PhysicsContactCollectionView constraintIslandContacts = constraintIslandWorld.Contacts();
    const PhysicsIslandCollectionView constraintIslandView = constraintIslandWorld.Islands();
    const bool constraintOnlyIslandBeforeDestroy =
        constraintIslandJointViewConsistent && constraintIslandStepSucceeded &&
        constraintIslandContacts.contactCount == 0u && constraintIslandView.islandCount == 1u &&
        constraintIslandView.islands && constraintIslandView.islands[0].bodyCount == 2u &&
        constraintIslandView.islands[0].bodies && constraintIslandView.islands[0].bodies[0] == constraintIslandBodyA &&
        constraintIslandView.islands[0].bodies[1] == constraintIslandBodyB;

    const bool destroyedConstraintIslandJoint = constraintIslandWorld.DestroyConstraint( constraintIslandJoint );
    const bool constraintIslandAfterDestroyStepSucceeded = constraintIslandWorld.Step( islandStepDesc );
    const PhysicsIslandCollectionView constraintIslandAfterDestroyView = constraintIslandWorld.Islands();
    const bool constraintOnlyIslandAfterDestroy =
        destroyedConstraintIslandJoint && constraintIslandAfterDestroyStepSucceeded &&
        constraintIslandAfterDestroyView.islandCount == 2u && constraintIslandAfterDestroyView.islands &&
        constraintIslandAfterDestroyView.islands[0].bodyCount == 1u &&
        constraintIslandAfterDestroyView.islands[0].bodies &&
        constraintIslandAfterDestroyView.islands[0].bodies[0] == constraintIslandBodyA &&
        constraintIslandAfterDestroyView.islands[1].bodyCount == 1u &&
        constraintIslandAfterDestroyView.islands[1].bodies &&
        constraintIslandAfterDestroyView.islands[1].bodies[0] == constraintIslandBodyB;
    const bool constraintOnlyIslandConsistent = constraintOnlyIslandBeforeDestroy && constraintOnlyIslandAfterDestroy;

    PhysicsStandaloneSmokeResult result;
    result.body = body;
    result.secondaryBody = secondaryBody;
    result.collider = collider;
    result.constraint = constraint;
    result.bodyCount = world.Bodies().bodyCount;
    result.colliderCount = world.Colliders().colliderCount;
    result.pointJointCount = world.PointJoints().pointJointCount;
    result.contactCount = contactView.contactCount;
    result.contactHash = HashContactCollection( contactView );
    result.islandCount = islandView.islandCount;
    result.islandHash = HashIslandCollection( islandView );
    result.stepCount = STEP_COUNT;
    result.activationCommandsPassed = activationCommandsConsistent;

    const PhysicsBodyView* finalBody = world.Body( body );
    if ( finalBody )
    {
        result.finalPosition = finalBody->position;
        result.finalLinearVelocity = finalBody->linearVelocity;
    }

    const PhysicsBodyView* finalSecondaryBody = world.Body( secondaryBody );
    if ( finalSecondaryBody )
    {
        result.secondaryFinalPosition = finalSecondaryBody->position;
        result.secondaryFinalLinearVelocity = finalSecondaryBody->linearVelocity;
    }
    result.secondaryBodyAdvanced = finalSecondaryBody &&
                                   result.secondaryFinalPosition == Vector3( -4.0f, 8.0f, 1.5f ) &&
                                   result.secondaryFinalLinearVelocity == Vector3( -1.0f, -7.0f, 0.5f );

    Vector3 expectedColliderCenter = Vector3( 0.0f, 0.0f, 0.0f );
    if ( finalBody )
    {
        auto expectedOrientation = finalBody->orientation;
        const RotationMatrix expectedRotation = expectedOrientation.GetOrientationMatrix();
        expectedColliderCenter = finalBody->position + expectedRotation * localColliderOffset;
    }

    PhysicsRayCastDesc rayCastDesc;
    rayCastDesc.origin = expectedColliderCenter - Vector3( 0.0f, 0.0f, CONSERVATIVE_COLLIDER_RADIUS + 2.0f );
    rayCastDesc.direction = Vector3( 0.0f, 0.0f, 1.0f );
    rayCastDesc.maxDistance = 20.0f;
    rayCastDesc.includeFixedBodies = false;
    rayCastDesc.includeSleepingBodies = false;
    const PhysicsRayCastHit rayHit = world.RayCast( rayCastDesc );
    const bool rayCastConsistent =
        finalBody && rayHit.hit && rayHit.body == body && rayHit.collider == collider &&
        rayHit.sceneObjectId == PhysicsSceneObjectId{ 17u } && rayHit.distance == 2.0f &&
        rayHit.point == expectedColliderCenter - Vector3( 0.0f, 0.0f, CONSERVATIVE_COLLIDER_RADIUS ) &&
        rayHit.normal == Vector3( 0.0f, 0.0f, -1.0f );

    PhysicsBroadphaseCellQueryDesc broadphaseDesc;
    const Vector3 broadphaseHalfExtents( 0.25f, 0.25f, 0.25f );
    broadphaseDesc.min = expectedColliderCenter - broadphaseHalfExtents;
    broadphaseDesc.max = expectedColliderCenter + broadphaseHalfExtents;
    broadphaseDesc.includeFixedBodies = false;
    broadphaseDesc.includeSleepingBodies = false;
    const SkullbonezCore::Physics::PhysicsBroadphaseQueryResultView broadphaseView =
        world.QueryBroadphaseCells( broadphaseDesc );
    const bool broadphaseQueryConsistent =
        broadphaseView.bodyCount == 1u && broadphaseView.bodies && broadphaseView.bodies[0] == body;

    result.broadphaseQueryCount = broadphaseView.bodyCount;
    result.rayCastHit = rayHit.hit;
    result.lifecycleChecksPassed =
        invalidBodyColliderRejected && invalidPointJointRejected && selfPointJointRejected && updatedCollider &&
        colliderUpdateConsistent && destroyedDirectCollider && staleDirectColliderRejected && updatedPointJoint &&
        pointJointUpdateConsistent && invalidEndpointUpdateRejected && pointJointEndpointUpdateConsistent &&
        destroyedDirectConstraint && staleDirectConstraintRejected && updatedTransient && fixedMassConsistent &&
        destroyedTransient && staleHandleRejected && childColliderStaleAfterBodyDestroy &&
        movedConstraintSurvivedOldEndpointDestroy && destroyedEndpoint && connectedConstraintStaleAfterBodyDestroy &&
        staleEndpointHandleRejected && staleBodyColliderCreationRejected && staleBodyPointJointCreationRejected &&
        poseVelocityUpdateConsistent && destroyedEditableBody && staleEditableBodyRejected &&
        impulseCommandConsistent && staleImpulseRejected && activationCommandsConsistent && rayCastConsistent &&
        broadphaseQueryConsistent && contactSmokeConsistent && islandSmokeConsistent && wakeIslandConsistent &&
        staleIslandExcluded && constraintOnlyIslandConsistent;

    result.deterministicHash = HashSmokeResult( result );
    result.passed = stepped && result.lifecycleChecksPassed && finalBody && result.secondaryBodyAdvanced &&
                    result.bodyCount == 2u && result.colliderCount == 1u && result.pointJointCount == 0u &&
                    result.contactCount == 2u && result.islandCount == 2u && result.broadphaseQueryCount == 1u &&
                    result.activationCommandsPassed && result.rayCastHit &&
                    result.finalPosition == Vector3( 3.0f, 9.0f, -2.0f ) &&
                    result.finalLinearVelocity == Vector3( 2.0f, -4.0f, 0.0f );
    return result;
}
