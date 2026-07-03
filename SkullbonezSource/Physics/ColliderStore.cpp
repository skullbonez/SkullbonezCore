/*
File: SkullbonezSource/Physics/ColliderStore.cpp
Purpose:
  Builds deterministic collider-order snapshots from GameModel collision state.

Mental model:
  Refresh copies the live compatibility models into a compact collider view.
  The order is intentionally the model order so solver, replay, and diagnostics
  can compare store data without remapping body ids.

Glossary:
  Collider: Shape metadata used to decide what precise collision test applies.
  Narrowphase: Precise collision pass that builds contacts for candidate pairs.
  Replay body id: Stable per-scene id used when replay and diagnostics name a
    physics body across frames.

Invariants:
  - Collider records stay in GameModelCollection model order for current solver
    traversal, but public collider handles are allocator-owned slots.
  - Refresh snapshots collision metadata only; model pose and solver state
    remain owned elsewhere.

Related:
  - SkullbonezSource/Physics/ColliderStore.h
*/
#include "ColliderStore.h"
#include "PhysicsBodyStore.h"

#include <cstddef>

#include "../Core/Common.h"
#include "../GameObjects/GameModel.h"

using SkullbonezCore::GameObjects::GameModel;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderShapeKind;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::PHYSICS_HANDLE_INITIAL_GENERATION;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsColliderHandle;

namespace
{
uint32_t NextHandleGeneration( uint32_t generation )
{
    ++generation;
    return generation == 0u ? PHYSICS_HANDLE_INITIAL_GENERATION : generation;
}
} // namespace


ColliderStore::ColliderStore()
{
    m_colliders.reserve( MAX_GAME_MODELS );
    m_modelColliderHandles.reserve( MAX_GAME_MODELS );
    m_handleGenerations.reserve( MAX_GAME_MODELS );
    m_handleAlive.reserve( MAX_GAME_MODELS );
    m_handleModelIndices.reserve( MAX_GAME_MODELS );
    m_handleReplayBodyIds.reserve( MAX_GAME_MODELS );
    m_freeHandleSlots.reserve( MAX_GAME_MODELS );
}


// Concept: handle slots are stable identities, not model indices.
//
// Refresh still walks GameModelCollection order, but callers receive handles
// from this slot table. Same-slot reuse keeps ids stable across refreshes;
// retiring a slot bumps its generation so stale collider handles stop resolving.
PhysicsColliderHandle ColliderStore::ResolveHandleForModelIndex( int modelIndex,
                                                                 uint32_t replayBodyId,
                                                                 std::vector<uint8_t>& assignedHandleSlots )
{
    auto assignSlot = [&]( uint32_t slot ) -> PhysicsColliderHandle
    {
        if ( slot >= assignedHandleSlots.size() )
        {
            assignedHandleSlots.resize( static_cast<std::size_t>( slot ) + 1u, 0 );
        }
        assignedHandleSlots[static_cast<std::size_t>( slot )] = 1;
        m_handleAlive[static_cast<std::size_t>( slot )] = 1;
        m_handleModelIndices[static_cast<std::size_t>( slot )] = modelIndex;
        m_handleReplayBodyIds[static_cast<std::size_t>( slot )] = replayBodyId;

        PhysicsColliderHandle handle;
        handle.index = slot;
        handle.generation = m_handleGenerations[static_cast<std::size_t>( slot )];
        return handle;
    };

    if ( modelIndex >= 0 && modelIndex < static_cast<int>( m_modelColliderHandles.size() ) )
    {
        const PhysicsColliderHandle previous = m_modelColliderHandles[static_cast<std::size_t>( modelIndex )];
        if ( previous.IsValid() && previous.index < m_handleGenerations.size() &&
             previous.generation == m_handleGenerations[static_cast<std::size_t>( previous.index )] &&
             m_handleAlive[static_cast<std::size_t>( previous.index )] != 0 &&
             m_handleReplayBodyIds[static_cast<std::size_t>( previous.index )] == replayBodyId &&
             ( previous.index >= assignedHandleSlots.size() ||
               assignedHandleSlots[static_cast<std::size_t>( previous.index )] == 0 ) )
        {
            return assignSlot( previous.index );
        }
    }

    if ( replayBodyId != 0 )
    {
        for ( uint32_t slot = 0; slot < static_cast<uint32_t>( m_handleReplayBodyIds.size() ); ++slot )
        {
            if ( m_handleAlive[slot] != 0 && m_handleReplayBodyIds[slot] == replayBodyId &&
                 slot < m_handleGenerations.size() &&
                 ( slot >= assignedHandleSlots.size() || assignedHandleSlots[slot] == 0 ) )
            {
                return assignSlot( slot );
            }
        }
    }

    uint32_t slot = 0;
    if ( !m_freeHandleSlots.empty() )
    {
        slot = m_freeHandleSlots.back();
        m_freeHandleSlots.pop_back();
    }
    else
    {
        slot = static_cast<uint32_t>( m_handleGenerations.size() );
        m_handleGenerations.push_back( PHYSICS_HANDLE_INITIAL_GENERATION );
        m_handleAlive.push_back( 0 );
        m_handleModelIndices.push_back( -1 );
        m_handleReplayBodyIds.push_back( 0 );
    }

    return assignSlot( slot );
}


void ColliderStore::RetireUnassignedHandles( const std::vector<uint8_t>& assignedHandleSlots )
{
    for ( uint32_t slot = 0; slot < static_cast<uint32_t>( m_handleAlive.size() ); ++slot )
    {
        if ( m_handleAlive[slot] == 0 )
        {
            continue;
        }
        if ( slot < assignedHandleSlots.size() && assignedHandleSlots[slot] != 0 )
        {
            continue;
        }

        m_handleAlive[slot] = 0;
        m_handleModelIndices[slot] = -1;
        m_handleReplayBodyIds[slot] = 0;
        m_handleGenerations[slot] = NextHandleGeneration( m_handleGenerations[slot] );
        m_freeHandleSlots.push_back( slot );
    }
}


void ColliderStore::Clear()
{
    m_colliders.clear();
    m_modelColliderHandles.clear();
    for ( uint32_t slot = 0; slot < static_cast<uint32_t>( m_handleAlive.size() ); ++slot )
    {
        if ( m_handleAlive[slot] != 0 )
        {
            m_handleGenerations[slot] = NextHandleGeneration( m_handleGenerations[slot] );
        }
        m_handleAlive[slot] = 0;
        m_handleModelIndices[slot] = -1;
        m_handleReplayBodyIds[slot] = 0;
    }
    m_freeHandleSlots.clear();
    for ( uint32_t slot = 0; slot < static_cast<uint32_t>( m_handleGenerations.size() ); ++slot )
    {
        m_freeHandleSlots.push_back( slot );
    }
}


void ColliderStore::Refresh( std::vector<GameModel>& models, const PhysicsBodyStore& bodyStore )
{
    Refresh( models.empty() ? nullptr : models.data(), static_cast<int>( models.size() ), bodyStore );
}


void ColliderStore::Refresh( GameModel* models, int modelCount, const PhysicsBodyStore& bodyStore )
{
    m_colliders.resize( static_cast<std::size_t>( modelCount ) );
    m_modelColliderHandles.resize( static_cast<std::size_t>( modelCount ) );
    std::vector<uint8_t> assignedHandleSlots( m_handleGenerations.size(), 0 );
    for ( int i = 0; i < modelCount; ++i )
    {
        GameModel& model = models[i];
        ColliderRecord& record = m_colliders[static_cast<std::size_t>( i )];
        record.handle = ResolveHandleForModelIndex( i, model.GetReplayBodyId(), assignedHandleSlots );
        record.body = bodyStore.HandleForModelIndex( i );
        record.replayBodyId = model.GetReplayBodyId();
        record.sceneObjectId = MakePhysicsSceneObjectIdFromReplayBodyId( record.replayBodyId );
        record.shape = model.GetCollisionShape();
        record.boundingRadius = model.GetBoundingRadius();
        record.restitution = model.GetCoefficientRestitution();
        record.contactMaterialId = model.GetContactMaterialId();
        record.projectedSurfaceArea = model.GetProjectedSurfaceArea();
        record.dragCoefficient = model.GetDragCoefficient();
        if ( model.IsBox() )
        {
            record.shapeKind = ColliderShapeKind::Box;
        }
        else if ( model.IsConvexHull() )
        {
            record.shapeKind = ColliderShapeKind::ConvexHull;
        }
        else
        {
            record.shapeKind = ColliderShapeKind::Sphere;
        }
        m_modelColliderHandles[static_cast<std::size_t>( i )] = record.handle;
    }
    RetireUnassignedHandles( assignedHandleSlots );
}


const ColliderRecord* ColliderStore::Data() const
{
    return m_colliders.empty() ? nullptr : m_colliders.data();
}


int ColliderStore::Count() const
{
    return static_cast<int>( m_colliders.size() );
}


bool ColliderStore::Empty() const
{
    return m_colliders.empty();
}


PhysicsColliderHandle ColliderStore::HandleForModelIndex( int modelIndex ) const
{
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( m_modelColliderHandles.size() ) )
    {
        return PhysicsColliderHandle{};
    }

    return m_modelColliderHandles[static_cast<std::size_t>( modelIndex )];
}


int ColliderStore::ModelIndexForHandle( PhysicsColliderHandle handle ) const
{
    if ( !Contains( handle ) )
    {
        return -1;
    }

    return m_handleModelIndices[static_cast<std::size_t>( handle.index )];
}


bool ColliderStore::Contains( PhysicsColliderHandle handle ) const
{
    if ( !handle.IsValid() || handle.index >= m_handleGenerations.size() )
    {
        return false;
    }
    const std::size_t slot = static_cast<std::size_t>( handle.index );
    if ( m_handleAlive[slot] == 0 || m_handleGenerations[slot] != handle.generation )
    {
        return false;
    }

    const int modelIndex = m_handleModelIndices[slot];
    return modelIndex >= 0 && modelIndex < static_cast<int>( m_colliders.size() ) &&
           m_colliders[static_cast<std::size_t>( modelIndex )].handle == handle;
}


const std::vector<ColliderRecord>& ColliderStore::Records() const
{
    return m_colliders;
}
