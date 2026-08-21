/*
File: SkullbonezSource/Runtime/Scene/SceneEntityStore.cpp
Purpose:
  Implements allocation-free scene entity metadata storage.

Summary:
  Creation first validates identity, behavior-root topology, and capacity,
  commits physics owner rows, then publishes the fully linked entity record.
  Reserved storage prevents steady-runtime growth.


Invariants:
  - Preflight is mutation-free.
  - Commit receives the exact descriptor that passed preflight.
  - Vector capacity is established before population and cannot grow in commit.
  - Invalid direct indexing is an engine invariant failure.

Related:
  - SkullbonezSource/Runtime/Scene/SceneEntityStore.h
  - SkullbonezSource/Runtime/Scene/SceneController.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "SceneEntityStore.h"
#include "../../Core/SbDiagnosticStore.h"

#include "../../Core/FatalError.h"

#include <algorithm>
#include <cstring>

using namespace SkullbonezCore::Runtime;
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


void SceneEntityCreateDesc::SetRenderTint( float tintR, float tintG, float tintB, float colorOverride )
{
    renderMaterial = Rendering::MakeRenderMaterialFromLegacyTint( tintR, tintG, tintB, colorOverride );
}

void SceneEntityCreateDesc::SetRenderMaterial( const Rendering::RenderMaterial& material )
{
    renderMaterial = material;
}


void SceneEntityCreateDesc::SetAssetAffiliation( Physics::PhysicsSceneObjectId rootObjectId, const char* libraryToken,
                                                 const char* assetName, const char* instanceName, const char* partName,
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

void SceneEntityCreateDesc::SetBehaviorGroup( SceneBehaviorGroupKind kind, Physics::PhysicsSceneObjectId rootObjectId,
                                              int partIndex )
{
    behaviorGroup.kind = kind;
    behaviorGroup.rootObjectId = rootObjectId;
    behaviorGroup.partIndex = partIndex;
}

SceneEntityStore::SceneEntityStore( SkullbonezCore::Core::SbDiagnosticStore& diagnostics ) : m_diagnostics( diagnostics )
{
    // Phase: startup preallocation. The default capacity is reserved before
    // scene population so steady gameplay entity commits cannot touch the heap.
    m_records.reserve( static_cast<std::size_t>( m_capacity ) );
}

void SceneEntityStore::ConfigureCapacity( int capacity )
{
    if ( capacity < 1 || capacity > SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS || Count() > capacity )
    {
        SB_FATAL( "Scene/SceneEntityStore", "Invalid scene entity capacity. requested=%d count=%d max=%d", capacity, Count(),
                  SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
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

SkullbonezCore::Core::SbResult SceneEntityStore::PreflightAppend( const SceneEntityCreateDesc& entity ) const
{
    if ( Count() >= m_capacity )
    {
        return m_diagnostics.Failure( "Scene/SceneEntityStore", "Scene entity capacity exhausted. count=%d capacity=%d",
                                      Count(), m_capacity );
    }

    if ( !entity.sceneObjectId.IsValid() )
    {
        return m_diagnostics.Failure( "Scene/SceneEntityStore", "Cannot append a scene entity with id 0." );
    }

    if ( FindBySceneObjectId( entity.sceneObjectId ) >= 0 )
    {
        return m_diagnostics.Failure( "Scene/SceneEntityStore", "Duplicate scene entity id %u.",
                                      entity.sceneObjectId.value );
    }

    // Invariant: stable group roots are validated before downstream physics or
    // render rows mutate. A root may name this part-zero entity; later members
    // must reference an already committed compatible root.
    const SceneBehaviorGroup& group = entity.behaviorGroup;

    if ( group.kind != SceneBehaviorGroupKind::None )
    {
        if ( !group.rootObjectId.IsValid() || group.partIndex < 0 )
        {
            return m_diagnostics.Failure( "Scene/SceneEntityStore", "Behavior group requires a valid root id and part." );
        }

        const int rootIndex = FindBySceneObjectId( group.rootObjectId );

        if ( group.rootObjectId.value == entity.sceneObjectId.value )
        {
            if ( group.partIndex != 0 )
            {
                return m_diagnostics.Failure( "Scene/SceneEntityStore", "Behavior group root must be part zero." );
            }
        }
        else if ( rootIndex < 0 )
        {
            return m_diagnostics.Failure( "Scene/SceneEntityStore", "Behavior group root id %u has not been created.",
                                          group.rootObjectId.value );
        }
        else
        {
            const SceneBehaviorGroup& rootGroup = At( rootIndex ).behaviorGroup;

            if ( rootGroup.kind != group.kind || rootGroup.rootObjectId.value != group.rootObjectId.value ||
                 rootGroup.partIndex != 0 )
            {
                return m_diagnostics.Failure( "Scene/SceneEntityStore",
                                              "Behavior group root id %u has incompatible metadata.",
                                              group.rootObjectId.value );
            }
        }
    }

    return SkullbonezCore::Core::SbResult::Success();
}

void SceneEntityStore::CommitAppend( const SceneEntityCreateDesc& entity, Physics::PhysicsBodyHandle body )
{
    const SkullbonezCore::Core::SbResult preflight = PreflightAppend( entity );
    const bool reservationReady = m_records.size() < m_records.capacity();

    if ( !preflight.Ok() || !body.IsValid() || !reservationReady )
    {
        const char* reason = !preflight.Ok()   ? preflight.ErrorMessage()
                             : !body.IsValid() ? "invalid body"
                                               : "configured reservation exhausted";

        SB_FATAL( "Scene/SceneEntityStore",
                  "Commit without successful entity/body/capacity preflight. id=%u body_valid=%d reserved=%zu "
                  "reason=%s",
                  entity.sceneObjectId.value, body.IsValid() ? 1 : 0, m_records.capacity(), reason );
    }

    // Precondition: reaching this point proves the exact entity, body, and
    // capacity checks succeeded for this commit.
    // Runtime allocation policy: the retained reservation makes this append
    // allocation-free.
    m_records.emplace_back();
    SceneEntityRecord& record = m_records.back();
    record.sceneObjectId = entity.sceneObjectId;
    record.body = body;
    record.renderMaterial = entity.renderMaterial;
    record.asset = entity.asset;
    record.behaviorGroup = entity.behaviorGroup;
    CopyBounded( record.displayName, sizeof( record.displayName ), entity.displayName );
    record.editorVisible = entity.editorVisible;
    record.editorLocked = entity.editorLocked;
}

void SceneEntityStore::UpdateBodyHandleAt( int index, Physics::PhysicsBodyHandle body,
                                           Physics::PhysicsSceneObjectId sceneObjectId )
{
    SceneEntityRecord& record = MutableAt( index );

    if ( !body.IsValid() || record.sceneObjectId.value != sceneObjectId.value )
    {
        SB_FATAL( "Scene/SceneEntityStore", "Body refresh identity mismatch. row=%d entity_id=%u body_id=%u body_valid=%d",
                  index, record.sceneObjectId.value, sceneObjectId.value, body.IsValid() ? 1 : 0 );
    }

    record.body = body;
}


bool SceneEntityStore::DestroyAtSwapLast( int index )
{
    if ( index < 0 || index >= Count() )
    {
        return false;
    }

    const std::size_t row = static_cast<std::size_t>( index );

    // Invariant: scene rows share dense order with physics and render rows. The
    // coordinating collection performs the same swap-last operation everywhere.
    if ( row + 1u != m_records.size() )
    {
        m_records[row] = std::move( m_records.back() );
    }

    m_records.pop_back();
    return true;
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


const SceneBehaviorGroup& SceneEntityStore::BehaviorGroupAt( int modelIndex ) const
{
    return At( modelIndex ).behaviorGroup;
}


int SceneEntityStore::ResolveBehaviorGroupRootModelIndex( const SceneBehaviorGroup& group ) const
{
    if ( group.kind == SceneBehaviorGroupKind::None )
    {
        return -1;
    }

    // Why: stable scene identity owns group membership. Dense roots are derived
    // only for synchronous physics/editor consumers and are never cached.
    const int rootIndex = FindBySceneObjectId( group.rootObjectId );

    if ( rootIndex < 0 )
    {
        SB_FATAL( "Scene/SceneEntityStore", "Behavior group root is missing. root_id=%u kind=%u", group.rootObjectId.value,
                  static_cast<unsigned int>( group.kind ) );
    }

    return rootIndex;
}


SceneBehaviorGroupKind SceneEntityStore::GroupKindAt( int modelIndex ) const
{
    return BehaviorGroupAt( modelIndex ).kind;
}


SkullbonezCore::Physics::PhysicsSceneObjectId SceneEntityStore::GroupRootObjectIdAt( int modelIndex ) const
{
    return BehaviorGroupAt( modelIndex ).rootObjectId;
}


bool SceneEntityStore::IsSimpleRagdollPart( int modelIndex ) const
{
    return GroupKindAt( modelIndex ) == SceneBehaviorGroupKind::SimpleRagdoll;
}


bool SceneEntityStore::TryFindSimpleRagdollPart( int selectedModelIndex, int partIndex, int& outModelIndex ) const
{
    outModelIndex = -1;

    if ( !IsSimpleRagdollPart( selectedModelIndex ) )
    {
        return false;
    }

    const SkullbonezCore::Physics::PhysicsSceneObjectId rootObjectId = GroupRootObjectIdAt( selectedModelIndex );

    for ( int i = 0; i < Count(); ++i )
    {
        const SceneBehaviorGroup& group = BehaviorGroupAt( i );

        if ( group.kind == SceneBehaviorGroupKind::SimpleRagdoll && group.rootObjectId.value == rootObjectId.value &&
             group.partIndex == partIndex )
        {
            outModelIndex = i;
            return true;
        }
    }

    return false;
}


int SceneEntityStore::GatherGroupMemberIndices( int selectedModelIndex, int* outIndices, int maxIndices ) const
{
    if ( outIndices && maxIndices > 0 )
    {
        for ( int i = 0; i < maxIndices; ++i )
        {
            outIndices[i] = -1;
        }
    }

    if ( !outIndices || maxIndices <= 0 || selectedModelIndex < 0 || selectedModelIndex >= Count() )
    {
        return 0;
    }

    const SceneBehaviorGroup& selectedGroup = BehaviorGroupAt( selectedModelIndex );

    if ( selectedGroup.kind != SceneBehaviorGroupKind::SimpleRagdoll )
    {
        outIndices[0] = selectedModelIndex;
        return 1;
    }

    (void)ResolveBehaviorGroupRootModelIndex( selectedGroup );
    int count = 0;

    for ( int i = 0; i < Count() && count < maxIndices; ++i )
    {
        const SceneBehaviorGroup& group = BehaviorGroupAt( i );

        if ( group.kind == selectedGroup.kind && group.rootObjectId.value == selectedGroup.rootObjectId.value )
        {
            outIndices[count++] = i;
        }
    }

    if ( count <= 0 )
    {
        outIndices[0] = selectedModelIndex;
        return 1;
    }

    return count;
}
