/*
File: SkullbonezSource/Runtime/Scene/SceneEntityStore.cpp
Purpose:
  Implements allocation-free scene entity metadata storage.

Mental model:
  Creation first asks whether an entity can be appended, commits physics owner
  rows, then publishes the fully linked entity record. The fixed array makes
  capacity checks honest and prevents steady-runtime growth.

Glossary:
  Lane R: Recoverable result for invalid authored input or capacity exhaustion.
  Stable identity: Nonzero PhysicsSceneObjectId that survives dense-row movement.

Invariants:
  - Preflight is mutation-free.
  - Commit receives the exact descriptor that passed preflight.
  - Vector capacity is established before population and cannot grow in commit.
  - Invalid direct indexing is an engine invariant failure.

Related:
  - SkullbonezSource/Runtime/Scene/SceneEntityStore.h
  - SkullbonezSource/Runtime/Scene/SceneController.cpp
*/
#include "SceneEntityStore.h"

#include "../../Core/FatalError.h"

#include <algorithm>
#include <cstring>

using namespace SkullbonezCore::Basics;
namespace Rendering = SkullbonezCore::Rendering;

namespace
{
void CopyBounded( char* target, std::size_t capacity, const char* source )
{
    strncpy_s( target, capacity, source ? source : "", _TRUNCATE );
}
} // namespace

SceneEntityCreateDesc::SceneEntityCreateDesc()
{
    renderMaterial = Rendering::MakeRenderMaterialFromLegacyTint( 1.0f, 1.0f, 1.0f, 0.0f );
}

void SceneEntityCreateDesc::SetName( const char* name )
{
    CopyBounded( displayName, sizeof( displayName ), name );
}

const char* SceneEntityCreateDesc::GetName() const
{
    return displayName;
}

void SceneEntityCreateDesc::SetRenderTint( float tintR, float tintG, float tintB, float colorOverride )
{
    renderMaterial = Rendering::MakeRenderMaterialFromLegacyTint( tintR, tintG, tintB, colorOverride );
}

void SceneEntityCreateDesc::SetRenderMaterial( const Rendering::RenderMaterial& material )
{
    renderMaterial = material;
}

const Rendering::RenderMaterial& SceneEntityCreateDesc::GetRenderMaterial() const
{
    return renderMaterial;
}

void SceneEntityCreateDesc::SetAssetAffiliation( Physics::PhysicsSceneObjectId rootObjectId,
                                                 const char* libraryToken,
                                                 const char* assetName,
                                                 const char* instanceName,
                                                 const char* partName,
                                                 uint32_t partIndex )
{
    asset.rootObjectId = rootObjectId;
    CopyBounded( asset.libraryToken, sizeof( asset.libraryToken ), libraryToken );
    CopyBounded( asset.assetName, sizeof( asset.assetName ), assetName );
    CopyBounded( asset.instanceName, sizeof( asset.instanceName ), instanceName );
    CopyBounded( asset.partName, sizeof( asset.partName ), partName );
    asset.partIndex = partIndex;
    asset.isAssetBacked = true;
}

SceneEntityStore::SceneEntityStore()
{
    // Phase: startup preallocation. The default capacity is reserved before
    // scene population so steady gameplay entity commits cannot touch the heap.
    m_records.reserve( static_cast<std::size_t>( m_capacity ) );
}

void SceneEntityStore::ConfigureCapacity( int capacity )
{
    if ( capacity < 1 || capacity > MAX_GAME_MODELS || Count() > capacity )
    {
        SB_FATAL( "Scene/SceneEntityStore",
                  "Invalid scene entity capacity. requested=%d count=%d max=%d",
                  capacity,
                  Count(),
                  MAX_GAME_MODELS );
    }
    // Phase: scene-load preallocation. Growth is allowed only before the first
    // entity of the replacement scene is published; Clear deliberately retains
    // the largest prior reservation for later reloads.
    if ( m_records.capacity() < static_cast<std::size_t>( capacity ) )
    {
        m_records.reserve( static_cast<std::size_t>( capacity ) );
    }
    m_capacity = capacity;
}

void SceneEntityStore::Clear()
{
    m_records.clear();
}

SbResult SceneEntityStore::PreflightAppend( const SceneEntityCreateDesc& entity ) const
{
    if ( Count() >= m_capacity )
    {
        return SbResult::Failure( "Scene/SceneEntityStore",
                                  "Scene entity capacity exhausted. count=%d capacity=%d",
                                  Count(),
                                  m_capacity );
    }
    if ( !entity.sceneObjectId.IsValid() )
    {
        return SbResult::Failure( "Scene/SceneEntityStore", "Cannot append a scene entity with id 0." );
    }
    if ( FindBySceneObjectId( entity.sceneObjectId ) >= 0 )
    {
        return SbResult::Failure( "Scene/SceneEntityStore",
                                  "Duplicate scene entity id %u.",
                                  entity.sceneObjectId.value );
    }
    return SbResult::Success();
}

void SceneEntityStore::CommitAppend( const SceneEntityCreateDesc& entity, Physics::PhysicsBodyHandle body )
{
    const SbResult preflight = PreflightAppend( entity );
    const bool reservationReady = m_records.size() < m_records.capacity();
    if ( !preflight.ok || !body.IsValid() || !reservationReady )
    {
        const char* reason = !preflight.ok     ? preflight.error.message
                             : !body.IsValid() ? "invalid body"
                                               : "configured reservation exhausted";
        SB_FATAL(
            "Scene/SceneEntityStore",
            "Commit without successful entity/body/capacity preflight. id=%u body_valid=%d reserved=%zu reason=%s",
            entity.sceneObjectId.value,
            body.IsValid() ? 1 : 0,
            m_records.capacity(),
            reason );
    }

    // Invariant: reservationReady makes this append allocation-free.
    m_records.emplace_back();
    SceneEntityRecord& record = m_records.back();
    record.sceneObjectId = entity.sceneObjectId;
    record.body = body;
    record.renderMaterial = entity.renderMaterial;
    record.asset = entity.asset;
    CopyBounded( record.displayName, sizeof( record.displayName ), entity.displayName );
}

void SceneEntityStore::UpdateBodyHandleAt( int index,
                                           Physics::PhysicsBodyHandle body,
                                           Physics::PhysicsSceneObjectId sceneObjectId )
{
    SceneEntityRecord& record = MutableAt( index );
    if ( !body.IsValid() || record.sceneObjectId.value != sceneObjectId.value )
    {
        SB_FATAL( "Scene/SceneEntityStore",
                  "Body refresh identity mismatch. row=%d entity_id=%u body_id=%u body_valid=%d",
                  index,
                  record.sceneObjectId.value,
                  sceneObjectId.value,
                  body.IsValid() ? 1 : 0 );
    }
    record.body = body;
}

bool SceneEntityStore::TrimToCount( int count )
{
    if ( count < 0 || count > Count() )
    {
        return false;
    }
    m_records.resize( static_cast<std::size_t>( count ) );
    return true;
}

int SceneEntityStore::Count() const
{
    return static_cast<int>( m_records.size() );
}

int SceneEntityStore::Capacity() const
{
    return m_capacity;
}

uint64_t SceneEntityStore::CapacityBytes() const
{
    return static_cast<uint64_t>( m_records.capacity() ) * sizeof( SceneEntityRecord );
}

const SceneEntityRecord& SceneEntityStore::At( int index ) const
{
    const SceneEntityRecord* record = TryGet( index );
    if ( !record )
    {
        SB_FATAL( "Scene/SceneEntityStore", "Scene entity index out of range. index=%d count=%d", index, Count() );
    }
    return *record;
}

SceneEntityRecord& SceneEntityStore::MutableAt( int index )
{
    SceneEntityRecord* record = TryGetMutable( index );
    if ( !record )
    {
        SB_FATAL( "Scene/SceneEntityStore", "Scene entity index out of range. index=%d count=%d", index, Count() );
    }
    return *record;
}

const SceneEntityRecord* SceneEntityStore::TryGet( int index ) const
{
    return index >= 0 && index < Count() ? &m_records[static_cast<std::size_t>( index )] : nullptr;
}

SceneEntityRecord* SceneEntityStore::TryGetMutable( int index )
{
    return index >= 0 && index < Count() ? &m_records[static_cast<std::size_t>( index )] : nullptr;
}

int SceneEntityStore::FindByDisplayName( const char* name ) const
{
    if ( !name || name[0] == '\0' )
    {
        return -1;
    }
    for ( int index = 0; index < Count(); ++index )
    {
        if ( std::strcmp( m_records[static_cast<std::size_t>( index )].displayName, name ) == 0 )
        {
            return index;
        }
    }
    return -1;
}

int SceneEntityStore::FindBySceneObjectId( Physics::PhysicsSceneObjectId sceneObjectId ) const
{
    if ( !sceneObjectId.IsValid() )
    {
        return -1;
    }
    for ( int index = 0; index < Count(); ++index )
    {
        if ( m_records[static_cast<std::size_t>( index )].sceneObjectId.value == sceneObjectId.value )
        {
            return index;
        }
    }
    return -1;
}
