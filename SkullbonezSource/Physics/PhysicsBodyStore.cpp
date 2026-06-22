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
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;


PhysicsBodyStore::PhysicsBodyStore()
{
    m_bodies.reserve( MAX_GAME_MODELS );
}


void PhysicsBodyStore::Clear()
{
    m_bodies.clear();
}


void PhysicsBodyStore::Refresh( std::vector<GameModel>& models, const std::vector<uint8_t>& sleepStates )
{
    m_bodies.resize( models.size() );
    for ( std::size_t i = 0; i < models.size(); ++i )
    {
        GameModel& model = models[i];
        PhysicsBodyRecord& record = m_bodies[i];
        record.replayBodyId = model.GetReplayBodyId();
        record.position = model.GetPosition();
        record.linearVelocity = model.GetVelocity();
        record.angularVelocity = model.GetAngularVelocity();
        record.rotationalInertia = model.GetRotationalInertia();
        record.invRotationalInertia = model.GetInvertedRotationalInertia();
        record.mass = model.GetMass();
        record.invMass = model.GetInvertedMass();
        record.isFixed = model.IsFixed();
        record.isSleeping = i < sleepStates.size() && sleepStates[i] != 0;
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


const std::vector<PhysicsBodyRecord>& PhysicsBodyStore::Records() const
{
    return m_bodies;
}
