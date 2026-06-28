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
  Standalone world: Public physics owner that can be constructed without runtime
    or scene/game-object storage.
  Handle generation: Counter paired with a slot index so stale handles fail
    after deletion and reuse.
  Point joint: Constraint record that keeps two local body anchors associated
    without exposing legacy model indices.
  Determinism: Same fixed-step inputs produce the same final state and hash.

Invariants:
  - Standalone handles never use the compatibility generation value.
  - Step mutates only alive, dynamic, awake body records.
  - The smoke sample uses binary-exact fixed-step values so validation can check
    exact final state without tolerance drift.

Related:
  - SkullbonezSource/Physics/PhysicsApi.h
  - Agentic/Plans/carmack-physics-standalone-boundary-plan.md
*/
#include "PhysicsApi.h"

#include <cstring>
#include <variant>

using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::PHYSICS_COMPATIBILITY_HANDLE_GENERATION;
using SkullbonezCore::Physics::PHYSICS_STANDALONE_HANDLE_INITIAL_GENERATION;
using SkullbonezCore::Physics::PhysicsBodyCreateDesc;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyMotionKind;
using SkullbonezCore::Physics::PhysicsBodyUpdateDesc;
using SkullbonezCore::Physics::PhysicsBodyView;
using SkullbonezCore::Physics::PhysicsColliderCreateDesc;
using SkullbonezCore::Physics::PhysicsColliderHandle;
using SkullbonezCore::Physics::PhysicsColliderUpdateDesc;
using SkullbonezCore::Physics::PhysicsColliderView;
using SkullbonezCore::Physics::PhysicsConstraintHandle;
using SkullbonezCore::Physics::PhysicsPointJointCreateDesc;
using SkullbonezCore::Physics::PhysicsPointJointUpdateDesc;
using SkullbonezCore::Physics::PhysicsPointJointView;
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
    hash = HashU32( hash, result.stepCount );
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

uint32_t NextStandaloneInitialGeneration( uint32_t current )
{
    // Invariant: generation 1 is reserved for GameModel compatibility handles.
    // Clear() must advance away from old standalone handles without colliding
    // with that compatibility range.
    ++current;
    if ( current == 0u || current == PHYSICS_COMPATIBILITY_HANDLE_GENERATION )
    {
        return PHYSICS_STANDALONE_HANDLE_INITIAL_GENERATION;
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
    m_nextInitialGeneration = NextStandaloneInitialGeneration( m_nextInitialGeneration );
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
        body.sleeping = desc.sleeping;
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
    if ( m_generations[body.index] == 0 || m_generations[body.index] == PHYSICS_COMPATIBILITY_HANDLE_GENERATION )
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
    if ( desc.updateMask & PHYSICS_COLLIDER_UPDATE_SHAPE )
    {
        collider.shape = desc.shape;
    }
    if ( desc.updateMask & PHYSICS_COLLIDER_UPDATE_RESPONSE )
    {
        collider.restitution = desc.restitution;
        collider.friction = desc.friction;
    }
    if ( desc.updateMask & PHYSICS_COLLIDER_UPDATE_BROADPHASE )
    {
        collider.boundingRadius = desc.boundingRadius;
        collider.projectedSurfaceArea = desc.projectedSurfaceArea;
        collider.dragCoefficient = desc.dragCoefficient;
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
        if ( body.motionKind == PhysicsBodyMotionKind::Fixed || body.sleeping )
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
    view.motionKind = desc.motionKind;
    view.sleeping = desc.startsAsleep;
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
    view.boundingRadius = desc.boundingRadius;
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
    if ( m_colliderGenerations[index] == 0 || m_colliderGenerations[index] == PHYSICS_COMPATIBILITY_HANDLE_GENERATION )
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
    if ( m_constraintGenerations[index] == 0 ||
         m_constraintGenerations[index] == PHYSICS_COMPATIBILITY_HANDLE_GENERATION )
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

    PhysicsColliderUpdateDesc colliderUpdate;
    colliderUpdate.collider = collider;
    colliderUpdate.updateMask =
        PHYSICS_COLLIDER_UPDATE_SHAPE | PHYSICS_COLLIDER_UPDATE_RESPONSE | PHYSICS_COLLIDER_UPDATE_BROADPHASE;
    colliderUpdate.shape = BoundingSphere( 2.0f, Vector3( 0.0f, 1.0f, 0.0f ) );
    colliderUpdate.boundingRadius = 2.5f;
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
        updatedColliderSphere->GetRadius() == 2.0f &&
        updatedColliderSphere->GetPosition() == Vector3( 0.0f, 1.0f, 0.0f ) &&
        updatedColliderView->boundingRadius == 2.5f && updatedColliderView->restitution == 0.25f &&
        updatedColliderView->friction == 0.35f && updatedColliderView->projectedSurfaceArea == 4.0f &&
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
    result.stepCount = STEP_COUNT;
    result.lifecycleChecksPassed =
        invalidBodyColliderRejected && invalidPointJointRejected && selfPointJointRejected && updatedCollider &&
        colliderUpdateConsistent && destroyedDirectCollider && staleDirectColliderRejected && updatedPointJoint &&
        pointJointUpdateConsistent && invalidEndpointUpdateRejected && pointJointEndpointUpdateConsistent &&
        destroyedDirectConstraint && staleDirectConstraintRejected && updatedTransient && fixedMassConsistent &&
        destroyedTransient && staleHandleRejected && childColliderStaleAfterBodyDestroy &&
        movedConstraintSurvivedOldEndpointDestroy && destroyedEndpoint && connectedConstraintStaleAfterBodyDestroy &&
        staleEndpointHandleRejected && staleBodyColliderCreationRejected && staleBodyPointJointCreationRejected;

    const PhysicsBodyView* finalBody = world.Body( body );
    if ( finalBody )
    {
        result.finalPosition = finalBody->position;
        result.finalLinearVelocity = finalBody->linearVelocity;
    }

    result.deterministicHash = HashSmokeResult( result );
    result.passed = stepped && result.lifecycleChecksPassed && finalBody && result.bodyCount == 1u &&
                    result.colliderCount == 1u && result.pointJointCount == 0u &&
                    result.finalPosition == Vector3( 3.0f, 9.0f, -2.0f ) &&
                    result.finalLinearVelocity == Vector3( 2.0f, -4.0f, 0.0f );
    return result;
}
