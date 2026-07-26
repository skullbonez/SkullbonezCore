/*
File: SkullbonezSource/Physics/ColliderStore.cpp
Purpose:
  Owns deterministic collider records from explicit create/update descriptors.

Summary:
  Per-tick collider values live in dense ColliderRecord rows. Cold scene
  round-trip text lives in an index-aligned authoring row that compacts with
  the hot row. Runtime editor/tooling code can replace both at cold authoring
  edges, while config changes update hot material scalars in-place and topology
  repair only rebases body identity and handle maps against PhysicsBodyStore.

Glossary:
  Collider: Shape metadata used to decide what precise collision test applies.
  Authoring row: Cold scene round-trip text paired with one hot collider row.
  Physics material: Runtime policy for collider friction and sphere drag.
  Narrowphase: Precise collision pass that builds contacts for candidate pairs.
  Scene object id: Stable per-scene id used when replay and diagnostics name a
    physics body across frames.

Invariants:
  - Dense collider rows stay in scene/model order for current solver traversal,
    but public collider handles are allocator-owned slots.
  - Authoring rows have exactly the same count and compaction moves as hot rows.
  - Standalone records stay dense; deleting a collider may move the final row
    and updates only the moved handle's row map.
  - Body-binding refresh preserves shape/material fields; it must not reopen
    model-owned collider authoring.

Related:
  - SkullbonezSource/Physics/ColliderStore.h
*/
#include "ColliderStore.h"
#include "PhysicsBodyStore.h"
#include "PhysicsObjectPolicy.h"

#include <cassert>
#include <cstddef>
#include <stdexcept>
#include <variant>

#include "../Core/Common.h"
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Physics::ColliderAuthoringRecord;
using SkullbonezCore::Physics::ColliderHandleAssignmentMask;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderRecordList;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::PHYSICS_HANDLE_INITIAL_GENERATION;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsColliderHandle;
using SkullbonezCore::Physics::PhysicsMaterial;

namespace
{
uint32_t NextHandleGeneration( uint32_t generation )
{
    ++generation;
    return generation == 0u ? PHYSICS_HANDLE_INITIAL_GENERATION : generation;
}
} // namespace


ColliderStore::ColliderStore() = default;


// Concept: handle slots are stable identities, not model indices.
//
// Refresh still walks scene/model order, but callers receive handles
// from this slot table. Same-slot reuse keeps ids stable across refreshes;
// retiring a slot bumps its generation so stale collider handles stop resolving.
PhysicsColliderHandle ColliderStore::ResolveHandleForModelIndex( int modelIndex, PhysicsSceneObjectId sceneObjectId,
                                                                 ColliderHandleAssignmentMask& assignedHandleSlots )
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
        m_handleSceneObjectIds[static_cast<std::size_t>( slot )] = sceneObjectId;

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
             m_handleSceneObjectIds[static_cast<std::size_t>( previous.index )] == sceneObjectId &&
             ( previous.index >= assignedHandleSlots.size() ||
               assignedHandleSlots[static_cast<std::size_t>( previous.index )] == 0 ) )
        {
            return assignSlot( previous.index );
        }
    }

    if ( sceneObjectId.IsValid() )
    {

        for ( uint32_t slot = 0; slot < static_cast<uint32_t>( m_handleSceneObjectIds.size() ); ++slot )
        {

            if ( m_handleAlive[slot] != 0 && m_handleSceneObjectIds[slot] == sceneObjectId &&
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
        m_handleSceneObjectIds.push_back( {} );
    }

    return assignSlot( slot );
}


void ColliderStore::RetireUnassignedHandles( const ColliderHandleAssignmentMask& assignedHandleSlots )
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
        m_handleSceneObjectIds[slot] = {};
        m_handleGenerations[slot] = NextHandleGeneration( m_handleGenerations[slot] );
        m_freeHandleSlots.push_back( slot );
    }
}


void ColliderStore::Clear()
{
    m_colliders.clear();
    m_authoringRecords.clear();
    m_modelColliderHandles.clear();

    for ( uint32_t slot = 0; slot < static_cast<uint32_t>( m_handleAlive.size() ); ++slot )
    {

        if ( m_handleAlive[slot] != 0 )
        {
            m_handleGenerations[slot] = NextHandleGeneration( m_handleGenerations[slot] );
        }

        m_handleAlive[slot] = 0;
        m_handleModelIndices[slot] = -1;
        m_handleSceneObjectIds[slot] = {};
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


bool ColliderStore::RefreshBodyBindings( const PhysicsBodyStore& bodyStore )
{
    const int bodyCount = bodyStore.Count();

    if ( Count() != bodyCount || static_cast<int>( m_modelColliderHandles.size() ) != bodyCount )
    {
        assert( false && "ColliderStore body-binding refresh requires existing collider rows." );
        return false;
    }

    m_assignedHandleScratch.assign( m_handleGenerations.size(), 0 );
    ColliderHandleAssignmentMask& assignedHandleSlots = m_assignedHandleScratch;

    for ( int i = 0; i < bodyCount; ++i )
    {
        ColliderRecord& record = m_colliders[static_cast<std::size_t>( i )];
        const PhysicsBodyRecord* body = bodyStore.RecordForModelIndex( i );
        assert( body != nullptr );

        // Invariant: topology repair updates identity only. Shape, material,
        // and broadphase fields stay in this dense row until an explicit
        // authoring/config command replaces the row. Missing body rows mean the
        // owner must repair PhysicsBodyStore before asking ColliderStore to bind.

        if ( !body )
        {
            return false;
        }

        record.body = body->handle;
        record.sceneObjectId = body->sceneObjectId;
        const PhysicsSceneObjectId sceneObjectId = record.sceneObjectId;
        record.handle = ResolveHandleForModelIndex( i, sceneObjectId, assignedHandleSlots );
        m_modelColliderHandles[static_cast<std::size_t>( i )] = record.handle;
    }

    RetireUnassignedHandles( assignedHandleSlots );
    return true;
}


PhysicsColliderHandle ColliderStore::CreateColliderRecord( const ColliderRecord& initialRecord )
{
    return CreateColliderRecord( initialRecord, ColliderAuthoringRecord {} );
}


PhysicsColliderHandle ColliderStore::CreateColliderRecord( const ColliderRecord& initialRecord,
                                                           const ColliderAuthoringRecord& initialAuthoringRecord )
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
        m_handleSceneObjectIds.push_back( {} );
    }

    const int recordIndex = static_cast<int>( m_colliders.size() );
    PhysicsColliderHandle handle;
    handle.index = slot;
    handle.generation = m_handleGenerations[static_cast<std::size_t>( slot )];

    ColliderRecord record = initialRecord;
    record.handle = handle;

    if ( !record.sceneObjectId.IsValid() )
    {
        record.sceneObjectId = PhysicsSceneObjectId { handle.index + 1u };
    }

    m_handleAlive[static_cast<std::size_t>( slot )] = 1;
    m_handleModelIndices[static_cast<std::size_t>( slot )] = recordIndex;
    m_handleSceneObjectIds[static_cast<std::size_t>( slot )] = record.sceneObjectId;

    // Invariant: hot and cold pushes are one fixed-capacity transaction. Both
    // lists share the same hard scene limit, so neither can grow independently.
    m_colliders.push_back( record );
    m_authoringRecords.push_back( initialAuthoringRecord );
    m_modelColliderHandles.push_back( handle );
    return handle;
}


bool ColliderStore::UpdateRecordForHandle( PhysicsColliderHandle handle, const ColliderRecord& record )
{

    if ( !Contains( handle ) )
    {
        return false;
    }

    const int recordIndex = m_handleModelIndices[static_cast<std::size_t>( handle.index )];
    return UpdateRecordForHandle( handle, record, m_authoringRecords[static_cast<std::size_t>( recordIndex )] );
}


bool ColliderStore::UpdateRecordForHandle( PhysicsColliderHandle handle, const ColliderRecord& record,
                                           const ColliderAuthoringRecord& authoringRecord )
{

    if ( !Contains( handle ) )
    {
        return false;
    }

    const int recordIndex = m_handleModelIndices[static_cast<std::size_t>( handle.index )];

    ColliderRecord updated = record;

    // Invariant: authoring edits replace collider contents, not identity. The
    // handle slot stays stable so picks, render snapshots, and stale-handle
    // rejection keep their existing contracts.
    updated.handle = handle;

    if ( !updated.sceneObjectId.IsValid() )
    {
        updated.sceneObjectId = PhysicsSceneObjectId { handle.index + 1u };
    }

    m_colliders[static_cast<std::size_t>( recordIndex )] = updated;
    m_authoringRecords[static_cast<std::size_t>( recordIndex )] = authoringRecord;
    m_handleModelIndices[static_cast<std::size_t>( handle.index )] = recordIndex;
    m_handleSceneObjectIds[static_cast<std::size_t>( handle.index )] = updated.sceneObjectId;
    return true;
}


void ColliderStore::ApplyPhysicsMaterial( const PhysicsMaterial& material )
{

    // Concept: runtime material config is scalar policy, not shape authoring.
    // Keep the existing exact shape variants in place and touch only the fields
    // consumed by contact response and fluid drag.

    for ( ColliderRecord& record : m_colliders )
    {
        record.friction = material.frictionCoefficient;

        if ( BoundingSphere* sphere = std::get_if<BoundingSphere>( &record.shape ) )
        {
            sphere->SetDragCoefficient( material.sphereDragCoefficient );
            record.dragCoefficient = material.sphereDragCoefficient;
        }
    }
}


bool ColliderStore::UpdateRecordForModelIndex( int modelIndex, const ColliderRecord& record )
{

    if ( modelIndex < 0 || modelIndex >= Count() || modelIndex >= static_cast<int>( m_modelColliderHandles.size() ) )
    {
        return false;
    }

    return UpdateRecordForHandle( m_modelColliderHandles[static_cast<std::size_t>( modelIndex )], record );
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
    // identities. Moving the final hot row also moves its cold authoring row
    // before updating the handle map; callers never infer storage position from
    // a handle index.

    if ( recordIndex != lastRecordIndex )
    {
        ColliderRecord& destination = m_colliders[static_cast<std::size_t>( recordIndex )];
        ColliderRecord& moved = m_colliders[static_cast<std::size_t>( lastRecordIndex )];
        destination = moved;
        m_authoringRecords[static_cast<std::size_t>( recordIndex )] = m_authoringRecords[static_cast<std::size_t>( lastRecordIndex )];

        m_modelColliderHandles[static_cast<std::size_t>( recordIndex )] = destination.handle;

        if ( destination.handle.IsValid() && destination.handle.index < m_handleModelIndices.size() )
        {
            m_handleModelIndices[static_cast<std::size_t>( destination.handle.index )] = recordIndex;
        }
    }

    m_colliders.pop_back();
    m_authoringRecords.pop_back();
    m_modelColliderHandles.pop_back();
    m_handleAlive[handleSlot] = 0;
    m_handleModelIndices[handleSlot] = -1;
    m_handleSceneObjectIds[handleSlot] = {};
    m_handleGenerations[handleSlot] = NextHandleGeneration( m_handleGenerations[handleSlot] );
    m_freeHandleSlots.push_back( handle.index );
    return true;
}


bool ColliderStore::TrimToCount( int colliderCount )
{

    if ( colliderCount < 0 || colliderCount > Count() )
    {
        return false;
    }

    while ( Count() > colliderCount )
    {

        if ( m_modelColliderHandles.empty() || !DestroyColliderRecord( m_modelColliderHandles.back() ) )
        {
            return false;
        }
    }

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
        return PhysicsColliderHandle {};
    }

    return m_modelColliderHandles[static_cast<std::size_t>( modelIndex )];
}


PhysicsColliderHandle ColliderStore::HandleForBodyHandle( PhysicsBodyHandle body ) const
{

    if ( !body.IsValid() )
    {
        return PhysicsColliderHandle {};
    }

    for ( const ColliderRecord& collider : m_colliders )
    {

        if ( collider.body == body )
        {
            return collider.handle;
        }
    }

    return PhysicsColliderHandle {};
}


PhysicsColliderHandle ColliderStore::HandleForSceneObjectId( PhysicsSceneObjectId sceneObjectId ) const
{

    if ( !sceneObjectId.IsValid() )
    {
        return PhysicsColliderHandle {};
    }

    for ( const ColliderRecord& collider : m_colliders )
    {

        if ( collider.sceneObjectId == sceneObjectId )
        {
            return collider.handle;
        }
    }

    return PhysicsColliderHandle {};
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


std::span<const ColliderRecord> ColliderStore::Records() const
{
    return { m_colliders.data(), m_colliders.size() };
}


std::span<ColliderRecord> ColliderStore::MutableRecords()
{
    return { m_colliders.data(), m_colliders.size() };
}


std::size_t ColliderStore::RecordCapacity() const
{
    return m_colliders.capacity();
}


std::size_t ColliderStore::AuthoringRecordCapacity() const
{
    return m_authoringRecords.capacity();
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


const ColliderAuthoringRecord* ColliderStore::AuthoringRecordForHandle( PhysicsColliderHandle handle ) const
{

    if ( !Contains( handle ) )
    {
        return nullptr;
    }

    return &m_authoringRecords[static_cast<std::size_t>( m_handleModelIndices[static_cast<std::size_t>( handle.index )] )];
}


const ColliderAuthoringRecord* ColliderStore::AuthoringRecordForModelIndex( int modelIndex ) const
{
    return AuthoringRecordForHandle( HandleForModelIndex( modelIndex ) );
}
