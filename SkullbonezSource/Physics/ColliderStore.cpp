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
  - Collider records stay in GameModelCollection model order so body/collider
    handles share the same compatibility index.
  - Refresh snapshots collision metadata only; model pose and solver state
    remain owned elsewhere.

Related:
  - SkullbonezSource/Physics/ColliderStore.h
*/
#include "ColliderStore.h"
#include "PhysicsModelAccess.h"

#include <cstddef>

#include "../Core/Common.h"
#include "../GameObjects/GameModel.h"

using SkullbonezCore::GameObjects::GameModel;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderShapeKind;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::PhysicsColliderHandle;


ColliderStore::ColliderStore()
{
    m_colliders.reserve( MAX_GAME_MODELS );
    m_modelColliderHandles.reserve( MAX_GAME_MODELS );
}


void ColliderStore::Clear()
{
    m_colliders.clear();
    m_modelColliderHandles.clear();
}


void ColliderStore::Refresh( std::vector<GameModel>& models )
{
    // Invariant: compatibility handles mirror model indices until the physics
    // facade owns allocation. Do not sort or compact this store independently.
    m_colliders.resize( models.size() );
    m_modelColliderHandles.resize( models.size() );
    for ( std::size_t i = 0; i < models.size(); ++i )
    {
        GameModel& model = models[i];
        ColliderRecord& record = m_colliders[i];
        const uint32_t modelIndex = static_cast<uint32_t>( i );
        record.handle = MakeCompatibilityPhysicsColliderHandle( modelIndex );
        record.body = MakeCompatibilityPhysicsBodyHandle( modelIndex );
        record.legacyModelIndex = static_cast<int>( i );
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
        m_modelColliderHandles[i] = record.handle;
    }
}


void ColliderStore::Refresh( PhysicsModelAccess& modelAccess )
{
    // Invariant: compatibility handles mirror model indices until the physics
    // facade owns allocation. Do not sort or compact this store independently.
    auto models = modelAccess.Models();
    const int modelCount = models.Count();
    m_colliders.resize( static_cast<std::size_t>( modelCount ) );
    m_modelColliderHandles.resize( static_cast<std::size_t>( modelCount ) );
    for ( int i = 0; i < modelCount; ++i )
    {
        GameModel& model = models[static_cast<std::size_t>( i )];
        ColliderRecord& record = m_colliders[static_cast<std::size_t>( i )];
        const uint32_t modelIndex = static_cast<uint32_t>( i );
        record.handle = MakeCompatibilityPhysicsColliderHandle( modelIndex );
        record.body = MakeCompatibilityPhysicsBodyHandle( modelIndex );
        record.legacyModelIndex = i;
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

    return m_colliders[static_cast<std::size_t>( handle.index )].legacyModelIndex;
}


bool ColliderStore::Contains( PhysicsColliderHandle handle ) const
{
    if ( !handle.IsValid() || handle.generation != PHYSICS_COMPATIBILITY_HANDLE_GENERATION )
    {
        return false;
    }
    if ( handle.index >= m_colliders.size() )
    {
        return false;
    }

    return m_colliders[static_cast<std::size_t>( handle.index )].handle == handle;
}


const std::vector<ColliderRecord>& ColliderStore::Records() const
{
    return m_colliders;
}
