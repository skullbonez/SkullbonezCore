/*
File: SkullbonezSource/Physics/PhysicsBodyStore.cpp
Purpose:
  Builds deterministic body-order snapshots from GameModel physics state.

Mental model:
  Refresh observes the compatibility model list after simulation has committed.
  It copies only body-facing values so future physics-store migrations can be
  checked without changing solver ordering.

Glossary:
  Body: Simulated object state such as position, orientation, velocity, mass,
    and sleep flag.
  Sleep: Optimization that stops simulating stable bodies until something wakes
    them.
  Replay body id: Stable per-scene id used by replay and SkullScope traces.

Related:
  - SkullbonezSource/Physics/PhysicsBodyStore.h
*/
#include "PhysicsBodyStore.h"

#include <cstddef>

#include "../Core/Common.h"
#include "../GameObjects/GameModel.h"

using SkullbonezCore::GameObjects::GameModel;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;


PhysicsBodyStore::PhysicsBodyStore()
{
    m_bodies.reserve( MAX_GAME_MODELS );
    m_modelBodyHandles.reserve( MAX_GAME_MODELS );
}


void PhysicsBodyStore::Clear()
{
    m_bodies.clear();
    m_modelBodyHandles.clear();
}


void PhysicsBodyStore::Refresh( std::vector<GameModel>& models, const std::vector<uint8_t>& sleepStates )
{
    m_bodies.resize( models.size() );
    m_modelBodyHandles.resize( models.size() );
    for ( std::size_t i = 0; i < models.size(); ++i )
    {
        GameModel& model = models[i];
        PhysicsBodyRecord& record = m_bodies[i];
        const uint32_t modelIndex = static_cast<uint32_t>( i );
        record.handle = MakeCompatibilityPhysicsBodyHandle( modelIndex );
        record.legacyModelIndex = static_cast<int>( i );
        record.replayBodyId = model.GetReplayBodyId();
        record.sceneObjectId = MakePhysicsSceneObjectIdFromReplayBodyId( record.replayBodyId );
        record.position = model.GetPosition();
        record.linearVelocity = model.GetVelocity();
        record.angularVelocity = model.GetAngularVelocity();
        record.rotationalInertia = model.GetRotationalInertia();
        record.invRotationalInertia = model.GetInvertedRotationalInertia();
        record.mass = model.GetMass();
        record.invMass = model.GetInvertedMass();
        record.isFixed = model.IsFixed();
        record.isSleeping = i < sleepStates.size() && sleepStates[i] != 0;
        m_modelBodyHandles[i] = record.handle;
    }
}


const PhysicsBodyRecord* PhysicsBodyStore::Data() const
{
    return m_bodies.empty() ? nullptr : m_bodies.data();
}


int PhysicsBodyStore::Count() const
{
    return static_cast<int>( m_bodies.size() );
}


bool PhysicsBodyStore::Empty() const
{
    return m_bodies.empty();
}


PhysicsBodyHandle PhysicsBodyStore::HandleForModelIndex( int modelIndex ) const
{
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( m_modelBodyHandles.size() ) )
    {
        return PhysicsBodyHandle{};
    }

    return m_modelBodyHandles[static_cast<std::size_t>( modelIndex )];
}


int PhysicsBodyStore::ModelIndexForHandle( PhysicsBodyHandle handle ) const
{
    if ( !Contains( handle ) )
    {
        return -1;
    }

    return m_bodies[static_cast<std::size_t>( handle.index )].legacyModelIndex;
}


bool PhysicsBodyStore::Contains( PhysicsBodyHandle handle ) const
{
    if ( !handle.IsValid() || handle.generation != PHYSICS_COMPATIBILITY_HANDLE_GENERATION )
    {
        return false;
    }
    if ( handle.index >= m_bodies.size() )
    {
        return false;
    }

    return m_bodies[static_cast<std::size_t>( handle.index )].handle == handle;
}


const std::vector<PhysicsBodyRecord>& PhysicsBodyStore::Records() const
{
    return m_bodies;
}
