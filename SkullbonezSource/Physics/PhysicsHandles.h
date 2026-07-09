/*
File: SkullbonezSource/Physics/PhysicsHandles.h
Purpose:
  Defines stable public handles and typed public-boundary counts for physics
  bodies, colliders, constraints, and scene correlation.

Mental model:
  Runtime, scene, tools, replay, rendering, and diagnostics should identify
  physics-owned objects by handles instead of owning solver arrays or collection
  storage. Generations make stale handles explicit once the authoritative
  physics facade starts allocating and recycling ids.

Glossary:
  Handle: Index plus generation pair used as opaque identity for physics-owned
  storage.
  Generation: Version counter that makes stale recycled handles detectable.
  Dense row: Compact store array index used by hot simulation scans.
  Boundary count: Public count used to validate topology or view size; it is not
    object identity and must not pick an individual store row.
  Model row hint: Cached dense-row guess paired with stable identity to avoid
    scans when the row has not moved.
  Scene object id: Stable scene/replay correlation id independent of storage.

Invariants:
  - Index/generation handles are identity only; they do not expose storage.
  - Count wrappers describe topology or view size only; they never identify a
    specific body, collider, or authoring row.
  - ModelRowHint is never identity; stale hints must be repaired by store resolvers.
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
inline constexpr uint32_t PHYSICS_HANDLE_INITIAL_GENERATION = 1u;

// Concept: typed public counts keep row authority explicit at API boundaries.
// A count may validate topology, but only handles or scene ids identify objects.
struct PhysicsBodyCount
{
    uint32_t value = 0;
};

struct PhysicsColliderCount
{
    uint32_t value = 0;
};

struct PhysicsAuthoredBodyCount
{
    uint32_t value = 0;
};

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

// Concept: a ModelRowHint is a cached dense-row guess, never identity.
// Persistent state stores PhysicsBodyHandle, ReplayBodyId, or scene ids; the
// hint only accelerates resolver fast paths and may be stale after compaction.
struct ModelRowHint
{
    int value = -1;

    bool IsValid() const
    {
        return value >= 0;
    }
};

// Why: owner edges still receive signed scene or replay counts from legacy
// call chains. Converting at the edge keeps the public physics API typed while
// the caller remains responsible for rejecting impossible negative counts.
inline PhysicsBodyCount MakePhysicsBodyCountFromNonNegativeInt( int value )
{
    PhysicsBodyCount count;
    count.value = value > 0 ? static_cast<uint32_t>( value ) : 0u;
    return count;
}

inline PhysicsColliderCount MakePhysicsColliderCountFromNonNegativeInt( int value )
{
    PhysicsColliderCount count;
    count.value = value > 0 ? static_cast<uint32_t>( value ) : 0u;
    return count;
}

inline PhysicsAuthoredBodyCount MakePhysicsAuthoredBodyCountFromNonNegativeInt( int value )
{
    PhysicsAuthoredBodyCount count;
    count.value = value > 0 ? static_cast<uint32_t>( value ) : 0u;
    return count;
}

inline ModelRowHint MakeModelRowHint( int value )
{
    ModelRowHint hint;
    hint.value = value;
    return hint;
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
