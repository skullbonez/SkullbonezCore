/*
File: SkullbonezSource/Runtime/App/ReplayValidation.Internal.h
Purpose:
  Shares the narrow replay-restore vocabulary used by product restore and the
  Debug-only replay probe implementation.

Summary:
  ReplayValidation.cpp owns production event replay and restore. The Debug
  probe TU reuses two authoritative store lookups and the same serialized flag
  values without reaching back into private translation-unit state.

Glossary:
  Restore flag: Stable bit in a recorded replay event that selects one authored
    mutation such as translation, rotation, or scale.
  Topology lookup: Resolution from durable replay/model identity into the live
    body and collider store rows owned by the scene.

Invariants:
  - These constants remain byte-identical to the recorded event schema.
  - The lookup helpers borrow store rows; callers never retain the pointers
    across scene mutation.
  - No probe entry point or mutable probe state crosses this header.

Related:
  - SkullbonezSource/Runtime/App/ReplayValidation.cpp
  - SkullbonezSource/Runtime/App/ReplayValidation.Probes.cpp
*/
#pragma once

#include <cstdint>

namespace SkullbonezCore::Physics
{
struct ColliderRecord;
struct PhysicsBodyRecord;
struct PhysicsColliderHandle;
} // namespace SkullbonezCore::Physics

namespace SkullbonezCore::Runtime
{
class SceneWorld;

namespace ReplayValidationInternal
{
inline constexpr uint32_t REPLAY_WORLD_OVERRIDE_GRAVITY_CHANGED = 1u;
inline constexpr uint32_t REPLAY_WORLD_OVERRIDE_FLUID_HEIGHT_CHANGED = 2u;
inline constexpr uint32_t REPLAY_WORLD_OVERRIDE_FLUID_DENSITY_CHANGED = 4u;
inline constexpr uint32_t REPLAY_LAUNCHER_FIRE_PROJECTILE = 1u;
inline constexpr uint32_t REPLAY_EDITOR_PLACE_FIXED = 1u;
inline constexpr uint32_t REPLAY_EDITOR_PLACE_TERRAIN_ALIGN = 2u;
inline constexpr uint32_t REPLAY_EDITOR_TRANSFORM_TRANSLATE = 1u;
inline constexpr uint32_t REPLAY_EDITOR_TRANSFORM_ROTATE = 2u;
inline constexpr uint32_t REPLAY_EDITOR_TRANSFORM_SCALE = 4u;
inline constexpr uint32_t REPLAY_EDITOR_TRANSFORM_SUPPORTED = REPLAY_EDITOR_TRANSFORM_TRANSLATE |
                                                              REPLAY_EDITOR_TRANSFORM_ROTATE | REPLAY_EDITOR_TRANSFORM_SCALE;

const Physics::PhysicsBodyRecord* TryGetReplayProbeBodyRecord( const SceneWorld& world, int modelIndex );

const Physics::ColliderRecord* TryGetEditorTransformColliderRecord( const SceneWorld& world,
                                                                    Physics::PhysicsColliderHandle colliderHandle,
                                                                    int modelIndex,
                                                                    Physics::PhysicsSceneObjectId sceneObjectId );
} // namespace ReplayValidationInternal
} // namespace SkullbonezCore::Runtime
