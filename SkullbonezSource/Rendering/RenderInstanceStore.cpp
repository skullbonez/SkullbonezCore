/*
File: SkullbonezSource/Rendering/RenderInstanceStore.cpp
Purpose:
  Builds model-order render instance snapshots from physics and presentation state.

Mental model:
  Refresh copies renderer-facing values after gameplay/physics have committed.
  Body pose and shape come from physics stores; material and contact flash alpha
  still come from GameModel presentation state. It does not allocate GPU
  resources; it records the CPU-side draw intent that a future render snapshot
  can consume.

Glossary:
  Render instance: One draw-facing object record with transform and material
    intent.
  Material intent: Engine-level material choice before a renderer maps it to
    shaders, textures, or descriptor rows.
  Replay body id: Stable per-scene id shared with physics/replay records.
  Contact highlight: Render-only feedback alpha copied from GameModel after
    gameplay/physics presentation state has advanced.

Invariants:
  - Records stay in GameModelCollection model order and compatibility handles
    mirror model indices.
  - Refresh snapshots CPU draw intent only; it does not create or destroy GPU
    resources.

Related:
  - SkullbonezSource/Rendering/RenderInstanceStore.h
*/
#include "RenderInstanceStore.h"

#include <cstddef>

#include "../Core/Common.h"
#include "../GameObjects/GameModel.h"
#include "../Maths/Matrix4.h"
#include "../Physics/ColliderStore.h"
#include "../Physics/PhysicsBodyStore.h"

using SkullbonezCore::GameObjects::GameModel;
using SkullbonezCore::Math::CollisionDetection::GetShapeModelMatrix;
using SkullbonezCore::Math::Transformation::Matrix4;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderShapeKind;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Rendering::RenderInstanceHandle;
using SkullbonezCore::Rendering::RenderInstanceRecord;
using SkullbonezCore::Rendering::RenderInstanceShapeKind;
using SkullbonezCore::Rendering::RenderInstanceStore;


namespace
{
Matrix4 BuildPhysicsModelMatrix( const PhysicsBodyRecord& body, const ColliderRecord& collider )
{
    const Matrix4 rotation = Matrix4::FromQuaternion( body.orientation );
    if ( collider.shapeKind == ColliderShapeKind::ConvexHull )
    {
        // Why: convex hull draw code transforms authored hull vertices directly.
        // Keep the legacy T * R body matrix here so moving renderers to this
        // snapshot does not add the collision-shape scale/offset a second time.
        return Matrix4::Translate( body.position ) * rotation;
    }
    return GetShapeModelMatrix( collider.shape, body.position, rotation );
}

RenderInstanceShapeKind ShapeKindFromCollider( ColliderShapeKind shapeKind )
{
    switch ( shapeKind )
    {
    case ColliderShapeKind::Sphere:
        return RenderInstanceShapeKind::Sphere;
    case ColliderShapeKind::Box:
        return RenderInstanceShapeKind::Box;
    case ColliderShapeKind::ConvexHull:
        return RenderInstanceShapeKind::ConvexHull;
    }
    return RenderInstanceShapeKind::Sphere;
}
} // namespace


RenderInstanceStore::RenderInstanceStore()
{
    m_instances.reserve( MAX_GAME_MODELS );
    m_modelInstanceHandles.reserve( MAX_GAME_MODELS );
}


void RenderInstanceStore::Clear()
{
    m_instances.clear();
    m_modelInstanceHandles.clear();
}


void RenderInstanceStore::Refresh( std::vector<GameModel>& models )
{
    Refresh( models.empty() ? nullptr : models.data(), static_cast<int>( models.size() ) );
}


void RenderInstanceStore::Refresh( GameModel* models, int modelCount )
{
    // Invariant: render instance handles intentionally mirror model slots until
    // a future renderer-facing allocation owner replaces compatibility ids.
    m_instances.resize( static_cast<std::size_t>( modelCount ) );
    m_modelInstanceHandles.resize( static_cast<std::size_t>( modelCount ) );
    for ( int i = 0; i < modelCount; ++i )
    {
        GameModel& model = models[i];
        RenderInstanceRecord& record = m_instances[static_cast<std::size_t>( i )];
        const uint32_t modelIndex = static_cast<uint32_t>( i );
        record.handle = MakeCompatibilityRenderInstanceHandle( modelIndex );
        record.replayBodyId = model.GetReplayBodyId();
        record.modelMatrix = model.GetModelMatrix();
        record.material = model.GetRenderMaterial();
        record.boundingRadius = model.GetBoundingRadius();
        record.shapeKind = model.IsBox() ? RenderInstanceShapeKind::Box
                                         : ( model.IsConvexHull() ? RenderInstanceShapeKind::ConvexHull
                                                                  : RenderInstanceShapeKind::Sphere );
        record.isFixed = model.IsFixed();
        record.fixedContactAlpha = model.GetFixedContactHighlightAlpha();
        record.audioContactAlpha = model.GetAudioContactHighlightAlpha();
        m_modelInstanceHandles[static_cast<std::size_t>( i )] = record.handle;
    }
}


void RenderInstanceStore::Refresh( std::vector<GameModel>& models,
                                   const PhysicsBodyStore& bodyStore,
                                   const ColliderStore& colliderStore )
{
    Refresh( models.empty() ? nullptr : models.data(), static_cast<int>( models.size() ), bodyStore, colliderStore );
}


void RenderInstanceStore::Refresh( GameModel* models,
                                   int modelCount,
                                   const PhysicsBodyStore& bodyStore,
                                   const ColliderStore& colliderStore )
{
    if ( bodyStore.Count() != modelCount || colliderStore.Count() != modelCount )
    {
        // Hazard: this is a defensive topology fallback only. PhysicsScene
        // refreshes stores before reaching this overload so the normal render
        // path reads physics-owned pose and shape state.
        Refresh( models, modelCount );
        return;
    }

    const std::vector<PhysicsBodyRecord>& bodies = bodyStore.Records();
    const std::vector<ColliderRecord>& colliders = colliderStore.Records();

    // Invariant: render instance handles intentionally mirror model slots until
    // a future renderer-facing allocation owner replaces compatibility ids.
    m_instances.resize( static_cast<std::size_t>( modelCount ) );
    m_modelInstanceHandles.resize( static_cast<std::size_t>( modelCount ) );
    for ( int i = 0; i < modelCount; ++i )
    {
        GameModel& model = models[i];
        const std::size_t index = static_cast<std::size_t>( i );
        const PhysicsBodyRecord& body = bodies[index];
        const ColliderRecord& collider = colliders[index];
        RenderInstanceRecord& record = m_instances[index];
        const uint32_t modelIndex = static_cast<uint32_t>( i );
        record.handle = MakeCompatibilityRenderInstanceHandle( modelIndex );
        record.replayBodyId = body.replayBodyId;
        record.modelMatrix = BuildPhysicsModelMatrix( body, collider );
        record.material = model.GetRenderMaterial();
        record.boundingRadius = collider.boundingRadius;
        record.shapeKind = ShapeKindFromCollider( collider.shapeKind );
        record.isFixed = body.isFixed;
        record.fixedContactAlpha = model.GetFixedContactHighlightAlpha();
        record.audioContactAlpha = model.GetAudioContactHighlightAlpha();
        m_modelInstanceHandles[index] = record.handle;
    }
}


void RenderInstanceStore::OverridePoseFromModel( int modelIndex, GameModel& model )
{
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( m_instances.size() ) )
    {
        return;
    }

    RenderInstanceRecord& record = m_instances[static_cast<std::size_t>( modelIndex )];
    if ( record.shapeKind == RenderInstanceShapeKind::ConvexHull )
    {
        const Matrix4 rotation = Matrix4::FromQuaternion( model.GetOrientation() );
        record.modelMatrix = Matrix4::Translate( model.GetPosition() ) * rotation;
    }
    else
    {
        record.modelMatrix = model.GetModelMatrix();
    }
    record.boundingRadius = model.GetBoundingRadius();
}


const RenderInstanceRecord* RenderInstanceStore::Data() const
{
    return m_instances.empty() ? nullptr : m_instances.data();
}


int RenderInstanceStore::Count() const
{
    return static_cast<int>( m_instances.size() );
}


bool RenderInstanceStore::Empty() const
{
    return m_instances.empty();
}


RenderInstanceHandle RenderInstanceStore::HandleForModelIndex( int modelIndex ) const
{
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( m_modelInstanceHandles.size() ) )
    {
        return RenderInstanceHandle{};
    }

    return m_modelInstanceHandles[static_cast<std::size_t>( modelIndex )];
}


int RenderInstanceStore::ModelIndexForHandle( RenderInstanceHandle handle ) const
{
    if ( !Contains( handle ) )
    {
        return -1;
    }

    return static_cast<int>( handle.index );
}


bool RenderInstanceStore::Contains( RenderInstanceHandle handle ) const
{
    if ( !handle.IsValid() || handle.generation != RENDER_INSTANCE_COMPATIBILITY_HANDLE_GENERATION )
    {
        return false;
    }
    if ( handle.index >= m_instances.size() )
    {
        return false;
    }

    return m_instances[static_cast<std::size_t>( handle.index )].handle == handle;
}


const std::vector<RenderInstanceRecord>& RenderInstanceStore::Records() const
{
    return m_instances;
}
