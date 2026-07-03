/*
File: SkullbonezSource/Physics/PhysicsApi.cpp
Purpose:
  Implements the model-free public physics API slice and standalone smoke sample.

Mental model:
  PhysicsStandaloneWorld is the first isolated owner for public physics handles.
  It does not route through Run, renderer setup, scene parsing, GameModel, or
  GameModelCollection. The world is intentionally small: it proves deterministic
  create/update/delete/query/step ownership before collision and solver authority
  migrate away from the compatibility path.

Glossary:
  Activation command: Handle-based request to wake a body, seed it asleep, or
    toggle the standalone world's sleep gate.
  Standalone world: Public physics owner that can be constructed without runtime
    or scene/game-object storage.
  Handle generation: Counter paired with a slot index so stale handles fail
    after deletion and reuse.
  Point joint: Constraint record that keeps two local body anchors associated
    without exposing legacy model indices.
  Ray cast: Query that reports the closest collider candidate along a directed
    segment.
  Broadphase query: Cheap AABB query that returns candidate bodies in slot order.
  Sleep gate: World policy deciding whether sleeping body flags are honored by
    steps and queries.
  Determinism: Same fixed-step inputs produce the same final state and hash.

Invariants:
  - Standalone handles pair a slot index with a nonzero generation value.
  - Step mutates only alive, dynamic, awake body records.
  - The smoke sample uses binary-exact fixed-step values so validation can check
    exact final state without tolerance drift.

Related:
  - SkullbonezSource/Physics/PhysicsApi.h
  - Agentic/Plans/carmack-physics-standalone-boundary-plan.md
*/
#include "PhysicsApi.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <variant>

using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::GetShapeBoundingRadius;
using SkullbonezCore::Math::CollisionDetection::GetShapePosition;
using SkullbonezCore::Math::Transformation::RotationMatrix;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Math::Vector::VectorMag;
using SkullbonezCore::Physics::PHYSICS_HANDLE_INITIAL_GENERATION;
using SkullbonezCore::Physics::PhysicsActivationCommand;
using SkullbonezCore::Physics::PhysicsActivationCommandKind;
using SkullbonezCore::Physics::PhysicsBodyCreateDesc;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyMotionKind;
using SkullbonezCore::Physics::PhysicsBodyUpdateDesc;
using SkullbonezCore::Physics::PhysicsBodyView;
using SkullbonezCore::Physics::PhysicsBroadphaseCellQueryDesc;
using SkullbonezCore::Physics::PhysicsBroadphaseQueryResultView;
using SkullbonezCore::Physics::PhysicsColliderCreateDesc;
using SkullbonezCore::Physics::PhysicsColliderHandle;
using SkullbonezCore::Physics::PhysicsColliderUpdateDesc;
using SkullbonezCore::Physics::PhysicsColliderView;
using SkullbonezCore::Physics::PhysicsConstraintHandle;
using SkullbonezCore::Physics::PhysicsPointJointCreateDesc;
using SkullbonezCore::Physics::PhysicsPointJointUpdateDesc;
using SkullbonezCore::Physics::PhysicsPointJointView;
using SkullbonezCore::Physics::PhysicsRayCastDesc;
using SkullbonezCore::Physics::PhysicsRayCastHit;
using SkullbonezCore::Physics::PhysicsSceneObjectId;
using SkullbonezCore::Physics::PhysicsStandaloneSmokeResult;
using SkullbonezCore::Physics::PhysicsStandaloneWorld;
using SkullbonezCore::Physics::PhysicsStepDesc;

namespace
{
constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ull;
constexpr uint64_t FNV_PRIME = 1099511628211ull;

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

uint64_t HashSmokeResult( const PhysicsStandaloneSmokeResult& result )
{
    uint64_t hash = FNV_OFFSET_BASIS;
    hash = HashU32( hash, result.body.index );
    hash = HashU32( hash, result.body.generation );
    hash = HashU32( hash, result.collider.index );
    hash = HashU32( hash, result.collider.generation );
    hash = HashU32( hash, result.constraint.index );
    hash = HashU32( hash, result.constraint.generation );
    hash = HashU32( hash, result.bodyCount );
    hash = HashU32( hash, result.colliderCount );
    hash = HashU32( hash, result.pointJointCount );
    hash = HashU32( hash, result.contactCount );
    hash = HashU32( hash, result.islandCount );
    hash = HashU32( hash, result.broadphaseQueryCount );
    hash = HashU32( hash, result.stepCount );
    hash = HashU32( hash, result.activationCommandsPassed ? 1u : 0u );
    hash = HashU32( hash, result.rayCastHit ? 1u : 0u );
    hash = HashVector( hash, result.finalPosition );
    return HashVector( hash, result.finalLinearVelocity );
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

bool BodyPassesQueryFilters( const PhysicsBodyView& body,
                             bool includeFixedBodies,
                             bool includeSleepingBodies,
                             bool sleepEnabled )
{
    if ( !includeFixedBodies && body.motionKind == PhysicsBodyMotionKind::Fixed )
    {
        return false;
    }
    return includeSleepingBodies || !sleepEnabled || !body.sleeping;
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

float EffectiveColliderRadius( const PhysicsColliderView& collider )
{
    return ConservativeBroadphaseRadius( collider.boundingRadius, collider.shape );
}

Vector3 ColliderWorldCenter( const PhysicsBodyView& body, const PhysicsColliderView& collider )
{
    // Why: local collider offsets live in body space. Rotate them through the
    // body orientation before doing any world-space query math so conservative
    // candidates match the narrowphase coordinate convention.
    auto orientation = body.orientation;
    const RotationMatrix rotation = orientation.GetOrientationMatrix();
    return body.position + rotation * GetShapePosition( collider.shape );
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
    m_bodies.clear();
    m_generations.clear();
    m_alive.clear();
    m_freeIndices.clear();
    m_bodyViewScratch.clear();
    m_colliders.clear();
    m_colliderGenerations.clear();
    m_colliderAlive.clear();
    m_freeColliderIndices.clear();
    m_colliderViewScratch.clear();
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
    uint32_t index = 0;
    if ( !m_freeIndices.empty() )
    {
        index = m_freeIndices.back();
        m_freeIndices.pop_back();
    }
    else
    {
        index = static_cast<uint32_t>( m_bodies.size() );
        m_bodies.push_back( PhysicsBodyView{} );
        m_generations.push_back( m_nextInitialGeneration );
        m_alive.push_back( 0 );
    }

    PhysicsBodyHandle handle;
    handle.index = index;
    handle.generation = m_generations[index];

    m_bodies[index] = MakeBodyView( desc, handle );
    m_alive[index] = 1;
    return handle;
}


bool PhysicsStandaloneWorld::UpdateBody( const PhysicsBodyUpdateDesc& desc )
{
    if ( !IsAlive( desc.body ) )
    {
        return false;
    }

    PhysicsBodyView& body = m_bodies[desc.body.index];
    if ( desc.updateMask & PHYSICS_BODY_UPDATE_POSE )
    {
        body.position = desc.position;
        body.orientation = desc.orientation;
    }
    if ( desc.updateMask & PHYSICS_BODY_UPDATE_VELOCITY )
    {
        body.linearVelocity = desc.linearVelocity;
        body.angularVelocity = desc.angularVelocity;
    }
    const bool updatesMotionKind = ( desc.updateMask & PHYSICS_BODY_UPDATE_MATERIAL_RESPONSE ) != 0;
    const bool updatesMass = ( desc.updateMask & PHYSICS_BODY_UPDATE_MASS ) != 0;
    if ( updatesMotionKind )
    {
        body.motionKind = desc.motionKind;
    }
    if ( updatesMass )
    {
        body.mass = desc.mass;
        body.inverseMass = ComputeInverseMass( body.motionKind, body.mass );
        body.rotationalInertia = desc.rotationalInertia;
        body.inverseRotationalInertia = InvertNonZeroComponents( desc.rotationalInertia );
    }
    else if ( updatesMotionKind )
    {
        body.inverseMass = ComputeInverseMass( body.motionKind, body.mass );
    }
    if ( desc.updateMask & PHYSICS_BODY_UPDATE_SLEEP_STATE )
    {
        body.sleeping = m_sleepEnabled && desc.sleeping;
    }
    return true;
}


bool PhysicsStandaloneWorld::DestroyBody( PhysicsBodyHandle body )
{
    if ( !IsAlive( body ) )
    {
        return false;
    }

    m_alive[body.index] = 0;
    ++m_generations[body.index];
    if ( m_generations[body.index] == 0 )
    {
        m_generations[body.index] = m_nextInitialGeneration;
    }
    m_freeIndices.push_back( body.index );

    // Invariant: body lifetime owns child collider and connected constraint
    // validity. Standalone callers should see dependent handles fail
    // immediately after their body is deleted.
    for ( std::size_t i = 0; i < m_colliders.size(); ++i )
    {
        if ( m_colliderAlive[i] && m_colliders[i].body == body )
        {
            TombstoneColliderSlot( static_cast<uint32_t>( i ) );
        }
    }
    for ( std::size_t i = 0; i < m_pointJoints.size(); ++i )
    {
        if ( m_constraintAlive[i] && ( m_pointJoints[i].bodyA == body || m_pointJoints[i].bodyB == body ) )
        {
            TombstoneConstraintSlot( static_cast<uint32_t>( i ) );
        }
    }
    return true;
}


PhysicsColliderHandle PhysicsStandaloneWorld::CreateCollider( const PhysicsColliderCreateDesc& desc )
{
    const PhysicsBodyView* body = Body( desc.body );
    if ( !body )
    {
        return PhysicsColliderHandle{};
    }

    uint32_t index = 0;
    if ( !m_freeColliderIndices.empty() )
    {
        index = m_freeColliderIndices.back();
        m_freeColliderIndices.pop_back();
    }
    else
    {
        index = static_cast<uint32_t>( m_colliders.size() );
        m_colliders.push_back( PhysicsColliderView{} );
        m_colliderGenerations.push_back( m_nextInitialGeneration );
        m_colliderAlive.push_back( 0 );
    }

    PhysicsColliderHandle handle;
    handle.index = index;
    handle.generation = m_colliderGenerations[index];

    m_colliders[index] = MakeColliderView( desc, handle );
    m_colliderAlive[index] = 1;
    return handle;
}


bool PhysicsStandaloneWorld::UpdateCollider( const PhysicsColliderUpdateDesc& desc )
{
    if ( !IsAlive( desc.collider ) )
    {
        return false;
    }

    PhysicsColliderView& collider = m_colliders[desc.collider.index];
    const bool updatesShape = ( desc.updateMask & PHYSICS_COLLIDER_UPDATE_SHAPE ) != 0;
    const bool updatesBroadphase = ( desc.updateMask & PHYSICS_COLLIDER_UPDATE_BROADPHASE ) != 0;
    if ( updatesShape )
    {
        collider.shape = desc.shape;
    }
    if ( desc.updateMask & PHYSICS_COLLIDER_UPDATE_RESPONSE )
    {
        collider.restitution = desc.restitution;
        collider.friction = desc.friction;
    }
    if ( updatesBroadphase )
    {
        collider.boundingRadius = desc.boundingRadius;
        collider.projectedSurfaceArea = desc.projectedSurfaceArea;
        collider.dragCoefficient = desc.dragCoefficient;
    }
    if ( updatesShape || updatesBroadphase )
    {
        collider.boundingRadius = ConservativeBroadphaseRadius( collider.boundingRadius, collider.shape );
    }
    return true;
}


bool PhysicsStandaloneWorld::DestroyCollider( PhysicsColliderHandle collider )
{
    if ( !IsAlive( collider ) )
    {
        return false;
    }

    TombstoneColliderSlot( collider.index );
    return true;
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
    return true;
}


bool PhysicsStandaloneWorld::DestroyConstraint( PhysicsConstraintHandle constraint )
{
    if ( !IsAlive( constraint ) )
    {
        return false;
    }

    TombstoneConstraintSlot( constraint.index );
    return true;
}


bool PhysicsStandaloneWorld::Step( const PhysicsStepDesc& desc )
{
    if ( desc.deltaSeconds < 0.0f )
    {
        return false;
    }
    if ( !desc.scenePhysicsEnabled || desc.deltaSeconds == 0.0f )
    {
        return true;
    }

    for ( std::size_t i = 0; i < m_bodies.size(); ++i )
    {
        if ( !m_alive[i] )
        {
            continue;
        }

        PhysicsBodyView& body = m_bodies[i];
        if ( body.motionKind == PhysicsBodyMotionKind::Fixed || ( m_sleepEnabled && body.sleeping ) )
        {
            continue;
        }

        // Invariant: use the same semi-implicit Euler order every step. Swapping
        // velocity/position integration changes the standalone smoke hash.
        body.linearVelocity += desc.worldLinearAcceleration * desc.deltaSeconds;
        body.position += body.linearVelocity * desc.deltaSeconds;
    }
    return true;
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
            for ( std::size_t i = 0; i < m_bodies.size(); ++i )
            {
                if ( m_alive[i] )
                {
                    m_bodies[i].sleeping = false;
                }
            }
        }
        return true;
    }

    if ( !IsAlive( command.body ) )
    {
        return false;
    }

    PhysicsBodyView& body = m_bodies[command.body.index];
    if ( body.motionKind == PhysicsBodyMotionKind::Fixed )
    {
        return false;
    }

    switch ( command.kind )
    {
    case PhysicsActivationCommandKind::WakeBody:
        body.sleeping = false;
        return true;
    case PhysicsActivationCommandKind::SeedBodyAsleep:
        if ( !m_sleepEnabled )
        {
            return false;
        }
        body.linearVelocity = Vector3( 0.0f, 0.0f, 0.0f );
        body.angularVelocity = Vector3( 0.0f, 0.0f, 0.0f );
        body.sleeping = true;
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

    // Concept: standalone ray casts use the same conservative broadphase
    // envelope as runtime tool rays. Exact shape-specific picks can replace
    // this later without changing the public handle-based query contract.
    for ( std::size_t i = 0; i < m_colliders.size(); ++i )
    {
        if ( !m_colliderAlive[i] )
        {
            continue;
        }

        const PhysicsColliderView& collider = m_colliders[i];
        const PhysicsBodyView* body = Body( collider.body );
        if ( !body ||
             !BodyPassesQueryFilters( *body, desc.includeFixedBodies, desc.includeSleepingBodies, m_sleepEnabled ) )
        {
            continue;
        }

        float distance = 0.0f;
        const float radius = EffectiveColliderRadius( collider );
        const Vector3 center = ColliderWorldCenter( *body, collider );
        if ( !IntersectRaySphere( desc.origin, direction, center, radius, distance ) || distance > desc.maxDistance ||
             distance >= closestDistance )
        {
            continue;
        }

        closestDistance = distance;
        closestHit.body = body->body;
        closestHit.collider = collider.collider;
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

    for ( std::size_t bodyIndex = 0; bodyIndex < m_bodies.size(); ++bodyIndex )
    {
        if ( !m_alive[bodyIndex] || !BodyPassesQueryFilters( m_bodies[bodyIndex],
                                                             desc.includeFixedBodies,
                                                             desc.includeSleepingBodies,
                                                             m_sleepEnabled ) )
        {
            continue;
        }

        const PhysicsBodyView& body = m_bodies[bodyIndex];
        bool overlaps =
            body.boundingRadius > 0.0f && SphereOverlapsAabb( body.position, body.boundingRadius, desc.min, desc.max );
        for ( std::size_t colliderIndex = 0; !overlaps && colliderIndex < m_colliders.size(); ++colliderIndex )
        {
            const PhysicsColliderView& collider = m_colliders[colliderIndex];
            if ( m_colliderAlive[colliderIndex] && collider.body == body.body )
            {
                overlaps = SphereOverlapsAabb( ColliderWorldCenter( body, collider ),
                                               EffectiveColliderRadius( collider ),
                                               desc.min,
                                               desc.max );
            }
        }

        if ( overlaps )
        {
            m_broadphaseQueryScratch.push_back( body.body );
        }
    }

    SkullbonezCore::Physics::PhysicsBroadphaseQueryResultView view;
    view.bodies = m_broadphaseQueryScratch.empty() ? nullptr : m_broadphaseQueryScratch.data();
    view.bodyCount = static_cast<uint32_t>( m_broadphaseQueryScratch.size() );
    return view;
}


const PhysicsBodyView* PhysicsStandaloneWorld::Body( PhysicsBodyHandle body ) const
{
    return IsAlive( body ) ? &m_bodies[body.index] : nullptr;
}


SkullbonezCore::Physics::PhysicsBodyCollectionView PhysicsStandaloneWorld::Bodies() const
{
    m_bodyViewScratch.clear();
    for ( std::size_t i = 0; i < m_bodies.size(); ++i )
    {
        if ( m_alive[i] )
        {
            m_bodyViewScratch.push_back( m_bodies[i] );
        }
    }

    SkullbonezCore::Physics::PhysicsBodyCollectionView view;
    view.bodies = m_bodyViewScratch.empty() ? nullptr : m_bodyViewScratch.data();
    view.bodyCount = static_cast<uint32_t>( m_bodyViewScratch.size() );
    return view;
}


const PhysicsColliderView* PhysicsStandaloneWorld::Collider( PhysicsColliderHandle collider ) const
{
    return IsAlive( collider ) ? &m_colliders[collider.index] : nullptr;
}


SkullbonezCore::Physics::PhysicsColliderCollectionView PhysicsStandaloneWorld::Colliders() const
{
    m_colliderViewScratch.clear();
    for ( std::size_t i = 0; i < m_colliders.size(); ++i )
    {
        if ( m_colliderAlive[i] )
        {
            m_colliderViewScratch.push_back( m_colliders[i] );
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
    // Concept: standalone collision generation has not migrated behind
    // PhysicsStandaloneWorld yet. Returning an empty immutable view gives
    // diagnostics/replay callers the public shape without exposing legacy
    // solver containers or GameModelCollection state.
    return SkullbonezCore::Physics::PhysicsContactCollectionView{};
}


SkullbonezCore::Physics::PhysicsIslandCollectionView PhysicsStandaloneWorld::Islands() const
{
    // Concept: sleep island authority still lives in the legacy step path. The
    // standalone API publishes a stable empty view now so future store-owned
    // islands can fill it without changing callers again.
    return SkullbonezCore::Physics::PhysicsIslandCollectionView{};
}


bool PhysicsStandaloneWorld::IsAlive( PhysicsBodyHandle body ) const
{
    return body.IsValid() && body.index < m_bodies.size() && m_alive[body.index] != 0 &&
           m_generations[body.index] == body.generation;
}


bool PhysicsStandaloneWorld::IsAlive( PhysicsColliderHandle collider ) const
{
    return collider.IsValid() && collider.index < m_colliders.size() && m_colliderAlive[collider.index] != 0 &&
           m_colliderGenerations[collider.index] == collider.generation && IsAlive( m_colliders[collider.index].body );
}


bool PhysicsStandaloneWorld::IsAlive( PhysicsConstraintHandle constraint ) const
{
    return constraint.IsValid() && constraint.index < m_pointJoints.size() &&
           m_constraintAlive[constraint.index] != 0 &&
           m_constraintGenerations[constraint.index] == constraint.generation &&
           IsAlive( m_pointJoints[constraint.index].bodyA ) && IsAlive( m_pointJoints[constraint.index].bodyB );
}


PhysicsBodyView PhysicsStandaloneWorld::MakeBodyView( const PhysicsBodyCreateDesc& desc, PhysicsBodyHandle body ) const
{
    PhysicsBodyView view;
    view.body = body;
    view.sceneObjectId = desc.sceneObjectId.IsValid() ? desc.sceneObjectId : PhysicsSceneObjectId{ body.index + 1u };
    view.position = desc.position;
    view.orientation = desc.orientation;
    view.linearVelocity = desc.linearVelocity;
    view.angularVelocity = desc.angularVelocity;
    view.rotationalInertia = desc.rotationalInertia;
    view.inverseRotationalInertia = InvertNonZeroComponents( desc.rotationalInertia );
    view.mass = desc.mass;
    view.inverseMass = ComputeInverseMass( desc.motionKind, desc.mass );
    view.boundingRadius = ConservativeShapeRadius( desc.shape );
    view.motionKind = desc.motionKind;
    view.sleeping = m_sleepEnabled && desc.startsAsleep;
    return view;
}


PhysicsColliderView PhysicsStandaloneWorld::MakeColliderView( const PhysicsColliderCreateDesc& desc,
                                                              PhysicsColliderHandle collider ) const
{
    const PhysicsBodyView* body = Body( desc.body );

    PhysicsColliderView view;
    view.collider = collider;
    view.body = desc.body;
    view.sceneObjectId =
        desc.sceneObjectId.IsValid() ? desc.sceneObjectId : ( body ? body->sceneObjectId : PhysicsSceneObjectId{} );
    view.shape = desc.shape;
    view.boundingRadius = ConservativeBroadphaseRadius( desc.boundingRadius, desc.shape );
    view.restitution = desc.restitution;
    view.friction = desc.friction;
    view.projectedSurfaceArea = desc.projectedSurfaceArea;
    view.dragCoefficient = desc.dragCoefficient;
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


void PhysicsStandaloneWorld::TombstoneColliderSlot( uint32_t index )
{
    if ( index >= m_colliders.size() || !m_colliderAlive[index] )
    {
        return;
    }

    m_colliderAlive[index] = 0;
    ++m_colliderGenerations[index];
    if ( m_colliderGenerations[index] == 0 )
    {
        m_colliderGenerations[index] = m_nextInitialGeneration;
    }
    m_freeColliderIndices.push_back( index );
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
    PhysicsStandaloneWorld world;

    PhysicsBodyCreateDesc bodyDesc;
    bodyDesc.sceneObjectId = PhysicsSceneObjectId{ 7u };
    bodyDesc.position = Vector3( 1.0f, 10.0f, -2.0f );
    bodyDesc.linearVelocity = Vector3( 2.0f, 4.0f, 0.0f );
    bodyDesc.mass = 2.0f;
    bodyDesc.motionKind = PhysicsBodyMotionKind::Dynamic;
    bodyDesc.orientation.RotateAboutAxis( Vector3( 1.0f, 0.0f, 0.0f ), _HALF_PI );

    const PhysicsBodyHandle body = world.CreateBody( bodyDesc );

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

    const bool invalidBodyColliderRejected = !world.CreateCollider( PhysicsColliderCreateDesc{} ).IsValid();
    const bool invalidPointJointRejected = !world.CreatePointJoint( PhysicsPointJointCreateDesc{} ).IsValid();
    PhysicsPointJointCreateDesc selfPointJointDesc;
    selfPointJointDesc.bodyA = body;
    selfPointJointDesc.bodyB = body;
    const bool selfPointJointRejected = !world.CreatePointJoint( selfPointJointDesc ).IsValid();

    PhysicsBodyUpdateDesc transientUpdate;
    transientUpdate.body = transientBody;
    transientUpdate.updateMask = PHYSICS_BODY_UPDATE_MASS | PHYSICS_BODY_UPDATE_MATERIAL_RESPONSE;
    transientUpdate.mass = 4.0f;
    transientUpdate.motionKind = PhysicsBodyMotionKind::Fixed;

    PhysicsColliderCreateDesc colliderDesc;
    colliderDesc.body = body;
    colliderDesc.sceneObjectId = PhysicsSceneObjectId{ 17u };
    colliderDesc.boundingRadius = 1.5f;
    colliderDesc.restitution = 0.1f;
    colliderDesc.friction = 0.2f;
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
        updatedColliderView->projectedSurfaceArea == 4.0f && updatedColliderView->dragCoefficient == 0.55f;

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

    PhysicsStandaloneWorld sleepGateWorld;
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

    PhysicsStepDesc sleepGateStep;
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

    const bool activationCommandsConsistent =
        invalidActivationRejected && fixedActivationRejected && seededActivationConsistent && disabledSleep &&
        disableSleepWokeBodies && seedRejectedWhileSleepDisabled && enabledSleep && reseededActivationConsistent &&
        wokeActivationConsistent && destroyedActivationBody && staleActivationRejected && sleepGateDisabled &&
        sleepDisabledCreateAwake && sleepDisabledUpdateStayedAwake && sleepDisabledStepIntegrated &&
        sleepDisabledQueryIncludesBody;

    PhysicsStepDesc stepDesc;
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

    PhysicsStandaloneSmokeResult result;
    result.body = body;
    result.collider = collider;
    result.constraint = constraint;
    result.bodyCount = world.Bodies().bodyCount;
    result.colliderCount = world.Colliders().colliderCount;
    result.pointJointCount = world.PointJoints().pointJointCount;
    result.contactCount = world.Contacts().contactCount;
    result.islandCount = world.Islands().islandCount;
    result.stepCount = STEP_COUNT;
    result.activationCommandsPassed = activationCommandsConsistent;

    const PhysicsBodyView* finalBody = world.Body( body );
    if ( finalBody )
    {
        result.finalPosition = finalBody->position;
        result.finalLinearVelocity = finalBody->linearVelocity;
    }

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
        activationCommandsConsistent && rayCastConsistent && broadphaseQueryConsistent;

    result.deterministicHash = HashSmokeResult( result );
    result.passed = stepped && result.lifecycleChecksPassed && finalBody && result.bodyCount == 1u &&
                    result.colliderCount == 1u && result.pointJointCount == 0u && result.contactCount == 0u &&
                    result.islandCount == 0u && result.broadphaseQueryCount == 1u && result.activationCommandsPassed &&
                    result.rayCastHit && result.finalPosition == Vector3( 3.0f, 9.0f, -2.0f ) &&
                    result.finalLinearVelocity == Vector3( 2.0f, -4.0f, 0.0f );
    return result;
}
