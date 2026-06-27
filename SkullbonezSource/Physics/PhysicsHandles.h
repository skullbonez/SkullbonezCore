/*
File: SkullbonezSource/Physics/PhysicsHandles.h
Purpose:
  Defines stable public handles for physics bodies, colliders, constraints, and scene correlation.

Mental model:
  Runtime, scene, tools, replay, rendering, and diagnostics should identify
  physics-owned objects by handles instead of owning solver arrays or
  GameModelCollection storage. Generations make stale handles explicit once the
  authoritative physics facade starts allocating and recycling ids.

Glossary:
  Handle: Index plus generation pair used as opaque identity for physics-owned
  storage.
  Generation: Version counter that makes stale recycled handles detectable.
  Scene object id: Stable scene/replay correlation id independent of storage.

Invariants:
  - Index/generation handles are identity only; they do not expose storage.
  - PhysicsSceneObjectId value 0 is reserved for "not assigned".

Related:
  - SkullbonezSource/Physics/PhysicsApi.h
  - Agentic/Plans/runtime-run-decomposition-plan.md
*/
#pragma once

#include <cstdint>

namespace SkullbonezCore
{
namespace Physics
{
inline constexpr uint32_t INVALID_PHYSICS_HANDLE_INDEX = 0xffffffffu;
inline constexpr uint32_t PHYSICS_COMPATIBILITY_HANDLE_GENERATION = 1u;

struct PhysicsBodyHandle
{
    uint32_t index = INVALID_PHYSICS_HANDLE_INDEX;
    uint32_t generation = 0;

    bool IsValid() const
    {
        return index != INVALID_PHYSICS_HANDLE_INDEX && generation != 0;
    }
};

struct PhysicsColliderHandle
{
    uint32_t index = INVALID_PHYSICS_HANDLE_INDEX;
    uint32_t generation = 0;

    bool IsValid() const
    {
        return index != INVALID_PHYSICS_HANDLE_INDEX && generation != 0;
    }
};

struct PhysicsConstraintHandle
{
    uint32_t index = INVALID_PHYSICS_HANDLE_INDEX;
    uint32_t generation = 0;

    bool IsValid() const
    {
        return index != INVALID_PHYSICS_HANDLE_INDEX && generation != 0;
    }
};

struct PhysicsSceneObjectId
{
    uint32_t value = 0;

    bool IsValid() const
    {
        return value != 0;
    }
};

inline PhysicsBodyHandle MakeCompatibilityPhysicsBodyHandle( uint32_t modelIndex )
{
    PhysicsBodyHandle handle;
    handle.index = modelIndex;
    handle.generation = PHYSICS_COMPATIBILITY_HANDLE_GENERATION;
    return handle;
}

inline PhysicsColliderHandle MakeCompatibilityPhysicsColliderHandle( uint32_t modelIndex )
{
    PhysicsColliderHandle handle;
    handle.index = modelIndex;
    handle.generation = PHYSICS_COMPATIBILITY_HANDLE_GENERATION;
    return handle;
}

inline PhysicsSceneObjectId MakePhysicsSceneObjectIdFromReplayBodyId( uint32_t replayBodyId )
{
    PhysicsSceneObjectId id;
    id.value = replayBodyId;
    return id;
}

inline bool operator==( const PhysicsBodyHandle& lhs, const PhysicsBodyHandle& rhs )
{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

inline bool operator!=( const PhysicsBodyHandle& lhs, const PhysicsBodyHandle& rhs )
{
    return !( lhs == rhs );
}

inline bool operator==( const PhysicsColliderHandle& lhs, const PhysicsColliderHandle& rhs )
{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

inline bool operator!=( const PhysicsColliderHandle& lhs, const PhysicsColliderHandle& rhs )
{
    return !( lhs == rhs );
}

inline bool operator==( const PhysicsConstraintHandle& lhs, const PhysicsConstraintHandle& rhs )
{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

inline bool operator!=( const PhysicsConstraintHandle& lhs, const PhysicsConstraintHandle& rhs )
{
    return !( lhs == rhs );
}

inline bool operator==( const PhysicsSceneObjectId& lhs, const PhysicsSceneObjectId& rhs )
{
    return lhs.value == rhs.value;
}

inline bool operator!=( const PhysicsSceneObjectId& lhs, const PhysicsSceneObjectId& rhs )
{
    return !( lhs == rhs );
}
} // namespace Physics
} // namespace SkullbonezCore
