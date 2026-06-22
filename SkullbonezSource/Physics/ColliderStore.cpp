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

Related:
  - SkullbonezSource/Physics/ColliderStore.h
*/
#include "ColliderStore.h"

#include <cstddef>

#include "../Core/Common.h"
#include "../GameObjects/GameModel.h"

using SkullbonezCore::GameObjects::GameModel;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderShapeKind;
using SkullbonezCore::Physics::ColliderStore;


ColliderStore::ColliderStore()
{
    m_colliders.reserve( MAX_GAME_MODELS );
}


void ColliderStore::Clear()
{
    m_colliders.clear();
}


void ColliderStore::Refresh( std::vector<GameModel>& models )
{
    m_colliders.resize( models.size() );
    for ( std::size_t i = 0; i < models.size(); ++i )
    {
        GameModel& model = models[i];
        ColliderRecord& record = m_colliders[i];
        record.replayBodyId = model.GetReplayBodyId();
        record.shape = model.GetCollisionShape();
        record.boundingRadius = model.GetBoundingRadius();
        record.restitution = model.GetCoefficientRestitution();
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


const std::vector<ColliderRecord>& ColliderStore::Records() const
{
    return m_colliders;
}
