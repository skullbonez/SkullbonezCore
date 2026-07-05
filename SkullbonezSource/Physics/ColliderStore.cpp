/*
File: SkullbonezSource/Physics/ColliderStore.cpp
Purpose:
  Builds deterministic collider records from either compatibility models or
  standalone descriptors.

Mental model:
  Refresh copies live compatibility models in model order for the legacy solver
  boundary. Standalone creation appends dense live records directly, with
  handles mapped to rows so queries scan compact data and stale handles fail.

Glossary:
  Collider: Shape metadata used to decide what precise collision test applies.
  Narrowphase: Precise collision pass that builds contacts for candidate pairs.
  Replay body id: Stable per-scene id used when replay and diagnostics name a
    physics body across frames.

Invariants:
  - Compatibility records stay in GameModelCollection order for current solver
    traversal, but public collider handles are allocator-owned slots.
  - Standalone records stay dense; deleting a collider may move the final row
    and updates only the moved handle's row map.
  - Refresh snapshots collision metadata only; model pose and solver state
    remain owned elsewhere.

Related:
  - SkullbonezSource/Physics/ColliderStore.h
*/
#include "ColliderStore.h"
#include "PhysicsBodyStore.h"

#include <cassert>
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
    // Invariant: CreateColliderRecord pops from the back of the free list.
    // Push in reverse so a full Clear() reuses low handle indices first while
    // still advancing generations for stale-handle rejection.
    for ( uint32_t remaining = static_cast<uint32_t>( m_handleGenerations.size() ); remaining > 0; --remaining )
    {
        m_freeHandleSlots.push_back( remaining - 1u );
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
        const PhysicsBodyRecord* body = bodyStore.RecordForModelIndex( i );
        assert( body != nullptr );
        // Invariant: collider identity follows the body row imported by
        // PhysicsBodyStore. GameModel still supplies authoring shape/material
        // data here, but it must not approve replay id or body-handle identity.
        const uint32_t replayBodyId = body ? body->replayBodyId : 0u;
        record.handle = ResolveHandleForModelIndex( i, replayBodyId, assignedHandleSlots );
        record.body = body ? body->handle : PhysicsBodyHandle{};
        record.replayBodyId = replayBodyId;
        record.sceneObjectId = MakePhysicsSceneObjectIdFromReplayBodyId( record.replayBodyId );
        record.shape = model.GetCollisionShape();
        record.boundingRadius = model.GetBoundingRadius();
        record.restitution = model.GetCoefficientRestitution();
        record.friction = model.GetFrictionCoefficient();
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


PhysicsColliderHandle ColliderStore::CreateColliderRecord( const ColliderRecord& initialRecord )
{
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

    const int recordIndex = static_cast<int>( m_colliders.size() );
    PhysicsColliderHandle handle;
    handle.index = slot;
    handle.generation = m_handleGenerations[static_cast<std::size_t>( slot )];

    ColliderRecord record = initialRecord;
    record.handle = handle;
    if ( !record.sceneObjectId.IsValid() )
    {
        record.sceneObjectId = PhysicsSceneObjectId{ handle.index + 1u };
    }

    m_handleAlive[static_cast<std::size_t>( slot )] = 1;
    m_handleModelIndices[static_cast<std::size_t>( slot )] = recordIndex;
    m_handleReplayBodyIds[static_cast<std::size_t>( slot )] = record.replayBodyId;
    m_colliders.push_back( record );
    m_modelColliderHandles.push_back( handle );
    return handle;
}


bool ColliderStore::DestroyColliderRecord( PhysicsColliderHandle handle )
{
    if ( !Contains( handle ) )
    {
        return false;
    }

    const std::size_t handleSlot = static_cast<std::size_t>( handle.index );
    const int recordIndex = m_handleModelIndices[handleSlot];
    const int lastRecordIndex = Count() - 1;
    if ( recordIndex < 0 || recordIndex > lastRecordIndex )
    {
        return false;
    }

    // Invariant: collider scans stay dense while handles remain allocator
    // identities. Moving the final row updates that row's handle map; callers
    // never infer storage position from a handle index.
    if ( recordIndex != lastRecordIndex )
    {
        ColliderRecord& destination = m_colliders[static_cast<std::size_t>( recordIndex )];
        ColliderRecord& moved = m_colliders[static_cast<std::size_t>( lastRecordIndex )];
        destination = moved;
        m_modelColliderHandles[static_cast<std::size_t>( recordIndex )] = destination.handle;
        if ( destination.handle.IsValid() && destination.handle.index < m_handleModelIndices.size() )
        {
            m_handleModelIndices[static_cast<std::size_t>( destination.handle.index )] = recordIndex;
        }
    }

    m_colliders.pop_back();
    m_modelColliderHandles.pop_back();
    m_handleAlive[handleSlot] = 0;
    m_handleModelIndices[handleSlot] = -1;
    m_handleReplayBodyIds[handleSlot] = 0;
    m_handleGenerations[handleSlot] = NextHandleGeneration( m_handleGenerations[handleSlot] );
    m_freeHandleSlots.push_back( handle.index );
    return true;
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

    const int recordIndex = m_handleModelIndices[slot];
    return recordIndex >= 0 && recordIndex < static_cast<int>( m_colliders.size() ) &&
           m_colliders[static_cast<std::size_t>( recordIndex )].handle == handle;
}


const std::vector<ColliderRecord>& ColliderStore::Records() const
{
    return m_colliders;
}


std::vector<ColliderRecord>& ColliderStore::MutableRecords()
{
    return m_colliders;
}


ColliderRecord* ColliderStore::MutableRecordForHandle( PhysicsColliderHandle handle )
{
    if ( !Contains( handle ) )
    {
        return nullptr;
    }

    return &m_colliders[static_cast<std::size_t>( m_handleModelIndices[static_cast<std::size_t>( handle.index )] )];
}


const ColliderRecord* ColliderStore::RecordForHandle( PhysicsColliderHandle handle ) const
{
    if ( !Contains( handle ) )
    {
        return nullptr;
    }

    return &m_colliders[static_cast<std::size_t>( m_handleModelIndices[static_cast<std::size_t>( handle.index )] )];
}
