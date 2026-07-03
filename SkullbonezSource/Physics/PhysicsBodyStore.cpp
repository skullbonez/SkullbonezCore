/*
File: SkullbonezSource/Physics/PhysicsBodyStore.cpp
Purpose:
  Owns deterministic body-order mutable physics state loaded from GameModel.

Mental model:
  LoadFromModels copies legacy construction/runtime state into the store.
  PhysicsWorld mutates records during the step, then WriteBackToModels keeps
  older render, replay, tool, terrain, and shape callers working until they move
  to store-backed views.

Glossary:
  Body: Simulated object state such as position, orientation, velocity, mass,
    and sleep flag.
  Sleep: Optimization that stops simulating stable bodies until something wakes
    them.
  Replay body id: Stable per-scene id used by replay and SkullScope traces.

Invariants:
  - Body records stay in GameModelCollection physics model order until the
    facade owns allocation and id recycling.
  - Pending impulses and sleep state are preserved across compatibility refresh
    only when the refreshed handle still names the same model slot.

Related:
  - SkullbonezSource/Physics/PhysicsBodyStore.h
*/
#include "PhysicsBodyStore.h"
#include "PhysicsModelAccess.h"

#include <cstddef>

#include "../Core/Common.h"
#include "../GameObjects/GameModel.h"

using SkullbonezCore::GameObjects::GameModel;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Math::Vector::ZERO_VECTOR;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;

namespace
{
void CaptureMutableBodyState( GameModel& model, PhysicsBodyRecord& record )
{
    record.position = model.GetPosition();
    record.orientation = model.GetOrientation();
    record.linearVelocity = model.GetVelocity();
    record.angularVelocity = model.GetAngularVelocity();
    record.rotationalInertia = model.GetRotationalInertia();
    record.invRotationalInertia = model.GetInvertedRotationalInertia();
    record.mass = model.GetMass();
    record.invMass = model.GetInvertedMass();
    record.boundingRadius = model.GetBoundingRadius();
    record.contactReleaseImpulseThreshold = model.GetContactReleaseImpulseThreshold();
    record.isFixed = model.IsFixed();
    record.usesWorldInertia = model.UsesWorldInertia();
    record.releasesFromFixedOnContact = model.ReleasesFromFixedOnContact();
}

void WriteRecordToCompatibilityModel( const PhysicsBodyRecord& record, GameModel& model )
{
    model.SetFixed( record.isFixed );
    model.SetPosition( record.position );
    model.SetOrientation( record.orientation );
    model.SetLinearVelocity( record.linearVelocity );
    model.SetAngularVelocity( record.angularVelocity );
    if ( record.hasPendingImpulse )
    {
        model.SetImpulseForce( record.pendingImpulse, record.pendingImpulseApplicationPoint );
    }
    else
    {
        model.ClearImpulseForce();
    }
}

} // namespace


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
    LoadFromModels( models, sleepStates );
}


void PhysicsBodyStore::Refresh( PhysicsModelAccess& modelAccess, const std::vector<uint8_t>& sleepStates )
{
    LoadFromModelAccess( modelAccess, sleepStates );
}


void PhysicsBodyStore::LoadFromModels( std::vector<GameModel>& models, const std::vector<uint8_t>& sleepStates )
{
    m_bodies.resize( models.size() );
    m_modelBodyHandles.resize( models.size() );
    for ( std::size_t i = 0; i < models.size(); ++i )
    {
        GameModel& model = models[i];
        PhysicsBodyRecord& record = m_bodies[i];
        const uint32_t modelIndex = static_cast<uint32_t>( i );
        const PhysicsBodyHandle handle = MakeCompatibilityPhysicsBodyHandle( modelIndex );
        const bool preservePendingImpulse = record.handle == handle && record.hasPendingImpulse;
        const bool preserveSleeping = record.handle == handle && record.isSleeping;
        // Why: refresh copies live compatibility state every frame, but a
        // pending tool impulse and sleep seed are physics-owned one-shot state.
        const Vector3 pendingImpulse = record.pendingImpulse;
        const Vector3 pendingImpulseApplicationPoint = record.pendingImpulseApplicationPoint;
        record.handle = MakeCompatibilityPhysicsBodyHandle( modelIndex );
        record.replayBodyId = model.GetReplayBodyId();
        record.sceneObjectId = MakePhysicsSceneObjectIdFromReplayBodyId( record.replayBodyId );
        CaptureMutableBodyState( model, record );
        if ( preservePendingImpulse )
        {
            record.pendingImpulse = pendingImpulse;
            record.pendingImpulseApplicationPoint = pendingImpulseApplicationPoint;
            record.hasPendingImpulse = true;
        }
        else
        {
            record.pendingImpulse = ZERO_VECTOR;
            record.pendingImpulseApplicationPoint = ZERO_VECTOR;
            record.hasPendingImpulse = false;
        }
        record.isSleeping = preserveSleeping || ( i < sleepStates.size() && sleepStates[i] != 0 );
        m_modelBodyHandles[i] = record.handle;
    }
}


void PhysicsBodyStore::LoadFromModels( PhysicsModelMutableRange models, const std::vector<uint8_t>& sleepStates )
{
    const int modelCount = models.Count();
    m_bodies.resize( static_cast<std::size_t>( modelCount ) );
    m_modelBodyHandles.resize( static_cast<std::size_t>( modelCount ) );
    for ( int i = 0; i < modelCount; ++i )
    {
        GameModel& model = models[static_cast<std::size_t>( i )];
        PhysicsBodyRecord& record = m_bodies[static_cast<std::size_t>( i )];
        const uint32_t modelIndex = static_cast<uint32_t>( i );
        const PhysicsBodyHandle handle = MakeCompatibilityPhysicsBodyHandle( modelIndex );
        const bool preservePendingImpulse = record.handle == handle && record.hasPendingImpulse;
        const bool preserveSleeping = record.handle == handle && record.isSleeping;
        const Vector3 pendingImpulse = record.pendingImpulse;
        const Vector3 pendingImpulseApplicationPoint = record.pendingImpulseApplicationPoint;
        record.handle = handle;
        record.replayBodyId = model.GetReplayBodyId();
        record.sceneObjectId = MakePhysicsSceneObjectIdFromReplayBodyId( record.replayBodyId );
        CaptureMutableBodyState( model, record );
        if ( preservePendingImpulse )
        {
            record.pendingImpulse = pendingImpulse;
            record.pendingImpulseApplicationPoint = pendingImpulseApplicationPoint;
            record.hasPendingImpulse = true;
        }
        else
        {
            record.pendingImpulse = ZERO_VECTOR;
            record.pendingImpulseApplicationPoint = ZERO_VECTOR;
            record.hasPendingImpulse = false;
        }
        record.isSleeping = preserveSleeping || ( i < static_cast<int>( sleepStates.size() ) &&
                                                  sleepStates[static_cast<std::size_t>( i )] != 0 );
        m_modelBodyHandles[static_cast<std::size_t>( i )] = record.handle;
    }
}


void PhysicsBodyStore::LoadFromModels( PhysicsModelAccess& modelAccess, const std::vector<uint8_t>& sleepStates )
{
    LoadFromModelAccess( modelAccess, sleepStates );
}


void PhysicsBodyStore::LoadFromModelAccess( PhysicsModelAccess& modelAccess, const std::vector<uint8_t>& sleepStates )
{
    LoadFromModels( modelAccess.Models(), sleepStates );
}


void PhysicsBodyStore::ClearPendingImpulses()
{
    for ( PhysicsBodyRecord& record : m_bodies )
    {
        record.pendingImpulse = ZERO_VECTOR;
        record.pendingImpulseApplicationPoint = ZERO_VECTOR;
        record.hasPendingImpulse = false;
    }
}


void PhysicsBodyStore::WriteBackToModels( std::vector<GameModel>& models ) const
{
    const int modelCount = (std::min)( static_cast<int>( models.size() ), Count() );
    for ( int i = 0; i < modelCount; ++i )
    {
        WriteBackToModelAt( models, i );
    }
}


void PhysicsBodyStore::WriteBackToModels( PhysicsModelMutableRange models ) const
{
    const int modelCount = (std::min)( models.Count(), Count() );
    for ( int i = 0; i < modelCount; ++i )
    {
        WriteBackToModelAt( models, i );
    }
}


void PhysicsBodyStore::WriteBackToModels( PhysicsModelAccess& modelAccess ) const
{
    WriteBackToModelAccess( modelAccess );
}


void PhysicsBodyStore::WriteBackToModelAt( std::vector<GameModel>& models, int modelIndex ) const
{
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( models.size() ) ||
         modelIndex >= static_cast<int>( m_bodies.size() ) )
    {
        return;
    }

    WriteRecordToCompatibilityModel( m_bodies[static_cast<std::size_t>( modelIndex )],
                                     models[static_cast<std::size_t>( modelIndex )] );
}


void PhysicsBodyStore::WriteBackToModelAt( PhysicsModelMutableRange models, int modelIndex ) const
{
    if ( modelIndex < 0 || modelIndex >= models.Count() || modelIndex >= static_cast<int>( m_bodies.size() ) )
    {
        return;
    }

    WriteRecordToCompatibilityModel( m_bodies[static_cast<std::size_t>( modelIndex )],
                                     models[static_cast<std::size_t>( modelIndex )] );
}


void PhysicsBodyStore::WriteBackToModelAt( PhysicsModelAccess& modelAccess, int modelIndex ) const
{
    WriteBackToModelAccessAt( modelAccess, modelIndex );
}


void PhysicsBodyStore::WriteBackToModelAccess( PhysicsModelAccess& modelAccess ) const
{
    WriteBackToModels( modelAccess.Models() );
}


void PhysicsBodyStore::WriteBackToModelAccessAt( PhysicsModelAccess& modelAccess, int modelIndex ) const
{
    WriteBackToModelAt( modelAccess.Models(), modelIndex );
}


void PhysicsBodyStore::CaptureMutableStateFromModelAt( std::vector<GameModel>& models, int modelIndex )
{
    PhysicsBodyRecord* record = MutableRecordForModelIndex( modelIndex );
    if ( !record || modelIndex < 0 || modelIndex >= static_cast<int>( models.size() ) )
    {
        return;
    }

    CaptureMutableBodyState( models[static_cast<std::size_t>( modelIndex )], *record );
}


void PhysicsBodyStore::CaptureMutableStateFromModelAt( PhysicsModelMutableRange models, int modelIndex )
{
    PhysicsBodyRecord* record = MutableRecordForModelIndex( modelIndex );
    if ( !record || modelIndex < 0 || modelIndex >= models.Count() )
    {
        return;
    }

    CaptureMutableBodyState( models[static_cast<std::size_t>( modelIndex )], *record );
}


void PhysicsBodyStore::CaptureMutableStateFromModelAt( PhysicsModelAccess& modelAccess, int modelIndex )
{
    CaptureMutableStateFromModelAccessAt( modelAccess, modelIndex );
}


void PhysicsBodyStore::CaptureMutableStateFromModelAccessAt( PhysicsModelAccess& modelAccess, int modelIndex )
{
    CaptureMutableStateFromModelAt( modelAccess.Models(), modelIndex );
}


void PhysicsBodyStore::CopySleepStatesFrom( const std::vector<uint8_t>& sleepStates )
{
    const int bodyCount = Count();
    for ( int i = 0; i < bodyCount; ++i )
    {
        m_bodies[static_cast<std::size_t>( i )].isSleeping =
            i < static_cast<int>( sleepStates.size() ) && sleepStates[static_cast<std::size_t>( i )] != 0;
    }
}


void PhysicsBodyStore::CopySleepStatesTo( std::vector<uint8_t>& sleepStates ) const
{
    sleepStates.resize( m_bodies.size(), 0 );
    for ( std::size_t i = 0; i < m_bodies.size(); ++i )
    {
        sleepStates[i] = m_bodies[i].isSleeping ? 1 : 0;
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

    return static_cast<int>( handle.index );
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


std::vector<PhysicsBodyRecord>& PhysicsBodyStore::MutableRecords()
{
    return m_bodies;
}


PhysicsBodyRecord* PhysicsBodyStore::MutableRecordForModelIndex( int modelIndex )
{
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( m_bodies.size() ) )
    {
        return nullptr;
    }

    return &m_bodies[static_cast<std::size_t>( modelIndex )];
}


const PhysicsBodyRecord* PhysicsBodyStore::RecordForModelIndex( int modelIndex ) const
{
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( m_bodies.size() ) )
    {
        return nullptr;
    }

    return &m_bodies[static_cast<std::size_t>( modelIndex )];
}


bool PhysicsBodyStore::WakeBody( int modelIndex )
{
    PhysicsBodyRecord* record = MutableRecordForModelIndex( modelIndex );
    if ( !record || record->isFixed )
    {
        return false;
    }

    record->isSleeping = false;
    return true;
}


bool PhysicsBodyStore::SeedBodyAsleep( int modelIndex )
{
    PhysicsBodyRecord* record = MutableRecordForModelIndex( modelIndex );
    if ( !record || record->isFixed )
    {
        return false;
    }

    record->linearVelocity = ZERO_VECTOR;
    record->angularVelocity = ZERO_VECTOR;
    record->isSleeping = true;
    return true;
}


bool PhysicsBodyStore::SetPendingBodyImpulse( int modelIndex,
                                              const Vector3& impulse,
                                              const Vector3& localApplicationPoint )
{
    PhysicsBodyRecord* record = MutableRecordForModelIndex( modelIndex );
    if ( !record )
    {
        return false;
    }

    record->pendingImpulse = impulse;
    record->pendingImpulseApplicationPoint = localApplicationPoint;
    record->hasPendingImpulse = true;
    return true;
}


bool PhysicsBodyStore::ApplyBodyImpulse( int modelIndex, const Vector3& impulse, const Vector3& localApplicationPoint )
{
    const bool pending = SetPendingBodyImpulse( modelIndex, impulse, localApplicationPoint );
    WakeBody( modelIndex );
    return pending;
}


bool PhysicsBodyStore::IntegrateBodyPose( std::vector<GameModel>& models, int modelIndex, float deltaSeconds )
{
    PhysicsBodyRecord* record = MutableRecordForModelIndex( modelIndex );
    if ( !record || modelIndex < 0 || modelIndex >= static_cast<int>( models.size() ) || record->isFixed ||
         record->isSleeping || deltaSeconds <= 0.0f )
    {
        return false;
    }

    WriteBackToModelAt( models, modelIndex );
    GameModel& model = models[static_cast<std::size_t>( modelIndex )];
    model.UpdatePosition( deltaSeconds );
    CaptureMutableBodyState( model, *record );
    return true;
}


bool PhysicsBodyStore::IntegrateBodyPose( PhysicsModelMutableRange models, int modelIndex, float deltaSeconds )
{
    PhysicsBodyRecord* record = MutableRecordForModelIndex( modelIndex );
    if ( !record || modelIndex < 0 || modelIndex >= models.Count() || record->isFixed || record->isSleeping ||
         deltaSeconds <= 0.0f )
    {
        return false;
    }

    WriteBackToModelAt( models, modelIndex );
    GameModel& model = models[static_cast<std::size_t>( modelIndex )];
    model.UpdatePosition( deltaSeconds );
    CaptureMutableBodyState( model, *record );
    return true;
}


bool PhysicsBodyStore::ApplyCompatibilityForces( std::vector<GameModel>& models, int modelIndex, float deltaSeconds )
{
    PhysicsBodyRecord* record = MutableRecordForModelIndex( modelIndex );
    if ( !record || modelIndex < 0 || modelIndex >= static_cast<int>( models.size() ) || record->isFixed )
    {
        return false;
    }

    GameModel& model = models[static_cast<std::size_t>( modelIndex )];
    // Why: command entry points and the frame-start store load have already
    // synchronized pending impulses into GameModel. Rewriting the same state for
    // every body here turns the force pass into unnecessary setter churn.
    model.ApplyForces( deltaSeconds );
    CaptureMutableBodyState( model, *record );
    record->pendingImpulse = ZERO_VECTOR;
    record->pendingImpulseApplicationPoint = ZERO_VECTOR;
    record->hasPendingImpulse = false;
    return true;
}


bool PhysicsBodyStore::ApplyCompatibilityForces( PhysicsModelMutableRange models, int modelIndex, float deltaSeconds )
{
    PhysicsBodyRecord* record = MutableRecordForModelIndex( modelIndex );
    if ( !record || modelIndex < 0 || modelIndex >= models.Count() || record->isFixed )
    {
        return false;
    }

    GameModel& model = models[static_cast<std::size_t>( modelIndex )];
    // Why: command entry points and the frame-start store load have already
    // synchronized pending impulses into GameModel. Rewriting the same state for
    // every body here turns the force pass into unnecessary setter churn.
    model.ApplyForces( deltaSeconds );
    CaptureMutableBodyState( model, *record );
    record->pendingImpulse = ZERO_VECTOR;
    record->pendingImpulseApplicationPoint = ZERO_VECTOR;
    record->hasPendingImpulse = false;
    return true;
}


bool PhysicsBodyStore::IntegrateBodyPose( PhysicsModelAccess& modelAccess, int modelIndex, float deltaSeconds )
{
    return IntegrateBodyPose( modelAccess.Models(), modelIndex, deltaSeconds );
}


bool PhysicsBodyStore::ApplyCompatibilityForces( PhysicsModelAccess& modelAccess, int modelIndex, float deltaSeconds )
{
    return ApplyCompatibilityForces( modelAccess.Models(), modelIndex, deltaSeconds );
}
