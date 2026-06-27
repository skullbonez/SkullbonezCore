/*
File: SkullbonezSource/Physics/PhysicsModelAccess.h
Purpose:
  Defines the temporary compatibility access contract between physics and legacy GameModel storage.

Mental model:
  Physics is migrating toward authoritative body, collider, and render stores,
  but several solver paths still need legacy GameModel behavior. This interface
  names that dependency without constructing a per-call view object or exposing
  the raw model vector to physics code. Hot loops borrow pointer/count ranges so
  they can keep direct indexed iteration while the compatibility surface shrinks.

Glossary:
  Compatibility access: Narrow borrowed interface over legacy GameModel state
    while authoritative stores take over body, collider, and render ownership.
  Body stream: SoA-backed read-only body data used by hot physics loops.
  SoA (Structure of Arrays): Cache layout that stores each field in a separate
    contiguous array for faster iteration.
  Model range: Non-owning pointer/count view over the current deterministic
    compatibility model order.
  SkullScope: Queryable physics diagnostics trace workflow.

Invariants:
  - Implementations own the underlying model storage and SoA cache.
  - Callers must not keep model ranges or references after the physics operation
    that requested them.
  - Mutations performed through compatibility model ranges must explicitly call
    InvalidatePhysicsStreams() before a later stream read can observe stale SoA data.

Related:
  - SkullbonezSource/GameObjects/GameModelCollection.h
  - SkullbonezSource/Physics/PhysicsScene.h
  - Agentic/Plans/physics-game-model-authority-plan.md
*/
#pragma once

#include <cstddef>

#include "../Maths/Vector3.h"
#include "../GameObjects/GameModelSoACache.h"

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModel;
class SkullScope;
} // namespace GameObjects

namespace Physics
{
class PhysicsModelMutableRange
{
  public:
    PhysicsModelMutableRange() = default;
    PhysicsModelMutableRange( GameObjects::GameModel* models, int count ) : m_models( models ), m_count( count )
    {
    }

    std::size_t size() const
    {
        return static_cast<std::size_t>( m_count );
    }

    int Count() const
    {
        return m_count;
    }

    bool empty() const
    {
        return m_count == 0;
    }

    GameObjects::GameModel& operator[]( std::size_t index ) const
    {
        return m_models[index];
    }

    GameObjects::GameModel* data() const
    {
        return m_models;
    }

  private:
    GameObjects::GameModel* m_models = nullptr;
    int m_count = 0;
};

class PhysicsModelConstRange
{
  public:
    PhysicsModelConstRange() = default;
    PhysicsModelConstRange( const GameObjects::GameModel* models, int count ) : m_models( models ), m_count( count )
    {
    }

    std::size_t size() const
    {
        return static_cast<std::size_t>( m_count );
    }

    int Count() const
    {
        return m_count;
    }

    bool empty() const
    {
        return m_count == 0;
    }

    const GameObjects::GameModel& operator[]( std::size_t index ) const
    {
        return m_models[index];
    }

    const GameObjects::GameModel* data() const
    {
        return m_models;
    }

  private:
    const GameObjects::GameModel* m_models = nullptr;
    int m_count = 0;
};

class PhysicsModelAccess
{
  public:
    virtual ~PhysicsModelAccess() = default;

    virtual int ModelCount() const = 0;
    virtual GameObjects::GameModel* MutableModelData() = 0;
    virtual const GameObjects::GameModel* ModelData() const = 0;
    virtual GameObjects::GameModelBodyStream GetPhysicsBodyStream() = 0;
    virtual void InvalidatePhysicsStreams() = 0;
    virtual void ReleaseAttachedFixedTreeParts( int sourceIndex,
                                                const Math::Vector::Vector3& seedLinearVelocity,
                                                const Math::Vector::Vector3& seedAngularVelocity ) = 0;
    virtual void EmitSkullScopeFrame( GameObjects::SkullScope& scope, float dt ) = 0;

    PhysicsModelMutableRange Models()
    {
        return PhysicsModelMutableRange( MutableModelData(), ModelCount() );
    }

    PhysicsModelConstRange Models() const
    {
        return PhysicsModelConstRange( ModelData(), ModelCount() );
    }

    std::size_t size() const
    {
        return static_cast<std::size_t>( ModelCount() );
    }

    GameObjects::GameModel& operator[]( std::size_t index )
    {
        return MutableModelData()[index];
    }

    const GameObjects::GameModel& operator[]( std::size_t index ) const
    {
        return ModelData()[index];
    }

    int Count() const
    {
        return ModelCount();
    }

    GameObjects::GameModelBodyStream GetBodyStream()
    {
        return GetPhysicsBodyStream();
    }
};
} // namespace Physics
} // namespace SkullbonezCore
