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

using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::PHYSICS_COMPATIBILITY_HANDLE_GENERATION;
using SkullbonezCore::Physics::PHYSICS_STANDALONE_HANDLE_INITIAL_GENERATION;
using SkullbonezCore::Physics::PhysicsBodyCreateDesc;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyMotionKind;
using SkullbonezCore::Physics::PhysicsBodyUpdateDesc;
using SkullbonezCore::Physics::PhysicsBodyView;
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
    hash = HashU32( hash, result.bodyCount );
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


bool PhysicsStandaloneWorld::IsAlive( PhysicsBodyHandle body ) const
{
    return body.IsValid() && body.index < m_bodies.size() && m_alive[body.index] != 0 &&
           m_generations[body.index] == body.generation;
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

    PhysicsBodyUpdateDesc transientUpdate;
    transientUpdate.body = transientBody;
    transientUpdate.updateMask = PHYSICS_BODY_UPDATE_MASS | PHYSICS_BODY_UPDATE_MATERIAL_RESPONSE;
    transientUpdate.mass = 4.0f;
    transientUpdate.motionKind = PhysicsBodyMotionKind::Fixed;

    const bool updatedTransient = world.UpdateBody( transientUpdate );
    const PhysicsBodyView* updatedTransientBody = world.Body( transientBody );
    const bool fixedMassConsistent = updatedTransientBody &&
                                     updatedTransientBody->motionKind == PhysicsBodyMotionKind::Fixed &&
                                     updatedTransientBody->inverseMass == 0.0f;
    const bool destroyedTransient = world.DestroyBody( transientBody );
    const bool staleHandleRejected = world.Body( transientBody ) == nullptr && !world.UpdateBody( transientUpdate ) &&
                                     !world.DestroyBody( transientBody );

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
    result.bodyCount = world.Bodies().bodyCount;
    result.stepCount = STEP_COUNT;
    result.lifecycleChecksPassed = updatedTransient && fixedMassConsistent && destroyedTransient && staleHandleRejected;

    const PhysicsBodyView* finalBody = world.Body( body );
    if ( finalBody )
    {
        result.finalPosition = finalBody->position;
        result.finalLinearVelocity = finalBody->linearVelocity;
    }

    result.deterministicHash = HashSmokeResult( result );
    result.passed = stepped && result.lifecycleChecksPassed && finalBody && result.bodyCount == 1u &&
                    result.finalPosition == Vector3( 3.0f, 9.0f, -2.0f ) &&
                    result.finalLinearVelocity == Vector3( 2.0f, -4.0f, 0.0f );
    return result;
}
