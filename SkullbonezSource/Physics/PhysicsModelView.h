/*
File: SkullbonezSource/Physics/PhysicsModelView.h
Purpose:
  Defines the compatibility model view used while physics storage moves out of GameModelCollection.

Mental model:
  Physics still needs legacy model data during the migration, but it should not
  depend on the collection type that owns scene/render/editor policy. This view
  is a narrow borrowed boundary over model order, stream cache invalidation,
  fixed-tree release callbacks, and debug trace emission.

Glossary:
  Compatibility view: Borrowed adapter over legacy storage while authority
    moves into body, collider, render, and entity stores.
  Model order: Current deterministic body iteration order, still matching the
    legacy GameModel vector.
  Callback: Function pointer back to the compatibility facade for operations
    that are not physics-store-owned yet.

Invariants:
  - The view never owns models, caches, or diagnostics sinks.
  - Model order is unchanged by constructing or passing this view.
  - Callback targets must outlive the physics step that receives the view.

Related:
  - SkullbonezSource/Physics/PhysicsEngine.h
  - SkullbonezSource/Physics/PhysicsWorld.h
  - SkullbonezSource/GameObjects/GameModelCollection.cpp
  - Agentic/Plans/engine-evaluation-fix-02-physics-data-boundary-plan.md
*/
#pragma once

#include <vector>

#include "../GameObjects/GameModelStreams.h"
#include "../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace GameObjects
{
class SkullScope;
}

namespace Physics
{
class PhysicsModelView
{
  public:
    using ReleaseAttachedFixedTreePartsFn = void ( * )( void* user,
                                                        int sourceIndex,
                                                        const Math::Vector::Vector3& seedLinearVelocity,
                                                        const Math::Vector::Vector3& seedAngularVelocity );
    using EmitSkullScopeFrameFn = void ( * )( void* user, GameObjects::SkullScope& scope, float dt );

    PhysicsModelView( std::vector<GameObjects::GameModel>& models,
                      GameObjects::GameModelSoACache& cache,
                      void* callbackUser,
                      ReleaseAttachedFixedTreePartsFn releaseAttachedFixedTreeParts,
                      EmitSkullScopeFrameFn emitSkullScopeFrame )
        : m_models( models ), m_cache( cache ), m_callbackUser( callbackUser ),
          m_releaseAttachedFixedTreeParts( releaseAttachedFixedTreeParts ), m_emitSkullScopeFrame( emitSkullScopeFrame )
    {
    }

    std::vector<GameObjects::GameModel>& Models()
    {
        return m_models;
    }

    const std::vector<GameObjects::GameModel>& Models() const
    {
        return m_models;
    }

    int Count() const
    {
        return static_cast<int>( m_models.size() );
    }

    GameObjects::GameModelBodyStream GetBodyStream()
    {
        return GameObjects::GameModelStreamProvider::GetBodyStream( m_cache, m_models );
    }

    void InvalidatePhysicsStreams()
    {
        // Concept: invalidation is still compatibility-owned. Physics can mark
        // the borrowed cache stale without knowing about GameModelCollection.
        m_cache.Invalidate();
    }

    void ReleaseAttachedFixedTreeParts( int sourceIndex,
                                        const Math::Vector::Vector3& seedLinearVelocity,
                                        const Math::Vector::Vector3& seedAngularVelocity )
    {
        if ( m_releaseAttachedFixedTreeParts )
        {
            m_releaseAttachedFixedTreeParts( m_callbackUser, sourceIndex, seedLinearVelocity, seedAngularVelocity );
        }
    }

    void EmitSkullScopeFrame( GameObjects::SkullScope& scope, float dt )
    {
        if ( m_emitSkullScopeFrame )
        {
            m_emitSkullScopeFrame( m_callbackUser, scope, dt );
        }
    }

  private:
    std::vector<GameObjects::GameModel>& m_models;
    GameObjects::GameModelSoACache& m_cache;
    void* m_callbackUser = nullptr;
    ReleaseAttachedFixedTreePartsFn m_releaseAttachedFixedTreeParts = nullptr;
    EmitSkullScopeFrameFn m_emitSkullScopeFrame = nullptr;
};
} // namespace Physics
} // namespace SkullbonezCore
